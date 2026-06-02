#include "App_Grid.h"
#include "../theme_mgr.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

extern void switch_to_app(IApp* app);

static const char* TAG = "APP_GRID";

App_Grid::App_Grid() : app_count(0), selected_index(0),
    _held_btn(BTN_NONE), _held_frames(0) {
    memset(apps, 0, sizeof(apps));
    cursor_x = 0;
    cursor_y = 0;
    target_cursor_x = 0;
    target_cursor_y = 0;
    cursor_vx = 0;
    cursor_vy = 0;
    cursor_w = 32;
    cursor_h = 32;
    target_cursor_w = 32;
    target_cursor_h = 32;
    cursor_vw = 0;
    cursor_vh = 0;
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
    _held_frames = 0;
}

void App_Grid::exit() {
}

void App_Grid::loop() {
    anim_time += 0.04f;
    
    const float ease = 0.45f;
    cursor_x += (target_cursor_x - cursor_x) * ease;
    cursor_y += (target_cursor_y - cursor_y) * ease;
    cursor_w += (target_cursor_w - cursor_w) * ease;
    cursor_h += (target_cursor_h - cursor_h) * ease;

    if (fabsf(cursor_x - target_cursor_x) < 0.5f) cursor_x = target_cursor_x;
    if (fabsf(cursor_y - target_cursor_y) < 0.5f) cursor_y = target_cursor_y;
    if (fabsf(cursor_w - target_cursor_w) < 0.5f) cursor_w = target_cursor_w;
    if (fabsf(cursor_h - target_cursor_h) < 0.5f) cursor_h = target_cursor_h;
}

void App_Grid::handleInput(input_event_t event) {
    if (app_count == 0) return;

    if (event != BTN_NONE) {
        if (event == _held_btn) {
            _held_frames++;
            if (_held_frames >= HOLD_DELAY &&
                (_held_frames - HOLD_DELAY) % HOLD_REPEAT == 0) {
            } else {
                return;
            }
        } else {
            _held_btn = event;
            _held_frames = 0;
        }
    } else {
        _held_btn = BTN_NONE;
        _held_frames = 0;
        return;
    }

    const uint8_t col = selected_index % 3;
    const uint8_t row = selected_index / 3;
    const uint8_t cols = 3;
    const uint8_t total_rows = (app_count + cols - 1) / cols;

    if (event == BTN_RIGHT) {
        if (col < cols - 1 && selected_index + 1 < app_count) {
            selected_index++;
        }
    }
    else if (event == BTN_LEFT) {
        if (col > 0) {
            selected_index--;
        }
    }
    else if (event == BTN_DOWN) {
        if (row < total_rows - 1) {
            uint8_t next = selected_index + cols;
            selected_index = (next < app_count) ? next : app_count - 1;
        }
    }
    else if (event == BTN_UP) {
        if (row > 0) {
            selected_index -= cols;
        }
    }
    else if (event == BTN_SELECT) {
        _held_btn = BTN_NONE;
        _held_frames = 0;
        if (apps[selected_index].app_instance != nullptr) {
            switch_to_app(apps[selected_index].app_instance);
        }
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

    const uint8_t page = selected_index / MAX_APPS_PER_PAGE;
    const uint8_t start_idx = page * MAX_APPS_PER_PAGE;
    uint8_t end_idx = start_idx + MAX_APPS_PER_PAGE;
    if (end_idx > app_count) end_idx = app_count;

    const uint8_t sel_idx_on_page = selected_index % MAX_APPS_PER_PAGE;
    const uint8_t sel_col = sel_idx_on_page % 3;
    const uint8_t sel_row = sel_idx_on_page / 3;
    
    constexpr int icon_size = 32;
    constexpr int gap_x = 16;
    constexpr int gap_y = 10;
    
    const int start_x = (canvas->width() - (3 * icon_size + 2 * gap_x)) / 2;
    const int start_y = (canvas->height() - (3 * icon_size + 2 * gap_y)) / 2 + 5;
    
    const float base_target_x = start_x + sel_col * (icon_size + gap_x);
    const float base_target_y = start_y + sel_row * (icon_size + gap_y);

    target_cursor_w = icon_size;
    target_cursor_h = icon_size;
    target_cursor_x = base_target_x - (target_cursor_w - icon_size) * 0.5f;
    target_cursor_y = base_target_y - (target_cursor_h - icon_size) * 0.5f;

    if (fabsf(cursor_x - base_target_x) > canvas->width() || cursor_w == 0) {
        cursor_x = base_target_x;
        cursor_y = base_target_y;
        cursor_w = icon_size;
        cursor_h = icon_size;
        target_cursor_x = base_target_x;
        target_cursor_y = base_target_y;
        target_cursor_w = icon_size;
        target_cursor_h = icon_size;
    }

    const int cx_draw = (int)cursor_x;
    const int cy_draw = (int)cursor_y;
    const int cw_draw = (int)cursor_w;
    const int ch_draw = (int)cursor_h;
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

    canvas->fillRect(cx_draw - 4, cy_draw - 4, cw_draw + 8, ch_draw + 8, blend_with_bg(30));
    canvas->drawRect(cx_draw - 7, cy_draw - 7, cw_draw + 14, ch_draw + 14, blend_with_bg(40));
    canvas->drawRect(cx_draw - 5, cy_draw - 5, cw_draw + 10, ch_draw + 10, blend_with_bg(80));
    canvas->drawRect(cx_draw - 4, cy_draw - 4, cw_draw + 8,  ch_draw + 8, blend_with_bg(130));
    canvas->drawRect(cx_draw - 3, cy_draw - 3, cw_draw + 6, ch_draw + 6, acc);

    const int tick = 5 + (int)(1.5f * (sinf(anim_time * 2.2f) * 0.5f + 0.5f) * 2.0f);
    const int bx0 = cx_draw - 4, by0 = cy_draw - 4;
    const int bx1 = cx_draw + cw_draw + 3, by1 = cy_draw + ch_draw + 3;

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

    for (uint8_t i = start_idx; i < end_idx; i++) {
        const uint8_t idx = i % MAX_APPS_PER_PAGE;
        const int x = start_x + (idx % 3) * (icon_size + gap_x);
        const int y = start_y + (idx / 3) * (icon_size + gap_y);
        
        if (apps[i].cached_icon32 != nullptr) {
            if (i == selected_index) {
                LGFX_Sprite temp_sp(canvas);
                temp_sp.setPsram(false);
                if (temp_sp.createSprite(32, 32)) {
                    temp_sp.pushImage(0, 0, 32, 32, apps[i].cached_icon32);
                    const float zoom = (cursor_w / (float)icon_size) * 1.15f; 
                    temp_sp.pushRotateZoom(x + 16, y + 16, 0, zoom, zoom);
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
            const uint16_t color = (p == page) ? theme_get_text() : theme_get_text_dim();
            canvas->fillCircle(dot_start_x + (p * 8) + 4, canvas->height() - 19, 2, color);
        }
    }
}
