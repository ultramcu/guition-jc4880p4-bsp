/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * lvgl_glue.c — decoupled LVGL 8.3 glue for guition-jc4880p4-bsp.
 *
 * Ported from the firmware shim board_bringup_p4.c, with the private app
 * dependency (board_bringup.h) REMOVED and only the example-relevant pieces
 * inlined: the display flush_cb / touch read_cb / tick callback, the recursive
 * LVGL mutex, and the lv_timer_handler service task. Brightness, the direct
 * RGB565 push, the non-LVGL touch helper and the Wi-Fi-quiesce hook from the
 * original shim are intentionally dropped — they were app-only.
 *
 * Geometry (see the BSP README / board_p4.h):
 *   - NATIVE glass (portrait):   480 x 800  (BOARD_P4_LCD_H_RES / _V_RES)
 *   - LOGICAL (what LVGL sees):   800 x 480  (landscape)
 * LVGL renders UN-rotated at 800x480; board_p4_present_rotated() PPA-rotates each
 * full frame 270deg into the native FB. sw_rotate is OFF, full_refresh is ON.
 */

#include "lvgl_glue.h"
#include "board_p4.h"

#include "lvgl.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "lvgl_glue";

/* Panel geometry. */
#define PANEL_NATIVE_W      BOARD_P4_LCD_H_RES   /* 480 */
#define PANEL_NATIVE_H      BOARD_P4_LCD_V_RES   /* 800 */
#define LV_HOR              PANEL_NATIVE_H        /* logical landscape width  = 800 */
#define LV_VER              PANEL_NATIVE_W        /* logical landscape height = 480 */
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_STACK     8192
#define LVGL_TASK_PRIO      2
#define LVGL_TASK_CORE      0

/* --- LVGL plumbing (static, single display) --- */
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_color_t        *s_buf1 = NULL;
static lv_color_t        *s_buf2 = NULL;
static lv_indev_drv_t     s_indev_drv;
static esp_timer_handle_t s_tick_timer = NULL;

/* --- LVGL access serialization (recursive so nested lock() is safe) --- */
static SemaphoreHandle_t  s_lvgl_mutex = NULL;
static TaskHandle_t       s_lvgl_task  = NULL;
static bool               s_started    = false;

/* ===================================================================== */
/* LVGL callbacks                                                        */
/* ===================================================================== */

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    /* full_refresh=1 + sw_rotate=0: LVGL hands us ONE flush per frame covering
     * the WHOLE logical 800x480 buffer, rendered un-rotated. The PPA rotates the
     * entire frame in hardware into the off-screen native FB and swaps it in, so
     * nothing renders into the scanned FB (no DPI-fetch underrun / white flash).
     * lv_color_t is RGB565 (LV_COLOR_DEPTH=16, LV_COLOR_16_SWAP=0). */
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    board_p4_present_rotated((const uint16_t *)color_p, w, h);
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    int  px = 0, py = 0;          /* RAW native-panel coords: px:0..480, py:0..800 */
    bool pressed = false;
    board_p4_touch_read(&px, &py, &pressed);

    if (pressed) {
        /* sw_rotate is OFF, so we map raw NATIVE coords (480x800 portrait) into
         * LOGICAL landscape (800x480) with the SAME rotation the PPA applies to
         * the image (270 CCW). Inverse of a 270 CCW display rotation:
         *     lx = py
         *     ly = (PANEL_NATIVE_W - 1) - px
         * If the display is flipped to ROT_90 instead, use the conjugate:
         *     lx = (PANEL_NATIVE_H - 1) - py ; ly = px
         * Touch handedness is verified on hardware; mirror an axis if reversed. */
        if (px < 0) px = 0; else if (px >= PANEL_NATIVE_W) px = PANEL_NATIVE_W - 1;
        if (py < 0) py = 0; else if (py >= PANEL_NATIVE_H) py = PANEL_NATIVE_H - 1;

        int lx = py;
        int ly = (PANEL_NATIVE_W - 1) - px;

        if (lx < 0) lx = 0; else if (lx >= LV_HOR) lx = LV_HOR - 1;
        if (ly < 0) ly = 0; else if (ly >= LV_VER) ly = LV_VER - 1;
        data->point.x = lx;
        data->point.y = ly;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ===================================================================== */
/* LVGL service task: run lv_timer_handler() under the lock.             */
/* ===================================================================== */
static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t delay_ms = 5;
        if (lvgl_glue_lock(0)) {
            delay_ms = lv_timer_handler();
            lvgl_glue_unlock();
        }
        if (delay_ms < 2)   delay_ms = 2;
        if (delay_ms > 100) delay_ms = 100;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ===================================================================== */
/* Public API                                                            */
/* ===================================================================== */

bool lvgl_glue_lock(unsigned int timeout_ms)
{
    if (!s_lvgl_mutex) return false;
    const TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY
                                               : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, ticks) == pdTRUE;
}

void lvgl_glue_unlock(void)
{
    if (s_lvgl_mutex) xSemaphoreGiveRecursive(s_lvgl_mutex);
}

unsigned int lvgl_glue_handler(void)
{
    unsigned int delay_ms = 5;
    if (lvgl_glue_lock(0)) {
        delay_ms = lv_timer_handler();
        lvgl_glue_unlock();
    }
    return delay_ms;
}

bool lvgl_glue_start(bool run_service_task)
{
    if (s_started) return true;

    /* Touch (panel is brought up by the caller via board_p4_display_init). */
    if (board_p4_touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "board_p4_touch_init failed — UI shows but touch is dead");
    }

    /* 1. LVGL core. */
    lv_init();

    /* 2. TWO FULL-SCREEN logical (800x480) draw buffers in PSRAM: LVGL renders
     *    frame N+1 into buffer B while the PPA still consumes buffer A.
     *    full_refresh needs a full-frame buffer; double-buffering keeps render
     *    and PPA-rotate pipelined. 2 x 800*480*2B = 1.5MB. */
    const size_t buf_px = (size_t)LV_HOR * LV_VER;
    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "draw buffer alloc failed");
        return false;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_px);

    /* 3. Display driver — registered at LOGICAL LANDSCAPE 800x480. The UI renders
     *    un-rotated; the PPA rotates each full frame into the native FB. NO
     *    sw_rotate, full_refresh=1 so flush_cb gets the whole frame in one shot. */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res      = LV_HOR;   /* 800 */
    s_disp_drv.ver_res      = LV_VER;   /* 480 */
    s_disp_drv.flush_cb     = disp_flush_cb;
    s_disp_drv.draw_buf     = &s_draw_buf;
    s_disp_drv.sw_rotate    = 0;        /* PPA does the rotation in hardware */
    s_disp_drv.full_refresh = 1;        /* whole frame per flush -> one PPA op */
    lv_disp_drv_register(&s_disp_drv);

    /* 4. Touch input device. */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    /* 5. Tick source: esp_timer periodic. */
    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_create(&tick_args, &s_tick_timer);
    esp_timer_start_periodic(s_tick_timer, LVGL_TICK_PERIOD_MS * 1000);

    /* 6. Recursive mutex (+ optional service task). */
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "LVGL mutex create failed");
        return false;
    }
    if (run_service_task) {
        xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL,
                                LVGL_TASK_PRIO, &s_lvgl_task, LVGL_TASK_CORE);
    }

    /* 7. Backlight on. */
    board_p4_backlight(true);

    s_started = true;
    ESP_LOGI(TAG, "lvgl_glue_start OK (LVGL logical %dx%d landscape, PPA HW rotate)",
             LV_HOR, LV_VER);
    return true;
}
