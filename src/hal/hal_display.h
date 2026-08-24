#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hal_display_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
