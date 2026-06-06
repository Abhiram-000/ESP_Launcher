#include "App_Torch.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "../../services/led_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "APP_TORCH";

extern void switch_to_grid();

const char* App_Torch::RANDOM_NAMES[32] = {
    "ALP", "NEB", "ZEN", "VEX", "KRY", "FLX", "HEX", "ORI",
    "PUL", "RAZ", "SOL", "TIT", "VOR", "WAV", "ZAP", "BLS",
    "CRX", "DRF", "ELY", "FNX", "GEM", "HMX", "ION", "JET",
    "KRN", "LMN", "MIR", "NRG", "OPT", "PRX", "QRT", "RIF"
};

static const char* MODE_NAMES[12] = {
    "WHITE", "RED BEACON", "GREEN BEACON", "BLUE BEACON",
    "RAINBOW", "CYBER PULSE", "SOS ALERT", "APEX SIGNAL",
    "STROBE", "POLICE", "CANDLE", "DISCO"
};

static inline uint16_t make_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

App_Torch::App_Torch() {
    _state = STATE_MAIN;
    _prev_state = STATE_MAIN;
    _anim_time = 0.0f;
    _current_mode = MODE_WHITE;
    _led_on = false;
    _pulse_size = 1.0f;
    _orb_glow = 0.0f;
    _mode_cursor = 0;
    _mode_scroll = 0;
    _brightness = 255;
    _brightness_preset = 3;
    memset(_custom_modes, 0, sizeof(_custom_modes));
    _custom_cursor = 0;
    _custom_edit_slot = 0;
    _step_cursor = 0;
    _edit_component = 0;
    _settings.max_brightness = 255;
    _settings.auto_off_ms = 0;
    _settings.strobe_safe = true;
    _adv_cursor = 0;
}

App_Torch::~App_Torch() {}

void App_Torch::init() {
    led_service_init();
    _load_settings();
    _load_custom_modes();
    _state = STATE_MAIN;
    _led_on = false;
    _anim_time = 0.0f;
    _pulse_size = 1.0f;
    _orb_glow = 0.0f;
    _brightness = _settings.max_brightness;
    led_service_set_brightness(_brightness);
    _update_led();
}

void App_Torch::exit() {
    led_service_off();
}

void App_Torch::_hsv_to_rgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s == 0.0f) {
        r = g = b = (uint8_t)(v * 255.0f);
        return;
    }
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    h /= 60.0f;
    int32_t i = (int32_t)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float rf = 0, gf = 0, bf = 0;
    switch (i) {
        case 0: rf = v; gf = t; bf = p; break;
        case 1: rf = q; gf = v; bf = p; break;
        case 2: rf = p; gf = v; bf = t; break;
        case 3: rf = p; gf = q; bf = v; break;
        case 4: rf = t; gf = p; bf = v; break;
        default: rf = v; gf = p; bf = q; break;
    }
    r = (uint8_t)(rf * 255.0f);
    g = (uint8_t)(gf * 255.0f);
    b = (uint8_t)(bf * 255.0f);
}

uint16_t App_Torch::_get_mode_color(Mode m, float t) {
    uint8_t r, g, b;
    switch (m) {
        case MODE_WHITE:        return theme_get_text();
        case MODE_RED_BEACON:   return theme_get_fail();
        case MODE_GREEN_BEACON: return theme_get_success();
        case MODE_BLUE_BEACON:  return theme_get_accent();
        case MODE_RAINBOW:
            _hsv_to_rgb(fmodf(t * 60.0f, 360.0f), 1.0f, 1.0f, r, g, b);
            return make_rgb565(r, g, b);
        case MODE_CYBER_PULSE: {
            float intensity = (sinf(t * 3.0f) + 1.0f) * 0.5f;
            return color_blend(theme_get_accent(), theme_get_bg(), 0.4f + intensity * 0.6f);
        }
        case MODE_SOS: {
            float tt = fmodf(t * 2.0f, 32.0f);
            bool on = false;
            if (tt < 6.0f) on = (fmodf(tt, 2.0f) < 1.0f);
            else if (tt >= 8.0f && tt < 20.0f) on = (fmodf(tt - 8.0f, 4.0f) < 3.0f);
            else if (tt >= 22.0f && tt < 28.0f) on = (fmodf(tt - 22.0f, 2.0f) < 1.0f);
            return on ? theme_get_text() : theme_get_text_dim();
        }
        case MODE_APEX_SIGNAL: {
            float tt = fmodf(t, 4.0f);
            if (tt < 1.0f) return theme_get_fail();
            if (tt < 2.0f) return theme_get_accent();
            if (tt < 3.0f) return theme_get_success();
            return theme_get_text_dim();
        }
        case MODE_STROBE:
            return (fmodf(t * 8.0f, 2.0f) < 1.0f) ? theme_get_text() : theme_get_text_dim();
        case MODE_POLICE:
            return (fmodf(t * 2.0f, 2.0f) < 1.0f) ? theme_get_fail() : theme_get_accent();
        case MODE_CANDLE: {
            float flicker = 0.8f + 0.2f * sinf(t * 12.0f) * sinf(t * 7.3f);
            r = (uint8_t)(255 * flicker);
            g = (uint8_t)(180 * flicker * 0.7f);
            b = (uint8_t)(80 * flicker * 0.3f);
            return make_rgb565(r, g, b);
        }
        case MODE_DISCO: {
            float hue = fmodf(floorf(t / 0.3f) * 0.3f * 47.0f, 360.0f);
            _hsv_to_rgb(hue, 1.0f, 1.0f, r, g, b);
            return make_rgb565(r, g, b);
        }
        default: return theme_get_text_dim();
    }
}

void App_Torch::_pick_random_name(char* buf, size_t len) {
    bool used[32] = {};
    for (int32_t i = 0; i < 4; i++) {
        if (_custom_modes[i].active) {
            for (int32_t j = 0; j < 32; j++) {
                if (strcmp(_custom_modes[i].name, RANDOM_NAMES[j]) == 0) {
                    used[j] = true;
                    break;
                }
            }
        }
    }
    int32_t unused[32];
    int32_t count = 0;
    for (int32_t i = 0; i < 32; i++) {
        if (!used[i]) unused[count++] = i;
    }
    if (count > 0) {
        strlcpy(buf, RANDOM_NAMES[unused[rand() % count]], len);
    } else {
        strlcpy(buf, "???", len);
    }
}

void App_Torch::_update_led() {
    if (!_led_on) {
        led_service_off();
        return;
    }
    uint8_t r = 0, g = 0, b = 0;
    float t = _anim_time;
    switch (_current_mode) {
        case MODE_WHITE:
            r = g = b = 255;
            break;
        case MODE_RED_BEACON:
            r = 255;
            break;
        case MODE_GREEN_BEACON:
            g = 255;
            break;
        case MODE_BLUE_BEACON:
            b = 255;
            break;
        case MODE_RAINBOW:
            _hsv_to_rgb(fmodf(t * 60.0f, 360.0f), 1.0f, 1.0f, r, g, b);
            break;
        case MODE_CYBER_PULSE: {
            float intensity = (sinf(t * 3.0f) + 1.0f) * 0.5f;
            uint16_t acc = theme_get_accent();
            r = (uint8_t)(((acc >> 11) & 0x1F) * 8.0f * intensity);
            g = (uint8_t)(((acc >> 5) & 0x3F) * 4.0f * intensity);
            b = (uint8_t)((acc & 0x1F) * 8.0f * intensity);
            break;
        }
        case MODE_SOS: {
            float tt = fmodf(t * 2.0f, 32.0f);
            bool on = false;
            if (tt < 6.0f) on = (fmodf(tt, 2.0f) < 1.0f);
            else if (tt >= 8.0f && tt < 20.0f) on = (fmodf(tt - 8.0f, 4.0f) < 3.0f);
            else if (tt >= 22.0f && tt < 28.0f) on = (fmodf(tt - 22.0f, 2.0f) < 1.0f);
            if (on) r = g = b = 255;
            break;
        }
        case MODE_APEX_SIGNAL: {
            float tt = fmodf(t, 4.0f);
            if (tt < 1.0f) r = 255;
            else if (tt < 2.0f) { r = 255; g = 120; }
            else if (tt < 3.0f) g = 255;
            break;
        }
        case MODE_STROBE:
            if (fmodf(t * 8.0f, 2.0f) < 1.0f) r = g = b = 255;
            break;
        case MODE_POLICE:
            if (fmodf(t * 2.0f, 2.0f) < 1.0f) r = 255;
            else b = 255;
            break;
        case MODE_CANDLE: {
            float flicker = 0.8f + 0.2f * sinf(t * 12.0f) * sinf(t * 7.3f);
            r = (uint8_t)(255 * flicker);
            g = (uint8_t)(180 * flicker * 0.7f);
            b = (uint8_t)(80 * flicker * 0.3f);
            break;
        }
        case MODE_DISCO: {
            float hue = fmodf(floorf(t / 0.3f) * 0.3f * 47.0f, 360.0f);
            _hsv_to_rgb(hue, 1.0f, 1.0f, r, g, b);
            break;
        }
        default: break;
    }
    led_service_set_rgb(r, g, b);
}

void App_Torch::_save_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("torch_cfg", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for settings: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, "max_bright", _settings.max_brightness);
    nvs_set_u32(h, "auto_off", _settings.auto_off_ms);
    nvs_set_u8(h, "strobe_safe", _settings.strobe_safe ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

void App_Torch::_load_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("torch_cfg", NVS_READONLY, &h);
    if (err != ESP_OK) return;
    uint8_t mb;
    if (nvs_get_u8(h, "max_bright", &mb) == ESP_OK) _settings.max_brightness = mb;
    uint32_t ao;
    if (nvs_get_u32(h, "auto_off", &ao) == ESP_OK) _settings.auto_off_ms = ao;
    uint8_t ss;
    if (nvs_get_u8(h, "strobe_safe", &ss) == ESP_OK) _settings.strobe_safe = (ss != 0);
    nvs_close(h);
}

void App_Torch::_save_custom_modes() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("torch_cfg", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for custom modes: %s", esp_err_to_name(err));
        return;
    }
    for (int32_t i = 0; i < 4; i++) {
        char key[12];
        snprintf(key, sizeof(key), "custom_%d", (int)i);
        nvs_set_blob(h, key, &_custom_modes[i], sizeof(custom_mode_t));
    }
    nvs_commit(h);
    nvs_close(h);
}

void App_Torch::_load_custom_modes() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("torch_cfg", NVS_READONLY, &h);
    if (err != ESP_OK) return;
    for (int32_t i = 0; i < 4; i++) {
        char key[12];
        snprintf(key, sizeof(key), "custom_%d", (int)i);
        size_t sz = sizeof(custom_mode_t);
        nvs_get_blob(h, key, &_custom_modes[i], &sz);
    }
    nvs_close(h);
}

void App_Torch::loop() {
    _anim_time += 0.025f;
    _pulse_size += (1.0f - _pulse_size) * 0.15f;
    _orb_glow += (0.0f - _orb_glow) * 0.1f;
    
    if (_state == STATE_MODES && _led_on) {
        Mode preview_mode = (Mode)_mode_cursor;
        uint8_t r = 0, g = 0, b = 0;
        float t = _anim_time;
        
        switch (preview_mode) {
            case MODE_WHITE:
                r = g = b = 255;
                break;
            case MODE_RED_BEACON:
                r = 255;
                break;
            case MODE_GREEN_BEACON:
                g = 255;
                break;
            case MODE_BLUE_BEACON:
                b = 255;
                break;
            case MODE_RAINBOW:
                _hsv_to_rgb(fmodf(t * 60.0f, 360.0f), 1.0f, 1.0f, r, g, b);
                break;
            case MODE_CYBER_PULSE: {
                float intensity = (sinf(t * 3.0f) + 1.0f) * 0.5f;
                uint16_t acc = theme_get_accent();
                r = (uint8_t)(((acc >> 11) & 0x1F) * 8.0f * intensity);
                g = (uint8_t)(((acc >> 5) & 0x3F) * 4.0f * intensity);
                b = (uint8_t)((acc & 0x1F) * 8.0f * intensity);
                break;
            }
            case MODE_SOS: {
                float tt = fmodf(t * 2.0f, 32.0f);
                bool on = false;
                if (tt < 6.0f) on = (fmodf(tt, 2.0f) < 1.0f);
                else if (tt >= 8.0f && tt < 20.0f) on = (fmodf(tt - 8.0f, 4.0f) < 3.0f);
                else if (tt >= 22.0f && tt < 28.0f) on = (fmodf(tt - 22.0f, 2.0f) < 1.0f);
                if (on) r = g = b = 255;
                break;
            }
            case MODE_APEX_SIGNAL: {
                float tt = fmodf(t, 4.0f);
                if (tt < 1.0f) r = 255;
                else if (tt < 2.0f) { r = 255; g = 120; }
                else if (tt < 3.0f) g = 255;
                break;
            }
            case MODE_STROBE:
                if (fmodf(t * 8.0f, 2.0f) < 1.0f) r = g = b = 255;
                break;
            case MODE_POLICE:
                if (fmodf(t * 2.0f, 2.0f) < 1.0f) r = 255;
                else b = 255;
                break;
            case MODE_CANDLE: {
                float flicker = 0.8f + 0.2f * sinf(t * 12.0f) * sinf(t * 7.3f);
                r = (uint8_t)(255 * flicker);
                g = (uint8_t)(180 * flicker * 0.7f);
                b = (uint8_t)(80 * flicker * 0.3f);
                break;
            }
            case MODE_DISCO: {
                float hue = fmodf(floorf(t / 0.3f) * 0.3f * 47.0f, 360.0f);
                _hsv_to_rgb(hue, 1.0f, 1.0f, r, g, b);
                break;
            }
            default: break;
        }
        led_service_set_rgb(r, g, b);
    } else if (_led_on) {
        _update_led();
    }
}

void App_Torch::handleInput(input_event_t event) {
    if (event == BTN_LEFT) {
        if (_state == STATE_MAIN) {
            switch_to_grid();
            return;
        }
        if (_state == STATE_CUSTOM_EDIT) {
            if (_step_cursor == 0 && _edit_component == 0) {
                _state = STATE_CUSTOM_LIST;
            } else {
                _edit_component = 0;
            }
        } else if (_state == STATE_MODES) {
            _state = STATE_MAIN;
        } else if (_state == STATE_BRIGHTNESS) {
            _state = STATE_MAIN;
        } else if (_state == STATE_CUSTOM_LIST) {
            _state = STATE_MAIN;
        } else if (_state == STATE_ADVANCED) {
            _state = STATE_MAIN;
        }
        return;
    }
    switch (_state) {
        case STATE_MAIN:        _handle_main(event); break;
        case STATE_MODES:       _handle_modes(event); break;
        case STATE_BRIGHTNESS:  _handle_brightness(event); break;
        case STATE_CUSTOM_LIST: _handle_custom_list(event); break;
        case STATE_CUSTOM_EDIT: _handle_custom_edit(event); break;
        case STATE_ADVANCED:    _handle_advanced(event); break;
    }
}

void App_Torch::_handle_main(input_event_t event) {
    switch (event) {
        case BTN_SELECT:
            _led_on = !_led_on;
            _pulse_size = 1.4f;
            _update_led();
            break;
        case BTN_RIGHT:
            _state = STATE_MODES;
            break;
        case BTN_UP: {
            uint8_t step = 16;
            if (_brightness + step > _settings.max_brightness) {
                _brightness = _settings.max_brightness;
            } else {
                _brightness += step;
            }
            led_service_set_brightness(_brightness);
            _brightness_preset = -1;
            if (_brightness == 64) _brightness_preset = 0;
            else if (_brightness == 128) _brightness_preset = 1;
            else if (_brightness == 192) _brightness_preset = 2;
            else if (_brightness == 255) _brightness_preset = 3;
            break;
        }
        case BTN_DOWN: {
            uint8_t step = 16;
            if (_brightness <= step) {
                _brightness = 16;
            } else {
                _brightness -= step;
            }
            led_service_set_brightness(_brightness);
            _brightness_preset = -1;
            if (_brightness == 64) _brightness_preset = 0;
            else if (_brightness == 128) _brightness_preset = 1;
            else if (_brightness == 192) _brightness_preset = 2;
            else if (_brightness == 255) _brightness_preset = 3;
            break;
        }
        default: break;
    }
}

void App_Torch::_handle_modes(input_event_t event) {
    switch (event) {
        case BTN_UP:
            _mode_cursor--;
            if (_mode_cursor < 0) _mode_cursor = MODE_COUNT - 1;
            break;
        case BTN_DOWN:
            _mode_cursor++;
            if (_mode_cursor >= MODE_COUNT) _mode_cursor = 0;
            break;
        case BTN_SELECT:
            _current_mode = (Mode)_mode_cursor;
            _state = STATE_MAIN;
            _update_led();
            break;
        default: break;
    }
}

void App_Torch::_handle_brightness(input_event_t event) {
    switch (event) {
        case BTN_LEFT:
            if (_brightness > 8) _brightness -= 8;
            else _brightness = 8;
            led_service_set_brightness(_brightness);
            break;
        case BTN_RIGHT:
            if (_brightness + 8 > _settings.max_brightness) {
                _brightness = _settings.max_brightness;
            } else {
                _brightness += 8;
            }
            led_service_set_brightness(_brightness);
            break;
        case BTN_UP:
            _brightness_preset++;
            if (_brightness_preset > 3) _brightness_preset = 0;
            if (_brightness_preset == 0) _brightness = 64;
            else if (_brightness_preset == 1) _brightness = 128;
            else if (_brightness_preset == 2) _brightness = 192;
            else _brightness = 255;
            if (_brightness > _settings.max_brightness) _brightness = _settings.max_brightness;
            led_service_set_brightness(_brightness);
            break;
        case BTN_DOWN:
            _brightness_preset--;
            if (_brightness_preset < 0) _brightness_preset = 3;
            if (_brightness_preset == 0) _brightness = 64;
            else if (_brightness_preset == 1) _brightness = 128;
            else if (_brightness_preset == 2) _brightness = 192;
            else _brightness = 255;
            if (_brightness > _settings.max_brightness) _brightness = _settings.max_brightness;
            led_service_set_brightness(_brightness);
            break;
        case BTN_SELECT:
            _state = STATE_MAIN;
            break;
        default: break;
    }
}

void App_Torch::_handle_custom_list(input_event_t event) {
    switch (event) {
        case BTN_UP:
            _custom_cursor--;
            if (_custom_cursor < 0) _custom_cursor = 3;
            break;
        case BTN_DOWN:
            _custom_cursor++;
            if (_custom_cursor > 3) _custom_cursor = 0;
            break;
        case BTN_SELECT:
            if (_custom_modes[_custom_cursor].active) {
                _custom_edit_slot = _custom_cursor;
                _step_cursor = 0;
                _edit_component = 0;
                _state = STATE_CUSTOM_EDIT;
            }
            break;
        case BTN_RIGHT: {
            int32_t slot = -1;
            for (int32_t i = 0; i < 4; i++) {
                if (!_custom_modes[i].active) { slot = i; break; }
            }
            if (slot < 0) break;
            memset(&_custom_modes[slot], 0, sizeof(custom_mode_t));
            _pick_random_name(_custom_modes[slot].name, sizeof(_custom_modes[slot].name));
            _custom_modes[slot].steps[0].r = 255;
            _custom_modes[slot].steps[0].g = 255;
            _custom_modes[slot].steps[0].b = 255;
            _custom_modes[slot].steps[0].duration_ms = 500;
            _custom_modes[slot].step_count = 1;
            _custom_modes[slot].repeat = true;
            _custom_modes[slot].active = true;
            _custom_edit_slot = slot;
            _step_cursor = 0;
            _edit_component = 0;
            _save_custom_modes();
            _state = STATE_CUSTOM_EDIT;
            break;
        }
        default: break;
    }
}

void App_Torch::_handle_custom_edit(input_event_t event) {
    custom_mode_t& cm = _custom_modes[_custom_edit_slot];
    switch (event) {
        case BTN_UP:
            _step_cursor--;
            if (_step_cursor < 0) _step_cursor = cm.step_count - 1;
            break;
        case BTN_DOWN:
            _step_cursor++;
            if (_step_cursor >= cm.step_count) _step_cursor = 0;
            break;
        case BTN_LEFT:
            _edit_component--;
            if (_edit_component < 0) _edit_component = 4;
            break;
        case BTN_RIGHT:
            _edit_component++;
            if (_edit_component > 4) _edit_component = 0;
            break;
        case BTN_SELECT: {
            custom_step_t& step = cm.steps[_step_cursor];
            if (_edit_component == 4) {
                cm.repeat = !cm.repeat;
            } else if (_edit_component == 3) {
                step.duration_ms += 100;
                if (step.duration_ms > 5000) step.duration_ms = 100;
            } else {
                uint8_t* comp = nullptr;
                if (_edit_component == 0) comp = &step.r;
                else if (_edit_component == 1) comp = &step.g;
                else if (_edit_component == 2) comp = &step.b;
                if (comp) {
                    *comp += 16;  /* uint8_t wraps 255→0 naturally */
                }
            }
            _save_custom_modes();
            break;
        }
        default: break;
    }
}

void App_Torch::_handle_advanced(input_event_t event) {
    switch (event) {
        case BTN_UP:
            _adv_cursor--;
            if (_adv_cursor < 0) _adv_cursor = 2;
            break;
        case BTN_DOWN:
            _adv_cursor++;
            if (_adv_cursor > 2) _adv_cursor = 0;
            break;
        case BTN_LEFT:
        case BTN_RIGHT: {
            int32_t dir = (event == BTN_RIGHT) ? 1 : -1;
            if (_adv_cursor == 0) {
                int32_t vals[] = {128, 160, 192, 224, 255};
                int32_t cur = 0;
                for (int32_t i = 0; i < 5; i++) {
                    if (vals[i] == _settings.max_brightness) { cur = i; break; }
                }
                cur += dir;
                if (cur < 0) cur = 4;
                if (cur > 4) cur = 0;
                _settings.max_brightness = vals[cur];
                if (_brightness > _settings.max_brightness) _brightness = _settings.max_brightness;
                led_service_set_brightness(_brightness);
            } else if (_adv_cursor == 1) {
                uint32_t vals[] = {0, 30000, 60000, 120000, 300000};
                int32_t cur = 0;
                for (int32_t i = 0; i < 5; i++) {
                    if (vals[i] == _settings.auto_off_ms) { cur = i; break; }
                }
                cur += dir;
                if (cur < 0) cur = 4;
                if (cur > 4) cur = 0;
                _settings.auto_off_ms = vals[cur];
            } else if (_adv_cursor == 2) {
                _settings.strobe_safe = !_settings.strobe_safe;
            }
            break;
        }
        case BTN_SELECT:
            _save_settings();
            _state = STATE_MAIN;
            break;
        default: break;
    }
}

void App_Torch::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    switch (_state) {
        case STATE_MAIN:        _render_main(canvas); break;
        case STATE_MODES:       _render_modes(canvas); break;
        case STATE_BRIGHTNESS:  _render_brightness(canvas); break;
        case STATE_CUSTOM_LIST: _render_custom_list(canvas); break;
        case STATE_CUSTOM_EDIT: _render_custom_edit(canvas); break;
        case STATE_ADVANCED:    _render_advanced(canvas); break;
    }
}

void App_Torch::_render_main(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;

    /* ── Enhanced Layout ─────────────────────────────────────────── */
    const int32_t BAR_W   = 12;
    const int32_t BAR_X   = MARGIN + 4;
    const int32_t BAR_TOP = 22;
    const int32_t BAR_BOT = H - 20;
    const int32_t BAR_H   = BAR_BOT - BAR_TOP;

    const int32_t ORB_AREA_L = BAR_X + BAR_W + 12;
    const int32_t ORB_AREA_R = W - MARGIN;
    int32_t orb_cx = ORB_AREA_L + (ORB_AREA_R - ORB_AREA_L) / 2;
    int32_t orb_cy = BAR_TOP + BAR_H / 2;
    int32_t orb_r  = 24;

    /* ── Percentage above bar with glow ──────────────────────────── */
    int32_t pct = (int32_t)((float)_brightness * 100.0f / 255.0f);
    char pct_buf[8];
    snprintf(pct_buf, sizeof(pct_buf), "%ld%%", (long)pct);
    canvas->setFont(&fonts::Font0);
    
    if (_led_on) {
        float glow_pulse = 0.3f + 0.15f * sinf(_anim_time * 3.0f);
        canvas->setTextColor(color_blend(theme_get_accent(), theme_get_bg(), glow_pulse));
        canvas->setTextDatum(textdatum_t::bottom_center);
        canvas->drawString(pct_buf, BAR_X + BAR_W / 2, BAR_TOP - 3);
    }
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString(pct_buf, BAR_X + BAR_W / 2, BAR_TOP - 4);

    /* ── Enhanced intensity bar with gradient ────────────────────── */
    uint16_t mode_color = _get_mode_color(_current_mode, _anim_time);
    
    if (_led_on) {
        float aura = 0.08f + 0.04f * sinf(_anim_time * 2.2f);
        canvas->fillRoundRect(BAR_X - 4, BAR_TOP - 4, BAR_W + 8, BAR_H + 8, 6,
                              color_blend(mode_color, theme_get_bg(), aura));
    }

    canvas->fillRoundRect(BAR_X, BAR_TOP, BAR_W, BAR_H, 4, 
                          color_blend(theme_get_text_dim(), theme_get_bg(), 0.2f));
    canvas->fillRoundRect(BAR_X + 1, BAR_TOP + 1, BAR_W - 2, BAR_H - 2, 3,
                          color_blend(theme_get_text_dim(), theme_get_bg(), 0.08f));

    int32_t fill_h = (int32_t)((float)BAR_H * (float)_brightness / 255.0f);
    if (fill_h > 0) {
        int32_t fill_y = BAR_BOT - fill_h;
        
        for (int32_t i = 0; i < fill_h; i += 2) {
            int32_t y = BAR_BOT - i - 1;
            float gradient = (float)i / (float)fill_h;
            uint16_t seg_color = color_blend(mode_color, theme_get_accent(), gradient * 0.6f);
            canvas->drawFastHLine(BAR_X + 2, y, BAR_W - 4, seg_color);
        }
        
        canvas->fillRoundRect(BAR_X + 2, fill_y, BAR_W - 4, 3, 1, theme_get_text());
        
        if (_led_on) {
            float shimmer = 0.4f + 0.2f * sinf(_anim_time * 4.5f);
            canvas->fillRoundRect(BAR_X + 3, fill_y + 1, BAR_W - 6, 1, 0,
                                  color_blend(theme_get_text(), mode_color, shimmer));
        }
    }

    int32_t ind_y = BAR_BOT - fill_h;
    if (_led_on) {
        float pulse = 1.0f + 0.3f * sinf(_anim_time * 5.0f);
        canvas->fillCircle(BAR_X + BAR_W / 2, ind_y, (int32_t)(6 * pulse), 
                          color_blend(mode_color, theme_get_bg(), 0.15f));
    }
    canvas->fillCircle(BAR_X + BAR_W / 2, ind_y, 5, theme_get_text());
    canvas->fillCircle(BAR_X + BAR_W / 2, ind_y, 3, mode_color);
    canvas->drawCircle(BAR_X + BAR_W / 2, ind_y, 5, theme_get_accent());

    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(color_blend(theme_get_text_dim(), theme_get_bg(), 0.3f));
    canvas->setTextDatum(textdatum_t::middle_center);
    int32_t bounce = (int32_t)(1.5f * sinf(_anim_time * 3.0f));
    canvas->drawString("^", BAR_X + BAR_W / 2, BAR_TOP - 12 - bounce);
    canvas->drawString("v", BAR_X + BAR_W / 2, BAR_BOT + 11 + bounce);

    /* ── Header ──────────────────────────────────────────────────── */
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("TORCH", MARGIN, 2);

    canvas->setTextDatum(textdatum_t::top_right);
    canvas->setTextColor(_led_on ? theme_get_success() : theme_get_text_dim());
    canvas->drawString(_led_on ? "ON" : "OFF", W - MARGIN, 2);

    /* ── Enhanced Orb with multi-layer glow ──────────────────────── */
    if (_led_on) {
        float pulse1 = sinf(_anim_time * 2.8f);
        float pulse2 = sinf(_anim_time * 3.5f + 1.0f);
        
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 18 + (int32_t)(pulse1 * 3),
                          color_blend(mode_color, theme_get_bg(), 0.06f));
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 14 + (int32_t)(pulse2 * 2),
                          color_blend(mode_color, theme_get_bg(), 0.12f));
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 9 + (int32_t)(pulse1 * 1.5f),
                          color_blend(mode_color, theme_get_bg(), 0.22f));
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 4,
                          color_blend(mode_color, theme_get_bg(), 0.35f));
    }

    float fill_alpha = _led_on ? 0.95f : 0.18f;
    canvas->fillCircle(orb_cx, orb_cy, orb_r, color_blend(mode_color, theme_get_bg(), fill_alpha));
    
    if (_led_on) {
        canvas->drawCircle(orb_cx, orb_cy, orb_r + 1, 
                          color_blend(theme_get_text(), mode_color, 0.7f));
        canvas->drawCircle(orb_cx, orb_cy, orb_r, mode_color);
    } else {
        canvas->drawCircle(orb_cx, orb_cy, orb_r, theme_get_text_dim());
    }

    int32_t hl_x = orb_cx - (int32_t)(orb_r * 0.35f);
    int32_t hl_y = orb_cy - (int32_t)(orb_r * 0.35f);
    int32_t hl_r = (int32_t)(orb_r * 0.35f);
    canvas->fillCircle(hl_x, hl_y, hl_r,
                      color_blend(theme_get_text(), mode_color, _led_on ? 0.65f : 0.08f));
    canvas->fillCircle(hl_x - 1, hl_y - 1, hl_r / 2,
                      color_blend(theme_get_text(), theme_get_bg(), _led_on ? 0.85f : 0.15f));

    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(_led_on ? theme_get_text() : theme_get_text_dim());
    canvas->drawString(MODE_NAMES[_current_mode], orb_cx, orb_cy + orb_r + 10);

    /* ── Footer ──────────────────────────────────────────────────── */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("SEL:ON  R:MOD", CX, H - 2);
}

void App_Torch::_render_modes(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ITEM_H = 18;
    const int32_t HEADER_H = 14;
    const int32_t FOOTER_H = 12;
    const int32_t LIST_Y = HEADER_H;
    const int32_t LIST_BOT = H - FOOTER_H;
    const int32_t VISIBLE_H = LIST_BOT - LIST_Y;
    const int32_t MAX_VISIBLE = VISIBLE_H / ITEM_H;

    if (_mode_cursor < _mode_scroll) {
        _mode_scroll = _mode_cursor;
    }
    if (_mode_cursor >= _mode_scroll + MAX_VISIBLE) {
        _mode_scroll = _mode_cursor - MAX_VISIBLE + 1;
    }

    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("MODES", MARGIN, 2);

    if (_mode_scroll > 0) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::top_center);
        int32_t bounce = (int32_t)(1.5f * sinf(_anim_time * 5.0f));
        canvas->drawString("^", CX, LIST_Y - 2 - bounce);
    }

    uint16_t preview_color = _get_mode_color((Mode)_mode_cursor, _anim_time);
    
    for (int32_t i = 0; i < MAX_VISIBLE; i++) {
        int32_t idx = _mode_scroll + i;
        if (idx >= MODE_COUNT) break;
        
        int32_t y = LIST_Y + i * ITEM_H;
        if (y + ITEM_H > LIST_BOT) break;
        
        bool sel = (idx == _mode_cursor);
        uint16_t dot_color = _get_mode_color((Mode)idx, _anim_time);

        if (sel) {
            float pulse = 0.75f + 0.25f * sinf(_anim_time * 4.5f);
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(preview_color, theme_get_bg(), 0.12f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(preview_color, theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
            
            int32_t arrow_bounce = (int32_t)(2.5f * sinf(_anim_time * 7.0f));
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->setTextColor(preview_color);
            canvas->drawString(">", W - MARGIN - 5 + arrow_bounce, y + ITEM_H / 2);
        } else {
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.04f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.12f));
            canvas->setTextColor(theme_get_text_dim());
        }

        if (sel) {
            float glow = 0.2f + 0.15f * sinf(_anim_time * 4.0f);
            canvas->fillCircle(MARGIN + 14, y + ITEM_H / 2, 5, 
                              color_blend(dot_color, theme_get_bg(), glow));
        }
        canvas->fillCircle(MARGIN + 14, y + ITEM_H / 2, 4, dot_color);
        canvas->drawCircle(MARGIN + 14, y + ITEM_H / 2, 4,
                          sel ? theme_get_text() : theme_get_text_dim());
        
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->drawString(MODE_NAMES[idx], MARGIN + 26, y + ITEM_H / 2);
    }

    if (_mode_scroll + MAX_VISIBLE < MODE_COUNT) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::bottom_center);
        int32_t bounce = (int32_t)(1.5f * sinf(_anim_time * 5.0f));
        canvas->drawString("v", CX, LIST_BOT + 2 + bounce);
    }

    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("SEL:Pick  L:Back", CX, H - 2);
}

void App_Torch::_render_brightness(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("BRIGHTNESS", MARGIN, 2);

    /* Large percentage */
    int32_t pct = (int32_t)((float)_brightness * 100.0f / 255.0f);
    char pct_buf[8];
    snprintf(pct_buf, sizeof(pct_buf), "%ld%%", (long)pct);
    canvas->setFont(&fonts::Font2);
    canvas->setTextColor(theme_get_text());
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString(pct_buf, CX, 30);

    /* Slider bar */
    int32_t bar_w = W * 70 / 100;
    int32_t bar_h = 8;
    int32_t bar_x = CX - bar_w / 2;
    int32_t bar_y = 44;

    canvas->fillRoundRect(bar_x, bar_y, bar_w, bar_h, 4, theme_get_text_dim());
    int32_t fill_w = (int32_t)((float)bar_w * (float)_brightness / 255.0f);
    if (fill_w > 0) {
        canvas->fillRoundRect(bar_x, bar_y, fill_w, bar_h, 4, theme_get_accent());
        canvas->fillRoundRect(bar_x + fill_w - 2, bar_y + 1, 3, bar_h - 2, 1, theme_get_text());
    }

    /* Preset boxes */
    const char* preset_labels[] = {"25%", "50%", "75%", "100%"};
    const int32_t preset_vals[] = {64, 128, 192, 255};
    const int32_t BOX_W = W / 5;
    const int32_t BOX_H = 14;
    const int32_t BOX_Y = 62;
    const int32_t BOX_SPACING = (W - 2 * MARGIN - 4 * BOX_W) / 3;

    for (int32_t i = 0; i < 4; i++) {
        int32_t bx = MARGIN + i * (BOX_W + BOX_SPACING);
        bool match = (_brightness == preset_vals[i]);

        if (match) {
            canvas->fillRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4, theme_get_accent());
            canvas->drawRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4, theme_get_text());
            canvas->setTextColor(theme_get_bg());
        } else {
            canvas->fillRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.08f));
            canvas->drawRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.2f));
            canvas->setTextColor(theme_get_text_dim());
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString(preset_labels[i], bx + BOX_W / 2, BOX_Y + BOX_H / 2);
    }

    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString("< L/R >", CX, BOX_Y + BOX_H + 8);

    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("SEL:OK  L:Back", CX, H - 2);
}

void App_Torch::_render_custom_list(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ITEM_H = 22;
    const int32_t LIST_Y = 16;

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("CUSTOM", MARGIN, 2);

    for (int32_t i = 0; i < 4; i++) {
        int32_t y = LIST_Y + i * (ITEM_H + 2);
        bool sel = (i == _custom_cursor);
        bool active = _custom_modes[i].active;

        if (sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_accent(), theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
            int32_t bounce = (int32_t)(2.0f * sinf(_anim_time * 6.0f));
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->setTextColor(theme_get_accent());
            canvas->drawString(">", W - MARGIN - 4 + bounce, y + ITEM_H / 2);
        } else {
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.06f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
            canvas->setTextColor(active ? theme_get_text() : theme_get_text_dim());
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        if (active) {
            canvas->drawString(_custom_modes[i].name, MARGIN + 8, y + ITEM_H / 2);
            int32_t dots_x = MARGIN + 40;
            for (int32_t s = 0; s < 8; s++) {
                uint16_t dot_col = (s < _custom_modes[i].step_count) ?
                    theme_get_accent() : theme_get_text_dim();
                canvas->fillCircle(dots_x + s * 6, y + ITEM_H / 2,
                                   (s < _custom_modes[i].step_count) ? 2 : 1, dot_col);
            }
            const char* rep = _custom_modes[i].repeat ? "LOOP" : "ONCE";
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->drawString(rep, W - MARGIN - 8, y + ITEM_H / 2);
        } else {
            canvas->drawString("--- empty ---", MARGIN + 8, y + ITEM_H / 2);
        }
    }

    /* Footer */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("R:New  SEL:Edit  L:Back", CX, H - 2);
}

void App_Torch::_render_custom_edit(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ROW_H = 16;
    const int32_t SLIDER_W = W * 40 / 100;
    const int32_t CONTENT_BOT = H - 16;  /* stop above footer */

    custom_mode_t& cm = _custom_modes[_custom_edit_slot];

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    char title[16];
    snprintf(title, sizeof(title), "EDIT: %s", cm.name);
    canvas->drawString(title, MARGIN, 2);

    /* Step indicator */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::top_center);
    char step_buf[24];
    snprintf(step_buf, sizeof(step_buf), "Step %ld/%d", (long)(_step_cursor + 1), cm.step_count);
    canvas->drawString(step_buf, CX, 14);

    /* Component rows */
    const char* labels[] = {"R", "G", "B", "Dur", "Rep"};
    uint16_t colors[] = {theme_get_fail(), theme_get_success(), theme_get_accent(),
                         theme_get_text(), theme_get_text_dim()};

    custom_step_t& step = cm.steps[_step_cursor];
    int32_t values[5] = {step.r, step.g, step.b, step.duration_ms, (int32_t)cm.repeat};
    int32_t max_vals[5] = {255, 255, 255, 5000, 1};

    int32_t row_y = 24;
    for (int32_t i = 0; i < 5; i++) {
        if (row_y + ROW_H > CONTENT_BOT) break;
        bool is_sel = (i == _edit_component);

        if (is_sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_accent(), theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
        } else {
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.06f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
            canvas->setTextColor(colors[i]);
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->drawString(labels[i], MARGIN + 4, row_y + ROW_H / 2);

        if (i < 4) {
            int32_t slider_x = MARGIN + 22;
            int32_t slider_y = row_y + ROW_H / 2 - 2;
            canvas->fillRoundRect(slider_x, slider_y, SLIDER_W, 4, 2, theme_get_text_dim());
            int32_t fill = (int32_t)((float)SLIDER_W * (float)values[i] / (float)max_vals[i]);
            if (fill > 0) {
                canvas->fillRoundRect(slider_x, slider_y, fill, 4, 2,
                                      is_sel ? theme_get_text() : colors[i]);
            }
            char val_buf[12];
            if (i == 3) snprintf(val_buf, sizeof(val_buf), "%ldms", (long)values[i]);
            else snprintf(val_buf, sizeof(val_buf), "%ld", (long)values[i]);
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->drawString(val_buf, W - MARGIN - 4, row_y + ROW_H / 2);
        } else {
            const char* rep_text = cm.repeat ? "LOOP" : "ONCE";
            canvas->drawString(rep_text, MARGIN + 22, row_y + ROW_H / 2);
        }
        row_y += ROW_H + 2;
    }

    /* Step dots — only if room */
    if (row_y + 8 <= CONTENT_BOT) {
        for (int32_t s = 0; s < cm.step_count; s++) {
            uint16_t dot_col = (s == _step_cursor) ? theme_get_accent() : theme_get_text_dim();
            int32_t dot_x = CX - (cm.step_count * 7) / 2 + s * 7;
            canvas->fillCircle(dot_x, row_y + 4, (s == _step_cursor) ? 3 : 2, dot_col);
        }
    }

    /* Footer */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("L/R:Val U/D:Row SEL:Edit", CX, H - 2);
}

void App_Torch::_render_advanced(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ROW_H = 24;

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("ADVANCED", MARGIN, 2);

    struct { const char* label; const char* value; } items[3];
    items[0].label = "Max Brightness";
    char mb_buf[8];
    snprintf(mb_buf, sizeof(mb_buf), "%d", _settings.max_brightness);
    items[0].value = mb_buf;
    items[1].label = "Auto-Off";
    if (_settings.auto_off_ms == 0) items[1].value = "OFF";
    else if (_settings.auto_off_ms == 30000) items[1].value = "30s";
    else if (_settings.auto_off_ms == 60000) items[1].value = "1m";
    else if (_settings.auto_off_ms == 120000) items[1].value = "2m";
    else items[1].value = "5m";
    items[2].label = "Strobe Safety";
    items[2].value = _settings.strobe_safe ? "ON" : "OFF";

    int32_t row_y = 16;
    for (int32_t i = 0; i < 3; i++) {
        bool is_sel = (i == _adv_cursor);

        if (is_sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_accent(), theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
        } else {
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.06f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
            canvas->setTextColor(theme_get_text_dim());
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->drawString(items[i].label, MARGIN + 8, row_y + ROW_H / 2 - 3);
        canvas->setTextDatum(textdatum_t::middle_right);
        canvas->drawString(items[i].value, W - MARGIN - 8, row_y + ROW_H / 2 - 3);

        if (i == 0) {
            int32_t bar_y = row_y + ROW_H / 2 + 3;
            int32_t bar_w = W * 50 / 100;
            canvas->fillRoundRect(CX - bar_w / 2, bar_y, bar_w, 3, 1, theme_get_text_dim());
            int32_t fill = (int32_t)((float)bar_w * (float)_settings.max_brightness / 255.0f);
            if (fill > 0) {
                canvas->fillRoundRect(CX - bar_w / 2, bar_y, fill, 3, 1,
                                      is_sel ? theme_get_text() : theme_get_accent());
            }
        }
        row_y += ROW_H + 4;
    }

    /* Footer */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("L/R:Change  SEL:Save", CX, H - 2);
}
