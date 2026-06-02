#include "App_Grid.h"
#include "../theme_mgr.h"
#include "../../platform/board.h"
#include "../../hal/hal_gpio.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

extern void switch_to_app(IApp* app);

static const char* TAG = "APP_GRID";

static bool _is_btn_physically_held(input_event_t btn) {
    gpio_num_t pin;
    switch (btn) {
        case BTN_UP:     pin = (gpio_num_t)LAUNCHER_PIN_UP; break;
        case BTN_DOWN:   pin = (gpio_num_t)LAUNCHER_PIN_DOWN; break;
        case BTN_LEFT:   pin = (gpio_num_t)LAUNCHER_PIN_LEFT; break;
        case BTN_RIGHT:  pin = (gpio_num_t)LAUNCHER_PIN_RIGHT; break;
        case BTN_SELECT: pin = (gpio_num_t)LAUNCHER_PIN_SELECT; break;
        default: return false;
    }
    return hal_gpio_get_level(pin) == 0;
}

App_Grid::App_Grid() : app_count(0), selected_index(0),
    _held_btn(BTN_NONE), _held_delay(0), _held_repeat(0), _move_pending(false) {
    memset(apps, 0, sizeof(apps));
    anim_time = 0;
}

App_Grid::~App_Grid() {
    for (uint8_t i = 0; i < app_count; i++) {
        if (apps[i].cached_icon32) {
            delete[] apps[i].cached_icon32;
            apps[i].cached_icon32 = nullptr;
        }
    }
}

static void downsample_64_to_32(const uint16_t* src64, uint16_t* dst32) {
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            dst32[row * 32 + col] = src64[(row * 2) * 64 + (col * 2)];
        }
    }
}

void App_Grid::registerApp(const char* name, const uint16_t* icon, IApp* app) {
    if (app_count >= MAX_TOTAL_APPS) {
        ESP_LOGE(TAG, "Cannot register app '%s': max %d apps reached", name, MAX_TOTAL_APPS);
        return;
    }
    
    apps[app_count].name = name;
    apps[app_count].icon_rgb565 = icon;
    apps[app_count].app_instance = app;
    apps[app_count].cached_icon32 = nullptr;
    
    if (icon) {
        apps[app_count].cached_icon32 = new uint16_t[32 * 32];
        if (apps[app_count].cached_icon32) {
            downsample_64_to_32(icon, apps[app_count].cached_icon32);
        } else {
            ESP_LOGE(TAG, "Failed to allocate icon cache for '%s'", name);
        }
    }
    
    app_count++;
}

void App_Grid::init() {
    anim_time = 0;
    _held_btn = BTN_NONE;
    _held_delay = 0;
    _held_repeat = 0;
    _move_pending = false;
}

void App_Grid::exit() {
}

void App_Grid::_move(input_event_t dir) {
    if (app_count == 0) return;

    const uint8_t cols = 3;
    const uint8_t total_rows = (app_count + cols - 1) / cols;

    if (dir == BTN_RIGHT) {
        selected_index = (selected_index + 1) % app_count;
    }
    else if (dir == BTN_LEFT) {
        selected_index = (selected_index - 1 + app_count) % app_count;
    }
    else if (dir == BTN_DOWN) {
        const uint8_t row = selected_index / cols;
        const uint8_t col = selected_index % cols;
        const uint8_t next_row = (row + 1) % total_rows;
        uint8_t next_idx = next_row * cols + col;
        if (next_idx >= app_count) next_idx = app_count - 1;
        selected_index = next_idx;
    }
    else if (dir == BTN_UP) {
        const uint8_t row = selected_index / cols;
        const uint8_t col = selected_index % cols;
        const uint8_t prev_row = (row - 1 + total_rows) % total_rows;
        uint8_t prev_idx = prev_row * cols + col;
        if (prev_idx >= app_count) prev_idx = app_count - 1;
        selected_index = prev_idx;
    }
}

void App_Grid::loop() {
    anim_time += 0.04f;

    if (_held_btn != BTN_NONE) {
        if (!_is_btn_physically_held(_held_btn)) {
            _held_btn = BTN_NONE;
            _held_delay = 0;
            _held_repeat = 0;
            return;
        }
        _held_delay++;
        if (_held_delay > 15) {
            _held_repeat++;
            if (_held_repeat % 3 == 0) {
                _move(_held_btn);
            }
        }
    }
}

void App_Grid::handleInput(input_event_t event) {
    ESP_LOGD(TAG, "handleInput: ev=%d held=%d delay=%d repeat=%d idx=%d apps=%d",
        event, _held_btn, _held_delay, _held_repeat, selected_index, app_count);

    if (event == BTN_SELECT) {
        _held_btn = BTN_NONE;
        _held_delay = 0;
        _held_repeat = 0;
        if (app_count > 0 && apps[selected_index].app_instance != nullptr) {
            switch_to_app(apps[selected_index].app_instance);
        }
        return;
    }

    if (event == BTN_UP || event == BTN_DOWN || event == BTN_LEFT || event == BTN_RIGHT) {
        _held_btn = event;
        _held_delay = 0;
        _held_repeat = 0;
        _move(event);
    }
}

void App_Grid::render(LGFX_Sprite* canvas) {
    const uint16_t bg = theme_get_bg();
    theme_draw_wallpaper(canvas, anim_time);

    if (app_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("NO APPS", canvas->width()/2, canvas->height()/2);
        return;
    }

    const uint8_t sel_col = selected_index % 3;
    const uint8_t sel_row = selected_index / 3;
    
    constexpr int icon_size = 32;
    constexpr int gap_x = 16;
    constexpr int gap_y = 10;
    
    const int start_x = (canvas->width() - (3 * icon_size + 2 * gap_x)) / 2;
    const int start_y = (canvas->height() - (3 * icon_size + 2 * gap_y)) / 2 + 5;
    
    const int sel_x = start_x + sel_col * (icon_size + gap_x);
    const int sel_y = start_y + sel_row * (icon_size + gap_y);

    const uint16_t acc = theme_get_accent();

    const uint8_t ar = ((acc >> 11) & 0x1F) << 3;
    const uint8_t ag = ((acc >> 5)  & 0x3F) << 2;
    const uint8_t ab = (acc & 0x1F) << 3;

    auto blend_with_bg = [&](uint8_t alpha) -> uint16_t {
        const uint8_t br = ((bg >> 11) & 0x1F) << 3;
        const uint8_t bg_g = ((bg >> 5) & 0x3F) << 2;
        const uint8_t bg_b = (bg & 0x1F) << 3;
        const uint8_t mr = br + ((int32_t)(ar - br) * alpha / 255);
        const uint8_t mg = bg_g + ((int32_t)(ag - bg_g) * alpha / 255);
        const uint8_t mb = bg_b + ((int32_t)(ab - bg_b) * alpha / 255);
        return canvas->color565(mr, mg, mb);
    };

    canvas->fillRect(sel_x - 4, sel_y - 4, icon_size + 8, icon_size + 8, blend_with_bg(30));
    canvas->drawRect(sel_x - 7, sel_y - 7, icon_size + 14, icon_size + 14, blend_with_bg(40));
    canvas->drawRect(sel_x - 5, sel_y - 5, icon_size + 10, icon_size + 10, blend_with_bg(80));
    canvas->drawRect(sel_x - 4, sel_y - 4, icon_size + 8,  icon_size + 8, blend_with_bg(130));
    canvas->drawRect(sel_x - 3, sel_y - 3, icon_size + 6, icon_size + 6, acc);

    const int tick = 5 + (int)(1.5f * (sinf(anim_time * 2.2f) * 0.5f + 0.5f) * 2.0f);
    const int bx0 = sel_x - 4, by0 = sel_y - 4;
    const int bx1 = sel_x + icon_size + 3, by1 = sel_y + icon_size + 3;

    canvas->drawFastHLine(bx0, by0, tick, acc);
    canvas->drawFastVLine(bx0, by0, tick, acc);
    canvas->drawFastHLine(bx1 - tick + 1, by0, tick, acc);
    canvas->drawFastVLine(bx1, by0, tick, acc);
    canvas->drawFastHLine(bx0, by1, tick, acc);
    canvas->drawFastVLine(bx0, by1 - tick + 1, tick, acc);
    canvas->drawFastHLine(bx1 - tick + 1, by1, tick, acc);
    canvas->drawFastVLine(bx1, by1 - tick + 1, tick, acc);

    const float pulse = sinf(anim_time * 3.0f);
    if (pulse > 0.0f) {
        const uint16_t pip = blend_with_bg((uint8_t)(pulse * 220));
        canvas->fillCircle(bx1 + 1, by0 - 1, 2, pip);
        canvas->fillCircle(bx1 + 1, by0 - 1, 1, acc);
    }

    for (uint8_t i = 0; i < app_count; i++) {
        const int x = start_x + (i % 3) * (icon_size + gap_x);
        const int y = start_y + (i / 3) * (icon_size + gap_y);
        
        if (apps[i].cached_icon32 != nullptr) {
            if (i == selected_index) {
                LGFX_Sprite temp_sp(canvas);
                temp_sp.setPsram(false);
                if (temp_sp.createSprite(32, 32)) {
                    temp_sp.pushImage(0, 0, 32, 32, apps[i].cached_icon32);
                    temp_sp.pushRotateZoom(x + 16, y + 16, 0, 1.15f, 1.15f);
                    temp_sp.deleteSprite();
                } else {
                    canvas->pushImage(x, y, 32, 32, apps[i].cached_icon32);
                }
            } else {
                canvas->pushImage(x, y, 32, 32, apps[i].cached_icon32);
            }
        } else {
            canvas->fillRect(x, y, 32, 32, canvas->color565(30, 40, 50));
        }
        
        if (i == selected_index) {
            canvas->setFont(&fonts::Font0);
            canvas->setTextColor(theme_get_text());
            canvas->setTextDatum(textdatum_t::bottom_center);
            canvas->drawString(apps[i].name, canvas->width()/2, canvas->height() - 8);
        }
    }

    const uint8_t total_pages = (app_count + MAX_APPS_PER_PAGE - 1) / MAX_APPS_PER_PAGE;
    if (total_pages > 1) {
        const int dot_start_x = (canvas->width() - (total_pages * 8)) / 2;
        for (uint8_t p = 0; p < total_pages; p++) {
            const uint16_t color = (p == selected_index / MAX_APPS_PER_PAGE)
                ? theme_get_text() : theme_get_text_dim();
            canvas->fillCircle(dot_start_x + (p * 8) + 4, canvas->height() - 19, 2, color);
        }
    }
}
