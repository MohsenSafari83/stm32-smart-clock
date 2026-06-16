/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart clock - single-file version
  *                   STM32G030C8Tx, TM1637 display, EEPROM, 3 buttons,
  *                   UART, RGB LED (TIM1 PWM), NTC temperature (ADC1)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Private typedef -----------------------------------------------------------*/

typedef enum {
    RGB_RED = 0,
    RGB_GREEN,
    RGB_BLUE,
    RGB_OFF
} RgbChannel_t;

typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} ClockTime_t;

typedef struct {
    uint8_t display_enabled;
    uint8_t uart_enabled;
    uint8_t uart_interval;
    RgbChannel_t rgb_channel;
    uint16_t boot_counter;
} AppSettings_t;

typedef enum {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_MODE_SHORT,
    BTN_MODE_LONG
} ButtonEvent_t;

typedef enum {
    ST_NORMAL,
    ST_MENU,
    ST_EDIT
} FsmState_t;

typedef enum {
    DISP_MODE_TIME,
    DISP_MODE_TEXT,
    DISP_MODE_OFF
} DispMode_t;

/* Private define ------------------------------------------------------------*/

#define TM1637_COMM1    0x40
#define TM1637_COMM2    0xC0
#define TM1637_COMM3    0x80

/* RGB / PWM */
#define RGB_R_Pin       GPIO_PIN_8
#define RGB_R_GPIO_Port GPIOA
#define RGB_G_Pin       GPIO_PIN_9
#define RGB_G_GPIO_Port GPIOA
#define RGB_B_Pin       GPIO_PIN_10
#define RGB_B_GPIO_Port GPIOA

/* ADC - NTC */
#define NTC_ADC_Pin     GPIO_PIN_0
#define NTC_ADC_GPIO_Port GPIOA

/* NTC thermistor constants (Beta parameter) */
#define NTC_R14       10000.0f
#define NTC_VCC       3.3f
#define NTC_ADC_MAX   4095.0f
#define NTC_BETA      3950.0f
#define NTC_R_NOMINAL 10000.0f
#define NTC_T_NOMINAL 298.15f

/* EEPROM */
#define EEPROM_I2C_ADDR (0x50 << 1)
#define EEPROM_VERSION     0x01
#define EEPROM_OFFSET_VER      0
#define EEPROM_OFFSET_DISPLAY  1
#define EEPROM_OFFSET_UART_EN  2
#define EEPROM_OFFSET_UART_INT 3
#define EEPROM_OFFSET_RGB_CH   4
#define EEPROM_OFFSET_BOOT_CNT 5

/* Defaults */
#define DEFAULT_DISPLAY_ENABLED 1
#define DEFAULT_UART_ENABLED    1
#define DEFAULT_UART_INTERVAL   5
#define DEFAULT_RGB_CHANNEL     RGB_RED

/* Timing */
#define PWM_PERIOD       999
#define TEMP_MIN         20.0f
#define TEMP_MAX         50.0f
#define BTN_DEBOUNCE_MS  50
#define LONG_PRESS_MS    1000
#define BTN_SCAN_MS      5
#define DISP_REFRESH_MS  50
#define CLOCK_TICK_MS    1000
#define ADC_INTERVAL_MS  2000
#define RGB_INTERVAL_MS  500
#define EEPROM_SAVE_MS   5000
#define BOOT_DISPLAY_MS  2000
#define UART_INTERVAL_MIN 1
#define UART_INTERVAL_MAX 60
#define COLON_BLINK_MS   500

/* Segment patterns for TM1637 */
#define  Seg0  0b00111111
#define  Seg1  0b00000110
#define  Seg2  0b01011011
#define  Seg3  0b01001111
#define  Seg4  0b01100110
#define  Seg5  0b01101101
#define  Seg6  0b01111101
#define  Seg7  0b00000111
#define  Seg8  0b01111111
#define  Seg9  0b01101111
#define  SegA  0b01110111
#define  SegB  0b01111100
#define  SegC  0b00111001
#define  SegD  0b01011110
#define  SegE  0b01111001
#define  SegF  0b01110001
#define  SegG  0b00111101
#define  SegH  0b01110100
#define  SegI  0b00000110
#define  SegJ  0b00011111
#define  SegK  0b01110000
#define  SegL  0b00111000
#define  SegM  0b01010101
#define  SegN  0b01010100
#define  SegO  0b01011100
#define  SegP  0b01110011
#define  SegQ  0b01100111
#define  SegR  0b01010000
#define  SegS  0b01101101
#define  SegT  0b01111000
#define  SegU  0b00011100
#define  SegV  0b00111110
#define  SegW  0b01111110
#define  SegX  0b01110110
#define  SegY  0b01101110
#define  SegZ  0b00000000
#define  Seg_  0b00001000

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* TM1637 bit-banging */
static const uint8_t tm1637_on[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t tm1637_off[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Display state */
static DispMode_t disp_mode = DISP_MODE_OFF;
static uint8_t disp_hh, disp_mm;
static char disp_text[5];
static uint8_t colon_on = 0;
static uint32_t last_colon_tick = 0;

/* Clock */
static ClockTime_t clock_time;

/* ADC handle (manually inited, no CubeMX) */
static ADC_HandleTypeDef hadc1;

/* TIM1 handle (manually inited, no CubeMX) */
static TIM_HandleTypeDef htim1;

/* Button context (3 buttons) */
static struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t raw;
    uint8_t debounced;
    uint32_t debounce_start;
    uint32_t press_start;
    uint8_t long_sent;
} btn_ctx[3];
static ButtonEvent_t btn_pending = BTN_NONE;

/* FSM */
static FsmState_t fsm_state = ST_NORMAL;
static AppSettings_t settings;
static uint8_t menu_idx = 0;
static uint8_t settings_changed = 0;

/* FSM timing */
static uint32_t last_btn_tick = 0;
static uint32_t last_disp_tick = 0;
static uint32_t last_clock_tick = 0;
static uint32_t last_adc_tick = 0;
static uint32_t last_rgb_tick = 0;
static uint32_t last_uart_tick = 0;
static uint32_t last_change_tick = 0;
static uint32_t boot_start_tick = 0;

/* Shared runtime state */
static float temperature = 25.0f;
static ClockTime_t current_time;

/* Private function prototypes -----------------------------------------------*/

/* USER CODE BEGIN 0 */

/* ======================================================================== */
/*  TM1637 bit-banging low-level                                           */
/* ======================================================================== */

static void tm1637_delay_us(uint8_t delay)
{
    while (delay > 0)
    {
        delay--;
        __nop(); __nop(); __nop(); __nop();
    }
}

static void tm1637_start(void)
{
    HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_RESET);
    tm1637_delay_us(20);
}

static void tm1637_stop(void)
{
    HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_RESET);
    tm1637_delay_us(20);
    HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_SET);
    tm1637_delay_us(20);
    HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_SET);
    tm1637_delay_us(20);
}

static uint8_t tm1637_write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_RESET);
        tm1637_delay_us(20);
        if (data & 0x01)
            HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_RESET);
        tm1637_delay_us(20);
        HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_SET);
        tm1637_delay_us(20);
        data = data >> 1;
    }
    HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_SET);
    tm1637_delay_us(20);
    HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_SET);
    tm1637_delay_us(20);
    uint8_t ack = HAL_GPIO_ReadPin(DIO_GPIO_Port, DIO_Pin);
    if (ack == 0)
        HAL_GPIO_WritePin(DIO_GPIO_Port, DIO_Pin, GPIO_PIN_RESET);
    tm1637_delay_us(20);
    HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_RESET);
    tm1637_delay_us(20);
    return ack;
}

static void tm1637_write_raw(const uint8_t *raw, uint8_t length, uint8_t pos)
{
    if (pos > 5) return;
    if (length > 6) length = 6;
    tm1637_start();
    tm1637_write_byte(TM1637_COMM1);
    tm1637_stop();
    tm1637_start();
    tm1637_write_byte(TM1637_COMM2 + (pos & 0x03));
    for (uint8_t k = 0; k < length; k++)
        tm1637_write_byte(raw[k]);
    tm1637_stop();
    tm1637_start();
    tm1637_write_byte(TM1637_COMM3 + 0x0F);
    tm1637_stop();
}

static void tm1637_write_segment(const uint8_t *segments, uint8_t length, uint8_t pos)
{
    tm1637_write_raw(segments, length, pos);
}

static void tm1637_fill(uint8_t val)
{
    if (val)
        tm1637_write_segment(tm1637_on, 6, 0);
    else
        tm1637_write_segment(tm1637_off, 6, 0);
}

static uint8_t TM1637_CharToSegment(char c)
{
    switch (c)
    {
        case '0': return Seg0; case '1': return Seg1;
        case '2': return Seg2; case '3': return Seg3;
        case '4': return Seg4; case '5': return Seg5;
        case '6': return Seg6; case '7': return Seg7;
        case '8': return Seg8; case '9': return Seg9;
        case 'A': case 'a': return SegA; case 'B': case 'b': return SegB;
        case 'C': case 'c': return SegC; case 'D': case 'd': return SegD;
        case 'E': case 'e': return SegE; case 'F': case 'f': return SegF;
        case 'G': case 'g': return SegG; case 'H': case 'h': return SegH;
        case 'I': case 'i': return SegI; case 'J': case 'j': return SegJ;
        case 'K': case 'k': return SegK; case 'L': case 'l': return SegL;
        case 'M': case 'm': return SegM; case 'N': case 'n': return SegN;
        case 'O': case 'o': return SegO; case 'P': case 'p': return SegP;
        case 'Q': case 'q': return SegQ; case 'R': case 'r': return SegR;
        case 'S': case 's': return SegS; case 'T': case 't': return SegT;
        case 'U': case 'u': return SegU; case 'V': case 'v': return SegV;
        case 'W': case 'w': return SegW; case 'X': case 'x': return SegX;
        case 'Y': case 'y': return SegY; case 'Z': case 'z': return SegZ;
        case '-': return Seg_;
        case ' ': return 0x00;
        default:  return 0x00;
    }
}

static void tm1637_write_string(const char *str)
{
    uint8_t seg[4] = {0, 0, 0, 0};
    uint8_t pos = 0;
    for (uint8_t i = 0; str[i] != '\0' && pos < 4; i++)
    {
        if (str[i] == '.')
        {
            if (pos > 0)
                seg[pos - 1] |= 0x80;
        }
        else
        {
            seg[pos] = TM1637_CharToSegment(str[i]);
            pos++;
        }
    }
    tm1637_write_segment(seg, 4, 0);
}

static void tm1637_init(void)
{
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin = CLK_Pin;
    HAL_GPIO_Init(CLK_GPIO_Port, &g);
    g.Pin = DIO_Pin;
    HAL_GPIO_Init(DIO_GPIO_Port, &g);
}

/* ======================================================================== */
/*  Display wrapper                                                        */
/* ======================================================================== */

static void display_show_time(uint8_t hours, uint8_t mins)
{
    disp_hh = hours;
    disp_mm = mins;
    disp_mode = DISP_MODE_TIME;
}

static void display_show_text(const char *str)
{
    strncpy(disp_text, str, 4);
    disp_text[4] = '\0';
    disp_mode = DISP_MODE_TEXT;
}

static void display_set_off(void)
{
    disp_mode = DISP_MODE_OFF;
}

static void display_refresh(void)
{
    uint8_t seg[4] = {0};

    if (disp_mode == DISP_MODE_OFF)
    {
        tm1637_fill(0);
        return;
    }

    if (disp_mode == DISP_MODE_TEXT)
    {
        tm1637_write_string(disp_text);
        return;
    }

    if (HAL_GetTick() - last_colon_tick >= COLON_BLINK_MS)
    {
        last_colon_tick = HAL_GetTick();
        colon_on = !colon_on;
    }

    seg[0] = TM1637_CharToSegment('0' + disp_hh / 10);
    seg[1] = TM1637_CharToSegment('0' + disp_hh % 10);
    seg[2] = TM1637_CharToSegment('0' + disp_mm / 10);
    seg[3] = TM1637_CharToSegment('0' + disp_mm % 10);
    if (colon_on)
        seg[1] |= 0x80;

    tm1637_write_segment(seg, 4, 0);
}

/* ======================================================================== */
/*  Clock (software RTC)                                                   */
/* ======================================================================== */

static void clock_init(uint8_t h, uint8_t m, uint8_t s)
{
    clock_time.hours = h;
    clock_time.minutes = m;
    clock_time.seconds = s;
}

static void clock_tick(void)
{
    clock_time.seconds++;
    if (clock_time.seconds >= 60)
    {
        clock_time.seconds = 0;
        clock_time.minutes++;
        if (clock_time.minutes >= 60)
        {
            clock_time.minutes = 0;
            clock_time.hours = (clock_time.hours + 1) % 24;
        }
    }
}

static void clock_get(ClockTime_t *t)
{
    *t = clock_time;
}

static void clock_set(uint8_t h, uint8_t m, uint8_t s)
{
    clock_time.hours = h;
    clock_time.minutes = m;
    clock_time.seconds = s;
}

/* ======================================================================== */
/*  ADC temperature (NTC thermistor)                                       */
/* ======================================================================== */

static void adc_temp_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = NTC_ADC_Pin;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(NTC_ADC_GPIO_Port, &gpio);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    HAL_ADC_Init(&hadc1);

    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel = ADC_CHANNEL_0;
    ch.Rank = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_79CYCLES_5;
    ch.SingleDiff = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    ch.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
}

static void adc_temp_read(float *temp_c)
{
    uint16_t adc_val = 0;
    float v_out, r_ntc, inv_t;

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
        adc_val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    if (adc_val == 0 || adc_val >= 4094)
    {
        *temp_c = 25.0f;
        return;
    }

    v_out = (float)adc_val * NTC_VCC / NTC_ADC_MAX;
    r_ntc = NTC_R14 * (v_out / (NTC_VCC - v_out));
    inv_t = (1.0f / NTC_T_NOMINAL)
          + (1.0f / NTC_BETA) * logf(r_ntc / NTC_R_NOMINAL);
    *temp_c = (1.0f / inv_t) - 273.15f;
}

/* ======================================================================== */
/*  RGB LED (TIM1 PWM CH1-3)                                               */
/* ======================================================================== */

static void rgb_init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF2_TIM1;

    gpio.Pin = RGB_R_Pin;
    HAL_GPIO_Init(RGB_R_GPIO_Port, &gpio);
    gpio.Pin = RGB_G_Pin;
    HAL_GPIO_Init(RGB_G_GPIO_Port, &gpio);
    gpio.Pin = RGB_B_Pin;
    HAL_GPIO_Init(RGB_B_GPIO_Port, &gpio);

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 7;
    htim1.Init.Period = 999;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_3);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
}

static void rgb_set(RgbChannel_t ch, uint16_t brightness)
{
    uint16_t r = 0, g = 0, b = 0;
    switch (ch)
    {
        case RGB_RED:   r = brightness; break;
        case RGB_GREEN: g = brightness; break;
        case RGB_BLUE:  b = brightness; break;
        case RGB_OFF:
        default: break;
    }
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, r);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, g);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, b);
}

/* ======================================================================== */
/*  EEPROM (I2C)                                                           */
/* ======================================================================== */

static HAL_StatusTypeDef eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(&hi2c2, EEPROM_I2C_ADDR,
                                               addr, I2C_MEMADD_SIZE_16BIT,
                                               (uint8_t *)data, len,
                                               HAL_MAX_DELAY);
    HAL_Delay(5);
    return ret;
}

static HAL_StatusTypeDef eeprom_read(uint16_t addr, uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c2, EEPROM_I2C_ADDR,
                            addr, I2C_MEMADD_SIZE_16BIT,
                            data, len, HAL_MAX_DELAY);
}

/* ======================================================================== */
/*  Buttons (GPIO debounce)                                                */
/* ======================================================================== */

static void buttons_init(void)
{
    btn_ctx[0].port = SW_1_GPIO_Port; btn_ctx[0].pin = SW_1_Pin;
    btn_ctx[1].port = SW_2_GPIO_Port; btn_ctx[1].pin = SW_2_Pin;
    btn_ctx[2].port = SW_3_GPIO_Port; btn_ctx[2].pin = SW_3_Pin;

    for (int i = 0; i < 3; i++)
    {
        btn_ctx[i].raw = 0;
        btn_ctx[i].debounced = 0;
        btn_ctx[i].debounce_start = 0;
        btn_ctx[i].press_start = 0;
        btn_ctx[i].long_sent = 0;
    }
    btn_pending = BTN_NONE;
}

static void buttons_scan(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 3; i++)
    {
        btn_ctx[i].raw = (HAL_GPIO_ReadPin(btn_ctx[i].port, btn_ctx[i].pin)
                          == GPIO_PIN_RESET) ? 1 : 0;

        if (btn_ctx[i].raw != btn_ctx[i].debounced)
        {
            if (btn_ctx[i].debounce_start == 0)
            {
                btn_ctx[i].debounce_start = now;
            }
            else if (now - btn_ctx[i].debounce_start >= BTN_DEBOUNCE_MS)
            {
                btn_ctx[i].debounced = btn_ctx[i].raw;
                btn_ctx[i].debounce_start = 0;

                if (btn_ctx[i].debounced)
                {
                    btn_ctx[i].press_start = now;
                    btn_ctx[i].long_sent = 0;
                    if (btn_pending == BTN_NONE)
                    {
                        switch (i)
                        {
                            case 0: btn_pending = BTN_MODE_SHORT; break;
                            case 1: btn_pending = BTN_UP;         break;
                            case 2: btn_pending = BTN_DOWN;       break;
                        }
                    }
                }
            }
        }
        else
        {
            btn_ctx[i].debounce_start = 0;
        }

        if (i == 0 && btn_ctx[i].debounced && !btn_ctx[i].long_sent)
        {
            if (now - btn_ctx[i].press_start >= LONG_PRESS_MS)
            {
                btn_pending = BTN_MODE_LONG;
                btn_ctx[i].long_sent = 1;
            }
        }
    }
}

static ButtonEvent_t buttons_get_event(void)
{
    ButtonEvent_t evt = btn_pending;
    btn_pending = BTN_NONE;
    return evt;
}

/* ======================================================================== */
/*  Settings persistence + UART broadcast                                  */
/* ======================================================================== */

static void settings_save(void)
{
    uint8_t data[7];
    data[0] = 0x01;
    data[1] = settings.display_enabled;
    data[2] = settings.uart_enabled;
    data[3] = settings.uart_interval;
    data[4] = (uint8_t)settings.rgb_channel;
    data[5] = settings.boot_counter >> 8;
    data[6] = settings.boot_counter & 0xFF;
    eeprom_write(0, data, 7);
}

static void settings_load(void)
{
    uint8_t data[7];

    if (eeprom_read(0, data, 7) != HAL_OK || data[0] != 0x01)
    {
        settings.display_enabled = 1;
        settings.uart_enabled = 1;
        settings.uart_interval = 5;
        settings.rgb_channel = RGB_RED;
        settings.boot_counter = 0;
        return;
    }

    settings.display_enabled = data[1] ? 1 : 0;
    settings.uart_enabled = data[2] ? 1 : 0;
    settings.uart_interval = data[3];
    settings.rgb_channel = (RgbChannel_t)data[4];
    settings.boot_counter = (data[5] << 8) | data[6];

    if (settings.uart_interval < 1 || settings.uart_interval > 60)
        settings.uart_interval = 5;
    if (settings.rgb_channel > RGB_OFF)
        settings.rgb_channel = RGB_RED;
}

static void uart_broadcast(void)
{
    char msg[32];
    snprintf(msg, sizeof(msg), "TIME %02d:%02d:%02d\r\n",
             current_time.hours, current_time.minutes, current_time.seconds);

    HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);
    HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_RESET);
}

/* ======================================================================== */
/*  Display helpers for FSM                                                */
/* ======================================================================== */

static void show_time_or_counter(void)
{
    if (HAL_GetTick() - boot_start_tick < BOOT_DISPLAY_MS)
    {
        char buf[5];
        snprintf(buf, sizeof(buf), "b%03d", settings.boot_counter % 1000);
        display_show_text(buf);
    }
    else if (settings.display_enabled)
    {
        display_show_time(current_time.hours, current_time.minutes);
    }
    else
    {
        display_set_off();
    }
}

static void update_display(void)
{
    char buf[5];

    if (fsm_state == ST_NORMAL)
        return;

    if (fsm_state == ST_MENU)
    {
        switch (menu_idx)
        {
            case 3:
                snprintf(buf, sizeof(buf),
                         settings.display_enabled ? "don" : "doF");
                break;
            case 4:
                snprintf(buf, sizeof(buf),
                         settings.uart_enabled ? "Uon" : "UoF");
                break;
            case 5:
                snprintf(buf, sizeof(buf), "U%02d", settings.uart_interval);
                break;
            case 6:
            {
                const char *ch[] = {"r   ", "g   ", "b   ", "OFF "};
                snprintf(buf, sizeof(buf), "%s", ch[settings.rgb_channel]);
                break;
            }
            default:
            {
                static const char *labels[] = {
                    "H   ", "M   ", "S   ", "don ", "Uon ", "U 05", "r   "
                };
                snprintf(buf, sizeof(buf), "%s", labels[menu_idx]);
                break;
            }
        }
        display_show_text(buf);
        return;
    }

    /* ST_EDIT */
    switch (menu_idx)
    {
        case 0:
            snprintf(buf, sizeof(buf), "H%02d", current_time.hours);
            break;
        case 1:
            snprintf(buf, sizeof(buf), "M%02d", current_time.minutes);
            break;
        case 2:
            snprintf(buf, sizeof(buf), "S%02d", current_time.seconds);
            break;
        case 5:
            snprintf(buf, sizeof(buf), "U%02d", settings.uart_interval);
            break;
        case 6:
        {
            const char *ch[] = {"r   ", "g   ", "b   ", "OFF "};
            snprintf(buf, sizeof(buf), "%s", ch[settings.rgb_channel]);
            break;
        }
        default:
            display_show_text("----");
            return;
    }
    display_show_text(buf);
}

/* ======================================================================== */
/*  FSM (Finite State Machine)                                             */
/* ======================================================================== */

static void app_fsm_run(void)
{
    uint32_t now = HAL_GetTick();
    uint16_t pwm;

    /* --- periodic background tasks --- */
    if (now - last_btn_tick >= BTN_SCAN_MS)
    {
        last_btn_tick = now;
        buttons_scan();
    }

    if (now - last_disp_tick >= DISP_REFRESH_MS)
    {
        last_disp_tick = now;
        display_refresh();
    }

    if (now - last_clock_tick >= CLOCK_TICK_MS)
    {
        last_clock_tick = now;
        clock_tick();
    }

    if (now - last_adc_tick >= ADC_INTERVAL_MS)
    {
        last_adc_tick = now;
        adc_temp_read(&temperature);
    }

    clock_get(&current_time);

    if (now - last_rgb_tick >= RGB_INTERVAL_MS)
    {
        last_rgb_tick = now;

        pwm = 0;
        if (temperature > TEMP_MIN)
        {
            if (temperature >= TEMP_MAX)
                pwm = PWM_PERIOD;
            else
                pwm = (uint16_t)((temperature - TEMP_MIN)
                      / (TEMP_MAX - TEMP_MIN) * PWM_PERIOD);
        }
        rgb_set(settings.rgb_channel, pwm);
    }

    if (settings.uart_enabled
        && (now - last_uart_tick >= (uint32_t)settings.uart_interval * 1000))
    {
        last_uart_tick = now;
        uart_broadcast();
    }

    /* --- FSM: read button event --- */
    ButtonEvent_t evt = buttons_get_event();

    switch (fsm_state)
    {
        case ST_NORMAL:
            show_time_or_counter();

            if (evt == BTN_MODE_SHORT)
            {
                fsm_state = ST_MENU;
                menu_idx = 0;
                update_display();
            }
            break;

        case ST_MENU:
            switch (evt)
            {
                case BTN_MODE_SHORT:
                    menu_idx++;
                    if (menu_idx >= 7)
                    {
                        fsm_state = ST_NORMAL;
                        break;
                    }
                    update_display();
                    break;

                case BTN_MODE_LONG:
                    fsm_state = ST_NORMAL;
                    break;

                case BTN_UP:
                case BTN_DOWN:
                {
                    switch (menu_idx)
                    {
                        case 3:
                            settings.display_enabled = !settings.display_enabled;
                            settings_changed = 1;
                            last_change_tick = now;
                            update_display();
                            break;

                        case 4:
                            settings.uart_enabled = !settings.uart_enabled;
                            settings_changed = 1;
                            last_change_tick = now;
                            update_display();
                            break;

                        default:
                            fsm_state = ST_EDIT;
                            update_display();
                            break;
                    }
                    break;
                }

                default:
                    break;
            }
            break;

        case ST_EDIT:
        {
            uint8_t changed = 0;

            switch (evt)
            {
                case BTN_MODE_SHORT:
                    fsm_state = ST_MENU;
                    update_display();
                    break;

                case BTN_MODE_LONG:
                    fsm_state = ST_NORMAL;
                    break;

                case BTN_UP:
                case BTN_DOWN:
                {
                    int inc = (evt == BTN_UP) ? 1 : -1;

                    switch (menu_idx)
                    {
                        case 0:
                        {
                            int h = (current_time.hours + inc + 24) % 24;
                            clock_set((uint8_t)h, current_time.minutes,
                                      current_time.seconds);
                            clock_get(&current_time);
                            changed = 1;
                            break;
                        }
                        case 1:
                        {
                            int m = (current_time.minutes + inc + 60) % 60;
                            clock_set(current_time.hours, (uint8_t)m,
                                      current_time.seconds);
                            clock_get(&current_time);
                            changed = 1;
                            break;
                        }
                        case 2:
                        {
                            int s = (current_time.seconds + inc + 60) % 60;
                            clock_set(current_time.hours, current_time.minutes,
                                      (uint8_t)s);
                            clock_get(&current_time);
                            changed = 1;
                            break;
                        }
                        case 5:
                        {
                            int v = (int)settings.uart_interval + inc;
                            if (v >= 1 && v <= 60)
                            {
                                settings.uart_interval = (uint8_t)v;
                                changed = 1;
                            }
                            break;
                        }
                        case 6:
                        {
                            int v = (int)settings.rgb_channel + inc;
                            if (v >= RGB_RED && v <= RGB_OFF)
                            {
                                settings.rgb_channel = (RgbChannel_t)v;
                                changed = 1;
                            }
                            break;
                        }
                    }
                    break;
                }

                default:
                    break;
            }

            if (changed)
            {
                settings_changed = 1;
                last_change_tick = now;
                update_display();
            }
            break;
        }
    }

    /* auto-save settings after 5s idle */
    if (settings_changed && (now - last_change_tick >= EEPROM_SAVE_MS))
    {
        settings_save();
        settings_changed = 0;
    }
}

static void app_fsm_init(void)
{
    settings_load();

    clock_init(12, 0, 0);

    if (settings.boot_counter == 0xFFFF)
        settings.boot_counter = 0;
    settings.boot_counter++;
    settings_changed = 1;
    last_change_tick = HAL_GetTick();

    boot_start_tick = HAL_GetTick();
    display_show_time(12, 0);
}

/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C2_Init();

    tm1637_init();
    buttons_init();
    clock_init(12, 0, 0);
    adc_temp_init();
    rgb_init();
    app_fsm_init();

    while (1)
    {
        app_fsm_run();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
        Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
