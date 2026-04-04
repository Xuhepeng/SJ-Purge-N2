#include "Purge_HostComm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usart.h"

/*
 * Purge_HostComm
 *
 * 该模块是设备与上位机之间的 ASCII 协议适配层，当前仅保留
 * `Doc/Purge_ASCII协议手册.md` 中定义的协议格式。
 *
 * 当前支持的主机命令：
 * 1. FSR FC=0/1/2
 * 2. HCS START_PURGE POD|MICRO
 * 3. HCS STOP_PURGE
 * 4. ECS XX=Data
 * 5. ECR XX
 * 6. EDER ON|OFF
 *
 * 模块额外负责：
 * 1. UART3 单字节中断接收
 * 2. 基于 CR/LF 或空闲超时的行收帧
 * 3. 事件 AERS 与报警 ARS 主动上报
 */

/* 当前唯一生效的主机通信实例，供 HAL UART 回调间接访问。 */
static PurgeHostComm_t *s_host_comm = 0;

/* 处理一整行主机发送的 ASCII 报文。 */
static void PurgeHostComm_HandleLine(PurgeHostComm_t *comm, const char *line);
/* 发送一段完整 ASCII 文本到 UART3。 */
static void PurgeHostComm_SendText(PurgeHostComm_t *comm, const char *text);
/* 发送内部诊断错误，如行过长。 */
static void PurgeHostComm_SendPromptErr(PurgeHostComm_t *comm, const char *tag);
/* 判断两个字符串是否完全相等。 */
static uint8_t PurgeHostComm_Equals(const char *a, const char *b);
/* 去除字符串首尾空白字符。 */
static void PurgeHostComm_Trim(char *text);
/* 解析形如 KEY=VALUE 的字段。 */
static uint8_t PurgeHostComm_ParseKeyValue(const char *text, char *key, size_t key_size, char *value, size_t value_size);
/* 尝试将字符串解析为 float。 */
static uint8_t PurgeHostComm_TryParseFloat(const char *text, float *value);
/* 尝试将字符串解析为 uint32。 */
static uint8_t PurgeHostComm_TryParseUint32(const char *text, uint32_t *value);
/* 协议主解析入口，仅处理手册中定义的 ASCII 命令。 */
static uint8_t PurgeHostComm_HandleAsciiProtocol(PurgeHostComm_t *comm, const char *line);
/* 主动上报事件，如 POWER_UP、START_PURGE、CMPL_PURGE_xxx。 */
static void PurgeHostComm_ProcessAsciiEvents(PurgeHostComm_t *comm, uint32_t now_ms);
/* 主动上报新增报警，仅对 fault_code 新增位发报文。 */
static void PurgeHostComm_ProcessAsciiAlarms(PurgeHostComm_t *comm);
/* 从环形缓冲区取出一个字节。 */
static uint8_t PurgeHostComm_RingPop(PurgeHostComm_t *comm, uint8_t *byte);
/* 在 UART ISR 中向环形缓冲区压入一个字节。 */
static void PurgeHostComm_RingPushIsr(PurgeHostComm_t *comm, uint8_t byte);
/* 结束当前行并交给协议层处理。 */
static void PurgeHostComm_FinalizeLine(PurgeHostComm_t *comm);

/* 初始化通信上下文并启动 UART3 接收。 */
void PurgeHostComm_Init(PurgeHostComm_t *comm)
{
    if (comm == 0)
    {
        return;
    }

    memset(comm, 0, sizeof(*comm));

    comm->frame_idle_timeout_ms = 30U;
    comm->ascii_event_enable = 1U;
    comm->ascii_power_up_pending = 1U;

    s_host_comm = comm;
    PurgeHostComm_StartUart3RxIT(comm);
}

/* UART 错误后重新挂起单字节接收，尽量保证链路不中断。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == &huart3) && (s_host_comm != 0))
    {
        (void)HAL_UART_Receive_IT(&huart3, &s_host_comm->uart_rx_byte, 1U);
    }
}

/* 主循环入口：处理接收缓存、空闲收帧、报警和事件上报。 */
void PurgeHostComm_Process(PurgeHostComm_t *comm)
{
    uint32_t now_ms;
    uint8_t rx_byte;

    if (comm == 0)
    {
        return;
    }

    while (PurgeHostComm_RingPop(comm, &rx_byte) != 0U)
    {
        PurgeHostComm_FeedByte(comm, rx_byte);
    }

    now_ms = HAL_GetTick();
    if ((comm->line_len > 0U) &&
        ((now_ms - comm->last_rx_tick) >= comm->frame_idle_timeout_ms))
    {
        PurgeHostComm_FinalizeLine(comm);
    }

    PurgeHostComm_ProcessAsciiAlarms(comm);
    PurgeHostComm_ProcessAsciiEvents(comm, now_ms);
}

/* 按字节收帧：遇到 CR/LF 立即结束一帧，否则继续缓存。 */
void PurgeHostComm_FeedByte(PurgeHostComm_t *comm, uint8_t byte)
{
    if (comm == 0)
    {
        return;
    }

    comm->last_rx_tick = HAL_GetTick();

    if ((byte == '\r') || (byte == '\n'))
    {
        PurgeHostComm_FinalizeLine(comm);
        return;
    }

    if (comm->line_len >= (uint16_t)(sizeof(comm->line_buf) - 1U))
    {
        comm->line_len = 0U;
        PurgeHostComm_SendPromptErr(comm, "LINE_TOO_LONG");
        return;
    }

    comm->line_buf[comm->line_len++] = byte;
}

/* 组包发送 FC=0 查询应答，即设备当前运行状态。 */
void PurgeHostComm_SendStatus(PurgeHostComm_t *comm)
{
    char tx[256];
    const char *mode;

    if (comm == 0)
    {
        return;
    }

    mode = (g_purge_ctrl.cavity == POD) ? "POD" : "MICRO";
    (void)snprintf(tx,
                   sizeof(tx),
                   "FSD=0 STATUS=%s MODE=%s FAULT=0x%08lX CYCLE=%lu\r\n",
                   PurgeControl_GetStateName(g_purge_ctrl.state),
                   mode,
                   (unsigned long)g_purge_ctrl.fault_code,
                   (unsigned long)g_purge_ctrl.cycle_counter);

    PurgeHostComm_SendText(comm, tx);
}

/* 组包发送 FC=1 查询应答，即当前传感器采样值。 */
void PurgeHostComm_SendSensors(PurgeHostComm_t *comm)
{
    char tx[256];

    if (comm == 0)
    {
        return;
    }

    (void)snprintf(tx,
                   sizeof(tx),
                   "FSD=1 O2=%.2f FLOW=%.2f AFLOW=%.2f INP=%.2f OUTP=%.2f T=%.2f H=%.2f\r\n",
                   (double)g_purge_ctrl.o2_percent,
                   (double)g_purge_ctrl.flow_feedback_lpm,
                   (double)g_purge_ctrl.analog_flow_lpm,
                   (double)g_purge_ctrl.inlet_pressure_bar,
                   (double)g_purge_ctrl.outlet_pressure_bar,
                   (double)g_purge_ctrl.temperature_c,
                   (double)g_purge_ctrl.humidity_percent);

    PurgeHostComm_SendText(comm, tx);
}

/* 启动 UART3 的单字节中断接收。 */
void PurgeHostComm_StartUart3RxIT(PurgeHostComm_t *comm)
{
    if (comm == 0)
    {
        return;
    }

    (void)HAL_UART_Receive_IT(&huart3, &comm->uart_rx_byte, 1U);
}

/* UART3 收到一个字节后，将其送入环形缓冲区并继续接收下一字节。 */
void PurgeHostComm_OnUart3RxCplt(void)
{
    if (s_host_comm == 0)
    {
        return;
    }

    PurgeHostComm_RingPushIsr(s_host_comm, s_host_comm->uart_rx_byte);
    (void)HAL_UART_Receive_IT(&huart3, &s_host_comm->uart_rx_byte, 1U);
}

/* HAL UART 接收完成回调，只关心 UART3。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart3)
    {
        PurgeHostComm_OnUart3RxCplt();
    }
}

/* 处理一整行命令；无法识别时统一回 HCA DENEID。 */
static void PurgeHostComm_HandleLine(PurgeHostComm_t *comm, const char *line)
{
    if ((comm == 0) || (line == 0))
    {
        return;
    }

    if (PurgeHostComm_HandleAsciiProtocol(comm, line) == 0U)
    {
        PurgeHostComm_SendText(comm, "HCA DENEID\r\n");
    }
}

/*
 * ASCII 协议解析主入口。
 *
 * 解析顺序按手册中的大类区分：
 * 1. FSR 查询类
 * 2. HCS 控制类
 * 3. ECS 参数设置类
 * 4. ECR 参数读取类
 * 5. EDER 事件开关类
 */
static uint8_t PurgeHostComm_HandleAsciiProtocol(PurgeHostComm_t *comm, const char *line)
{
    char tx[192];
    char work[128];
    char token1[32];
    char token2[32];
    char token3[32];
    char key[32];
    char value[32];
    unsigned long fc;
    float float_value;
    uint32_t uint_value;
    uint8_t set_ok = 0U;
    float backup_fill_flow_lpm;
    float backup_run_flow_lpm;
    float backup_run_enter_o2_percent;
    float backup_run_exit_humidity_percent;
    float backup_max_inlet_pressure_bar;
    float backup_min_inlet_pressure_bar;
    float backup_max_outlet_pressure_bar;
    float backup_min_outlet_pressure_bar;
    uint8_t backup_external_output_flag;
    Gas_Type_t backup_gas_type;

    if ((comm == 0) || (line == 0))
    {
        return 0U;
    }

    /* 在本地副本上做裁剪，避免直接修改原始输入。 */
    (void)snprintf(work, sizeof(work), "%s", line);
    PurgeHostComm_Trim(work);
    if (work[0] == '\0')
    {
        return 1U;
    }

    /* FSR FC=0/1/2：状态、传感器、参数查询。 */
    if (sscanf(work, "FSR FC=%lu %31s", &fc, token1) == 1)
    {
        if (fc == 0UL)
        {
            PurgeHostComm_SendStatus(comm);
        }
        else if (fc == 1UL)
        {
            PurgeHostComm_SendSensors(comm);
        }
        else if (fc == 2UL)
        {
            (void)snprintf(tx,
                           sizeof(tx),
                           "FSD=2 FILLFLOW=%.2f RUNFLOW=%.2f TARGETO2=%.2f TARGETHUMI=%.2f POS_PRESS_MAX=%.2f POS_PRESS_MIN=%.2f NEG_PRESS_MAX=%.2f NEG_PRESS_MIN=%.2f EXTERNAL_OUTPUT=%lu GAS_TYPE=%s\r\n",
                           (double)g_purge_ctrl.fill_flow_lpm,
                           (double)g_purge_ctrl.run_flow_lpm,
                           (double)g_purge_ctrl.run_enter_o2_percent,
                           (double)g_purge_ctrl.run_exit_humidity_percent,
                           (double)g_purge_ctrl.max_inlet_pressure_bar,
                           (double)g_purge_ctrl.min_inlet_pressure_bar,
                           (double)g_purge_ctrl.max_outlet_pressure_bar,
                           (double)g_purge_ctrl.min_outlet_pressure_bar,
                           (unsigned long)g_purge_ctrl.external_output_flag,
                           (g_purge_ctrl.gas_type == XCDA) ? "XCDA" : "N2");
            PurgeHostComm_SendText(comm, tx);
        }
        else
        {
            PurgeHostComm_SendText(comm, "FSD=255 ERROR=DENEID\r\n");
        }
        return 1U;
    }

    /* HCS 控制命令：START_PURGE POD|MICRO、STOP_PURGE。 */
    token1[0] = '\0';
    token2[0] = '\0';
    token3[0] = '\0';
    if (sscanf(work, "HCS %31s %31s %31s", token1, token2, token3) >= 1)
    {
        if (PurgeHostComm_Equals(token1, "START_PURGE") != 0U)
        {
            /* 手册要求：参数缺失/未定义时返回 DENEID。 */
            if (token2[0] == '\0')
            {
                PurgeHostComm_SendText(comm, "HCA DENEID\r\n");
            }
            /* 设备已进入故障态时，控制命令拒绝执行。 */
            else if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FAULT) || (g_purge_ctrl.fault_code != 0UL))
            {
                PurgeHostComm_SendText(comm, "HCA ALARM\r\n");
            }
            /* 非待机态一律视为忙。 */
            else if (g_purge_ctrl.state != PURGE_CTRL_STATE_STANDBY)
            {
                PurgeHostComm_SendText(comm, "HCA BUSY\r\n");
            }
            /* 选择 POD 腔体并启动 purge。 */
            else if (PurgeHostComm_Equals(token2, "POD") != 0U)
            {
                PurgeControl_PodCavity();
                PurgeControl_Start();
                comm->ascii_purge_active = 1U;
                PurgeHostComm_SendText(comm, "HCA OK\r\n");
                if (comm->ascii_event_enable != 0U)
                {
                    PurgeHostComm_SendText(comm, "AERS START_PURGE\r\n");
                }
            }
            /* 选择 MICRO 腔体并启动 purge。 */
            else if (PurgeHostComm_Equals(token2, "MICRO") != 0U)
            {
                PurgeControl_MicroCavity();
                PurgeControl_Start();
                comm->ascii_purge_active = 1U;
                PurgeHostComm_SendText(comm, "HCA OK\r\n");
                if (comm->ascii_event_enable != 0U)
                {
                    PurgeHostComm_SendText(comm, "AERS START_PURGE\r\n");
                }
            }
            else
            {
                PurgeHostComm_SendText(comm, "HCA DENEID\r\n");
            }
            return 1U;
        }

        if (PurgeHostComm_Equals(token1, "STOP_PURGE") != 0U)
        {
            /* 与 START_PURGE 一样，故障态下返回 ALARM。 */
            if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FAULT) || (g_purge_ctrl.fault_code != 0UL))
            {
                PurgeHostComm_SendText(comm, "HCA ALARM\r\n");
            }
            else
            {
                /* 停止后同步结束本次 purge 跟踪。 */
                PurgeControl_Stop();
                comm->ascii_purge_active = 0U;
                PurgeHostComm_SendText(comm, "HCA OK\r\n");
                if (comm->ascii_event_enable != 0U)
                {
                    PurgeHostComm_SendText(comm, "AERS STOP_PURGE\r\n");
                }
            }
            return 1U;
        }

        PurgeHostComm_SendText(comm, "HCA DENEID\r\n");
        return 1U;
    }

    /* ECS：参数设置。仅允许待机/自检态修改。 */
    if (strncmp(work, "ECS ", 4) == 0)
    {
        if (PurgeHostComm_ParseKeyValue(&work[4], key, sizeof(key), value, sizeof(value)) == 0U)
        {
            PurgeHostComm_SendText(comm, "ECA DENEID\r\n");
            return 1U;
        }

        if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FAULT) || (g_purge_ctrl.fault_code != 0UL))
        {
            PurgeHostComm_SendText(comm, "ECA ALARM\r\n");
            return 1U;
        }

        if ((g_purge_ctrl.state != PURGE_CTRL_STATE_STANDBY) &&
            (g_purge_ctrl.state != PURGE_CTRL_STATE_SELF_CHECK))
        {
            PurgeHostComm_SendText(comm, "ECA BUSY\r\n");
            return 1U;
        }

        backup_fill_flow_lpm = g_purge_ctrl.fill_flow_lpm;
        backup_run_flow_lpm = g_purge_ctrl.run_flow_lpm;
        backup_run_enter_o2_percent = g_purge_ctrl.run_enter_o2_percent;
        backup_run_exit_humidity_percent = g_purge_ctrl.run_exit_humidity_percent;
        backup_max_inlet_pressure_bar = g_purge_ctrl.max_inlet_pressure_bar;
        backup_min_inlet_pressure_bar = g_purge_ctrl.min_inlet_pressure_bar;
        backup_max_outlet_pressure_bar = g_purge_ctrl.max_outlet_pressure_bar;
        backup_min_outlet_pressure_bar = g_purge_ctrl.min_outlet_pressure_bar;
        backup_external_output_flag = g_purge_ctrl.external_output_flag;
        backup_gas_type = g_purge_ctrl.gas_type;

        /* 按协议中支持的参数名一项项映射到控制层配置。 */
        if ((PurgeHostComm_Equals(key, "FILLFLOW") != 0U) &&
            (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.fill_flow_lpm = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "RUNFLOW") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.run_flow_lpm = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "TARGETO2") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.run_enter_o2_percent = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "TARGETHUMI") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.run_exit_humidity_percent = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "POS_PRESS_MAX") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.max_inlet_pressure_bar = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "POS_PRESS_MIN") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.min_inlet_pressure_bar = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "NEG_PRESS_MAX") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.max_outlet_pressure_bar = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "NEG_PRESS_MIN") != 0U) &&
                 (PurgeHostComm_TryParseFloat(value, &float_value) != 0U))
        {
            g_purge_ctrl.min_outlet_pressure_bar = float_value;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "EXTERNAL_OUTPUT") != 0U) &&
                 (PurgeHostComm_TryParseUint32(value, &uint_value) != 0U))
        {
            g_purge_ctrl.external_output_flag = (uint8_t)((uint_value != 0U) ? 1U : 0U);
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "GAS_TYPE") != 0U) &&
                 (PurgeHostComm_Equals(value, "N2") != 0U))
        {
            g_purge_ctrl.gas_type = N2;
            set_ok = 1U;
        }
        else if ((PurgeHostComm_Equals(key, "GAS_TYPE") != 0U) &&
                 (PurgeHostComm_Equals(value, "XCDA") != 0U))
        {
            g_purge_ctrl.gas_type = XCDA;
            set_ok = 1U;
        }

        if (set_ok != 0U)
        {
            if (PurgeControl_SaveConfig() != 0U)
            {
                PurgeHostComm_SendText(comm, "ECA OK\r\n");
                if (comm->ascii_event_enable != 0U)
                {
                    PurgeHostComm_SendText(comm, "AERS CMPL_SET\r\n");
                }
            }
            else
            {
                g_purge_ctrl.fill_flow_lpm = backup_fill_flow_lpm;
                g_purge_ctrl.run_flow_lpm = backup_run_flow_lpm;
                g_purge_ctrl.run_enter_o2_percent = backup_run_enter_o2_percent;
                g_purge_ctrl.run_exit_humidity_percent = backup_run_exit_humidity_percent;
                g_purge_ctrl.max_inlet_pressure_bar = backup_max_inlet_pressure_bar;
                g_purge_ctrl.min_inlet_pressure_bar = backup_min_inlet_pressure_bar;
                g_purge_ctrl.max_outlet_pressure_bar = backup_max_outlet_pressure_bar;
                g_purge_ctrl.min_outlet_pressure_bar = backup_min_outlet_pressure_bar;
                g_purge_ctrl.external_output_flag = backup_external_output_flag;
                g_purge_ctrl.gas_type = backup_gas_type;
                PurgeHostComm_SendText(comm, "ECA DENEID\r\n");
            }
        }
        else
        {
            PurgeHostComm_SendText(comm, "ECA DENEID\r\n");
        }
        return 1U;
    }

    if (strncmp(work, "ECR ", 4) == 0)
    {
        (void)snprintf(key, sizeof(key), "%s", &work[4]);
        PurgeHostComm_Trim(key);
        if (key[0] == '\0')
        {
            PurgeHostComm_SendText(comm, "ECD DENEID\r\n");
            return 1U;
        }

        /* ECR：逐项回读当前控制参数。 */
        if (PurgeHostComm_Equals(key, "FILLFLOW") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD FILLFLOW=%.2f\r\n", (double)g_purge_ctrl.fill_flow_lpm);
        }
        else if (PurgeHostComm_Equals(key, "RUNFLOW") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD RUNFLOW=%.2f\r\n", (double)g_purge_ctrl.run_flow_lpm);
        }
        else if (PurgeHostComm_Equals(key, "TARGETO2") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD TARGETO2=%.2f\r\n", (double)g_purge_ctrl.run_enter_o2_percent);
        }
        else if (PurgeHostComm_Equals(key, "TARGETHUMI") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD TARGETHUMI=%.2f\r\n", (double)g_purge_ctrl.run_exit_humidity_percent);
        }
        else if (PurgeHostComm_Equals(key, "POS_PRESS_MAX") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD POS_PRESS_MAX=%.2f\r\n", (double)g_purge_ctrl.max_inlet_pressure_bar);
        }
        else if (PurgeHostComm_Equals(key, "POS_PRESS_MIN") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD POS_PRESS_MIN=%.2f\r\n", (double)g_purge_ctrl.min_inlet_pressure_bar);
        }
        else if (PurgeHostComm_Equals(key, "NEG_PRESS_MAX") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD NEG_PRESS_MAX=%.2f\r\n", (double)g_purge_ctrl.max_outlet_pressure_bar);
        }
        else if (PurgeHostComm_Equals(key, "NEG_PRESS_MIN") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD NEG_PRESS_MIN=%.2f\r\n", (double)g_purge_ctrl.min_outlet_pressure_bar);
        }
        else if (PurgeHostComm_Equals(key, "EXTERNAL_OUTPUT") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD EXTERNAL_OUTPUT=%lu\r\n", (unsigned long)g_purge_ctrl.external_output_flag);
        }
        else if (PurgeHostComm_Equals(key, "GAS_TYPE") != 0U)
        {
            (void)snprintf(tx, sizeof(tx), "ECD GAS_TYPE=%s\r\n", (g_purge_ctrl.gas_type == XCDA) ? "XCDA" : "N2");
        }
        else
        {
            (void)snprintf(tx, sizeof(tx), "ECD DENEID\r\n");
        }
        PurgeHostComm_SendText(comm, tx);
        return 1U;
    }

    /* EDER：打开/关闭事件主动上报。故障态下拒绝执行。 */
    if (strncmp(work, "EDER ", 5) == 0)
    {
        (void)snprintf(token1, sizeof(token1), "%s", &work[5]);
        PurgeHostComm_Trim(token1);

        if ((g_purge_ctrl.state == PURGE_CTRL_STATE_FAULT) || (g_purge_ctrl.fault_code != 0UL))
        {
            PurgeHostComm_SendText(comm, "EERA ALARM\r\n");
        }
        else if (PurgeHostComm_Equals(token1, "ON") != 0U)
        {
            comm->ascii_event_enable = 1U;
            PurgeHostComm_SendText(comm, "EERA OK\r\n");
        }
        else if (PurgeHostComm_Equals(token1, "OFF") != 0U)
        {
            comm->ascii_event_enable = 0U;
            PurgeHostComm_SendText(comm, "EERA OK\r\n");
        }
        else
        {
            PurgeHostComm_SendText(comm, "EERA DENEID\r\n");
        }
        return 1U;
    }

    return 0U;
}

/*
 * 报警主动上报处理。
 *
 * 这里不是“当前 fault_code 非 0 就重复发送”，而是：
 * 仅当 fault_code 出现新增 bit 时，才上报对应的 ARS WARNING。
 */
static void PurgeHostComm_ProcessAsciiAlarms(PurgeHostComm_t *comm)
{
    uint32_t new_faults;

    if (comm == 0)
    {
        return;
    }

    new_faults = g_purge_ctrl.fault_code & (~comm->ascii_last_alarm_fault_code);
    if (new_faults == 0UL)
    {
        comm->ascii_last_alarm_fault_code = g_purge_ctrl.fault_code;
        return;
    }

    /* 0x00000001：控制流程兜底故障。 */
    if ((new_faults & PURGE_CTRL_FAULT_SENSOR_SAMPLE) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING DEVICE_FAULT ALID=0x00000001\r\n");
    }

    /* 0x00000010：正压异常。 */
    if ((new_faults & PURGE_CTRL_FAULT_INLET_OVERPRESSURE) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING POS_PRESS_ABNORMAL ALID=0x00000010\r\n");
    }

    /* 0x00000020：负压异常。 */
    if ((new_faults & PURGE_CTRL_FAULT_OUTLET_LOW_PRESSURE) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING NEG_PRESS_ABNORMAL ALID=0x00000020\r\n");
    }

    /* 0x00000004：流量反馈异常。 */
    if ((new_faults & PURGE_CTRL_FAULT_FLOW_INVALID) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING FLOW_ABNORMAL ALID=0x00000004\r\n");
    }

    /* 0x00000008：温湿度异常。 */
    if ((new_faults & PURGE_CTRL_FAULT_TEMP_HUMI_INVALID) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING TEMP_HUMI_ABNORMAL ALID=0x00000008\r\n");
    }

    /* 0x00000002：O2 长时间无有效更新。 */
    if ((new_faults & PURGE_CTRL_FAULT_O2_INVALID) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING O2_ABNORMAL ALID=0x00000002\r\n");
    }

    /* 0x00000040：RUN 阶段 O2 再次升高。 */
    if ((new_faults & PURGE_CTRL_FAULT_O2_TOO_HIGH) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING O2_ABNORMAL ALID=0x00000040\r\n");
    }

    /* 0x00000080：本次 purge 超时。 */
    if ((new_faults & PURGE_CTRL_FAULT_PURGE_TIMEOUT) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING PURGE_TIMEOUT ALID=0x00000080\r\n");
    }

    /* 0x00000100：RUN 阶段动态补偿后仍未恢复。 */
    if ((new_faults & PURGE_CTRL_FAULT_RUN_QUALITY_TIMEOUT) != 0UL)
    {
        PurgeHostComm_SendText(comm, "ARS WARNING DEVICE_FAULT ALID=0x00000100\r\n");
    }

    comm->ascii_last_alarm_fault_code = g_purge_ctrl.fault_code;
}

/*
 * 事件主动上报处理。
 *
 * 当前会处理三类事件：
 * 1. 上电事件 POWER_UP
 * 2. 参数设置完成事件 CMPL_SET（在 ECS 成功分支即时发送）
 * 3. purge 完成事件 CMPL_PURGE_POD / CMPL_PURGE_MIC
 */
static void PurgeHostComm_ProcessAsciiEvents(PurgeHostComm_t *comm, uint32_t now_ms)
{
    if (comm == 0)
    {
        return;
    }

    /* 上电事件只发一次。 */
    if ((comm->ascii_event_enable != 0U) && (comm->ascii_power_up_pending != 0U))
    {
        comm->ascii_power_up_pending = 0U;
        PurgeHostComm_SendText(comm, "AERS POWER_UP\r\n");
    }

    if (comm->ascii_event_enable == 0U)
    {
        return;
    }

    if (comm->ascii_purge_active == 0U)
    {
        return;
    }

    /* 已对当前 cycle 发过完成事件时，不再重复发送。 */
    if (comm->ascii_last_done_cycle == g_purge_ctrl.cycle_counter)
    {
        return;
    }

    /*
     * 完成判据与协议手册保持一致：
     * 1. 必须进入 RUN
     * 2. 湿度达到目标
     * 3. N2 模式还需 O2 达到目标
     * 4. XCDA 模式仅检查湿度
     */
    if ((g_purge_ctrl.state == PURGE_CTRL_STATE_RUN) &&
        (g_purge_ctrl.temp_humi_valid != 0U) &&
        (g_purge_ctrl.humidity_percent <= g_purge_ctrl.run_exit_humidity_percent) &&
        (((g_purge_ctrl.gas_type == N2) &&
          (g_purge_ctrl.o2_valid != 0U) &&
          (g_purge_ctrl.o2_percent <= g_purge_ctrl.run_enter_o2_percent)) ||
         (g_purge_ctrl.gas_type == XCDA)))
    {
        (void)now_ms;
        if (g_purge_ctrl.cavity == POD)
        {
            PurgeHostComm_SendText(comm, "AERS CMPL_PURGE_POD\r\n");
        }
        else
        {
            PurgeHostComm_SendText(comm, "AERS CMPL_PURGE_MIC\r\n");
        }

        comm->ascii_last_done_cycle = g_purge_ctrl.cycle_counter;
        comm->ascii_purge_active = 0U;
    }
}

/* 底层发送函数：统一做空指针、空串和统计处理。 */
static void PurgeHostComm_SendText(PurgeHostComm_t *comm, const char *text)
{
    uint16_t len;

    if ((comm == 0) || (text == 0))
    {
        return;
    }

    len = (uint16_t)strlen(text);
    if (len == 0U)
    {
        return;
    }

    (void)HAL_UART_Transmit(&huart3, (uint8_t *)text, len, 1000U);
    comm->tx_count++;
}

/* 发送内部错误提示，不属于对外协议正文。 */
static void PurgeHostComm_SendPromptErr(PurgeHostComm_t *comm, const char *tag)
{
    char tx[64];

    (void)snprintf(tx, sizeof(tx), "ERR:%s\r\n", tag);
    PurgeHostComm_SendText(comm, tx);
}

/* 去掉字符串首尾的空白字符，便于协议容错解析。 */
static void PurgeHostComm_Trim(char *text)
{
    size_t len;
    size_t start;

    if (text == 0)
    {
        return;
    }

    len = strlen(text);
    while ((len > 0U) && (isspace((unsigned char)text[len - 1U]) != 0))
    {
        text[len - 1U] = '\0';
        len--;
    }

    start = 0U;
    while (text[start] != '\0')
    {
        if (isspace((unsigned char)text[start]) == 0)
        {
            break;
        }
        start++;
    }

    if (start > 0U)
    {
        (void)memmove(text, &text[start], strlen(&text[start]) + 1U);
    }
}

/* 解析单个 KEY=VALUE 字段，供 ECS 使用。 */
static uint8_t PurgeHostComm_ParseKeyValue(const char *text, char *key, size_t key_size, char *value, size_t value_size)
{
    const char *equal_pos;
    size_t key_len;
    size_t value_len;

    if ((text == 0) || (key == 0) || (value == 0) || (key_size == 0U) || (value_size == 0U))
    {
        return 0U;
    }

    equal_pos = strchr(text, '=');
    if (equal_pos == 0)
    {
        return 0U;
    }

    key_len = (size_t)(equal_pos - text);
    value_len = strlen(equal_pos + 1U);
    if ((key_len == 0U) || (value_len == 0U) || (key_len >= key_size) || (value_len >= value_size))
    {
        return 0U;
    }

    (void)memcpy(key, text, key_len);
    key[key_len] = '\0';
    (void)snprintf(value, value_size, "%s", equal_pos + 1U);

    PurgeHostComm_Trim(key);
    PurgeHostComm_Trim(value);
    return ((key[0] != '\0') && (value[0] != '\0')) ? 1U : 0U;
}

/* 将纯数字/浮点数字符串解析为 float。 */
static uint8_t PurgeHostComm_TryParseFloat(const char *text, float *value)
{
    char *end_ptr;
    double temp;

    if ((text == 0) || (value == 0))
    {
        return 0U;
    }

    temp = strtod(text, &end_ptr);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return 0U;
    }

    *value = (float)temp;
    return 1U;
}

/* 将十进制数字符串解析为 uint32。 */
static uint8_t PurgeHostComm_TryParseUint32(const char *text, uint32_t *value)
{
    char *end_ptr;
    unsigned long temp;

    if ((text == 0) || (value == 0))
    {
        return 0U;
    }

    temp = strtoul(text, &end_ptr, 10);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return 0U;
    }

    *value = (uint32_t)temp;
    return 1U;
}

/* 简单字符串精确比较辅助函数。 */
static uint8_t PurgeHostComm_Equals(const char *a, const char *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    return (strcmp(a, b) == 0) ? 1U : 0U;
}

/* 将当前缓存的一行提取出来并送去解析。 */
static void PurgeHostComm_FinalizeLine(PurgeHostComm_t *comm)
{
    char line[128];

    if (comm == 0)
    {
        return;
    }

    if (comm->line_len == 0U)
    {
        return;
    }

    memcpy(line, comm->line_buf, comm->line_len);
    line[comm->line_len] = '\0';

    comm->line_len = 0U;
    comm->rx_line_count++;

    PurgeHostComm_HandleLine(comm, line);
}

/* 从环形缓冲区取 1 字节，供主循环消费。 */
static uint8_t PurgeHostComm_RingPop(PurgeHostComm_t *comm, uint8_t *byte)
{
    uint16_t head;
    uint16_t tail;

    if ((comm == 0) || (byte == 0))
    {
        return 0U;
    }

    head = comm->ring_head;
    tail = comm->ring_tail;

    if (head == tail)
    {
        return 0U;
    }

    *byte = comm->ring_buf[tail];
    comm->ring_tail = (uint16_t)((tail + 1U) % PURGE_HOSTCOMM_RING_SIZE);
    return 1U;
}

/* 在中断中向环形缓冲写入 1 字节；满时仅记溢出计数。 */
static void PurgeHostComm_RingPushIsr(PurgeHostComm_t *comm, uint8_t byte)
{
    uint16_t head;
    uint16_t tail;
    uint16_t next_head;

    if (comm == 0)
    {
        return;
    }

    head = comm->ring_head;
    tail = comm->ring_tail;
    next_head = (uint16_t)((head + 1U) % PURGE_HOSTCOMM_RING_SIZE);

    if (next_head == tail)
    {
        comm->rx_overflow_count++;
        return;
    }

    comm->ring_buf[head] = byte;
    comm->ring_head = next_head;
    comm->rx_byte_count++;
}
