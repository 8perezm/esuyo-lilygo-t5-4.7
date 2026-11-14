# NOTES

## Current blocker

Panel updates only land once per boot with our epdiy display layer.

Evidence: LilyGo's own firmware on the same board updates repeatedly and draws
clean greys (`PANEL TEST 3` observed). The panel hardware is therefore fine; the
fault is in our display path (`main/display/display.cpp`).

The uncommitted direct-draw experiment currently returns
`EPD_DRAW_LOOKUP_NOT_IMPLEMENTED (0x2)` from `epd_draw_base(..., MODE_GC16 |
MODE_PACKING_2PPB, ...)`.

Immediate next attempt: restore the committed high-level path and force a
full-area diff by zeroing `state.back_fb` before `epd_hl_update_screen()`.

Full context, measurements, commands and code map: **`docs/HANDOVER.md`**.

## Build

```powershell
. .\scripts\env.ps1
idf.py build
idf.py -p COM12 -b 460800 flash
.\scripts\capture.ps1 COM12 20
```

Vendor reference firmware (proven good on this board):
`C:\SERVER\REPOS\Esuyo\t5-owm-check`, build with `pio run -e t5-4_7`
(not the default S3 env).
