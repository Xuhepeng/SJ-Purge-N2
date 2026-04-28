#ifndef __PURGE_HOSTCOMM_H
#define __PURGE_HOSTCOMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "Purge_Control.h"

#define PURGE_HOSTCOMM_RING_SIZE 256U

typedef struct
{
    /* 当前正在拼接的一行 ASCII 命令缓存。 */
    uint8_t line_buf[128];
    /* line_buf 当前有效长度。 */
    uint16_t line_len;
    /* 最近一个收到的字节是否为 '\r'，用于识别 CRLF 结束符。 */
    uint8_t rx_prev_was_cr;

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
    /* POD + N2 完成条件首次满足的时刻，用于延迟上报完成事件。 */
    uint32_t ascii_done_qualify_tick;
    /* 上一次已上报完成事件的 cycle 号，用于避免重复上报。 */
    uint32_t ascii_last_done_cycle;
    /* 上一次已上报的 fault_code 快照，用于只上报新增故障。 */
    uint32_t ascii_last_alarm_fault_code;

    /* UART3 中断单字节接收临时缓存。 */
    uint8_t uart_rx_byte;
} PurgeHostComm_t;

void PurgeHostComm_Init(PurgeHostComm_t *comm);
void PurgeHostComm_Process(PurgeHostComm_t *comm);
void PurgeHostComm_FeedByte(PurgeHostComm_t *comm, uint8_t byte);
void PurgeHostComm_SendStatus(PurgeHostComm_t *comm);
void PurgeHostComm_SendSensors(PurgeHostComm_t *comm);
void PurgeHostComm_StartUart3RxIT(PurgeHostComm_t *comm);
void PurgeHostComm_OnUart3RxCplt(void);

#ifdef __cplusplus
}
#endif

#endif /* __PURGE_HOSTCOMM_H */
