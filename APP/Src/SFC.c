#include "SFC.h"
#include "string.h"

/* 保存最近一次解析出的原始流量值 */
uint16_t flow_ctr_rawvalue;

/* 写命令和读命令使用的收发缓冲区 */
uint8_t tx_setframe[8];
uint8_t rx_setframe[8];
uint8_t tx_readframe[8];
uint8_t rx_readframe[8];

/*
 * 设定 SFC 目标流量。
 *
 * 处理步骤：
 * 1. 检查输入流量是否在 SFC 允许范围内
 * 2. 把浮点流量放大 100 倍，转成协议里的整数
 * 3. 组 Modbus 写单寄存器命令帧
 * 4. 发送命令帧
 * 5. 接收回包
 * 6. 校验回包是否与发送内容一致
 */
HAL_StatusTypeDef SFC_SetFlowValue(float flow_value)
{
    uint16_t scaled_value;
    uint16_t crc;

    /* 协议要求把流量值放大 100 倍后发送 */
    scaled_value = (uint16_t)(flow_value * FLOW_MULTIPLIER);

    /* 组写单寄存器命令帧 */
    tx_setframe[0] = SFC_SLAVE_ADDR;
    tx_setframe[1] = SFC_FUNC_CODE_WRITE;
    tx_setframe[2] = (SFC_REG_SET_ADDR >> 8) & 0xFF;
    tx_setframe[3] = SFC_REG_SET_ADDR & 0xFF;
    tx_setframe[4] = (scaled_value >> 8) & 0xFF;
    tx_setframe[5] = scaled_value & 0xFF;

    /* CRC 放在帧尾，低字节在前，高字节在后 */
    crc = Modbus_CRC16(tx_setframe, 6);
    tx_setframe[6] = crc & 0xFF;
    tx_setframe[7] = (crc >> 8) & 0xFF;

    /* 发送写流量命令 */
    if (HAL_UART_Transmit(SFC_UART_HANDLER, tx_setframe, sizeof(tx_setframe), SFC_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 接收回包，正常情况下回包会把刚写入的内容原样回显 */
    if (HAL_UART_Receive(SFC_UART_HANDLER, rx_setframe, sizeof(rx_setframe), SFC_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 简单校验：地址、功能码、寄存器地址和数据都应与发送时一致 */
    if ((rx_setframe[0] == SFC_SLAVE_ADDR) &&
        (rx_setframe[1] == SFC_FUNC_CODE_WRITE) &&
        (rx_setframe[2] == tx_setframe[2]) &&
        (rx_setframe[3] == tx_setframe[3]) &&
        (rx_setframe[4] == tx_setframe[4]) &&
        (rx_setframe[5] == tx_setframe[5]))
    {
        return HAL_OK;
    }

    return HAL_ERROR;
}

/*
 * 读取 SFC 当前流量反馈。
 *
 * 处理步骤：
 * 1. 组 Modbus 读寄存器命令帧
 * 2. 发送读取命令
 * 3. 短暂等待设备准备回包
 * 4. 接收 7 字节回包
 * 5. 校验地址/功能码/字节数
 * 6. 把回包里的原始值还原成 float 流量
 */
HAL_StatusTypeDef SFC_ReadFlowValue(float *flow_value)
{
    uint16_t crc;

    /* 组读命令帧 */
    tx_readframe[0] = SFC_SLAVE_ADDR;
    tx_readframe[1] = SFC_FUNC_CODE_READ;
    tx_readframe[2] = (SFC_REG_READ_ADDR >> 8) & 0xFF;
    tx_readframe[3] = SFC_REG_READ_ADDR & 0xFF;
    tx_readframe[4] = (SFC_REG_NUM >> 8) & 0xFF;
    tx_readframe[5] = SFC_REG_NUM & 0xFF;

    crc = Modbus_CRC16(tx_readframe, 6);
    tx_readframe[6] = crc & 0xFF;
    tx_readframe[7] = (crc >> 8) & 0xFF;

    memset(rx_readframe, 0, sizeof(rx_readframe));

    /* 发送读命令 */
    if (HAL_UART_Transmit(SFC_UART_HANDLER, tx_readframe, 8, SFC_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 给 SFC 一点处理时间，避免发完立刻收导致回包还没准备好 */
    HAL_Delay(1);

    /* 接收回包 */
    if (HAL_UART_Receive(SFC_UART_HANDLER, rx_readframe, 7, SFC_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /*
     * 回包基本格式检查：
     * [0] 从机地址
     * [1] 功能码
     * [2] 数据字节数，当前应为 0x02
     */
    if ((rx_readframe[0] != SFC_SLAVE_ADDR) ||
        (rx_readframe[1] != SFC_FUNC_CODE_READ) ||
        (rx_readframe[2] != 0x02))
    {
        return HAL_ERROR;
    }

    /* 组合高低字节，还原原始流量值 */
    flow_ctr_rawvalue = (uint16_t)((rx_readframe[3] << 8) | rx_readframe[4]);

    /* 再除以 100，恢复成实际流量值，单位 L/min */
    *flow_value = flow_ctr_rawvalue / (float)FLOW_MULTIPLIER;

    return HAL_OK;
}
