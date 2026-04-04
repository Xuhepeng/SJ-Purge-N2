#ifndef __PURGE_HOSTCOMM_H
#define __PURGE_HOSTCOMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "Purge_Control.h"

/* UART3 接收环形缓冲区大小。 */
#define PURGE_HOSTCOMM_RING_SIZE 256U

/*
 * 主机通信上下文。
 *
 * 该结构体由主循环持有，负责完成以下职责：
 * 1. UART3 字节接收与环形缓冲缓存
 * 2. 一行 ASCII 命令的拼包
 * 3. 协议统计信息记录
 * 4. 事件/报警上报状态保持
 */
typedef struct
{
    /* 当前正在拼接的一行 ASCII 报文缓存。 */
    uint8_t line_buf[128];
    /* line_buf 当前有效长度。 */
    uint16_t line_len;
    /* 最近一次收到字节的系统时刻，用于空闲超时收帧。 */
    uint32_t last_rx_tick;
    /* 报文帧空闲超时，超过该时间认为一帧结束。 */
    uint32_t frame_idle_timeout_ms;

    /* UART ISR 写入、主循环读取的环形缓冲区。 */
    uint8_t ring_buf[PURGE_HOSTCOMM_RING_SIZE];
    /* 环形缓冲写指针，由中断推进。 */
    volatile uint16_t ring_head;
    /* 环形缓冲读指针，由主循环推进。 */
    volatile uint16_t ring_tail;

    /* 已接收完整行计数。 */
    uint32_t rx_line_count;
    /* 已发送报文计数。 */
    uint32_t tx_count;
    /* 已接收字节总数。 */
    uint32_t rx_byte_count;
    /* 环形缓冲溢出次数。 */
    uint32_t rx_overflow_count;

    /* 事件主动上报开关，对应 EDER ON/OFF。 */
    uint8_t ascii_event_enable;
    /* 上电事件待上报标志，初始化后首次 Process 时发出 POWER_UP。 */
    uint8_t ascii_power_up_pending;

    /* 当前是否处于一次 purge 流程的跟踪周期内。 */
    uint8_t ascii_purge_active;
    /* 上一次已上报完成事件的 cycle 号，用于避免重复上报。 */
    uint32_t ascii_last_done_cycle;
    /* 上一次已上报的 fault_code 快照，用于只上报新增故障。 */
    uint32_t ascii_last_alarm_fault_code;

    /* UART3 中断单字节接收临时缓存。 */
    uint8_t uart_rx_byte;
} PurgeHostComm_t;

/* 初始化主机通信模块，并启动 UART3 单字节中断接收。 */
void PurgeHostComm_Init(PurgeHostComm_t *comm);
/* 在主循环中周期调用，处理接收、命令解析、事件与报警上报。 */
void PurgeHostComm_Process(PurgeHostComm_t *comm);
/* 喂入单个接收字节，完成按行组帧。 */
void PurgeHostComm_FeedByte(PurgeHostComm_t *comm, uint8_t byte);
/* 发送 FC=0 状态查询应答。 */
void PurgeHostComm_SendStatus(PurgeHostComm_t *comm);
/* 发送 FC=1 传感器查询应答。 */
void PurgeHostComm_SendSensors(PurgeHostComm_t *comm);
/* 启动 UART3 1 字节中断接收。 */
void PurgeHostComm_StartUart3RxIT(PurgeHostComm_t *comm);
/* UART3 接收完成回调入口，由 HAL 回调间接调用。 */
void PurgeHostComm_OnUart3RxCplt(void);

#ifdef __cplusplus
}
#endif

#endif /* __PURGE_HOSTCOMM_H */
