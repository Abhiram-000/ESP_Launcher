#include "hal_spi.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG __attribute__((unused)) = "HAL_SPI";

esp_err_t hal_spi_bus_init(const hal_spi_bus_config_t* config) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = config->max_transfer_sz,
        .flags = SPICOMMON_BUSFLAG_MASTER
    };
    
    return spi_bus_initialize(config->host, &bus_cfg, (spi_dma_chan_t)config->dma_chan);
}

esp_err_t hal_spi_device_init(hal_spi_device_t* out_handle, const hal_spi_device_config_t* config, spi_host_device_t host) {
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = config->mode,
        .clock_speed_hz = config->clock_speed_hz,
        .spics_io_num = config->cs_pin,
        .flags = 0,
        .queue_size = config->queue_size,
        .pre_cb = NULL,
        .post_cb = NULL
    };
    
    return spi_bus_add_device(host, &dev_cfg, out_handle);
}

esp_err_t hal_spi_transmit(hal_spi_device_t device, const uint8_t* data, size_t len) {
    if (len == 0) return ESP_OK;
    
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = len * 8;
    trans.tx_buffer = data;
    
    return spi_device_transmit(device, &trans);
}
