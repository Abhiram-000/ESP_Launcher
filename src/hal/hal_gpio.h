#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hal_gpio_isr_t)(void* arg);

esp_err_t hal_gpio_init_input(gpio_num_t pin, bool pullup);
esp_err_t hal_gpio_init_output(gpio_num_t pin);
esp_err_t hal_gpio_set_level(gpio_num_t pin, uint32_t level);
int32_t IRAM_ATTR hal_gpio_get_level(gpio_num_t pin);
esp_err_t hal_gpio_install_isr_service(void);
esp_err_t hal_gpio_add_isr_handler(gpio_num_t pin, hal_gpio_isr_t handler, void* arg);

#ifdef __cplusplus
}
#endif
