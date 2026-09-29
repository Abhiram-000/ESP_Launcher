#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "platform/board.h"
/* Panel frequency lives in display_hal.h so it is derived from
 * LAUNCHER_DISPLAY_DRIVER rather than restated here. It used to be hardcoded
 * to 40 MHz in this file while display_hal.h independently computed the same
 * value, so porting to ILI9341/ST7789 would have changed one and not the other
 * and the panel would silently keep running at 40 MHz. */
#include "utils/display_hal.h"

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
      cfg.freq_write = DISPLAY_SPI_FREQ_HZ;   // derived from LAUNCHER_DISPLAY_DRIVER
      cfg.freq_read  = DISPLAY_SPI_FREQ_HZ / 2;
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
      // ST7735 GRAM is 132x162 while the visible panel is 128x160. In
      // rotation 1 LovyanGFX computes _rowstart = memory_width-(panel_width+
      // offset_x) = 132-(128+offset_x). With offset_x=0 that leaves a 4px
      // vertical shift (content slides down, bottom rows wrap to the top).
      // offset_x=4 makes _rowstart=0 so the framebuffer maps 1:1 to the glass.
      // This only affects the vertical axis; _colstart (horizontal) stays 0.
      cfg.offset_x         = 4;
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