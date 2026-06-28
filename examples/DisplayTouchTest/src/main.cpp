/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * DisplayTouchTest — the standalone proof for guition-jc4880p4-bsp.
 *
 * Brings up the Guition JC4880P443C_I_W (ESP32-P4) ST7701S 480x800 MIPI-DSI
 * panel with the BSP's MANUAL bring-up (NO esp_lcd_st7701 vendor driver), paints
 * 8 vertical colour bars straight into the native RGB565 framebuffer, then polls
 * the GT911 touch and prints native (480x800) coordinates over USB serial.
 *
 * Pure framebuffer path — no LVGL. This is the example that must build green to
 * prove the BSP compiles standalone with zero app coupling.
 *
 * Wiring / pins / DSI timings live in the BSP (../../../src/board_p4.*); the
 * pin overrides are supplied as build_flags in this example's platformio.ini.
 */

#include <Arduino.h>
#include "board_p4.h"

/* 8 vertical colour bars across the 480-wide native panel. */
static const uint16_t kBarColors[8] = {
    0xF800, /* red     */
    0x07E0, /* green   */
    0x001F, /* blue    */
    0xFFE0, /* yellow  */
    0x07FF, /* cyan    */
    0xF81F, /* magenta */
    0xFFFF, /* white   */
    0x0000, /* black   */
};

/* Paint the colour bars once into the back framebuffer, then present it. */
static void draw_color_bars(void) {
  uint16_t *fb = board_p4_get_framebuffer();
  if (fb == NULL) {
    Serial.println("[DisplayTouchTest] framebuffer is NULL — display init failed?");
    return;
  }

  const int w = BOARD_P4_LCD_H_RES; /* 480 */
  const int h = BOARD_P4_LCD_V_RES; /* 800 */
  const int bar_w = w / 8;          /* 60 px per bar */

  /* Write directly into the back FB, row-major RGB565. */
  for (int y = 0; y < h; ++y) {
    uint16_t *row = fb + (size_t)y * w;
    for (int x = 0; x < w; ++x) {
      int bar = x / bar_w;
      if (bar > 7) bar = 7; /* clamp the remainder column */
      row[x] = kBarColors[bar];
    }
  }

  /* Flush the whole frame so the DPI scan-out sees it, then swap it in. */
  board_p4_flush_region(0, 0, w, h, fb);
  board_p4_present();
  Serial.println("[DisplayTouchTest] colour bars drawn.");
}

void setup(void) {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[DisplayTouchTest] Guition JC4880P443 (ESP32-P4) BSP demo");

  /* Bring up the MIPI-DSI panel (LDO -> DSI -> DPI -> ST7701 init -> backlight). */
  esp_err_t err = board_p4_display_init();
  if (err != ESP_OK) {
    Serial.printf("[DisplayTouchTest] board_p4_display_init failed: %d\n", (int)err);
  }

  /* Comfortable brightness (0..255; values are clamped up off zero by the BSP). */
  board_p4_set_brightness(200);

  /* Bring up GT911 touch (probes 0x5D then 0x14 on I2C SDA7/SCL8). */
  err = board_p4_touch_init();
  if (err != ESP_OK) {
    Serial.printf("[DisplayTouchTest] board_p4_touch_init failed: %d\n", (int)err);
  }

  draw_color_bars();
  Serial.println("[DisplayTouchTest] setup done — touch the screen.");
}

void loop(void) {
  static uint32_t last_print_ms = 0;

  int x = 0, y = 0;
  bool pressed = false;
  board_p4_touch_read(&x, &y, &pressed);

  if (pressed) {
    uint32_t now = millis();
    if (now - last_print_ms >= 100) { /* throttle to ~10 Hz */
      Serial.printf("touch %d,%d\n", x, y);
      last_print_ms = now;
    }
  }

  delay(10);
}
