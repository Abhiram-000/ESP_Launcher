#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Display Settings
#define LAUNCHER_DISPLAY_DRIVER  "ST7735"
#define LAUNCHER_DISPLAY_W       160
#define LAUNCHER_DISPLAY_H       128

// Input Settings
// NOTE: GPIO 6-11 are reserved for Octal PSRAM/Flash — cannot be used as GPIO
#define LAUNCHER_PIN_UP          3
#define LAUNCHER_PIN_DOWN        17
#define LAUNCHER_PIN_LEFT        18
#define LAUNCHER_PIN_RIGHT       15
#define LAUNCHER_PIN_SELECT      16

// LED Settings
#define LAUNCHER_PIN_LED          48

// Display SPI pins
#define LAUNCHER_PIN_SCLK    12
#define LAUNCHER_PIN_MOSI    11
#define LAUNCHER_PIN_DC       9
#define LAUNCHER_PIN_CS       8
#define LAUNCHER_PIN_RST     10
#define LAUNCHER_PIN_BL       4

#ifdef __cplusplus
}
#endif
