/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * Part of guition-jc4880p4-bsp — board support for the Guition JC4880P443C_I_W
 * (ESP32-P4) 4.3" ST7701S 480x800 MIPI-DSI touch board.
 * https://github.com/ultramcu/guition-jc4880p4-bsp
 *
 * Pin values, DSI timings and the ST7701 init sequence were cross-referenced
 * against elik745i/ESP32-2432S024C-Remote (no license; hardware facts only)
 * and our own on-hardware bring-up. Full HW notes + schematic links:
 * https://github.com/ultramcu/guition-jc4880p443c-i-w
 */
/*
 * board_p4.h — public BSP API for the Guition JC4880P443 (ESP32-P4):
 * ST7701S 480x800 MIPI-DSI panel + GT911 I2C touch.
 *
 * The display path uses the MANUAL bring-up recipe proven on hardware in
 * this BSP's on-hardware bring-up: LDO -> dsi_bus -> dbi -> dpi -> panel
 * reset -> PANEL_INIT_CMDS -> panel_init -> backlight. NO esp_lcd_st7701
 * vendor component. The DPI panel is DOUBLE-buffered (num_fbs=2): LVGL renders
 * the next frame into the off-screen BACK buffer (board_p4_get_framebuffer() /
 * board_p4_flush_region()), then board_p4_present() ping-pongs it to the scanned
 * buffer so the panel only ever shows COMPLETE frames (no mid-redraw tearing).
 *
 * Implemented in C (board_p4.c) so the esp_lcd headers parse in C mode, dodging
 * the C++ overload conflict in esp_lcd_io_i2c.h.
 *
 * Pins / timings / sources: see the sibling repo HW notes (see README).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Native panel geometry (portrait, as the glass is wired) ---- */
#define BOARD_P4_LCD_H_RES   480
#define BOARD_P4_LCD_V_RES   800

/* ---- Pins / LDO channels (defaults; override any with -DBOARD_P4_xxx=...) ----
 * The COMPLETE board GPIO map (display, touch, SD, Wi-Fi C6, audio, UART,
 * RS-485, buttons) lives in board_p4_pins.h. */
#include "board_p4_pins.h"

/* ===========================================================================
 * Display
 * ===========================================================================*/

/**
 * @brief Bring up the MIPI-DSI panel with the manual ST7701S recipe:
 *        acquire DSI-PHY LDO -> dsi_bus (2 lanes, 500Mbps) -> DBI io ->
 *        DPI panel (480x800, RGB565, 1 FB, 34MHz) -> GPIO panel reset ->
 *        PANEL_INIT_CMDS -> esp_lcd_panel_init() -> backlight on.
 * @return ESP_OK on success. Heavy ESP_LOG output on every step.
 */
esp_err_t board_p4_display_init(void);

/** @brief esp_lcd panel handle from board_p4_display_init(), or NULL. */
esp_lcd_panel_handle_t board_p4_get_panel_handle(void);

/**
 * @brief Pointer to the current BACK DPI framebuffer (RGB565, the off-screen one
 *        of the num_fbs=2 pair), H_RES*V_RES uint16_t. NULL until
 *        board_p4_display_init() succeeds. Write directly then flush via
 *        board_p4_flush_region(); the value changes after each board_p4_present()
 *        swap, so re-read it rather than caching it.
 */
uint16_t *board_p4_get_framebuffer(void);

/**
 * @brief Pointer to the CURRENTLY-SCANNED (front) native framebuffer — the real
 *        post-PPA-rotation pixels the DPI is displaying (native 480x800 RGB565,
 *        2 B/px), NOT the off-screen back buffer. Sets *w/*h to the NATIVE panel
 *        dims (480x800). Cache-invalidates (M2C) the frame before returning so
 *        the CPU sees the PPA's DMA output, not stale cache. NULL before init.
 *        Intended for dev tooling (serial framebuffer dump); the image is the
 *        un-rotated native frame, so a host must un-rotate 480x800 -> 800x480.
 */
const uint16_t *board_p4_front_fb(int *w, int *h);

/**
 * @brief Copy an RGB565 source rectangle into the framebuffer at (x,y,w,h)
 *        honouring the FB stride, then esp_cache_msync() just that region so the
 *        DPI scan-out sees it. This is what the LVGL flush_cb calls.
 * @param x,y    top-left of the destination rect (native portrait coords).
 * @param w,h    rect size in pixels.
 * @param src    w*h contiguous RGB565 pixels (row-major).
 */
void board_p4_flush_region(int x, int y, int w, int h, const uint16_t *src);

/**
 * @brief Present the completed back framebuffer: swap it to be the one the DPI
 *        bridge scans out (ping-pong), then make the other buffer the new back
 *        buffer and re-sync it so partial-mode LVGL refreshes stay coherent.
 *        Call this exactly ONCE per LVGL refresh, on the LAST flush part
 *        (lv_disp_flush_is_last()), AFTER all board_p4_flush_region() calls for
 *        that refresh. This is what makes the panel only ever scan COMPLETE
 *        frames (kills the white flash on heavy transitions).
 */
void board_p4_present(void);

/**
 * @brief Present a full LOGICAL (landscape) frame using PPA HARDWARE rotation.
 *        PPA-rotates the @p logical (log_w x log_h, RGB565) frame into the
 *        off-screen BACK native framebuffer (480x800), waits for completion,
 *        then swaps it to be the DPI-scanned buffer. This is the LVGL flush path
 *        when sw_rotate is OFF + full_refresh is ON: LVGL renders un-rotated into
 *        an 800x480 buffer and the PPA does the rotate, so nothing renders into
 *        the scanned FB (kills the DPI-fetch underrun / white flash).
 *        Rotation angle is BOARD_P4_PPA_ROTATE (compile-time; flip 90<->270 if
 *        the image is upside-down on hardware).
 * @param logical  log_w*log_h contiguous RGB565 pixels (row-major, landscape).
 * @param log_w    logical frame width  (800).
 * @param log_h    logical frame height (480).
 */
void board_p4_present_rotated(const uint16_t *logical, int log_w, int log_h);

/** @brief Push an RGB565 bitmap to a rectangle via the panel draw path. */
esp_err_t board_p4_draw_bitmap(int x1, int y1, int x2, int y2, const void *data);

/** @brief Turn the backlight on (full PWM duty) / off (0). No-op if BL pin is -1. */
void board_p4_backlight(bool on);

/**
 * @brief Set backlight brightness via LEDC PWM on BOARD_P4_LCD_BL.
 * @param duty 8-bit PWM duty (0..255). 0 = off; any non-zero value is clamped up
 *             to a ~10% floor so the panel never goes fully black. Lazily inits
 *             the LEDC timer/channel on first call. No-op if BL pin is -1.
 */
void board_p4_set_brightness(uint8_t duty);

/* ===========================================================================
 * Touch (GT911)
 * ===========================================================================*/

/**
 * @brief Bring up I2C + GT911. Probes 0x5D then 0x14 and uses whichever ACKs.
 * @return ESP_OK if a GT911 was found and initialised.
 */
esp_err_t board_p4_touch_init(void);

/** @brief esp_lcd_touch handle from board_p4_touch_init(), or NULL. */
esp_lcd_touch_handle_t board_p4_get_touch_handle(void);

/**
 * @brief Read one touch point.
 * @param x,y      filled with native (portrait 480x800) coords when pressed.
 * @param pressed  set true if a finger is down. May be NULL.
 * @return true if a finger is down (same as *pressed).
 */
bool board_p4_touch_read(int *x, int *y, bool *pressed);

#ifdef __cplusplus
}
#endif
