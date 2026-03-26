#include "Purge_Business.h"

#include <string.h>

/* 内部函数只服务于当前业务模块，不对外暴露。
 * 这样做可以让外部接口保持简洁，后续你改内部实现时也不影响外部使用方式。
 */
static void PurgeBiz_SetState(PurgeBiz_Context_t *ctx, PurgeBiz_State_t next_state, uint32_t now_ms);
static void PurgeBiz_ClearOutput(PurgeBiz_OutputData_t *output);
static void PurgeBiz_PrepareStandbyOutput(PurgeBiz_OutputData_t *output);
static void PurgeBiz_PrepareEvacuateOutput(PurgeBiz_OutputData_t *output);
static void PurgeBiz_PrepareFillOutput(PurgeBiz_OutputData_t *output, float target_flow_lpm);
static void PurgeBiz_PrepareRunOutput(PurgeBiz_OutputData_t *output, float target_flow_lpm);
static void PurgeBiz_UpdateFaults(PurgeBiz_Context_t *ctx);
static uint8_t PurgeBiz_IsTimeout(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms);

/* 加载一套默认业务参数。
 * 这些值只是“框架默认值”，不是最终工艺值。
 * 你后续可以根据实际气路、腔体体积、阀门响应速度再去调整。
 */
void PurgeBiz_LoadDefaultConfig(PurgeBiz_Config_t *config)
{
    if (config == 0)
    {
        return;
    }

    config->fill_flow_lpm = 80.0f;
    config->run_flow_lpm = 50.0f;
    config->run_enter_o2_percent = 5.0f;
    config->run_fault_o2_percent = 8.0f;
    config->max_inlet_pressure_bar = 8.5f;
    config->min_outlet_pressure_bar = -0.95f;
    config->sensor_period_ms = 500U;
    config->control_period_ms = 100U;
    config->evacuate_time_ms = 8000U;
    config->fill_time_ms = 6000U;
    config->stabilize_time_ms = 5000U;
}

/* 初始化业务上下文。
 * 这里会：
 * 1. 清零整个上下文
 * 2. 装载默认参数
 * 3. 设置初始状态为 INIT
 * 4. 把输出准备为安全待机态
 */
void PurgeBiz_Init(PurgeBiz_Context_t *ctx)
{
    if (ctx == 0)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    PurgeBiz_LoadDefaultConfig(&ctx->config);
    ctx->state = PURGE_BIZ_STATE_INIT;
    PurgeBiz_PrepareStandbyOutput(&ctx->output);
}

/* 绑定底层驱动回调。
 * 业务层并不直接访问串口、ADC、GPIO，而是通过回调函数去读写。
 */
void PurgeBiz_BindDriver(PurgeBiz_Context_t *ctx, const PurgeBiz_Driver_t *driver)
{
    if ((ctx == 0) || (driver == 0))
    {
        return;
    }

    ctx->driver = *driver;
}

/* 缓存外部命令。
 * 这里只是写入 pending_command，不会立即切状态。
 * 真正执行命令统一放在 PurgeBiz_Process() 里做，避免逻辑分散。
 */
void PurgeBiz_SetCommand(PurgeBiz_Context_t *ctx, PurgeBiz_Command_t command)
{
    if (ctx == 0)
    {
        return;
    }

    ctx->pending_command = command;
}

/* 强制修改当前状态。
 * 主要用于联调，例如你想单独测试 EVACUATE/FILL/RUN 某一个阶段。
 */
void PurgeBiz_ForceState(PurgeBiz_Context_t *ctx, PurgeBiz_State_t state)
{
    if (ctx == 0)
    {
        return;
    }

    ctx->state = state;
    ctx->state_enter_tick = 0U;
}

/* 业务层周期处理入口。
 * 建议理解成两个周期任务合并在一起：
 * 1. 采样任务：按 sensor_period_ms 更新传感器快照
 * 2. 控制任务：按 control_period_ms 执行状态机并输出控制命令
 *
 * 这样做的好处是：
 * 1. 裸机阶段简单，直接循环调用
 * 2. 后续迁移 FreeRTOS 时，很容易拆成两个任务
 */
void PurgeBiz_Process(PurgeBiz_Context_t *ctx, uint32_t now_ms)
{
    uint8_t sample_ok;

    if (ctx == 0)
    {
        return;
    }

    if ((ctx->driver.read_sensors != 0) &&
        ((now_ms - ctx->last_sensor_tick) >= ctx->config.sensor_period_ms))
    {
        ctx->last_sensor_tick = now_ms;

        /* 由外部驱动层负责真正读取现场设备。
         * 例如：
         * - O2 传感器走串口/Modbus
         * - 流量控制器走串口/Modbus
         * - 压力、流量模拟量走 ADC
         * - 温湿度走 I2C
         */
        sample_ok = ctx->driver.read_sensors(&ctx->sensor);
        if (sample_ok == PURGE_BIZ_BOOL_FALSE)
        {
            ctx->fault_code |= PURGE_BIZ_FAULT_SENSOR_SAMPLE;
        }

        /* 根据最新传感器快照刷新故障状态。 */
        PurgeBiz_UpdateFaults(ctx);
    }

    /* 控制周期还没到，就先返回。 */
    if ((now_ms - ctx->last_control_tick) < ctx->config.control_period_ms)
    {
        return;
    }
    ctx->last_control_tick = now_ms;

    /* 只要当前存在故障，就强制进入 FAULT。 */
    if (ctx->fault_code != PURGE_BIZ_FAULT_NONE)
    {
        PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_FAULT, now_ms);
    }

    /* 状态机主体。
     * 每个状态只负责两件事：
     * 1. 准备该状态下的输出
     * 2. 判断何时切到下一个状态
     */
    switch (ctx->state)
    {
        case PURGE_BIZ_STATE_INIT:
            /* INIT 阶段只做一次性准备，然后进入自检。 */
            PurgeBiz_PrepareStandbyOutput(&ctx->output);
            PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_SELF_CHECK, now_ms);
            break;

        case PURGE_BIZ_STATE_SELF_CHECK:
            /* 自检阶段默认保持安全输出。
             * 当前示例里只要求流量和温湿度有效，你后续也可以把 O2、压力等条件加进来。
             */
            PurgeBiz_PrepareStandbyOutput(&ctx->output);
            if ((ctx->sensor.flow_valid != 0U) && (ctx->sensor.temp_humi_valid != 0U))
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_STANDBY, now_ms);
            }
            break;

        case PURGE_BIZ_STATE_STANDBY:
            /* 待机态：所有输出关闭，只等待启动命令。 */
            PurgeBiz_PrepareStandbyOutput(&ctx->output);
            if (ctx->pending_command == PURGE_BIZ_CMD_START)
            {
                ctx->cycle_counter++;
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_EVACUATE, now_ms);
            }
            break;

        case PURGE_BIZ_STATE_EVACUATE:
            /* 抽真空阶段：
             * 打开总使能、真空阀、出气阀、Pod 联动。
             * 到时间后切到充气阶段。
             */
            PurgeBiz_PrepareEvacuateOutput(&ctx->output);
            if (ctx->pending_command == PURGE_BIZ_CMD_STOP)
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_STANDBY, now_ms);
            }
            else if (PurgeBiz_IsTimeout(now_ms, ctx->state_enter_tick, ctx->config.evacuate_time_ms) != 0U)
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_FILL, now_ms);
            }
            break;

        case PURGE_BIZ_STATE_FILL:
            /* 充气阶段：
             * 打开进气通路，给流量控制器下发 fill_flow_lpm。
             */
            PurgeBiz_PrepareFillOutput(&ctx->output, ctx->config.fill_flow_lpm);
            if (ctx->pending_command == PURGE_BIZ_CMD_STOP)
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_STANDBY, now_ms);
            }
            else if (PurgeBiz_IsTimeout(now_ms, ctx->state_enter_tick, ctx->config.fill_time_ms) != 0U)
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_STABILIZE, now_ms);
            }
            break;

        case PURGE_BIZ_STATE_STABILIZE:
            /* 稳定阶段：
             * 仍然维持正常进气，但暂时不进入 RUN。
             * 等待两个条件同时满足：
             * 1. 稳定时间到
             * 2. O2 降到允许阈值以下
             */
            PurgeBiz_PrepareRunOutput(&ctx->output, ctx->config.run_flow_lpm);
            if (ctx->pending_command == PURGE_BIZ_CMD_STOP)
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_STANDBY, now_ms);
            }
            else if ((ctx->sensor.o2_valid != 0U) &&
                     (ctx->sensor.o2_percent <= ctx->config.run_enter_o2_percent) &&
                     (PurgeBiz_IsTimeout(now_ms, ctx->state_enter_tick, ctx->config.stabilize_time_ms) != 0U))
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_RUN, now_ms);
            }
            break;

        case PURGE_BIZ_STATE_RUN:
            /* 正常运行阶段：
             * 维持运行态输出，只有人工 STOP 或故障才退出。
             */
            PurgeBiz_PrepareRunOutput(&ctx->output, ctx->config.run_flow_lpm);
            if (ctx->pending_command == PURGE_BIZ_CMD_STOP)
            {
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_STANDBY, now_ms);
            }
            break;

        case PURGE_BIZ_STATE_FAULT:
            /* 故障态：
             * 统一回到安全输出，等待外部发送 RESET_FAULT。
             */
            PurgeBiz_PrepareStandbyOutput(&ctx->output);
            if (ctx->pending_command == PURGE_BIZ_CMD_RESET_FAULT)
            {
                ctx->fault_code = PURGE_BIZ_FAULT_NONE;
                PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_SELF_CHECK, now_ms);
            }
            break;

        default:
            /* 理论上不应进入这里。
             * 如果进来了，说明状态异常，直接拉入故障态更安全。
             */
            PurgeBiz_PrepareStandbyOutput(&ctx->output);
            ctx->fault_code |= PURGE_BIZ_FAULT_SENSOR_SAMPLE;
            PurgeBiz_SetState(ctx, PURGE_BIZ_STATE_FAULT, now_ms);
            break;
    }

    /* 输出下发放在状态机末尾统一做。
     * 这样任何状态都只需要准备 output，不需要自己直接操作底层硬件。
     */
    if (ctx->driver.apply_outputs != 0)
    {
        (void)ctx->driver.apply_outputs(&ctx->output);
    }

    /* 命令默认只消费一次，处理完后清空。 */
    ctx->pending_command = PURGE_BIZ_CMD_NONE;
}

/* 返回状态字符串，便于调试输出或上位机显示。 */
const char *PurgeBiz_GetStateName(PurgeBiz_State_t state)
{
    switch (state)
    {
        case PURGE_BIZ_STATE_INIT:
            return "INIT";
        case PURGE_BIZ_STATE_SELF_CHECK:
            return "SELF_CHECK";
        case PURGE_BIZ_STATE_STANDBY:
            return "STANDBY";
        case PURGE_BIZ_STATE_EVACUATE:
            return "EVACUATE";
        case PURGE_BIZ_STATE_FILL:
            return "FILL";
        case PURGE_BIZ_STATE_STABILIZE:
            return "STABILIZE";
        case PURGE_BIZ_STATE_RUN:
            return "RUN";
        case PURGE_BIZ_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

/* 内部状态切换函数。
 * 只有状态真正发生变化时，才刷新 state_enter_tick。
 * 这样超时判断才能准确基于“进入当前状态的时刻”来算。
 */
static void PurgeBiz_SetState(PurgeBiz_Context_t *ctx, PurgeBiz_State_t next_state, uint32_t now_ms)
{
    if (ctx->state != next_state)
    {
        ctx->state = next_state;
        ctx->state_enter_tick = now_ms;
    }
}

/* 清空输出结构体，恢复到“所有数字输出关闭、流量无效”的安全默认状态。 */
static void PurgeBiz_ClearOutput(PurgeBiz_OutputData_t *output)
{
    uint32_t i;

    for (i = 0U; i < PURGE_BIZ_DO_COUNT; i++)
    {
        output->digital_output[i] = PURGE_BIZ_BOOL_FALSE;
    }
    output->target_flow_lpm = 0.0f;
    output->target_flow_valid = PURGE_BIZ_BOOL_FALSE;
}

/* 待机输出：
 * 全部关闭。
 */
static void PurgeBiz_PrepareStandbyOutput(PurgeBiz_OutputData_t *output)
{
    PurgeBiz_ClearOutput(output);
}

/* 抽真空输出：
 * 打开总继电器、真空阀、出气阀和 Pod 继电器。
 * 这里的组合只是按当前气路图做的一个通用示例，你可以后续自己改。
 */
static void PurgeBiz_PrepareEvacuateOutput(PurgeBiz_OutputData_t *output)
{
    PurgeBiz_ClearOutput(output);
    output->digital_output[PURGE_BIZ_DO_RELAY] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_VACUUM] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_AIR_OUTLET] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_POD_RELAY] = PURGE_BIZ_BOOL_TRUE;
}

/* 充气输出：
 * 打开总继电器、两路进气阀和 Pod 联动，同时下发目标流量。
 */
static void PurgeBiz_PrepareFillOutput(PurgeBiz_OutputData_t *output, float target_flow_lpm)
{
    PurgeBiz_ClearOutput(output);
    output->digital_output[PURGE_BIZ_DO_RELAY] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_AIR_INLET_1] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_AIR_INLET_2] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_POD_RELAY] = PURGE_BIZ_BOOL_TRUE;
    output->target_flow_lpm = target_flow_lpm;
    output->target_flow_valid = PURGE_BIZ_BOOL_TRUE;
}

/* 运行输出：
 * 保持进气，同时打开出气阀，形成稳定流路，并下发运行流量。
 */
static void PurgeBiz_PrepareRunOutput(PurgeBiz_OutputData_t *output, float target_flow_lpm)
{
    PurgeBiz_ClearOutput(output);
    output->digital_output[PURGE_BIZ_DO_RELAY] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_AIR_INLET_1] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_AIR_INLET_2] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_AIR_OUTLET] = PURGE_BIZ_BOOL_TRUE;
    output->digital_output[PURGE_BIZ_DO_POD_RELAY] = PURGE_BIZ_BOOL_TRUE;
    output->target_flow_lpm = target_flow_lpm;
    output->target_flow_valid = PURGE_BIZ_BOOL_TRUE;
}

/* 根据当前采样值计算故障码。
 * 这里采用“每次重算”的思路，而不是一直累加，避免旧故障残留。
 * 但 SENSOR_SAMPLE 会保留本次采样动作失败的信息。
 */
static void PurgeBiz_UpdateFaults(PurgeBiz_Context_t *ctx)
{
    uint32_t fault_code = ctx->fault_code & PURGE_BIZ_FAULT_SENSOR_SAMPLE;

    if (ctx->sensor.o2_valid == 0U)
    {
        fault_code |= PURGE_BIZ_FAULT_O2_INVALID;
    }
    if (ctx->sensor.flow_valid == 0U)
    {
        fault_code |= PURGE_BIZ_FAULT_FLOW_INVALID;
    }
    if (ctx->sensor.temp_humi_valid == 0U)
    {
        fault_code |= PURGE_BIZ_FAULT_TEMP_HUMI_INVALID;
    }
    if (ctx->sensor.inlet_pressure_bar > ctx->config.max_inlet_pressure_bar)
    {
        fault_code |= PURGE_BIZ_FAULT_INLET_OVERPRESSURE;
    }
    if (ctx->sensor.outlet_pressure_bar < ctx->config.min_outlet_pressure_bar)
    {
        fault_code |= PURGE_BIZ_FAULT_OUTLET_LOW_PRESSURE;
    }
    if ((ctx->state == PURGE_BIZ_STATE_RUN) &&
        (ctx->sensor.o2_valid != 0U) &&
        (ctx->sensor.o2_percent > ctx->config.run_fault_o2_percent))
    {
        fault_code |= PURGE_BIZ_FAULT_O2_TOO_HIGH;
    }

    ctx->fault_code = fault_code;
}

/* 超时判断工具函数。
 * now_ms 和 start_ms 都建议使用同一套毫秒时基，比如 HAL_GetTick()。
 */
static uint8_t PurgeBiz_IsTimeout(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms)
{
    return (((now_ms - start_ms) >= timeout_ms) ? PURGE_BIZ_BOOL_TRUE : PURGE_BIZ_BOOL_FALSE);
}
