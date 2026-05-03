/************************************************************************/
/*                                                                      */
/*  subtitle_hw.c  --  CPU-side subtitle bitmap + overlay driver       */
/*                                                                      */
/************************************************************************/
/*  Bare-metal driver for the subtitle hardware pipeline.              */
/*  See subtitle_hw.h for the hardware register map and bitmap layout. */
/************************************************************************/

#include "subtitle_hw.h"
#include "font8x8.h"
#include "../timer_ps/timer_ps.h"   /* TimerDelay (microseconds) */
#include "xil_printf.h"

/* ------------------------------------------------------------------ */
/*  subtitle_hw_init                                                   */
/*                                                                     */
/*  Write default bar geometry and colours, clear the bitmap, then    */
/*  enable the overlay.  Call once during system initialisation.      */
/* ------------------------------------------------------------------ */
void subtitle_hw_init(void)
{
    /* Bar position and size */
    subtitle_write_reg(OVLY_REG_POS,
        SUBTITLE_PACK_POS(SUBTITLE_DEFAULT_X, SUBTITLE_DEFAULT_Y));
    subtitle_write_reg(OVLY_REG_SIZE,
        SUBTITLE_PACK_SIZE(SUBTITLE_DEFAULT_W, SUBTITLE_DEFAULT_H));

    /* Bar background and text colours */
    subtitle_write_reg(OVLY_REG_BAR_COLOR,  SUBTITLE_DEFAULT_BAR_COLOR);
    subtitle_write_reg(OVLY_REG_TEXT_COLOR, SUBTITLE_DEFAULT_TEXT_COLOR);

    /* Clear any stale bitmap in the BRAM */
    subtitle_clear();

    /* Enable the overlay (subtitle_enable writes OVLY_REG_CTRL) */
    subtitle_enable(1);

    xil_printf("[subtitle] hw init done — bar %dx%d at (%d,%d)\r\n",
               SUBTITLE_DEFAULT_W, SUBTITLE_DEFAULT_H,
               SUBTITLE_DEFAULT_X, SUBTITLE_DEFAULT_Y);
}

/* ------------------------------------------------------------------ */
/*  subtitle_clear                                                     */
/*                                                                     */
/*  Zero all 2 KB of the subtitle BRAM (all pixels off).              */
/*  Uses a simple word-by-word loop so the compiler can generate      */
/*  efficient 32-bit AXI writes.                                      */
/* ------------------------------------------------------------------ */
void subtitle_clear(void)
{
    int i;
    volatile u32 *bram = SUBTITLE_BRAM_PTR;

    for (i = 0; i < (BRAM_SIZE_BYTES / 4); i++) {
        bram[i] = 0u;
    }
}

/* ------------------------------------------------------------------ */
/*  subtitle_set_pixel / subtitle_clear_pixel                         */
/*                                                                     */
/*  Bounds-checked wrappers around the header macros.                 */
/* ------------------------------------------------------------------ */
void subtitle_set_pixel(int px, int py)
{
    if ((unsigned)px >= MASK_W || (unsigned)py >= MASK_H) return;
    subtitle_set_pixel_m(px, py);
}

void subtitle_clear_pixel(int px, int py)
{
    if ((unsigned)px >= MASK_W || (unsigned)py >= MASK_H) return;
    subtitle_clear_pixel_m(px, py);
}

/* ------------------------------------------------------------------ */
/*  subtitle_write_bitmap                                              */
/*                                                                     */
/*  Copy a packed 1bpp MSB-first bitmap region into the BRAM mask.   */
/*                                                                     */
/*  Coordinate system:                                                 */
/*    src[0] bit 7 = pixel (px, py)                                   */
/*    src[0] bit 6 = pixel (px+1, py)  ...etc.                        */
/*                                                                     */
/*  The BRAM uses LSB-first packing (bit 0 = leftmost pixel), so each */
/*  source bit must be mapped to the correct BRAM word and bit.       */
/*                                                                     */
/*  Out-of-bounds destination pixels are silently skipped.            */
/* ------------------------------------------------------------------ */
void subtitle_write_bitmap(const u8 *src, int px, int py, int w, int h)
{
    int row, col;
    int src_stride;     /* bytes per source row */
    int dx, dy;         /* destination pixel coords */
    int src_byte, src_bit_val;
    u32 word_idx, bit_idx;
    volatile u32 *bram = SUBTITLE_BRAM_PTR;

    if (!src || w <= 0 || h <= 0) return;

    src_stride = (w + 7) / 8;  /* ceil(w/8) bytes per row */

    for (row = 0; row < h; row++) {
        dy = py + row;
        if (dy < 0 || dy >= MASK_H) continue;  /* clip top/bottom */

        for (col = 0; col < w; col++) {
            dx = px + col;
            if (dx < 0 || dx >= MASK_W) continue;  /* clip left/right */

            /* Extract bit from source (MSB-first) */
            src_byte    = src[row * src_stride + col / 8];
            src_bit_val = (src_byte >> (7 - (col % 8))) & 1;

            /* Write to BRAM (LSB-first within 32-bit word) */
            word_idx = (u32)(dy * WORDS_PER_ROW + dx / 32);
            bit_idx  = (u32)(dx % 32);

            if (src_bit_val) {
                bram[word_idx] |=  (1u << bit_idx);
            } else {
                bram[word_idx] &= ~(1u << bit_idx);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  subtitle_commit                                                    */
/*                                                                     */
/*  Synchronise bitmap updates to a frame boundary using the          */
/*  hardware SOF (start-of-frame) sticky flag:                        */
/*                                                                     */
/*    1. Clear sof_flag by writing 0 to bit 1 of OVLY_REG_CTRL.      */
/*    2. Spin until the hardware sets bit 1 (next frame starts).      */
/*    3. Return — caller now has ~16 ms to finish any remaining BRAM  */
/*       writes before the hardware reads the mask for that frame.    */
/*                                                                     */
/*  NOTE: only the currently queued BRAM contents are used for the    */
/*  frame that follows the SOF.  For a clean swap, always call        */
/*  subtitle_clear() then subtitle_write_bitmap() BEFORE calling      */
/*  subtitle_commit().                                                 */
/* ------------------------------------------------------------------ */
int subtitle_commit(void)
{
    u32 ctrl;
    u32 timeout;

    /* Clear the sticky SOF flag (write 0 to bit 1, preserve bit 0) */
    ctrl = subtitle_read_reg(OVLY_REG_CTRL);
    subtitle_write_reg(OVLY_REG_CTRL, ctrl & ~OVLY_CTRL_SOF);

    /*
     * Wait for hardware to set the SOF flag (next frame start).
     * Timeout after ~50 ms worth of reads so we never hang if the
     * sof_flag feature is not yet wired up in the hardware.
     * Returns 1 on clean sync, 0 on timeout.
     */
    timeout = 5000000u;
    while (!(subtitle_read_reg(OVLY_REG_CTRL) & OVLY_CTRL_SOF)) {
        if (--timeout == 0) {
            xil_printf("[subtitle] WARN: SOF timeout — sof_flag not set by HW\r\n");
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  subtitle_enable                                                    */
/*                                                                     */
/*  Turn the subtitle overlay on (on=1) or pass video straight        */
/*  through (on=0) without touching any other control bits.           */
/* ------------------------------------------------------------------ */
void subtitle_enable(int on)
{
    u32 ctrl = subtitle_read_reg(OVLY_REG_CTRL);

    if (on) {
        ctrl |=  OVLY_CTRL_ENABLE;
    } else {
        ctrl &= ~OVLY_CTRL_ENABLE;
    }

    subtitle_write_reg(OVLY_REG_CTRL, ctrl);
}

/* ------------------------------------------------------------------ */
/*  subtitle_draw_char                                                 */
/*                                                                     */
/*  Render one 8×8 glyph at mask coordinates (x, y).                 */
/*  Font is MSB-first (bit 7 of each row byte = leftmost column).    */
/* ------------------------------------------------------------------ */
void subtitle_draw_char(char c, int x, int y)
{
    const u8 *glyph;
    int row, col;
    u8 bits;
    unsigned char uc = (unsigned char)c;

    if (uc == 0xD1u)        glyph = font8x8_N_tilde;
    else if (uc == 0xF1u)   glyph = font8x8_n_tilde;
    else if (uc >= 0x20u && uc <= 0x7Fu) glyph = font8x8[uc - 0x20u];
    else                    glyph = font8x8['?' - 0x20u];

    for (row = 0; row < 8; row++) {
        bits = glyph[row];
        for (col = 0; col < 8; col++) {
            if (bits & (0x80u >> col))
                subtitle_set_pixel(x + col, y + row);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  subtitle_draw_text                                                 */
/*                                                                     */
/*  Render a string left-to-right from (x, y).                       */
/*  Handles UTF-8 Ñ (0xC3 0x91) and ñ (0xC3 0xB1) transparently.   */
/* ------------------------------------------------------------------ */
void subtitle_draw_text(const char *str, int x, int y)
{
    const unsigned char *s = (const unsigned char *)str;
    while (*s) {
        if (*s == 0xC3u && (*(s+1) == 0x91u || *(s+1) == 0xB1u)) {
            /* 2-byte UTF-8 Ñ or ñ — convert to Latin-1 and draw */
            subtitle_draw_char((char)(*(s+1) == 0x91u ? 0xD1u : 0xF1u), x, y);
            s += 2;
        } else {
            subtitle_draw_char((char)*s, x, y);
            s++;
        }
        x += 8;
    }
}

/* ------------------------------------------------------------------ */
/*  subtitle_draw_text_centered                                        */
/* ------------------------------------------------------------------ */
void subtitle_draw_text_centered(const char *str, int y)
{
    /* Count display characters (UTF-8 multi-byte counts as one) */
    int len = 0;
    const unsigned char *s = (const unsigned char *)str;
    while (*s) {
        if (*s == 0xC3u && (*(s+1) == 0x91u || *(s+1) == 0xB1u))
            s++;   /* skip second byte of 2-byte sequence */
        s++;
        len++;
    }
    subtitle_draw_text(str, (MASK_W - len * 8) / 2, y);
}

/* ------------------------------------------------------------------ */
/*  subtitle_hw_self_test                                              */
/*                                                                     */
/*  Smoke-test sequence:                                               */
/*    1. Draw a 16x16 checkerboard starting at mask origin            */
/*    2. commit() to sync to a frame boundary                         */
/*    3. Toggle subtitle_enable on/off every 2 s for 6 cycles         */
/*                                                                     */
/*  Expected result on screen: a checkerboard pattern appears in the  */
/*  top-left corner of the subtitle bar, blinking every 2 seconds.   */
/* ------------------------------------------------------------------ */
void subtitle_hw_self_test(void)
{
    int i, px, py;

    xil_printf("[subtitle] self-test start\r\n");

    /* ---- 1. Render the phrase centred in the mask ---- */
    subtitle_clear();
    /* Vertical centre of a 64-row mask: (64 - 8) / 2 = 28 */
    subtitle_draw_text_centered("TE AMO POCHIS HASTA MA\xD1" "ANA", 28);

    /* ---- 2. Sync to a frame boundary ---- */
    TimerDelay(50000u);  /* 50 ms — ~3 frames at 60 Hz */
    xil_printf("[subtitle] phrase live (reg_ctrl=0x%08lX)\r\n",
               (unsigned long)subtitle_read_reg(OVLY_REG_CTRL));

    /* ---- 3. Blink the overlay on/off every 2 s for 6 cycles ---- */
    for (i = 0; i < 6; i++) {
        int is_on = (i % 2 == 0) ? 1 : 0;
        subtitle_enable(is_on);
        xil_printf("[subtitle] overlay %s\r\n", is_on ? "ON " : "OFF");
        TimerDelay(2000000u);
    }

    subtitle_enable(1);
    xil_printf("[subtitle] self-test done — overlay left ON\r\n");
}
