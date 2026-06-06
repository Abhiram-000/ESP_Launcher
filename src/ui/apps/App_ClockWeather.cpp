/*
 * App_ClockWeather.cpp — Visually Stunning Clock & Weather Display
 * =================================================================
 * UI/UX Design:
 *   - Breathing gradient background (theme colors animated)
 *   - Large clock with smooth pulsing colon animation
 *   - Weather panel with animated icon and glow effects
 *   - Status indicators (NTP synced, weather freshness)
 *   - Smooth transitions between all states
 */
#include "App_ClockWeather.h"
#include "../theme_mgr.h"
#include "../../services/ntp_service.h"
#include "../../services/weather_service.h"
#include "../../services/wifi_mgr.h"
#include "../../services/idle_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

extern void switch_to_grid();

static const char* TAG = "APP_CLOCK";

App_ClockWeather::App_ClockWeather() 
    : _screen(SCREEN_CLOCK), _anim_time(0),
      _ntp_synced(false), _weather_has_data(false), _weather_is_stale(false),
      _weather_busy(false), _temperature(0), _wind_speed(0), _humidity(0),
      _weather_code(0), _is_day(1), _settings_cursor(0),
      _keyboard_active(false), _breath_phase(0), _weather_icon_bounce(0),
      _temp_glow(0), _cx(80), _w(160), _h(128)
{
    memset(&_time_info, 0, sizeof(_time_info));
    memset(_city_name, 0, sizeof(_city_name));
    memset(_status_msg, 0, sizeof(_status_msg));
}

App_ClockWeather::~App_ClockWeather() {}

void App_ClockWeather::init() {
    _screen = SCREEN_CLOCK;
    _anim_time = 0;
    _settings_cursor = 0;
    _keyboard_active = false;
    _breath_phase = 0;
    _weather_icon_bounce = 0;
    _temp_glow = 0;
    
    // Disable sleep mode while clock app is active
    idle_mgr_disable();
    
    // Trigger NTP sync if not already synced
    if (!ntp_service_is_synced()) {
        ESP_LOGI(TAG, "NTP not synced - triggering sync request");
        ntp_service_get_local_time(&_time_info); // This should trigger sync internally
    }
    
    ESP_LOGI(TAG, "Clock & Weather app initialized (sleep disabled)");
}

void App_ClockWeather::exit() {
    // Re-enable sleep mode when exiting clock app
    idle_mgr_enable();
    ESP_LOGI(TAG, "Clock & Weather app exited (sleep re-enabled)");
}

void App_ClockWeather::loop() {
    _anim_time += 0.05f;  // 40fps increment
    
    // Breathing animation (slow sine wave)
    _breath_phase = 0.5f + 0.5f * sinf(_anim_time * 0.3f);
    
    // Weather icon bounce (subtle)
    _weather_icon_bounce = 2.0f * sinf(_anim_time * 2.0f);
    
    // Temperature glow pulse
    _temp_glow = 0.3f + 0.2f * sinf(_anim_time * 1.5f);
    
    // Poll NTP service
    _ntp_synced = ntp_service_is_synced();
    ntp_service_get_local_time(&_time_info);
    
    // Poll weather service
    weather_status_t wx_status;
    weather_service_get_status(&wx_status);
    
    _weather_has_data = wx_status.has_data;
    _weather_is_stale = wx_status.is_stale;
    _weather_busy = wx_status.is_busy;
    
    if (wx_status.has_data) {
        _temperature = wx_status.current.temperature;
        _humidity = wx_status.current.humidity;
        _wind_speed = wx_status.current.wind_speed;
        _weather_code = wx_status.current.weather_code;
        _is_day = wx_status.current.is_day;
        strncpy(_city_name, wx_status.location.city_name, sizeof(_city_name) - 1);
    }
    strncpy(_status_msg, wx_status.status_msg, sizeof(_status_msg) - 1);
    
    // Auto-refresh weather if connected and data is stale
    if (wifi_mgr_is_connected() && _weather_is_stale && !_weather_busy) {
        ESP_LOGI(TAG, "Auto-refreshing stale weather");
        weather_service_refresh();
    }
}

void App_ClockWeather::handleInput(input_event_t event) {
    if (_keyboard_active) {
        _keyboard.handleInput(event);
        return;
    }
    
    if (_screen == SCREEN_CLOCK) {
        if (event == BTN_LEFT) {
            switch_to_grid();
            return;
        }
        if (event == BTN_SELECT) {
            _screen = SCREEN_SETTINGS;
            _settings_cursor = 0;
        }
        if (event == BTN_RIGHT && wifi_mgr_is_connected() && !_weather_busy) {
            ESP_LOGI(TAG, "Manual weather refresh");
            weather_service_refresh();
        }
    }
    else if (_screen == SCREEN_SETTINGS) {
        if (event == BTN_LEFT) {
            _screen = SCREEN_CLOCK;
        }
        if (event == BTN_UP && _settings_cursor > 0) {
            _settings_cursor--;
        }
        if (event == BTN_DOWN && _settings_cursor < 2) {
            _settings_cursor++;
        }
        if (event == BTN_SELECT) {
            if (_settings_cursor == 0) {
                // Change location via keyboard
                _keyboard_active = true;
                    _keyboard.begin(
                    "City Name:",
                    [this](const char* city) {
                        ESP_LOGI(TAG, "User entered city: %s", city);
                        this->_keyboard_active = false;
                    },
                    [this]() {
                        this->_keyboard_active = false;
                    }
                );
            }
            else if (_settings_cursor == 1 && wifi_mgr_is_connected() && !_weather_busy) {
                // Refresh weather
                weather_service_refresh();
            }
            else if (_settings_cursor == 2) {
                // Back to clock
                _screen = SCREEN_CLOCK;
            }
        }
    }
}

uint16_t App_ClockWeather::_blend_color(uint16_t c1, uint16_t c2, float ratio) {
    if (ratio <= 0.0f) return c1;
    if (ratio >= 1.0f) return c2;
    
    uint8_t r1 = (c1 >> 11) & 0x1F;
    uint8_t g1 = (c1 >> 5) & 0x3F;
    uint8_t b1 = c1 & 0x1F;
    
    uint8_t r2 = (c2 >> 11) & 0x1F;
    uint8_t g2 = (c2 >> 5) & 0x3F;
    uint8_t b2 = c2 & 0x1F;
    
    uint8_t r = r1 + (uint8_t)((r2 - r1) * ratio);
    uint8_t g = g1 + (uint8_t)((g2 - g1) * ratio);
    uint8_t b = b1 + (uint8_t)((b2 - b1) * ratio);
    
    return (r << 11) | (g << 5) | b;
}

void App_ClockWeather::_draw_digit_pair(LGFX_Sprite* canvas, int value, int32_t x, int32_t y, bool show_colon) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02d", value);
    
    canvas->setFont(&fonts::Font2);
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString(buf, x, y);
    
    // Static colon (no blinking)
    if (show_colon) {
        canvas->drawString(":", x + 32, y);
    }
}

void App_ClockWeather::_draw_time_large(LGFX_Sprite* canvas, int cx, int cy) {
    // Large clock display: HH:MM:SS with thick text
    int hour = _time_info.tm_hour;
    int min = _time_info.tm_min;
    int sec = _time_info.tm_sec;
    
    // Draw shadow/outline for thickness effect (4 passes: down-right, down-left, up-right, up-left)
    uint16_t shadow_color = _blend_color(theme_get_bg(), theme_get_text(), 0.4f);
    canvas->setTextColor(shadow_color);
    
    // Shadow pass 1 (bottom-right)
    _draw_digit_pair(canvas, hour, cx - 46, cy + 1, true);
    _draw_digit_pair(canvas, min, cx - 6, cy + 1, true);
    _draw_digit_pair(canvas, sec, cx + 34, cy + 1, false);
    
    // Shadow pass 2 (right)
    _draw_digit_pair(canvas, hour, cx - 47, cy, true);
    _draw_digit_pair(canvas, min, cx - 7, cy, true);
    _draw_digit_pair(canvas, sec, cx + 33, cy, false);
    
    // Shadow pass 3 (bottom)
    _draw_digit_pair(canvas, hour, cx - 48, cy + 1, true);
    _draw_digit_pair(canvas, min, cx - 8, cy + 1, true);
    _draw_digit_pair(canvas, sec, cx + 32, cy + 1, false);
    
    // Main pass (bright, on top)
    canvas->setTextColor(theme_get_text());
    _draw_digit_pair(canvas, hour, cx - 48, cy, true);
    _draw_digit_pair(canvas, min, cx - 8, cy, true);
    _draw_digit_pair(canvas, sec, cx + 32, cy, false);
}

void App_ClockWeather::_draw_date_small(LGFX_Sprite* canvas, int cx, int cy) {
    char date_buf[32];
    const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    // Only show date if NTP is synced (to avoid showing wrong year)
    if (_ntp_synced) {
        snprintf(date_buf, sizeof(date_buf), "%s, %s %d %d",
                 weekdays[_time_info.tm_wday],
                 months[_time_info.tm_mon],
                 _time_info.tm_mday,
                 _time_info.tm_year + 1900);
    } else {
        // Show "Syncing..." message when NTP not ready
        snprintf(date_buf, sizeof(date_buf), "Syncing time...");
    }
    
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(_ntp_synced ? theme_get_text_dim() : theme_get_accent());
    canvas->drawString(date_buf, cx, cy);
}

void App_ClockWeather::_draw_weather_panel(LGFX_Sprite* canvas) {
    if (!_weather_has_data) return;
    
    int panel_y = _h - 54;
    int panel_h = _h / 3;
    
    // Beautiful rounded panel with gradient
    uint16_t panel_bg = _blend_color(theme_get_bg(), theme_get_accent(), 0.12f);
    canvas->fillRoundRect(4, panel_y, _w - 8, panel_h, 8, panel_bg);
    
    uint16_t border_color = _blend_color(theme_get_accent(), theme_get_text(), 0.6f);
    canvas->drawRoundRect(4, panel_y, _w - 8, panel_h, 8, border_color);
    canvas->drawRoundRect(5, panel_y + 1, _w - 10, panel_h - 2, 7, _blend_color(border_color, theme_get_bg(), 0.5f));
    
    // Draw a simple cloud icon (box-based, not emoji)
    uint16_t icon_color = _blend_color(theme_get_accent(), theme_get_text(), 0.7f);
    int cloud_x = _w / 10;
    int cloud_y = panel_y + 18 + (int)_weather_icon_bounce;
    
    // Draw cloud shape with circles
    canvas->fillCircle(cloud_x - 4, cloud_y, 5, icon_color);
    canvas->fillCircle(cloud_x + 2, cloud_y - 2, 6, icon_color);
    canvas->fillCircle(cloud_x + 8, cloud_y, 5, icon_color);
    canvas->fillRect(cloud_x - 4, cloud_y, 12, 5, icon_color);
    
    // RIGHT SIDE - Perfectly spaced in 3 rows
    int right_start_x = _w / 4 + 2;
    
    // ROW 1 (TOP): Temperature with degree dot - NO GAP before C
    char temp_buf[8];
    snprintf(temp_buf, sizeof(temp_buf), "%.0f", _temperature);
    
    uint16_t temp_color = _blend_color(theme_get_text(), theme_get_accent(), _temp_glow);
    canvas->setFont(&fonts::Font2);
    canvas->setTextColor(temp_color);
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString(temp_buf, right_start_x, panel_y + 2);
    
    // Degree dot positioned right after number (no gap)
    int temp_width = strlen(temp_buf) * 16;
    canvas->fillCircle(right_start_x + temp_width + 2, panel_y + 6, 2, temp_color);
    
    // "C" immediately after degree dot (no gap)
    canvas->setFont(&fonts::Font2);
    canvas->drawString("C", right_start_x + temp_width + 6, panel_y + 2);
    
    // ROW 2 (MIDDLE): Weather description
    const char* desc = weather_code_to_description(_weather_code);
    canvas->setFont(&fonts::Font0);
    uint16_t desc_color = _blend_color(theme_get_text(), theme_get_accent(), 0.6f);
    canvas->setTextColor(desc_color);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->drawString(desc, right_start_x, panel_y + 20);
    
    // ROW 3 (BOTTOM): Details - moved lower to avoid overlap
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_left);
    
    // Humidity (bottom left) - moved down
    char humidity_buf[16];
    snprintf(humidity_buf, sizeof(humidity_buf), "Hum %d%%", _humidity);
    canvas->drawString(humidity_buf, right_start_x, panel_y + panel_h - 2);
    
    // Wind (bottom right)
    char wind_buf[16];
    snprintf(wind_buf, sizeof(wind_buf), "%.0fkm/h", _wind_speed);
    canvas->setTextDatum(textdatum_t::bottom_right);
    canvas->drawString(wind_buf, _w - 10, panel_y + panel_h - 2);
}

void App_ClockWeather::_draw_status_indicator(LGFX_Sprite* canvas) {
    int x = 4;
    int y = 4;
    
    // NTP sync indicator
    uint16_t ntp_color = _ntp_synced ? theme_get_success() : theme_get_text_dim();
    canvas->fillCircle(x + 2, y + 2, 2, ntp_color);
    
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString(_ntp_synced ? "NTP" : "RTC", x + 8, y + 2);
    
    // Weather status indicator
    if (_weather_busy) {
        // Spinning indicator
        float angle = fmodf(_anim_time * 200.0f, 360.0f);
        int cx = _w - 10, cy = y + 3;
        int r = 3;
        canvas->drawArc(cx, cy, r, r - 1, (int)angle, (int)angle + 90, theme_get_accent());
        canvas->drawArc(cx, cy, r, r - 1, (int)angle + 180, (int)angle + 270, theme_get_accent());
    }
    else if (_weather_is_stale) {
        canvas->setTextDatum(textdatum_t::middle_right);
        canvas->setTextColor(theme_get_fail());
        canvas->drawString("STALE", _w - 4, y + 2);
    }
    else if (_weather_has_data) {
        canvas->fillCircle(_w - 6, y + 2, 2, theme_get_success());
    }
}

void App_ClockWeather::_draw_clock_screen(LGFX_Sprite* canvas) {
    // Static background color (no breathing animation)
    uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);
    
    // Parallax grid wallpaper (subtle)
    theme_draw_wallpaper(canvas, _anim_time);
    
    // Status indicators (top)
    _draw_status_indicator(canvas);
    
    // City name (top center)
    if (_city_name[0] != '\0') {
        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->setTextColor(theme_get_accent());
        canvas->drawString(_city_name, _cx, _h / 9);
    }
    
    // Large clock display (center)
    _draw_time_large(canvas, _cx, _h / 4);
    
    // Date (below clock)
    _draw_date_small(canvas, _cx, _h / 2 - 6);
    
    // Weather panel (bottom)
    _draw_weather_panel(canvas);
    
    // Footer hints
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("L=Back  SEL=Set  R=Refresh", _cx, _h - 2);
}

void App_ClockWeather::_draw_settings_screen(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    
    // Header
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(theme_get_accent());
    canvas->drawString("- SETTINGS -", _cx, 4);
    canvas->drawFastHLine(8, 14, _w - 16, theme_get_accent());
    
    // Menu items
    const char* items[] = {"Change Location", "Refresh Weather", "Back to Clock"};
    int item_y = _h / 5 + 2;
    int item_h = _h / 6 + 2;
    
    for (int i = 0; i < 3; i++) {
        bool selected = (i == _settings_cursor);
        
        if (selected) {
            uint16_t sel_bg = _blend_color(theme_get_accent(), theme_get_bg(), 0.2f);
            canvas->fillRoundRect(8, item_y + i * item_h, _w - 16, item_h - 2, 4, sel_bg);
            canvas->drawRoundRect(8, item_y + i * item_h, _w - 16, item_h - 2, 4, theme_get_accent());
        }
        
        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->setTextColor(selected ? theme_get_text() : theme_get_text_dim());
        canvas->drawString(items[i], 16, item_y + i * item_h + (item_h - 2) / 2);
    }
    
    // Current location info
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("Current:", 8, _h - 36);
    canvas->setTextColor(theme_get_text());
    canvas->drawString(_city_name, 8, _h - 26);
    
    // Footer
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("L=Back  UP/DN=Move  SEL=OK", _cx, _h - 2);
}

void App_ClockWeather::render(LGFX_Sprite* canvas) {
    // Update dimensions from actual canvas for resolution independence
    _w = canvas->width();
    _h = canvas->height();
    _cx = _w / 2;

    if (_keyboard_active) {
        // Draw base screen first
        if (_screen == SCREEN_CLOCK) {
            _draw_clock_screen(canvas);
        } else {
            _draw_settings_screen(canvas);
        }
        // Overlay keyboard
        _keyboard.render(canvas);
    }
    else {
        if (_screen == SCREEN_CLOCK) {
            _draw_clock_screen(canvas);
        } else {
            _draw_settings_screen(canvas);
        }
    }
}
