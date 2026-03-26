#ifndef __PURGE_BUSINESS_H
#define __PURGE_BUSINESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 简单布尔定义。
 * 这里不依赖 stdbool，是为了后续在裸机、HAL、FreeRTOS 或其他老工程里都更容易复用。
 */
#define PURGE_BIZ_BOOL_FALSE 0U
#define PURGE_BIZ_BOOL_TRUE  1U

/* 业务层接收的外部命令。
 * 一般来自按键、串口上位机、Modbus 主站，或者后续的 FreeRTOS 消息队列。
 */
typedef enum
{
    PURGE_BIZ_CMD_NONE = 0,      /* 无新命令，本周期保持当前流程 */
    PURGE_BIZ_CMD_START,         /* 启动一次吹扫流程 */
    PURGE_BIZ_CMD_STOP,          /* 人工停止，回到待机 */
    PURGE_BIZ_CMD_RESET_FAULT    /* 故障复位，清除故障后重新自检 */
} PurgeBiz_Command_t;

/* 业务状态机。
 * 这是整套控制逻辑的核心，建议你后续所有扩展都围绕这个状态机做增删改。
 */
typedef enum
{
    PURGE_BIZ_STATE_INIT = 0,    /* 上电初始化，做上下文和输出默认值准备 */
    PURGE_BIZ_STATE_SELF_CHECK,  /* 自检，确认关键传感器和执行链路具备运行条件 */
    PURGE_BIZ_STATE_STANDBY,     /* 待机，所有执行器关闭，等待 START */
    PURGE_BIZ_STATE_EVACUATE,    /* 抽真空阶段 */
    PURGE_BIZ_STATE_FILL,        /* 充气阶段 */
    PURGE_BIZ_STATE_STABILIZE,   /* 稳定阶段，等待氧浓度和流场稳定 */
    PURGE_BIZ_STATE_RUN,         /* 正常运行阶段 */
    PURGE_BIZ_STATE_FAULT        /* 故障阶段，关闭输出并等待人工复位 */
} PurgeBiz_State_t;

/* 业务层抽象出来的数字输出通道。
 * 这里故意不用具体 GPIO 名称，而是用“业务名”，便于后面做硬件映射。
 */
typedef enum
{
    PURGE_BIZ_DO_RELAY = 0,      /* 总继电器/总使能 */
    PURGE_BIZ_DO_AIR_INLET_1,    /* 进气阀 1 */
    PURGE_BIZ_DO_AIR_INLET_2,    /* 进气阀 2 */
    PURGE_BIZ_DO_VACUUM,         /* 真空阀 */
    PURGE_BIZ_DO_AIR_OUTLET,     /* 出气阀 */
    PURGE_BIZ_DO_POD_RELAY,      /* Pod 侧继电器或联动控制 */
    PURGE_BIZ_DO_COUNT           /* 输出数量，用于数组长度 */
} PurgeBiz_DigitalOutput_t;

/* 故障码定义，按位表示。
 * 这样做的好处是：多个故障可以同时存在，调试和上报都比较方便。
 */
typedef enum
{
    PURGE_BIZ_FAULT_NONE                 = 0x00000000UL,
    PURGE_BIZ_FAULT_SENSOR_SAMPLE        = 0x00000001UL, /* 整次采样动作失败 */
    PURGE_BIZ_FAULT_O2_INVALID           = 0x00000002UL, /* 氧浓度数据无效 */
    PURGE_BIZ_FAULT_FLOW_INVALID         = 0x00000004UL, /* 流量反馈无效 */
    PURGE_BIZ_FAULT_TEMP_HUMI_INVALID    = 0x00000008UL, /* 温湿度数据无效 */
    PURGE_BIZ_FAULT_INLET_OVERPRESSURE   = 0x00000010UL, /* 进气压力超限 */
    PURGE_BIZ_FAULT_OUTLET_LOW_PRESSURE  = 0x00000020UL, /* 出口压力过低 */
    PURGE_BIZ_FAULT_O2_TOO_HIGH          = 0x00000040UL  /* 运行期氧浓度超限 */
} PurgeBiz_Fault_t;

/* 传感器快照。
 * 业务层每次只关心“当前系统状态”，所以把所有现场数据汇总到一个结构体里。
 * read_sensors() 每次更新这个结构体即可。
 */
typedef struct
{
    float o2_percent;            /* 氧浓度，单位：% */
    float flow_feedback_lpm;     /* 流量控制器反馈流量，单位：L/min */
    float analog_flow_lpm;       /* 模拟量采集到的流量，单位：L/min，可用于交叉验证 */
    float inlet_pressure_bar;    /* 进气压力，单位：bar */
    float outlet_pressure_bar;   /* 出口压力，单位：bar */
    float temperature_c;         /* 温度，单位：摄氏度 */
    float humidity_percent;      /* 湿度，单位：%RH */

    uint8_t o2_valid;            /* 1=氧浓度有效 */
    uint8_t flow_valid;          /* 1=流量反馈有效 */
    uint8_t temp_humi_valid;     /* 1=温湿度有效 */
} PurgeBiz_SensorData_t;

/* 业务层输出命令。
 * apply_outputs() 只需要关心“现在该输出什么”，不需要知道状态机内部细节。
 */
typedef struct
{
    uint8_t digital_output[PURGE_BIZ_DO_COUNT]; /* 每一路数字输出的期望状态 */
    float target_flow_lpm;                      /* 目标流量设定值，单位：L/min */
    uint8_t target_flow_valid;                  /* 1=本周期需要下发流量设定 */
} PurgeBiz_OutputData_t;

/* 业务参数。
 * 后续你可以把这部分迁移到参数表、Flash、EEPROM 或上位机下载配置里。
 */
typedef struct
{
    float fill_flow_lpm;             /* 充气阶段目标流量 */
    float run_flow_lpm;              /* 运行阶段目标流量 */
    float run_enter_o2_percent;      /* 进入 RUN 的氧浓度阈值 */
    float run_fault_o2_percent;      /* RUN 状态下的氧浓度故障阈值 */
    float max_inlet_pressure_bar;    /* 最大允许进气压力 */
    float min_outlet_pressure_bar;   /* 最小允许出口压力 */

    uint32_t sensor_period_ms;       /* 采样周期 */
    uint32_t control_period_ms;      /* 控制周期 */
    uint32_t evacuate_time_ms;       /* 抽真空阶段持续时间 */
    uint32_t fill_time_ms;           /* 充气阶段持续时间 */
    uint32_t stabilize_time_ms;      /* 稳定阶段持续时间 */
} PurgeBiz_Config_t;

/* 驱动层回调。
 * 业务层只定义“要什么”，底层驱动负责“怎么做”。
 * 这样后续迁移到 FreeRTOS 或替换硬件时，业务层基本不用动。
 */
typedef struct
{
    uint8_t (*read_sensors)(PurgeBiz_SensorData_t *sensor);              /* 读取现场数据 */
    uint8_t (*apply_outputs)(const PurgeBiz_OutputData_t *output);       /* 下发执行器控制 */
} PurgeBiz_Driver_t;

/* 业务上下文。
 * 整个状态机运行时需要的数据都集中在这里，便于单实例或多实例扩展。
 */
typedef struct
{
    PurgeBiz_State_t state;              /* 当前状态 */
    PurgeBiz_Command_t pending_command;  /* 等待处理的命令 */
    PurgeBiz_SensorData_t sensor;        /* 最新传感器快照 */
    PurgeBiz_OutputData_t output;        /* 最新业务输出 */
    PurgeBiz_Config_t config;            /* 当前业务参数 */
    PurgeBiz_Driver_t driver;            /* 驱动接口 */

    uint32_t fault_code;                 /* 当前故障码，按位组合 */
    uint32_t state_enter_tick;           /* 进入当前状态的时刻 */
    uint32_t last_sensor_tick;           /* 上次采样时刻 */
    uint32_t last_control_tick;          /* 上次控制时刻 */
    uint32_t cycle_counter;              /* 已启动流程计数 */
} PurgeBiz_Context_t;

/* 初始化上下文。
 * 会清空上下文，并加载默认配置，同时把输出置为安全待机状态。
 */
void PurgeBiz_Init(PurgeBiz_Context_t *ctx);

/* 加载默认工艺参数。
 * 如果你后续有参数表，可以先调用默认值，再按需覆盖。
 */
void PurgeBiz_LoadDefaultConfig(PurgeBiz_Config_t *config);

/* 绑定底层驱动回调。 */
void PurgeBiz_BindDriver(PurgeBiz_Context_t *ctx, const PurgeBiz_Driver_t *driver);

/* 设置一个待处理命令。
 * 命令会在下一次 PurgeBiz_Process() 执行时生效。
 */
void PurgeBiz_SetCommand(PurgeBiz_Context_t *ctx, PurgeBiz_Command_t command);

/* 周期调用的主入口。
 * 裸机下可以在 while(1) 里按固定频率调用；
 * FreeRTOS 下可以放到一个 10ms/20ms 周期任务里调用。
 */
void PurgeBiz_Process(PurgeBiz_Context_t *ctx, uint32_t now_ms);

/* 强制切换状态。
 * 主要用于调试、联调或特殊人工干预，不建议作为正常业务入口长期使用。
 */
void PurgeBiz_ForceState(PurgeBiz_Context_t *ctx, PurgeBiz_State_t state);

/* 获取状态名字，便于串口打印、日志记录、上位机显示。 */
const char *PurgeBiz_GetStateName(PurgeBiz_State_t state);

#ifdef __cplusplus
}
#endif

#endif /* __PURGE_BUSINESS_H */
