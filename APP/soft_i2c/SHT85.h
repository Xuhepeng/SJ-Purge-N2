#ifndef _SHT85_H_
#define _SHT85_H_

#define SHT85_ADDR 0x44         //1000100

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

#include "SOFT_I2C.h"

extern uint32_t SHT85_ID;
extern float Temperature;
extern float Humidity;

void SHT85_Start(void);
void SHT85_Stop(void);
I2C_ACK_TypeDef SHT85_Cmd_Write(void);
I2C_ACK_TypeDef SHT85_Cmd_Read(void);
I2C_ACK_TypeDef SHT85_Send_Cmd(uint16_t cmd);
I2C_ACK_TypeDef SHT85_Reset(void);
I2C_ACK_TypeDef SHT85_Read_ID(void);
void SHT85_Init(void);
I2C_ACK_TypeDef SHT85_Read_Result(void);

float Humidity_Offset(float hum);

#endif