#include "UI_WifiScanner.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char* TAG = "UI_WIFI";

UI_WifiScanner::UI_WifiScanner() 
    : _active(false), _state(State::Idle), _anim_time(0), _spinner_angle(0),
      _network_count(0), _connect_timeout_ticks(0) {}
UI_WifiScanner::~UI_WifiScanner() {}

void UI_WifiScanner::begin(std::function<void(bool)> on_complete) {
    _active = true;
    _on_complete = on_complete;
    _state = State::Scanning;
    _network_count = 0;
    _list_sel = 0;
    _list_scroll = 0;
    _connect_success = false;
    _connect_timeout_ticks = 0;
    memset(_selected_ssid, 0, sizeof(_selected_ssid));
    memset(_password, 0, sizeof(_password));
    
    wifi_mgr_scan_start();
}

void UI_WifiScanner::loop() {
    if (!_active) return;
    _anim_time += 0.05f;
    _spinner_angle += 0.15f;
    
    if (_state == State::Scanning) {
        if (wifi_mgr_is_scan_done()) {
            _network_count = wifi_mgr_get_scan_results(_networks, 15);
            _state = State::List;
        }
    }
    else if (_state == State::Connecting) {
        if (wifi_mgr_is_connected()) {
            _connect_success = true;
            _state = State::Done;
        } else {
            _connect_timeout_ticks++;
            if (_connect_timeout_ticks > 200) { // roughly 10 seconds at 50ms loop
                ESP_LOGW(TAG, "connect timeout for %s", _selected_ssid);
                _connect_success = false;
                _state = State::Done;
            }
        }
    }
    else if (_state == State::Done) {
        _active = false;
        if (_on_complete) _on_complete(_connect_success);
    }
}

void UI_WifiScanner::handleInput(input_event_t ev) {
    if (!_active) return;

    if (_state == State::Password && _keyboard.isActive()) {
        _keyboard.handleInput(ev);
        return;
    }

    if (_state == State::List) {
        if (ev == BTN_UP && _list_sel > 0) {
            _list_sel--;
            if (_list_sel < _list_scroll) _list_scroll--;
        }
        else if (ev == BTN_DOWN && _list_sel < _network_count - 1) {
            _list_sel++;
            if (_list_sel >= _list_scroll + 4) _list_scroll++;
        }
        else if (ev == BTN_LEFT) {
            _connect_success = false;
            _state = State::Done; // Cancel
        }
        else if (ev == BTN_RIGHT) {
            _state = State::Scanning;
            wifi_mgr_scan_start();
        }
        else if (ev == BTN_SELECT) {
            strncpy(_selected_ssid, _networks[_list_sel].ssid, sizeof(_selected_ssid)-1);
            if (_networks[_list_sel].authmode == WIFI_AUTH_OPEN) {
                wifi_mgr_connect(_selected_ssid, "");
                _state = State::Connecting;
            } else {
                _state = State::Password;
                _keyboard.begin(
                    "Password:", 
                    [this](const char* pwd) {
                        strncpy(this->_password, pwd, sizeof(this->_password)-1);
                        wifi_mgr_connect(this->_selected_ssid, this->_password);
                        this->_state = State::Connecting;
                    },
                    [this]() {
                        this->_state = State::List;
                    }
                );
            }
        }
    }
    else if (_state == State::Scanning || _state == State::Connecting) {
        if (ev == BTN_LEFT) { // Cancel
            _connect_success = false;
            _state = State::Done;
        }
    }
}

void UI_WifiScanner::_draw_header(LGFX_Sprite* c, const char* title, uint16_t accent) {
    c->fillRect(0, 0, 160, 14, theme_get_bg());
    c->drawFastHLine(0, 14, 160, accent);
    c->setTextDatum(textdatum_t::middle_left);
    c->setFont(&fonts::Font0);
    c->setTextColor(theme_get_text());
    c->drawString(title, 4, 7);
}

void UI_WifiScanner::_draw_spinner(LGFX_Sprite* c, int cx, int cy, int r) {
    float a = fmodf(_anim_time * 100.0f, 360.0f);
    c->drawArc(cx, cy, r, r - 3, (int)a, (int)a + 90, theme_get_accent());
    c->drawArc(cx, cy, r, r - 3, (int)a + 180, (int)a + 270, theme_get_accent());
}

void UI_WifiScanner::render(LGFX_Sprite* c) {
    if (!_active) return;
    
    // Dim the background
    c->fillRect(0, 0, 160, 128, color_blend(theme_get_bg(), 0x0000, 0.8f));

    if (_state == State::Scanning) {
        _draw_header(c, "SCANNING WI-FI", theme_get_accent());
        _draw_spinner(c, 80, 64, 18);
    }
    else if (_state == State::Connecting) {
        _draw_header(c, "CONNECTING...", theme_get_accent());
        _draw_spinner(c, 80, 64, 18);
        c->setTextDatum(textdatum_t::middle_center);
        c->setFont(&fonts::Font0);
        c->setTextColor(theme_get_text_dim());
        c->drawString(_selected_ssid, 80, 95);
    }
    else if (_state == State::List) {
        _draw_header(c, "SELECT NETWORK", theme_get_accent());
        
        if (_network_count == 0) {
            c->setTextDatum(textdatum_t::middle_center);
            c->setTextColor(theme_get_text_dim());
            c->drawString("No networks found.", 80, 64);
        } else {
            for (int row = 0; row < 4; row++) {
                int i = _list_scroll + row;
                if (i >= _network_count) break;
                
                int y = 18 + row * 25;
                bool sel = (i == _list_sel);
                
                uint16_t bg = sel ? theme_get_accent() : color_blend(theme_get_text_dim(), theme_get_bg(), 0.1f);
                uint16_t fg = sel ? theme_get_bg() : theme_get_text();
                
                c->fillRoundRect(4, y, 152, 22, 4, bg);
                c->setTextDatum(textdatum_t::middle_left);
                c->setFont(&fonts::Font0);
                c->setTextColor(fg);
                c->drawString(_networks[i].ssid, 10, y + 11);
                
                // Draw RSSI or lock icon hint
                if (_networks[i].authmode != WIFI_AUTH_OPEN) {
                    c->drawString("*", 140, y + 11);
                }
            }
        }
        c->setTextDatum(textdatum_t::bottom_center);
        c->setTextColor(theme_get_text_dim());
        c->drawString("L=Cancel  R=Rescan", 80, 126);
    }
    else if (_state == State::Password) {
        _keyboard.render(c);
    }
}
