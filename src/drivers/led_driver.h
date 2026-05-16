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

#ifdef __cplusplus
}
#endif
