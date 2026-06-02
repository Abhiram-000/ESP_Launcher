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
#define LAUNCHER_PIN_UP          5
#define LAUNCHER_PIN_DOWN        17
#define LAUNCHER_PIN_LEFT        18
#define LAUNCHER_PIN_RIGHT       15
#define LAUNCHER_PIN_SELECT      16

#ifdef __cplusplus
}
#endif
