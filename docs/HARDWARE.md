# Hardware Reference — STM32 Smart Clock

> Detailed pin mapping, peripheral configuration, and hardware design notes.

---

## Pin Assignment

| Pin   | Function       | Direction | Peripheral    | Purpose                 |
|-------|---------------|-----------|---------------|-------------------------|
| PA0   | ADC1_IN0       | Analog    | ADC1          | NTC thermistor input    |
| PA2   | USART2_TX      | Output    | USART2        | UART serial transmit    |
| PA3   | USART2_RX      | Input     | USART2        | UART RX (unused)        |
| PA8   | TIM1_CH1       | AF2       | TIM1          | RGB LED — Red           |
| PA9   | TIM1_CH2       | AF2       | TIM1          | RGB LED — Green         |
| PA10  | TIM1_CH3       | AF2       | TIM1          | RGB LED — Blue          |
| PA11  | I2C2_SCL       | AF6-OD    | I2C2          | EEPROM clock            |
| PA12  | I2C2_SDA       | AF6-OD    | I2C2          | EEPROM data             |
| PB3   | GPIO_Output    | PP        | GPIOB         | LED1 — UART activity    |
| PB13  | GPIO_Input     | Input     | GPIOB         | SW1 — UP                |
| PB14  | GPIO_Input     | Input     | GPIOB         | SW2 — DOWN              |
| PB15  | GPIO_Input     | Input     | GPIOB         | SW3 — SELECT            |
| PC6   | GPIO_OD        | I/O       | GPIOC         | TM1637 DIO              |
| PC7   | GPIO_OD        | Output    | GPIOC         | TM1637 CLK              |
| PD3   | GPIO_Output    | PP        | GPIOD         | LED2 — Menu active      |

16 pins used across 4 ports. 3 alternate function mappings: AF1 (USART2), AF2 (TIM1), AF6 (I²C2). TM1637 pins reconfigured from push-pull to open-drain at runtime by `tm1637_init()`.

---

## Clock Tree

```
HSI 16 MHz → AHB ÷2 → HCLK 8 MHz
  ├─ APB1: USART2 (8 MHz → 9600 baud), I2C2 (8 MHz → 100 kHz)
  ├─ APB2: TIM1 (PSC=7 → 1 MHz → ARR=999 → 1 kHz PWM)
  ├─ ADC: HSI ÷4 → ~2 MHz (79.5 sample cycles → ~10 µs)
  └─ LSE: 32.768 kHz → async=127 + sync=255 → ~1.01 Hz
```

---

## Peripheral Configuration

### NTC Thermistor
- 10 kΩ @ 25°C, B=3950
- R14 = 10 kΩ to GND (voltage divider)
- Beta equation: T = 1 / (A + B·ln(R) + C·ln(R)³) − 273
- A = 1.129E-3, B = 2.341E-4, C = 8.767E-8

### ADC1
- 12-bit resolution, channel 0 (PA0)
- 79.5 sample cycles, continuous mode configured
- DMA configured but unused (polled mode in practice)

### TIM1
- 1 kHz PWM, 3 independent channels
- PSC = 7 (1 MHz counter tick), ARR = 999
- CH1(PA8) = Red, CH2(PA9) = Green, CH3(PA10) = Blue

### USART2
- 9600 8N1, PA2(TX) only, PA3(RX) unused
- Blocking HAL_UART_Transmit(), ~4 ms per message

### I²C2
- 100 kHz standard mode
- AT24C128 EEPROM at 0xA0
- 128 Kbit, used for settings persistence + boot_count

### RTC + LSE
- LSE 32.768 kHz crystal
- Asynchronous prescaler = 127, Synchronous prescaler = 255
- Result: ~1.01 Hz update rate
- Known issue: MX_RTC_Init() sets time to 00:36:00 on every boot

### TM1637
- 4-digit × 7-segment display
- Bit-banged I²C-like protocol at 20 µs/bit
- PC6(DIO) = open-drain bidirectional, PC7(CLK) = push-pull output
- Start condition, address frame (0x40), 4 data frames, ACK sampling, STOP
