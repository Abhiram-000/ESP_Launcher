#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the UI Task (uses LovyanGFX).
 * This acts as a C wrapper so the pure C system manager can call it.
 */
void ui_task_init(void);

/**
 * @brief Set the TFT display brightness and save it persistently to NVS.
 * @param val Brightness level (0-255).
 */
void ui_set_brightness(uint8_t val);

/**
 * @brief Get the current TFT display brightness.
 * @return Current brightness level (0-255).
 */
uint8_t ui_get_brightness(void);

/**
 * @brief Get current launcher theme.
 * @return 0=Carousel, 1=Tile
 */
uint8_t ui_get_launcher_theme(void);

/**
 * @brief Set launcher theme and persist to NVS.
 * @param theme 0=Carousel, 1=Tile
 */
void ui_set_launcher_theme(uint8_t theme);

#ifdef __cplusplus
}
#endif
