/************************************************************************/
/*                                                                      */
/*  overlay_ctrl.h  --  AXI4-Lite overlay bar control                  */
/*                                                                      */
/************************************************************************/
/*  Bare-metal driver for the custom overlay IP. Exposes register       */
/*  access helpers and an interactive UART control loop.                */
/*                                                                      */
/*  BEFORE USE: replace XPAR_OVERLAY_0_S_AXI_BASEADDR with the         */
/*  correct macro from your xparameters.h.                              */
/************************************************************************/

#ifndef OVERLAY_CTRL_H_
#define OVERLAY_CTRL_H_

#include "xil_types.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"

/* -------------------------------------------------------------------- */
/*  Register map                                                         */
/*                                                                       */
/*  !! PLACEHOLDER: replace with the correct macro from xparameters.h   */
/*  e.g. XPAR_MYOVERLAY_0_S_AXI_BASEADDR                                */
/* -------------------------------------------------------------------- */
#define OVERLAY_BASEADDR    XPAR_AXIS_VIDEO_OVERLAY_R_0_BASEADDR   /* <-- CHANGE ME */

#define OVERLAY_REG_POS     0x00    /* [15:0]=X,     [31:16]=Y      */
#define OVERLAY_REG_SIZE    0x04    /* [15:0]=width, [31:16]=height */
#define OVERLAY_REG_COLOR   0x08    /* [23:16]=R, [15:8]=G, [7:0]=B */
#define OVERLAY_REG_ALPHA   0x0C    /* [7:0]=alpha (0=transparent, 255=opaque) */

/* -------------------------------------------------------------------- */
/*  Register packing helpers                                             */
/* -------------------------------------------------------------------- */
#define OVERLAY_PACK_POS(x, y)      (((u32)(y) << 16) | ((u32)(x) & 0xFFFFu))
#define OVERLAY_PACK_SIZE(w, h)     (((u32)(h) << 16) | ((u32)(w) & 0xFFFFu))
#define OVERLAY_PACK_COLOR(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | ((u32)(b)))

/* -------------------------------------------------------------------- */
/*  Screen bounds  (adjust to match your active video resolution)       */
/* -------------------------------------------------------------------- */
#define OVERLAY_SCREEN_W    1280
#define OVERLAY_SCREEN_H    720

/* Step sizes for interactive controls */
#define OVERLAY_MOVE_STEP   10      /* pixels per w/a/s/d keypress      */
#define OVERLAY_SIZE_STEP   10      /* pixels per i/k/j/l keypress      */
#define OVERLAY_ALPHA_STEP  16      /* per t/g keypress (0-255 range)   */

/* -------------------------------------------------------------------- */
/*  Software-side state                                                  */
/* -------------------------------------------------------------------- */
typedef struct {
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u8  r, g, b;
    u8  alpha;
} OverlayState;

/* -------------------------------------------------------------------- */
/*  API                                                                  */
/* -------------------------------------------------------------------- */

/* Low-level register access */
static inline void overlay_write_reg(u32 offset, u32 value)
{
    Xil_Out32(OVERLAY_BASEADDR + offset, value);
}

static inline u32 overlay_read_reg(u32 offset)
{
    return Xil_In32(OVERLAY_BASEADDR + offset);
}

/* High-level setters (update hardware registers) */
void overlay_set_position(u16 x, u16 y);
void overlay_set_size(u16 w, u16 h);
void overlay_set_color(u8 r, u8 g, u8 b);
void overlay_set_alpha(u8 alpha);

/* State management */
void overlay_init_defaults(void);
void overlay_apply_all(void);
void overlay_print_state(void);

/* Interactive control */
void overlay_process_key(char c);
void overlay_uart_interactive_loop(void);
void overlay_run_demo(void);

#endif /* OVERLAY_CTRL_H_ */
