# Waveshare ESP32-S3-Touch-LCD-7

The board definition used by CatDesk is `esp32_s3_touch_lcd_7` from the official `waveshare_boards` repository.

The checked definition describes:

- ESP32-S3 with 8 MB embedded PSRAM
- 800x480 RGB LCD
- GT911 capacitive touch controller
- I2C SDA GPIO8 / SCL GPIO9
- Touch interrupt GPIO4
- RGB LCD timing and data pins supplied by the Board Manager definition

CatDesk does not duplicate those pin assignments in application code.
