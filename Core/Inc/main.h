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
#include "stm32g0xx_hal.h"

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
#define SW_1_Pin GPIO_PIN_13
#define SW_1_GPIO_Port GPIOB
#define SW_1_EXTI_IRQn EXTI4_15_IRQn
#define SW_2_Pin GPIO_PIN_14
#define SW_2_GPIO_Port GPIOB
#define SW_2_EXTI_IRQn EXTI4_15_IRQn
#define SW_3_Pin GPIO_PIN_15
#define SW_3_GPIO_Port GPIOB
#define SW_3_EXTI_IRQn EXTI4_15_IRQn
#define DIO_Pin GPIO_PIN_6
#define DIO_GPIO_Port GPIOC
#define CLK_Pin GPIO_PIN_7
#define CLK_GPIO_Port GPIOC
#define LED_2_Pin GPIO_PIN_3
#define LED_2_GPIO_Port GPIOD
#define LED_1_Pin GPIO_PIN_3
#define LED_1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
