#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "platform/board.h"

class LGFX_ESP32S3_ST7735 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX_ESP32S3_ST7735() {
    { // Configure SPI Bus with DMA
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;     
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;    // Increased from 27MHz to 40MHz for ST7735S
      cfg.freq_read = 16000000;
      cfg.pin_sclk = LAUNCHER_PIN_SCLK;
      cfg.pin_mosi = LAUNCHER_PIN_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc   = LAUNCHER_PIN_DC;
      cfg.dma_channel = SPI_DMA_CH_AUTO; // Enable DMA for optimal performance
      cfg.spi_3wire = false;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Configure Panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = LAUNCHER_PIN_CS;
      cfg.pin_rst          = LAUNCHER_PIN_RST;    
      cfg.panel_width      = 128;
      cfg.panel_height     = 160;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }

    { // Configure Backlight
      auto cfg = _light_instance.config();
      cfg.pin_bl = LAUNCHER_PIN_BL;               
      cfg.invert = false;           
      cfg.freq   = 5000;
      cfg.pwm_channel = 0;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};