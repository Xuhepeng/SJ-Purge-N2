#ifndef __Modbus_o2_H
#define __Modbus_o2_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "usart.h"

/*氧气传感器Modbus参数配置*/
#define O2_SENSOR_ADDR 0x01 //从机地址
#define O2_REG_ADDR 0x0000 //寄存器地址
#define O2_REG_LEN 0x0001  //读取寄存器长度
#define O2_UART_HANDLER &huart1 //使用的UART句柄(接RS485)
#define O2_TIMEOUT_MS 1000 //UART通信超时时间

/*函数声明*/
//CRC16校验计算
uint16_t Modbus_CRC16(uint8_t *data, uint16_t length);
//组装Modbus RTU请求帧
void Modbus_AssembleReqFrame(uint8_t *req_frame);
//读取氧气浓度（返回值：氧气浓度%，返回-1表示通信失败）
float O2_Sensor_ReadConcentration(void);

#endif /*__Modbus_o2_H */

