/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart Clock with NTC, RTC, EEPROM, RGB LED, and UART Menu
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "rtc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "tm1637.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#define R14         10000.0    
#define VCC         3.3        
#define ADC_MAX     4095.0     
#define BETA        3950.0     
#define R_NOMINAL   10000.0    
#define T_NOMINAL   298.15     
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
typedef enum {
    STATE_NORMAL,
    STATE_MAIN_MENU,
    STATE_SUB_SET_TIME_H,
    STATE_SUB_SET_TIME_M,
    STATE_SUB_TOGGLE_DISPLAY,
    STATE_SUB_SET_COLOR,
    STATE_SUB_UART_INTERVAL,
    STATE_SUB_UART_TOGGLE
} SystemState;

typedef struct {
    uint8_t display_en;
    uint8_t led_color;      // 0:RED, 1:GREEN, 2:BLUE
    uint8_t uart_en;
    uint8_t uart_interval;
    uint16_t boot_count;
} SystemSettings;
/* USER CODE END PTD */

/* USER CODE BEGIN PD */
#define EEPROM_ADDR 0xA0 
/* USER CODE END PD */

/* USER CODE BEGIN PM */
/* USER CODE END PM */



/* USER CODE BEGIN PV */
SystemState current_state = STATE_NORMAL;
int menu_index = 0;
const int MAX_MENU_ITEMS = 5;
SystemSettings settings;


uint8_t buffer_h = 0, buffer_m = 0;
uint32_t last_uart_tick = 0;
uint32_t last_display_update = 0;
tm1637_t tm1637;


volatile uint32_t current_adc;
volatile float temp_c = 25.0; 
float v_out;
float r_ntc;
float inv_t;
/* USER CODE END PV */

void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void Load_Settings_From_EEPROM(void);
void Save_Settings_To_EEPROM(void);
void Update_Temperature_LED(void);
void Handle_UART_Transmission(void);
void Refresh_Display_Output(void);
void Execute_Button_Action(uint8_t button_id);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */


void Load_Settings_From_EEPROM(void) {
    if (HAL_I2C_Mem_Read(&hi2c2, EEPROM_ADDR, 0x00, 1, (uint8_t*)&settings, sizeof(settings), 100) != HAL_OK) {
        settings.display_en = 1;
        settings.led_color = 0; 
        settings.uart_en = 1;
        settings.uart_interval = 5; 
        settings.boot_count = 0;
    }
    settings.boot_count++;
    HAL_I2C_Mem_Write(&hi2c2, EEPROM_ADDR, 0x00, 1, (uint8_t*)&settings, sizeof(settings), 100);
    HAL_Delay(10); 
}

void Save_Settings_To_EEPROM(void) {
    HAL_I2C_Mem_Write(&hi2c2, EEPROM_ADDR, 0x00, 1, (uint8_t*)&settings, sizeof(settings), 100);
    HAL_Delay(10);
}


void Update_Temperature_LED(void) {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY) == HAL_OK) {
        current_adc = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        
        if (current_adc > 0 && current_adc < 4095) {
            
            v_out = (float)current_adc * VCC / ADC_MAX;
            r_ntc = R14 * (v_out / (VCC - v_out));
            inv_t = (1.0 / T_NOMINAL) + (1.0 / BETA) * logf(r_ntc / R_NOMINAL);
            temp_c = (1.0 / inv_t) - 273.15; 
            
            
            float t_min = 20.0;
            float t_max = 100.0;
            float pwm_val = 0;
            
            
             pwm_val = ((temp_c - t_min) / (t_max - t_min)) * 999.0;
            
            
            uint32_t pwm_intensity = (uint32_t)pwm_val;

            
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0); 
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0); 
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0); 
            
            if (settings.led_color == 0)      __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pwm_intensity);
            else if (settings.led_color == 1) __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, pwm_intensity);
            else if (settings.led_color == 2) __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, pwm_intensity);
        }
    }
}


void Handle_UART_Transmission(void) {
    if (!settings.uart_en) return;

    uint32_t current_time = HAL_GetTick();
    if (current_time - last_uart_tick >= (settings.uart_interval * 1000)) {
        last_uart_tick = current_time;
        
        RTC_TimeTypeDef sTime = {0};
        RTC_DateTypeDef sDate = {0};
        HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
        HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
        
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET); 
        
        char tx_buffer[80];
        
        
        int temp_integer = (int)temp_c;
        int temp_fraction = (int)((temp_c - temp_integer) * 10);
        if (temp_fraction < 0) temp_fraction = -temp_fraction; 

        
        snprintf(tx_buffer, sizeof(tx_buffer), 
                 "Time: %02d:%02d:%02d | Boots: %d | Temp: %d.%d C\r\n", 
                 sTime.Hours, sTime.Minutes, sTime.Seconds, settings.boot_count, temp_integer, temp_fraction);
        
        HAL_UART_Transmit(&huart2, (uint8_t*)tx_buffer, strlen(tx_buffer), 100);
        
        HAL_Delay(5); 
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET); 
    }
}


void Refresh_Display_Output(void) {
    char str_buf[6];
    
    
    if (current_state != STATE_NORMAL)
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_RESET);
    
    switch (current_state) {
        case STATE_NORMAL:
            if (settings.display_en) {
                RTC_TimeTypeDef sTime = {0};
                RTC_DateTypeDef sDate = {0};
                HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
                HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
                
                snprintf(str_buf, sizeof(str_buf), "%02d%02d", sTime.Hours, sTime.Minutes);
                tm1637_write_string(&tm1637, str_buf);
            } else {
                tm1637_fill(&tm1637, false);
            }
            break;

        case STATE_MAIN_MENU:
            if (menu_index == 0) tm1637_write_string(&tm1637, "SETN"); 
            if (menu_index == 1) tm1637_write_string(&tm1637, "DISP"); 
            if (menu_index == 2) tm1637_write_string(&tm1637, "COLR"); 
            if (menu_index == 3) tm1637_write_string(&tm1637, "INTV"); 
            if (menu_index == 4) tm1637_write_string(&tm1637, "UART"); 
            break;

        case STATE_SUB_SET_TIME_H:
            snprintf(str_buf, sizeof(str_buf), "H.%02d", buffer_h);
            tm1637_write_string(&tm1637, str_buf);
            break;
            
        case STATE_SUB_SET_TIME_M:
            snprintf(str_buf, sizeof(str_buf), "M.%02d", buffer_m);
            tm1637_write_string(&tm1637, str_buf);
            break;

        case STATE_SUB_TOGGLE_DISPLAY:
            if (settings.display_en) tm1637_write_string(&tm1637, "D.ON ");
            else tm1637_write_string(&tm1637, "D.OFF");
            break;

        case STATE_SUB_SET_COLOR:
            if (settings.led_color == 0)      tm1637_write_string(&tm1637, "RED ");
            else if (settings.led_color == 1) tm1637_write_string(&tm1637, "GREN ");
            else if (settings.led_color == 2) tm1637_write_string(&tm1637, "BLU ");
            break;

        case STATE_SUB_UART_INTERVAL:
            snprintf(str_buf, sizeof(str_buf), "U.%02d", settings.uart_interval);
            tm1637_write_string(&tm1637, str_buf);
            break;

        case STATE_SUB_UART_TOGGLE:
            if (settings.uart_en) tm1637_write_string(&tm1637, "U.ON ");
            else tm1637_write_string(&tm1637, "U.OFF");
            break;
    }
}


void Execute_Button_Action(uint8_t button_id) { // 1: UP, 2: DOWN, 3: SELECT
    switch (current_state) {
        case STATE_NORMAL:
            if (button_id == 3) { 
                current_state = STATE_MAIN_MENU;
                menu_index = 0;
            }
            break;

        case STATE_MAIN_MENU:
            if (button_id == 1) { menu_index--; if (menu_index < 0) menu_index = MAX_MENU_ITEMS - 1; }
            if (button_id == 2) { menu_index++; if (menu_index >= MAX_MENU_ITEMS) menu_index = 0; }
            if (button_id == 3) {
                if (menu_index == 0) {
                    RTC_TimeTypeDef sTime = {0};
                    RTC_DateTypeDef sDate = {0};
                    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
                    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
                    buffer_h = sTime.Hours;
                    buffer_m = sTime.Minutes;
                    current_state = STATE_SUB_SET_TIME_H;
                }
                else if (menu_index == 1) current_state = STATE_SUB_TOGGLE_DISPLAY;
                else if (menu_index == 2) current_state = STATE_SUB_SET_COLOR;
                else if (menu_index == 3) current_state = STATE_SUB_UART_INTERVAL;
                else if (menu_index == 4) current_state = STATE_SUB_UART_TOGGLE;
            }
            break;

        case STATE_SUB_SET_TIME_H:
            if (button_id == 1) { buffer_h++; if (buffer_h > 23) buffer_h = 0; }
            if (button_id == 2) { if (buffer_h == 0) buffer_h = 23; else buffer_h--; }
            if (button_id == 3) current_state = STATE_SUB_SET_TIME_M;
            break;

        case STATE_SUB_SET_TIME_M:
            if (button_id == 1) { buffer_m++; if (buffer_m > 59) buffer_m = 0; }
            if (button_id == 2) { if (buffer_m == 0) buffer_m = 59; else buffer_m--; }
            if (button_id == 3) { 
                RTC_TimeTypeDef sTime = {0};
                sTime.Hours = buffer_h;
                sTime.Minutes = buffer_m;
                sTime.Seconds = 0; 
                HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
                
                current_state = STATE_NORMAL; 
            }
            break;

        case STATE_SUB_TOGGLE_DISPLAY:
            if (button_id == 1 || button_id == 2) settings.display_en = !settings.display_en;
            if (button_id == 3) { Save_Settings_To_EEPROM(); current_state = STATE_NORMAL; }
            break;

        case STATE_SUB_SET_COLOR:
            if (button_id == 1) { settings.led_color++; if(settings.led_color > 2) settings.led_color = 0; }
            if (button_id == 2) { if(settings.led_color == 0) settings.led_color = 2; else settings.led_color--; }
            if (button_id == 3) { Save_Settings_To_EEPROM(); current_state = STATE_NORMAL; }
            break;

        case STATE_SUB_UART_INTERVAL:
            if (button_id == 1) { settings.uart_interval++; if(settings.uart_interval > 99) settings.uart_interval = 1; }
            if (button_id == 2) { if(settings.uart_interval <= 1) settings.uart_interval = 99; else settings.uart_interval--; }
            if (button_id == 3) { Save_Settings_To_EEPROM(); current_state = STATE_NORMAL; }
            break;

        case STATE_SUB_UART_TOGGLE:
            if (button_id == 1 || button_id == 2) settings.uart_en = !settings.uart_en;
            if (button_id == 3) { Save_Settings_To_EEPROM(); current_state = STATE_NORMAL; }
            break;
    }
    Refresh_Display_Output();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_I2C2_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  
  HAL_ADCEx_Calibration_Start(&hadc1);
  
  Load_Settings_From_EEPROM();

  tm1637_init(&tm1637, GPIOC, GPIO_PIN_7, GPIOC, GPIO_PIN_6);
  tm1637_brightness(&tm1637, 5);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);

  uint32_t btn1_debounce = 0, btn2_debounce = 0, btn3_debounce = 0;
  Refresh_Display_Output();
  /* USER CODE END 2 */


  while (1)
  {
    uint32_t current_tick = HAL_GetTick();

  
    if (current_state == STATE_NORMAL) {
        if (current_tick - last_display_update >= 1000) {
            last_display_update = current_tick;
            Refresh_Display_Output();
        }
    }

    
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_RESET) {
        if (current_tick - btn1_debounce > 250) { Execute_Button_Action(1); btn1_debounce = current_tick; }
    }
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_RESET) {
        if (current_tick - btn2_debounce > 250) { Execute_Button_Action(2); btn2_debounce = current_tick; }
    }
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_RESET) {
        if (current_tick - btn3_debounce > 250) { Execute_Button_Action(3); btn3_debounce = current_tick; }
    }

    Update_Temperature_LED();     
    Handle_UART_Transmission();   
    
    HAL_Delay(5); 
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */


void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
