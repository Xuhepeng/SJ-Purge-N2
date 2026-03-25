#include "../Inc/app_cytc_sth85.h"

uint8_t test = 0;
uint32_t serial_num = 0; //全局变量存储STH85序列号
uint8_t flag = 0;

/*
*@brief  CRC8位校验计算
*@param data 待校验数据
*@param length 数据长度
*/
uint8_t STH85_CRC8(uint8_t *data, uint16_t length)
{
    uint8_t crc = 0xFF; //初始值
    uint8_t i,j;
    
    for(i = 0;i < length;i++)
    {
        crc ^= data[i]; //与数据异或
        for(j = 0;j < 8;j++)
        {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1); //左移并根据最高位异或多项式
        }
    }
    return crc;
}

/*
*@brief STH85复位
*@param HAL_OK表示复位成功，HAL_ERROR表示复位失败
*/
HAL_StatusTypeDef STH85_Reset(void)
{
    uint8_t reset_cmd[2] = {(CMD_SOFT_RESET >> 8) & 0xFF, CMD_SOFT_RESET & 0xFF}; //复位命令高字节在前
    if(HAL_I2C_Master_Transmit(STH85_I2C_HANDLER, STH85_I2C_ADDR, reset_cmd, 2, 20) != HAL_OK)
    {
        flag = 1; //标记复位失败
        return HAL_ERROR;
    }
    HAL_Delay(10); //复位后等待10ms
    return HAL_OK;
}

/*
*@brief  读取STH85序列号
*/
HAL_StatusTypeDef STH85_ReadID(void)
{
    uint8_t recv_buf[6] = {0}; //接收缓冲区（序列号3字节+CRC1+CRC2）
    uint8_t read_id_cmd[2] = {(CMD_READ_SERIALNBR >> 8) & 0xFF, CMD_READ_SERIALNBR & 0xFF}; //读取序列号命令高字节在前
    if(HAL_I2C_Master_Transmit(STH85_I2C_HANDLER, STH85_I2C_ADDR, read_id_cmd, 2, 100) != HAL_OK)
    {
        flag = 2; //标记读取ID失败
        return HAL_ERROR; //发送命令失败
    }
    HAL_Delay(10); //等待传感器响应
    if(HAL_I2C_Master_Receive(STH85_I2C_HANDLER, STH85_I2C_ADDR, recv_buf, 6, 100) != HAL_OK)
    {
        flag = 3; //标记接收数据失败
        return HAL_ERROR; //接收数据失败
    }
    //CRC校验
    if(STH85_CRC8(recv_buf, 3) != recv_buf[3] || STH85_CRC8(&recv_buf[3], 3) != recv_buf[5])
    {
        flag = 4; //标记CRC校验失败
        return HAL_ERROR; //CRC校验失败
    }
    serial_num = (recv_buf[0] << 16) | (recv_buf[1] << 8) | recv_buf[2]; //序列号换算
    //printf("STH85 Serial Number: %lu\r\n", serial_num);
    return HAL_OK;
}

/*
*@brief  STH85配置测量方式
*@param void
*/
void STH85_ConfigMeasurement(void)
{
    uint8_t meas_cmd[2] = {(CMD_MEAS_PERI_1_H >> 8) & 0xFF, CMD_MEAS_PERI_1_H & 0xFF}; //测量命令高字节在前
    HAL_I2C_Master_Transmit(STH85_I2C_HANDLER, STH85_I2C_ADDR, meas_cmd, 2, 100);
}

/*
*@brief  STH85初始化
*@param void
*@return HAL_OK表示初始化成功，HAL_ERROR表示初始化失败
*/
HAL_StatusTypeDef STH85_Init(void)
{
    if(STH85_Reset() != HAL_OK)
    {
        return HAL_ERROR;
    }
    if(STH85_ReadID() != HAL_OK)
    {
        return HAL_ERROR;
    }
    STH85_ConfigMeasurement();
    return HAL_OK;
}


/*
*@brief  连续测量模式采集温湿度
*@param 无
*@return STH85_Data_t结构体，包含温度、湿度和数据有效标志
*/
STH85_Data_t STH85_ReadContinuous(void)
{
    STH85_Data_t data = {0,0,0}; //初始化数据结构体
    uint8_t recv_buf[6] = {0}; //接收缓冲区（温度2字节+湿度2字节+CRC1字节）

    //1.发送连续测量命令
    uint8_t fetch_cmd[2] = {(CMD_FETCH_DATA >> 8) & 0xFF, CMD_FETCH_DATA & 0xFF}; //获取数据命令高字节在前
    if(HAL_I2C_Master_Transmit(STH85_I2C_HANDLER, STH85_I2C_ADDR, fetch_cmd, 2, 100) != HAL_OK)
    {
        data.valid = 0; //标记数据无效
        return data; //发送失败，返回默认数据
    }
    
    //2.等待测量完成
    HAL_Delay(15);

    // //3.发送读取数据命令(0xE000)
    // uint8_t read_cmd[2] = {0xE0, 0x00};
    // if(HAL_I2C_Master_Transmit(STH85_I2C_HANDLER, STH85_I2C_ADDR, read_cmd, 2, 100) != HAL_OK)
    // {
    //     data.valid = 0; //标记数据无效
    //     return data; //发送失败，返回默认数据
    // }

    //4.接收6字节数据（温度2字节+CRC1+湿度2字节+CRC2）
    if(HAL_I2C_Master_Receive(STH85_I2C_HANDLER, STH85_I2C_ADDR, recv_buf, 6, 100) != HAL_OK)
    {
        data.valid = 0; //标记数据无效
        return data; //接收失败，返回默认数据
    }

    //5.CRC校验
    if(STH85_CRC8(recv_buf, 2) != recv_buf[2] || STH85_CRC8(&recv_buf[3], 2) != recv_buf[5])
    {
        data.valid = 0; //标记数据无效
        return data; //CRC校验失败，返回默认数据
    }

    //6.数据换算
    uint16_t temp_raw = (recv_buf[0] << 8) | recv_buf[1]; //温度原始值
    uint16_t hum_raw = (recv_buf[3] << 8) | recv_buf[4]; //湿度原始值
    data.temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f); //温度换算
    data.humidity = 100.0f * ((float)hum_raw / 65535.0f); //湿度换算
    data.valid = 1; //标记数据有效

    // Implementation for reading single shot data
    return data;
}