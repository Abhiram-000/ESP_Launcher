#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the onboard RGB LED (NeoPixel on GPIO 48).
 */
void led_driver_init(void);

/**
 * @brief Set the LED to a specific RGB value.
 */
void led_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn the LED completely OFF.
 */
void led_driver_off(void);

/**
 * @brief Set global brightness scaling factor.
 *        Applied to all subsequent set_rgb calls.
 * @param brightness  0 (off) to 255 (full). Scaled internally.
 */
void led_driver_set_brightness(uint8_t brightness);

/**
 * @brief Get current brightness setting.
 * @return Current brightness (0-255).
 */
uint8_t led_driver_get_brightness(void);

#ifdef __cplusplus
}
#endif
