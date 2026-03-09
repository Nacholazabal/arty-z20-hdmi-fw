# arty-z20-hdmi-fw

Bare-metal ARM firmware for the **Arty Z7-20 HDMI pass-through** project.

Runs on the Zynq-7000 PS7 (ARM Cortex-A9) and manages the full video pipeline: HDMI input detection, frame buffer allocation in DDR3, and HDMI output configuration. A simple UART command interface lets you change resolution, toggle video processing effects, and generate test patterns at runtime — no recompilation needed.

> This firmware is tracked as a git submodule inside [arty-z20-hdmi](https://github.com/Nacholazabal/arty-z20-hdmi). Clone the parent repo with `--recurse-submodules` to get everything in one step.

---

## How it works

```
HDMI In ──► VDMA (PL) ──► DDR3 Frame Buffer ──► VDMA (PL) ──► HDMI Out
                                  ▲
                             ARM Cortex-A9
                          (this firmware)
                                  │
                            UART Console
```

The PL (FPGA fabric) handles the low-level HDMI PHY, pixel clock recovery, and DMA transfers. The PS (ARM) firmware initialises all IP cores, starts the video pipeline, and then sits in a UART command loop — making it easy to reconfigure the pipeline live without touching the hardware.

---

## Requirements

| Tool | Version |
|------|---------|
| Xilinx SDK | 2018.2 |
| Hardware platform | `hw/export/hdmi_in_wrapper.hdf` (from the parent repo) |
| Board | Arty Z7-20 |

> **Vitis users:** Xilinx SDK 2018.2 is a legacy IDE. If you prefer Vitis (2019.2+), you can import the project as a legacy application — the source code is plain C and does not require changes.

---

## Building the firmware

### 1. Import the hardware platform

1. Open **Xilinx SDK 2018.2**.
2. Set the workspace to any convenient directory.
3. Go to **File → New → Application Project**.
4. In the *Hardware Platform* step, click **Create New** and browse to `hw/export/hdmi_in_wrapper.hdf` (from the parent repository).
5. SDK will generate the BSP (Board Support Package) automatically.

### 2. Import the application source

1. In the *New Application Project* wizard, choose **Empty Application** as the template.
2. After the project is created, right-click the `src/` folder and select **Import → File System**.
3. Browse to the `src/` directory of this repository and import all `.c` and `.h` files.

### 3. Build

- Press **Ctrl+B** (or right-click the project → **Build Project**).
- The compiled ELF will appear in `<project>/Debug/` or `<project>/Release/`.

---

## Programming the board

You need both the **bitstream** and the **ELF** loaded onto the board.

### Option A — Program from SDK (recommended for development)

1. Connect the Arty Z7-20 via USB.
2. In SDK, go to **Xilinx → Program FPGA**.
3. Select the bitstream: `release/hdmi_in_wrapper.bit` (from the parent repo).
4. Right-click your application project → **Run As → Launch on Hardware (System Debugger)**.

### Option B — Command line with `xsdb`

```tcl
xsdb
connect
targets -set -filter {name =~ "APU"}
fpga -file release/hdmi_in_wrapper.bit
targets -set -filter {name =~ "ARM Cortex-A9 #0"}
dow release/Arty-Z7-20-hdmi-in.elf
con
exit
```

---

## UART command interface

Connect a serial terminal to the board's USB-UART port at **115200 baud, 8N1**. On boot, the firmware prints a welcome message and waits for single-character commands.

| Key | Action |
|-----|--------|
| `1` | Set output resolution to **1280 × 720 @ 60 Hz** |
| `2` | Set output resolution to **1920 × 1080 @ 60 Hz** |
| `+` | Cycle to the next supported resolution |
| `v` | Toggle **video inversion** (colour negative effect) |
| `s` | Toggle **frame scaling** (fit input to output resolution) |
| `t` | Toggle **test pattern** generator (bypass HDMI input) |
| `r` | **Reset** the video pipeline |
| `?` | Print the command menu |

All commands take effect immediately without restarting the board.

---

## Repository structure

```
sw/
├── src/
│   ├── main.c               Application entry point and UART command loop
│   ├── video_demo.c/h       Video pipeline initialisation and control
│   ├── vdma_config.c/h      AXI VDMA configuration helpers
│   └── ...                  Supporting drivers and utilities
└── README.md                This file
```

---

## Credits

Based on the firmware from **[Digilent/Arty-Z7-20-hdmi-in](https://github.com/Digilent/Arty-Z7-20-hdmi-in)** by [Digilent, Inc.](https://digilent.com) The original application demonstrated HDMI pass-through with UART control on the Arty Z7-20. This version has been significantly reworked to resolve initialisation issues and adapt the pipeline for extended use.
