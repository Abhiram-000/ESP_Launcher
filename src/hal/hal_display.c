/*
 * hal_display.c — Display backlight control via LEDC PWM
 *
 * NOTE: LovyanGFX configures LEDC_TIMER_0 and LEDC_CHANNEL_0 for backlight
 * control in LGFX_Config.h. The init function below is intentionally disabled
 * to avoid conflicting with LovyanGFX. Use tft.setBrightness() directly
 * or call hal_display_set_brightness() which uses the same LEDC channel.
 */

#include "hal_display.h"
#include "esp_log.h"

static const char* TAG __attribute__((unused)) = "HAL_DISP";
#include "hal_spi.h"
#include "hal_gpio.h"
#include "driver/ledc.h"
#include <string.h>

esp_err_t hal_display_init(const hal_display_config_t* config) {
    /* WARNING: This function conflicts with LovyanGFX backlight control.
     * LovyanGFX already configures LEDC_TIMER_0 and LEDC_CHANNEL_0.
     * Do NOT call this function — use tft.setBrightness() instead.
     * Kept for reference only. */
    (void)config;
    return ESP_OK;
}

esp_err_t hal_display_set_brightness(uint8_t brightness) {
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t hal_display_push_rect(const hal_display_rect_t* rect, const uint16_t* data) {
    // Stub - actual implementation would use LovyanGFX or direct SPI
    (void)rect;
    (void)data;
    return ESP_OK;
}

esp_err_t hal_display_fill_rect(const hal_display_rect_t* rect, uint16_t color) {
    // Stub - actual implementation would use LovyanGFX or direct SPI
    (void)rect;
    (void)color;
    return ESP_OK;
}
