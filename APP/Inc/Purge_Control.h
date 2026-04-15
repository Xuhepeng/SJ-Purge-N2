#ifndef __PURGE_CONTROL_H
#define __PURGE_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * fault_code 按位定义，供控制层和通信层统一使用。
 * 通信层做主动 ALM 上报时，也是直接按这些 bit 判断是否出现了新的故障。
 */
#define PURGE_CTRL_FAULT_SENSOR_SAMPLE       1UL   /* 控制流程兜底故障 */
#define PURGE_CTRL_FAULT_O2_INVALID          2UL   /* O2 传感器长时间未得到有效更新 */
#define PURGE_CTRL_FAULT_FLOW_INVALID        4UL   /* 流量反馈长时间无效 */
#define PURGE_CTRL_FAULT_TEMP_HUMI_INVALID   8UL   /* 温湿度传感器长时间无效 */
#define PURGE_CTRL_FAULT_INLET_OVERPRESSURE  16UL  /* 正压侧超出设定范围 */
#define PURGE_CTRL_FAULT_OUTLET_LOW_PRESSURE 32UL  /* 负压侧超出设定范围 */
#define PURGE_CTRL_FAULT_O2_TOO_HIGH         64UL  /* RUN 阶段 O2 再次升高 */
#define PURGE_CTRL_FAULT_PURGE_TIMEOUT       128UL /* 置换流程在限定时间内未完成 */

/*
 * Purge 主状态机。
 * 这几个状态就是后面流程图里看到的主线：
 * 初始化 -> 自检 -> 待机 -> 抽真空 -> 充气置换 -> 稳定 -> 运行 -> 故障
 */
typedef enum
{
    PURGE_CTRL_STATE_INIT = 0,   /* 上电后的初始状态：加载参数、准备进入自检 */
    PURGE_CTRL_STATE_SELF_CHECK, /* 自检状态：读取关键传感器，确认系统具备启动条件 */
    PURGE_CTRL_STATE_STANDBY,    /* 待机状态：所有输出保持安全态，等待主机发送 START */
    //PURGE_CTRL_STATE_EVACUATE,   /* 抽真空状态：打开真空支路，对腔体进行抽空预处理 */
    PURGE_CTRL_STATE_FILL,       /* 充气置换状态：按“边进边排”工艺注入目标气体并持续排气 */
    PURGE_CTRL_STATE_STABILIZE,  /* 稳定状态：切换到运行流量，等待 O2/压力/流量逐步稳定 */
    PURGE_CTRL_STATE_RUN,        /* 正常运行状态：维持目标流量和微环境，持续监测异常 */
    PURGE_CTRL_STATE_FAULT       /* 故障状态：任一关键条件异常后进入，输出回到安全态 */
} PurgeCtrl_State_t;


/*充气腔体选择*/
typedef enum
{
    POD = 0,
    Microenvironment
}Target_Cavity;

/*充气气体类型*/
typedef enum
{
    N2 = 0,
    XCDA = 1,
}Gas_Type_t;

#define PURGE_CTRL_FAULT_RUN_QUALITY_TIMEOUT 256UL /* RUN 阶段补偿超时后，O2/湿度仍未回到设定范围内 */

/*
 * Purge 运行时上下文。
 * 这里集中保存：
 * 1. 当前状态
 * 2. 当前故障码
 * 3. 工艺参数
 * 4. 最近一次有效传感器值
 * 5. 主机下发的命令标志
 *
 * 这样做的好处是调试时可以直接在 Watch 窗口里看 g_purge_ctrl，
 * 不需要来回跳很多层指针。
 */
typedef struct
{
    /* 当前状态机状态 */
    PurgeCtrl_State_t state;

    /* 当前故障码，按位组合 */
    uint32_t fault_code;

    /* 进入当前状态时的时间戳，来自 HAL_GetTick() */
    uint32_t state_enter_tick;

    /* 周期调度时间戳，来自 HAL_GetTick() */
    uint32_t last_sensor_tick;
    uint32_t last_control_tick;

    /* 已完成的流程次数，可用于主机查询或调试统计 */
    uint32_t cycle_counter;

    /*
     * 最近一次成功读到各类传感器的时间。
     * 这几个时间戳是“容错型故障判断”的关键：
     * 不是本轮没读到就直接报码，而是超过允许的失联时间才报码。
     */
    uint32_t last_o2_ok_tick;
    uint32_t last_flow_ok_tick;
    uint32_t last_temp_humi_ok_tick;

    /* 工艺参数 */
    float fill_flow_lpm;          /* FILL 阶段目标流量 */
    float run_flow_lpm;           /* STABILIZE/RUN 阶段目标流量 */
    float run_enter_o2_percent;   /* O2 低于该值后，允许从 STABILIZE 进入 RUN */
    float run_exit_humidity_percent;/*空气湿度低于该值再进入稳定维持状态*/
    float run_fault_o2_percent;   /* RUN 阶段 O2 高于该值则报码 */
    float max_inlet_pressure_bar; /* 入口压力上限 */
    float min_inlet_pressure_bar; /* 入口压力下限 */
    float max_outlet_pressure_bar;/* 出口压力上限 */
    float min_outlet_pressure_bar;/* 出口压力下限 */

    /* 周期和阶段时间参数 */
    uint32_t sensor_period_ms;    /* 传感器轮询周期 */
    uint32_t control_period_ms;   /* 状态机执行周期 */
    uint32_t evacuate_time_ms;    /* 抽真空持续时间 */
    uint32_t fill_time_ms;        /* 充气置换持续时间 */
    uint32_t stabilize_time_ms;   /* 稳定阶段持续时间 */

    /*
     * 传感器“允许失联时间”。
     * 例如 O2 可能偶尔一轮没回包，只要没超过 o2_valid_timeout_ms，
     * 就继续沿用上一次有效值，不立刻当故障。
     */
    uint32_t o2_valid_timeout_ms;
    uint32_t flow_valid_timeout_ms;
    uint32_t temp_humi_valid_timeout_ms;

    /* 最近一次有效值 */
    float o2_percent;
    float flow_feedback_lpm;
    float analog_flow_lpm;
    float inlet_pressure_bar;
    float outlet_pressure_bar;
    float temperature_c;
    float humidity_percent;

    /*
     * 当前是否仍认为该数据“可用”。
     * 注意：这里的 valid 不是“这一轮是否成功读取”，
     * 而是“截止当前时刻，这个值是否还在允许的有效窗口内”。
     */
    uint8_t o2_valid;
    uint8_t flow_valid;
    uint8_t temp_humi_valid;

    /* 主机一次性命令标志，由 HostComm 层置位，控制层消费后清零 */
    uint8_t cmd_start;
    uint8_t cmd_stop;
    uint8_t cmd_reset_fault;

    /* 控制层是否已经完成初始化 */
    uint8_t initialized;

    Target_Cavity cavity; /* 当前操作的腔体，POD 或 Microenvironment */

    /*是否使用外部旁路出气*/
    uint8_t external_output_flag;

    Gas_Type_t gas_type; /* 充气气体类型 */
} PurgeControl_t;

/* 全局控制对象，便于 main、通信层和调试器直接观察 */
extern PurgeControl_t g_purge_ctrl;



/* 初始化控制层：加载参数、初始化传感器、下发安全输出 */
void PurgeControl_Init(void);
/* Save current configurable parameters to internal flash. */
uint8_t PurgeControl_SaveConfig(void);

/* 主循环周期调用入口，now_ms 一般直接传 HAL_GetTick() */
void PurgeControl_Process(uint32_t now_ms);

/* 以下三个函数通常由主机命令触发 */
void PurgeControl_Start(void);
void PurgeControl_PodCavity(void);
void PurgeControl_MicroCavity(void);
void PurgeControl_Stop(void);
void PurgeControl_Home(void);
void PurgeControl_ResetFault(void);

/* 强制退回待机态 */
void PurgeControl_EnterStandby(void);

/* 强制把所有输出拉回安全状态 */
void PurgeControl_ApplySafeOutputs(void);

/* 状态转字符串，便于主机查询和串口调试 */
const char *PurgeControl_GetStateName(PurgeCtrl_State_t state);

#ifdef __cplusplus
}
#endif

#endif /* __PURGE_CONTROL_H */
