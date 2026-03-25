#include "SFC.h"
#include "string.h"

uint16_t flow_ctr_rawvalue; //全局变量存储原始流量值
uint8_t tx_setframe[8]; //全局变量存储发送帧
uint8_t rx_setframe[8]; //全局变量存储接收帧
uint8_t tx_readframe[8];
uint8_t rx_readframe[8];
/*
*@brief 设定流量值（功能码0x06）
*@param flow_value: 流量值（单位：L/min，范围：4.0~200.0）
*@retval HAL_OK: 成功，其他: 失败
*/
HAL_StatusTypeDef SFC_SetFlowValue(float flow_value)
{
    if (flow_value < FLOW_MIN || flow_value > FLOW_MAX)
        return HAL_ERROR; // 流量值超出范围

    uint16_t scaled_value = (uint16_t)(flow_value * FLOW_MULTIPLIER); // 数值放大
    
    // 组装Modbus RTU请求帧
    tx_setframe[0] = SFC_SLAVE_ADDR; // 从机地址
    tx_setframe[1] = SFC_FUNC_CODE_WRITE; // 功能码：写单个寄存器
    tx_setframe[2] = (SFC_REG_SET_ADDR >> 8) & 0xFF; // 寄存器地址高字节
    tx_setframe[3] = SFC_REG_SET_ADDR & 0xFF; // 寄存器地址低字节
    tx_setframe[4] = (scaled_value >> 8) & 0xFF; // 数据高字节
    tx_setframe[5] = scaled_value & 0xFF; // 数据低字节
    
    uint16_t crc = Modbus_CRC16(tx_setframe, 6); // 计算CRC16校验码
    tx_setframe[6] = crc & 0xFF; // CRC低字节
    tx_setframe[7] = (crc >> 8) & 0xFF; // CRC高字节
    
    // 发送请求帧
    if (HAL_UART_Transmit(SFC_UART_HANDLER, tx_setframe, sizeof(tx_setframe), SFC_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR; // 发送失败

    //接收响应帧（流量控制器会回发相同指令帧）
    //uint8_t rx_frame[8];
    if (HAL_UART_Receive(SFC_UART_HANDLER, rx_setframe, sizeof(rx_setframe), SFC_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR; // 接收失败
    // 6. 验证响应合法性
    if(rx_setframe[0] == SFC_SLAVE_ADDR && rx_setframe[1] == SFC_FUNC_CODE_WRITE &&
       rx_setframe[2] == tx_setframe[2] && rx_setframe[3] == tx_setframe[3] &&
       rx_setframe[4] == tx_setframe[4] && rx_setframe[5] == tx_setframe[5]) {
        return HAL_OK;
    } else {
        return HAL_ERROR;
    }
}

/**
*@brief 读取流量值（功能码0x04）
*@param flow_value: 指针，存储读取到的流量值（单位：L/min）
*@retval HAL_OK: 成功，其他: 失败
*/
HAL_StatusTypeDef SFC_ReadFlowValue(float *flow_value)
{
    
    // 组装Modbus RTU请求帧
    tx_readframe[0] = SFC_SLAVE_ADDR; // 从机地址
    tx_readframe[1] = SFC_FUNC_CODE_READ; // 功能码：读保持寄存器
    tx_readframe[2] = (SFC_REG_READ_ADDR >> 8) & 0xFF; // 寄存器地址高字节
    tx_readframe[3] = SFC_REG_READ_ADDR & 0xFF; // 寄存器地址低字节
    tx_readframe[4] = (SFC_REG_NUM >> 8) & 0xFF; // 读取寄存器数量高字节
    tx_readframe[5] = SFC_REG_NUM & 0xFF; // 读取寄存器数量低字节
    
    uint16_t crc = Modbus_CRC16(tx_readframe, 6); // 计算CRC16校验码
    tx_readframe[6] = crc & 0xFF; // CRC低字节
    tx_readframe[7] = (crc >> 8) & 0xFF; // CRC高字节
    
    memset(rx_readframe, 0, O2_DMA_BUF_SIZE);
    // 发送请求帧
    if (HAL_UART_Transmit(SFC_UART_HANDLER, tx_readframe, 8, SFC_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR; // 发送失败

    HAL_Delay(1); //短暂延时等待控制器处理指令并准备响应
    // 接收响应帧
    if(HAL_UART_Receive(SFC_UART_HANDLER, rx_readframe, 7, SFC_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR; // 接收失败
    // if (HAL_UART_Receive_DMA(SFC_UART_HANDLER, rx_readframe, 7) != HAL_OK)
    //     return HAL_ERROR; // 接收失败

    // //4. 轮询等待DMA接收完成（简单方式，替代中断）
    // uint32_t timeout = HAL_GetTick() + SFC_TIMEOUT_MS;
    // while (HAL_UART_GetState(SFC_UART_HANDLER) == HAL_UART_STATE_BUSY_RX)
    // {
    //     if(HAL_GetTick() > timeout)
    //     {
    //         HAL_UART_Abort(SFC_UART_HANDLER);
    //         return HAL_ERROR; //接收超时
    //     }
    // }

    // 验证响应合法性
    if(rx_readframe[0] != SFC_SLAVE_ADDR || rx_readframe[1] != SFC_FUNC_CODE_READ || 
    rx_readframe[2] != 0x02)
    {
        return HAL_ERROR; // 响应帧格式错误
    }

    //解析实时流量（缩小100倍）
    flow_ctr_rawvalue = (rx_readframe[3] << 8) | rx_readframe[4]; // 合成原始数据
    *flow_value = flow_ctr_rawvalue / (float)FLOW_MULTIPLIER; // 转换为实际流量值
    return HAL_OK;
}