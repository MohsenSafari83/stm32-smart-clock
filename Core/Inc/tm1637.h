#ifndef _TM1637_H_
#define _TM1637_H_



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
//-----------------------------------------------------------------------------------



/*
    Edited by Ali Fakoor and the Houshidar Team
*/
 
#ifdef __cplusplus
extern "C" {
#endif
  
#include <stdint.h>
#include <stdbool.h>
#include "main.h"

//####################################################################################################################

typedef struct
{
  uint8_t               lock;
  uint8_t               brightness;
  bool                  show_zero;    
  GPIO_TypeDef          *gpio_clk;
  GPIO_TypeDef          *gpio_dat;
  uint16_t              pin_clk;   
  uint16_t              pin_dat;  
  
}tm1637_t;

//####################################################################################################################

void tm1637_init(tm1637_t *tm1637, GPIO_TypeDef *gpio_clk, uint16_t pin_clk, GPIO_TypeDef *gpio_dat, uint16_t pin_dat);
void tm1637_brightness(tm1637_t *tm1637, uint8_t brightness_0_to_7);
void tm1637_write_segment(tm1637_t *tm1637, const uint8_t *segments, uint8_t length, uint8_t pos);
void tm1637_write_int(tm1637_t *tm1637, int32_t digit, uint8_t pos);
void tm1637_write_float(tm1637_t *tm1637, float digit, uint8_t floating_digit, uint8_t pos);
void tm1637_show_zero(tm1637_t *tm1637, bool enable);
void tm1637_fill(tm1637_t *tm1637, bool enable);
void tm1637_write_string(tm1637_t *tm1637, char *str);
//####################################################################################################################

#ifdef __cplusplus
}
#endif

#endif
