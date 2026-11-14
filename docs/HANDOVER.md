# Handover — T5 Dashboard

Last updated 2026-11-14, at the end of the first working day.

## TL;DR

- The firmware works end to end: the panel renders and **updates on every screen
  change**, joins Wi-Fi, fetches live weather and market data, and serves the web
  panel at the board's LAN address.
- The panel is **healthy**. A vendor-firmware A/B test (LilyGo's own sketch on the
  same glass) drew repeatedly and cleanly; every earlier display fault was in our
  integration, not the hardware.
- There are **three programmable buttons**, not five: two of the five switches are
  RESET and BOOT. BOOT is also the panel's shift-register strobe and cannot be used
  as a normal input.
- The board is currently on Wi-Fi `FOO`, address `192.168.5.29`, with five cities
  and five coins configured.

## Measured hardware

| Item | Value |
|---|---|
| Board | LilyGo T5 4.7", classic **ESP32-WROVER-E** (not the S3) |
| Chip | ESP32-D0WD-V3, rev 3.0, 240 MHz |
| Flash | 16 MB, DIO, 40 MHz |
| PSRAM | 8 MB module; **4 MB mapped** by IDF |
| Panel | ED047TC1, 960 × 540, 16 greys, parallel via 74HCT4094 |
| USB | CP210x bridge, **COM12** |
| Battery | JST-PH 2.0 mm 2-pin, HX6610S charger (500 mA, 4.2 V, no protection FETs) |
| Battery ADC | GPIO36 through a 2:1 divider (`board::kBatteryDivider`) |
| Switches | 5 physical: RESET, BOOT (GPIO0), GPIO35, GPIO34, GPIO39 |

Panel pins (epdiy `epd_board_lilygo_t5_47`): data `33,32,4,2,19,27,21,22`,
CKV 25, STH 26, CKH 5, latch 15, CFG_DATA 23, CFG_CLK 18, **CFG_STR 0**.

## What works

- 4 bpp greyscale rendering through an LVGL 9 L8 canvas, Roboto (Medium/Bold) and
  Lucide icons; everything except the 48 px title and 96 px temperature is at least
  28 px.
- Five screens, carousel navigation, weather city cycling, long-press refresh,
  long-press sleep.
- Wi-Fi: STA with three association attempts, setup AP `T5-Dashboard`
  (`t5dashboard`), captive-portal redirects, NTP.
- Web panel: status, screens, Wi-Fi scan/join, cities with geocoding search,
  crypto ids, index/commodity symbols, refresh and sleep timers, OTA, reboot.
- Data: Open-Meteo (keyless) and Yahoo Finance (keyless, needs a browser
  User-Agent). HTTP client pins IPv4 and verifies certificates against the IDF
  bundle.
- Deep sleep with EXT1 wake on the three user buttons; the image stays on the glass.
- Diagnostics: `/api/gpio/live`, `/api/gpio/events`, `/api/gpio/clear`,
  `/api/gpio/scan`, `/api/display/repair`, per-request HTTP log, boot heap report,
  and a UART console.

## Known limits

- **BOOT cannot navigate.** epdiy drives GPIO0 as `CFG_STR`; it idles low and is
  pulsed eight times per panel update. Reading it as a button would clock garbage
  into the display control register. It can only be used as a deep-sleep wake
  source (not implemented yet).
- **RESET is hardware only.**
- The physical order is reset, boot, 35, 34, 39 (left to right). Consequently the
  rightmost button steps **back** and the one before it advances. Swapping 34/39 is
  a one-line change if that direction feels wrong.
- The clock shows `--:--` if SNTP has not answered within the 8 s boot wait; data
  still loads, and the clock corrects itself on the next refresh.
- The battery path has not been load-tested. `4.36 V` on USB is the charger rail,
  not a cell; expect 3.7–4.2 V with a battery and calibrate the divider against a
  multimeter if it reads more than ~100 mV off.
- Market rows lost the ticker line when the body type went to 28 px; the readable
  name remains. Daily rain probability is shown as `96%` rather than `96% RAIN`,
  for the same reason.

## Boot flow

1. NVS init, settings load (versioned blob).
2. `display::init()` — epdiy with `EPD_LUT_64K | EPD_FEED_QUEUE_8`, high-level
   state, framebuffer in PSRAM.
3. `ui::begin()` — LVGL canvas; `buttons::begin()`, `power::init()`,
   `probe::start()`, `serial_cli_start()`.
4. `wifi_start()` — up to three associations, then setup AP on failure.
5. NTP start and up to 8 s wait (TLS needs a plausible clock).
6. HTTP server starts, heap reported.
7. Current screen is built, data is fetched, one frame is drawn.
8. Loop: pending web requests, button events, idle sleep, refresh timer.

## Display path

`display::refresh()` uses epdiy's high-level diff (`epd_hl_update_screen`,
`MODE_GC16`). A screen change first calls `display::wipe()`:
`epd_clear_area_cycles(full, 3, 40 ms)` plus resetting both the front and back
framebuffers to white, so the diff covers the whole panel. Measured: wipe ≈ 2.5 s,
draw ≈ 1.5 s. `/api/display/repair` runs a deeper 3 × 300 ms clear (~20 s).

Do not power-cycle the panel between updates while awake; the rails stay up until
sleep. Temperature is clamped to the waveform's 20–30 °C range by epdiy.

## Memory constraints (hard-won)

- epdiy's 64K waveform LUT costs 64 KB of internal RAM; the 1K LUT has no GC16
  lookup on ESP32.
- The default 32-line render queue reserves `2 × 32 × 960` bytes internally. Using
  `EPD_FEED_QUEUE_8` saves ~46 KB. Without it the Wi-Fi stack cannot allocate its
  coexistence timer when a station associates, and the board aborts with
  `ESP_ERR_NO_MEM` — seen as a reboot loop with the panel flashing.
- TLS needs more internal RAM than epdiy leaves; mbedTLS is configured for
  **external (PSRAM) allocation** in `sdkconfig.defaults`.
- Wi-Fi runs with `CONFIG_ESP_WIFI_IRAM_OPT=n`, `RX_IRAM_OPT=n`, static RX 10,
  dynamic RX 16, static TX 10, httpd stack 5120, three sockets.
- City geocoding is the only HTTPS route driven by the web panel. Its TLS call
  runs on a lazy 8 KB `geocode` task, created on the first search, because the
  httpd task stack cannot host mbedTLS. That is ~8 KB of internal RAM while the
  worker exists (it is never deleted; a reboot frees it).
- Boot currently reports roughly **45–48 KB internal free, ~40 KB largest block**
  after the server starts (`app: heap after server`). Watch those numbers.

## Network and data

- Wi-Fi storage is **RAM only**; our NVS blob is the single source of truth. The
  driver otherwise auto-connects from its own NVS copy at `esp_wifi_start()` and
  raises a transient disconnect, which previously made a healthy boot fall back to
  the setup AP after ~30 ms. `wifi_connect()` now retries three times.
- HTTP is pinned to IPv4 (`HTTP_ADDR_TYPE_INET`) because the LAN advertises IPv6
  without a global address, and HTTPS waits for SNTP so certificates validate.
- The weather URL must not include `time` in the `daily` list: Open-Meteo answers
  400. Weather retries once after 1.5 s so a transient TLS failure does not paint
  `NO DATA` over a good screen.
- Yahoo needs a browser-like `User-Agent` (the device sends a Chrome-style string);
  without it the API returns 429.

## Buttons

Reported by the owner and consistent with the event log:

| Position (left → right) | Switch | GPIO | Action |
|---|---|---|---|
| 0 | RESET | EN | Hardware reboot |
| 1 | BOOT | 0 | None at runtime; shared with `CFG_STR` |
| 2 | user | 35 | City on Weather, refresh elsewhere |
| 3 | user | 34 | Next screen; hold 1.2 s to sleep |
| 4 | user | 39 | Previous screen; hold to refresh |

Discovery uses `input/gpio_probe`, which samples every safe pin continuously,
keeps per-pin press counts and a 64-entry ring buffer of debounced level changes.
`/api/gpio/events` returns the buffer newest-first, so a slow HTTP client cannot
miss a press. The web panel has a live Button test table.

## Serial console (UART0, 115200)

```
wifi <ssid> <password>   save credentials and reboot
status                   stored SSID, link state, clock, heap
reboot
help
```

Useful when the browser route is unavailable.

## Build, flash, capture

```powershell
cd C:\SERVER\REPOS\Esuyo\empty-14
. .\scripts\env.ps1                 # ESP-IDF 5.3.6, Python 3.13 venv
idf.py build
idf.py -p COM12 -b 460800 flash
.\scripts\capture.ps1 COM12 20      # 20 s log capture, resets the board
```

- Toolchain: `C:\Users\miguel\esp\esp-idf` (release/v5.3), tools in
  `C:\Users\miguel\.espressif`, venv `idf5.3_py3.13_env`.
- `capture.ps1` de-asserts DTR/RTS before resetting; pyserial's defaults would
  otherwise hold GPIO0 low and park the chip in download mode.
- New source files need `idf.py reconfigure` because `main/CMakeLists.txt` uses a
  configure-time glob.
- Vendor reference firmware (proven on this board):
  `C:\SERVER\REPOS\Esuyo\t5-owm-check`, build with `pio run -e t5-4_7`
  (not the default S3 environment). It writes a 4 MB partition table; restore ours
  with `idf.py flash`.

## Diagnostics quick reference

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | firmware, internal heap, PSRAM, uptime, RSSI, IP, screen, battery |
| `GET /api/gpio/live` | level, ADC mV, press count, age for each probed pin |
| `GET /api/gpio/events` | ring buffer of level changes, newest first |
| `POST /api/gpio/clear` | clear the ring buffer |
| `GET /api/gpio/scan` | 5 s blocking scan of the candidate pins |
| `POST /api/display/repair` | deep panel clear, ~20 s |

## Next steps

1. Decide whether BOOT should be added as a deep-sleep **wake-only** source, and
   whether 34/39 should be swapped so the rightmost button advances.
2. Fit a battery, measure the cell, and calibrate `kBatteryDivider` if needed; add a
   percentage and low-battery warning to Status and the web panel.
3. Update the clock label when SNTP completes after the 8 s boot window.
4. Consider caching the last good weather snapshot in NVS so a cold boot without
   network can still show data.
5. Optional: partial refresh for page turns to cut the 2.5 s wipe.

## Provenance

- The workspace was wiped on request at the start of this rebuild. The earlier
  Python agent is still on GitHub: `8perezm/talaria`, branch `2-dev`, commit
  `65374ba`. The 16 MB factory flash dump and the old hardware-spec document were
  deleted in that wipe.
- The panel A/B test lives in `C:\SERVER\REPOS\Esuyo\t5-owm-check` (vendor `web`
  branch with a three-pass `PANEL TEST` block in `setup()`).
- Current branch state is in `git log`; documentation was last committed with the
  release of this file.

## Things not to repeat

- Do not conclude the panel is broken from a stale image; check the vendor test
  first.
- Do not use the 1K LUT (no GC16 lookup) or pass `EPD_BUILTIN_WAVEFORM` (it is
  `NULL`) to `epd_draw_base`.
- Do not draw outside 20–30 °C without clamping the temperature.
- Do not call `epd_draw_base` with a 4 bpp buffer without `MODE_PACKING_2PPB`.
- Do not trust a single `xEventGroupWaitBits` result for Wi-Fi association.
- Do not fetch HTTPS before the clock is set, and do not let mbedTLS allocate from
  internal RAM on this board.
- Do not rely on HTTP polling alone to capture button presses; the device-side
  event log exists for that reason.
