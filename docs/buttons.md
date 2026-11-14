# Buttons

The stock T5 4.7 exposes five switches: reset, a BOOT/IO0 switch, and three user
buttons. Only three are usable as UI inputs:

| Role | GPIO | Notes |
|---|---|---|
| Left | 39 | input-only, external pull-up, RTC (can wake) |
| Middle | 35 | input-only, external pull-up, RTC (can wake) |
| Right | 34 | input-only, external pull-up, RTC (can wake) |
| BOOT | 0 | **not usable** — epdiy drives it as the panel's shift-register strobe (`CFG_STR`) |
| Reset | — | hardware reset |

`input/buttons.cpp` holds the table (`g_state`). Wake-up uses EXT1 across every
entry, so adding a button automatically extends wake support as long as the pin is
RTC-capable (GPIO0–39 with caveats; 34–39 are input-only and ideal).

## Finding extra buttons

If a revision (or a mod) exposes more than three, do not guess the pins. With the
board on the network:

```bash
curl http://192.168.4.1/api/gpio/scan   # or the panel's LAN address
```

The request blocks for five seconds while sampling GPIO 12, 13, 14, 34, 35, 36 and
39. Press each physical button once during that window; the response marks the pins
that changed:

```json
{"pins":[{"gpio":12,"changed":false,"level":1},
         {"gpio":34,"changed":true,"level":1},
         ...]}
```

A pin that toggles is a button. Add it to `g_state` with a role, rebuild, and it is
part of navigation and wake automatically.

Candidates are limited to pins the display does not use: the panel owns GPIO 0, 2,
4, 5, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33 and the latch on 15, while GPIO 16/17
belong to the PSRAM. GPIO 36 is the battery divider (input-only, safe to sample).
