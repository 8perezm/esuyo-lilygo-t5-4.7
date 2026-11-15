# Architecture

```
                    ┌──────────── main loop (app/app.cpp) ───────────┐
 buttons ──queue──▶ │ screen switch │ refresh timer │ idle sleep     │
                    └───────┬────────────────────────────────────────┘
                            │ build / load / on_action
                    ┌───────▼────────┐
                    │    Screen*     │  weather · markets · quote · joke · status
                    └───────┬────────┘
             widgets into   │            data from
                    ┌───────▼────────┐  ┌──────────────────────────┐
                    │  ui (LVGL L8)  │  │ weather / quotes provider│
                    └───────┬────────┘  └───────────┬──────────────┘
                    flush   │                       │ HTTPS (IPv4)
                    ┌───────▼────────┐          ┌───▼───────────┐
                    │ display (epdiy)│          │ Open-Meteo    │
                    │ 4 bpp, GC16    │          │ Yahoo Finance │
                    └───────┬────────┘          └───────────────┘
                            │
                        ED047TC1 panel
```

## Layers

- **app** — owns the screen list, current index, timing and deep sleep. It is the
  only place that knows about all screens; everything else is a leaf. It also
  serialises settings to JSON and applies `PUT /api/settings`.
- **screens** — each class owns LVGL widgets and one data structure. `build()` is
  called on every switch (widgets are destroyed by `ui::clear()`), `load()` fetches
  and re-renders, `on_action()` handles the middle button (next city, next market
  sub-screen; on Quote/Joke it has nothing to cycle, so the refresh that follows the
  action fetches the next one).
- **display/ui** — `ui` renders into an L8 canvas in PSRAM and converts it to
  epdiy's 4 bpp framebuffer on flush. `display` is a thin epdiy wrapper (init,
  power, clear, wipe, refresh) and is the only code that includes `epdiy.h`.
- **data** — providers translate HTTP JSON into the plain structs in `data/model.h`.
  They never touch LVGL. A failed fetch leaves the previous struct intact and sets
  `stale`.
- **net** — Wi-Fi state machine, setup AP, NTP, HTTPS client, REST server, captive
  portal and the UART console. The server only queues screen/refresh requests; the
  main loop performs them, so a slow panel update never blocks a response.
- **input** — a debounced table of GPIO buttons with short/long events and EXT1
  wake configuration, plus `gpio_probe` for raw pin discovery.

## Screen lifecycle

1. HTTP or a button requests a screen.
2. The main loop calls `display::wipe()` (full clear, both framebuffers to white,
   ~2.5 s).
3. `ui::clear()` destroys the previous widget tree.
4. `screen->build(root)` creates widgets.
5. `screen->load()` fetches data (blocking HTTPS) and writes labels.
6. `ui::present()` flushes the canvas into the epdiy framebuffer and calls
   `epd_hl_update_screen()` (~1.5 s).

Nothing is drawn incrementally: every page turn is a full white frame plus a full
GC16 update, which is what keeps the glass free of leftovers.

## Screen state

The last screen id and the Markets sub-screen are stored in the `ui` NVS
namespace (`app/ui_state.*`); the active weather city lives in `Settings`. On
boot the app restores them, then `show(..., true)` rebuilds that screen and
fetches fresh data. A reboot therefore returns to the same view rather than the
first screen. Changes made with the middle button are saved immediately.

## Hardware

Classic **LilyGo T5 4.7"** (ESP32-WROVER-E, not the S3): ESP32-D0WD-V3 rev 3.0 at
240 MHz, 16 MB flash (DIO, 40 MHz), 8 MB PSRAM module of which IDF maps 4 MB.
Panel ED047TC1, 960 x 540, 16 greys, driven parallel through a 74HCT4094.
Serial over a CP210x USB bridge. Panel pins (epdiy epd_board_lilygo_t5_47):
data 33,32,4,2,19,27,22, CKV 25, STH 26, CKH 5, latch 15, CFG_DATA 23,
CFG_CLK 18, CFG_STR 0.

## Boot flow

1. NVS init, settings load (versioned blob with migration).
2. display::init() (epdiy, high-level state, framebuffer in PSRAM), LVGL canvas, buttons, power, pin probe, serial console.
3. wifi_start() — up to three STA associations, else the setup AP.
4. NTP start; up to 8 s wait so HTTPS certificates validate.
5. HTTP server starts, heap reported.
6. Current screen is built, data fetched, one frame drawn.
7. Main loop: pending web requests, button events, idle sleep, refresh timer.

On a timer wake with rotate_on_wake set, the carousel advances one enabled screen before that first draw, so successive wakes cycle through the screens.

## Data providers

| Provider | Endpoint | Notes |
|---|---|---|
| Weather | `api.open-meteo.com/v1/forecast` | current + 5 daily; `time` must not be in the `daily` list; retries once |
| Geocoding | `geocoding-api.open-meteo.com/v1/search` | used by the city search box |
| Quotes | `query1.finance.yahoo.com/v8/finance/chart/<symbol>` | requires a browser User-Agent; one request per symbol, 120 ms apart |

Both are keyless. Crypto ids are CoinGecko slugs (`bitcoin`) mapped to Yahoo
tickers (`BTC-USD`); indices and commodities are Yahoo symbols.

## Memory budget

| Buffer | Size | Where |
|---|---|---|
| LVGL L8 canvas | 960 × 540 = 518 KB | PSRAM |
| epdiy 4 bpp planes (front/back) | 2 × 259 KB | PSRAM |
| epdiy 64K LUT | 64 KB | internal |
| epdiy feed queues (8 lines) | 2 × 8 × 960 B | internal |
| LVGL draw buffers + widgets | tens of KB | internal |
| mbedTLS heap | dynamic | PSRAM (external alloc mode) |

Internal RAM is the scarce resource. After boot the firmware reports about
**45–48 KB free** with a ~40 KB largest block. Anything that adds internal
allocations (Wi-Fi buffers, TLS from internal memory, a 32-line epdiy queue) can
make a client association abort the chip.

## Hard-won constraints

- Wi-Fi runs with CONFIG_ESP_WIFI_IRAM_OPT=n, RX_IRAM_OPT=n, static RX 10,
  dynamic RX 16, static TX 10, httpd stack 5120, three sockets.
- Wi-Fi storage is RAM only; the NVS Settings blob is the single source of truth. Association is retried three times before falling back to the setup AP.
- HTTPS is pinned to IPv4 (the LAN advertises IPv6 without a global address) and waits for SNTP so certificates validate. Never fetch HTTPS before the clock is set, and never let mbedTLS allocate from internal RAM (PSRAM mode is set in sdkconfig.defaults).
- City geocoding is the only HTTPS route driven by the web panel. Its TLS call runs on a lazy 8 KB worker task because the httpd task stack cannot host mbedTLS.
- Do not power-cycle the panel between updates while awake; the rails stay up until sleep. Temperature is clamped to the waveform's 20–30 C range by epdiy.
- Panel faults are usually integration, not hardware: a vendor A/B sketch drew repeatedly and cleanly on the same glass.
- Do not trust one xEventGroupWaitBits result for association; do not draw outside 20–30 C; do not use the 1K LUT (no GC16 lookup); EPD_BUILTIN_WAVEFORM is NULL; never call epd_draw_base on a 4 bpp buffer without MODE_PACKING_2PPB; never rely on HTTP polling alone to catch button presses (the device-side /api/gpio/events ring buffer exists for that).

## Failure behaviour

- Data fetch fails → keep the last snapshot, show `STALE`; weather retries once.
- Wi-Fi fails or is unconfigured → setup AP; the panel still draws placeholders.
- OTA image rejected → HTTP 400, running slot untouched.
- Idle → panel powered down, deep sleep; the image stays on the glass. An open
  control panel defers this: its `GET /api/status` poll (every 5 s) counts as
  activity, so the board sleeps `sleep_after_s` after the last poll.
- Asleep → the timer wakes the board every `sleep_refresh_min` (default 30 min):
  it boots, fetches fresh data, redraws once — the current screen, or the next
  enabled one when wake rotation is on — and sleeps again. A user button wakes
  it immediately.
- Screen request during a refresh → queued until the panel is free.

## Adding a screen

```cpp
class ClockScreen : public Screen {
  const char* id() const override { return "clock"; }
  const char* title() const override { return "Clock"; }
  void build(lv_obj_t* root) override;   // create labels
  bool load() override;                  // fill them
};
```

Register it in the `g_screens` array in `app/app.cpp`. Navigation, wake behaviour,
the web UI screen list and `/api/display` pick it up automatically. Keep the
minimum type size at 28 px and use `screens::kGrey`/`kFaint`; anything lighter
disappears on this panel.

Long prose (the Quote and Joke screens) uses `screens::body_text()` +
`fit_body()`, which tries 48, 36 and 28 px bold and settles on the largest size
that fits the space above the footer. Header titles that are data rather than
boilerplate — the weather screen's city name — go through `fit_title()` so a
long geocoded name shrinks instead of running under the clock.

## Panel fonts

`main/assets/fonts.c` is generated by `scripts/gen_assets.mjs`; the glyph set is
`0x20-0x7F` plus `£ ° · € – — … ↑ ↓`. Anything else would draw as an empty box,
so `data::clean_text()` folds accented Latin to plain ASCII (`Zürich` →
`Zurich`), maps curly quotes and dashes to characters the fonts carry, and drops
what is left (emoji, CJK). Any text from an API or from the web panel that goes
on the panel should pass through it.

To make the new screen switchable in the web panel, add a flag to `Settings`, map
its id in `settings::toggle_for()`, default it on in `settings::defaults()`, bump
`kVersion` and add a migration branch in `settings::init()`, then list it in the
`screens` object of `app::settings_to_json()` / `apply_settings_json()`. The
carousel skips disabled screens through `next_enabled()`.

## External text APIs

The Quote screen uses `https://zenquotes.io/api/random` (keyless; attribution
"ZENQUOTES.IO" is shown in the header). The Joke screen uses
`https://v2.jokeapi.dev/joke/Any?type=single&blacklistFlags=nsfw,racist,explicit`.
Both are fetched through `net::http_get()` on the main task, like weather and
markets, and both must be checked in the body: ZenQuotes answers HTTP 200 with a
"too many requests" quote, and JokeAPI returns `"error": true` inside a 200.
`data::clean_text()` then drops code points the LVGL fonts cannot draw.
