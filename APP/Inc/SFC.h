#ifndef __SFC_H
#define __SFC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "usart.h"
#include "Modbus_o2.h"

/* SFC 与 O2 当前共用同一串口总线，这里实际走的是 huart1。 */
#define SFC_UART_HANDLER   &huart1

/* Modbus 从机地址 */
#define SFC_SLAVE_ADDR     0x02

/* SFC 写设定值的寄存器地址 */
#define SFC_REG_SET_ADDR   0x0000

/* SFC 读回当前流量的寄存器地址 */
#define SFC_REG_READ_ADDR  0x0000

/* 一次读取的寄存器数量，当前只读 1 个寄存器 */
#define SFC_REG_NUM        0x0001

/* 协议里流量值放大 100 倍传输，例如 50.00 L/min -> 5000 */
#define FLOW_MULTIPLIER    100

/* 使用专用名字，避免和 My_ADC_ReadData.h 中的 FLOW_MIN/FLOW_MAX 冲突 */
#define SFC_FLOW_MIN       4.0f
#define SFC_FLOW_MAX       200.0f

/* Modbus 功能码 */
#define SFC_FUNC_CODE_WRITE 0x06
#define SFC_FUNC_CODE_READ  0x04

/* 串口收发超时时间 */
#define SFC_TIMEOUT_MS      500

/* CRC16 复用 O2 模块中的实现 */
extern uint16_t Modbus_CRC16(uint8_t *data, uint16_t length);

/*
 * 设定 SFC 目标流量。
 * 输入单位：L/min
 * 返回值：
 * - HAL_OK：写入成功且回包校验通过
 * - HAL_ERROR：范围错误、发送失败或回包异常
 */
HAL_StatusTypeDef SFC_SetFlowValue(float flow_value);

/*
 * 读取 SFC 当前反馈流量。
 * 输出单位：L/min
 * 返回值：
 * - HAL_OK：读取成功
 * - HAL_ERROR：发送失败、接收失败或回包格式不对
 */
HAL_StatusTypeDef SFC_ReadFlowValue(float *flow_value);

#ifdef __cplusplus
}
#endif

#endif /* __SFC_H */
