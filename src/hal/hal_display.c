/*
 * hal_display.c — Display backlight control via LEDC PWM
 *
 * WHY: LovyanGFX owns LEDC_TIMER_0/LEDC_CHANNEL_0 and is the sole display
 * API (see LGFX_Config.h). Panel init/draw functions are intentionally
 * absent — routing pixels or backlight setup through this HAL would
 * conflict with LovyanGFX's own LEDC configuration.
 */

#include "hal_display.h"
#include "driver/ledc.h"

esp_err_t hal_display_set_brightness(uint8_t brightness) {
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
