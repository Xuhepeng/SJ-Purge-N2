#include "Modbus_o2.h"
#include "string.h"


uint8_t req_frame[8] = {0}; //请求帧数组
uint8_t resp_frame[8] = {0}; //响应帧数组
uint16_t o2_raw = 0; //原始氧气浓度值（整数形式）

/**
 * @brief 计算CRC16校验码
 * @param data 待校验数据
 * @param length 数据长度
 * @return 16位CRC校验码
 */
uint16_t Modbus_CRC16(uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF; //初始值
    uint8_t i,j;
    
    for(i = 0;i < length;i++)
    {
        crc ^= data[i]; //与数据异或
        for(j = 0;j < 8;j++)
        {
            if(crc & 0x0001) //如果最低位为1
                crc = (crc >> 1) ^ 0xA001; //右移并与多项式异或
            else
                crc >>= 1; //仅右移
        }
    }
    return crc;

}

/**
 * @brief 组装Modbus RTU请求帧
 * @param req_frame 存储请求帧的数组（长度至少8字节）
 * @return 无
 */
void Modbus_AssembleReqFrame(uint8_t *req_frame)
{
    //清空数组
    memset(req_frame, 0, 8);

    //组装请求帧核心内容（前6字节）
    req_frame[0] = O2_SENSOR_ADDR; //从机地址
    req_frame[1] = 0x04; //功能码（读输入寄存器）
    req_frame[2] = (O2_REG_ADDR >> 8) & 0xFF; //寄存器地址高8位
    req_frame[3] = O2_REG_ADDR & 0xFF; //寄存器地址低8位
    req_frame[4] = (O2_REG_LEN >> 8) & 0xFF; //读取寄存器长度高8位
    req_frame[5] = O2_REG_LEN & 0xFF; //读取寄存器长度低8位

    //计算CRC16并填充（低字节在前，高字节在后）
    uint16_t crc = Modbus_CRC16(req_frame, 6);
    req_frame[6] = crc & 0xFF; //CRC低字节
    req_frame[7] = (crc >> 8) & 0xFF; //CRC高字节
}

/**
 * @brief 读取氧气浓度
 * @return 氧气浓度百分比（单位：%），通信失败返回-1
 */
float O2_Sensor_ReadConcentration(void)
{
    // uint8_t req_frame[8] = {0}; //请求帧数组
    // uint8_t resp_frame[8] = {0}; //响应帧数组
    float o2_conc = -1.0f; //默认返回值（通信失败）
    //memset(resp_frame, 0, 8); //清空响应帧
    Modbus_AssembleReqFrame(req_frame); //组装请求帧
    if(HAL_UART_Transmit(O2_UART_HANDLER, req_frame, 8, O2_TIMEOUT_MS) != HAL_OK)
    {
        return o2_conc; //发送失败
    }

    //3.等待并接收响应帧
    if(HAL_UART_Receive(O2_UART_HANDLER, resp_frame, 7, O2_TIMEOUT_MS) != HAL_OK)
    {
        return o2_conc; //接收失败
    }

    //4.校验响应帧合法性
    //检查地址、功能码、数据长度
    if(resp_frame[0] != O2_SENSOR_ADDR || resp_frame[1] != 0x04 || resp_frame[2] != 0x02)
    {
        return o2_conc; //响应帧格式错误
    }

    //5.校验CRC
    uint16_t crc_calc = Modbus_CRC16(resp_frame, 5);
    uint16_t crc_recv = (resp_frame[6] << 8) | resp_frame[5]; //CRC高字节在后
    if(crc_calc != crc_recv)
    {
        return o2_conc; //CRC校验失败
    }

    //6.解析氧气浓度（原始值/100 = 实际浓度%）
    //uint16_t o2_raw = (resp_frame[3] << 8) | resp_frame[4]; //数据高字节在前
    o2_raw = resp_frame[3] *256 + resp_frame[4];
    o2_conc = (float)o2_raw / 100; //转换为百分比

    return o2_conc; //返回氧气浓度
}