#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

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
      cfg.pin_sclk = 12;            
      cfg.pin_mosi = 11;            
      cfg.pin_miso = -1;            
      cfg.pin_dc   = 9;
      cfg.dma_channel = SPI_DMA_CH_AUTO; // Enable DMA for optimal performance
      cfg.spi_3wire = false;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Configure Panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 8;     
      cfg.pin_rst          = 10;    
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
      cfg.pin_bl = 4;               
      cfg.invert = false;           
      cfg.freq   = 5000;
      cfg.pwm_channel = 0;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};