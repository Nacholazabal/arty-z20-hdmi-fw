# Firmware - HDMI Subtitle Overlay Controller

Bare-metal ARM firmware for the Arty Z7-20 HDMI subtitle overlay platform.

This software runs on the Zynq PS7 and manages the video pipeline around the FPGA hardware: display timing, capture start/stop, frame-buffer selection, diagnostics, and runtime control of the subtitle overlay hardware.

It is tracked here as the `sw/` submodule of the main repository so the hardware and firmware stay documented together.

## What The Firmware Does

The firmware is responsible for:

- initializing the HDMI display/output path,
- initializing HDMI input capture and handling signal-detect events,
- managing three DDR-backed frame buffers used by the VDMA pipeline,
- exposing a UART menu for live control and debugging,
- initializing the subtitle overlay hardware,
- and providing runtime tools for overlay placement and subtitle-memory testing.

At a high level:

```text
HDMI IN
  -> capture pipeline in PL
  -> DDR frame buffers
  -> overlay pipeline in PL
  -> HDMI OUT
         ^
         |
   Cortex-A9 firmware
   + UART control loop
```

## Toolchain

- Xilinx SDK 2018.2
- Arty Z7-20 hardware platform exported from the parent repo
- Bare-metal C project flow

The firmware source is plain C, but the checked-in workflow and project structure are aimed at the classic SDK flow.

## Building In SDK

### Hardware Platform

Use the hardware export from the parent repository:

- `hw/export/hdmi_in_wrapper.hdf`

### Suggested Import Flow

1. Open Xilinx SDK 2018.2.
2. Create or choose a workspace.
3. Create a new application project using the exported hardware platform.
4. Choose `Empty Application`.
5. Import the contents of `sw/src/` into the new project's `src/` folder.
6. Let SDK generate the BSP.
7. Build the project in SDK.

The generated ELF will appear in the SDK project's build output directory.

## Programming During Development

The firmware README no longer includes command-line board-programming steps. For development, the expected workflow is:

1. Program the FPGA from Xilinx SDK using the bitstream from the parent repo.
2. Launch the firmware ELF from SDK onto the board.
3. Open the USB-UART console at `115200 8N1`.

The parent repository README documents the hardware-side bitstream context.

## UART Main Menu

On boot, the firmware presents a single-character UART menu. The current top-level commands implemented in `video_demo.c` are:

| Key | Action |
|---|---|
| `1` | Change display resolution |
| `2` | Cycle display frame buffer index |
| `3` | Write blended test pattern to the current display buffer |
| `4` | Write color bars to the current display buffer |
| `5` | Start or stop HDMI input capture |
| `6` | Cycle capture frame buffer index |
| `7` | Grab a frame and display an inverted copy |
| `8` | Grab a frame and display a scaled copy |
| `o` | Enter overlay control mode |
| `t` | Run subtitle hardware self-test |
| `d` | Print system diagnostics |
| `q` | Quit |

The menu also refreshes itself when HDMI signal-detect events occur.

When you press `1`, the resolution submenu currently offers:

- `1` - `640x480`
- `2` - `800x600`
- `3` - `1280x720`
- `4` - `1280x1024`
- `5` - `1920x1080`
- `q` - leave the resolution unchanged

## Overlay Control Mode

Press `o` from the main menu to enter the interactive overlay-control loop.

Implemented keys in `overlay_ctrl.c`:

| Key | Action |
|---|---|
| `w` / `s` | Move overlay up / down |
| `a` / `d` | Move overlay left / right |
| `i` / `k` | Increase / decrease height |
| `l` / `j` | Increase / decrease width |
| `t` / `g` | Increase / decrease opacity setting used by the software control mode |
| `r` | Red |
| `e` | Green |
| `b` | Blue |
| `y` | Yellow |
| `c` | Cyan |
| `m` | Magenta |
| `0` | Black |
| `W` | White |
| `p` | Print current overlay state |
| `D` | Run overlay movement/color demo |
| `h` | Reprint help |
| `q` | Exit overlay control mode |

## Subtitle Hardware Support

The firmware includes a dedicated subtitle hardware helper in:

- `src/subtitle_hw/subtitle_hw.c`
- `src/subtitle_hw/subtitle_hw.h`

This code is responsible for:

- writing the packed 1 bpp subtitle bitmap into BRAM,
- configuring the overlay register block,
- enabling and disabling the subtitle overlay,
- drawing text into the subtitle bitmap,
- and waiting for frame-safe updates using the hardware SOF flag.

The `t` command in the main menu runs a built-in self-test that exercises this path.

## Important Build Note

Before building, verify that the base-address macros in the firmware headers match the generated `xparameters.h` from your exported hardware platform.

The most important one for the current subtitle-overlay hardware is:

- `src/subtitle_hw/subtitle_hw.h`

That header expects the BRAM controller and overlay IP instance names exported by the current hardware design.

## Source Layout

```text
sw/src/
|-- video_demo.c                 Main application loop and UART menu
|-- video_demo.h                 Shared demo definitions and logging macros
|-- display_ctrl/                HDMI output / display pipeline helpers
|-- video_capture/               HDMI input capture and signal handling
|-- dynclk/                      Dynamic pixel-clock support
|-- intc/                        Interrupt controller helpers
|-- timer_ps/                    PS timer helpers
|-- overlay_ctrl/                Interactive overlay-control mode
`-- subtitle_hw/                 Subtitle bitmap and overlay hardware driver
```

## Relationship To The Hardware Repo

This firmware is paired with the hardware tracked in the parent repository:

- the VDMA-based HDMI input/output pipeline,
- the `axis_video_overlay_rect` hardware block,
- and the `subtitle_mask_mem` BRAM-backed subtitle store.

The firmware README focuses on runtime control and software structure; the hardware architecture is documented in the parent repo.

## Credits

This firmware started from Digilent's **[Arty-Z7-20 HDMI In demo](https://github.com/Digilent/Arty-Z7-20-hdmi-in)**, but the current codebase has been reworked around the thesis platform: improved diagnostics, revised UART flow, subtitle hardware support, and runtime overlay control.
