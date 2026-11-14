# Architecture

```
                    ┌──────────── main loop (app/app.cpp) ───────────┐
 buttons ──queue──▶ │  screen switch │ refresh timer │ idle sleep    │
                    └───────┬────────────────────────────────────────┘
                            │ build / load / on_action
                    ┌───────▼────────┐
                    │    Screen*     │  weather · market ×3 · status
                    └───────┬────────┘
             widgets into   │            data from
                    ┌───────▼────────┐  ┌──────────────────────────┐
                    │  ui (LVGL L8)  │  │ weather / quotes provider│
                    └───────┬────────┘  └───────────┬──────────────┘
                    flush   │                       │ HTTPS
                    ┌───────▼────────┐          ┌───▼───────────┐
                    │ display (epdiy)│          │ Open-Meteo    │
                    │  4 bpp, GC16   │          │ Yahoo Finance │
                    └───────┬────────┘          └───────────────┘
                            │
                        ED047TC1 panel
```

## Layers

- **app** — owns the screen list, the current index, timing and deep sleep. It is the
  only place that knows about all screens; everything else is a leaf.
- **screens** — each class owns LVGL widgets and one data structure. `build()` is
  called on every switch (widgets are destroyed by `ui::clear()`), `load()` fetches
  and re-renders, `on_action()` handles the middle button.
- **display/ui** — `ui` renders into an L8 canvas in PSRAM and converts it to epdiy's
  4 bpp framebuffer on flush. `display` is a thin epdiy wrapper (init, power, clear,
  update) and is the only code that includes `epdiy.h`.
- **data** — providers translate HTTP JSON into the plain structs in `data/model.h`.
  They never touch LVGL. A failed fetch leaves the previous struct intact and sets
  `stale`.
- **net** — Wi-Fi state machine, setup AP, NTP, the HTTPS client and the REST server.
  The server only queues screen/refresh requests; the main loop performs them, so a
  slow panel update never blocks an HTTP response.
- **input** — a debounced table of GPIO buttons with short/long events and EXT1
  wake configuration.

## Rendering budget

| Buffer | Size | Where |
|---|---|---|
| LVGL L8 canvas | 960×540 = 518 KB | PSRAM |
| epdiy 4 bpp planes (2) | 2 × 259 KB | PSRAM |
| epdiy line buffers | tens of KB | internal RAM |
| LVGL + widgets | ~10 KB | internal RAM |

A full GC16 refresh takes roughly 2.5 s on this panel. That is why screens fetch
first and then draw once, and why the web UI queues changes instead of drawing
inside an HTTP handler.

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
the web UI screen list and `/api/display` pick it up automatically.

## Failure behaviour

- Data fetch fails → keep the last snapshot, show `STALE` in the footer.
- Wi-Fi fails or is unconfigured → setup AP, panel still draws with placeholders.
- OTA image rejected → HTTP 400, running slot untouched.
- Idle → panel powered down, deep sleep; the image stays on the glass.
