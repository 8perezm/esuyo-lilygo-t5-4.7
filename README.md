# T5 Dashboard

A C++ dashboard firmware for the **LilyGo T5 4.7" e-paper** board (ESP32-WROVER-E,
960×540 ED047TC1, 16 greys). It replaces the MicroPython agent on this device with a
purpose-built UI: several dashboard screens, a 4 bpp greyscale renderer, physical
button navigation, Wi-Fi setup without a cable, and a web control panel.

## Screens

| Screen | What | Keys |
|---|---|---|
| Weather | Today emphasised, plus 4 following days; touch the middle button to cycle the configured cities | `city >` |
| Crypto | Top coins, last price, 24 h change, 5-day sparkline | |
| Indices | S&P 500, Nasdaq, Dow, FTSE 100, DAX (configurable) | |
| Commodities | Gold, silver, natural gas, crude (configurable) | |
| Status | IP, RSSI, battery, heap, uptime, firmware, MAC | |

Buttons, left to right: **previous screen**, **context action** (city on the weather
screen), **next screen**. Hold the last button for 1.2 s to sleep; hold any of the
first two to refresh. Any button wakes the board. Deep sleep leaves the panel image
in place.

## Build

ESP-IDF 5.3+ and the component manager are required (LVGL 9 and epdiy are fetched
from the registry on the first build).

```powershell
. .\scripts\env.ps1          # ESP-IDF 5.3 on this machine
idf.py set-target esp32
idf.py build
idf.py -p COM12 -b 460800 flash monitor
```

Regenerate fonts and icons after changing the asset list:

```powershell
npm install
node scripts/gen_assets.mjs
```

## First run

With no saved network the board starts an access point:

```
SSID:     T5-Dashboard
Password: t5dashboard
Panel:    http://192.168.4.1/
```

Pick your network there, save, and the board restarts on it. Afterwards the panel is
served on the LAN at `http://t5-dashboard.local/` (or the address shown on the Status
screen). If the credentials stop working, the board falls back to the setup AP
automatically.

Everything the UI exposes is also a small REST API; see `docs/api.md`. Firmware can
be uploaded from the panel (Firmware → choose `.bin`), so the cable is only needed
for the first flash.

## How it is put together

```
main/
  app/        app loop, screen carousel, settings (NVS), power/deep sleep
  display/    epdiy wrapper + LVGL L8 canvas and flush
  input/      button table, debounce, EXT1 wake
  net/        Wi-Fi + setup AP + captive DNS, REST server, HTTPS client, NTP
  data/       Open-Meteo and Yahoo providers, plain data model
  screens/    Screen interface, weather, market, status
  assets/     generated Roboto (4 bpp) and Lucide (A4) data
  www/        embedded control panel
scripts/      toolchain env + asset generation
```

Rendering path: LVGL draws into a 960×540 **L8** buffer in PSRAM, the flush callback
converts luminance to epdiy's **4 bpp** framebuffer (even pixel in the high nibble),
and `epd_hl_update_screen()` pushes it with the GC16 waveform. That is where the 16
greys come from — the panel is not limited to 1 bit, only the old Python wrapper was.

Adding a screen: implement `Screen` (id, title, build, load, optional on_action),
add it to the array in `app/app.cpp`, done. Data fetching and panel refresh stay in
the app, so a screen only owns widgets and a parser.

## Data sources

- **Weather** — Open-Meteo (`api.open-meteo.com`), keyless, 5 daily values.
- **Markets** — Yahoo Finance chart endpoint, keyless, one request per symbol.
  Change is taken from `regularMarketChangePercent`; when that is absent it is
  computed from the last two closes (never from `chartPreviousClose`, which is the
  close before the range starts).

Both are unofficial/free endpoints: quotes are delayed, and either can rate-limit.
The last good snapshot stays on screen with a STALE badge rather than blanking.

## Power

The e-paper image survives with the panel unpowered. After `sleep_after_s` idle the
board powers the panel down and enters deep sleep with an EXT1 wake on the three
buttons and a timer set to `max(refresh_min, 30)` minutes. A timer wake refreshes
the current screen once and goes back to sleep; a button wake starts an interactive
session.

## Notes

- The board uses GPIO0 for the display shift-register strobe, so the BOOT button
  cannot be a UI button while the panel is active.
- epdiy's 1K LUT has no GC16/i2s lookup table on the ESP32, so the 64K LUT is
  required; it costs 64 KB of internal RAM, which is why the Wi-Fi buffer counts
  are reduced (`CONFIG_ESP_WIFI_*_BUFFER_NUM`) and `CONFIG_ESP_WIFI_IRAM_OPT` is
  off. Wi-Fi init is still allowed to fail without aborting the boot.
- GPIO34/35/39 are input-only RTC pins: they have external pull-ups and can wake
  the chip. If a board revision exposes more buttons, add them to the table in
  `input/buttons.cpp` (and to `arm_wake`).
- `partitions.csv` gives each OTA slot 3 MB and leaves ~9.9 MB of SPIFFS for future
  assets or caching.
