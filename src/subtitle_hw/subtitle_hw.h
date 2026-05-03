/************************************************************************/
/*                                                                      */
/*  subtitle_hw.h  --  CPU-side subtitle bitmap + overlay driver       */
/*                                                                      */
/************************************************************************/
/*  Bare-metal driver for the subtitle hardware pipeline:              */
/*                                                                      */
/*    - AXI BRAM Controller  : holds the 1bpp subtitle mask (2 KB)    */
/*    - axis_video_overlay_rect : AXI-Lite overlay control registers   */
/*                                                                      */
/*  BEFORE USE: verify the two BASEADDR macros below against your      */
/*  xparameters.h (search for AXI_BRAM_CTRL and OVERLAY_RECT).        */
/************************************************************************/

#ifndef SUBTITLE_HW_H_
#define SUBTITLE_HW_H_

#include "xil_types.h"
#include "xil_io.h"
#include "xparameters.h"
#include <string.h>     /* memset */

/* ------------------------------------------------------------------ */
/*  Address map                                                        */
/*                                                                     */
/*  Typical xparameters.h names — adjust if yours differ:             */
/*    BRAM:    XPAR_AXI_BRAM_CTRL_0_S_AXI_BASEADDR                    */
/*    Overlay: XPAR_AXIS_VIDEO_OVERLAY_RECT_0_BASEADDR                 */
/*             (or XPAR_AXIS_VIDEO_OVERLAY_RECT_0_S_AXI_BASEADDR)     */
/* ------------------------------------------------------------------ */
#define SUBTITLE_BRAM_BASEADDR   XPAR_AXI_BRAM_CTRL_0_S_AXI_BASEADDR
#define SUBTITLE_OVLY_BASEADDR   XPAR_AXIS_VIDEO_OVERLAY_R_0_BASEADDR

/* ------------------------------------------------------------------ */
/*  Bitmap mask geometry                                               */
/* ------------------------------------------------------------------ */
#define MASK_W              256     /* mask width in pixels             */
#define MASK_H              64      /* mask height in pixels            */
#define WORDS_PER_ROW       8       /* MASK_W / 32                      */
#define BRAM_SIZE_BYTES     2048    /* 512 words x 4 bytes              */

/* ------------------------------------------------------------------ */
/*  Overlay register offsets  (axis_video_overlay_rect)               */
/*                                                                     */
/*   0x00  reg0  [15:0]=x_start   [31:16]=y_start                     */
/*   0x04  reg1  [15:0]=bar_width [31:16]=bar_height                  */
/*   0x08  reg2  [23:0] bar background colour  RGB888                  */
/*   0x0C  reg3  [23:0] text / foreground colour RGB888                */
/*   0x10  reg4  [0]=subtitle_enable  [1]=sof_flag (hw sticky)        */
/* ------------------------------------------------------------------ */
#define OVLY_REG_POS        0x00u
#define OVLY_REG_SIZE       0x04u
#define OVLY_REG_BAR_COLOR  0x08u
#define OVLY_REG_TEXT_COLOR 0x0Cu
#define OVLY_REG_CTRL       0x10u

/* Control register bit masks */
#define OVLY_CTRL_ENABLE    (1u << 0)   /* 1 = show overlay, 0 = passthrough */
#define OVLY_CTRL_SOF       (1u << 1)   /* set by HW each frame start        */

/* ------------------------------------------------------------------ */
/*  Default bar geometry and colours                                   */
/*  Adjust to match your display resolution and preferred subtitle bar */
/* ------------------------------------------------------------------ */
#define SUBTITLE_DEFAULT_X          160u
#define SUBTITLE_DEFAULT_Y          900u
#define SUBTITLE_DEFAULT_W          1600u
#define SUBTITLE_DEFAULT_H          120u
#define SUBTITLE_DEFAULT_BAR_COLOR  0x000000u   /* black background */
#define SUBTITLE_DEFAULT_TEXT_COLOR 0xFFFFFFu   /* white text       */

/* ------------------------------------------------------------------ */
/*  Register packing helpers                                           */
/* ------------------------------------------------------------------ */
#define SUBTITLE_PACK_POS(x, y)     (((u32)(y) << 16) | ((u32)(x) & 0xFFFFu))
#define SUBTITLE_PACK_SIZE(w, h)    (((u32)(h) << 16) | ((u32)(w) & 0xFFFFu))

/* ------------------------------------------------------------------ */
/*  Low-level register accessors                                       */
/* ------------------------------------------------------------------ */
static inline void subtitle_write_reg(u32 offset, u32 val)
{
    Xil_Out32(SUBTITLE_OVLY_BASEADDR + offset, val);
}

static inline u32 subtitle_read_reg(u32 offset)
{
    return Xil_In32(SUBTITLE_OVLY_BASEADDR + offset);
}

/* ------------------------------------------------------------------ */
/*  BRAM pixel macros                                                  */
/*                                                                     */
/*  Layout: 1bpp, row-major, 32 pixels packed per u32 word            */
/*    word_index = py * WORDS_PER_ROW + px / 32                       */
/*    bit_index  = px % 32  (bit 0 = leftmost pixel of the group)     */
/*                                                                     */
/*  The BRAM region is memory-mapped as a peripheral (not cached),    */
/*  so no dcache flush is needed after CPU writes.                     */
/* ------------------------------------------------------------------ */
#define SUBTITLE_BRAM_PTR   ((volatile u32 *)(SUBTITLE_BRAM_BASEADDR))

#define SUBTITLE_WORD_IDX(px, py)   ((py) * WORDS_PER_ROW + (px) / 32)
#define SUBTITLE_BIT_IDX(px)        ((u32)((px) % 32))

#define subtitle_set_pixel_m(px, py) \
    (SUBTITLE_BRAM_PTR[SUBTITLE_WORD_IDX((px), (py))] |=  (1u << SUBTITLE_BIT_IDX(px)))

#define subtitle_clear_pixel_m(px, py) \
    (SUBTITLE_BRAM_PTR[SUBTITLE_WORD_IDX((px), (py))] &= ~(1u << SUBTITLE_BIT_IDX(px)))

#define subtitle_test_pixel_m(px, py) \
    (!!(SUBTITLE_BRAM_PTR[SUBTITLE_WORD_IDX((px), (py))] & (1u << SUBTITLE_BIT_IDX(px))))

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/* Initialise: set default bar position / colours, enable overlay */
void subtitle_hw_init(void);

/* Clear all pixels in the 2 KB bitmap (sets entire mask to 0) */
void subtitle_clear(void);

/* Set / clear a single pixel.  Bounds-checked — out-of-range coords ignored */
void subtitle_set_pixel(int px, int py);
void subtitle_clear_pixel(int px, int py);

/*
 * subtitle_write_bitmap
 *   Copy a packed 1bpp region into the mask at top-left offset (px, py).
 *
 *   src  : row-major 1bpp bitmap, MSB-first (standard font glyph format)
 *          byte 0 bit 7 = leftmost pixel of first row
 *   px, py : destination top-left in mask coordinates
 *   w, h   : width and height of the source region in pixels
 *
 *   The function converts MSB-first source bits to the LSB-first BRAM layout.
 *   Pixels that would land outside [0, MASK_W) x [0, MASK_H) are silently
 *   clipped.
 */
void subtitle_write_bitmap(const u8 *src, int px, int py, int w, int h);

/*
 * subtitle_commit
 *   Wait for the next frame-start (SOF) boundary, then atomically install
 *   whatever has been written into the BRAM.  Call this after
 *   subtitle_clear() + subtitle_write_bitmap() to avoid tearing.
 *
 *   Returns 1 if SOF was received, 0 on timeout (~50 ms).
 *   On timeout the BRAM contents are still live — only frame-sync is lost.
 */
int subtitle_commit(void);

/* Turn the subtitle overlay on (on=1) or off/passthrough (on=0) */
void subtitle_enable(int on);

/*
 * subtitle_draw_char
 *   Render one ASCII character (0x20-0x7E) at mask position (x, y)
 *   using the built-in 8×8 bitmap font.  Out-of-range pixels are clipped.
 *   Supports Latin-1 extras: 0xD1 = Ñ, 0xF1 = ñ.
 */
void subtitle_draw_char(char c, int x, int y);

/*
 * subtitle_draw_text
 *   Render a null-terminated string starting at mask position (x, y).
 *   Characters are 8 pixels wide; the cursor advances 8 px per character.
 *   Handles UTF-8 encoded Ñ (0xC3 0x91) and ñ (0xC3 0xB1) automatically.
 */
void subtitle_draw_text(const char *str, int x, int y);

/*
 * subtitle_draw_text_centered
 *   Same as subtitle_draw_text but horizontally centred within [0, MASK_W).
 *   y is the top pixel row of the text (use MASK_H/2 - 4 for vertical centre).
 */
void subtitle_draw_text_centered(const char *str, int y);

/*
 * subtitle_hw_self_test
 *   Renders "TE AMO POCHIS HASTA MAÑANA" centred in the subtitle bar,
 *   then toggles the overlay on/off every 2 seconds for 6 cycles.
 *   Logs progress to UART.  Safe to call from the UART menu.
 */
void subtitle_hw_self_test(void);

#endif /* SUBTITLE_HW_H_ */
