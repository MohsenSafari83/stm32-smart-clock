# Finite State Machine Reference

> 8-state, 12-transition menu system driven by 3 buttons (UP/DOWN/SELECT).

---

## State Transition Table

| State | ID | Trigger | Behavior |
|---|---|---|---|
| NORMAL | 0 | — | Clock display, all systems active |
| MAIN_MENU | 1 | SW3 (1.5s hold) | 5-item menu: SET, DISP, COLOR, INT, UART |
| SET_TIME_H | 2 | SEL @ 0 | Hours adjustment (0–23) → SW3 advances to SET_TIME_M |
| SET_TIME_M | 3 | SEL(3) from SET_TIME_H | Minutes adjustment (0–59) → SW3 saves & returns |
| TOGGLE_DISPLAY | 4 | SEL @ 1 | Display on/off → SW3 saves & returns |
| SET_COLOR | 5 | SEL @ 2 | LED color selection (0=R, 1=G, 2=B) → SW3 saves & returns |
| UART_INTERVAL | 6 | SEL @ 3 | Report interval (1–99 s) → SW3 saves & returns |
| UART_TOGGLE | 7 | SEL @ 4 | UART on/off → SW3 saves & returns |

All sub-states write to EEPROM on save. The FSM is polled from the super-loop — no interrupt-driven state machine.

---

## Transition Diagram

12 transitions connect the 8 states:

- NORMAL ↔ MAIN_MENU (bidirectional)
- MAIN_MENU → SET_TIME_H, SET_TIME_M, TOGGLE_DISPLAY, SET_COLOR, UART_INTERVAL, UART_TOGGLE
- Each sub-state → MAIN_MENU (save and return)

---

## Button Mapping

| Button | Pin  | Action in NORMAL | Action in sub-states |
|--------|------|-----------------|---------------------|
| SW1 (UP)   | PB13 | — | Increment value / cycle forward |
| SW2 (DOWN) | PB14 | — | Decrement value / cycle backward |
| SW3 (SEL)  | PB15 | Hold 1.5s → MAIN_MENU | Confirm selection / save / return |

Debounce: 250 ms gating per button via HAL_GetTick() delta.
