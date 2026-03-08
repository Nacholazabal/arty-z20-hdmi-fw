# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Xilinx SDK (Vivado 2018.2) embedded C project for the **Digilent Arty Z7-20** board (Zynq-7000 SoC, Cortex-A9). It demonstrates HDMI input capture and display output, originally authored by Digilent Inc. The application runs bare-metal (no OS) on the ARM Cortex-A9.

Video data streams in through the HDMI In port and out through the HDMI Out port. A UART serial interface (USB-UART bridge, MicroUSB) allows the user to configure the demo at runtime using a terminal emulator (e.g. Tera Term or PuTTY).

### UART Menu Options

| Key | Function |
|---|---|
| `1` | Change the resolution of the HDMI output |
| `2` | Change the framebuffer index shown on the HDMI output |
| `3` | Write a blended test pattern to the current display framebuffer |
| `4` | Write a color bar test pattern to the current display framebuffer |
| `5` | Start/Stop streaming video from HDMI In into the video framebuffer |
| `6` | Change the framebuffer index that HDMI input streams into |
| `7` | Grab the current video frame, invert its colors, store to next buffer and display it |
| `8` | Scale the current video frame to display resolution, store to next buffer and display it |
| `q` | Quit |

## Build Environment

This project is built and deployed entirely through **Xilinx SDK (Eclipse-based IDE)**. There are no standalone Makefile-based CLI build commands — compilation, linking, and deployment happen within the SDK GUI or via the SDK's command-line tools (`xsct`).

- **Toolchain**: `arm-none-eabi-gcc` (provided by Xilinx SDK)
- **Linker script**: [lscript.ld](lscript.ld) — targets `ps7_ddr_0` at base `0x100000` with 511 MB length
- **Hardware platform**: Generated from Vivado bitstream; peripheral addresses come from `xparameters.h` (auto-generated, not in this directory)
- **BSP libraries**: Xilinx standalone BSP (xil, xuartps, xaxivdma, xvtc, xgpio, xscugic, xscutimer)

## Architecture

The application uses a **triple-framebuffer** scheme shared between a video capture path (HDMI In → DDR) and a display output path (DDR → HDMI Out), both driven by the same AXI VDMA instance.

### Data Flow

```
HDMI In → DVI2RGB → AXI Stream → AXI VDMA (S2MM) → DDR framebuffers
DDR framebuffers → AXI VDMA (MM2S) → AXI Stream to Video → RGB2DVI → HDMI Out
```

### Key Hardware Peripherals (mapped via `xparameters.h`)

| Alias (in video_demo.c) | Purpose |
|---|---|
| `AXI_DYNCLK_0` | Pixel clock generator for display output |
| `AXIVDMA_0` | AXI Video DMA (shared by both capture and display) |
| `VTC_0` | Video Timing Controller for display output |
| `VTC_1` | Video Timing Controller for video capture (detects input timing) |
| `AXI_GPIO_VIDEO` | GPIO for Hot-Plug Detect (HPD) and pixel clock lock signals |
| `SCUTIMER` | SCU timer used for millisecond delays |
| `PS7_UART_0` | UART for the interactive serial menu |

### Module Breakdown

- **[video_demo.c](video_demo.c) / [video_demo.h](video_demo.h)** — Top-level application. Owns the framebuffer arrays (`frameBuf[3][1920*1080*3]`), the interrupt vector table, and the UART-driven interactive menu. Entry point is `main()` → `DemoInitialize()` → `DemoRun()`.

- **[display_ctrl/display_ctrl.h](display_ctrl/display_ctrl.h)** — Driver for the display output path. API: `DisplayInitialize`, `DisplayStart`, `DisplayStop`, `DisplaySetMode`, `DisplayChangeFrame`. Manages the VDMA MM2S channel and VTC core for the output side.

- **[display_ctrl/vga_modes.h](display_ctrl/vga_modes.h)** — Defines `VideoMode` structs for supported output resolutions: 640×480, 800×600, 1280×720, 1280×1024, 1920×1080 (all at 60 Hz).

- **[video_capture/video_capture.h](video_capture/video_capture.h)** — Driver for the HDMI input path. API: `VideoInitialize`, `VideoStart`, `VideoStop`, `VideoChangeFrame`, `VideoSetCallback`. Manages the VDMA S2MM channel, VTC_1 for timing detection, and GPIO for HPD/lock interrupts. Fires a user callback (`DemoISR`) on signal connect/disconnect.

- **[dynclk/dynclk.h](dynclk/dynclk.h)** — Driver for the Digilent `axi_dynclk` core (MMCM/PLL reconfiguration). Used by `display_ctrl` to set the pixel clock frequency at runtime. API: `ClkFindParams`, `ClkFindReg`, `ClkWriteReg`, `ClkStart`, `ClkStop`.

- **[intc/intc.h](intc/intc.h)** — Thin wrapper around `XScuGic` (Zynq) or `XIntc` (MicroBlaze). Provides `fnInitInterruptController` and `fnEnableInterrupts` using the `ivt_t` interrupt vector table type. The preprocessor selects the correct implementation based on `XPAR_INTC_0_DEVICE_ID`.

- **[timer_ps/timer_ps.h](timer_ps/timer_ps.h)** — Microsecond delay using the Zynq SCU private timer. API: `TimerInitialize(SCU_TIMER_ID)`, `TimerDelay(uSec)`.

### Framebuffer Memory Layout

- 3 frames × (1920 × 1080 × 3 bytes) = ~18 MB allocated statically in BSS
- Pixel format is **24-bit RGB** packed: byte order `[R, B, G]` per pixel (note: Blue and Green are swapped from the typical RGB order)
- Stride is fixed at `1920 * 3 = 5760` bytes regardless of active resolution
- After CPU writes to framebuffers, `Xil_DCacheFlushRange` must be called to ensure VDMA (which bypasses cache) sees the updated data

### Interrupt Architecture

Two interrupt sources are registered via `ivt_t` array in `video_demo.c`:
1. **GPIO interrupt** (`VID_GPIO_IRPT_ID` = FPGA3) → `GpioIsr` → detects HPD and pixel clock lock changes
2. **VTC interrupt** (`VID_VTC_IRPT_ID` = FPGA4) → `XVtc_IntrHandler` → signals stable video timing detection

Both feed into the `DemoISR` callback which sets `fRefresh = 1`, causing the UART menu to redisplay with updated capture resolution.
