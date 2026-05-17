#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} hal_display_rect_t;

typedef struct {
    uint8_t spi_host;
    uint8_t pin_cs;
    uint8_t pin_dc;
    uint8_t pin_rst;
    uint8_t pin_bl;
    uint8_t pin_sclk;
    uint8_t pin_mosi;
    uint32_t spi_freq;
    uint16_t width;
    uint16_t height;
} hal_display_config_t;

esp_err_t hal_display_init(const hal_display_config_t* config);
esp_err_t hal_display_set_brightness(uint8_t brightness);
esp_err_t hal_display_push_rect(const hal_display_rect_t* rect, const uint16_t* data);
esp_err_t hal_display_fill_rect(const hal_display_rect_t* rect, uint16_t color);

#ifdef __cplusplus
}
#endif
