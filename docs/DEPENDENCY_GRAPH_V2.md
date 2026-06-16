# Dependency Graph — Engineering Audit (v2 Flat)

## 1. Static Dependency Graph (`#include`)

```mermaid
flowchart TD
    subgraph USER[Application - single translation unit]
        main_c[main.c]
    end

    subgraph CFG[CubeMX-Generated HAL Wrappers]
        i2c_c[i2c.c<br/>defines hi2c2]
        usart_c[usart.c<br/>defines huart2]
        gpio_c[gpio.c]
        it_c[stm32g0xx_it.c]
        msp_c[stm32g0xx_hal_msp.c]
    end

    subgraph HDL[STM32 HAL Drivers]
        hal_core[stm32g0xx_hal.c]
        hal_adc[stm32g0xx_hal_adc.c]
        hal_tim[stm32g0xx_hal_tim.c]
        hal_i2c[stm32g0xx_hal_i2c.c]
        hal_uart[stm32g0xx_hal_uart.c]
        hal_gpio[stm32g0xx_hal_gpio.c]
    end

    subgraph STD[C Standard Library]
        libc[string.h, stdio.h, math.h]
    end

    %% main.c includes
    main_c -->|#include| main_h[main.h]
    main_c -->|#include| i2c_h[i2c.h]
    main_c -->|#include| usart_h[usart.h]
    main_c -->|#include| gpio_h[gpio.h]
    main_c --> libc

    %% Header chain
    i2c_h -->|#include main.h| main_h
    usart_h -->|#include main.h| main_h
    gpio_h -->|#include main.h| main_h
    main_h -->|#include| hal_core_h[stm32g0xx_hal.h]
    hal_core_h -->|#include| hal_conf[stm32g0xx_hal_conf.h]
    hal_conf -->|#include| HDL

    %% CubeMX .c files include their headers
    i2c_c --> i2c_h
    usart_c --> usart_h
    gpio_c --> gpio_h
    it_c -->|#include| main_h
    it_c -->|#include| it_h[stm32g0xx_it.h]
    msp_c -->|#include| main_h

    %% Extern links (runtime, not compile-time)
    main_c ---|reads hi2c2| i2c_c
    main_c ---|reads huart2| usart_c
```

### Anomaly Fixed — Redundant `extern` Declarations Removed

Previously `main.c` duplicated extern declarations that already existed in the included headers. These were removed in the refactor — no redundant externs remain.

| Removed | Already declared in |
|---------|---------------------|
| `extern I2C_HandleTypeDef hi2c2;` | `i2c.h:35` |
| `extern UART_HandleTypeDef huart2;` | `usart.h:35` |

---

## 2. Runtime Dependency Graph

```mermaid
flowchart TD
    subgraph INIT[Boot Sequence]
        main[main] -->|calls| init_disp[tm1637_init]
        main -->|calls| init_btn[buttons_init]
        main -->|calls| init_clk[clock_init]
        main -->|calls| init_adc[adc_temp_init]
        main -->|calls| init_rgb[rgb_init]
        main -->|calls| init_fsm[app_fsm_init]
    end

    subgraph LOOP[Main Loop - app_fsm_run - runs every iteration]
        direction TB
        loop_start[while 1]
        loop_start --> poll{delta timing}

        poll -->|5 ms| buttons_scan
        poll -->|50 ms| display_refresh
        poll -->|1 s| clock_tick
        poll -->|2 s| adc_temp_read
        poll -->|500 ms| rgb_set
        poll -->|config s| uart_broadcast
        poll -->|every cycle| clock_get
        poll -->|event| fsm[FSM switch]
        poll -->|5 s idle| settings_save
    end

    subgraph HAL[Peripheral HAL Calls]
        btn_hal[HAL_GPIO_ReadPin x3]
        disp_hal[HAL_GPIO_WritePin x bitbang]
        adc_hal[HAL_ADC_Start<br/>HAL_ADC_PollForConversion<br/>HAL_ADC_Stop]
        rgb_hal[HAL_TIM_PWM_Init<br/>__HAL_TIM_SET_COMPARE]
        uart_hal[HAL_UART_Transmit<br/>HAL_GPIO_WritePin LED2]
        eeprom_hal[HAL_I2C_Mem_Write<br/>HAL_I2C_Mem_Read<br/>HAL_Delay]
    end

    buttons_scan --> btn_hal
    display_refresh --> disp_hal
    adc_temp_read --> adc_hal
    rgb_set --> rgb_hal
    uart_broadcast --> uart_hal
    settings_save --> eeprom_hal
    settings_load --> eeprom_hal
```

---

## 3. Structural Analysis

### Circular Dependencies

**None detected.** The dependency graph is a strict DAG:

```
main.c → CubeMX headers → HAL headers → CMSIS
main.c → CubeMX .c files (via extern handles only)
```

No module depends on `main.c`. `main.c` is the single root.

### GOD Module Detection

| Module | Source Lines | % of Total | Role |
|--------|-------------|------------|------|
| **main.c** | 1134 | **100%** | All logic, all state, all peripheral init, all HAL calls |

**Verdict:** `main.c` is a GOD file. This is intentional (single-file project), but it means:
- All coupling is internal — impossible to test subsystems in isolation
- Any change touches the same file
- No separation of concerns at the translation-unit level

### Over-Coupled Modules

Since the entire application is one file, the question of "over-coupling between modules" does not apply in the traditional sense. However, within the file:

| Internal Function Group | External Dependencies | Coupling Level |
|------------------------|----------------------|----------------|
| TM1637 bitbang (180–365) | `HAL_GPIO_WritePin`, `HAL_GPIO_ReadPin` | Minimal — pure bitbang |
| Clock (436–470) | None (pure C) | **Zero** — weakly coupled |
| ADC (474–535) | `HAL_ADC_*` | Tight to ADC HAL, but expected |
| RGB (538–585) | `HAL_TIM_PWM_*` | Tight to TIM HAL, but expected |
| EEPROM (599–615) | `HAL_I2C_Mem_*` | Tight to I2C HAL, but expected |
| Buttons (620–696) | `HAL_GPIO_ReadPin` | Expected for GPIO input |
| FSM (700–1052) | All of the above | **Maximum coupling** — FSM touches every subsystem |

### Summary Table

| Metric | Result |
|--------|--------|
| Circular dependencies | 0 |
| GOD modules | 1 (`main.c`) |
| Translation units (app logic) | 1 |
| Redundant extern declarations | 0 (fixed) |
| HAL layer purity | Clean — CubeMX files are pure leaf nodes |
| Standard lib dependencies | `string.h`, `stdio.h`, `math.h` |

---

## Engineering Verdict (3 lines max)

No circular dependencies. The single-file design collapses all coupling into one translation unit — acceptable for a student project but not testable in isolation. All redundant extern declarations have been removed.
