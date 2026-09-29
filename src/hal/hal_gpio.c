#include "hal_gpio.h"
#include "esp_log.h"

static const char* TAG __attribute__((unused)) = "HAL_GPIO";

static bool s_isr_service_installed = false;

/* IRAM_ATTR: called directly from the input GPIO ISR — must reside in
 * instruction RAM or the ISR faults when flash cache is disabled. */
int32_t IRAM_ATTR hal_gpio_get_level(gpio_num_t pin) {
    return gpio_get_level(pin);
}

esp_err_t hal_gpio_install_isr_service(void) {
    if (s_isr_service_installed) {
        return ESP_OK;
    }
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_isr_service_installed = true;
        return ESP_OK;
    }
    return err;
}

esp_err_t hal_gpio_add_isr_handler(gpio_num_t pin, hal_gpio_isr_t handler, void* arg) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    
    return gpio_isr_handler_add(pin, (gpio_isr_t)handler, arg);
}
