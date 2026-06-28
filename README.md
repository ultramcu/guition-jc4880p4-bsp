# guition-jc4880p4-bsp

**Board support for the Guition JC4880P443C_I_W (ESP32-P4 + ESP32-C6) 4.3″ MIPI-DSI touch board.**

Bring the panel up in minutes instead of fighting the same walls everyone hits: a **black screen** with the stock ST7701 driver, **Wi-Fi that associates then drops**, an **SD card that won't mount**, and the **screen flashing white** during OTA. This library packages the bring-up recipe we proved on real hardware as a clean, MIT-licensed Arduino / PlatformIO library — `lib_deps` it and go.

> The deep field notes, the full pinout, schematic links and the C6 Wi-Fi fix walkthrough live in the sibling repo: **[ultramcu/guition-jc4880p443c-i-w](https://github.com/ultramcu/guition-jc4880p443c-i-w)**.

---

## What it is

The **Guition JC4880P443C_I_W** (module **JC-ESP32P4-M3**) is a 4.3″ touch-display board built around:

- **ESP32-P4** — RISC-V dual-core @ 400 MHz, 768 KB L2MEM
- **ESP32-C6** Wi-Fi/BT co-processor over **ESP-HOSTED (SDIO)**
- **32 MB QSPI PSRAM @ 200 MHz**, **16 MB QIO flash**
- **4.3″ ST7701S 480×800 IPS MIPI-DSI** panel (2-lane DSI)
- **GT911** I²C capacitive touch
- **ES8311** audio codec + NS4150 speaker amp
- **IP5306** Li-ion charger (single-cell 3.7 V, 2-pin CN4 BAT+/BAT-, ~MX1.25 1.25 mm)
- **microSD via SDMMC** (4-bit), dual USB-C, 26-pin 2.54 mm GPIO header, RS-485

A `_Y` variant ships with a plastic case.

The board arrives with almost no English documentation, and the community keeps hitting the same problems. This BSP solves them:

- **Black screen** — the stock `esp_lcd_st7701` component does not light this panel. The library does a **manual ST7701S bring-up** (LDO → DSI → DPI → reset → init table → backlight) that works.
- **Landscape rendering** — hardware **PPA 270° rotate** turns a logical 800×480 frame into the native 480×800 framebuffer, with double-buffering so the panel only ever scans complete frames.
- **SD won't mount** (`0x107` timeout) — the library powers the TF_VCC rail via the on-chip LDO so SDMMC actually comes up.
- **Wi-Fi drops / `ASSOC_LEAVE` loop** — documented C6 ESP-Hosted firmware fix (see [Gotchas](#hard-won-gotchas)).
- **White flash on OTA** — root-caused as a DPI underrun, with the known mitigations documented.

The display/touch core is **framework-agnostic** (pure ESP-IDF `esp_lcd` + `esp_driver_ppa` + GT911) — **no LVGL, no app coupling** — so you can drive a raw framebuffer or layer LVGL on top.

> ⚠️ **Engineering-sample chip.** This board's P4 is **rev v1.3 (engineering sample)**. The board JSON **must** use `"chip_variant": "esp32p4_es"` and the platform **must** be pinned to **pioarduino 55.03.36-1**, or you get *Illegal instruction* at the 2nd-stage bootloader. See [Gotchas](#hard-won-gotchas).

---

## Quick start

### 1. Wire up the library

```ini
[platformio]
; the board definition (chip_variant esp32p4_es) lives in this repo's boards/ dir
boards_dir = boards               ; in an example: ../../boards

[env:jc4880p4-example]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.36-1/platform-espressif32.zip
board = jc4880p4
framework = arduino
board_build.flash_size = 16MB
board_build.flash_mode = qio
board_build.partitions = default_16MB.csv
lib_deps =
    https://github.com/ultramcu/guition-jc4880p4-bsp.git
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCONFIG_ESP_LCD_TOUCH_MAX_POINTS=5
```

> **Two wiring notes that trip everyone up:**
> - **`boards_dir` must be in the `[platformio]` section, not `[env]`** — PlatformIO ignores it under `[env]` and you get `UnknownBoard: jc4880p4`.
> - **The GT911 touch driver (`esp_lcd_touch_gt911`) is an ESP-IDF component, *not* a PlatformIO/Arduino registry library**, so a bare `lib_deps = esp_lcd_touch_gt911` won't resolve. The core (`board_p4.c`) `#include`s it, so you must provide it one of two ways: **(a)** vendor it into your project's `lib/` — both examples bundle a ready copy at [`examples/DisplayTouchTest/lib/esp_lcd_touch_gt911/`](examples/DisplayTouchTest/lib/) (Apache-2.0, copy it into your own `lib/`); or **(b)** for an ESP-IDF build, `idf.py add-dependency "espressif/esp_lcd_touch_gt911"`.

### 2a. Raw framebuffer (no LVGL)

The minimal path: init the display, write RGB565 pixels, present.

```cpp
#include "board_p4.h"

void setup() {
  board_p4_display_init();   // DSI + ST7701S + backlight; allocates the 2 PSRAM framebuffers
  board_p4_touch_init();     // GT911 on I2C SDA7 / SCL8

  // (a) Write the native back framebuffer directly (portrait 480x800):
  uint16_t *fb = board_p4_get_framebuffer();
  for (int i = 0; i < BOARD_P4_LCD_H_RES * BOARD_P4_LCD_V_RES; i++)
    fb[i] = 0xF800;                 // fill red (RGB565)
  board_p4_flush_region(0, 0, BOARD_P4_LCD_H_RES, BOARD_P4_LCD_V_RES, fb);
  board_p4_present();               // swap the back FB to be scanned

  // (b) Or present a landscape (800x480) frame via hardware PPA 270° rotate:
  static uint16_t logical[800 * 480];
  // ... draw into `logical` un-rotated ...
  board_p4_present_rotated(logical, 800, 480);
}

void loop() {
  int x, y; bool pressed;
  if (board_p4_touch_read(&x, &y, &pressed))
    printf("touch native %d,%d\n", x, y);   // raw portrait 480x800 coords
}
```

See **[`examples/DisplayTouchTest`](examples/DisplayTouchTest/)** for a full color-bar + GT911 touch-readout sketch with no LVGL — the standalone proof that the library compiles and runs on its own.

### 2b. LVGL

LVGL renders un-rotated into a logical 800×480 PSRAM buffer; `board_p4_present_rotated()` does the rotate into the native framebuffer. See **[`examples/LvglUi`](examples/LvglUi/)** for the LVGL 8.3 glue (display flush callback → `board_p4_present_rotated`, touch read callback, and the bundled `lv_conf.h`).

> For LVGL, return **raw** panel coords from the touch indev — LVGL rotates the point itself. Pre-rotating double-rotates and kills touch.

> **Status:** both examples compile green on the ES chip. `examples/DisplayTouchTest` is the minimal LVGL-free proof; `examples/LvglUi` bundles the GT911 driver + `lv_conf.h` and pulls `lvgl@~8.3.0`.

---

## API reference

All functions are declared in [`src/board_p4.h`](src/board_p4.h). The core is C (`board_p4.c`) so the `esp_lcd` headers parse in C mode.

### Display

| Function | Behavior |
|---|---|
| `esp_err_t board_p4_display_init(void)` | Manual ST7701S bring-up: acquire DSI-PHY LDO ch3 → DSI bus (2 lanes, 500 Mbps) → DBI IO → DPI panel (480×800, RGB565, 34 MHz) → GPIO5 reset → init table → `esp_lcd_panel_init()` → backlight on. Allocates the two PSRAM framebuffers. Returns `ESP_OK` on success. |
| `esp_lcd_panel_handle_t board_p4_get_panel_handle(void)` | The `esp_lcd` panel handle from init, or `NULL`. |
| `uint16_t *board_p4_get_framebuffer(void)` | Pointer to the current **back** framebuffer (off-screen, RGB565, native 480×800). `NULL` before init. The value changes after each `board_p4_present()` swap — re-read it, don't cache it. |
| `const uint16_t *board_p4_front_fb(int *w, int *h)` | Pointer to the currently-scanned (front) native framebuffer (post-PPA-rotation pixels, 480×800 RGB565). Sets `*w`/`*h` to native dims and cache-invalidates the frame first. For dev tooling (serial framebuffer dump); image is un-rotated. `NULL` before init. |
| `void board_p4_flush_region(int x, int y, int w, int h, const uint16_t *src)` | Copy a `w×h` RGB565 source rect into the back framebuffer at `(x,y)` honoring stride, then `esp_cache_msync()` just that region so the DPI scan-out sees it. This is what the LVGL flush callback calls. |
| `void board_p4_present(void)` | Swap the completed back framebuffer to be the one the DPI scans (ping-pong), then re-sync the new back buffer. Call exactly once per refresh, after all `flush_region` calls. Makes the panel only ever scan complete frames (kills the white flash on heavy transitions). |
| `void board_p4_present_rotated(const uint16_t *logical, int log_w, int log_h)` | PPA **hardware 270° rotate** a logical (landscape, `log_w×log_h`, RGB565) frame into the native back framebuffer (480×800), wait for completion, then swap. The LVGL landscape flush path. `log_w`=800, `log_h`=480. |
| `esp_err_t board_p4_draw_bitmap(int x1, int y1, int x2, int y2, const void *data)` | Push an RGB565 bitmap to a rectangle via the panel draw path. |
| `void board_p4_backlight(bool on)` | Backlight full on / off. No-op if the BL pin is `-1`. |
| `void board_p4_set_brightness(uint8_t duty)` | Set backlight brightness via LEDC PWM on GPIO23 (5 kHz / 8-bit). `duty` 0..255; `0` = off, non-zero is clamped up to a ~10% floor so the panel never goes fully black. Lazily inits the LEDC timer/channel on first call. No-op if the BL pin is `-1`. |

### Touch (GT911)

| Function | Behavior |
|---|---|
| `esp_err_t board_p4_touch_init(void)` | Bring up I²C + GT911. Probes address `0x5D` then `0x14` and uses whichever ACKs. Returns `ESP_OK` if a GT911 was found and initialised. |
| `esp_lcd_touch_handle_t board_p4_get_touch_handle(void)` | The `esp_lcd_touch` handle from init, or `NULL`. |
| `bool board_p4_touch_read(int *x, int *y, bool *pressed)` | Read one touch point. `x`/`y` filled with native (portrait 480×800) coords when pressed; `pressed` set true if a finger is down (may be `NULL`). Returns true if a finger is down. |

### Build-time constants (`board_p4.h`)

| Macro | Default | Meaning |
|---|---|---|
| `BOARD_P4_LCD_H_RES` / `BOARD_P4_LCD_V_RES` | 480 / 800 | Native portrait panel geometry. |
| `BOARD_P4_LCD_RST` | 5 | Panel reset GPIO (`-1` = none). |
| `BOARD_P4_LCD_BL` | 23 | Backlight GPIO (`-1` = skip). |
| `BOARD_P4_TOUCH_SDA` / `BOARD_P4_TOUCH_SCL` | 7 / 8 | GT911 I²C pins. |
| `BOARD_P4_TOUCH_RST` | 3 | GT911 reset (`-1` = none). |
| `BOARD_P4_TOUCH_INT` | -1 | GT911 INT (`-1` = poll). |
| `BOARD_P4_DSI_LDO_CHAN` / `BOARD_P4_DSI_LDO_MV` | 3 / 2500 | DSI-PHY on-chip LDO channel / millivolts. |

Override any pin at build time (`-DBOARD_P4_LCD_RST=...`) if your unit's schematic disagrees.

---

## Pinout summary

The values the firmware actually drives, verified on hardware:

| Block | Signal | GPIO |
|---|---|---|
| **Display (ST7701S DSI)** | LCD reset | **GPIO5** |
| | Backlight enable (drives MP3202 boost) | **GPIO23** |
| | DSI-PHY power | on-chip **LDO ch3 @ 2500 mV** |
| | DSI lanes | 2 lanes @ 500 Mbps, DPI 34 MHz |
| **Touch (GT911, I²C)** | SDA / SCL (shared with audio codec) | **GPIO7 / GPIO8** |
| | RST | **GPIO3** |
| | INT | not used (`-1`) |
| | I²C address | **0x5D** (probe `0x14`) |
| **microSD (SDMMC 4-bit)** | CLK / CMD | **GPIO43 / GPIO44** |
| | D0 / D1 / D2 / D3 | **GPIO39 / 40 / 41 / 42** |
| | TF_VCC power | on-chip **LDO ch4 @ 3300 mV** |
| **Wi-Fi (C6, ESP-Hosted SDIO)** | CLK / CMD | GPIO18 / GPIO19 |
| | D0–D3 | GPIO14 / 15 / 16 / 17 |
| | C6 reset | GPIO54 |
| **Battery** | IP5306 charger | 2-pin **CN4** (BAT+/BAT-) |

SD (SDMMC) and Wi-Fi (C6 SDIO) are **separate buses**, so they coexist.

> **Every board pin is defined as an overridable `BOARD_P4_*` macro in [`src/board_p4_pins.h`](src/board_p4_pins.h)** — display, touch, SD (SDMMC), Wi-Fi C6 (SDIO), audio (ES8311 I²S), UART, RS-485, buttons/LED, and the on-chip LDO channels. Override any with `-DBOARD_P4_xxx=<gpio>`. The **schematic sheets** + deep field notes live in the sibling repo — see **[ultramcu/guition-jc4880p443c-i-w](https://github.com/ultramcu/guition-jc4880p443c-i-w)**.

---

## Hard-won gotchas

Each is summarized here; the sibling repo has the full depth.

### 1. Engineering-sample chip → `esp32p4_es` + pinned platform

This board's P4 is **rev v1.3 = engineering sample**. The board JSON **must** use `"chip_variant": "esp32p4_es"` (plain `esp32p4` is production → *Illegal instruction* at the 2nd-stage bootloader), and the platform **must** be pinned to **pioarduino 55.03.36-1** (55.03.35/37 bootloop the P4; newer releases may drop ES support). The shipped `boards/jc4880p4.json` is already correct.

### 2. Display — do NOT use `esp_lcd_st7701` (black screen)

The stock `esp_lcd_st7701` vendor component leaves the panel black. This library does a **manual bring-up chain**:

> LDO ch3 @ 2500 mV → DSI bus (2-lane / 500 Mbps) → DPI panel (480×800 RGB565, 34 MHz) → reset on GPIO5 → ST7701S init table → backlight on GPIO23.

**Landscape** is done with hardware **PPA 270° rotate**: LVGL (or your code) renders un-rotated into a logical 800×480 PSRAM buffer, and `board_p4_present_rotated()` PPA-rotates it into the native 480×800 framebuffer. The DPI panel is **double-buffered** (`num_fbs=2`) and writes are `esp_cache_msync()`'d, so nothing renders directly into the scanned buffer — that's what kills the white flash on heavy transitions. **Touch remap** for landscape: `lx = py; ly = 479 - px`.

### 3. SD (SDMMC, not SPI) — power the TF_VCC rail first

Pins: CLK GPIO43 / CMD GPIO44 / D0–D3 = GPIO39–42. The classic symptom is `sdmmc_init_ocr: send_op_cond returned 0x107` (timeout) — that means **no power on TF_VCC**. Fix: acquire the on-chip **LDO channel 4 @ 3300 mV** at init, then mount with `SD_MMC.setPowerChannel(-1)` so SD_MMC does **not** grab its own LDO (the default channel collides → "already in use / not adjustable"). GPIO45 ("TF power enable") is a **red herring** — R10 is unpopulated (NC), so it does nothing.

### 4. Wi-Fi via C6 — the #1 community pain

The board ships the C6 with **ESP-Hosted slave firmware 2.3.0**, but arduino-esp32 3.3.6's host expects **~2.12** → `Version mismatch` → SDIO drop / `ASSOC_LEAVE` / restart loop (0/13 connects).

**Fix: flash the C6 to `network_adapter_esp32c6.bin` 2.12.9** ([esphome.github.io/esp-hosted-firmware](https://esphome.github.io/esp-hosted-firmware/)) over **direct UART** on header **JP1** (TX→C6_U0RXD, RX→C6_U0TXD, GND).

> **CRITICAL: halt the P4 first**, or the running app keeps resetting the C6 (via GPIO54) mid-flash:
> ```
> esptool --chip esp32p4 --before default_reset --after no_reset flash_id
> esptool --chip esp32c6 write_flash 0x10000 network_adapter_esp32c6.bin
> esptool --chip esp32c6 erase_region 0xd000 0x2000      # otadata → boots ota_0
> ```

After this, Wi-Fi is rock solid and the host-side 8 s-timeout / direct-connect hacks become unnecessary.

### 5. White flash / OTA flicker = DPI underrun (not rendering)

Any **flash or NVS write disables the cache**. On the P4 the PSRAM shares that cache, so the MIPI-DSI framebuffer DMA stalls → **DPI underrun** → a light-blue/white frame. During OTA it flashes ~150× (one per 4 KB sector).

- **Keep flash/NVS writes off hot paths** while the DPI is live. A `disp_off` / backlight-blank during the write is the pragmatic mitigation.
- The real fix is `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` (keeps the cache alive during flash writes) — but enabling `custom_sdkconfig` flips pioarduino into IDF/CMake mode, which breaks PlatformIO library resolution (`lvgl.h not found`) and risks [esp-idf#17855](https://github.com/espressif/esp-idf/issues/17855) (P4 OTA hash-corrupt with XIP). Documented as a **known limitation**, not enabled by default.
- (`CONFIG_SPI_FLASH_AUTO_SUSPEND` is **not** supported on the P4.)

---

## Attribution

- **Our code is ours** — `board_p4.c` is our own implementation of the bring-up, licensed **MIT, © ultramcu**.
- **[elik745i/ESP32-2432S024C-Remote](https://github.com/elik745i/ESP32-2432S024C-Remote)** was used as a **reference for pin values, DSI timings, and the ST7701 init sequence**. That repo has **no license** (all rights reserved); pin numbers, register addresses and init sequences are **facts** (not copyrightable), so we referenced the facts — we did **not** copy its prose/comments. Credited here and in the source header.
- **[seobaeksol/JC4880P443C_I_W](https://github.com/seobaeksol/JC4880P443C_I_W)** — pin-map docs we cross-checked. Credited.
- **Schematic** is Guition's IP. We **link the vendor SDK** (below) rather than re-hosting it. Derived facts (pin map, connector list) are published; the **ST7701 init table** values are panel-register facts (also in the vendor SDK / datasheet), noted as such.

---

## License

**MIT** — © 2026 ultramcu. See [LICENSE](LICENSE).

---

## Links

**This project**
- Sibling troubleshooting repo (full pinout, schematic links, field notes, C6 fix): https://github.com/ultramcu/guition-jc4880p443c-i-w
- Vendor SDK download (demo code, drivers, datasheets, schematic, tools, ~385 MB): https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip

**References / attribution**
- elik745i/ESP32-2432S024C-Remote (pin/timing reference, no license — facts only): https://github.com/elik745i/ESP32-2432S024C-Remote
- seobaeksol/JC4880P443C_I_W (pin-map docs): https://github.com/seobaeksol/JC4880P443C_I_W
- CNX Software board overview: https://www.cnx-software.com/2025/08/12/4-3-inch-touch-display-board-features-single-esp32-p4-esp32-c6-module-supports-camera-and-speakers/

**Toolchain / firmware**
- pioarduino platform 55.03.36-1: https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.36-1
- C6 ESP-Hosted slave firmware (`network_adapter_esp32c6.bin` 2.12.9): https://esphome.github.io/esp-hosted-firmware/
- ESP-IDF MIPI-DSI LCD docs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html
- ESP-IDF P4 External RAM (XIP-from-PSRAM, cache-disable behavior): https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/external-ram.html
- esp-idf#17855 (P4 OTA hash-corrupt with XIP): https://github.com/espressif/esp-idf/issues/17855

**Community**
- ESPHome device page: https://devices.esphome.io/devices/guition-esp32-p4-m3-dev/
- Home Assistant working-config thread: https://community.home-assistant.io/t/guition-esp32-p4-jc4880p443-working-config/999211

**Buy / case**
- DIYmalls (with case, `_Y` variant): https://www.amazon.com/DIYmalls-ESP32-P4-JC4880P443C_I_W_Y-Capacitive-Development/dp/B0FX9XM87X
- AliExpress (+ enclosure): https://www.aliexpress.com/item/1005009675498892.html
