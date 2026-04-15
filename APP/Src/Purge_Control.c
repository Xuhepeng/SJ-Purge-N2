#include "Purge_Control.h"

#include <string.h>

#include "main.h"
#include "My_ADC_ReadData.h"
#include "Modbus_o2.h"
#include "Purge_ConfigStore.h"
#include "SFC.h"
#include "SHT85.h"

/*
 * 故障码按位定义。
 * 这样主机侧可以按位判断是 O2 问题、流量问题还是压力问题。
 */
#define PURGE_CTRL_FAULT_SENSOR_SAMPLE       1UL   /* 传感器采样流程异常，通常作为默认兜底故障 */
#define PURGE_CTRL_FAULT_O2_INVALID          2UL   /* 氧气浓度数据无效，超过允许时间没有成功更新 */
#define PURGE_CTRL_FAULT_FLOW_INVALID        4UL   /* SFC 流量反馈无效，超过允许时间没有成功更新 */
#define PURGE_CTRL_FAULT_TEMP_HUMI_INVALID   8UL   /* 温湿度数据无效，超过允许时间没有成功更新 */
#define PURGE_CTRL_FAULT_INLET_OVERPRESSURE  16UL  /* 入口压力过高，超过 max_inlet_pressure_bar */
#define PURGE_CTRL_FAULT_OUTLET_LOW_PRESSURE 32UL  /* 出口压力过低，低于 min_outlet_pressure_bar */
#define PURGE_CTRL_FAULT_O2_TOO_HIGH         64UL  /* RUN 阶段氧气浓度过高，超过 run_fault_o2_percent */

/* 全局控制对象 */
#define PURGE_CTRL_FAULT_RUN_QUALITY_TIMEOUT 256UL /* RUN 阶段补偿超时后，O2/湿度仍未回到设定范围内 */

PurgeControl_t g_purge_ctrl;

HAL_StatusTypeDef status;
static float s_run_command_flow_lpm = 0.0f;
static float s_run_flow_margin_lpm = 1.0f;
static float s_run_adjust_step_lpm = 2.0f;
static uint32_t s_run_adjust_timeout_ms = 5000U;
static uint8_t s_run_adjust_active = 0U;
static uint32_t s_run_adjust_start_tick = 0U;

/* 内部函数声明 */
static void PurgeControl_LoadDefaultConfig(void);
static void PurgeControl_ReadSensors(uint32_t now_ms);
static void PurgeControl_UpdateFaults(uint32_t now_ms);
static void PurgeControl_UpdateStateMachine(uint32_t now_ms);
static void PurgeControl_SetState(PurgeCtrl_State_t next_state, uint32_t now_ms);
static void PurgeControl_ClearCommands(void);
static uint8_t PurgeControl_IsTimeout(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms);
static float PurgeControl_CalcRunCommandFlow(uint32_t now_ms);
static void PurgeControl_OutputEvacuate(void);
static void PurgeControl_OutputFill(float flow_lpm);
static void PurgeControl_OutputRun(float flow_lpm);
static void PurgeControl_SetVacuumRelay(uint8_t on);
static void PurgeControl_SetVacuumGenerator(uint8_t on);
static void PurgeControl_SetInlet1(uint8_t on);
static void PurgeControl_SetInlet2(uint8_t on);
static void PurgeControl_SetAirOutlet(uint8_t on);
static void PurgeControl_SetPodRelay(uint8_t on);

/*
 * 控制层初始化。
 * 做三件事：
 * 1. 清空运行时结构体
 * 2. 装载默认工艺参数
 * 3. 把所有输出先拉到安全态
 */
void PurgeControl_Init(void)
{
    memset(&g_purge_ctrl, 0, sizeof(g_purge_ctrl));

    PurgeControl_LoadDefaultConfig();
    (void)PurgeConfigStore_Load();
    SHT85_Init();
    PurgeControl_ApplySafeOutputs();

    /* 初始化 RUN 阶段动态补偿状态。 */
    s_run_command_flow_lpm = g_purge_ctrl.run_flow_lpm;
    s_run_flow_margin_lpm = 1.0f;
    s_run_adjust_step_lpm = 2.0f;
    s_run_adjust_timeout_ms = 5000U;
    s_run_adjust_active = 0U;
    s_run_adjust_start_tick = 0U;

    g_purge_ctrl.state = PURGE_CTRL_STATE_INIT;
    g_purge_ctrl.initialized = 1U;
}

uint8_t PurgeControl_SaveConfig(void)
{
    return PurgeConfigStore_Save();
}

/*
 * 主循环周期调用入口。
 * 这里故意分成两拍：
 * 1. 到采样周期就读一轮传感器
 * 2. 到控制周期就跑一轮状态机
 *
 * 这样后面迁移到 RTOS 时也比较容易拆成独立任务。
 */
void PurgeControl_Process(uint32_t now_ms)
{
    if (g_purge_ctrl.initialized == 0U)
    {
        return;
    }

    if ((now_ms - g_purge_ctrl.last_sensor_tick) >= g_purge_ctrl.sensor_period_ms)
    {
        g_purge_ctrl.last_sensor_tick = now_ms;
        PurgeControl_ReadSensors(now_ms);
        PurgeControl_UpdateFaults(now_ms);
    }

    if ((now_ms - g_purge_ctrl.last_control_tick) < g_purge_ctrl.control_period_ms)
    {
        return;
    }
    HAL_Delay(20); //延时20ms，保证稳定性
    g_purge_ctrl.last_control_tick = now_ms;
    PurgeControl_UpdateStateMachine(now_ms);
    PurgeControl_ClearCommands();
}

/* 主机发 START 后，只是先置位命令标志，真正切状态在 Process 中统一完成 */
void PurgeControl_Start(void)
{
    g_purge_ctrl.cmd_start = 1U;
}

void PurgeControl_PodCavity(void)
{
    g_purge_ctrl.cavity = POD;
}

void PurgeControl_MicroCavity(void)
{
    g_purge_ctrl.cavity = Microenvironment;
}

/*
 * STOP 需要同步生效。
 * 否则通信层已经回复 HCA OK，但控制状态还没切回 STANDBY，
 * 紧跟着来的下一条 START_PURGE 会因为看到旧状态而返回 HCA BUSY。
 */
void PurgeControl_Stop(void)
{
    PurgeControl_EnterStandby();
}

/* 强制回到 INIT，供 HCS HOME 使用 */
void PurgeControl_Home(void)
{
    g_purge_ctrl.fault_code = 0UL;
    PurgeControl_ApplySafeOutputs();
    PurgeControl_ClearCommands();
    g_purge_ctrl.state = PURGE_CTRL_STATE_INIT;
    g_purge_ctrl.state_enter_tick = HAL_GetTick();
}

/* 主机发 RESET 后，只是先置位复位命令标志 */
void PurgeControl_ResetFault(void)
{
    g_purge_ctrl.cmd_reset_fault = 1U;
}

/* 强制退回待机态，并把输出回到安全状态 */
void PurgeControl_EnterStandby(void)
{
    g_purge_ctrl.state = PURGE_CTRL_STATE_STANDBY;
    g_purge_ctrl.state_enter_tick = 0U;
    PurgeControl_ApplySafeOutputs();
    PurgeControl_ClearCommands();
}

/*
 * 下发安全输出。
 * 只要系统初始化、待机或故障，都应该优先走这里。
 */
void PurgeControl_ApplySafeOutputs(void)
{
    PurgeControl_SetVacuumRelay(0U);
    PurgeControl_SetVacuumGenerator(0U);
    PurgeControl_SetInlet1(0U);
    //PurgeControl_SetInlet2(0U);
    PurgeControl_SetAirOutlet(0U);
    PurgeControl_SetPodRelay(0U);

    SFC_SetFlowValue(0.0f);//关闭流量控制
}

/* 状态转字符串，方便主机查询和调试打印 */
const char *PurgeControl_GetStateName(PurgeCtrl_State_t state)
{
    switch (state)
    {
        case PURGE_CTRL_STATE_INIT: return "INIT";
        case PURGE_CTRL_STATE_SELF_CHECK: return "SELF_CHECK";
        case PURGE_CTRL_STATE_STANDBY: return "STANDBY";
        case PURGE_CTRL_STATE_FILL: return "FILL";
        case PURGE_CTRL_STATE_STABILIZE: return "STABILIZE";
        case PURGE_CTRL_STATE_RUN: return "RUN";
        case PURGE_CTRL_STATE_FAULT: return "FAULT";
        default: return "UNKNOWN";
    }
}

/*
 * 默认参数。
 * 这些值现在更像首版联调用的工艺默认值，后续可以根据现场再微调。
 *
 * 下面三组 timeout 很关键：
 * 它们不是采样周期，而是“允许多久没更新还算有效”的容错窗口。
 */
static void PurgeControl_LoadDefaultConfig(void)
{
    g_purge_ctrl.fill_flow_lpm = 80.0f;       //充气置换阶段设定流量值
    g_purge_ctrl.run_flow_lpm = 20.0f;        //稳定和运行阶段设定流量值
    g_purge_ctrl.run_enter_o2_percent = 5.0f; //允许从稳定进入运行的 O2 条件：低于该值才允许进入 RUN
    //g_purge_ctrl.run_fault_o2_percent = 8.0f;  //氧气浓度超过该值则认为异常
    g_purge_ctrl.run_exit_humidity_percent = 10.0f; //允许从稳定进入运行的湿度条件：低于该值才允许进入 RUN
    g_purge_ctrl.max_inlet_pressure_bar = 7.0f;     //进气压力上限，单位 Bar
    g_purge_ctrl.min_inlet_pressure_bar = 5.0f;     //进气压力下限，单位 Bar
    g_purge_ctrl.max_outlet_pressure_bar = -0.03f;   //出气压力上限，单位 Bar
    g_purge_ctrl.min_outlet_pressure_bar = -0.9f;   //出气压力下限，单位 Bar
    g_purge_ctrl.sensor_period_ms = 300U;
    g_purge_ctrl.control_period_ms = 50U;
    g_purge_ctrl.evacuate_time_ms = 8000U;
    g_purge_ctrl.fill_time_ms = 300000U;  
    g_purge_ctrl.stabilize_time_ms = 5000U;

    /* 允许 O2 偶尔几轮没回包 */
    g_purge_ctrl.o2_valid_timeout_ms = 3000U;

    /* SFC 通常比 O2 快，容忍窗口可以更短一些 */
    g_purge_ctrl.flow_valid_timeout_ms = 1500U;

    /* 温湿度常常刷新较慢，所以给更长的容忍窗口 */
    g_purge_ctrl.temp_humi_valid_timeout_ms = 5000U;

    g_purge_ctrl.external_output_flag = 0U; //默认不使用外部旁路出气
    g_purge_ctrl.gas_type = N2; //默认充气气体类型为氮气
}

/*
 * 读一轮传感器。
 *
 * 当前策略是“保留最后一次有效值”：
 * 1. 本轮读取成功 -> 更新值，同时刷新 last_xxx_ok_tick
 * 2. 本轮读取失败 -> 不把旧值立即清掉
 * 3. 只有超过允许失联时间后，才由 UpdateFaults() 把该通道判成无效
 *
 * 这样做是为了适应 O2、温湿度这类可能有延迟或偶尔丢包的设备，
 * 避免“某一轮没读到就马上 fault”。
 */
static void PurgeControl_ReadSensors(uint32_t now_ms)
{
    float flow_feedback = 0.0f;
    float o2_percent = -1.0f;

    /*
     * SFC 和 O2 共用同一串口总线。
     * 所以这里按固定顺序轮询，避免两边同时抢串口。
     */
    if (SFC_ReadFlowValue(&flow_feedback) == HAL_OK)
    {
        g_purge_ctrl.flow_feedback_lpm = (flow_feedback < 4.0f) ? 0.0f : flow_feedback;
        g_purge_ctrl.flow_valid = 1U;
        g_purge_ctrl.last_flow_ok_tick = now_ms;
    }
    /* 给共总线设备一点切换缓冲时间 */
    HAL_Delay(500);

    //读取氧气浓度
    o2_percent = O2_Sensor_ReadConcentration();
    if (o2_percent >= 0.0f)
    {
        g_purge_ctrl.o2_percent = o2_percent;
        g_purge_ctrl.o2_valid = 1U;
        g_purge_ctrl.last_o2_ok_tick = now_ms;
    }
    
    /* 压力和模拟流量这几路是本地 ADC，正常每轮都刷新 */
    g_purge_ctrl.inlet_pressure_bar = Get_Inlet_Pressure_Bar();
    g_purge_ctrl.outlet_pressure_bar = Get_Outlet_Pressure_Bar();
    g_purge_ctrl.analog_flow_lpm = Get_Flow_LPM();
    g_purge_ctrl.analog_flow_lpm = (g_purge_ctrl.analog_flow_lpm < 2.0f) ? 0.0f : g_purge_ctrl.analog_flow_lpm;

    
    if (SHT85_Read_Result() == ACK)
    {
        g_purge_ctrl.temperature_c = Temperature;
        g_purge_ctrl.humidity_percent = Humidity;
        g_purge_ctrl.temp_humi_valid = 1U;
        g_purge_ctrl.last_temp_humi_ok_tick = now_ms;
    }
}

/*
 * 故障判定。
 *
 * 这部分是本次改动的重点：
 * 以前是“本轮没读到就立刻 fault”
 * 现在是“超过 stale timeout 后才 fault”
 *
 * 所以 valid 的含义已经变成：
 * “到当前时刻为止，这个通道是否还在允许的有效时间窗口内”
 */
static void PurgeControl_UpdateFaults(uint32_t now_ms)
{
    uint32_t fault_code = 0UL;
    uint8_t micro_ignore_quality;

    /*
     * MICRO 腔体的 purge 流程不再依赖 O2 和湿度来判定流程是否合格。
     * 因此后面的 O2/湿度相关 fault 也需要一起绕开，
     * 避免 MICRO 流程因为这些非关注量而被提前打断。
     */
    micro_ignore_quality = (g_purge_ctrl.cavity == Microenvironment) ? 1U : 0U;

    /* 流量反馈超过允许时间没更新，才认为失效 */
    if ((g_purge_ctrl.flow_valid != 0U) &&
        (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.last_flow_ok_tick, g_purge_ctrl.flow_valid_timeout_ms) != 0U))
    {
        g_purge_ctrl.flow_valid = 0U;
    }

    /* O2 超过允许时间没更新，才认为失效 */
    if ((g_purge_ctrl.o2_valid != 0U) &&
        (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.last_o2_ok_tick, g_purge_ctrl.o2_valid_timeout_ms) != 0U))
    {
        g_purge_ctrl.o2_valid = 0U;
    }

    /* 温湿度超过允许时间没更新，才认为失效 */
    if ((g_purge_ctrl.temp_humi_valid != 0U) &&
        (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.last_temp_humi_ok_tick, g_purge_ctrl.temp_humi_valid_timeout_ms) != 0U))
    {
        g_purge_ctrl.temp_humi_valid = 0U;
    }

    /*
     * 上电初期也要给宽限期。
     * 例如 O2 设备第一次成功回包前，不能因为 now_ms 很小就立刻报码。
     */
    if ((micro_ignore_quality == 0U) &&
        (g_purge_ctrl.gas_type == N2) &&
        (g_purge_ctrl.o2_valid == 0U) &&
        (now_ms >= g_purge_ctrl.o2_valid_timeout_ms))
    {
        fault_code |= PURGE_CTRL_FAULT_O2_INVALID;
    }

    if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FILL) ||
        (g_purge_ctrl.state == PURGE_CTRL_STATE_RUN))
    {
        if ((g_purge_ctrl.flow_valid == 0U) &&
        (now_ms >= g_purge_ctrl.flow_valid_timeout_ms))
        {
            fault_code |= PURGE_CTRL_FAULT_FLOW_INVALID;
        }
    }
    

    if ((micro_ignore_quality == 0U) &&
        (g_purge_ctrl.temp_humi_valid == 0U) &&
        (now_ms >= g_purge_ctrl.temp_humi_valid_timeout_ms))
    {
        fault_code |= PURGE_CTRL_FAULT_TEMP_HUMI_INVALID;
    }

    /*
     * 压力异常只在置换流程真正开始后才参与判定。
     * 这样 INIT / SELF_CHECK / STANDBY 阶段即使压力不在工艺范围内，
     * 也不会提前触发正压或负压异常上报。
     */
    if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FILL) ||
        (g_purge_ctrl.state == PURGE_CTRL_STATE_STABILIZE) ||
        (g_purge_ctrl.state == PURGE_CTRL_STATE_RUN))
    {
        if ((g_purge_ctrl.inlet_pressure_bar > g_purge_ctrl.max_inlet_pressure_bar) ||
            (g_purge_ctrl.inlet_pressure_bar < g_purge_ctrl.min_inlet_pressure_bar))
        {
            fault_code |= PURGE_CTRL_FAULT_INLET_OVERPRESSURE;
        }
    }

    /*
     * 负压异常单独收窄到 FILL 阶段判断。
     * 原因是系统未开始置换前，负压表通常就是 0 Bar，
     * 如果在待机后刚收到 START 就立刻按范围判定，会导致流程还没真正建立负压就先报码。
     *
     * 这里再额外留出 1000ms 建压缓冲时间，
     * 让真空支路和传感器有机会进入正常工作区间后再判断是否越界。
     */
    if (((g_purge_ctrl.state == PURGE_CTRL_STATE_FILL) || (g_purge_ctrl.state == PURGE_CTRL_STATE_RUN)) &&
        (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.state_enter_tick, 2000U) != 0U))
    {
        if ((g_purge_ctrl.outlet_pressure_bar < g_purge_ctrl.min_outlet_pressure_bar) ||
            (g_purge_ctrl.outlet_pressure_bar > g_purge_ctrl.max_outlet_pressure_bar))
        {
            fault_code |= PURGE_CTRL_FAULT_OUTLET_LOW_PRESSURE;
        }
    }

    /* 置换流程在 FILL 阶段超时仍未满足完成条件，则记为流程超时。 */
    /*
     * 置换流程超时判定。
     * 当前约定在 FILL 阶段内，如果持续超过 `fill_time_ms`
     * 仍未达到进入 RUN 的完成条件，就置位超时 fault。
     */
    if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FILL) &&
        (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.state_enter_tick, g_purge_ctrl.fill_time_ms) != 0U))
    {
        fault_code |= PURGE_CTRL_FAULT_PURGE_TIMEOUT;
    }

    /* O2 超限只在 RUN 阶段作为运行故障处理 */
    if ((micro_ignore_quality == 0U) &&
        (g_purge_ctrl.state == PURGE_CTRL_STATE_RUN) &&
        (s_run_adjust_active != 0U) &&
        (PurgeControl_IsTimeout(now_ms, s_run_adjust_start_tick, s_run_adjust_timeout_ms) != 0U))
    {
        fault_code |= PURGE_CTRL_FAULT_RUN_QUALITY_TIMEOUT;
    }

    g_purge_ctrl.fault_code = fault_code;
}

/*
 * 状态机主体。
 * 这里决定每个阶段该输出什么，以及满足什么条件时切换阶段。
 */
static void PurgeControl_UpdateStateMachine(uint32_t now_ms)
{
    /* 只要当前 fault_code 非 0，就优先切到故障态 */
    if (g_purge_ctrl.fault_code != 0UL)
    {
        PurgeControl_SetState(PURGE_CTRL_STATE_FAULT, now_ms);
    }

    switch (g_purge_ctrl.state)
    {
        case PURGE_CTRL_STATE_INIT:
            PurgeControl_ApplySafeOutputs();
            PurgeControl_SetState(PURGE_CTRL_STATE_SELF_CHECK, now_ms);
            break;

        case PURGE_CTRL_STATE_SELF_CHECK:
            PurgeControl_ApplySafeOutputs();

            /*
             * 当前版本自检通过的条件比较简单：
             * 只要求流量和温湿度有效。
             * O2 没放在这里做硬条件，是为了避免慢设备刚上电时过早 fault。
             */
            if ((g_purge_ctrl.flow_valid != 0U) && (g_purge_ctrl.temp_humi_valid != 0U))
            {
                PurgeControl_SetState(PURGE_CTRL_STATE_STANDBY, now_ms);
            }
            break;

        case PURGE_CTRL_STATE_STANDBY:
            PurgeControl_ApplySafeOutputs();

            if (g_purge_ctrl.cmd_start != 0U)
            {
                g_purge_ctrl.cycle_counter++;
                PurgeControl_SetState(PURGE_CTRL_STATE_FILL, now_ms);
            }
            break;

        case PURGE_CTRL_STATE_FILL:
            PurgeControl_OutputFill(g_purge_ctrl.fill_flow_lpm);

            if (g_purge_ctrl.cmd_stop != 0U)
            {
                PurgeControl_SetState(PURGE_CTRL_STATE_STANDBY, now_ms);
            }
            /*
             * POD 流程仍按气体质量条件决定何时完成置换：
             * - N2：同时满足 O2 和湿度阈值
             * - XCDA：只看湿度
             *
             * MICRO 流程不再判断 O2 和湿度，
             * 只要置换阶段维持到最小工艺时间就进入 RUN。
             */
            else if (((g_purge_ctrl.cavity == Microenvironment) &&
                      (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.state_enter_tick, g_purge_ctrl.stabilize_time_ms) != 0U)) ||
                     ((((g_purge_ctrl.gas_type == N2) &&
                        (g_purge_ctrl.o2_valid != 0U) &&
                        (g_purge_ctrl.o2_percent <= g_purge_ctrl.run_enter_o2_percent)) ||
                       (g_purge_ctrl.gas_type == XCDA)) &&
                      (g_purge_ctrl.humidity_percent <= g_purge_ctrl.run_exit_humidity_percent) &&
                      (PurgeControl_IsTimeout(now_ms, g_purge_ctrl.state_enter_tick, g_purge_ctrl.stabilize_time_ms) != 0U)))
            {
                PurgeControl_SetState(PURGE_CTRL_STATE_RUN, now_ms);
            }
            break;

        case PURGE_CTRL_STATE_RUN:
            /*
             * RUN 阶段不再始终固定使用 run_flow_lpm。
             * 如果检测到出气流量偏大，或 O2/湿度在运行中再次超限，
             * 这里会动态提高进气目标流量尝试拉回工况。
             */
            PurgeControl_OutputRun(PurgeControl_CalcRunCommandFlow(now_ms));

            if (g_purge_ctrl.cmd_stop != 0U)
            {
                PurgeControl_SetState(PURGE_CTRL_STATE_STANDBY, now_ms);
            }
            break;

        case PURGE_CTRL_STATE_FAULT:
            PurgeControl_ApplySafeOutputs();

            /*
             * 当前改成“自恢复”策略：
             * 1. 只要故障条件还存在，就继续留在 FAULT
             * 2. 一旦 fault_code 自动恢复为 0，就回到 SELF_CHECK
             * 3. RESET 仍然保留，作为人工强制重新检查的入口
             *
             * 这样像 O2、温湿度这类偶发通信延迟恢复后，
             * 不需要主机再专门发 RESET 才能退出故障态。
             */
            if ((g_purge_ctrl.fault_code == 0UL) ||
                (g_purge_ctrl.cmd_reset_fault != 0U))
            {
                g_purge_ctrl.fault_code = 0UL;
                PurgeControl_SetState(PURGE_CTRL_STATE_SELF_CHECK, now_ms);
            }
            break;

        default:
            PurgeControl_ApplySafeOutputs();
            g_purge_ctrl.fault_code = PURGE_CTRL_FAULT_SENSOR_SAMPLE;
            PurgeControl_SetState(PURGE_CTRL_STATE_FAULT, now_ms);
            break;
    }
}

/* 切状态时顺便记录进入该状态的时间 */
static void PurgeControl_SetState(PurgeCtrl_State_t next_state, uint32_t now_ms)
{
    if (g_purge_ctrl.state != next_state)
    {
        if (next_state != PURGE_CTRL_STATE_RUN)
        {
            s_run_command_flow_lpm = g_purge_ctrl.run_flow_lpm;
            s_run_adjust_active = 0U;
            s_run_adjust_start_tick = 0U;
        }

        g_purge_ctrl.state = next_state;
        g_purge_ctrl.state_enter_tick = now_ms;
    }
}

/* 每轮控制处理完后，清掉一次性命令标志 */
static void PurgeControl_ClearCommands(void)
{
    g_purge_ctrl.cmd_start = 0U;
    g_purge_ctrl.cmd_stop = 0U;
    g_purge_ctrl.cmd_reset_fault = 0U;
}

/* 通用超时判断，now_ms 和 start_ms 都来自 HAL_GetTick() */
static uint8_t PurgeControl_IsTimeout(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms)
{
    return (((now_ms - start_ms) >= timeout_ms) ? 1U : 0U);
}

/*
 * RUN 阶段动态计算要下发给 SFC 的目标流量。
 *
 * 处理两类补偿场景：
 * 1. 如果检测到出气流量大于进气流量，则把进气目标提高到“出气流量 + 安全余量”。
 * 2. 如果 RUN 阶段 O2/湿度重新超限，则进入短时补偿窗口，按步进继续提高进气流量，
 *    争取把腔体环境拉回到设定值以内。
 */
static float PurgeControl_CalcRunCommandFlow(uint32_t now_ms)
{
    float target_flow = g_purge_ctrl.run_flow_lpm;
    uint8_t quality_abnormal = 0U;

    /*
     * MICRO 腔体在 RUN 阶段继续保持置换流量，
     * 不再切换到 run_flow_lpm，也不参与 O2/湿度动态补偿。
     * 这样可以保证 MICRO 一直以置换工况运行。
     */
    if (g_purge_ctrl.cavity == Microenvironment)
    {
        target_flow = g_purge_ctrl.fill_flow_lpm;
        if (target_flow > SFC_FLOW_MAX)
        {
            target_flow = SFC_FLOW_MAX;
        }

        s_run_adjust_active = 0U;
        s_run_adjust_start_tick = 0U;
        s_run_command_flow_lpm = target_flow;
        return target_flow;
    }

    if ((g_purge_ctrl.flow_valid != 0U) &&
        (g_purge_ctrl.analog_flow_lpm > g_purge_ctrl.flow_feedback_lpm))
    {
        float required_flow = g_purge_ctrl.analog_flow_lpm + s_run_flow_margin_lpm;
        if (required_flow > target_flow)
        {
            target_flow = required_flow;
        }
    }

    if ((g_purge_ctrl.temp_humi_valid != 0U) &&
        (g_purge_ctrl.humidity_percent > g_purge_ctrl.run_exit_humidity_percent))
    {
        quality_abnormal = 1U;
    }

    if ((g_purge_ctrl.gas_type == N2) &&
        (g_purge_ctrl.o2_valid != 0U) &&
        (g_purge_ctrl.o2_percent > g_purge_ctrl.run_enter_o2_percent))
    {
        quality_abnormal = 1U;
    }

    if (quality_abnormal != 0U)
    {
        if (s_run_adjust_active == 0U)
        {
            s_run_adjust_active = 1U;
            s_run_adjust_start_tick = now_ms;
            s_run_command_flow_lpm = target_flow;
        }

        if ((s_run_command_flow_lpm + s_run_adjust_step_lpm) > target_flow)
        {
            target_flow = s_run_command_flow_lpm + s_run_adjust_step_lpm;
        }
    }
    else
    {
        s_run_adjust_active = 0U;
        s_run_adjust_start_tick = 0U;
    }

    if (target_flow < g_purge_ctrl.run_flow_lpm)
    {
        target_flow = g_purge_ctrl.run_flow_lpm;
    }

    if (target_flow > SFC_FLOW_MAX)
    {
        target_flow = SFC_FLOW_MAX;
    }

    s_run_command_flow_lpm = target_flow;
    return target_flow;
}

/*
 * 充气置换输出：
 * - 停止抽真空
 * - 打开 Inlet1
 * - 打开出气阀
 * - 设定 fill_flow_lpm
 *
 * 这对应当前工艺“边进边排”。
 */
static void PurgeControl_OutputFill(float flow_lpm)
{
    //设置工作流量
    status = SFC_SetFlowValue(flow_lpm);
    
    if(g_purge_ctrl.cavity == POD)
    {
        //进气
        PurgeControl_SetInlet2(0U); //关闭电磁阀1
        PurgeControl_SetInlet1(1U); //如果是POD腔体，打开电磁阀2

        //出气
        if(g_purge_ctrl.external_output_flag == 1U)
        {
            PurgeControl_SetVacuumRelay(0U);//真空电磁阀3关闭
            PurgeControl_SetVacuumGenerator(0U);//真空发生器关闭
            PurgeControl_SetAirOutlet(1U);//外部旁路出气阀打开
        }
        else
        {
            PurgeControl_SetVacuumRelay(1U);//真空电磁阀3打开
            PurgeControl_SetVacuumGenerator(1U);//真空发生器打开
            PurgeControl_SetAirOutlet(0U);//外部旁路出气阀关闭
        }
        
    }
    else if(g_purge_ctrl.cavity == Microenvironment)
    {
        //进气
        PurgeControl_SetInlet2(1U); //如果是微环境腔体，打开电磁阀1
        PurgeControl_SetInlet1(0U); //关闭电磁阀2

        //出气
        //出气
        if(g_purge_ctrl.external_output_flag == 1U)
        {
            PurgeControl_SetVacuumRelay(0U);//真空电磁阀3关闭
            PurgeControl_SetVacuumGenerator(0U);//真空发生器关闭
            PurgeControl_SetAirOutlet(1U);//外部旁路出气阀打开
        }
        else
        {
            PurgeControl_SetVacuumRelay(1U);//真空电磁阀3打开
            PurgeControl_SetVacuumGenerator(1U);//真空发生器打开
            PurgeControl_SetAirOutlet(0U);//外部旁路出气阀关闭
        }
    }
    
}

/*
 * 稳定/运行输出：
 * - 阀门动作和 FILL 基本一致
 * - 只是流量切换到运行流量
 */
static void PurgeControl_OutputRun(float flow_lpm)
{
    //设置维持流量
    status = SFC_SetFlowValue(flow_lpm);
    
    if(g_purge_ctrl.cavity == POD)
    {
        //进气
        PurgeControl_SetInlet2(0U); //关闭电磁阀1
        PurgeControl_SetInlet1(1U); //如果是POD腔体，打开电磁阀2

        //出气
        //出气
        if(g_purge_ctrl.external_output_flag == 1U)
        {
            PurgeControl_SetVacuumRelay(0U);//真空电磁阀3关闭
            PurgeControl_SetVacuumGenerator(0U);//真空发生器关闭
            PurgeControl_SetAirOutlet(1U);//外部旁路出气阀打开
        }
        else
        {
            PurgeControl_SetVacuumRelay(1U);//真空电磁阀3打开
            PurgeControl_SetVacuumGenerator(1U);//维持阶段真空发生器打开
            PurgeControl_SetAirOutlet(0U);//外部旁路出气阀关闭
        }
    }
    else if(g_purge_ctrl.cavity == Microenvironment)
    {
        //进气
        PurgeControl_SetInlet2(1U); //如果是微环境腔体，打开电磁阀1
        PurgeControl_SetInlet1(0U); //关闭电磁阀2

        //出气
        //出气
        if(g_purge_ctrl.external_output_flag == 1U)
        {
            PurgeControl_SetVacuumRelay(0U);//真空电磁阀3关闭
            PurgeControl_SetVacuumGenerator(0U);//真空发生器关闭
            PurgeControl_SetAirOutlet(1U);//外部旁路出气阀打开
        }
        else
        {
            PurgeControl_SetVacuumRelay(1U);//真空电磁阀3打开
            PurgeControl_SetVacuumGenerator(1U);//真空发生器打开
            PurgeControl_SetAirOutlet(0U);//外部旁路出气阀关闭
        }
    }
}

/* 真空电磁阀，on=1 表示接通真空支路 */
static void PurgeControl_SetVacuumRelay(uint8_t on)
{
    if (on != 0U)
    {
        relay_on();
    }
    else
    {
        relay_off();
    }
}

/* 真空发生器输出 */
static void PurgeControl_SetVacuumGenerator(uint8_t on)
{
    if (on != 0U)
    {
        vacuum_open();
    }
    else
    {
        vacuum_close();
    }
}

/* Inlet1 是当前实际使用的主进气阀 */
static void PurgeControl_SetInlet1(uint8_t on)
{
    if (on != 0U)
    {
        air_inlet1_open();
    }
    else
    {
        air_inlet1_close();
    }
}

/* Inlet2 当前工艺未使用，接口保留着便于后面扩展 */
static void PurgeControl_SetInlet2(uint8_t on)
{
    if (on != 0U)
    {
        air_inlet2_open();
    }
    else
    {
        air_inlet2_close();
    }
}

/* 出气阀：在 EVACUATE/FILL/STABILIZE/RUN 阶段按工艺需求打开 */
static void PurgeControl_SetAirOutlet(uint8_t on)
{
    if (on != 0U)
    {
        air_outlet_open();
    }
    else
    {
        air_outlet_close();
    }
}

/* 预留控制阀，当前先保持关闭 */
static void PurgeControl_SetPodRelay(uint8_t on)
{
    if (on != 0U)
    {
        py_relay_on();
    }
    else
    {
        py_relay_off();
    }
}
