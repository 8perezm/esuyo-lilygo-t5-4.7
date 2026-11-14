# Handover — T5 4.7 dashboard firmware

Written 2026-11-14 after the first working build and the panel bring-up
investigation. Read this before touching the display layer.

## TL;DR

- The C++ firmware is complete and builds: LVGL 9 UI, 5 screens, buttons, Wi-Fi
  setup AP, REST API, web panel, OTA, deep sleep.
- **The panel only accepts a frame once per boot with our epdiy integration.**
  LilyGo's own firmware (same board, same glass, same power ICs) updates
  repeatedly and cleanly, so the hardware is fine and the fault is in our
  display path.
- We are mid-fix: tried switching from epdiy's high-level diff
  (`epd_hl_update_screen`) to a direct full-frame push (`epd_draw_base`, the
  vendor-equivalent call). That experiment fails with
  `0x2 EPD_DRAW_LOOKUP_NOT_IMPLEMENTED` even with `MODE_GC16 | MODE_PACKING_2PPB`,
  the 64K LUT and the display's default waveform. It has been reverted; the
  details are kept below so nobody repeats it blind.

## Hardware (measured on this unit, not assumed)

| Item | Value |
|---|---|
| Board | LilyGo T5 4.7" **classic ESP32-WROVER-E** (not the S3) |
| Chip | ESP32-D0WD-V3, rev 3.0, 240 MHz |
| Flash | 16 MB, DIO, 40 MHz |
| PSRAM | 8 MB module; **4 MB mapped** by IDF (`Virtual address not enough…`) |
| Panel | ED047TC1, 960×540, 16 greys, parallel via 74HCT4094 |
| USB | CP210x bridge, **COM12** |
| Buttons | GPIO **39 / 35 / 34** (active low, external pull-ups); GPIO0 is the EPD strobe, RST separate. Idle levels confirmed `1`. |
| Battery ADC | GPIO36 (divider ratio assumed 2.0) |

Board pinout used by epdiy's `epd_board_lilygo_t5_47`:
data `33,32,4,2,19,27,21,22`, CKV 25, STH 26, CKH 5, latch 15,
CFG_DATA 23, CFG_CLK 18, CFG_STR **0**.

## What works

- Build: ESP-IDF 5.3.6, `idf.py build` → ~1.88 MB app, 40 % of the 3 MB slot free.
- Boots, Wi-Fi setup AP `T5-Dashboard` / `t5dashboard`, HTTP panel on
  `192.168.4.1`, panel renders the weather screen (placeholders without data).
- LVGL canvas is provably correct: probe counts showed all 44 widgets with ink
  at the right coordinates (title 4239 dark px, icon 500, temp 594, day labels
  80, footer 935…).
- Weather/quote parsing, settings in NVS, REST endpoints, OTA handler, button
  polling task and EXT1 wake are all implemented and compile.

## What does not work

Panel updates after the first frame. Symptoms seen across builds:

1. Python firmware (before this repo): only the first draw after reboot visible.
2. Our first C++ build: screen 1 visible; pressing next showed screen 2 once;
   later updates no-ops.
3. After adding `epd_clear()` wipes: one frame per boot, then frozen.
4. Latest: direct full-frame push returns `EPD_DRAW_LOOKUP_NOT_IMPLEMENTED`.

### Decisive experiment (already done)

Cloned `https://github.com/Xinyuan-LilyGO/LilyGo-EPD-4-7-OWM-Weather-Display`
(branch `web`) to `C:\SERVER\REPOS\Esuyo\t5-owm-check`, built env **`t5-4_7`**
(the default env is the S3 Plus V2 — wrong board), added a 20-line bring-up block
that draws four grey bars + `PANEL TEST n` three times with LilyGo's driver
(`epd_poweron → epd_clear → edp_update → epd_poweroff_all`), and flashed it.

Result: **`PANEL TEST 3` displayed correctly**, grey levels right, image clean.
⇒ Panel, waveform and power hardware are fine. Our epdiy path is the problem.

The clone still exists with that patch (search for "local bring-up addition").

## Rejected experiment: direct full-frame push

`main/display/display.cpp` was rewritten (now reverted) to allocate one 4 bpp
plane in PSRAM itself (no high-level front/back pair), keep the rails up, and do
`epd_clear()` then
`epd_draw_base(epd_full_screen(), fb, epd_full_screen(), MODE_GC16|MODE_PACKING_2PPB, temp, nullptr, nullptr, epd_get_display()->default_waveform)`.

It logs `draw error 0x2`.

### epdiy 2.1.3 facts that caused this

- `EPD_BUILTIN_WAVEFORM` is `#define ... NULL`; `epd_hl_init(NULL)` substitutes
  the display default, but `epd_draw_base(..., NULL)` returns
  `EPD_DRAW_NO_PHASES_AVAILABLE (0x100)`. Pass `epd_get_display()->default_waveform`.
- `EPD_LUT_1K` has no GC16/i2s lookup on ESP32; the 64K LUT is mandatory.
- Builtin ED047TC1 waveform only has a **20–30 °C** phase range. Clamp the
  temperature or draws outside that range fail with `0x100`.
- `epd_draw_base` needs an explicit packing bit for a 4 bpp buffer
  (`MODE_PACKING_2PPB = 0x80`). Without it: `0x1`/`0x2`.
- Error codes: `0x1` invalid packing, `0x2` lookup not implemented,
  `0x40` invalid crop, `0x80` mode missing, `0x100` no phases, `0x400` line queue.
- `epd_clear()` = `epd_clear_area_cycles(full, 3, 12)`; 3×40 ms is a stronger
  but still fast wipe; 3×300 ms is the deep "repair" used by `/api/display/repair`.

## Next steps (in order)

1. **Try the high-level path with a forced full-area diff.** Restore the
   committed files, then before `epd_hl_update_screen()` do
   `memset(state.back_fb, 0x00, fb_size)` so every pixel differs; the update then
   covers the whole screen. This uses the exact code path that already renders.
   (`back_fb` is a public field of `EpdiyHighlevelState`.)
2. If that works, keep it: set the back buffer to black before each full frame,
   or to the previous frame for partial refreshes.
3. If not, inspect `managed_components/epdiy/src/output_common/lut.c` →
   `find_lut_functions()` to see which (mode, LUT size, render method) pairs
   exist for `MODE_PACKING_2PPB` on I2S; try `EPD_LUT_1K` with 2PPB.
4. Fallback: port LilyGo's `edp_update()` path from the `t5-owm-check` clone
   (`epd_driver.c`) or vendor the 1.x driver as a component.
5. Only after the panel updates reliably: remove `ui::dump/dump_tree/probe`
   calls (already removed from the app loop), re-test buttons and sleep.

## Build / flash / capture

```powershell
cd C:\SERVER\REPOS\Esuyo\empty-14
. .\scripts\env.ps1                 # IDF 5.3.6, Python 3.13 venv
idf.py build
idf.py -p COM12 -b 460800 flash
.\scripts\capture.ps1 COM12 20      # 20 s log capture, resets the board
```

- Toolchain: `C:\Users\miguel\esp\esp-idf` (release/v5.3), tools in
  `C:\Users\miguel\.espressif`, venv `idf5.3_py3.13_env`.
- `capture.ps1` de-asserts DTR/RTS before resetting; pyserial's defaults would
  otherwise hold GPIO0 low and park the chip in download mode.
- The vendored sketch is built with PlatformIO:
  `cd C:\SERVER\REPOS\Esuyo\t5-owm-check; pio run -e t5-4_7 -t upload --upload-port COM12`.
  It writes a 4 MB partition table; reflash ours with `idf.py flash` to restore.

## Memory constraints (hard-won)

- **The setup AP crashes on client connect if internal RAM is tight.** The Wi-Fi
  stack creates a coexistence timer (`hostap_auth_open` →
  `esp_coex_common_timer_setfn_wrapper` → `ets_timer_setfn`) the moment a station
  associates; `esp_timer_create()` returned `ESP_ERR_NO_MEM`, `ESP_ERROR_CHECK`
  called `abort()`, and the board rebooted in a loop — seen by the user as
  "connecting fails and the screen flashes". Fixed by initialising epdiy with
  `EPD_LUT_64K | EPD_FEED_QUEUE_8`: the default 32-line feed queue reserves
  2 × 32 × 960 bytes of internal RAM, the 8-line queue a quarter of that.
  Boot now reports `internal free 48355, largest 38912` after the server starts
  (logged by `report_heap()` in `app.cpp`). Keep an eye on those numbers.
- epdiy's 64K LUT takes 64 KB of internal RAM; Wi-Fi then cannot fit with
  default buffers. Current settings:
  `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`,
  `STATIC_RX_BUFFER_NUM=10`, `DYNAMIC_RX_BUFFER_NUM=16`, `STATIC_TX_BUFFER_NUM=10`,
  httpd stack 5120, `max_open_sockets=3`.
- Start the HTTP server **before** anything that fragments internal RAM.
- mDNS is not available in IDF 5.3 core (component manager only) and was dropped.

## Code map

```
main/
  main.cpp                 app_main -> app::run()
  app/app.cpp              boot order, screen carousel, pending requests, settings JSON
  app/settings.{h,cpp}     versioned blob in NVS, defaults
  app/power.{h,cpp}        battery ADC, deep sleep + EXT1 wake
  display/display.{h,cpp}  epdiy wrapper  <-- the current fault line
  display/ui.{h,cpp}       LVGL L8 canvas, flush -> 4 bpp, helpers, bring-up dump/probe
  input/buttons.{h,cpp}    debounced table, short/long, wake arm, idle-level log
  net/wifi.{h,cpp}         STA + setup AP + captive DNS + NTP
  net/http_server.{h,cpp}  REST, static assets, OTA, gpio scan, repair
  net/http_client.{h,cpp}  HTTPS GET with cert bundle
  data/weather.{h,cpp}     Open-Meteo
  data/quotes.{h,cpp}      Yahoo chart, CoinGecko-id aliasing
  screens/*                Screen interface, weather, market (used 3x), status
  assets/*                 generated Roboto 4 bpp fonts + Lucide A8 icons
  www/*                    embedded control panel (vanilla JS)
scripts/                   env.ps1, flash.ps1, capture.ps1, gen_assets.mjs
docs/                      api.md, architecture.md, buttons.md
```

Rendering path intended: LVGL draws into a 518 KB L8 canvas in PSRAM; the flush
callback converts each byte to a 4 bpp nibble pair (`hi = px>>4`, even pixel in
the high nibble) into the epdiy framebuffer.

## API quick reference

`GET /api/status` · `GET|PUT /api/settings` · `GET /api/screens` ·
`POST /api/display` · `POST /api/refresh` · `GET /api/wifi/scan` ·
`POST /api/wifi` · `GET /api/geocode?q=` · `POST /api/ota` ·
`POST /api/reboot` · `GET /api/gpio/scan` · `GET|POST /api/display/repair`.

## Git state

```
6e1cdc7 Keep the panel powered while awake
92d0077 Stronger clear, darker secondary ink, and button idle logging
4ddb33e Wipe the panel on every screen change and add a deep repair endpoint
22e7bdb Wipe the panel on boot and tighten the weather/market layouts
20af97e C++ dashboard firmware for the T5 4.7: ... (initial)
```
Working tree at time of writing: clean (the direct-draw experiment was reverted).

## Provenance / what was deleted

The workspace was wiped on request at the start of this rebuild:

- the Python agent (`talaria`) is still on GitHub: `8perezm/talaria`, branch
  `2-dev`, commit `65374ba` ("Harden the agent tool loop and add panel text
  rendering"). Local copy and its `lib/`, session data and device config are gone.
- the 16 MB factory flash dump and `docs/hardware-specs.md` from the earlier
  session were deleted. There is no longer a stock-firmware image to restore;
  the closest known-good reference is the vendor repo above.
- the device's saved Wi-Fi credentials went with it, so the board boots into the
  `T5-Dashboard` setup AP until configured again.

## Things not to repeat

- Do not assume "first draw only" is a Python problem; it reproduces in C++.
- Do not trust the framebuffer alone: dump/probe it (the canvas was always right;
  the glass was not).
- Do not use `epd_poweroff()` between operations while awake; the vendor leaves
  power up for the whole update and the board's poweroff is documented as leaky.
- Do not flash blind repeatedly: the vendor test isolated hardware in one flash.
