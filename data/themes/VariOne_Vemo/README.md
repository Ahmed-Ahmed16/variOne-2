# VariOne Vemo Theme

Firmware-sized VariOne theme pack for the 128x160 ST7735 VariOne S3 display.

The menu images are capped at 160 px wide because the PNG decoder line buffer is sized from the board's maximum display dimension. The extra `vemo_*` images are optional status screens used by VariOne UI helpers during scan, success, and error states. Other Bruce themes continue to work without those keys.

Install by copying this folder to the SD card or LittleFS and selecting `theme.json` from `Config > UI Theme`.
