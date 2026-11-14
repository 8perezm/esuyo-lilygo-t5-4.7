# Architecture

```
                    ┌──────────── main loop (app/app.cpp) ───────────┐
 buttons ──queue──▶ │ screen switch │ refresh timer │ idle sleep     │
                    └───────┬────────────────────────────────────────┘
                            │ build / load / on_action
                    ┌───────▼────────┐
                    │    Screen*     │  weather · market ×3 · status
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
  and re-renders, `on_action()` handles the middle button.
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

## Failure behaviour

- Data fetch fails → keep the last snapshot, show `STALE`; weather retries once.
- Wi-Fi fails or is unconfigured → setup AP; the panel still draws placeholders.
- OTA image rejected → HTTP 400, running slot untouched.
- Idle → panel powered down, deep sleep; the image stays on the glass.
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
