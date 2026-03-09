# CHANGES.md — Arty Z7-20 HDMI-In Demo

Changelog tracking modifications to this codebase and why they were made.
Most recent changes are listed first.

---

## 2026-03-06 — Fix framebuf alignment (DmaSetBufferAddr XST_INVALID_PARAM)

### Problem
`VideoStart` failed with `DmaSetBufferAddr failed 15` (XST_INVALID_PARAM = 15).

### Root Cause
The AXI VDMA S2MM channel has a 64-bit (8-byte) AXI data bus. Its driver rejects
frame buffer addresses that are not word-aligned. `frameBuf` was placed by the linker
at `0x00120634` — 4-byte aligned but **not** 8-byte aligned (`0x634 & 7 = 4 ≠ 0`).
The MM2S (display) channel uses a 32-bit bus (4-byte alignment suffices) so it worked
with the same addresses; only S2MM failed.

### Fix Applied

| File | Change |
|------|--------|
| `video_demo.c` frameBuf declaration | Added `__attribute__((aligned(64)))` |

`DEMO_MAX_FRAME = 1920×1080×3 = 6,220,800` bytes is divisible by 64, so all three
frames in the array stay 64-byte aligned.

### Also Added
- `XAxiVdma_Reset` of S2MM channel before config in `VideoStart` (clears error state)
- `xil_printf` error reporting in `VideoStart` replacing silent `xdbg_printf`
- Register-level diagnostics section in `DemoDiagnostics` (GPIO, GIC, VDMA S2MM)

---

## 2026-03-06 — Fix GPIO ISR interrupt storm (xil_printf in ISR causing lockup)

### Problem
After changing resolution to 1920x1080 (or at any time no HDMI source is connected),
the terminal would freeze. UART log showed rapid bouncing:
```
[INFO] HDMI: LOCKED signal HIGH - initializing VTC timing detection
[WARN] HDMI: LOCKED signal LOW - signal lost
[INFO] HDMI: LOCKED signal HIGH - initializing VTC timing detection
...
```
The board became completely unresponsive to UART input.

### Root Cause
`xil_printf` inside `GpioIsr` and `VtcIsr` is a blocking UART TX loop.
At 115200 baud, printing ~70 characters takes ~6ms. When the DVI2RGB MMCM
"locked" signal bounces (normal behavior with no HDMI source — floating clock
input causes MMCM lock/unlock oscillation), each bounce queues a new GPIO
interrupt before the previous ISR finishes its UART output. The CPU spends
100% of its time in ISR context, starving the main loop entirely.

Additionally, the XVtc_CfgInitialize + SelfTest + EnableDetector sequence
in the HIGH path takes additional time, widening the window for edge
accumulation.

### Fixes Applied

| File | Change |
|------|--------|
| `video_capture/video_capture.c` GpioIsr | Removed `xil_printf` from HIGH path |
| `video_capture/video_capture.c` GpioIsr | Removed `xil_printf` from LOW path |
| `video_capture/video_capture.c` GpioIsr | Added `XGpio_InterruptClear` at end of HIGH path to absorb edges accumulated during VTC init |
| `video_capture/video_capture.c` VtcIsr | Removed both `xil_printf` calls |

### Rule Learned
**Never call `xil_printf` (or any blocking I/O) from an ISR.** ISRs must be fast.
Logging HDMI events is available via the `[INFO] HDMI event:` in case `'r'` of
the main loop, which fires when `fRefresh=1` is set by the callback.

### Verification
- Rebuild and run with no HDMI source connected
- Menu should be fully responsive to UART input at all times
- HDMI connect/disconnect events still logged via case 'r' in main loop

---

## 2026-03-03 — Fix UART menu hang + volatile fRefresh + remove startup color bars

### Problem
After the UART revamp, the board appeared "stuck" on boot: UART output would stop
mid-menu at the `Pixel Clock` line, and the menu was unresponsive to input.

### Root Causes

1. **`printf("%f")` hangs with newlib-nano** (`video_demo.c`)
   - `DemoPrintMenu()`, `DemoCRMenu()`, and `DemoDiagnostics()` called `printf()` with
     `%f` for the pixel clock frequency.
   - Xilinx SDK bare-metal uses newlib-nano by default. Without `-u _printf_float`,
     `printf("%f")` either stalls in an infinite loop or silently produces nothing.
   - Output stopped exactly at `640x480@60Hz*` because the very next call was the
     `printf` float line. System froze there.

2. **`fRefresh` not `volatile`** (`video_demo.c`)
   - `fRefresh` is a plain `char` written by ISRs (GpioIsr → DemoISR) but read in a
     busy-wait loop in `DemoRun()`. With optimization, the compiler can cache the value
     in a register, making the ISR write invisible to the main loop — causing an infinite
     spin even after an HDMI event fires.

### Fixes Applied

| File | Change |
|------|--------|
| `video_demo.c:66` | `char fRefresh` → `volatile char fRefresh` |
| `video_demo.c:366` | `printf("%f")` in `DemoPrintMenu()` → `xil_printf` with int+frac |
| `video_demo.c:514` | `printf("%f")` in `DemoCRMenu()` → `xil_printf` with int+frac |
| `video_demo.c:805` | `printf("%f")` in `DemoDiagnostics()` → `xil_printf` with int+frac |
| `video_demo.c:32` | Removed `#include <stdio.h>` (was only needed for printf) |
| `video_demo.c:204` | Removed startup color bar test pattern (user saw bars immediately on monitor) |
| `video_demo.h:55` | Updated comment: printf not needed, use integer split instead |

### Verification
- Rebuild in Xilinx SDK (`Project > Build All`)
- Program FPGA + run binary
- UART menu should fully render including `Pixel Clock` line
- All menu keys (`1`-`8`, `d`, `q`) should respond
- Monitor should show blank/black at startup (not color bars)

---

## 2026-03-02 — UART revamp: logging macros + verbose init + enhanced menu

### Problem
Silent failures during init (VDMA, VTC, GPIO) made debugging impossible.
Timer init had a logic bug. Menu showed no state, no context hints.

### Fixes Applied

| File | Change |
|------|--------|
| `video_demo.h` | Added `LOG_INFO / LOG_WARN / LOG_ERR / LOG_DBG` macros |
| `video_demo.h` | Added `DemoDiagnostics()` declaration |
| `video_demo.c` | Verbose init logging throughout `DemoInitialize()` |
| `video_demo.c` | Per-action feedback and enhanced menu with state labels |
| `video_demo.c` | Added `'d'` diagnostic command (`DemoDiagnostics()`) |
| `video_demo.c` | Case `'r'` logs HDMI event details before 2s debounce |
| `timer_ps/timer_ps.c:76` | Fixed logic bug: `\|\|` → `&&` (was always returning failure) |
| `video_capture/video_capture.c` | Added `xil_printf` in `GpioIsr` and `VtcIsr` |

---
