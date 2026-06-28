# Vendored: esp_lcd_touch_gt911 (+ esp_lcd_touch base)

Vendored verbatim for the Arduino-LDF flow (IDF managed components, not PIO libs).

- **Origin:** github.com/espressif/esp-bsp `components/lcd_touch/`
- **Versions:** `esp_lcd_touch_gt911` 1.2.0~2 ; `esp_lcd_touch` base 1.2.1
  (from each component's `idf_component.yml`)
- **License:** Apache-2.0 (`license.txt`, `license_esp_lcd_touch.txt`; headers kept)
- **Fetched:** 2026-06-25 via GitHub API.

## Files
- `esp_lcd_touch_gt911.c` + `include/esp_lcd_touch_gt911.h` — GT911 driver
- `esp_lcd_touch.c` + `include/esp_lcd_touch.h` — touch base (process/get coords, etc.)
- `license.txt`, `license_esp_lcd_touch.txt`

## Why the touch base is bundled here
The existing `lib/board_axs15231b/` ALSO vendors its own `esp_lcd_touch.c/.h`. That
copy is compiled only in the S3 envs. The P4 (`jc4880p4`) env uses a DIFFERENT
`build_src_filter` and lib set, so to keep the P4 env self-contained — and to avoid
a duplicate-symbol clash if both libs were ever pulled into one link — the touch
base is vendored here too. Dev-A must ensure the jc4880p4 env's `lib_deps` /
`lib_ignore` pulls in `esp_lcd_touch_gt911` and NOT `board_axs15231b`.

## GT911 address note
Address is selected by the INT pin level at reset: INT low → 0x5D, INT high → 0x14.
The board bringup probes 0x5D then 0x14 (INT pin is unknown on this board — see
`firmware/src/boards/jc4880p4/HW_NOTES.md` §2).
