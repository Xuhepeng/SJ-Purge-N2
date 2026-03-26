/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define sys_led_Pin GPIO_PIN_6
#define sys_led_GPIO_Port GPIOE
#define work_led_Pin GPIO_PIN_13
#define work_led_GPIO_Port GPIOC
#define SHT85_SCL_Pin_Pin GPIO_PIN_10
#define SHT85_SCL_Pin_GPIO_Port GPIOB
#define SHT85_SDA_Pin_Pin GPIO_PIN_11
#define SHT85_SDA_Pin_GPIO_Port GPIOB
#define o_relay_pin_Pin GPIO_PIN_6
#define o_relay_pin_GPIO_Port GPIOB
#define o_air_inlet_pin_Pin GPIO_PIN_7
#define o_air_inlet_pin_GPIO_Port GPIOB
#define o_air_inlet2_pin_Pin GPIO_PIN_8
#define o_air_inlet2_pin_GPIO_Port GPIOB
#define o_vacuum_pin_Pin GPIO_PIN_9
#define o_vacuum_pin_GPIO_Port GPIOB
#define o_air_outlet_pin_Pin GPIO_PIN_0
#define o_air_outlet_pin_GPIO_Port GPIOE
#define o_py_relay_pin_Pin GPIO_PIN_1
#define o_py_relay_pin_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define air_inlet1_open() HAL_GPIO_WritePin(o_air_inlet_pin_GPIO_Port, o_air_inlet_pin_Pin, GPIO_PIN_RESET)
#define air_inlet1_close() HAL_GPIO_WritePin(o_air_inlet_pin_GPIO_Port, o_air_inlet_pin_Pin, GPIO_PIN_SET)
#define air_inlet2_open() HAL_GPIO_WritePin(o_air_inlet2_pin_GPIO_Port, o_air_inlet2_pin_Pin, GPIO_PIN_RESET)
#define air_inlet2_close() HAL_GPIO_WritePin(o_air_inlet2_pin_GPIO_Port, o_air_inlet2_pin_Pin, GPIO_PIN_SET)
#define vacuum_open() HAL_GPIO_WritePin(o_vacuum_pin_GPIO_Port, o_vacuum_pin_Pin, GPIO_PIN_RESET)
#define vacuum_close() HAL_GPIO_WritePin(o_vacuum_pin_GPIO_Port, o_vacuum_pin_Pin, GPIO_PIN_SET)
#define air_outlet_open() HAL_GPIO_WritePin(o_air_outlet_pin_GPIO_Port, o_air_outlet_pin_Pin, GPIO_PIN_RESET)
#define air_outlet_close() HAL_GPIO_WritePin(o_air_outlet_pin_GPIO_Port, o_air_outlet_pin_Pin, GPIO_PIN_SET)
#define py_relay_on() HAL_GPIO_WritePin(o_py_relay_pin_GPIO_Port, o_py_relay_pin_Pin, GPIO_PIN_RESET)
#define py_relay_off() HAL_GPIO_WritePin(o_py_relay_pin_GPIO_Port, o_py_relay_pin_Pin, GPIO_PIN_SET)
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
