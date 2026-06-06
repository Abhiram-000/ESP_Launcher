#include "App_Grid.h"
#include "../theme_mgr.h"
#include "../../platform/board.h"
#include "../../managers/input_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>

extern void switch_to_app(IApp* app);

static const char* TAG = "APP_GRID";

static bool _is_btn_physically_held(input_event_t btn) {
    return input_mgr_is_button_held(btn);
}

App_Grid::App_Grid() : app_count(0), selected_index(0),
    _current_scroll(0.0f), _target_scroll(0.0f),
    _held_btn(BTN_NONE), _held_delay(0), _held_repeat(0), _move_pending(false),
    _launcher_theme(LAUNCHER_CAROUSEL) {
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

void App_Grid::registerApp(const char* name, const uint16_t* icon, IApp* app, const char* desc, const char* category) {
    if (app_count >= MAX_TOTAL_APPS) {
        ESP_LOGE(TAG, "Cannot register app '%s': max %d apps reached", name, MAX_TOTAL_APPS);
        return;
    }
    
    apps[app_count].name = name;
    apps[app_count].icon_rgb565 = icon;
    apps[app_count].app_instance = app;
    apps[app_count].desc = desc;
    apps[app_count].category = category;
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
    _current_scroll = selected_index;
    _target_scroll = selected_index;
    _held_btn = BTN_NONE;
    _held_delay = 0;
    _held_repeat = 0;
    _move_pending = false;
    
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
    if (app_count == 0) return;

    if (dir == BTN_DOWN) {
        if (selected_index < app_count - 1) selected_index++;
        else selected_index = 0;
    }
    else if (dir == BTN_UP) {
        if (selected_index > 0) selected_index--;
        else selected_index = app_count - 1;
    }
    else if (dir == BTN_RIGHT) {
        // Page down
        int next = selected_index + 3;
        if (next >= app_count) next = app_count - 1;
        selected_index = next;
    }
    else if (dir == BTN_LEFT) {
        // Page up
        int prev = selected_index - 3;
        if (prev < 0) prev = 0;
        selected_index = prev;
    }

    _target_scroll = selected_index;
    
    // Handle wrap-around distance for animation
    if (_target_scroll == 0 && _current_scroll > app_count - 2) {
         _current_scroll -= app_count; // Seamless scroll forward
    } else if (_target_scroll == app_count - 1 && _current_scroll < 1) {
         _current_scroll += app_count; // Seamless scroll backward
    }
}

void App_Grid::loop() {
    anim_time += 0.04f;
    
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
    ESP_LOGD(TAG, "handleInput: ev=%d idx=%d", event, selected_index);

    if (event == BTN_SELECT) {
        _held_btn = BTN_NONE;
        _held_delay = 0;
        _held_repeat = 0;
        
        // Ensure we wait for scroll to settle slightly before launching
        if (app_count > 0 && apps[selected_index].app_instance != nullptr && fabsf(_target_scroll - _current_scroll) < 0.5f) {
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
    if (_launcher_theme == LAUNCHER_CAROUSEL) {
        _render_carousel(canvas);
    } else {
        _render_tile(canvas);
    }
}

void App_Grid::_render_carousel(LGFX_Sprite* canvas) {
    const uint16_t bg = theme_get_bg();
    theme_draw_wallpaper(canvas, anim_time);

    if (app_count == 0) {
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
    
    float scroll_pct = (app_count > 1) ? (_current_scroll / (app_count - 1)) : 0.0f;
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
    
    for (int i = 0; i < app_count; i++) {
        float dist = i - _current_scroll;
        if (dist > app_count / 2.0f) dist -= app_count;
        if (dist < -app_count / 2.0f) dist += app_count;
        
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
    
    if (!_sp64) {
        _sp64 = new LGFX_Sprite(canvas);
        _sp64->setPsram(true);
        _sp64->createSprite(64, 64);
    }
    if (!_sp32) {
        _sp32 = new LGFX_Sprite(canvas);
        _sp32->setPsram(true);
        _sp32->createSprite(32, 32);
    }

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
    theme_draw_wallpaper(canvas, anim_time);

    if (app_count == 0) {
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

    // Layout: 48px left column for app list | remaining right panel for details
    const int LEFT_W = 48;
    const int RIGHT_X = LEFT_W + 2;
    const int RIGHT_W = W - RIGHT_X - 2;
    
    // Left column background with subtle depth
    float pulse = 0.5f + 0.5f * sinf(anim_time * 2.5f);
    uint16_t panel_bg = color_blend(txt_dim, bg, 0.08f + pulse * 0.02f);
    canvas->fillRect(0, 0, LEFT_W, H, panel_bg);
    canvas->drawFastVLine(LEFT_W, 0, H, color_blend(txt_dim, bg, 0.3f));
    
    // Render app icons in left column (32x32 centered)
    const int ITEM_H = 36;
    const int ICON_SIZE = 28;
    int visible_count = (H - 4) / ITEM_H;
    
    int scroll_offset = 0;
    if (selected_index >= visible_count - 1) {
        scroll_offset = selected_index - visible_count + 2;
    }
    if (scroll_offset < 0) scroll_offset = 0;
    
    for (int i = scroll_offset; i < app_count && (i - scroll_offset) * ITEM_H < H; i++) {
        int item_y = 2 + (i - scroll_offset) * ITEM_H;
        bool is_sel = (i == selected_index);
        
        if (is_sel) {
            // Stunning selection highlight with glow
            float glow = 0.7f + 0.3f * sinf(anim_time * 5.0f);
            canvas->fillRoundRect(2, item_y, LEFT_W - 4, ITEM_H - 2, 5, color_blend(acc, bg, 0.2f * glow));
            canvas->drawRoundRect(2, item_y, LEFT_W - 4, ITEM_H - 2, 5, color_blend(acc, txt, glow));
        }
        
        // Icon rendering (32x32 downsampled, centered)
        int icon_x = (LEFT_W - ICON_SIZE) / 2;
        int icon_y = item_y + (ITEM_H - ICON_SIZE) / 2;
        
        if (apps[i].cached_icon32) {
            if (!_sp32) {
                _sp32 = new LGFX_Sprite(canvas);
                _sp32->setPsram(true);
                _sp32->createSprite(32, 32);
            }
            
            if (_sp32) {
                _sp32->pushImage(0, 0, 32, 32, apps[i].cached_icon32);
                
                // Apply rounded corners
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
                
                uint16_t corner_col = is_sel ? color_blend(acc, bg, 0.2f * (0.7f + 0.3f * sinf(anim_time * 5.0f))) : panel_bg;
                apply_corners(_sp32, 32, 32, 6, corner_col);
                
                // Scale to ICON_SIZE
                float scale = (float)ICON_SIZE / 32.0f;
                _sp32->pushRotateZoom(icon_x + ICON_SIZE/2, icon_y + ICON_SIZE/2, 0, scale, scale);
            }
        }
    }
    
    // Scroll indicators
    if (scroll_offset > 0) {
        canvas->setTextColor(acc);
        canvas->setTextDatum(textdatum_t::top_center);
        int bounce = (int)(2.0f * sinf(anim_time * 6.0f));
        canvas->drawString("^", LEFT_W/2, 2 - bounce);
    }
    if (scroll_offset + visible_count < app_count) {
        canvas->setTextColor(acc);
        canvas->setTextDatum(textdatum_t::bottom_center);
        int bounce = (int)(2.0f * sinf(anim_time * 6.0f));
        canvas->drawString("v", LEFT_W/2, H - 2 + bounce);
    }
    
    // Right panel: Selected app details
    if (selected_index < app_count) {
        const AppEntry& app = apps[selected_index];
        
        // Panel background with depth
        uint16_t panel_fill = color_blend(txt_dim, bg, 0.12f);
        canvas->fillRoundRect(RIGHT_X, 2, RIGHT_W, H - 4, 6, panel_fill);
        canvas->drawRoundRect(RIGHT_X, 2, RIGHT_W, H - 4, 6, color_blend(txt_dim, bg, 0.35f));
        
        int panel_cx = RIGHT_X + RIGHT_W / 2;
        int content_y = 8;
        
        // Large icon at top (48x48)
        const int HERO_ICON = 48;
        
        if (app.icon_rgb565) {
            if (!_sp64) {
                _sp64 = new LGFX_Sprite(canvas);
                _sp64->setPsram(true);
                _sp64->createSprite(64, 64);
            }
            
            if (_sp64) {
                _sp64->pushImage(0, 0, 64, 64, app.icon_rgb565);
                
                // Premium rounded corners with glow
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
                
                apply_corners(_sp64, 64, 64, 12, panel_fill);
                
                float scale = (float)HERO_ICON / 64.0f;
                _sp64->pushRotateZoom(panel_cx, content_y + HERO_ICON/2, 0, scale, scale);
            }
        }
        
        content_y += HERO_ICON + 6;
        
        // App name with stunning typography
        canvas->setFont(&fonts::Font2);
        canvas->setTextColor(txt);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString(app.name, panel_cx, content_y);
        content_y += 12;
        
        // Separator line with glow effect
        float glow = 0.6f + 0.4f * sinf(anim_time * 3.0f);
        int sep_w = RIGHT_W - 12;
        canvas->drawFastHLine(RIGHT_X + 6, content_y, sep_w, color_blend(acc, bg, 0.5f * glow));
        content_y += 4;
        
        // Category badge
        if (app.category) {
            canvas->setFont(&fonts::Font0);
            canvas->setTextColor(color_blend(acc, txt, 0.8f));
            canvas->setTextDatum(textdatum_t::top_left);
            canvas->drawString("CATEGORY", RIGHT_X + 6, content_y);
            
            canvas->setTextColor(txt_dim);
            canvas->setTextDatum(textdatum_t::top_right);
            canvas->drawString(app.category, RIGHT_X + RIGHT_W - 6, content_y);
            content_y += 10;
        }
        
        // Description with word wrap
        if (app.desc) {
            canvas->setFont(&fonts::Font0);
            canvas->setTextColor(txt_dim);
            canvas->setTextDatum(textdatum_t::top_left);
            
            // Simple word-wrap rendering
            int desc_x = RIGHT_X + 6;
            int desc_w = RIGHT_W - 12;
            int line_h = 9;
            
            const char* desc = app.desc;
            char line_buf[32];
            int buf_idx = 0;
            
            while (*desc && content_y < H - 20) {
                if (*desc == ' ' || *desc == '\0') {
                    if (buf_idx > 0) {
                        line_buf[buf_idx] = '\0';
                        
                        if (canvas->textWidth(line_buf) > desc_w) {
                            canvas->drawString(line_buf, desc_x, content_y);
                            content_y += line_h;
                            buf_idx = 0;
                        }
                    }
                    if (*desc == ' ') desc++;
                } else {
                    if (buf_idx < 31) {
                        line_buf[buf_idx++] = *desc;
                    }
                    desc++;
                }
                
                if (buf_idx > 0 && (*desc == '\0' || *(desc+1) == '\0')) {
                    line_buf[buf_idx] = '\0';
                    canvas->drawString(line_buf, desc_x, content_y);
                    content_y += line_h;
                    buf_idx = 0;
                }
            }
        }
        
        // Launch button at bottom with pulse
        int btn_h = 16;
        int btn_y = H - btn_h - 4;
        int btn_w = RIGHT_W - 12;
        int btn_x = RIGHT_X + 6;
        
        float btn_pulse = 0.75f + 0.25f * sinf(anim_time * 4.0f);
        canvas->fillRoundRect(btn_x - 1, btn_y - 1, btn_w + 2, btn_h + 2, 6, color_blend(acc, bg, 0.3f * btn_pulse));
        canvas->fillRoundRect(btn_x, btn_y, btn_w, btn_h, 5, color_blend(acc, bg, 0.2f));
        canvas->drawRoundRect(btn_x, btn_y, btn_w, btn_h, 5, color_blend(acc, txt, btn_pulse));
        
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(txt);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("[ SEL TO LAUNCH ]", panel_cx, btn_y + btn_h/2);
    }
}
