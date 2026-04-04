#ifndef __MY_ADC_READDATA_H
#define __MY_ADC_READDATA_H

#ifdef __cplusplus
 extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#include "stm32f4xx_hal.h"
#include "adc.h"

#define INTAKE_PRESSURE_ADC_CHANNEL ADC_CHANNEL_2 //进气压力通道
#define EXHAUST_PRESSURE_ADC_CHANNEL ADC_CHANNEL_1 //出气压力通道

//进气压力传感器：0~10bar
#define IN_P_MIN_BAR 0.0f //压力最小值（单位：Bar，根据实际传感器范围调整）
#define IN_P_MAX_BAR 10.0f //压力最大值（单位：Bar，根据实际传感器范围调整）

//出气压力传感器：-1~+1bar
#define OUT_P_MIN_BAR -1.0f //压力最小值（单位：Bar，根据实际传感器范围调整）
#define OUT_P_MAX_BAR 1.0f //压力最大值（单位：Bar，根据实际传感器范围调整）

//流量计：2 ~ 200L/min
#define FLOW_MIN 0.0f //流量最小值（单位：L/min，根据实际传感器范围调整）
#define FLOW_MAX 200.0f //流量最大值（单位：L/min，根据实际传感器范围调整）

//ADC参考电压3.3v
#define ADC_VREF 3.3f
#define ADC_RESOLUTION 4096.0f //12位ADC分辨率

//硬件分压比例
#define VOLTAGE_DIVIDER 3.03f //10/3.3 = 3.03

//滤波次数
#define FILTER_COUNT 10

//校准结构体
typedef struct{
    float k;//增益
    float b;//偏移
}Calib_TypeDef;

uint16_t ADC_Read_Single(uint32_t adc_channel);
float Get_Inlet_Pressure_Bar(void); //获取进气压力值（单位：Bar）
float Get_Outlet_Pressure_Bar(void); //获取出气压力值（单位：Bar）
float Get_Flow_LPM(void); //获取流量值（单位：L/min）
void Get_ADC_Data(void);

#endif /* __MY_ADC_READDATA_H */