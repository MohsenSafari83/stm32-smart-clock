# Simulator Reference — STM32 Smart Clock

> A browser-based digital twin of the STM32G030 firmware, peripherals, and signal paths — single HTML file, zero dependencies, ~9900 lines.

---

## Hardware Simulation Engine

| Subsystem | Modeled behavior | Accuracy |
|---|---|---|
| NTC Thermistor | Beta parameter equation: R = R₀ · exp(B · (1/T − 1/T₀)) | Bit-accurate float |
| ADC1 | 12-bit quantization, 79.5 cycle sample time, continuous mode config | Register-level |
| TIM1 | 1 kHz PWM, ARR=999, PSC=7, 3 independent channels | Register-level |
| USART2 | 9600 8N1, blocking TX, configurable interval | Character-accurate |
| I²C2 | 100 kHz standard mode, START/STOP/ACK generation | Frame-level |
| RTC + LSE | async=127, sync=255 → ~1 Hz, BCD register model | Register-level |
| TM1637 | Bit-banged protocol: start condition, address frame, data frames, ACK bit | Bit-level |
| GPIO | Open-drain, push-pull, pull-up/pull-down, alternate function | Pin-level |
| Button debounce | 250 ms gating per button via HAL_GetTick() delta | Cycle-accurate |

---

## Debug Tabs

| Tab | What it shows | Why it matters |
|---|---|---|
| **Architecture Diagram** | Layered system block diagram with live state annotations | See how components connect at a glance |
| **Firmware Debugger** | Module call graph (Sugiyama layout), data ownership table, IRQ table, clock tree | Trace blame for bugs — who reads/writes each global? |
| **Workflow & FSM** | Animated 8-state machine with step-through mode | Watch transitions fire as buttons are pressed |
| **Peripheral Inspector** | Expandable configuration cards for every peripheral | Verify register settings without reading CubeMX output |
| **Signal / Data Flow** | End-to-end signal chains with particle animation | Follow NTC→ADC→Beta→PWM→RGB in real time |
| **Live Display** | Bit-level TM1637 protocol viewer + 7-segment pattern table | Debug display glitches at the GPIO level |
| **Console Logger** | Ring-buffered 1000-line log with timestamped, categorized output | Replace serial terminal for simulator sessions |

---

## Data Ownership

16 global variables mapped across modules:

| Variable | Type | Owner | Readers | Writers |
|---|---|---|---|---|
| current_state | SystemState | fsm | display_mgr | fsm |
| menu_index | int | fsm | display_mgr | fsm |
| buffer_h / buffer_m | uint8_t | fsm | settings | fsm |
| current_adc | uint32_t | temp_sense | ntc, temp_sense | ADC Update |
| temp_c | float | temp_sense | display, uart | temp_sense |
| settings | SystemSettings | settings | display, uart | settings, fsm |
| last_uart_tick | uint32_t | uart_report | uart_report | uart_report |
| tm1637_buf | uint8_t[4] | display_mgr | tm1637 driver | display_mgr |
| uwTick | uint32_t | HAL (SysTick) | all (HAL_GetTick) | SysTick_Handler |
| hadc1 / htim1 / hi2c2 / huart2 / hrtc / hdma_adc1 | HAL handles | hal_* | hal_* | cubemx |

Full global data ownership map is visible in the simulator's Firmware Debugger tab.
