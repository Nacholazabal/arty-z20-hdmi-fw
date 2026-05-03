/************************************************************************/
/*                                                                      */
/*  overlay_ctrl.c  --  AXI4-Lite overlay bar control                  */
/*                                                                      */
/************************************************************************/
/*  Bare-metal driver for the custom overlay IP.                        */
/*  Implements register helpers, state management, and an interactive   */
/*  UART control loop using the PS UART polling API (no OS required).   */
/************************************************************************/

#include "overlay_ctrl.h"
#include "xuartps.h"        /* XUartPs_IsReceiveData, XUartPs_ReadReg */
#include "../timer_ps/timer_ps.h"   /* TimerDelay */

/* UART base address — matches video_demo.c */
#define OVERLAY_UART_BASEADDR   XPAR_PS7_UART_0_BASEADDR

/* -------------------------------------------------------------------- */
/*  Module-private state                                                 */
/* -------------------------------------------------------------------- */

static OverlayState overlay;    /* single software-side copy of register state */

/* -------------------------------------------------------------------- */
/*  Internal clamping helpers                                            */
/* -------------------------------------------------------------------- */

static u16 clamp_u16(int v, int lo, int hi)
{
    if (v < lo) return (u16)lo;
    if (v > hi) return (u16)hi;
    return (u16)v;
}

/* -------------------------------------------------------------------- */
/*  High-level register setters                                          */
/*  These write to hardware AND update the software state struct.       */
/* -------------------------------------------------------------------- */

void overlay_set_position(u16 x, u16 y)
{
    overlay.x = x;
    overlay.y = y;
    overlay_write_reg(OVERLAY_REG_POS, OVERLAY_PACK_POS(x, y));
}

void overlay_set_size(u16 w, u16 h)
{
    overlay.width  = w;
    overlay.height = h;
    overlay_write_reg(OVERLAY_REG_SIZE, OVERLAY_PACK_SIZE(w, h));
}

void overlay_set_color(u8 r, u8 g, u8 b)
{
    overlay.r = r;
    overlay.g = g;
    overlay.b = b;
    overlay_write_reg(OVERLAY_REG_COLOR, OVERLAY_PACK_COLOR(r, g, b));
}

void overlay_set_alpha(u8 alpha)
{
    overlay.alpha = alpha;
    overlay_write_reg(OVERLAY_REG_ALPHA, (u32)alpha);
}

/* -------------------------------------------------------------------- */
/*  State management                                                     */
/* -------------------------------------------------------------------- */

/*
 * overlay_init_defaults
 *   Populate the software state with sensible starting values.
 *   Call overlay_apply_all() afterward to push them to hardware.
 */
void overlay_init_defaults(void)
{
    overlay.x      = 0;
    overlay.y      = 600;       /* near bottom, subtitle-bar style */
    overlay.width  = 1280;
    overlay.height = 80;
    overlay.r      = 0;
    overlay.g      = 0;
    overlay.b      = 0;
    overlay.alpha  = 180;       /* semi-transparent */
}

/*
 * overlay_apply_all
 *   Write the entire software state to hardware in one call.
 *   Safe to call at any time to re-sync hardware after soft reset etc.
 */
void overlay_apply_all(void)
{
    overlay_write_reg(OVERLAY_REG_POS,   OVERLAY_PACK_POS(overlay.x, overlay.y));
    overlay_write_reg(OVERLAY_REG_SIZE,  OVERLAY_PACK_SIZE(overlay.width, overlay.height));
    overlay_write_reg(OVERLAY_REG_COLOR, OVERLAY_PACK_COLOR(overlay.r, overlay.g, overlay.b));
    overlay_write_reg(OVERLAY_REG_ALPHA, (u32)overlay.alpha);
}

/*
 * overlay_print_state
 *   Print a single compact status line to UART.
 *   Also echoes the raw register values so you can cross-check with hw.
 */
void overlay_print_state(void)
{
    xil_printf("  pos=(%4d,%4d)  size=%4dx%4d  color=#%02X%02X%02X  alpha=%3d\r\n",
               overlay.x, overlay.y,
               overlay.width, overlay.height,
               overlay.r, overlay.g, overlay.b,
               overlay.alpha);
}

/* -------------------------------------------------------------------- */
/*  Automated demo                                                       */
/* -------------------------------------------------------------------- */

/*
 * demo_delay_ms
 *   Convenience wrapper — TimerDelay takes microseconds.
 */
static void demo_delay_ms(u32 ms)
{
    TimerDelay(ms * 1000u);
}

/*
 * overlay_run_demo
 *   Plays a scripted sequence showing every overlay capability:
 *     1. Reset to a thin centered bar
 *     2. Sweep across colours
 *     3. Pulse alpha (fade in / out)
 *     4. Grow / shrink height (thin bar <-> tall bar)
 *     5. Grow / shrink width
 *     6. Slide up then back down
 *     7. Slide left then back right
 *     8. Restore default state
 *
 *   Each step is printed to UART so the user can follow along.
 *   The demo runs to completion; it cannot be interrupted mid-sequence.
 */
void overlay_run_demo(void)
{
    int i;
    u16 cx;     /* horizontal centre for the bar */

    xil_printf("\r\n--- DEMO START ---\r\n");

    /* ---- 1. Reset to a known starting point ---- */
    xil_printf("[demo] Reset: thin bar, full width, centred\r\n");
    cx = (OVERLAY_SCREEN_W - 1280) / 2;
    overlay_set_size(1280, 60);
    overlay_set_position(cx, (OVERLAY_SCREEN_H - 60) / 2);
    overlay_set_color(255, 255, 255);
    overlay_set_alpha(200);
    demo_delay_ms(600);

    /* ---- 2. Colour sweep ---- */
    xil_printf("[demo] Colour sweep\r\n");
    overlay_set_color(255,   0,   0); demo_delay_ms(400);   /* red     */
    overlay_set_color(255, 128,   0); demo_delay_ms(400);   /* orange  */
    overlay_set_color(255, 255,   0); demo_delay_ms(400);   /* yellow  */
    overlay_set_color(  0, 255,   0); demo_delay_ms(400);   /* green   */
    overlay_set_color(  0, 255, 255); demo_delay_ms(400);   /* cyan    */
    overlay_set_color(  0,   0, 255); demo_delay_ms(400);   /* blue    */
    overlay_set_color(255,   0, 255); demo_delay_ms(400);   /* magenta */
    overlay_set_color(255, 255, 255); demo_delay_ms(400);   /* white   */

    /* ---- 3. Alpha pulse (fade out then in) ---- */
    xil_printf("[demo] Alpha pulse\r\n");
    overlay_set_color(0, 200, 255);
    for (i = 255; i >= 0; i -= 17) {
        overlay_set_alpha((u8)i);
        demo_delay_ms(60);
    }
    overlay_set_alpha(0);
    demo_delay_ms(200);
    for (i = 0; i <= 255; i += 17) {
        overlay_set_alpha((u8)i);
        demo_delay_ms(60);
    }
    overlay_set_alpha(255);
    demo_delay_ms(300);

    /* ---- 4. Height grow / shrink ---- */
    xil_printf("[demo] Height grow/shrink\r\n");
    overlay_set_color(255, 80, 0);
    for (i = 60; i <= 300; i += 20) {
        overlay_set_size(overlay.width, (u16)i);
        /* keep bar vertically centred */
        overlay_set_position(overlay.x, (u16)((OVERLAY_SCREEN_H - i) / 2));
        demo_delay_ms(60);
    }
    demo_delay_ms(300);
    for (i = 300; i >= 60; i -= 20) {
        overlay_set_size(overlay.width, (u16)i);
        overlay_set_position(overlay.x, (u16)((OVERLAY_SCREEN_H - i) / 2));
        demo_delay_ms(60);
    }
    demo_delay_ms(300);

    /* ---- 5. Width shrink / grow ---- */
    xil_printf("[demo] Width shrink/grow\r\n");
    overlay_set_color(0, 220, 100);
    overlay_set_size(1280, 60);
    overlay_set_position((OVERLAY_SCREEN_W - 1280) / 2,
                         (OVERLAY_SCREEN_H - 60) / 2);
    for (i = 1280; i >= 100; i -= 60) {
        overlay_set_size((u16)i, overlay.height);
        /* keep horizontally centred */
        overlay_set_position((u16)((OVERLAY_SCREEN_W - i) / 2), overlay.y);
        demo_delay_ms(60);
    }
    demo_delay_ms(300);
    for (i = 100; i <= 1280; i += 60) {
        overlay_set_size((u16)i, overlay.height);
        overlay_set_position((u16)((OVERLAY_SCREEN_W - i) / 2), overlay.y);
        demo_delay_ms(60);
    }
    demo_delay_ms(300);

    /* ---- 6. Slide up then down ---- */
    xil_printf("[demo] Slide up/down\r\n");
    overlay_set_color(200, 0, 255);
    overlay_set_size(1280, 60);
    overlay_set_position((OVERLAY_SCREEN_W - 1280) / 2,
                         (OVERLAY_SCREEN_H - 60) / 2);
    for (i = (OVERLAY_SCREEN_H - 60) / 2; i >= 0; i -= 15) {
        overlay_set_position(overlay.x, (u16)i);
        demo_delay_ms(40);
    }
    overlay_set_position(overlay.x, 0);
    demo_delay_ms(300);
    for (i = 0; i <= OVERLAY_SCREEN_H - 60; i += 15) {
        overlay_set_position(overlay.x, (u16)i);
        demo_delay_ms(40);
    }
    overlay_set_position(overlay.x, OVERLAY_SCREEN_H - 60);
    demo_delay_ms(300);
    /* return to centre */
    overlay_set_position(overlay.x, (OVERLAY_SCREEN_H - 60) / 2);

    /* ---- 7. Slide left then right ---- */
    xil_printf("[demo] Slide left/right\r\n");
    overlay_set_color(255, 220, 0);
    overlay_set_size(400, 60);
    overlay_set_position(0, (OVERLAY_SCREEN_H - 60) / 2);
    demo_delay_ms(200);
    for (i = 0; i <= OVERLAY_SCREEN_W - 400; i += 20) {
        overlay_set_position((u16)i, overlay.y);
        demo_delay_ms(30);
    }
    overlay_set_position(OVERLAY_SCREEN_W - 400, overlay.y);
    demo_delay_ms(300);
    for (i = OVERLAY_SCREEN_W - 400; i >= 0; i -= 20) {
        overlay_set_position((u16)i, overlay.y);
        demo_delay_ms(30);
    }
    overlay_set_position(0, overlay.y);
    demo_delay_ms(300);

    /* ---- 8. Restore defaults ---- */
    xil_printf("[demo] Restore defaults\r\n");
    overlay_init_defaults();
    overlay_apply_all();
    demo_delay_ms(500);

    xil_printf("--- DEMO END ---\r\n\r\n");
    overlay_print_state();
}

/* -------------------------------------------------------------------- */
/*  Internal help text                                                   */
/* -------------------------------------------------------------------- */

static void overlay_print_help(void)
{
    xil_printf("\x1B[2J\x1B[H"); /* clear screen, cursor home */
    xil_printf("**************************************************\r\n");
    xil_printf("*          Overlay Control Mode                  *\r\n");
    xil_printf("*  (bare-metal UART, 115200 baud)                *\r\n");
    xil_printf("**************************************************\r\n");
    xil_printf("\r\n");
    xil_printf("  --- POSITION ---\r\n");
    xil_printf("  w / s   move up / down   (%d px)\r\n", OVERLAY_MOVE_STEP);
    xil_printf("  a / d   move left / right\r\n");
    xil_printf("\r\n");
    xil_printf("  --- SIZE ---\r\n");
    xil_printf("  i / k   increase / decrease height (%d px)\r\n", OVERLAY_SIZE_STEP);
    xil_printf("  l / j   increase / decrease width\r\n");
    xil_printf("\r\n");
    xil_printf("  --- TRANSPARENCY ---\r\n");
    xil_printf("  t / g   more / less opaque (step %d)\r\n", OVERLAY_ALPHA_STEP);
    xil_printf("\r\n");
    xil_printf("  --- COLOR ---\r\n");
    xil_printf("  r=Red  e=Green  b=Blue\r\n");
    xil_printf("  y=Yellow  c=Cyan  m=Magenta\r\n");
    xil_printf("  0=Black    W=White\r\n");
    xil_printf("\r\n");
    xil_printf("  --- OTHER ---\r\n");
    xil_printf("  p   print current state\r\n");
    xil_printf("  D   run automated demo (move/resize/color/alpha)\r\n");
    xil_printf("  h   show this help screen\r\n");
    xil_printf("  q   exit overlay control mode\r\n");
    xil_printf("\r\n");
    xil_printf("**************************************************\r\n");
    xil_printf("Current state:\r\n");
    overlay_print_state();
    xil_printf("\r\n");
}

/* -------------------------------------------------------------------- */
/*  Key processor                                                        */
/* -------------------------------------------------------------------- */

/*
 * overlay_process_key
 *   Handle a single character from the UART FIFO.
 *   Updates state and writes to hardware registers immediately.
 *   Returns without printing anything — caller decides what to show.
 */
void overlay_process_key(char c)
{
    int nx, ny, nw, nh, na;

    switch (c)
    {
    /* --- position --- */
    case 'w':   /* move up (Y decreases toward top of screen) */
        ny = (int)overlay.y - OVERLAY_MOVE_STEP;
        overlay_set_position(overlay.x, clamp_u16(ny, 0, OVERLAY_SCREEN_H - 1));
        break;

    case 's':   /* move down */
        ny = (int)overlay.y + OVERLAY_MOVE_STEP;
        overlay_set_position(overlay.x, clamp_u16(ny, 0, OVERLAY_SCREEN_H - 1));
        break;

    case 'a':   /* move left */
        nx = (int)overlay.x - OVERLAY_MOVE_STEP;
        overlay_set_position(clamp_u16(nx, 0, OVERLAY_SCREEN_W - 1), overlay.y);
        break;

    case 'd':   /* move right */
        nx = (int)overlay.x + OVERLAY_MOVE_STEP;
        overlay_set_position(clamp_u16(nx, 0, OVERLAY_SCREEN_W - 1), overlay.y);
        break;

    /* --- size --- */
    case 'i':   /* increase height */
        nh = (int)overlay.height + OVERLAY_SIZE_STEP;
        overlay_set_size(overlay.width, clamp_u16(nh, 1, OVERLAY_SCREEN_H));
        break;

    case 'k':   /* decrease height */
        nh = (int)overlay.height - OVERLAY_SIZE_STEP;
        overlay_set_size(overlay.width, clamp_u16(nh, 1, OVERLAY_SCREEN_H));
        break;

    case 'l':   /* increase width */
        nw = (int)overlay.width + OVERLAY_SIZE_STEP;
        overlay_set_size(clamp_u16(nw, 1, OVERLAY_SCREEN_W), overlay.height);
        break;

    case 'j':   /* decrease width */
        nw = (int)overlay.width - OVERLAY_SIZE_STEP;
        overlay_set_size(clamp_u16(nw, 1, OVERLAY_SCREEN_W), overlay.height);
        break;

    /* --- alpha --- */
    case 't':   /* more opaque */
        na = (int)overlay.alpha + OVERLAY_ALPHA_STEP;
        overlay_set_alpha(clamp_u16(na, 0, 255));
        break;

    case 'g':   /* less opaque */
        na = (int)overlay.alpha - OVERLAY_ALPHA_STEP;
        overlay_set_alpha(clamp_u16(na, 0, 255));
        break;

    /* --- colors --- */
    case 'r':   overlay_set_color(255,   0,   0); break;  /* red     */
    case 'e':   overlay_set_color(  0, 255,   0); break;  /* green   */
    case 'b':   overlay_set_color(  0,   0, 255); break;  /* blue    */
    case 'y':   overlay_set_color(255, 255,   0); break;  /* yellow  */
    case 'c':   overlay_set_color(  0, 255, 255); break;  /* cyan    */
    case 'm':   overlay_set_color(255,   0, 255); break;  /* magenta */
    case '0':   overlay_set_color(  0,   0,   0); break;  /* black   */
    case 'W':   overlay_set_color(255, 255, 255); break;  /* white   */

    /* --- info / control --- */
    case 'p':
        overlay_print_state();
        break;

    case 'D':
        overlay_run_demo();
        break;

    case 'h':
        overlay_print_help();
        break;

    case 'q':
        /* handled by caller loop */
        break;

    default:
        /* ignore unknown keys silently */
        break;
    }
}

/* -------------------------------------------------------------------- */
/*  Interactive UART loop                                                */
/* -------------------------------------------------------------------- */

/*
 * overlay_uart_interactive_loop
 *   Blocks until the user presses 'q'.
 *   On each recognized keypress: update hardware, print new state.
 *   On 'h': reprint the full help screen.
 *   On 'p': reprint just the current state.
 *   On 'q': print a farewell line and return to the caller.
 *
 *   UART access matches video_demo.c polling pattern — no interrupts.
 */
void overlay_uart_interactive_loop(void)
{
    char c;

    /* Flush any stale bytes in the receive FIFO */
    while (XUartPs_IsReceiveData(OVERLAY_UART_BASEADDR))
    {
        XUartPs_ReadReg(OVERLAY_UART_BASEADDR, XUARTPS_FIFO_OFFSET);
    }

    overlay_print_help();

    xil_printf("Overlay control active. Press h for help, q to exit.\r\n\r\n");

    while (1)
    {
        /* Polling wait — spin until a byte arrives */
        while (!XUartPs_IsReceiveData(OVERLAY_UART_BASEADDR))
        {}

        c = (char)XUartPs_ReadReg(OVERLAY_UART_BASEADDR, XUARTPS_FIFO_OFFSET);

        if (c == 'q')
        {
            xil_printf("\r\nExiting overlay control mode.\r\n\r\n");
            break;
        }

        if (c == 'h')
        {
            overlay_print_help();
        }
        else if (c == 'p')
        {
            overlay_print_state();
        }
        else if (c == 'D')
        {
            overlay_run_demo();
        }
        else
        {
            overlay_process_key(c);
            /* Print updated state on every change */
            overlay_print_state();
        }
    }
}
