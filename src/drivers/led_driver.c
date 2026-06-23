#include "led_driver.h"
#include "esp_log.h"
#include "../utils/error_handling.h"
#include "led_strip.h"
#include "../platform/board.h"

static const char *TAG = "LED_DRIVER";
static led_strip_handle_t led_strip = NULL;
static volatile bool initialized = false;
static volatile uint8_t brightness = 255;  /* Global brightness scaling (0-255) */

void led_driver_init(void) {
    ESP_LOGI(TAG, "Initializing NeoPixel on GPIO %d", LAUNCHER_PIN_LED);
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = LAUNCHER_PIN_LED,
        .max_leds = 1,
        .color_component_format = (led_color_component_format_t) {
            .format = {
                .r_pos = 1,
                .g_pos = 0,
                .b_pos = 2,
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
        .resolution_hz = 10 * 1000 * 1000,
        .flags = { .with_dma = false },
    };
    
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err == ESP_OK) {
        initialized = true;
        ESP_ERROR_WARN(led_strip_clear(led_strip), TAG, "LED clear");
        ESP_LOGI(TAG, "LED driver initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to init LED strip: %s", esp_err_to_name(err));
    }
}

void led_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized || !led_strip) return;
    /* Apply brightness scaling: (value * brightness) >> 8 */
    uint8_t br = brightness;
    if (br < 255) {
        r = (uint8_t)(((uint16_t)r * br) >> 8);
        g = (uint8_t)(((uint16_t)g * br) >> 8);
        b = (uint8_t)(((uint16_t)b * br) >> 8);
    }
    ESP_ERROR_WARN(led_strip_set_pixel(led_strip, 0, r, g, b), TAG, "LED set pixel");
    ESP_ERROR_WARN(led_strip_refresh(led_strip), TAG, "LED refresh");
}

void led_driver_set_brightness(uint8_t br) {
    brightness = br;
}

uint8_t led_driver_get_brightness(void) {
    return brightness;
}

void led_driver_off(void) {
    if (!initialized || !led_strip) return;
    ESP_ERROR_WARN(led_strip_clear(led_strip), TAG, "LED clear");
}
