/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * lvgl_glue.h — minimal, self-contained LVGL 8.3 glue for the Guition
 * JC4880P443 (ESP32-P4) BSP (guition-jc4880p4-bsp).
 *
 * This is a DECOUPLED port of the firmware's board_bringup_p4.c: it keeps only
 * what an LVGL example needs and has NO dependency on the private app's
 * board_bringup.h. It wires:
 *   - an LVGL display driver whose flush_cb hands each full LOGICAL landscape
 *     (800x480) frame to board_p4_present_rotated() (the BSP PPA-rotates it into
 *     the native 480x800 framebuffer),
 *   - a GT911 pointer indev whose read_cb calls board_p4_touch_read() and remaps
 *     the native (480x800 portrait) point into LOGICAL landscape (lx=py; ly=479-px),
 *   - an esp_timer tick source, a FreeRTOS recursive mutex, and a service task
 *     that runs lv_timer_handler() under that mutex.
 *
 * Usage from your sketch:
 *     board_p4_display_init();   // BSP: bring up the ST7701S panel
 *     lvgl_glue_start();         // this: lv_init + drivers + tick + task
 *     lvgl_glue_lock(0);
 *     // ... build your UI with lv_* calls ...
 *     lvgl_glue_unlock();
 *     // the service task drives lv_timer_handler() for you; OR drive it
 *     // yourself in loop() and skip the task (see lvgl_glue_start arg).
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring LVGL up in LOGICAL LANDSCAPE (800x480) mode on top of the BSP.
 *
 * Initializes LVGL, allocates two full-screen 800x480 draw buffers in PSRAM,
 * registers the display + touch drivers, starts the esp_timer tick, and creates
 * the recursive mutex. Call board_p4_display_init() (and optionally
 * board_p4_touch_init(), done here automatically) BEFORE this.
 *
 * @param run_service_task  true  -> spawn a FreeRTOS task that calls
 *                                   lv_timer_handler() under the lock (you do
 *                                   NOT call it yourself).
 *                          false -> you must call lvgl_glue_handler() from your
 *                                   own loop().
 * @return true on success.
 */
bool lvgl_glue_start(bool run_service_task);

/** @brief Take the recursive LVGL mutex. timeout_ms==0 -> wait forever. */
bool lvgl_glue_lock(unsigned int timeout_ms);

/** @brief Release the recursive LVGL mutex. */
void lvgl_glue_unlock(void);

/**
 * @brief Run one lv_timer_handler() pass under the lock and return its
 *        suggested delay in ms. Use ONLY when started with run_service_task=false.
 */
unsigned int lvgl_glue_handler(void);

#ifdef __cplusplus
}
#endif
