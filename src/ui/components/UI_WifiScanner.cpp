#include "UI_WifiScanner.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "../../config/sys_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char* TAG = "UI_WIFI";

UI_WifiScanner::UI_WifiScanner() 
    : _active(false), _state(State::Idle), _anim_time(0), _spinner_angle(0),
      _network_count(0), _connect_start_ms(0) {}
UI_WifiScanner::~UI_WifiScanner() {}

void UI_WifiScanner::begin(std::function<void(bool)> on_complete) {
    _active = true;
    _on_complete = on_complete;
    _state = State::Scanning;
    _network_count = 0;
    _list_sel = 0;
    _list_scroll = 0;
    _connect_success = false;
    _connect_start_ms = 0;
    _scan_start_ms = 0;
    _fail_reason = nullptr;
    memset(_selected_ssid, 0, sizeof(_selected_ssid));
    memset(_password, 0, sizeof(_password));

    wifi_mgr_scan_start();
}

/* Map a driver disconnect reason to user-facing text, or nullptr when the
 * code is not a definitive connect failure (transient codes must not abort
 * a still-running attempt). */
static const char* _reason_to_text(wifi_err_reason_t r) {
    switch (r) {
        case WIFI_REASON_NO_AP_FOUND:            return "Network not in range";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "Wrong password";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "Auth failed";
        case WIFI_REASON_AUTH_EXPIRE:            return "Auth expired";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_CONNECTION_FAIL:        return "Join failed";
        default: return nullptr;
    }
}

void UI_WifiScanner::loop() {
    if (!_active) return;
    _anim_time += UI_ANIM_TIME_STEP;
    _spinner_angle += 0.15f;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (_state == State::Scanning) {
        if (wifi_mgr_is_scan_done()) {
            _network_count = wifi_mgr_get_scan_results(_networks, MAX_WIFI_NETWORKS);
            _state = State::List;
        } else {
            /* WHY the timeout: wifi_mgr now guarantees a done-marker even
             * when the scan cannot start, but this belt-and-braces bound
             * means the list can never spin forever regardless of what the
             * radio layer does. It did exactly that before — no timeout
             * existed on this state at all. */
            if (_scan_start_ms == 0) _scan_start_ms = now_ms;
            if ((now_ms - _scan_start_ms) >= WIFI_SCANNER_SCAN_TIMEOUT_MS) {
                ESP_LOGW(TAG, "scan timeout — showing empty list");
                _network_count = 0;
                _state = State::List;
            }
        }
    }
    else if (_state == State::Connecting) {
        if (wifi_mgr_is_connected()) {
            _connect_success = true;
            _state = State::Done;
        } else {
            /* Fail fast on a definitive reason instead of burning the whole
             * timeout — a wrong password is known within ~2s, not 10. */
            const char* why = _reason_to_text(wifi_mgr_get_last_fail_reason());
            if (why) {
                ESP_LOGW(TAG, "connect failed fast: %s", why);
                _fail_reason = why;
                _state = State::Failed;
            } else {
                if (_connect_start_ms == 0) _connect_start_ms = now_ms;
                if ((now_ms - _connect_start_ms) >= WIFI_SCANNER_CONNECT_TIMEOUT_MS) {
                    ESP_LOGW(TAG, "connect timeout for %s", _selected_ssid);
                    _fail_reason = "Timed out";
                    _state = State::Failed;
                }
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
            _scan_start_ms = 0;
            wifi_mgr_scan_start();
        }
        else if (ev == BTN_SELECT) {
            strncpy(_selected_ssid, _networks[_list_sel].ssid, sizeof(_selected_ssid)-1);
            if (_networks[_list_sel].authmode == WIFI_AUTH_OPEN) {
                wifi_mgr_connect(_selected_ssid, "");
                _connect_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
                _state = State::Connecting;
            } else {
                _state = State::Password;
                _keyboard.begin(
                    "Password:",
                    [this](const char* pwd) {
                        strncpy(this->_password, pwd, sizeof(this->_password)-1);
                        wifi_mgr_connect(this->_selected_ssid, this->_password);
                        this->_connect_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
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
    else if (_state == State::Failed) {
        if (ev == BTN_LEFT) {
            _connect_success = false;
            _state = State::Done;
        }
        else if (ev == BTN_SELECT || ev == BTN_RIGHT) {
            /* Retry the same SSID/password — a fresh attempt with the failure
             * reason cleared and the timeout restarted. */
            _fail_reason = nullptr;
            _connect_start_ms = 0;
            wifi_mgr_connect(_selected_ssid, _password);
            _connect_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
            _state = State::Connecting;
        }
    }
}

void UI_WifiScanner::_draw_header(LGFX_Sprite* c, const char* title, uint16_t accent) {
    const int32_t W = c->width();
    c->fillRect(0, 0, W, 14, theme_get_bg());
    c->drawFastHLine(0, 14, W, accent);
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
    c->fillRect(0, 0, c->width(), c->height(), color_blend(theme_get_bg(), RGB565_BLACK, 0.8f));

    if (_state == State::Scanning) {
        _draw_header(c, "SCANNING WI-FI", theme_get_accent());
        _draw_spinner(c, c->width()/2, c->height()/2, 18);
        c->setTextDatum(textdatum_t::bottom_center);
        c->setFont(&fonts::Font0);
        c->setTextColor(theme_get_text_dim());
        c->drawString("L=Cancel", c->width()/2, c->height()-2);
    }
    else if (_state == State::Connecting) {
        _draw_header(c, "CONNECTING...", theme_get_accent());
        _draw_spinner(c, c->width()/2, c->height()/2, 18);
        c->setTextDatum(textdatum_t::middle_center);
        c->setFont(&fonts::Font0);
        c->setTextColor(theme_get_text_dim());
        c->drawString(_selected_ssid, c->width()/2, c->height() * 3/4);
        c->setTextDatum(textdatum_t::bottom_center);
        c->drawString("L=Cancel", c->width()/2, c->height()-2);
    }
    else if (_state == State::Failed) {
        _draw_header(c, "CONNECT FAILED", theme_get_fail());

        c->setTextDatum(textdatum_t::middle_center);
        c->setFont(&fonts::Font2);
        c->setTextColor(theme_get_fail());
        c->drawString(_fail_reason ? _fail_reason : "Failed",
                      c->width()/2, c->height()/2 - 10);

        c->setFont(&fonts::Font0);
        c->setTextColor(theme_get_text_dim());
        c->drawString(_selected_ssid, c->width()/2, c->height()/2 + 6);

        c->setTextColor(theme_get_text_dim());
        c->setTextDatum(textdatum_t::bottom_center);
        c->drawString("SEL=Retry  L=Back", c->width()/2, c->height()-2);
    }
    else if (_state == State::List) {
        _draw_header(c, "SELECT NETWORK", theme_get_accent());

        if (_network_count == 0) {
            c->setTextDatum(textdatum_t::middle_center);
            c->setTextColor(theme_get_text_dim());
            c->drawString("No networks found.", c->width()/2, c->height()/2);
        } else {
            for (int row = 0; row < 4; row++) {
                int i = _list_scroll + row;
                if (i >= _network_count) break;

                int y = 18 + row * 25;
                bool sel = (i == _list_sel);

                uint16_t bg = sel ? theme_get_accent() : color_blend(theme_get_text_dim(), theme_get_bg(), 0.1f);
                uint16_t fg = sel ? theme_get_bg() : theme_get_text();

                c->fillRoundRect(4, y, c->width()-8, 22, 4, bg);
                c->setTextDatum(textdatum_t::middle_left);
                c->setFont(&fonts::Font0);
                c->setTextColor(fg);
                c->drawString(_networks[i].ssid, 10, y + 11);

                // Draw RSSI or lock icon hint
                if (_networks[i].authmode != WIFI_AUTH_OPEN) {
                    c->drawString("*", c->width()-20, y + 11);
                }
            }
        }
        c->setTextDatum(textdatum_t::bottom_center);
        c->setTextColor(theme_get_text_dim());
        c->drawString("L=Cancel  R=Rescan", c->width()/2, c->height()-2);
    }
    else if (_state == State::Password) {
        _keyboard.render(c);
    }
}
