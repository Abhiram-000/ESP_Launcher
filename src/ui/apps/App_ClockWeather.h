/*
 * App_ClockWeather.h — Clock & Weather Display with Stunning Animations
 * ======================================================================
 * Features:
 *   - Large animated clock display (HH:MM:SS) with pulsing colon
 *   - Live weather panel with temperature, humidity, wind, condition icon
 *   - Smooth color-breathing background gradient
 *   - Auto-refresh on internet connection
 *   - Offline mode with cached data
 *   - Settings screen for manual location override
 */
#pragma once

#include "IApp.h"
#include "../components/UI_Keyboard.h"

class App_ClockWeather : public IApp {
private:
    enum Screen { 
        SCREEN_CLOCK,       // Main clock display with weather
        SCREEN_SETTINGS     // Location override + refresh
    };
    
    Screen      _screen;
    float       _anim_time;
    
    // Cached service data (polled in loop(), rendered in render())
    struct tm   _time_info;
    bool        _ntp_synced;
    bool        _weather_has_data;
    bool        _weather_is_stale;
    bool        _weather_busy;
    float       _temperature;
    float       _wind_speed;
    int         _humidity;
    int         _weather_code;
    int         _is_day;
    char        _city_name[48];
    char        _status_msg[64];
    
    // Settings screen
    int         _settings_cursor;
    UI_Keyboard _keyboard;
    bool        _keyboard_active;
    
    // Animation state
    float       _breath_phase;
    float       _weather_icon_bounce;
    float       _temp_glow;
    
    // Layout cache
    int32_t     _cx;
    int32_t     _w;
    int32_t     _h;

public:
    App_ClockWeather();
    ~App_ClockWeather() override;
    
    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

private:
    void _draw_clock_screen(LGFX_Sprite* canvas);
    void _draw_settings_screen(LGFX_Sprite* canvas);
    void _draw_time_large(LGFX_Sprite* canvas, int cx, int cy);
    void _draw_date_small(LGFX_Sprite* canvas, int cx, int cy);
    void _draw_weather_panel(LGFX_Sprite* canvas);
    void _draw_status_indicator(LGFX_Sprite* canvas);
    void _draw_digit_pair(LGFX_Sprite* canvas, int value, int32_t x, int32_t y, bool show_colon);
    uint16_t _blend_color(uint16_t c1, uint16_t c2, float ratio);
};
