#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_host_device_t host;
    int8_t mosi_pin;
    int8_t miso_pin;
    int8_t sclk_pin;
    size_t max_transfer_sz;
    uint32_t dma_chan;
} hal_spi_bus_config_t;

typedef struct {
    int8_t cs_pin;
    int8_t dc_pin;
    uint32_t clock_speed_hz;
    uint8_t mode;
    uint8_t queue_size;
} hal_spi_device_config_t;

typedef spi_device_handle_t hal_spi_device_t;

esp_err_t hal_spi_bus_init(const hal_spi_bus_config_t* config);
esp_err_t hal_spi_device_init(hal_spi_device_t* out_handle, const hal_spi_device_config_t* config, spi_host_device_t host);
esp_err_t hal_spi_transmit(hal_spi_device_t device, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
