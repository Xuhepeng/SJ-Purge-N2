#include "My_ADC_ReadData.h"

uint16_t val = 0; //ADC原始值
float in_pressure = 0.0f; //实际进气压力值
float out_pressure = 0.0f; //实际出气压力值
float Flow_value = 0.0f; //实际流量值
/**
 * @brief 读取ADC单通道值
 * @param adc_channel ADC通道（如INTAKE_PRESSURE_ADC_CHANNEL或EXHAUST_PRESSURE_ADC_CHANNEL）
 * @return ADC原始值（0~4095）
 */
uint16_t ADC_Read_Single(uint32_t adc_channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    
    //配置ADC通道
    sConfig.Channel = adc_channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES; //采样时间根据需要调整
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler(); //配置失败，进入错误处理
    }
    
    //启动ADC转换
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        Error_Handler(); //启动失败，进入错误处理
    }
    
    //等待转换完成
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) //等待10ms
    {
        val = HAL_ADC_GetValue(&hadc1); //读取ADC原始值
        return val; //返回ADC原始值
    }
    else
    {
        return 0; //转换失败，返回0
    }
    
}

//带均值滤波的ADC读取函数
float ADC_Read_Filter(uint32_t adc_channel)
{
    uint32_t sum = 0;
    for (int i = 0; i < FILTER_COUNT; i++)
    {
        sum += ADC_Read_Single(adc_channel); //累加多次读取的ADC值
    }
    return (float)sum / FILTER_COUNT; //返回平均值
}

/**
 * @brief 获取进气压力值（单位：Bar）
 * @param adc_value ADC原始值（0~4095）
 * @return 实际压力值（单位：Bar）
 */
float Get_Inlet_Pressure_Bar(void)
{
    float adc_value = ADC_Read_Filter(INTAKE_PRESSURE_ADC_CHANNEL); //读取进气压力ADC值
    //1.把ADC转换成ADC引脚的实际电压
    float adc_voltage = (float)adc_value*1.1f * ADC_VREF / ADC_RESOLUTION; //ADC电压值

    //2.还原成传感器原始输出（硬件有分压）
    float sensor_voltage = adc_voltage * VOLTAGE_DIVIDER; //传感器输出电压

    //3.电压->压力（0~10V对应0-10Bar）
    //pressure = (sensor_voltage - 0.0f) / (10.0f - 0.0f) * (PRESSURE_MAX_BAR - PRESSURE_MIN_BAR) + PRESSURE_MIN_BAR;
    in_pressure = sensor_voltage * (IN_P_MAX_BAR / 10.0f);
    return in_pressure;
}

/**
 * @brief 获取出气压力值（单位：Bar）
 * @return 出气压力值（单位：Bar）
 */
float Get_Outlet_Pressure_Bar(void)
{
    float adc_value = ADC_Read_Filter(EXHAUST_PRESSURE_ADC_CHANNEL); //读取出气压力ADC值
    //1.把ADC转换成ADC引脚的实际电压
    float adc_voltage = (float)adc_value*1.1f * ADC_VREF / ADC_RESOLUTION; //ADC电压值
    //2.还原成传感器原始输出（硬件有分压）
    float sensor_voltage = adc_voltage * VOLTAGE_DIVIDER; //传感器输出电压
    //3.0-10v线性映射到-1-+1bar
    out_pressure = OUT_P_MIN_BAR+(sensor_voltage / 10.0f) * (OUT_P_MAX_BAR - OUT_P_MIN_BAR);
    return out_pressure; 
}

/**
 * @brief 获取流量值（单位：L/min）
 * @return 流量值（单位：L/min）
 */
float Get_Flow_LPM(void)
{
    //通道：PA5 = ADC1_IN5
    float adc_value = ADC_Read_Filter(ADC_CHANNEL_5); //读取流量计ADC值
    //1.把ADC转换成ADC引脚的实际电压
    float adc_voltage = (float)adc_value*1.1f * ADC_VREF / ADC_RESOLUTION; //ADC电压值
    //2.还原成传感器原始输出（硬件有分压）
    float sensor_voltage = adc_voltage * VOLTAGE_DIVIDER; //传感器输出电压
    //3.0-10v线性映射到2-200L/min
    Flow_value = FLOW_MIN+(sensor_voltage / 10.0f) * (FLOW_MAX - FLOW_MIN);

    if(Flow_value < FLOW_MIN) Flow_value = FLOW_MIN; //限制最小值,避免电压波动出现小于2L/min的情况
    
    return Flow_value;
}

float Apply_Calib(float raw, Calib_TypeDef *calib)
{
    return raw * calib->k + calib->b;
}

//零点校准
void Calib_Zero(Calib_TypeDef *calib, float current_raw, float target_true)
{
    calib->b = target_true - current_raw;
    calib->k = 1.0f;
}


void Get_ADC_Data(void)
{
    Get_Inlet_Pressure_Bar(); //获取进气压力值
    Get_Outlet_Pressure_Bar(); //获取出气压力值
    Get_Flow_LPM(); //获取流量值
}