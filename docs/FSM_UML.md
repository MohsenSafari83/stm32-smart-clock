# FSM — UML State Diagram

```mermaid
stateDiagram-v2
    [*] --> ST_NORMAL : Reset / init complete

    state ST_NORMAL {
        [*] --> Running
        Running --> [*] : entry / show time or boot counter
    }

    ST_NORMAL --> ST_MENU : SW1 short pressed

    state ST_MENU {
        [*] --> Idle
        Idle --> Idle : SW1 short → advance to next item\n(past item 6 → exit to ST_NORMAL)
        Idle --> Idle : SW2/SW3 on item 3/4\n(toggle Display / UART on‑off)
    }

    ST_MENU --> ST_NORMAL : SW1 long pressed
    ST_MENU --> ST_NORMAL : SW1 short past last menu item
    ST_MENU --> ST_EDIT : SW2/SW3 on numeric item\n(0=Hour,1=Min,2=Sec,5=UART int,6=RGB ch)

    state ST_EDIT {
        [*] --> Adjust
        Adjust --> Adjust : SW2 (inc) / SW3 (dec)\nvalues clamped to valid range
        Adjust --> [*] : SW1 short (confirm)
    }

    ST_EDIT --> ST_MENU : SW1 short (confirm change)
    ST_EDIT --> ST_NORMAL : SW1 long pressed
```

## FSM Rules Summary

1. **ST_NORMAL** — Default state. Displays time (or boot counter for 2 s after reset). SW1 short enters the menu.
2. **ST_MENU** — Seven items (Hour → Min → Sec → Display toggle → UART toggle → UART interval → RGB channel). SW1 advances; past item 6 returns to normal. SW2/SW3 toggle items 3–4 or enter ST_EDIT for numeric items. SW1 long returns to normal.
3. **ST_EDIT** — SW2 increments, SW3 decrements the selected value. SW1 short confirms and returns to menu. SW1 long aborts to normal.
