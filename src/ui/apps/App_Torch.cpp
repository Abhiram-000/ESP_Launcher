#include "App_Torch.h"
#include "../theme_mgr.h"
#include "../../config/sys_config.h"
#include "../../utils/color_utils.h"
#include "../../services/led_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "APP_TORCH";

#include "../ui_task.h"

const char* App_Torch::RANDOM_NAMES[32] = {
    "ALP", "NEB", "ZEN", "VEX", "KRY", "FLX", "HEX", "ORI",
    "PUL", "RAZ", "SOL", "TIT", "VOR", "WAV", "ZAP", "BLS",
    "CRX", "DRF", "ELY", "FNX", "GEM", "HMX", "ION", "JET",
    "KRN", "LMN", "MIR", "NRG", "OPT", "PRX", "QRT", "RIF"
};

const char* MODE_NAMES[12] = {
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
    _anim_time += UI_ANIM_TIME_STEP;
    _pulse_size += (1.0f - _pulse_size) * 0.15f;
    
    /* WHY the range check: _mode_cursor can also point at one of the three
     * navigation pseudo-entries appended to this list, and those are not
     * Modes. Casting such a value to Mode would fall through the switch's
     * default and drive the LED black; preview the committed mode instead. */
    if (_state == STATE_MODES && _led_on && _mode_cursor < MODE_COUNT) {
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
            if (_mode_cursor < 0) _mode_cursor = MODE_LIST_TOTAL - 1;
            break;
        case BTN_DOWN:
            _mode_cursor++;
            if (_mode_cursor >= MODE_LIST_TOTAL) _mode_cursor = 0;
            break;
        case BTN_SELECT:
            /* Entries past the real modes navigate to the settings screens
             * instead of picking a lighting mode. */
            if (_mode_cursor == MODE_ENTRY_BRIGHTNESS) {
                _brightness_preset = -1;
                _state = STATE_BRIGHTNESS;
            } else if (_mode_cursor == MODE_ENTRY_CUSTOM) {
                _custom_cursor = 0;
                _state = STATE_CUSTOM_LIST;
            } else if (_mode_cursor == MODE_ENTRY_ADVANCED) {
                _adv_cursor = 0;
                _state = STATE_ADVANCED;
            } else {
                _current_mode = (Mode)_mode_cursor;
                _state = STATE_MAIN;
                _update_led();
            }
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

// Render functions moved to App_Torch_ui.cpp
