# Architecture Reference — STM32 Smart Clock

> 5-layer bare-metal firmware stack on STM32G030 Cortex-M0+.

---

## Layer Overview

| Layer | Content | Key files |
|---|---|---|
| **Application** | Menu FSM, temperature sensing, display management, UART reporting, settings persistence | `main.c` |
| **HAL** | STM32G0xx hardware abstraction — GPIO, ADC, TIM, I²C, RTC, UART, DMA | `stm32g0xx_hal_*.c` |
| **Driver** | TM1637 bit-banged driver, NTC Beta math (inline), CubeMX init code | `tm1637.c`, `mx_*_init()` |
| **CMSIS-Core** | Cortex-M0+ core — SysTick, NVIC, startup, SystemCoreClock | `system_stm32g0xx.c`, `startup_stm32g030xx.s` |
| **Hardware** | STM32G030 MCU, NTC, TM1637, EEPROM, RGB LED, buttons, LSE | PCB / schematic |

---

## Module Dependency Map

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
  ├── tm1637.h, tm1637_config.h
  ├── main.h   → HAL_GPIO
  └── string.h → snprintf
```

---

## Super-Loop Sequence

The super-loop dispatches all application modules sequentially every ~6 ms:

1. HAL_GetTick() poll — <1 µs
2. Display refresh check — ~5 ms (every 1 s)
3. Button polling ×3 — ~6 µs (every loop, 250 ms debounce)
4. Temperature processing — ~10 ms
5. UART handling — ~1–4 ms (every 5 s)
6. HAL_Delay(5) — 5 ms pacing

Worst-case iteration: ~24 ms (display refresh + UART TX coincide).

---

## Known Issues

| Issue | Severity | Impact |
|---|---|---|
| ADC DMA configured but polled manually | Medium | Dead config, no functional issue |
| RTC time resets to 00:36 every boot | High | Clock always wrong after power cycle |
| main.c ~700 lines monolithic | Medium | Hard to test, hard to extend |
| Software float on M0+ (no FPU) | Low | logf() and snprintf(%f) are slow |
| All I/O is blocking/polled | Medium | CPU wasted during UART TX and ADC conversion |
| ADC sampled every loop (~200 Hz) | Low | Sensor bandwidth is < 1 Hz |
| EEPROM boot_count write each boot | Medium | AT24C128 rated ~1M write cycles |
