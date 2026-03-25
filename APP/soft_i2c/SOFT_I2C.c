#include "main.h"
#include "SOFT_I2C.h"

void DelayUs(uint32_t t)
{
  while(t--)
  {
    for(int i=0; i<30; i++)
      __NOP();
  }
}

void Set_SCL1(uint8_t level)
{
  if(level)
    HAL_GPIO_WritePin(SCL1_GPIO_Port, SCL1_Pin, GPIO_PIN_SET);
  else
    HAL_GPIO_WritePin(SCL1_GPIO_Port, SCL1_Pin, GPIO_PIN_RESET);
}


void Set_SDA1(uint8_t level)
{
  if(level)
    HAL_GPIO_WritePin(SDA1_GPIO_Port, SDA1_Pin, GPIO_PIN_SET);
  else
    HAL_GPIO_WritePin(SDA1_GPIO_Port, SDA1_Pin, GPIO_PIN_RESET);
}

void I2C1_Start(void)
{
  Set_SDA1(1);
  DelayUs(2);
  Set_SCL1(1);
  DelayUs(5);
  Set_SDA1(0);
  DelayUs(5);
  Set_SCL1(0);
  DelayUs(3);
}

void I2C1_Stop(void)
{
  Set_SDA1(0);
  DelayUs(2);
  Set_SCL1(0);
  DelayUs(2);
  Set_SCL1(1);
  DelayUs(5);
  Set_SDA1(1);
  DelayUs(5);
}

I2C_ACK_TypeDef I2C1_Read_ACK(void)
{
  uint8_t ack;
  DelayUs(2);
  Set_SDA1(1);
  ack=HAL_GPIO_ReadPin(SDA1_GPIO_Port, SDA1_Pin);
  Set_SCL1(1);
  DelayUs(2);
  ack=HAL_GPIO_ReadPin(SDA1_GPIO_Port, SDA1_Pin);
  DelayUs(1);
  Set_SCL1(0);
  DelayUs(2);
  
  if(ack == 0)
    return ACK;
  else
    return NACK;
}

void I2C11_Send_ACK(I2C_ACK_TypeDef ack)
{
  if(ack == ACK)
    Set_SDA1(0);
  else
    Set_SDA1(1);
  
  DelayUs(2);
  Set_SCL1(1); 	
  DelayUs(10);
  Set_SCL1(0);
  DelayUs(10);
}

I2C_ACK_TypeDef I2C1_Send_Byte(uint8_t data)
{
  uint8_t mask;
  I2C_ACK_TypeDef ack;
  
  DelayUs(2);
  
  for(mask=0x80; mask>0; mask>>=1)
  {
    DelayUs(2);
    
    if ((mask & data) == 0) 
      Set_SDA1(0);
    else 
      Set_SDA1(1);

    DelayUs(2);
    Set_SCL1(1);
    DelayUs(2);
    Set_SCL1(0);
  }
  
  ack=I2C1_Read_ACK();
  
    return ack;
}

uint8_t I2C1_Read_Byte(I2C_ACK_TypeDef ack)
{
  uint8_t mask;
  uint8_t data,sda;
  
  DelayUs(1);
  Set_SDA1(1);
  sda=HAL_GPIO_ReadPin(SDA1_GPIO_Port, SDA1_Pin);
  DelayUs(1);
  
  for(mask=0x80; mask>0; mask>>=1)
  {
    Set_SCL1(1);
    DelayUs(2);
    
    sda = HAL_GPIO_ReadPin(SDA1_GPIO_Port, SDA1_Pin);
    if(sda)
      data=(data | mask);
    
    Set_SCL1(0);
    DelayUs(2);
  }
  
  I2C11_Send_ACK(ack);
  return data;
}