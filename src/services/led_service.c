#include "led_service.h"
#include "../drivers/led_driver.h"
#include "esp_log.h"

static const char* TAG = "LED_SVC";

void led_service_init(void) {
    ESP_LOGI(TAG, "Initializing LED service");
    led_driver_init();
}

void led_service_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    led_driver_set_rgb(r, g, b);
}

void led_service_off(void) {
    led_driver_off();
}

void led_service_set_brightness(uint8_t brightness) {
    led_driver_set_brightness(brightness);
}

uint8_t led_service_get_brightness(void) {
    return led_driver_get_brightness();
}
