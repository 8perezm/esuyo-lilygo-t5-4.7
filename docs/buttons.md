# Buttons

The stock T5 4.7 exposes **five switches**: reset, BOOT/IO0, and three user
buttons. Only three are usable as UI inputs.

## Physical map (left to right)

Measured on this unit on 2026-11-14 by pressing each switch and reading
`/api/gpio/events`:

| # | Switch | GPIO | Runtime behaviour |
|---|---|---|---|
| 0 | RESET | EN | Hardware reboot; not programmable |
| 1 | BOOT | 0 | Nothing while running — see below |
| 2 | user | 35 | Weather: next city. Markets: next sub-screen (Crypto/Indices/Commodities). Quote/Joke: fetch another. Other screens: refresh current data |
| 3 | user | 34 | Next screen; **hold 1.2 s** to enter deep sleep |
| 4 | user | 39 | Previous screen; hold to refresh |

Short press events are queued by `input/buttons.cpp`; the main loop consumes them
one at a time so a screen update never splits a press. Any of the three user
buttons also wakes the board from deep sleep (EXT1).

The direction is not ideal: the rightmost button (39) steps back, the one before it
(34) steps forward. Swapping the roles is a one-line change in `buttons.cpp` if it
should be the other way round.

## Why BOOT cannot be a button

epdiy's T5 4.7 board definition assigns **GPIO0 to `CFG_STR`**, the panel's
shift-register strobe:

```c
#define CFG_STR GPIO_NUM_0
```

The pin idles low and is pulsed high eight times per panel update to load the
74HCT4094 control register. Reading it as a button means floating or driving the
strobe line, which clocks arbitrary bits into that register and can leave the panel
misconfigured. BOOT is therefore reserved for flashing.

A safe future option is to add GPIO0 to the **EXT1 wake mask**, so BOOT wakes the
board from deep sleep (the panel is unpowered then). This is not implemented.

## Discovery mode

`input/gpio_probe` samples every pin that the panel, flash, PSRAM and USB do not
use: 34, 35, 36, 39 (input-only, with ADC), 37, 38 (input-only), and 12, 13, 14,
24, 28, 29, 30, 31 (internal pull-ups). It debounces for 100 ms, keeps a press
counter per pin, and records the last 64 level changes on the device.

With the board on the network:

| Request | Result |
|---|---|
| `GET /api/gpio/live` | current level, ADC millivolts, press count, age |
| `GET /api/gpio/events` | ring buffer of changes, newest first |
| `POST /api/gpio/clear` | clear the buffer before a test |
| `GET /api/gpio/scan` | older 5 s blocking scan of 12/13/14/34/35/36/39 |

The web panel has a **Button test** card that polls `/api/gpio/live` four times a
second and highlights the pressed row, which is the easiest way to identify a pin
by eye.

### Procedure that found the map

1. Clear the event buffer.
2. Press the switches one at a time, ~2 s apart.
3. Read `/api/gpio/events`.

The first run produced presses on 35, 34 and 39 only. The other two switches are
RESET and BOOT; RESET restarts the board (and clears the buffer), and BOOT has no
readable signal.

## Adding a button

If a revision or a mod exposes another GPIO, add an entry to `g_state` in
`input/buttons.cpp` with a role, and to the probe list in `input/gpio_probe.cpp`
while discovering it. EXT1 wake is built from the table, so a new RTC-capable pin
is included automatically. Do not pick GPIO0, 2, 4, 5, 15, 16, 17, 18, 19, 21, 22,
23, 25, 26, 27, 32 or 33: the panel, flash or PSRAM own them.
