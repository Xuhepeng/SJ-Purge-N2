#ifndef __app_cytc_sth85_H
#define __app_cytc_sth85_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f4xx_hal.h"
//#include "i2c.h"

#define STH85_I2C_ADDR 0x44 //SHT85 7位I2C地址（I2C配置为7BIT寻址）
#define STH85_SINGLE_H_CMD 0x2400 //单拍高重复性指令（高精度）
#define STH85_SINGLE_M_CMD 0x240B //单拍中重复性指令（中精度）
#define STH85_SINGLE_L_CMD 0x2416 //单拍低重复性指令（快速响应）
#define STH85_READ_DATA 0xE000 //读取测量结果指令

#define  CMD_READ_SERIALNBR     0x3682    // read serial number
#define  CMD_READ_STATUS        0xF32D    // read status register
#define  CMD_CLEAR_STATUS       0x3041    // clear status register
#define  CMD_HEATER_ENABLE      0x306D    // enabled heater
#define  CMD_HEATER_DISABLE     0x3066    // disable heater
#define  CMD_SOFT_RESET         0x30A2    // soft reset
#define  CMD_MEAS_SINGLE_H      0x2400    // single meas., high repeatability
#define  CMD_MEAS_SINGLE_M      0x240B    // single meas., medium repeatability
#define  CMD_MEAS_SINGLE_L      0x2416    // single meas., low repeatability
#define  CMD_MEAS_PERI_05_H     0x2032    // periodic meas. 0.5 mps, high repeatability
#define  CMD_MEAS_PERI_05_M     0x2024    // periodic meas. 0.5 mps, medium repeatability
#define  CMD_MEAS_PERI_05_L     0x202F    // periodic meas. 0.5 mps, low repeatability
#define  CMD_MEAS_PERI_1_H      0x2130    // periodic meas. 1 mps, high repeatability
#define  CMD_MEAS_PERI_1_M      0x2126    // periodic meas. 1 mps, medium repeatability
#define  CMD_MEAS_PERI_1_L      0x212D    // periodic meas. 1 mps, low repeatability
#define  CMD_MEAS_PERI_2_H      0x2236    // periodic meas. 2 mps, high repeatability
#define  CMD_MEAS_PERI_2_M      0x2220    // periodic meas. 2 mps, medium repeatability
#define  CMD_MEAS_PERI_2_L      0x222B    // periodic meas. 2 mps, low repeatability
#define  CMD_MEAS_PERI_4_H      0x2334    // periodic meas. 4 mps, high repeatability
#define  CMD_MEAS_PERI_4_M      0x2322    // periodic meas. 4 mps, medium repeatability
#define  CMD_MEAS_PERI_4_L      0x2329    // periodic meas. 4 mps, low repeatability
#define  CMD_MEAS_PERI_10_H     0x2737    // periodic meas. 10 mps, high repeatability
#define  CMD_MEAS_PERI_10_M     0x2721    // periodic meas. 10 mps, medium repeatability
#define  CMD_MEAS_PERI_10_L     0x272A    // periodic meas. 10 mps, low repeatability
#define  CMD_FETCH_DATA         0xE000    // readout measurements for periodic mode
#define  CMD_BREAK              0x3093    // stop periodic measurement
//I2C句柄（根据实际使用的I2C接口调整）
#define STH85_I2C_HANDLER &hi2c2

//温湿度数据结构体
typedef struct {
    float temperature; //温度值（°C）
    float humidity;    //湿度值（%RH）
    uint8_t valid;       //数据有效标志（1表示有效，0表示无效）
} STH85_Data_t;


uint8_t STH85_CRC8(uint8_t *data, uint16_t length);
HAL_StatusTypeDef STH85_Reset(void);
HAL_StatusTypeDef STH85_ReadID(void);
HAL_StatusTypeDef STH85_Init(void);
STH85_Data_t STH85_ReadContinuous(void);

#endif /*__app_cytc_sth85_H */