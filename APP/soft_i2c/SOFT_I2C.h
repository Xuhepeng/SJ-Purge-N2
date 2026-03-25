#ifndef _I2C_H_
#define _I2C_H_

#include "stm32f4xx_hal.h"

#define SHT85_SCL_Pin GPIO_PIN_10
#define SHT85_SCL_GPIO_Port GPIOB
#define SHT85_SDA_Pin GPIO_PIN_11
#define SHT85_SDA_GPIO_Port GPIOB

#define SCL1_GPIO_Port  SHT85_SCL_GPIO_Port
#define SCL1_Pin        SHT85_SCL_Pin
#define SDA1_GPIO_Port  SHT85_SDA_GPIO_Port
#define SDA1_Pin        SHT85_SDA_Pin

typedef enum I2C_ACK {
  ACK,
  NACK
}I2C_ACK_TypeDef;

void DelayUs(uint32_t t);
void Set_SCL1(uint8_t level);
void Set_SDA1(uint8_t level);
void I2C1_Start(void);
void I2C1_Stop(void);
I2C_ACK_TypeDef I2C1_Read_ACK(void);
void I2C11_Send_ACK(I2C_ACK_TypeDef ack);
I2C_ACK_TypeDef I2C1_Send_Byte(uint8_t data);
uint8_t I2C1_Read_Byte(I2C_ACK_TypeDef ack);


#endif