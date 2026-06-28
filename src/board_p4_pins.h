/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * board_p4_pins.h — complete GPIO / LDO map for the Guition JC4880P443C_I_W
 * (module JC-ESP32P4-M3, ESP32-P4 + ESP32-C6).
 *
 * Every pin is an overridable #ifndef default — pass -DBOARD_P4_xxx=<gpio> in
 * build_flags if your schematic revision differs. Values are the on-hardware
 * VERIFIED set; ✅ = exercised by this BSP, others are board-documented but not
 * driven by the core (audio / RS-485 / buttons). Full notes + schematic:
 * https://github.com/ultramcu/guition-jc4880p443c-i-w
 */
#pragma once

/* ============================ Display — ST7701S, MIPI-DSI (✅) ============== */
#ifndef BOARD_P4_LCD_RST          /* panel reset (low 20ms, high 120ms). -1 = none. */
#define BOARD_P4_LCD_RST      5
#endif
#ifndef BOARD_P4_LCD_BL           /* backlight, driven by LEDC PWM (5kHz/8-bit). -1 = skip. Feeds the MP3202 boost. */
#define BOARD_P4_LCD_BL       23
#endif
/* DSI is 2 data lanes @ 500 Mbps, DPI 34 MHz (h 12/42/42, v 2/8/166); the lanes
 * are MIPI-DSI PHY pins, not GPIOs. The PHY is powered by the on-chip LDO below. */

/* ============================ On-chip LDO regulator channels ================ */
#ifndef BOARD_P4_DSI_LDO_CHAN     /* DSI-PHY supply (✅) */
#define BOARD_P4_DSI_LDO_CHAN 3
#endif
#ifndef BOARD_P4_DSI_LDO_MV
#define BOARD_P4_DSI_LDO_MV   2500
#endif
#ifndef BOARD_P4_SD_LDO_CHAN      /* TF_VCC supply for the microSD rail (✅; the SD-bring-up hinge) */
#define BOARD_P4_SD_LDO_CHAN  4
#endif
#ifndef BOARD_P4_SD_LDO_MV
#define BOARD_P4_SD_LDO_MV    3300
#endif

/* ============================ Touch — GT911, I2C (✅) ======================= */
#ifndef BOARD_P4_TOUCH_SDA        /* shared I2C bus (also the ES8311 codec) */
#define BOARD_P4_TOUCH_SDA    7
#endif
#ifndef BOARD_P4_TOUCH_SCL
#define BOARD_P4_TOUCH_SCL    8
#endif
#ifndef BOARD_P4_TOUCH_RST        /* GT911 reset. -1 = none. */
#define BOARD_P4_TOUCH_RST    3
#endif
#ifndef BOARD_P4_TOUCH_INT        /* GT911 INT. -1 = poll. (community docs list GPIO21) */
#define BOARD_P4_TOUCH_INT    -1
#endif
#ifndef BOARD_P4_TOUCH_I2C_ADDR   /* GT911 primary address (probe 0x14 as fallback) */
#define BOARD_P4_TOUCH_I2C_ADDR   0x5D
#endif
#ifndef BOARD_P4_TOUCH_I2C_ADDR_ALT
#define BOARD_P4_TOUCH_I2C_ADDR_ALT 0x14
#endif

/* ============================ microSD / TF card — SDMMC 4-bit (✅) ========== */
/* Separate bus from the C6 Wi-Fi SDIO, so SD + Wi-Fi run together.
 * NOTE: GPIO45 (a documented "power enable") is NOT CONNECTED (R10 = NC); power
 * comes from BOARD_P4_SD_LDO_CHAN above, not a GPIO. */
#ifndef BOARD_P4_SD_CLK
#define BOARD_P4_SD_CLK       43
#endif
#ifndef BOARD_P4_SD_CMD            /* HW 5.1k pull-up on board */
#define BOARD_P4_SD_CMD       44
#endif
#ifndef BOARD_P4_SD_D0
#define BOARD_P4_SD_D0        39
#endif
#ifndef BOARD_P4_SD_D1
#define BOARD_P4_SD_D1        40
#endif
#ifndef BOARD_P4_SD_D2
#define BOARD_P4_SD_D2        41
#endif
#ifndef BOARD_P4_SD_D3
#define BOARD_P4_SD_D3        42
#endif

/* ============================ Wi-Fi/BT — ESP32-C6 over ESP-Hosted/SDIO (✅) = */
#ifndef BOARD_P4_WIFI_SDIO_CLK
#define BOARD_P4_WIFI_SDIO_CLK 18
#endif
#ifndef BOARD_P4_WIFI_SDIO_CMD
#define BOARD_P4_WIFI_SDIO_CMD 19
#endif
#ifndef BOARD_P4_WIFI_SDIO_D0
#define BOARD_P4_WIFI_SDIO_D0  14
#endif
#ifndef BOARD_P4_WIFI_SDIO_D1
#define BOARD_P4_WIFI_SDIO_D1  15
#endif
#ifndef BOARD_P4_WIFI_SDIO_D2
#define BOARD_P4_WIFI_SDIO_D2  16
#endif
#ifndef BOARD_P4_WIFI_SDIO_D3
#define BOARD_P4_WIFI_SDIO_D3  17
#endif
#ifndef BOARD_P4_C6_RESET         /* P4 drives the C6 reset line */
#define BOARD_P4_C6_RESET     54
#endif

/* ============================ Audio — ES8311 codec + NS4150 amp ============= */
/* I2S; codec control I2C is shared with the touch bus (SDA7/SCL8). Not driven
 * by the core BSP, defined here for completeness. */
#ifndef BOARD_P4_I2S_MCLK
#define BOARD_P4_I2S_MCLK     13
#endif
#ifndef BOARD_P4_I2S_BCLK
#define BOARD_P4_I2S_BCLK     12
#endif
#ifndef BOARD_P4_I2S_LRCK
#define BOARD_P4_I2S_LRCK     10
#endif
#ifndef BOARD_P4_I2S_DOUT         /* P4 -> codec */
#define BOARD_P4_I2S_DOUT     9
#endif
#ifndef BOARD_P4_I2S_DIN          /* codec -> P4 */
#define BOARD_P4_I2S_DIN      48
#endif
#ifndef BOARD_P4_AMP_PA_EN        /* NS4150 power-amp enable */
#define BOARD_P4_AMP_PA_EN    11
#endif

/* ============================ Buttons / LED / UART / RS-485 ================= */
#ifndef BOARD_P4_BOOT_BTN
#define BOARD_P4_BOOT_BTN     35
#endif
#ifndef BOARD_P4_LED              /* ⚠️ also RS-485 UART1-TX in the RS-485 build — pick one use */
#define BOARD_P4_LED          26
#endif
#ifndef BOARD_P4_UART0_TX         /* USB-serial console */
#define BOARD_P4_UART0_TX     37
#endif
#ifndef BOARD_P4_UART0_RX
#define BOARD_P4_UART0_RX     38
#endif
#ifndef BOARD_P4_RS485_TX         /* MAX485 (UART1) — shares GPIO26 with the LED */
#define BOARD_P4_RS485_TX     26
#endif
#ifndef BOARD_P4_RS485_RX
#define BOARD_P4_RS485_RX     27
#endif
