#ifndef __SFC_H
#define __SFC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "usart.h"
#include "Modbus_o2.h"

#define SFC_UART_HANDLER &huart1
#define SFC_SLAVE_ADDR 0x02 // Modbus从机地址（根据实际情况调整）
#define SFC_REG_SET_ADDR 0x0000 // 流量设置寄存器地址0：设置寄存器
#define SFC_REG_READ_ADDR 0x0000 // 流量读取寄存器地址1：读取寄存器
#define SFC_REG_NUM 0x0001
#define FLOW_MULTIPLIER 100 // 数值放大/缩小倍数
#define FLOW_MIN 4.0f // 最小流量值
#define FLOW_MAX 200.0f // 最大流量值
#define SFC_FUNC_CODE_WRITE 0x06 // 写单个寄存器功能码
#define SFC_FUNC_CODE_READ 0x04 // 读保持寄存器功能码
#define SFC_TIMEOUT_MS 500 // UART通信超时时间

extern uint16_t Modbus_CRC16(uint8_t *data, uint16_t length);

//流量设定函数
HAL_StatusTypeDef SFC_SetFlowValue(float flow_value);

//流量读取函数
HAL_StatusTypeDef SFC_ReadFlowValue(float *flow_value);

#endif /* __SFC_H */