# STM32 Smart Clock — Complete Technical Review

**Project:** Smart Clock with NTC, RTC, EEPROM, RGB LED, and UART Menu  
**MCU:** STM32G030 (Cortex-M0+, 8 MHz HCLK)  
**Toolchain:** Keil MDK-ARM (STM32CubeMX generated)  
**RTOS:** None (bare-metal super-loop)  
**Review Date:** 2026-07-24  
**Reviewer:** Senior Embedded Systems Engineering

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Hardware Architecture](#3-hardware-architecture)
4. [Firmware Architecture](#4-firmware-architecture)
5. [Finite State Machine Analysis](#5-finite-state-machine-analysis)
6. [Timing Analysis](#6-timing-analysis)
7. [GPIO Allocation](#7-gpio-allocation)
8. [Peripheral Analysis](#8-peripheral-analysis)
9. [Data Flow Analysis](#9-data-flow-analysis)
10. [Engineering Review](#10-engineering-review)
11. [Improvement Roadmap](#11-improvement-roadmap)

---

## 1. Project Overview

This firmware implements a **digital clock** with the following features:

- **Timekeeping:** RTC driven by an external 32.768 kHz LSE crystal
- **Display:** 4-digit 7-segment TM1637 LED display (bit-banged protocol)
- **Temperature Sensing:** NTC thermistor read via ADC1, converted using the Beta parameter equation
- **RGB LED:** PWM-driven RGB LED with color mapped to temperature
- **User Interface:** 3-button menu system (UP/DOWN/SELECT) for setting time, toggling display, selecting LED color, and configuring UART output
- **Data Logging:** Periodic UART output of time, temperature, and boot count
- **Persistence:** Settings stored in external I2C EEPROM

**Total source lines (project code, excluding HAL/CMSIS):** ~1,700 lines across 18 files

---

## 2. System Architecture

### 2.1 High-Level Architecture

The system uses a **layered bare-metal architecture** with four distinct layers:

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                         │
│  main.c — Menu FSM, Temp Sensing, Display, UART, Settings   │
├─────────────────────────────────────────────────────────────┤
│               HARDWARE ABSTRACTION LAYER (HAL)              │
│  STM32G0xx HAL Driver — GPIO, ADC, TIM, I2C, RTC, UART     │
├─────────────────────────────────────────────────────────────┤
│                    DRIVER LAYER                              │
│  tm1637.c — TM1637 bit-banged display driver                │
│  CubeMX-generated MX_*_Init() functions                     │
├─────────────────────────────────────────────────────────────┤
│                    CMSIS-CORE                                │
│  Cortex-M0+ core: SysTick, NVIC, startup, SystemCoreClock   │
├─────────────────────────────────────────────────────────────┤
│                    HARDWARE                                  │
│  STM32G030 MCU + NTC + TM1637 + EEPROM + RGB LED + Buttons │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Module Dependency Map

```
main.c
  ├── adc.h    → Update_Temperature_LED()
  ├── rtc.h    → Refresh_Display_Output(), Handle_UART_Transmission()
  ├── i2c.h    → Load/Save_Settings_To_EEPROM()
  ├── usart.h  → Handle_UART_Transmission()
  ├── tim.h    → RGB LED PWM control
  ├── gpio.h   → Button reading, indicator LEDs
  ├── tm1637.h → Display output
  └── math.h   → logf() for Beta equation

tm1637.c
  ├── tm1637.h
  ├── tm1637_config.h
  ├── main.h   → HAL_GPIO
  └── string.h → snprintf
```

### 2.3 Architecture Diagram

See the separate file: [`docs/software_architecture.svg`](software_architecture.svg)

---

## 3. Hardware Architecture

### 3.1 Hardware Block Diagram

See the separate file: [`docs/hw_block_diagram.svg`](hw_block_diagram.svg)

### 3.2 Pin Assignment Summary

| Pin   | Function     | Direction | Peripheral    | Purpose                    |
|-------|-------------|-----------|---------------|----------------------------|
| PA0   | ADC1_IN0     | Analog    | ADC1          | NTC thermistor input       |
| PA2   | USART2_TX    | Output    | USART2        | UART serial transmit       |
| PA3   | USART2_RX    | Input     | USART2        | UART serial receive        |
| PA8   | TIM1_CH1     | Alternate | TIM1          | RGB LED — Red channel      |
| PA9   | TIM1_CH2     | Alternate | TIM1          | RGB LED — Green channel    |
| PA10  | TIM1_CH3     | Alternate | TIM1          | RGB LED — Blue channel     |
| PA11  | I2C2_SCL     | Alternate | I2C2          | EEPROM clock               |
| PA12  | I2C2_SDA     | Alternate | I2C2          | EEPROM data                |
| PB3   | GPIO Output  | Output    | GPIOB         | LED1 — UART activity       |
| PB13  | GPIO Input   | Input     | GPIOB         | SW1 — UP button            |
| PB14  | GPIO Input   | Input     | GPIOB         | SW2 — DOWN button          |
| PB15  | GPIO Input   | Input     | GPIOB         | SW3 — SELECT button        |
| PC6   | GPIO OD      | I/O       | GPIOC         | TM1637 DIO (data)          |
| PC7   | GPIO OD      | Output    | GPIOC         | TM1637 CLK (clock)         |
| PD3   | GPIO Output  | Output    | GPIOD         | LED2 — Menu active         |

### 3.3 Hardware Design Observations

**Strengths:**
- LSE crystal provides accurate RTC timekeeping
- External EEPROM allows settings persistence independent of MCU flash
- PWM-driven RGB LED enables smooth intensity control

**Weaknesses:**
- Buttons have no external pull-up resistors (`GPIO_NOPULL`) — relies on external hardware pull-ups
- No external voltage reference for ADC (uses VDD = 3.3V as reference, which may be noisy)
- TM1637 display uses bit-banged GPIO instead of hardware I2C/SPI, consuming CPU cycles
- No isolation or protection on any I/O pins

---

## 4. Firmware Architecture

### 4.1 Startup Sequence

```
Reset Vector
  └─ startup_stm32g030xx.s
       └─ SystemInit()         — Configure vector table
            └─ main()
                 ├─ HAL_Init()                 — SysTick 1ms, HAL config
                 ├─ SystemClock_Config()       — HSI 16 MHz → HCLK 8 MHz + LSE
                 ├─ MX_GPIO_Init()             — All GPIO pins
                 ├─ MX_DMA_Init()              — DMA1_CH1 (unused)
                 ├─ MX_ADC1_Init()             — ADC, continuous+DMA mode
                 ├─ MX_I2C2_Init()             — I2C for EEPROM
                 ├─ MX_USART2_UART_Init()      — UART 9600 8N1
                 ├─ MX_TIM1_Init()             — PWM 1 kHz 3-channel
                 ├─ MX_RTC_Init()              — RTC + LSE (resets time!)
                 │
                 ├─ HAL_ADCEx_Calibration_Start()
                 ├─ Load_Settings_From_EEPROM()
                 ├─ tm1637_init()              — Display init (OD GPIO)
                 ├─ tm1637_brightness(5)
                 ├─ HAL_TIM_PWM_Start() ×3
                 ├─ Refresh_Display_Output()
                 │
                 └─ while(1)  [Super Loop]
```

### 4.2 Main Loop Execution

```
while (1):
  1. Check 1-second display refresh timer (HAL_GetTick)
  2. Poll 3 buttons with 250 ms debounce
  3. Call Update_Temperature_LED() — ADC + math + PWM
  4. Call Handle_UART_Transmission() — RTC read + UART TX
  5. HAL_Delay(5)
```

### 4.3 Software Architecture Strengths

- **CubeMX-generated structure** provides clean separation of peripheral initialization from application code
- **TM1637 driver is modular** — self-contained with config, proper open-drain I/O, and FreeRTOS compatibility option
- **Menu FSM is complete** — covers all user-configurable settings with wrap-around navigation
- **Global state machine** makes the UI flow predictable and debuggable

### 4.4 Software Architecture Weaknesses

- **Monolithic main.c** — all application logic in a single ~700-line file with global variables
- **No hardware abstraction** — application code directly calls HAL and manipulates GPIO registers
- **No separation of concerns** — temperature math, display formatting, UART formatting all interleaved
- **No error handling strategy** — HAL errors trigger `Error_Handler()` which disables interrupts and hangs
- **No modularization of business logic** — no separate modules for `temperature.c`, `menu.c`, `settings.c`, `display.c`
- **CubeMX regeneration will overwrite custom code** if not carefully placed in USER CODE sections

---

## 5. Finite State Machine Analysis

### 5.1 FSM Diagram

See the separate file: [`docs/fsm_diagram.svg`](fsm_diagram.svg)

### 5.2 State Definitions

| State                     | ID  | Description                          | Display     |
|---------------------------|-----|--------------------------------------|-------------|
| `STATE_NORMAL`            | 0   | Clock display, all systems active    | HH:MM       |
| `STATE_MAIN_MENU`         | 1   | Menu navigation (5 items)            | SETN/DISP/COLR/INTV/UART |
| `STATE_SUB_SET_TIME_H`    | 2   | Set hours                            | H.HH        |
| `STATE_SUB_SET_TIME_M`    | 3   | Set minutes                          | M.MM        |
| `STATE_SUB_TOGGLE_DISPLAY`| 4   | Toggle display on/off                | D.ON/D.OFF  |
| `STATE_SUB_SET_COLOR`     | 5   | Select LED color                     | GREN/RED/BLU|
| `STATE_SUB_UART_INTERVAL` | 6   | Set UART report interval (1-99 s)    | U.UU        |
| `STATE_SUB_UART_TOGGLE`   | 7   | Toggle UART on/off                   | U.ON/U.OFF  |

### 5.3 State Transitions

```
STATE_NORMAL
  └─ SEL(3) → STATE_MAIN_MENU (menu_index = 0)

STATE_MAIN_MENU
  ├─ UP(1)   → menu_index-- (wrap 0-4)
  ├─ DOWN(2) → menu_index++ (wrap 0-4)
  └─ SEL(3)  → sub-state based on menu_index:
       ├─ 0 → STATE_SUB_SET_TIME_H
       ├─ 1 → STATE_SUB_TOGGLE_DISPLAY
       ├─ 2 → STATE_SUB_SET_COLOR
       ├─ 3 → STATE_SUB_UART_INTERVAL
       └─ 4 → STATE_SUB_UART_TOGGLE

STATE_SUB_SET_TIME_H
  ├─ UP(1)   → buffer_h++ (wrap 0-23)
  ├─ DOWN(2) → buffer_h-- (wrap 0-23)
  └─ SEL(3)  → STATE_SUB_SET_TIME_M

STATE_SUB_SET_TIME_M
  ├─ UP(1)   → buffer_m++ (wrap 0-59)
  ├─ DOWN(2) → buffer_m-- (wrap 0-59)
  └─ SEL(3)  → HAL_RTC_SetTime() → STATE_NORMAL

STATE_SUB_TOGGLE_DISPLAY
  ├─ UP(1)/DOWN(2) → settings.display_en ^= 1
  └─ SEL(3) → Save_Settings_To_EEPROM() → STATE_NORMAL

STATE_SUB_SET_COLOR
  ├─ UP(1)   → settings.led_color++ (wrap 0-2)
  ├─ DOWN(2) → settings.led_color-- (wrap 0-2)
  └─ SEL(3) → Save_Settings_To_EEPROM() → STATE_NORMAL

STATE_SUB_UART_INTERVAL
  ├─ UP(1)   → settings.uart_interval++ (wrap 1-99)
  ├─ DOWN(2) → settings.uart_interval-- (wrap 1-99)
  └─ SEL(3) → Save_Settings_To_EEPROM() → STATE_NORMAL

STATE_SUB_UART_TOGGLE
  ├─ UP(1)/DOWN(2) → settings.uart_en ^= 1
  └─ SEL(3) → Save_Settings_To_EEPROM() → STATE_NORMAL
```

### 5.4 FSM Design Assessment

**What works:**
- Simple, flat hierarchy is easy to understand
- All transitions are deterministic
- Display updates on every action

**What doesn't work:**
- No exit/back button — user must cycle through to complete an action to return to normal
- EEPROM written on every sub-menu exit even if nothing changed
- Menu index and buffer variables are global, not encapsulated
- No timeout — if user enters a sub-menu and walks away, the display stays in sub-menu mode forever

---

## 6. Timing Analysis

### 6.1 Timing Diagram

See the separate file: [`docs/timing_diagram.svg`](timing_diagram.svg)

### 6.2 Timing Constants

| Parameter               | Value      | Location      | Notes                           |
|-------------------------|------------|---------------|---------------------------------|
| Main loop delay         | 5 ms       | main.c:405    | HAL_Delay(5) at loop end        |
| ADC sampling interval   | ~5 ms      | main.c:402    | Every loop iteration (200 Hz!)  |
| Display refresh         | 1 s        | main.c:385    | HAL_GetTick() delta check       |
| Button debounce         | 250 ms     | main.c:393    | Excessive — should be 20-50 ms  |
| UART interval (default) | 5 s        | settings      | Configurable 1-99 s             |
| UART baud rate          | 9600       | usart.c:42    | 80 chars ~8.3 ms transmit time  |
| EEPROM write hold       | 10 ms      | main.c:114    | After I2C write                 |
| I2C timeout             | 100 ms     | main.c:100    | Both read and write             |
| PWM frequency           | 1 kHz      | tim.c:46-48   | 8 MHz /8 prescaler /1000 period |
| ADC sample time         | 79.5 cycles| adc.c:60      | ~10 µs at ADC clock             |
| SysTick                 | 1 ms       | HAL Init      | HAL_GetTick() timebase          |

### 6.3 Worst-Case Loop Timing

When all operations coincide:
```
Button polling:      ~5 µs
Display refresh:     ~5 ms (RTC I2C + TM1637 write)  [only every 1 s]
Temperature read:    ~2 ms (ADC + logf + PWM)
UART transmission:   ~15 ms (RTC + snprintf + TX @9600 + Delay)  [only at interval]
HAL_Delay(5):        5 ms
────────────────────────────────────
Total worst case:    ~27 ms
```

This means the main loop can miss its 5 ms target by 5x, and SysTick jitter exceeds acceptable bounds for any real-time application.

### 6.4 Critical Timing Issues

1. **ADC sampled at 200 Hz for < 1 Hz signal** — 200x faster than necessary
2. **`logf()` on M0+ (no FPU)** — each call costs 5,000-20,000 CPU cycles
3. **Blocking UART at 9600 baud** — 80 characters take ~8 ms during which the CPU is blocked
4. **`HAL_MAX_DELAY` in ADC poll** — if ADC fails, the entire loop hangs forever
5. **No interrupt-driven I/O** — everything is polled, no parallelism

---

## 7. GPIO Allocation

| Pin   | Label     | Direction | Mode        | Pull   | Speed   | Alternate | Purpose                    |
|-------|-----------|-----------|-------------|--------|---------|-----------|----------------------------|
| PA0   | —         | Analog    | Analog      | NOPULL | —       | —         | NTC ADC input              |
| PA2   | —         | Output    | AF_PP       | NOPULL | LOW     | AF1       | USART2 TX                  |
| PA3   | —         | Input     | AF_PP       | NOPULL | LOW     | AF1       | USART2 RX                  |
| PA8   | —         | Output    | AF_PP       | NOPULL | LOW     | AF2       | TIM1_CH1 — Red PWM         |
| PA9   | —         | Output    | AF_PP       | NOPULL | LOW     | AF2       | TIM1_CH2 — Green PWM       |
| PA10  | —         | Output    | AF_PP       | NOPULL | LOW     | AF2       | TIM1_CH3 — Blue PWM        |
| PA11  | —         | Alternate | AF_OD       | NOPULL | LOW     | AF6       | I2C2_SCL                   |
| PA12  | —         | Alternate | AF_OD       | NOPULL | LOW     | AF6       | I2C2_SDA                   |
| PB3   | LED_1     | Output    | PP          | NOPULL | LOW     | —         | UART activity indicator    |
| PB13  | SW_1      | Input     | Input       | NOPULL | —       | —         | UP button (active low)     |
| PB14  | SW_2      | Input     | Input       | NOPULL | —       | —         | DOWN button (active low)   |
| PB15  | SW_3      | Input     | Input       | NOPULL | —       | —         | SELECT button (active low) |
| PC6   | DIO       | I/O       | OD         | NOPULL | HIGH→LOW| —         | TM1637 data line           |
| PC7   | CLK       | Output    | OD         | NOPULL | HIGH→LOW| —         | TM1637 clock line          |
| PD3   | LED_2     | Output    | PP          | NOPULL | LOW     | —         | Menu active indicator      |

**Note:** PC6/PC7 initially configured as `GPIO_MODE_OUTPUT_PP` in `MX_GPIO_Init()` (`gpio.c:68-73`), then reconfigured as `GPIO_MODE_OUTPUT_OD` in `tm1637_init()` (`tm1637.c:104-111`). The open-drain mode is correct for TM1637 communication.

---

## 8. Peripheral Analysis

### 8.1 ADC1

| Parameter              | Value             | Notes                               |
|------------------------|-------------------|-------------------------------------|
| Resolution             | 12-bit            | 0-4095                              |
| Clock prescaler        | PCLK/4            | ~2 MHz (from 8 MHz HCLK)           |
| Sampling time          | 79.5 cycles       | ~10 µs                              |
| Continuous conversion  | ENABLED           | **Contradicts polled usage**        |
| DMA continuous requests| ENABLED           | DMA configured but never used       |
| External trigger       | Software (none)   |                                     |
| Calibration            | ADCEx_Calibration | Called once at startup              |

**Assessment:** Configuring continuous mode + DMA while using polled single-conversion is a major inconsistency. The DMA channel, interrupt, and continuous mode are wasted.

**Fix:** Set `ContinuousConvMode = DISABLE`, `DMAContinuousRequests = DISABLE`, or properly implement DMA-driven continuous conversion with a callback.

### 8.2 TIM1 (PWM)

| Parameter              | Value             | Notes                               |
|------------------------|-------------------|-------------------------------------|
| Prescaler              | 7                 | Timer clock = 8 MHz / (7+1) = 1 MHz|
| Period                 | 999               | PWM freq = 1 MHz / (999+1) = 1 kHz |
| Channels               | CH1, CH2, CH3     | Red, Green, Blue                    |
| PWM Mode               | PWM1              | Active high                         |
| Auto-reload preload    | ENABLED           | Smooth period changes               |
| Break/dead-time        | DISABLED          | Not needed for LED                  |

**Assessment:** Configuration is correct and well-chosen for LED PWM. 1 kHz is above visible flicker threshold. The `Pulse = 0` initial value ensures LEDs are off until explicitly set.

### 8.3 RTC

| Parameter              | Value             | Notes                               |
|------------------------|-------------------|-------------------------------------|
| Clock source           | LSE               | 32.768 kHz crystal                  |
| Hour format            | 24-hour           |                                     |
| Asynch prescaler       | 127               |                                     |
| Synch prescaler        | 255               | Combined: 32768 / 128 / 256 = 1 Hz  |
| Output                 | DISABLED          |                                     |
| Initial time           | 0:36:00           | **Reset every boot**                |
| Initial date           | Mon, Jan 1, 2000  | **Reset every boot**                |

**Critical issue:** The RTC time is unconditionally set on every power-up/reset (`rtc.c:66-84`). The backup domain is not checked to preserve time across resets. The `USER CODE BEGIN Check_RTC_BKUP` section is empty.

**Fix:** Read RTC backup register before initialization. If a magic value is present, skip time/date initialization.

### 8.4 I2C2

| Parameter              | Value             | Notes                               |
|------------------------|-------------------|-------------------------------------|
| Timing                 | 0x00201D2B        | CubeMX-generated for 100 kHz        |
| Addressing mode        | 7-bit             |                                     |
| Own address            | 0                 | Not used (MCU is master only)       |
| Analog filter          | ENABLED           |                                     |
| Digital filter         | 0                 |                                     |
| Pins                   | PA11 (SCL), PA12 (SDA) | AF6, open-drain               |

**Assessment:** Standard I2C master configuration. No issues. The comment in `i2c.c:85-86` mentioning PA9/PA10 in brackets is confusing but the actual pin assignment to PA11/PA12 is correct.

### 8.5 USART2

| Parameter              | Value             | Notes                               |
|------------------------|-------------------|-------------------------------------|
| Baud rate              | 9600              | Slow for a smart device             |
| Word length            | 8-bit             |                                     |
| Stop bits              | 1                 |                                     |
| Parity                 | None              |                                     |
| Mode                   | TX_RX             | RX configured but unused            |
| Hardware flow control  | None              |                                     |
| Oversampling           | 16x               |                                     |

**Assessment:** 9600 baud is slow. At 8 MHz, 115200 baud is easily achievable with standard clocks. The 8.3 ms transmit time for an 80-byte message is 3x the main loop period.

### 8.6 DMA1

| Parameter              | Value             | Notes                               |
|------------------------|-------------------|-------------------------------------|
| Channel                | Channel 1         |                                     |
| Request                | ADC1              |                                     |
| Direction              | Peripheral→Memory |                                     |
| Mode                   | CIRCULAR          |                                     |
| Data alignment         | Half-word (16-bit)| Correct for 12-bit ADC             |
| Priority               | LOW               |                                     |

**Assessment:** Configured but **never started**. `Update_Temperature_LED()` uses polled ADC, not DMA. The DMA interrupt handler in `stm32g0xx_it.c` is dead code.

### 8.7 GPIO

| Port | Pins Used | Notes                               |
|------|-----------|-------------------------------------|
| A    | 0,2,3,8,9,10,11,12 | ADC, UART, PWM, I2C      |
| B    | 3,13,14,15          | LED1, buttons             |
| C    | 6,7                 | TM1637 display            |
| D    | 3                   | LED2                      |

**Assessment:** All configuration is standard. No external interrupts used for buttons. TM1637 pins are initially configured as push-pull (`gpio.c:68-73`) then overridden to open-drain (`tm1637.c:104-111`) — redundant but functional.

---

## 9. Data Flow Analysis

### 9.1 Data Flow Diagrams

See the separate file: [`docs/data_flow.svg`](data_flow.svg)

### 9.2 Temperature Measurement Flow

```
NTC (voltage divider)
  │  Analog voltage (0-3.3V)
  ▼
ADC1 (PA0), 12-bit
  │  raw = 0-4095
  ▼
Update_Temperature_LED()
  │  v_out = raw × 3.3 / 4095
  │  r_ntc = 10k × v_out / (3.3 - v_out)
  │  inv_t = 1/298.15 + ln(r_ntc/10k) / 3950
  │  temp_c = 1/inv_t - 273.15
  │  pwm = (temp_c - 20) / (100 - 20) × 999
  ▼
TIM1 PWM channels
  │  CH1 (R) if led_color==0
  │  CH2 (G) if led_color==1
  │  CH3 (B) if led_color==2
  ▼
RGB LED — intensity proportional to temperature
```

**Issues:** logf() on M0+, no PWM clamping, 200 Hz sampling, all channels cleared before set.

### 9.3 User Interaction Flow

```
Button pressed (PB13/PB14/PB15)
  │  GPIO ReadPin → active low
  ▼
Debounce: 250 ms cooldown (HAL_GetTick delta)
  │  if cooldown expired → Execute_Button_Action(id)
  ▼
FSM switch(current_state)
  │  Updates state, menu_index, or settings based on button + state
  ▼
Refresh_Display_Output()
  │  RTC GetTime (I2C) → snprintf → tm1637_write_string
  ▼
4-digit TM1637 display updated
```

**Issues:** 250 ms debounce feels sluggish. No EXTI interrupts. EEPROM written even when nothing changed.

### 9.4 Settings Persistence Flow

```
System Boot
  │
  ▼
Load_Settings_From_EEPROM()
  │  I2C Mem_Read(address=0, size=sizeof(SystemSettings))
  │  If read fails → use defaults (display=on, color=0, uart=on, interval=5s)
  │  boot_count++
  │  I2C Mem_Write (persists boot_count)
  ▼
Runtime: User modifies settings via FSM
  │  Changes buffered in global `settings` struct
  ▼
Save_Settings_To_EEPROM() on sub-menu exit
  │  I2C Mem_Write + HAL_Delay(10)
  ▼
Settings survive power cycle
```

**Issues:** EEPROM write every boot (boot_count). No validation (magic number/version/CRC). No wear leveling.

---

## 10. Engineering Review

### 10.1 What Works Well

1. **TM1637 driver quality** — The driver is well-structured with proper open-drain GPIO, configurable timing, and optional FreeRTOS support. The character-to-segment mapping is comprehensive (full alphabet).

2. **Menu FSM completeness** — The state machine covers all necessary user-configurable parameters with wrap-around navigation. The pattern of UP/DOWN for adjustment and SELECT for confirm is intuitive.

3. **CubMX project structure** — Using CubeMX for peripheral initialization provides a solid foundation with clear USER CODE sections.

4. **RTC + LSE** — Using an external 32.768 kHz crystal for RTC is the correct approach for accurate timekeeping.

5. **PWM frequency choice** — 1 kHz is appropriate for LED dimming without visible flicker.

### 10.2 What Is Weak

1. **Monolithic main.c** — All business logic in one file. No modularization, no separation of concerns.

2. **ADC configuration contradiction** — Configured for continuous DMA but used in polled mode. The DMA channel is wasted.

3. **RTC time reset on every boot** — Fundamental design flaw. The battery-backed RTC is never preserved.

4. **Software floating-point on M0+** — `float`, `logf()`, `snprintf` with `%f` — all emulated in software. This is the single largest performance issue.

5. **No real-time architecture** — Everything blocks. No interrupt-driven I/O. No timer-based scheduling.

6. **200 Hz temperature sampling** — Sensor changes at < 1 Hz but sampled 200 times/second.

7. **EEPROM wear from boot_count** — Write cycle consumed on every boot.

8. **Error handling** — Any HAL error disables interrupts and hangs forever.

### 10.3 What Can Fail

| Failure Mode | Root Cause | Consequence |
|---|---|---|
| ADC hangs | `HAL_ADC_PollForConversion(HAL_MAX_DELAY)` | Entire clock freezes |
| EEPROM wear out | boot_count write every boot | Settings lost after 100K-1M boots |
| RTC time lost | Unconditional time set in MX_RTC_Init | Time resets after any power glitch |
| TM1637 display garbage | Missing EEPROM validation | Invalid segment patterns displayed |
| I2C bus lock | No bus recovery logic | EEPROM becomes inaccessible |
| LED full brightness at cold temps | No PWM clamping | Negative temperatures produce 100% duty |
| Boot loop if EEPROM corrupted | No CRC/version check | Settings constantly reset to defaults |

### 10.4 What Should Be Redesigned

1. **ADC subsystem** — Remove DMA config, use polled single-conversion with 1-second interval timer.
2. **RTC initialization** — Add backup register check to preserve time across resets.
3. **Temperature math** — Replace `float` + `logf()` with fixed-point + lookup table.
4. **Button input** — Use EXTI interrupts with hardware debounce instead of polling.
5. **Display formatting** — Replace `snprintf` with integer arithmetic for segment conversion.
6. **EEPROM strategy** — Add magic number, version, CRC. Only write when values actually change.
7. **Scheduling** — Use timer interrupts for periodic tasks instead of `HAL_GetTick()` polling.

### 10.5 FSM Appropriateness

The FSM approach is **appropriate** for this application's complexity. The menu system has well-defined states and deterministic transitions. However, the implementation could be improved:

- Use a **state transition table** (array of function pointers) instead of a large `switch` statement
- Encapsulate state data in a struct instead of using global variables
- Add a **timeout** to auto-return to NORMAL state
- Add a **back** navigation path (e.g., long-press SELECT to go back one level)

### 10.6 Timing Architecture Assessment

**Not professional-grade.** The timing architecture is the weakest aspect of this firmware:

- No deterministic scheduling
- No bounded execution time
- No interrupt-driven I/O
- Blocking calls throughout
- SysTick jitter due to variable-length main loop iterations

For a simple clock, this might be acceptable. But any addition of features (network, sensors, audio) would break the timing model entirely.

**Required for professional level:**
- Timer interrupt for the 1-second display refresh tick
- Interrupt-driven UART with DMA or ring buffer
- ADC reading triggered by timer, not polled in main loop
- Main loop becomes an idle/sleep hook with wake from interrupts

---

## 11. Improvement Roadmap

### Phase 1: Critical Bug Fixes (Immediate)

1. Fix ADC configuration — change to single conversion mode, remove DMA
2. Fix RTC initialization — add backup register check
3. Fix color display strings to match actual PWM channel mapping
4. Add PWM output clamping (`if (pwm > 999) pwm = 999; if (pwm < 0) pwm = 0;`)
5. Remove three-channel clear before single-channel set (PWM glitch fix)
6. Reduce button debounce from 250 ms to 30 ms
7. Remove duplicate `system_stm32g0xx.c` from build

### Phase 2: Performance Optimization (1-2 weeks)

1. Replace `float` temperature math with fixed-point integer arithmetic
2. Replace `logf()` with a precomputed lookup table (piecewise linear interpolation)
3. Replace all `snprintf()` usage with integer formatting for TM1637 segments
4. Reduce ADC sampling from 200 Hz to 1 Hz (use HAL_GetTick timer)
5. Increase UART baud rate from 9600 to 115200

### Phase 3: Architecture Improvement (2-4 weeks)

1. Modularize: split `main.c` into `temperature.c`, `display.c`, `menu.c`, `settings.c`, `uart_handler.c`
2. Add EEPROM magic number + version + CRC validation
3. Only write EEPROM when settings actually change (compare before write)
4. Store boot_count in RTC backup register instead of EEPROM
5. Add IWDG watchdog with 4-second timeout
6. Implement proper error handling with recovery instead of `while(1)`

### Phase 4: Professionalization (4-8 weeks)

1. Replace button polling with EXTI interrupts + debounce timer
2. Implement interrupt-driven UART TX with DMA
3. Add STOP mode with RTC alarm wake-up for battery-powered idle
4. Use TIM3 as a dedicated scheduler tick for periodic tasks
5. Add state transition table (function pointer array) for the menu FSM
6. Add unit tests for temperature conversion and menu logic
7. Implement software reset on critical fault (instead of infinite loop)
8. Migrate build to CMake for CI/CD compatibility

### Phase 5: Production Readiness (ongoing)

1. Add manufacturing test firmware (PCBA test)
2. Implement bootloader for OTA/UART firmware updates
3. Add RTC digital calibration for crystal tolerance
4. Implement EEPROM wear leveling
5. Add comprehensive error logging with timestamps via UART
6. Consider FreeRTOS if feature set expands beyond current scope
7. Perform EMI/ESD testing and add protective circuitry as needed
8. Create hardware abstraction layer (HAL) above ST HAL for portability

---

## References

- **STM32G0x3 Reference Manual** (RM0444)
- **STM32G030 Datasheet** (DS12921)
- **TM1637 Datasheet** — 4-digit LED driver
- **Beta Parameter Equation** — NTC thermistor linearization

---

*Document generated from source code analysis. All diagrams created from reverse-engineered firmware behavior.*

**Files:**
- `docs/hw_block_diagram.svg` — Hardware block diagram
- `docs/software_architecture.svg` — Software architecture layers
- `docs/fsm_diagram.svg` — Finite state machine diagram
- `docs/timing_diagram.svg` — Timing analysis diagram
- `docs/data_flow.svg` — Data flow diagrams
