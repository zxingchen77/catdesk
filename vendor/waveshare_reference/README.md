# Official hardware reference (not compiled)

Source: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7

Commit: bc51a84b30c0cea018fee6e0d7427ec10102060e

Files from `examples/ESP-IDF/09_lvgl_v9_demo` establish CH422G touch-reset/backlight sequencing and OPI PSRAM settings. The original sample defaults to 8 MB flash; CatDesk uses 16 MB verified with esptool on the actual N16R8 board.
