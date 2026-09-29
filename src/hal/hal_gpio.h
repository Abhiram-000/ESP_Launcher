#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hal_gpio_isr_t)(void* arg);

/* Only the ISR-facing surface remains. init_input/init_output/set_level were
 * removed as dead code: nothing called them, and the Lua GPIO bindings (since
 * deleted) were the only thing that bypassed the HAL by calling gpio_* directly. */
int32_t IRAM_ATTR hal_gpio_get_level(gpio_num_t pin);
esp_err_t hal_gpio_install_isr_service(void);
esp_err_t hal_gpio_add_isr_handler(gpio_num_t pin, hal_gpio_isr_t handler, void* arg);

#ifdef __cplusplus
}
#endif
