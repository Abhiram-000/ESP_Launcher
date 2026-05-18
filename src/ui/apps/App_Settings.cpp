#include "App_Settings.h"
#include "../theme_mgr.h"
#include "../ui_task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include <math.h>

extern void switch_to_grid();

App_Settings::App_Settings() {
    current_state = STATE_MENU;
    previous_state = STATE_MENU;
    is_transitioning = false;
    transition_progress = 0.0f;
    menu_cursor = 0;
    theme_cursor = 0;
    temp_brightness = 255;
    free_heap = 0;
    flash_size = 0;
    idf_version = nullptr;
    anim_time = 0.0f;
}

App_Settings::~App_Settings() {}

void App_Settings::init() {
    current_state = STATE_MENU;
    previous_state = STATE_MENU;
    is_transitioning = false;
    transition_progress = 0.0f;
    
    menu_cursor = 0;
    theme_cursor = theme_get_current();
    temp_brightness = ui_get_brightness();
    anim_time = 0.0f;
    
    free_heap = esp_get_free_heap_size();
    uint32_t sz = 0;
    if (esp_flash_get_size(NULL, &sz) == ESP_OK) {
        flash_size = sz;
    }
    idf_version = esp_get_idf_version();
}

void App_Settings::exit() {}

void App_Settings::loop() {
    anim_time += 0.05f;
    if (current_state == STATE_SYS_INFO) {
        free_heap = esp_get_free_heap_size(); 
    }
    
    if (is_transitioning) {
        transition_progress += 0.08f; // ~12 frames
        if (transition_progress >= 1.0f) {
            transition_progress = 1.0f;
            is_transitioning = false;
        }
    }
}

void App_Settings::_change_state(State new_state) {
    previous_state = current_state;
    current_state = new_state;
    is_transitioning = true;
    transition_progress = 0.0f;
}

void App_Settings::handleInput(input_event_t event) {
    if (is_transitioning) return; // Block input during animation

    if (current_state == STATE_MENU) {
        if (event == BTN_UP) {
            if (menu_cursor > 0) menu_cursor--;
        } else if (event == BTN_DOWN) {
            if (menu_cursor < 2) menu_cursor++;
        } else if (event == BTN_LEFT) {
            switch_to_grid(); // Exit Settings
        } else if (event == BTN_SELECT || event == BTN_RIGHT) {
            if (menu_cursor == 0) _change_state(STATE_SYS_INFO);
            else if (menu_cursor == 1) _change_state(STATE_THEMES);
            else if (menu_cursor == 2) _change_state(STATE_BRIGHTNESS);
        }
    } 
    else if (current_state == STATE_SYS_INFO) {
        if (event == BTN_LEFT || event == BTN_SELECT) {
            _change_state(STATE_MENU);
        }
    } 
    else if (current_state == STATE_THEMES) {
        if (event == BTN_UP || event == BTN_LEFT) {
            if (theme_cursor > 0) theme_cursor--;
            theme_set((theme_type_t)theme_cursor);
        } else if (event == BTN_DOWN || event == BTN_RIGHT) {
            if (theme_cursor < THEME_COUNT - 1) theme_cursor++;
            theme_set((theme_type_t)theme_cursor);
        } else if (event == BTN_SELECT) {
            _change_state(STATE_MENU);
        }
    }
    else if (current_state == STATE_BRIGHTNESS) {
        if (event == BTN_RIGHT || event == BTN_UP) {
            temp_brightness += 15;
            if (temp_brightness > 255) temp_brightness = 255;
            ui_set_brightness((uint8_t)temp_brightness);
        } else if (event == BTN_LEFT || event == BTN_DOWN) {
            temp_brightness -= 15;
            if (temp_brightness < 10) temp_brightness = 10;
            ui_set_brightness((uint8_t)temp_brightness);
        } else if (event == BTN_SELECT) {
            _change_state(STATE_MENU);
        }
    }
}

/* Helper to blend colors for glowing effects */
static uint16_t blend_color(uint16_t fg, uint16_t bg, float alpha) {
    if (alpha <= 0.0f) return bg;
    if (alpha >= 1.0f) return fg;
    uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
    uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
    return ((uint16_t)(r2 + (r1 - r2) * alpha) << 11) |
           ((uint16_t)(g2 + (g1 - g2) * alpha) << 5) |
           (uint16_t)(b2 + (b1 - b2) * alpha);
}

void App_Settings::_draw_state_content(LGFX_Sprite* canvas, State state, int x_offset) {
    int cx = canvas->width() / 2 + x_offset;
    uint16_t bg = theme_get_bg();
    uint16_t accent = theme_get_accent();
    
    int list_y = 35; // Moved up slightly to give more room
    canvas->setFont(&fonts::Font0); // 6x8
    
    if (state == STATE_MENU) {
        const char* options[] = {"SYSTEM INFO", "THEME SETUP", "BRIGHTNESS"};
        
        for (int i = 0; i < 3; i++) {
            int item_y = list_y + i * 26;
            
            // Draw rounded card
            if (i == menu_cursor) {
                float pulse = 0.8f + 0.2f * sinf(anim_time * 4.0f);
                canvas->fillRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, blend_color(accent, bg, 0.2f));
                canvas->drawRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, blend_color(accent, bg, pulse));
                canvas->setTextColor(theme_get_text());
                
                // Animated chevron
                int bounce = (int)(2.0f * sinf(anim_time * 5.0f));
                canvas->setTextDatum(textdatum_t::middle_right);
                canvas->drawString(">", x_offset + canvas->width() - 15 + bounce, item_y + 11);
            } else {
                canvas->fillRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, blend_color(theme_get_text_dim(), bg, 0.1f));
                canvas->drawRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, blend_color(theme_get_text_dim(), bg, 0.3f));
                canvas->setTextColor(theme_get_text_dim());
            }
            
            canvas->setTextDatum(textdatum_t::middle_left);
            canvas->drawString(options[i], x_offset + 20, item_y + 11);
        }
        
    } else if (state == STATE_SYS_INFO) {
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString("SYSTEM CORE", cx, list_y);
        list_y += 12;
        
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        
        char buf[32];
        canvas->setTextColor(theme_get_text());
        
        // Premium grouped info box
        canvas->fillRoundRect(x_offset + 8, list_y, canvas->width()-16, 68, 6, blend_color(theme_get_text_dim(), bg, 0.05f));
        canvas->drawRoundRect(x_offset + 8, list_y, canvas->width()-16, 68, 6, blend_color(accent, bg, 0.3f));
        
        list_y += 6;
        snprintf(buf, sizeof(buf), "CPU: ESP32-S%d", (chip_info.model == CHIP_ESP32S3) ? 3 : 2);
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "RAM: %lu KB FREE", (unsigned long)(free_heap / 1024));
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "NVM: %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "OS: %s", idf_version);
        canvas->drawString(buf, cx, list_y); list_y += 14;
        
    } else if (state == STATE_THEMES) {
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString("ACTIVE THEME", cx, list_y);
        list_y += 20;
        
        // Draw Left/Right carousel arrows
        int bounce = (int)(2.0f * sinf(anim_time * 4.0f));
        canvas->setTextColor(theme_get_text());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("<", x_offset + 15 - bounce, list_y + 10);
        canvas->drawString(">", x_offset + canvas->width() - 15 + bounce, list_y + 10);

        // Highlight box for current selection
        canvas->fillRoundRect(x_offset + 25, list_y, canvas->width()-50, 20, 6, blend_color(accent, bg, 0.15f));
        canvas->drawRoundRect(x_offset + 25, list_y, canvas->width()-50, 20, 6, accent);
        
        canvas->setTextColor(theme_get_text());
        canvas->drawString(theme_get_name((theme_type_t)theme_cursor), cx, list_y + 10);
        
        list_y += 40;
        // Mini preview swatches
        canvas->fillRoundRect(cx - 24, list_y - 8, 16, 16, 4, theme_get_bg());
        canvas->drawRoundRect(cx - 24, list_y - 8, 16, 16, 4, theme_get_text_dim());
        
        canvas->fillRoundRect(cx - 4, list_y - 8, 16, 16, 4, theme_get_accent());
        canvas->drawRoundRect(cx - 4, list_y - 8, 16, 16, 4, blend_color(theme_get_text(), theme_get_accent(), 0.2f));
        
        canvas->fillRoundRect(cx + 16, list_y - 8, 16, 16, 4, theme_get_text());
        
    } else if (state == STATE_BRIGHTNESS) {
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString("DISPLAY LUMA", cx, list_y);
        list_y += 25;
        
        int bar_w = 110;
        int bar_h = 10;
        int bar_x = cx - bar_w / 2;
        int bar_y = list_y;
        
        // Beautiful slider track
        canvas->fillRoundRect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 6, blend_color(theme_get_text_dim(), bg, 0.2f));
        canvas->drawRoundRect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 6, blend_color(theme_get_text_dim(), bg, 0.5f));
        
        int fill_w = (temp_brightness * bar_w) / 255;
        if (fill_w < 4) fill_w = 4;
        
        // Glowing fill
        canvas->fillRoundRect(bar_x, bar_y, fill_w, bar_h, 4, accent);
        
        // L/R indicators
        int bounce = (int)(2.0f * sinf(anim_time * 4.0f));
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("-", x_offset + 10 - bounce/2, bar_y + 5);
        canvas->drawString("+", x_offset + canvas->width() - 10 + bounce/2, bar_y + 5);
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (temp_brightness * 100) / 255);
        canvas->setTextColor(theme_get_text());
        canvas->drawString(buf, cx, bar_y + 24);
    }
}

void App_Settings::render(LGFX_Sprite* canvas) {
    uint16_t bg = theme_get_bg();
    theme_draw_wallpaper(canvas, anim_time);

    int cx = canvas->width() / 2;
    int header_y = 12;

    // --- Modern Header ---
    uint16_t accent = theme_get_accent();
    
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(theme_get_text());
    canvas->drawString("DEVICE SETTINGS", cx, header_y);
    
    // Glowing separator line
    float pulse = 0.5f + 0.5f * sinf(anim_time * 3.0f);
    int line_w = 100 + (int)(20.0f * pulse);
    canvas->drawFastHLine(cx - line_w/2, header_y + 10, line_w, blend_color(accent, bg, 0.8f));
    canvas->drawFastHLine(cx - line_w/2 + 10, header_y + 11, line_w - 20, blend_color(accent, bg, 0.4f));

    // --- Content Area with Transitions ---
    // Create a clipping window so content doesn't overwrite the header or footer
    canvas->setClipRect(0, 25, canvas->width(), canvas->height() - 40);

    if (is_transitioning) {
        float t = transition_progress;
        // Cubic ease out
        float inv = 1.0f - t;
        float eased = 1.0f - (inv * inv * inv);
        
        int w = canvas->width();
        bool going_back = (current_state == STATE_MENU);
        
        int prev_x = going_back ? (int)(eased * w) : (int)(-eased * w);
        int curr_x = going_back ? (int)(-w + eased * w) : (int)(w - eased * w);
        
        _draw_state_content(canvas, previous_state, prev_x);
        _draw_state_content(canvas, current_state, curr_x);
    } else {
        _draw_state_content(canvas, current_state, 0);
    }
    
    canvas->clearClipRect();

    // --- Footer ---
    canvas->setTextColor(blend_color(theme_get_text_dim(), bg, 0.5f));
    canvas->setTextDatum(textdatum_t::bottom_center);
    if(current_state == STATE_MENU) {
        canvas->drawString("L: EXIT   R/SEL: OK", cx, canvas->height() - 2);
    } else {
        canvas->drawString("L: BACK   R/SEL: OK", cx, canvas->height() - 2);
    }
}
