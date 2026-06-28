/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * LvglUi — minimal LVGL 8.3 example for the Guition JC4880P443 (ESP32-P4)
 * using guition-jc4880p4-bsp.
 *
 * Brings up the ST7701S 480x800 MIPI-DSI panel via the BSP, wires LVGL through
 * the decoupled glue (lvgl_glue.{c,h}) — which renders un-rotated to a logical
 * 800x480 landscape buffer and PPA-rotates each frame into the native FB — then
 * builds a tiny UI: a centered label "JC4880P4 BSP" and a button that logs each
 * press over Serial.
 *
 * The glue's service task drives lv_timer_handler(); loop() just idles. Any UI
 * mutation from another context MUST be wrapped in lvgl_glue_lock()/unlock().
 */

#include <Arduino.h>
#include "lvgl.h"

#include "board_p4.h"
#include "lvgl_glue.h"

static uint32_t s_press_count = 0;

/* Button click handler — logs each press. */
static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    s_press_count++;
    Serial.printf("[LvglUi] button pressed (#%lu)\n", (unsigned long)s_press_count);
    if (label) {
        lv_label_set_text_fmt(label, "Pressed: %lu", (unsigned long)s_press_count);
    }
}

/* Build the demo UI. Caller must hold the LVGL lock. */
static void build_ui(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* Title label, centered toward the top. */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "JC4880P4 BSP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    /* Press-count label below the button. */
    lv_obj_t *count_label = lv_label_create(scr);
    lv_label_set_text(count_label, "Pressed: 0");
    lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 70);

    /* A button that logs presses. */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 70);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, count_label);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap me");
    lv_obj_center(btn_label);
}

void setup(void)
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[LvglUi] Guition JC4880P443 (ESP32-P4) LVGL example");

    /* 1. BSP: bring up the ST7701S MIPI-DSI panel. */
    if (board_p4_display_init() != ESP_OK) {
        Serial.println("[LvglUi] board_p4_display_init FAILED");
        return;
    }

    /* 2. LVGL + drivers + tick + service task (touch init is done inside). */
    if (!lvgl_glue_start(true /* run service task */)) {
        Serial.println("[LvglUi] lvgl_glue_start FAILED");
        return;
    }

    /* 3. Build the UI under the lock (the service task may already be running). */
    if (lvgl_glue_lock(0)) {
        build_ui();
        lvgl_glue_unlock();
    }

    Serial.println("[LvglUi] ready — tap the button");
}

void loop(void)
{
    /* The glue's service task drives lv_timer_handler(); nothing to do here. */
    delay(1000);
}
