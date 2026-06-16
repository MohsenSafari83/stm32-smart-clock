# Smart Clock — STM32G030 Firmware

Single-file firmware for a feature clock on **STM32G030C8Tx** (LQFP48). All logic lives in one translation unit (`main.c`) with no RTOS, no interrupts — pure polling on `HAL_GetTick()`.

---

## 1. Overview

The system implements a digital clock with:

- TM1637 4-digit 7-segment display (bit-banged GPIO)
- Software RTC (hours, minutes, seconds)
- 3-button menu navigation (SW1=menu, SW2=up, SW3=down)
- NTC thermistor temperature measurement via ADC1 (Beta-parameter equation)
- RGB LED controlled by TIM1 PWM (3 channels), intensity scales with temperature
- Periodic UART time broadcast (`TIME HH:MM:SS` at configurable interval)
- Persistent settings + boot counter stored on I2C EEPROM (AT24Cxx)
- Auto-save to EEPROM 5 seconds after the last key press

All background tasks run inside a single polling loop — no DMA, no interrupts.

---

## 2. Architecture

Three layers with strict one-way dependencies:

| Layer | Contents | Origin |
|-------|----------|--------|
| **HAL** | GPIO, I2C, USART init + register-level drivers | CubeMX-generated (read-only) |
| **Drivers** | TM1637 bitbang, ADC sampler, TIM1 PWM, I2C EEPROM | Manual — inside `main.c` |
| **Application** | FSM, button debounce, display logic, settings management | Manual — inside `main.c` |

```
main.c ←────── CubeMX HAL (gpio, i2c, usart)
   │
   ├── TM1637 bitbang
   ├── ADC1 + NTC Beta equation
   ├── TIM1 PWM (RGB)
   ├── I2C2 EEPROM
   ├── GPIO (buttons, LEDs)
   └── USART2 (time broadcast)
```

---

## 3. Finite State Machine

Three states, implemented in `main.c:897–1052`:

### ST_NORMAL
- Display current time; show boot counter for 2 seconds after reset
- Short SW1 → enter menu
- RGB LED adjusts brightness proportional to temperature (20–50°C → PWM 0–999)
- Periodic UART broadcast if enabled

### ST_MENU
Seven items navigated by SW1: Hour → Minute → Second → Display on/off → UART on/off → UART interval → RGB channel.
- SW1 advances to next item (past item 7 returns to normal)
- Long SW1 returns to normal
- SW2/SW3 on toggle items (3–4) change value immediately
- SW2/SW3 on numeric items (0,1,2,5,6) enter ST_EDIT

### ST_EDIT
- SW2 increment / SW3 decrement
- SW1 confirms and returns to menu
- Long SW1 aborts to normal (no save)
- All values clamped to valid ranges (e.g., UART interval 1–60 s)

---

## 4. Pin Map

| Pin | Mode | Function |
|-----|------|----------|
| PA0 | Analog | ADC1_IN0 — NTC thermistor |
| PA2 | AF push-pull | USART2 TX |
| PA3 | Floating input | USART2 RX |
| PA8 | AF2 (TIM1) | PWM CH1 — Red (RGB) |
| PA9 | AF2 (TIM1) | PWM CH2 — Green (RGB) |
| PA10 | AF2 (TIM1) | PWM CH3 — Blue (RGB) |
| PA11 | AF6 (I2C2) | SCL — EEPROM |
| PA12 | AF6 (I2C2) | SDA — EEPROM |
| PA13 | Serial Wire | SWDIO |
| PA14 | Serial Wire | SWCLK |
| PB3 | Output (Low) | LED_1 |
| PB13 | Input (Pull-up) | SW_1 (menu) |
| PB14 | Input (Pull-up) | SW_2 (up) |
| PB15 | Input (Pull-up) | SW_3 (down) |
| PC6 | Open-Drain | TM1637 DIO |
| PC7 | Open-Drain | TM1637 CLK |
| PD3 | Output (Low) | LED_2 (UART indicator) |

### Peripherals

| Peripheral | Details |
|------------|---------|
| **TM1637** | 4-digit 7-segment display, bit-banged on PC6/PC7 |
| **ADC1** | 12-bit, software trigger, sampling time 79.5 cycles |
| **TIM1** | Prescaler=7, Period=999 → ~1 kHz PWM on CH1-3 |
| **I2C2** | Standard 100 kHz, EEPROM AT24Cxx at address 0x50 |
| **USART2** | 9600-8-N-1, async |
| **EEPROM** | 7-byte layout: version(1), display(1), UART enable(1), UART interval(1), RGB channel(1), boot counter(2) |

---

## 5. System Timing

All intervals are polled with `HAL_GetTick()` delta timing:

| Task | Period | Details |
|------|--------|---------|
| Button scan | 5 ms | 50 ms debounce, 1000 ms long-press detection |
| Display refresh | 50 ms | Segment data to TM1637, colon blink at 500 ms |
| Clock tick | 1000 ms | Increments software RTC seconds |
| Temperature read | 2000 ms | ADC conversion + Beta-parameter calculation |
| RGB update | 500 ms | Maps 20–50°C to PWM duty 0–999 |
| UART broadcast | Configurable 1–60 s | Transmits `TIME HH:MM:SS` |
| EEPROM auto-save | 5 s after last change | Persists settings |

---

## 6. Module Reference (all in `main.c`)

### TM1637 Bitbang (`main.c:230–393`)
GPIO bit-banging implementation of the TM1637 protocol. Low-level primitives (`start`, `stop`, `write_byte`) and high-level API (`write_segment`, `write_string`, `fill`). No external dependencies beyond `HAL_GPIO_WritePin` / `HAL_GPIO_ReadPin`.

### Display Wrapper (`main.c:395–431`)
Thin layer over TM1637 supporting three modes: time display (with colon blink), text display (4 chars), and off.

### Clock (`main.c:436–470`)
Pure software RTC — three `uint8_t` fields (hours, minutes, seconds). Functions: `clock_init`, `clock_tick`, `clock_get`, `clock_set`. No HAL dependency. `clock_tick` is called every 1000 ms in the main loop.

### ADC Temperature (`main.c:474–535`)
Initializes ADC1 (12-bit, software trigger, 79.5 cycle sample time). `adc_temp_read` performs one conversion and computes temperature using the Beta-parameter equation. Out-of-range readings (0 or ≥ 4094) return a safe 25°C default.

### RGB LED (`main.c:538–593`)
Initializes TIM1 with three PWM channels. `rgb_set` sets the duty cycle on the selected channel; unused channels are driven to 0.

### EEPROM (`main.c:598–614`)
Two functions for I2C read/write to AT24Cxx at address 0x50, 16-bit memory addressing. A 5 ms write-cycle delay (`HAL_Delay(5)`) follows each write.

### Buttons (`main.c:620–696`)
Scans three GPIOs with software debounce (50 ms) and long-press detection (1000 ms on SW1). Events: `BTN_NONE`, `BTN_UP`, `BTN_DOWN`, `BTN_MODE_SHORT`, `BTN_MODE_LONG`. A single event queue stores the most recent event.

### FSM (`main.c:844–1057`)
Central orchestrator. Each loop iteration: runs timed background tasks, reads the button event, and dispatches to the current state handler.

---

## 7. Execution Flow

### Boot Sequence

```
HAL_Init()
    └── SystemClock_Config()   ─── HSI 16 MHz, AHB/2 = 8 MHz HCLK
    └── MX_GPIO_Init()          ─── GPIO pins + buttons
    └── MX_USART2_UART_Init()   ─── 9600 baud
    └── MX_I2C2_Init()          ─── 100 kHz
    └── tm1637_init()           ─── TM1637 pin config
    └── buttons_init()          ─── Button context init
    └── clock_init(12,0,0)      ─── Default time
    └── adc_temp_init()         ─── ADC1 setup
    └── rgb_init()              ─── TIM1 PWM setup
    └── app_fsm_init()          ─── Load EEPROM, increment boot counter
```

### Main Loop

```
while(1)
    └── app_fsm_run()
        ├── buttons_scan()         ─── every 5 ms
        ├── display_refresh()      ─── every 50 ms
        ├── clock_tick()           ─── every 1000 ms
        ├── adc_temp_read()        ─── every 2 s
        ├── clock_get()            ─── every cycle
        ├── rgb_set()              ─── every 500 ms
        ├── uart_broadcast()       ─── per configured interval
        ├── buttons_get_event()    ─── read event
        ├── switch(fsm_state)      ─── FSM processing
        └── settings_save()        ─── 5 s after last change
```

---

## 8. Design Decisions

### Centralized FSM
All system logic is concentrated in `app_fsm_run()`. This simplifies debugging and eliminates concurrency concerns at the cost of testability — subsystems cannot be exercised in isolation.

### Polling over RTOS
With 8 KB of RAM and low task count, a `HAL_GetTick()` polling loop is sufficient. The system is not designed for hard real-time or low-latency response.

### No DMA or Interrupts
All I2C, UART, and ADC operations are blocking. This keeps the code simple but introduces bounded latency during UART transmission (~33 ms at 9600 baud) and EEPROM writes (5 ms `HAL_Delay`).

### Single-File Structure
All application logic resides in `main.c` (1134 lines). This is deliberately student-project scoped — every function is visible in one place at the cost of modularity.

---

## 9. Limitations

- **UART blocking**: `HAL_UART_Transmit` uses a 100 ms timeout; transmission at 9600 baud takes ~33 ms, blocking the main loop.
- **EEPROM write delay**: A 5 ms blocking `HAL_Delay` follows each write for the AT24Cxx internal write cycle.
- **No RTC backup**: The software RTC loses time on power loss — no external RTC or battery backup.
- **16-bit boot counter**: Wraps to 0 after 65535 reboots.
- **Math library dependency**: `logf` requires MicroLIB or the standard math library in Keil (Project → Options → Target → enable "Use MicroLIB").

---

## 10. Conclusion

This is an **academic / student-project** embedded system. Single-file structure, polling architecture, and deliberately minimal abstraction make it suitable for learning STM32 firmware fundamentals.

**Ready for laboratory demonstration and submission.** All critical bugs (stuck clock, unbounded UART timeout, excessive EEPROM delay) have been fixed. The blocking I/O model limits its suitability for production or latency-sensitive applications.
