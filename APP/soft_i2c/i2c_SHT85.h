#ifndef __i2c_SHT85_H__
#define __i2c_SHT85_H__
#include "main.h"
 
#define CRC_POLYNOMIAL  0x131 // P(x) = x^8 + x^5 + x^4 + 1 = 100110001
#define I2C_ADDR        0x44  //SHT85地址
 
/****************************错误码***************************/
#define NO_ERROR        0x00
#define ACK_ERROR       0x01
#define CHECKSUM_ERROR  0x02
#define TIMEOUT_ERROR   0x04
 
 
/****************************IIC错误码***************************/
typedef enum{
  ACK    = 0,
  NO_ACK = 1,
}etI2cAck;
 
/****************************CMD定义***************************/
typedef enum {
  CMD_READ_SERIALNBR = 0x3780, // read serial number
  CMD_READ_STATUS    = 0xF32D, // read status register
  CMD_CLEAR_STATUS   = 0x3041, // clear status register
  CMD_HEATER_ENABLE  = 0x306D, // enabled heater
  CMD_HEATER_DISABLE = 0x3066, // disable heater
  CMD_SOFT_RESET     = 0x30A2, // soft reset
  CMD_MEAS_SINGLE_H  = 0x2400, // single meas., high repeatability
  CMD_MEAS_SINGLE_M  = 0x240B, // single meas., medium repeatability
  CMD_MEAS_SINGLE_L  = 0x2416, // single meas., low repeatability
  CMD_MEAS_PERI_05_H = 0x2032, // periodic meas. 0.5 mps, high repeatability
  CMD_MEAS_PERI_05_M = 0x2024, // periodic meas. 0.5 mps, medium repeatability
  CMD_MEAS_PERI_05_L = 0x202F, // periodic meas. 0.5 mps, low repeatability
  CMD_MEAS_PERI_1_H  = 0x2130, // periodic meas. 1 mps, high repeatability
  CMD_MEAS_PERI_1_M  = 0x2126, // periodic meas. 1 mps, medium repeatability
  CMD_MEAS_PERI_1_L  = 0x212D, // periodic meas. 1 mps, low repeatability
  CMD_MEAS_PERI_2_H  = 0x2236, // periodic meas. 2 mps, high repeatability
  CMD_MEAS_PERI_2_M  = 0x2220, // periodic meas. 2 mps, medium repeatability
  CMD_MEAS_PERI_2_L  = 0x222B, // periodic meas. 2 mps, low repeatability
  CMD_MEAS_PERI_4_H  = 0x2334, // periodic meas. 4 mps, high repeatability
  CMD_MEAS_PERI_4_M  = 0x2322, // periodic meas. 4 mps, medium repeatability
  CMD_MEAS_PERI_4_L  = 0x2329, // periodic meas. 4 mps, low repeatability
  CMD_MEAS_PERI_10_H = 0x2737, // periodic meas. 10 mps, high repeatability
  CMD_MEAS_PERI_10_M = 0x2721, // periodic meas. 10 mps, medium repeatability
  CMD_MEAS_PERI_10_L = 0x272A, // periodic meas. 10 mps, low repeatability
  CMD_FETCH_DATA     = 0xE000, // readout measurements for periodic mode
  CMD_BREAK          = 0x3093, // stop periodic measurement
}etCommands;
 
// Single Shot Measurement Repeatability
typedef enum {
  SINGLE_MEAS_LOW        = CMD_MEAS_SINGLE_L, // low repeatability
  SINGLE_MEAS_MEDIUM     = CMD_MEAS_SINGLE_M, // medium repeatability
  SINGLE_MEAS_HIGH       = CMD_MEAS_SINGLE_H  // high repeatability
}etSingleMeasureModes;
 
// Periodic Measurement Configurations
typedef enum {
  PERI_MEAS_LOW_05_HZ    = CMD_MEAS_PERI_05_L,
  PERI_MEAS_LOW_1_HZ     = CMD_MEAS_PERI_1_L,
  PERI_MEAS_LOW_2_HZ     = CMD_MEAS_PERI_2_L,
  PERI_MEAS_LOW_4_HZ     = CMD_MEAS_PERI_4_L,
  PERI_MEAS_LOW_10_HZ    = CMD_MEAS_PERI_10_L,
  PERI_MEAS_MEDIUM_05_HZ = CMD_MEAS_PERI_05_M,
  PERI_MEAS_MEDIUM_1_HZ  = CMD_MEAS_PERI_1_M,
  PERI_MEAS_MEDIUM_2_HZ  = CMD_MEAS_PERI_2_M,
  PERI_MEAS_MEDIUM_4_HZ  = CMD_MEAS_PERI_4_M,
  PERI_MEAS_MEDIUM_10_HZ = CMD_MEAS_PERI_10_M,
  PERI_MEAS_HIGH_05_HZ   = CMD_MEAS_PERI_05_H,
  PERI_MEAS_HIGH_1_HZ    = CMD_MEAS_PERI_1_H,
  PERI_MEAS_HIGH_2_HZ    = CMD_MEAS_PERI_2_H,
  PERI_MEAS_HIGH_4_HZ    = CMD_MEAS_PERI_4_H,
  PERI_MEAS_HIGH_10_HZ   = CMD_MEAS_PERI_10_H,
}etPeriodicMeasureModes;
 
 
/****************************温湿度传感器结构体定义**************************/
typedef struct _TH_Class{
  uint32_t SerialNumber;//传感器SN号
  float Temperature;//温度
  float Humidity;//相对湿度
  float H2O;//绝对湿度
 
  void (*init)(void);//初始化函数指针
  void (*loop)(void);//loop函数指针，循环读取温湿度，周期:1S
}TH_Class_t;
 
extern TH_Class_t TH_Class_SHT85;
 
 
#endif