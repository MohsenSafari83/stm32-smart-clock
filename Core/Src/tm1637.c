#include "tm1637.h"
#include "tm1637_config.h"
#include <string.h>
#include <stdio.h>

#if _TM1637_FREERTOS == 0
#define tm1637_delay_ms(x)  HAL_Delay(x)
#else
#include "cmsis_os.h"
#define tm1637_delay_ms(x)  osDelay(x)
#endif

#define TM1637_COMM1    0x40
#define TM1637_COMM2    0xC0
#define TM1637_COMM3    0x80

const uint8_t _tm1637_digit[] =
  {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
const uint8_t _tm1637_on[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t _tm1637_off[6] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
const uint8_t fill_off[4] = {0x00, 0x00, 0x00, 0x00};
const uint8_t _tm1637_minus = 0x40;
const uint8_t _tm1637_dot = 0x80;  
//#######################################################################################################################
void tm1637_delay_us(uint8_t delay)
{
  while (delay > 0)
  {
    delay--;
    __nop();__nop();__nop();__nop();
  }
}
//#######################################################################################################################
void tm1637_start(tm1637_t *tm1637)
{
  HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_RESET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
}
//#######################################################################################################################
void tm1637_stop(tm1637_t *tm1637)
{
  HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_RESET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
  HAL_GPIO_WritePin(tm1637->gpio_clk, tm1637->pin_clk, GPIO_PIN_SET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
  HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_SET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
}
//#######################################################################################################################
uint8_t tm1637_write_byte(tm1637_t *tm1637, uint8_t data)
{
  //  write 8 bit data
  for (uint8_t i = 0; i < 8; i++)
  {
    HAL_GPIO_WritePin(tm1637->gpio_clk, tm1637->pin_clk, GPIO_PIN_RESET);
    tm1637_delay_us(_TM1637_BIT_DELAY);
    if (data & 0x01)
      HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_RESET);
    tm1637_delay_us(_TM1637_BIT_DELAY);
    HAL_GPIO_WritePin(tm1637->gpio_clk, tm1637->pin_clk, GPIO_PIN_SET);
    tm1637_delay_us(_TM1637_BIT_DELAY);
    data = data >> 1;
  }
  // wait for acknowledge
  HAL_GPIO_WritePin(tm1637->gpio_clk, tm1637->pin_clk, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_SET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
  HAL_GPIO_WritePin(tm1637->gpio_clk, tm1637->pin_clk, GPIO_PIN_SET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
  uint8_t ack = HAL_GPIO_ReadPin(tm1637->gpio_dat, tm1637->pin_dat);
  if (ack == 0)
    HAL_GPIO_WritePin(tm1637->gpio_dat, tm1637->pin_dat, GPIO_PIN_RESET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
  HAL_GPIO_WritePin(tm1637->gpio_clk, tm1637->pin_clk, GPIO_PIN_RESET);
  tm1637_delay_us(_TM1637_BIT_DELAY);
  return ack;
}
//#######################################################################################################################
void tm1637_lock(tm1637_t *tm1637)
{
  while (tm1637->lock == 1)
    tm1637_delay_ms(1);
  tm1637->lock = 1;  
}
//#######################################################################################################################
void tm1637_unlock(tm1637_t *tm1637)
{
  tm1637->lock = 0;  
}
//#######################################################################################################################
void tm1637_init(tm1637_t *tm1637, GPIO_TypeDef *gpio_clk, uint16_t pin_clk, GPIO_TypeDef *gpio_dat, uint16_t pin_dat)
{
  memset(tm1637, 0, sizeof(tm1637_t)); 
  //  set max brightess
  tm1637_brightness(tm1637, 7);  
  tm1637_lock(tm1637);
  //  init gpio
  tm1637->gpio_clk = gpio_clk;
  tm1637->pin_clk = pin_clk;
  tm1637->gpio_dat = gpio_dat;
  tm1637->pin_dat = pin_dat;
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_OUTPUT_OD;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pin = pin_clk;
  HAL_GPIO_Init(gpio_clk, &g);
  g.Pin = pin_dat;
  HAL_GPIO_Init(gpio_dat, &g);    
  tm1637_unlock(tm1637);
}
//#######################################################################################################################
void tm1637_brightness(tm1637_t *tm1637, uint8_t brightness_0_to_7)
{
  tm1637_lock(tm1637);
  tm1637->brightness = (brightness_0_to_7 & 0x7) | 0x08;
  tm1637_unlock(tm1637);
}
//#######################################################################################################################
void tm1637_write_raw(tm1637_t *tm1637, const uint8_t *raw, uint8_t length, uint8_t pos)
{
  if (pos > 5)
    return;
  if (length > 6)
    length = 6;
  // write COMM1
  tm1637_start(tm1637);
  tm1637_write_byte(tm1637, TM1637_COMM1);
  tm1637_stop(tm1637);
  // write COMM2 + first digit address
  tm1637_start(tm1637);
  tm1637_write_byte(tm1637, TM1637_COMM2 + (pos & 0x03));
  // write the data bytes
  for (uint8_t k=0; k < length; k++)
    tm1637_write_byte(tm1637, raw[k]);
  tm1637_stop(tm1637);
  // write COMM3 + brightness
  tm1637_start(tm1637);
  tm1637_write_byte(tm1637, TM1637_COMM3 + tm1637->brightness);
  tm1637_stop(tm1637);
}
//#######################################################################################################################
void tm1637_write_segment(tm1637_t *tm1637, const uint8_t *segments, uint8_t length, uint8_t pos)
{
  tm1637_lock(tm1637);
  tm1637_write_raw(tm1637, segments, length, pos);
  tm1637_unlock(tm1637);  
}
//#######################################################################################################################
void tm1637_write_int(tm1637_t *tm1637, int32_t digit, uint8_t pos)
{
  tm1637_lock(tm1637);
  char str[7];
  uint8_t buffer[6] = {0};
  snprintf(str, sizeof(str) , "%d", digit);
  for (uint8_t i=0; i < 6; i++)
  {
    if (str[i] == '-')
      buffer[i] = _tm1637_minus;
    else if((str[i] >= '0') && (str[i] <= '9'))
      buffer[i] = _tm1637_digit[str[i] - 48];
    else
    {
      buffer[i] = 0;
      break;
    }
  }
  tm1637_write_raw(tm1637, buffer, 6, pos);              
  tm1637_unlock(tm1637);  
}
//#######################################################################################################################
void tm1637_write_float(tm1637_t *tm1637, float digit, uint8_t floating_digit, uint8_t pos)
{
  tm1637_lock(tm1637);
  char str[8];
  uint8_t buffer[6] = {0};
  if (floating_digit >6)
    floating_digit = 6;
  switch (floating_digit)
  {
    case 0:
      snprintf(str, sizeof(str) , "%.0f", digit);
    break;
    case 1:
      snprintf(str, sizeof(str) , "%.1f", digit);
    break;
    case 2:
      snprintf(str, sizeof(str) , "%.2f", digit);
    break;
    case 3:
      snprintf(str, sizeof(str) , "%.3f", digit);
    break;
    case 4:
      snprintf(str, sizeof(str) , "%.4f", digit);
    break;
    case 5:
      snprintf(str, sizeof(str) , "%.5f", digit);
    break;
    case 6:
      snprintf(str, sizeof(str) , "%.6f", digit);
    break;
  } 
  if (tm1637->show_zero == false)
  {
    for (int8_t i = strlen(str) - 1; i > 0; i--)
    {
      if (str[i] == '0')
        str[i] = 0;
      else
        break;            
    }
  }
  uint8_t index = 0;  
  for (uint8_t i=0; i < 7; i++)
  {
    if (str[i] == '-')
    {
      buffer[index] = _tm1637_minus;
      index++;
    }
    else if((str[i] >= '0') && (str[i] <= '9'))
    {
      buffer[index] = _tm1637_digit[str[i] - 48];
      index++;
    }
    else if (str[i] == '.')
    {
      if (index > 0)
        buffer[index - 1] |= _tm1637_dot;      
    }
    else
    {
      buffer[index] = 0;
      break;
    }
  }
  tm1637_write_raw(tm1637, buffer, 6, pos);              
  tm1637_unlock(tm1637);  
}
//#######################################################################################################################
void tm1637_show_zero(tm1637_t *tm1637, bool enable)
{
  tm1637->show_zero = enable;
}
//#######################################################################################################################
void tm1637_fill(tm1637_t *tm1637, bool enable)
{
	if (enable)
		tm1637_write_segment(tm1637, _tm1637_on, 6, 0);
	else
		tm1637_write_segment(tm1637, _tm1637_off, 6, 0);		
}
//#######################################################################################################################
uint8_t TM1637_CharToSegment(char c)
{
    switch (c)
    {
        case '0': return Seg0;
        case '1': return Seg1;
        case '2': return Seg2;
        case '3': return Seg3;
        case '4': return Seg4;
        case '5': return Seg5;
        case '6': return Seg6;
        case '7': return Seg7;
        case '8': return Seg8;
        case '9': return Seg9;

        case 'A': case 'a': return SegA;
        case 'B': case 'b': return SegB;
        case 'C': case 'c': return SegC;
        case 'D': case 'd': return SegD;
        case 'E': case 'e': return SegE;
        case 'F': case 'f': return SegF;
        case 'G': case 'g': return SegG;
        case 'H': case 'h': return SegH;
        case 'I': case 'i': return SegI;
        case 'J': case 'j': return SegJ;
        case 'K': case 'k': return SegK;
        case 'L': case 'l': return SegL;
        case 'M': case 'm': return SegM;
        case 'N': case 'n': return SegN;
        case 'O': case 'o': return SegO;
        case 'P': case 'p': return SegP;
        case 'Q': case 'q': return SegQ;
        case 'R': case 'r': return SegR;
        case 'S': case 's': return SegS;
        case 'T': case 't': return SegT;
        case 'U': case 'u': return SegU;
        case 'V': case 'v': return SegV;
        case 'W': case 'w': return SegW;
        case 'X': case 'x': return SegX;
        case 'Y': case 'y': return SegY;
        case 'Z': case 'z': return SegZ;

        case '-': return Seg_;
        case ' ': return 0x00;

        default:  return 0x00;
    }
}

//#######################################################################################################################
void tm1637_write_string(tm1637_t *tm1637, char *str)
{
    uint8_t seg[4] = {0, 0, 0, 0};
    uint8_t pos = 0;

    for (uint8_t i = 0; str[i] != '\0' && pos < 4; i++)
    {
        if (str[i] == '.')
        {
            if (pos > 0)
                seg[pos - 1] |= 0x80;   // DOT
        }
        else
        {
            seg[pos] = TM1637_CharToSegment(str[i]);
            pos++;
        }
    }

    tm1637_write_segment(tm1637, seg, 4, 0);
}








