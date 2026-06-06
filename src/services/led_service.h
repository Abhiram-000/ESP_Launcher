#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the LED driver hardware.
 */
void led_service_init(void);

/**
 * @brief Set the RGB LED to a specific color.
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void led_service_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off the RGB LED.
 */
void led_service_off(void);

/**
 * @brief Set global brightness scaling.
 * @param brightness  0 (off) to 255 (full).
 */
void led_service_set_brightness(uint8_t brightness);

/**
 * @brief Get current brightness.
 * @return Brightness (0-255).
 */
uint8_t led_service_get_brightness(void);

#ifdef __cplusplus
}
#endif
