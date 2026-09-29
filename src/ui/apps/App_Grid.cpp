#include "App_Grid.h"
#include "../theme_mgr.h"
#include "../../config/sys_config.h"
#include "../../platform/board.h"
#include "../../managers/input_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>

#include "../ui_task.h"

static const char* TAG = "APP_GRID";

static bool _is_btn_physically_held(input_event_t btn) {
    return input_mgr_is_button_held(btn);
}

App_Grid::App_Grid() : _app_count(0), _selected_index(0), _current_page(0),
    _current_scroll(0.0f), _target_scroll(0.0f),
    _held_btn(BTN_NONE), _held_delay(0), _held_repeat(0),
    _launcher_theme(LAUNCHER_CAROUSEL) {
    memset(apps, 0, sizeof(apps));
    _anim_time = 0;
}

App_Grid::~App_Grid() {
    for (uint8_t i = 0; i < _app_count; i++) {
        if (apps[i].cached_icon32) {
            heap_caps_free(apps[i].cached_icon32);
            apps[i].cached_icon32 = nullptr;
        }
    }
    /* The icon sprites were previously never released — a 10 KB leak masked
     * only by this being an immortal singleton. */
    if (_sp64) { _sp64->deleteSprite(); delete _sp64; _sp64 = nullptr; }
    if (_sp32) { _sp32->deleteSprite(); delete _sp32; _sp32 = nullptr; }
}

static void downsample_64_to_32(const uint16_t* src64, uint16_t* dst32) {
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            dst32[row * 32 + col] = src64[(row * 2) * 64 + (col * 2)];
        }
    }
}

/* Single lazy-creation point for the icon scratch sprites.
 * WHY one helper: this logic was previously spelled out three times inside
 * render(), each a copy-paste of `new LGFX_Sprite(canvas); setPsram(true);
 * createSprite(...)`. None of the three checked createSprite()'s return
 * value — on a PSRAM shortfall createSprite() returns nullptr and the first
 * pushImage() into it faults. Centralising means the check exists once.
 * The null-guard makes allocation one-shot; the renderers already null-check
 * before use. */
void App_Grid::_ensure_sprites(LGFX_Sprite* canvas) {
    if (!_sp64) {
        _sp64 = new LGFX_Sprite(canvas);
        _sp64->setPsram(true);
        _sp64->setColorDepth(16);
        if (!_sp64->createSprite(64, 64)) {
            ESP_LOGE(TAG, "64x64 icon sprite alloc failed");
            delete _sp64;
            _sp64 = nullptr;
        }
    }
    if (!_sp32) {
        _sp32 = new LGFX_Sprite(canvas);
        _sp32->setPsram(true);
        _sp32->setColorDepth(16);
        if (!_sp32->createSprite(32, 32)) {
            ESP_LOGE(TAG, "32x32 icon sprite alloc failed");
            delete _sp32;
            _sp32 = nullptr;
        }
    }
}

void App_Grid::registerApp(const char* name, const uint16_t* icon, IApp* app, const char* desc, const char* category) {
    if (_app_count >= MAX_TOTAL_APPS) {
        ESP_LOGE(TAG, "Cannot register app '%s': max %d apps reached", name, MAX_TOTAL_APPS);
        return;
    }
    
    apps[_app_count].name = name;
    apps[_app_count].icon_rgb565 = icon;
    apps[_app_count].app_instance = app;
    apps[_app_count].desc = desc;
    apps[_app_count].category = category;
    apps[_app_count].cached_icon32 = nullptr;
    
    if (icon) {
        apps[_app_count].cached_icon32 = (uint16_t*)heap_caps_malloc(32 * 32 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (apps[_app_count].cached_icon32) {
            downsample_64_to_32(icon, apps[_app_count].cached_icon32);
        } else {
            ESP_LOGE(TAG, "Failed to allocate icon cache for '%s'", name);
        }
    }
    
    _app_count++;
}

void App_Grid::init() {
    _anim_time = 0;
    _current_scroll = _selected_index;
    _target_scroll = _selected_index;
    _held_btn = BTN_NONE;
    _held_delay = 0;
    _held_repeat = 0;
    
    // Load launcher theme from NVS
    nvs_handle_t nvs_h;
    if (nvs_open("ui_cfg", NVS_READONLY, &nvs_h) == ESP_OK) {
        uint8_t theme_val = 0;
        if (nvs_get_u8(nvs_h, "launcher_theme", &theme_val) == ESP_OK) {
            _launcher_theme = (launcher_theme_t)theme_val;
        }
        nvs_close(nvs_h);
    }
}

void App_Grid::setLauncherTheme(launcher_theme_t theme) {
    _launcher_theme = theme;
    
    // Persist to NVS
    nvs_handle_t nvs_h;
    if (nvs_open("ui_cfg", NVS_READWRITE, &nvs_h) == ESP_OK) {
        nvs_set_u8(nvs_h, "launcher_theme", (uint8_t)theme);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
    }
}

void App_Grid::exit() {
}

void App_Grid::_move(input_event_t dir) {
    if (_app_count == 0) return;

    if (_launcher_theme == LAUNCHER_GRID) {
        // Grid navigation: 3 columns × 2 rows
        const int COLS = 3;
        const int APPS_PER_PAGE = 6;
        
        int current_page = _selected_index / APPS_PER_PAGE;
        int page_start = current_page * APPS_PER_PAGE;
        int local_idx = _selected_index - page_start;
        int row = local_idx / COLS;
        int col = local_idx % COLS;
        
        if (dir == BTN_UP) {
            if (row > 0) {
                // Move up in same column
                _selected_index -= COLS;
            } else if (current_page > 0) {
                // Jump to previous page, same column.
                // WHY the signed temporary: _selected_index is uint8_t, so the
                // unsigned subtraction wraps (2 - 6 -> 252) and the previous
                // `if (_selected_index < 0)` guard could never fire, because an
                // unsigned value is never < 0. That left apps[] indexed far
                // past its 12 entries. Compute in a signed int and clamp.
                int next = (int)_selected_index - APPS_PER_PAGE;
                if (next < 0) next = 0;
                if (next >= (int)_app_count) next = (int)_app_count - 1;
                _selected_index = (uint8_t)next;
            }
        }
        else if (dir == BTN_DOWN) {
            if (row < 1 && _selected_index + COLS < _app_count) {
                // Move down in same column
                _selected_index += COLS;
            } else if ((current_page + 1) * APPS_PER_PAGE < _app_count) {
                // Jump to next page, same column. Same signed-arithmetic
                // reasoning as the UP case — the unsigned result is clamped
                // here too so it cannot wrap past _app_count.
                int next = (int)_selected_index + APPS_PER_PAGE;
                if (next >= (int)_app_count) next = (int)_app_count - 1;
                if (next < 0) next = 0;
                _selected_index = (uint8_t)next;
            }
        }
        else if (dir == BTN_LEFT) {
            if (col > 0) {
                _selected_index--;
            }
        }
        else if (dir == BTN_RIGHT) {
            if (col < COLS - 1 && _selected_index + 1 < _app_count && local_idx + 1 < APPS_PER_PAGE) {
                _selected_index++;
            }
        }
    } else {
        // Carousel/Tile navigation (existing logic)
        if (dir == BTN_DOWN) {
            if (_selected_index < _app_count - 1) _selected_index++;
            else _selected_index = 0;
        }
        else if (dir == BTN_UP) {
            if (_selected_index > 0) _selected_index--;
            else _selected_index = _app_count - 1;
        }
        else if (dir == BTN_RIGHT) {
            // Page down
            int next = _selected_index + 3;
            if (next >= _app_count) next = _app_count - 1;
            _selected_index = next;
        }
        else if (dir == BTN_LEFT) {
            // Page up
            int prev = _selected_index - 3;
            if (prev < 0) prev = 0;
            _selected_index = prev;
        }

        _target_scroll = _selected_index;
        
        // Handle wrap-around distance for animation
        if (_target_scroll == 0 && _current_scroll > _app_count - 2) {
             _current_scroll -= _app_count; // Seamless scroll forward
        } else if (_target_scroll == _app_count - 1 && _current_scroll < 1) {
             _current_scroll += _app_count; // Seamless scroll backward
        }
    }
}

void App_Grid::loop() {
    _anim_time += UI_ANIM_TIME_STEP;
    
    // Spring physics for scroll
    float diff = _target_scroll - _current_scroll;
    _current_scroll += diff * 0.2f;
    

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
            if (_held_repeat % 4 == 0) {
                _move(_held_btn);
            }
        }
    }
}

void App_Grid::handleInput(input_event_t event) {
    ESP_LOGD(TAG, "handleInput: ev=%d idx=%d", event, _selected_index);

    if (event == BTN_SELECT) {
        _held_btn = BTN_NONE;
        _held_delay = 0;
        _held_repeat = 0;
        
        // Ensure we wait for scroll to settle slightly before launching
        if (_app_count > 0 && apps[_selected_index].app_instance != nullptr && fabsf(_target_scroll - _current_scroll) < 0.5f) {
            switch_to_app(apps[_selected_index].app_instance);
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
    if (_launcher_theme == LAUNCHER_CAROUSEL) {
        _render_carousel(canvas);
    } else if (_launcher_theme == LAUNCHER_TILE) {
        _render_tile(canvas);
    } else {
        _render_grid(canvas);
    }
}

void App_Grid::_render_carousel(LGFX_Sprite* canvas) {
    const uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);
    theme_draw_wallpaper(canvas, _anim_time);

    if (_app_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("NO APPS", canvas->width()/2, canvas->height()/2);
        return;
    }

    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t CY = H / 2;
    
    const uint16_t acc = theme_get_accent();

    auto blend_with_bg = [&](uint16_t color, uint8_t alpha) -> uint16_t {
        if (alpha == 255) return color;
        if (alpha == 0) return bg;
        const uint8_t ar = ((color >> 11) & 0x1F) << 3;
        const uint8_t ag = ((color >> 5)  & 0x3F) << 2;
        const uint8_t ab = (color & 0x1F) << 3;
        const uint8_t br = ((bg >> 11) & 0x1F) << 3;
        const uint8_t bg_g = ((bg >> 5) & 0x3F) << 2;
        const uint8_t bg_b = (bg & 0x1F) << 3;
        const uint8_t mr = br + ((int32_t)(ar - br) * alpha / 255);
        const uint8_t mg = bg_g + ((int32_t)(ag - bg_g) * alpha / 255);
        const uint8_t mb = bg_b + ((int32_t)(ab - bg_b) * alpha / 255);
        return canvas->color565(mr, mg, mb);
    };

    // Draw scrollbar indicator
    const int scrollbar_h = H - 40;
    const int scrollbar_y = 20;
    const int scrollbar_x = W - 6;
    canvas->drawFastVLine(scrollbar_x, scrollbar_y, scrollbar_h, blend_with_bg(theme_get_text_dim(), 50));
    
    float scroll_pct = (_app_count > 1) ? (_current_scroll / (_app_count - 1)) : 0.0f;
    if (scroll_pct < 0) scroll_pct = 0;
    if (scroll_pct > 1) scroll_pct = 1;
    
    int indicator_h = 16;
    int indicator_y = scrollbar_y + (int)((scrollbar_h - indicator_h) * scroll_pct);
    canvas->fillRoundRect(scrollbar_x - 1, indicator_y, 3, indicator_h, 1, theme_get_text());

    struct RenderItem {
        int index;
        float distance;
        float abs_dist;
    };
    
    RenderItem items[MAX_TOTAL_APPS];
    int render_count = 0;
    
    for (int i = 0; i < _app_count; i++) {
        float dist = i - _current_scroll;
        if (dist > _app_count / 2.0f) dist -= _app_count;
        if (dist < -_app_count / 2.0f) dist += _app_count;
        
        float abs_dist = fabsf(dist);
        if (abs_dist < 3.5f) {
            items[render_count++] = {i, dist, abs_dist};
        }
    }
    
    // Sort items by abs_dist descending (farthest first)
    for (int i = 0; i < render_count - 1; i++) {
        for (int j = 0; j < render_count - i - 1; j++) {
            if (items[j].abs_dist < items[j+1].abs_dist) {
                RenderItem temp = items[j];
                items[j] = items[j+1];
                items[j+1] = temp;
            }
        }
    }
    
    _ensure_sprites(canvas);

    auto apply_corners = [&](LGFX_Sprite* sp, int w, int h, int r, uint16_t color) {
        for (int y=0; y<r; y++) {
            for (int x=0; x<r; x++) {
                if ((r-x)*(r-x) + (r-y)*(r-y) >= r*r) {
                    sp->drawPixel(x, y, color);
                    sp->drawPixel(w-1-x, y, color);
                    sp->drawPixel(x, h-1-y, color);
                    sp->drawPixel(w-1-x, h-1-y, color);
                }
            }
        }
    };

    for (int i = 0; i < render_count; i++) {
        int idx = items[i].index;
        float dist = items[i].distance;
        float abs_dist = items[i].abs_dist;
        
        const int spacing = 45; 
        float y = CY + dist * spacing;
        
        float curve_x = CX - 30;
        float x = curve_x - (dist * dist * 4.5f);
        
        if (abs_dist <= 0.5f) {
            
            int pill_w = 140;
            int pill_h = 52;  // Increased from 48 for better balance
            int pill_x = CX - pill_w/2;
            int pill_y = y - pill_h/2;
            
            canvas->fillRoundRect(pill_x, pill_y, pill_w, pill_h, 8, blend_with_bg(bg, 190));
            canvas->drawRoundRect(pill_x, pill_y, pill_w, pill_h, 8, blend_with_bg(acc, 180));
            
            int icon_cx = pill_x + 24;  // Adjusted from 28 for better centering
            
            if (apps[idx].icon_rgb565) {
                float scale = 0.65f + (1.0f - abs_dist*2.0f) * 0.05f;  // Max 0.70, ~45px rendered
                if (_sp64) {
                    _sp64->pushImage(0, 0, 64, 64, apps[idx].icon_rgb565);
                    apply_corners(_sp64, 64, 64, 12, blend_with_bg(bg, 190));
                    _sp64->pushRotateZoom(icon_cx, y, 0, scale, scale);
                } else if (apps[idx].cached_icon32) {
                    canvas->pushImage(icon_cx - 16, y - 16, 32, 32, apps[idx].cached_icon32);
                }
            }
            
            canvas->setFont(&fonts::Font2);
            canvas->setTextColor(theme_get_text());
            canvas->setTextDatum(textdatum_t::middle_left);
            canvas->drawString(apps[idx].name, pill_x + 52, y);  // Adjusted text offset
            
        } else {
            int alpha = 255 - (int)(abs_dist * 80);
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            
            uint16_t tint = blend_with_bg(theme_get_text_dim(), alpha);
            
            int icon_cx = x;
            if (apps[idx].cached_icon32) {
                 float scale = 1.0f - abs_dist * 0.15f;
                 if (scale < 0.45f) scale = 0.45f;  // Reduced floor from 0.5
                 
                 if (_sp32) {
                     _sp32->pushImage(0, 0, 32, 32, apps[idx].cached_icon32);
                     apply_corners(_sp32, 32, 32, 6, bg);
                     _sp32->pushRotateZoom(icon_cx, y, 0, scale, scale);
                 } else {
                     canvas->pushImage(icon_cx - 16, y - 16, 32, 32, apps[idx].cached_icon32);
                 }
            }
            
            canvas->setFont(&fonts::Font0);
            canvas->setTextColor(tint);
            canvas->setTextDatum(textdatum_t::middle_left);
            canvas->drawString(apps[idx].name, icon_cx + 24, y);
        }
    }
    
}

void App_Grid::_render_tile(LGFX_Sprite* canvas) {
    const uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);
    theme_draw_wallpaper(canvas, _anim_time);

    if (_app_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("NO APPS", canvas->width()/2, canvas->height()/2);
        return;
    }

    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const uint16_t acc = theme_get_accent();
    const uint16_t txt = theme_get_text();
    const uint16_t txt_dim = theme_get_text_dim();

    // Layout: 50px left column | 2px divider | remaining right panel
    const int LEFT_W = 50;
    const int RIGHT_X = LEFT_W + 2;
    const int RIGHT_W = W - RIGHT_X - 2;
    
    // Left column background
    float pulse = 0.5f + 0.5f * sinf(_anim_time * 2.5f);
    uint16_t panel_bg = color_blend(txt_dim, bg, 0.08f + pulse * 0.02f);
    canvas->fillRect(0, 0, LEFT_W, H, panel_bg);
    canvas->drawFastVLine(LEFT_W, 0, H, color_blend(txt_dim, bg, 0.35f));
    canvas->drawFastVLine(LEFT_W + 1, 0, H, color_blend(txt_dim, bg, 0.15f));
    
    // Icon list parameters
    const int ITEM_H = 34;
    const int ICON_SIZE = 30;
    int visible_count = H / ITEM_H;
    
    int scroll_offset = 0;
    if (_selected_index >= visible_count - 1) {
        scroll_offset = _selected_index - visible_count + 2;
    }
    if (scroll_offset < 0) scroll_offset = 0;
    
    // Render icons in left column
    for (int i = scroll_offset; i < _app_count && (i - scroll_offset) < visible_count; i++) {
        int item_y = 2 + (i - scroll_offset) * ITEM_H;
        bool is_sel = (i == _selected_index);
        
        if (is_sel) {
            float glow = 0.75f + 0.25f * sinf(_anim_time * 5.0f);
            int border_w = 2;
            canvas->fillRoundRect(3, item_y, LEFT_W - 6, ITEM_H - 2, 6, color_blend(acc, bg, 0.18f * glow));
            canvas->drawRoundRect(3, item_y, LEFT_W - 6, ITEM_H - 2, 6, color_blend(acc, txt, 0.9f * glow));
            canvas->drawRoundRect(3 + border_w, item_y + border_w, LEFT_W - 6 - border_w*2, ITEM_H - 2 - border_w*2, 5, color_blend(acc, txt, 0.5f * glow));
        }
        
        // Render icon
        int icon_x = (LEFT_W - ICON_SIZE) / 2;
        int icon_y = item_y + (ITEM_H - ICON_SIZE) / 2;
        
        if (apps[i].cached_icon32) {
            _ensure_sprites(canvas);
            
            if (_sp32) {
                _sp32->pushImage(0, 0, 32, 32, apps[i].cached_icon32);
                
                auto apply_corners = [&](LGFX_Sprite* sp, int w, int h, int r, uint16_t color) {
                    for (int y=0; y<r; y++) {
                        for (int x=0; x<r; x++) {
                            if ((r-x)*(r-x) + (r-y)*(r-y) >= r*r) {
                                sp->drawPixel(x, y, color);
                                sp->drawPixel(w-1-x, y, color);
                                sp->drawPixel(x, h-1-y, color);
                                sp->drawPixel(w-1-x, h-1-y, color);
                            }
                        }
                    }
                };
                
                uint16_t corner_col = is_sel ? color_blend(acc, bg, 0.18f * (0.75f + 0.25f * sinf(_anim_time * 5.0f))) : panel_bg;
                apply_corners(_sp32, 32, 32, 7, corner_col);
                
                float scale = (float)ICON_SIZE / 32.0f;
                _sp32->pushRotateZoom(icon_x + ICON_SIZE/2, icon_y + ICON_SIZE/2, 0, scale, scale);
            }
        }
    }
    
    // Scroll indicators
    if (scroll_offset > 0) {
        canvas->setTextColor(acc);
        canvas->setTextDatum(textdatum_t::top_center);
        int bounce = (int)(2.0f * sinf(_anim_time * 6.0f));
        canvas->drawString("^", LEFT_W/2, 1 - bounce);
    }
    if (scroll_offset + visible_count < _app_count) {
        canvas->setTextColor(acc);
        canvas->setTextDatum(textdatum_t::bottom_center);
        int bounce = (int)(2.0f * sinf(_anim_time * 6.0f));
        canvas->drawString("v", LEFT_W/2, H - 1 + bounce);
    }
    
    // Right panel: Selected app details
    if (_selected_index < _app_count) {
        const AppEntry& app = apps[_selected_index];
        
        // Panel background
        uint16_t panel_fill = color_blend(txt_dim, bg, 0.12f);
        canvas->fillRoundRect(RIGHT_X, 2, RIGHT_W, H - 4, 7, panel_fill);
        canvas->drawRoundRect(RIGHT_X, 2, RIGHT_W, H - 4, 7, color_blend(txt_dim, bg, 0.4f));
        
        int panel_cx = RIGHT_X + RIGHT_W / 2;
        int content_y = 6;
        const int USABLE_H = H - 12;  // 6px top + 6px bottom margin
        
        // Hero icon (42×42 to fit better)
        const int HERO_ICON = 42;
        
        if (app.icon_rgb565) {
            _ensure_sprites(canvas);
            
            if (_sp64) {
                _sp64->pushImage(0, 0, 64, 64, app.icon_rgb565);
                
                auto apply_corners = [&](LGFX_Sprite* sp, int w, int h, int r, uint16_t color) {
                    for (int y=0; y<r; y++) {
                        for (int x=0; x<r; x++) {
                            if ((r-x)*(r-x) + (r-y)*(r-y) >= r*r) {
                                sp->drawPixel(x, y, color);
                                sp->drawPixel(w-1-x, y, color);
                                sp->drawPixel(x, h-1-y, color);
                                sp->drawPixel(w-1-x, h-1-y, color);
                            }
                        }
                    }
                };
                
                apply_corners(_sp64, 64, 64, 11, panel_fill);
                
                float scale = (float)HERO_ICON / 64.0f;
                _sp64->pushRotateZoom(panel_cx, content_y + HERO_ICON/2, 0, scale, scale);
            }
        }
        
        content_y += HERO_ICON + 4;
        
        // App name
        canvas->setFont(&fonts::Font2);
        canvas->setTextColor(txt);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString(app.name, panel_cx, content_y);
        content_y += 11;
        
        // Separator with glow
        float glow = 0.65f + 0.35f * sinf(_anim_time * 3.0f);
        int sep_w = RIGHT_W - 16;
        canvas->drawFastHLine(RIGHT_X + 8, content_y, sep_w, color_blend(acc, bg, 0.55f * glow));
        content_y += 3;
        
        // Category (compact single line)
        if (app.category) {
            canvas->setFont(&fonts::Font0);
            canvas->setTextColor(color_blend(acc, txt, 0.75f));
            canvas->setTextDatum(textdatum_t::top_center);
            canvas->drawString(app.category, panel_cx, content_y);
            content_y += 9;
        }
        
        // Description with proper word-wrap and line limiting
        if (app.desc) {
            const int MAX_DESC_LINES = 3;
            const int LINE_H = 9;
            const int DESC_MAX_Y = 6 + USABLE_H - 8;  // Reserve 8px bottom
            
            canvas->setFont(&fonts::Font0);
            canvas->setTextColor(txt_dim);
            canvas->setTextDatum(textdatum_t::top_left);
            
            int desc_x = RIGHT_X + 6;
            int desc_w = RIGHT_W - 12;
            int lines_rendered = 0;
            
            const char* p = app.desc;
            char line_buf[48];
            int line_idx = 0;
            
            while (*p && lines_rendered < MAX_DESC_LINES && content_y + LINE_H <= DESC_MAX_Y) {
                // Build word
                while (*p && *p != ' ' && line_idx < 47) {
                    line_buf[line_idx++] = *p++;
                }
                
                // Check if adding space + next word would overflow
                if (*p == ' ') {
                    line_buf[line_idx++] = ' ';
                    p++;
                }
                
                line_buf[line_idx] = '\0';
                
                // If line exceeds width, render accumulated text and start new line
                if (canvas->textWidth(line_buf) > desc_w || *p == '\0') {
                    // Trim trailing space
                    if (line_idx > 0 && line_buf[line_idx-1] == ' ') {
                        line_buf[line_idx-1] = '\0';
                    }
                    
                    canvas->drawString(line_buf, desc_x, content_y);
                    content_y += LINE_H;
                    lines_rendered++;
                    line_idx = 0;
                }
            }
        }
        
        // Clean bottom accent bar
        int bar_y = H - 8;
        int bar_w = RIGHT_W - 20;
        int bar_x = panel_cx - bar_w/2;
        float bar_pulse = 0.7f + 0.3f * sinf(_anim_time * 3.5f);
        canvas->fillRoundRect(bar_x, bar_y, bar_w, 2, 1, color_blend(acc, txt, bar_pulse));
    }
}

void App_Grid::_render_grid(LGFX_Sprite* canvas) {
    const uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);
    theme_draw_wallpaper(canvas, _anim_time);

    if (_app_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("NO APPS", canvas->width()/2, canvas->height()/2);
        return;
    }

    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const uint16_t acc = theme_get_accent();
    const uint16_t txt = theme_get_text();
    const uint16_t txt_dim = theme_get_text_dim();

    // 3×2 grid layout
    const int COLS = 3;
    const int ROWS = 2;
    const int APPS_PER_PAGE = COLS * ROWS;
    
    // Calculate grid metrics with margins
    const int MARGIN_X = 8;
    const int MARGIN_Y = 10;
    const int SPACING_X = 6;
    const int SPACING_Y = 8;
    const int FOOTER_H = 16;  // Increased for app name display
    
    const int GRID_W = W - (MARGIN_X * 2);
    const int GRID_H = H - MARGIN_Y - FOOTER_H;
    
    const int CELL_W = (GRID_W - (SPACING_X * (COLS - 1))) / COLS;
    const int CELL_H = (GRID_H - (SPACING_Y * (ROWS - 1))) / ROWS;
    
    // Calculate page from selected index
    _current_page = _selected_index / APPS_PER_PAGE;
    int page_start = _current_page * APPS_PER_PAGE;
    int page_end = page_start + APPS_PER_PAGE;
    if (page_end > _app_count) page_end = _app_count;
    
    // Render grid items
    for (int i = page_start; i < page_end; i++) {
        int grid_idx = i - page_start;
        int col = grid_idx % COLS;
        int row = grid_idx / COLS;
        
        int cell_x = MARGIN_X + col * (CELL_W + SPACING_X);
        int cell_y = MARGIN_Y + row * (CELL_H + SPACING_Y);
        
        bool is_sel = (i == _selected_index);
        
        // Cell background with selection state
        if (is_sel) {
            float glow = 0.8f + 0.2f * sinf(_anim_time * 5.0f);
            canvas->fillRoundRect(cell_x - 1, cell_y - 1, CELL_W + 2, CELL_H + 2, 6, color_blend(acc, bg, 0.15f * glow));
            canvas->fillRoundRect(cell_x, cell_y, CELL_W, CELL_H, 5, color_blend(acc, bg, 0.1f));
            canvas->drawRoundRect(cell_x, cell_y, CELL_W, CELL_H, 5, color_blend(acc, txt, 0.85f * glow));
            canvas->drawRoundRect(cell_x + 1, cell_y + 1, CELL_W - 2, CELL_H - 2, 4, color_blend(acc, txt, 0.4f * glow));
        } else {
            canvas->fillRoundRect(cell_x, cell_y, CELL_W, CELL_H, 5, color_blend(txt_dim, bg, 0.06f));
            canvas->drawRoundRect(cell_x, cell_y, CELL_W, CELL_H, 5, color_blend(txt_dim, bg, 0.25f));
        }
        
        // Icon rendering (32×32 centered in cell)
        const int ICON_SIZE = 32;
        int icon_cx = cell_x + CELL_W / 2;
        int icon_cy = cell_y + CELL_H / 2;  // Fully centered now
        
        if (apps[i].cached_icon32) {
            _ensure_sprites(canvas);
            
            if (_sp32) {
                _sp32->pushImage(0, 0, 32, 32, apps[i].cached_icon32);
                
                auto apply_corners = [&](LGFX_Sprite* sp, int w, int h, int r, uint16_t color) {
                    for (int y=0; y<r; y++) {
                        for (int x=0; x<r; x++) {
                            if ((r-x)*(r-x) + (r-y)*(r-y) >= r*r) {
                                sp->drawPixel(x, y, color);
                                sp->drawPixel(w-1-x, y, color);
                                sp->drawPixel(x, h-1-y, color);
                                sp->drawPixel(w-1-x, h-1-y, color);
                            }
                        }
                    }
                };
                
                uint16_t corner_col = is_sel ? color_blend(acc, bg, 0.1f) : color_blend(txt_dim, bg, 0.06f);
                apply_corners(_sp32, 32, 32, 6, corner_col);
                
                _sp32->pushSprite(icon_cx - ICON_SIZE/2, icon_cy - ICON_SIZE/2);
            }
        }
        
    }
    
    // Selected app name at bottom center
    canvas->setFont(&fonts::Font2);
    canvas->setTextColor(txt);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString(apps[_selected_index].name, W / 2, H - 2);
    
    // Page indicator at bottom
    int total_pages = (_app_count + APPS_PER_PAGE - 1) / APPS_PER_PAGE;
    if (total_pages > 1) {
        int dot_spacing = 8;
        int indicator_w = total_pages * dot_spacing;
        int indicator_x = W / 2 - indicator_w / 2;
        int indicator_y = H - 5;
        
        for (int p = 0; p < total_pages; p++) {
            int dot_x = indicator_x + p * dot_spacing;
            if (p == _current_page) {
                float pulse = 0.75f + 0.25f * sinf(_anim_time * 4.0f);
                canvas->fillCircle(dot_x, indicator_y, 3, color_blend(acc, txt, pulse));
                canvas->drawCircle(dot_x, indicator_y, 4, color_blend(acc, txt, 0.6f * pulse));
            } else {
                canvas->fillCircle(dot_x, indicator_y, 2, color_blend(txt_dim, bg, 0.4f));
            }
        }
    }
}
