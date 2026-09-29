/*
 * display_hal.h — Display Hardware Abstraction Layer
 * ===================================================
 * Purpose: Single point of display configuration. Change ONE file to port
 *          to any supported panel. All display-specific constants flow from
 *          board.h and this header. App code NEVER touches panel internals.
 *
 * Supported panels (set LAUNCHER_DISPLAY_DRIVER in board.h):
 *   LAUNCHER_PANEL_ST7735  — 160x128, 40MHz SPI, no inversion
 *   LAUNCHER_PANEL_ILI9341 — 320x240, 80MHz SPI, no inversion
 *   LAUNCHER_PANEL_ST7789  — 240x240/320, 80MHz SPI, inversion=true
 *
 * LAUNCHER_DISPLAY_DRIVER must be one of the LAUNCHER_PANEL_* tokens above —
 * it is compared with #if, so a string literal will not compile.
 *
 * To port to a new panel:
 *   1. Update board.h: LAUNCHER_DISPLAY_DRIVER, _W, _H, pin assignments
 *   2. Add panel class to LGFX_Config.h (copy existing pattern)
 *   3. Rebuild — everything else adapts automatically
 */
#pragma once

#include "../platform/board.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Derived display constants (computed from board.h) ──────────────────── */
#define DISPLAY_PIXEL_COUNT     (LAUNCHER_DISPLAY_W * LAUNCHER_DISPLAY_H)
#define DISPLAY_FB_SIZE_BYTES   (DISPLAY_PIXEL_COUNT * 2)  /* RGB565 = 2 bytes/pixel */
#define DISPLAY_ASPECT_RATIO_W  (LAUNCHER_DISPLAY_W)
#define DISPLAY_ASPECT_RATIO_H  (LAUNCHER_DISPLAY_H)

/* ── Proportional layout helpers (resolution-independent) ───────────────── */
/* Use these in render() to compute positions that scale to any resolution.  */
#define DISPLAY_MARGIN(w)       ((w) / 20)           /* 5% margin             */
#define DISPLAY_HEADER_H(h)     ((h) / 9)            /* ~11% header height    */
#define DISPLAY_FOOTER_Y(h)     ((h) - 3)            /* 3px from bottom       */
#define DISPLAY_CENTER_X(w)     ((w) / 2)
#define DISPLAY_CENTER_Y(h)     ((h) / 2)

/* ── Panel-specific SPI frequency ───────────────────────────────────────── */
#if defined(LAUNCHER_DISPLAY_DRIVER) && \
    (LAUNCHER_DISPLAY_DRIVER == LAUNCHER_PANEL_ILI9341 || \
     LAUNCHER_DISPLAY_DRIVER == LAUNCHER_PANEL_ST7789)
/* ILI9341 / ST7789: 80MHz. */
#define DISPLAY_SPI_FREQ_HZ     80000000
#elif defined(LAUNCHER_DISPLAY_DRIVER) && (LAUNCHER_DISPLAY_DRIVER == LAUNCHER_PANEL_ST7735)
/* ST7735S: 40MHz (reliable limit) */
#define DISPLAY_SPI_FREQ_HZ     40000000
#else
#error "LAUNCHER_DISPLAY_DRIVER in board.h is not a recognised LAUNCHER_PANEL_* token"
#endif

/* ── Performance budgets ────────────────────────────────────────────────── */
#define DISPLAY_FRAME_TIME_MS   25       /* 40fps target                      */
#define DISPLAY_FRAME_TIME_US   (DISPLAY_FRAME_TIME_MS * 1000)

/* ── Canvas memory requirement check ────────────────────────────────────── */
/* At 320x240: 150KB canvas + 2x150KB transitions = 450KB PSRAM             */
/* At 160x128: 40KB canvas + 2x40KB transitions = 120KB PSRAM               */
/* Both are well within 8MB PSRAM budget                                     */
#define DISPLAY_CANVAS_PSRAM_KB (DISPLAY_FB_SIZE_BYTES / 1024)
#define DISPLAY_TRANSITION_PSRAM_KB (DISPLAY_CANVAS_PSRAM_KB * 3)

#ifdef __cplusplus
}
#endif
