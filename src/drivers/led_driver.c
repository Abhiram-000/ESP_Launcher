#include "led_driver.h"
#include "esp_log.h"
#include "led_strip.h"

// Typically GPIO 48 for ESP32-S3 DevKitC-1 built-in NeoPixel
#ifndef BOARD_LED_GPIO
#define BOARD_LED_GPIO 48
#endif

static const char *TAG = "LED_DRIVER";
static led_strip_handle_t led_strip;
static bool initialized = false;

void led_driver_init(void) {
    ESP_LOGI(TAG, "Initializing onboard NeoPixel on GPIO %d", BOARD_LED_GPIO);
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_LED_GPIO,
        .max_leds = 1,
        .color_component_format = (led_color_component_format_t) {
            .format = {
                .r_pos = 1,
                .g_pos = 2, /* Swapped Green and Blue */
                .b_pos = 0,
                .w_pos = 3,
                .reserved = 0,
                .bytes_per_color = 1,
                .num_components = 3
            }
        },
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false },
    };
    
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags = { .with_dma = false },
    };
    
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err == ESP_OK) {
        initialized = true;
        led_strip_clear(led_strip);
    } else {
        ESP_LOGE(TAG, "Failed to init led_strip (%s)", esp_err_to_name(err));
    }
}

void led_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized) return;
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

void led_driver_off(void) {
    if (!initialized) return;
    led_strip_clear(led_strip);
}
