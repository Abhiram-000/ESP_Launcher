#include "hal_display.h"
#include "hal_spi.h"
#include "hal_gpio.h"
#include "driver/ledc.h"
#include <string.h>

static hal_display_config_t s_config;
static bool s_initialized = false;

esp_err_t hal_display_init(const hal_display_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    memcpy(&s_config, config, sizeof(hal_display_config_t));
    
    // Initialize backlight PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) return err;
    
    ledc_channel_config_t ledc_channel = {
        .gpio_num = config->pin_bl,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) return err;
    
    s_initialized = true;
    return ESP_OK;
}

esp_err_t hal_display_set_brightness(uint8_t brightness) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
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
