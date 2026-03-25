#include "main.h"
#include "SHT85.h"
#include "math.h"
#
/*
SHT85   ---->   I2C1
*/

//SHT85序列号
uint32_t SHT85_ID = 0;
float Temperature = 0;
float Humidity = 0;

void SHT85_Start(void)
{
  I2C1_Start();
}

void SHT85_Stop(void)
{
  I2C1_Stop();
}

//发送地址+写
I2C_ACK_TypeDef SHT85_Cmd_Write(void)
{
  I2C_ACK_TypeDef ack;
  
  ack=I2C1_Send_Byte( SHT85_ADDR << 1 );
  return ack;
}

//发送地址+读
I2C_ACK_TypeDef SHT85_Cmd_Read(void)
{
  I2C_ACK_TypeDef ack;
  
  ack=I2C1_Send_Byte( SHT85_ADDR << 1 | 0x01);
  return ack;
}

//向SHT85发送指令，需要先发送地址+写
I2C_ACK_TypeDef SHT85_Send_Cmd(uint16_t cmd)
{
  I2C_ACK_TypeDef ack;
  uint8_t data;
  
  data = cmd >> 8;
  ack=I2C1_Send_Byte(data);
  
  if(ack != ACK)
    return NACK;
  
  data = cmd;
  ack=I2C1_Send_Byte(data);
  
  return ack;
}

//发送复位指令
I2C_ACK_TypeDef SHT85_Reset(void)
{
  I2C_ACK_TypeDef ack;
  
  SHT85_Start();
  ack = SHT85_Cmd_Write();
  if( ack == ACK )
  {
    ack = SHT85_Send_Cmd(CMD_SOFT_RESET);
  }
  SHT85_Stop();
  return ack;
}

//读取序列号
I2C_ACK_TypeDef SHT85_Read_ID(void)
{
  I2C_ACK_TypeDef ack;
  uint8_t data[6];
  
  SHT85_Start();
  ack = SHT85_Cmd_Write();
  if( ack == ACK )
  {
    ack = SHT85_Send_Cmd(CMD_READ_SERIALNBR);
  }
  if( ack == ACK )
  {
    DelayUs(1000);
    SHT85_Start();
    ack = SHT85_Cmd_Read();
  }
  if( ack == ACK )
  {
    data[5] = I2C1_Read_Byte(ACK);
    data[4] = I2C1_Read_Byte(ACK);
    data[3] = I2C1_Read_Byte(ACK);
    data[2] = I2C1_Read_Byte(ACK);
    data[1] = I2C1_Read_Byte(ACK);
    data[0] = I2C1_Read_Byte(NACK);
  }
  SHT85_Stop();
  
  if( ack == ACK )//需要增加CRC校验（待定）
  {
    SHT85_ID |= (uint32_t)data[5] << 24;
    SHT85_ID |= (uint32_t)data[4] << 16;
    SHT85_ID |= (uint32_t)data[2] << 8;
    SHT85_ID |= (uint32_t)data[1];
  }
  return ack;
}

//配置测量方式
I2C_ACK_TypeDef SHT85_Config(void)
{
  I2C_ACK_TypeDef ack;
  
  SHT85_Start();
  ack = SHT85_Cmd_Write();
  if( ack == ACK )
  {
    ack = SHT85_Send_Cmd(CMD_MEAS_PERI_1_H);
  }
  return ack;
}

//SHT85初始化
void SHT85_Init(void)
{
  I2C_ACK_TypeDef ack;
  
  ack = SHT85_Reset();
  HAL_Delay(20);
  if( ack == ACK )
  {
    ack = SHT85_Read_ID();
  }
  if( ack == ACK )
  {
    SHT85_Config();
  }
   
  
}

//读取连续测量模式结果
I2C_ACK_TypeDef SHT85_Read_Result(void)
{
  I2C_ACK_TypeDef ack;
  uint8_t data[6];
  uint16_t temperature = 0;
  uint16_t humidity = 0;
  
  SHT85_Start();
  ack = SHT85_Cmd_Write();
  if( ack == ACK )
  {
    ack = SHT85_Send_Cmd(CMD_FETCH_DATA);
  }
  if( ack == ACK )
  {
    SHT85_Start();
    ack = SHT85_Cmd_Read();
  }
  if( ack == ACK )
  {
    data[5] = I2C1_Read_Byte(ACK);
    data[4] = I2C1_Read_Byte(ACK);
    data[3] = I2C1_Read_Byte(ACK);
    data[2] = I2C1_Read_Byte(ACK);
    data[1] = I2C1_Read_Byte(ACK);
    data[0] = I2C1_Read_Byte(NACK);
  }
  SHT85_Stop();
  
  if( ack == ACK )//需要增加CRC校验（待定）
  {
    temperature |= (uint16_t)data[5] << 8;
    temperature |= (uint16_t)data[4];
    humidity |= (uint16_t)data[2] << 8;
    humidity |= (uint16_t)data[1];
    Temperature = 175.0f * (float)temperature / 65535.0f - 45.0f;
    Humidity = 100.0f * (float)humidity / 65535.0f;
//    Humidity = Humidity_Offset(Humidity);
  }
  return ack;
}

float Humidity_Offset(float hum)
{
  float humidity;
  if(hum >= 3 && hum < 50)
  {
    humidity = (hum-3)/47*49+1;
  }
  else if(hum >= 2.8 && hum < 3)
  {
    humidity = 0.5*hum-0.5;
  }
  else if(hum >=0 && hum < 2.8)
  {
    humidity = 0.075*sinf( (hum-0.3) * 3.1415926535 )+0.825;
  }
  else
  {
    humidity = hum;
  }
  return humidity;
}









