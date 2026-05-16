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
}

void App_Settings::handleInput(input_event_t event) {
    if (current_state == STATE_MENU) {
        if (event == BTN_UP) {
            if (menu_cursor > 0) menu_cursor--;
        } else if (event == BTN_DOWN) {
            if (menu_cursor < 2) menu_cursor++;
        } else if (event == BTN_LEFT) {
            switch_to_grid(); // Exit Settings
        } else if (event == BTN_SELECT || event == BTN_RIGHT) {
            if (menu_cursor == 0) current_state = STATE_SYS_INFO;
            else if (menu_cursor == 1) current_state = STATE_THEMES;
            else if (menu_cursor == 2) current_state = STATE_BRIGHTNESS;
        }
    } 
    else if (current_state == STATE_SYS_INFO) {
        if (event == BTN_LEFT || event == BTN_SELECT) {
            current_state = STATE_MENU;
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
            current_state = STATE_MENU;
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
            current_state = STATE_MENU;
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

void App_Settings::render(LGFX_Sprite* canvas) {
    uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);

    int cx = canvas->width() / 2;
    int header_y = 20;

    // --- Premium Marvellous Cyber Header Elements ---
    // Dynamic pulsing rings inspired by the OTA theme
    uint16_t accent = theme_get_accent();
    float pulse = 12.0f + 2.5f * sinf(anim_time * 2.0f);
    
    // Outer neon glow ring
    canvas->drawArc(cx, header_y, (int)pulse + 4, (int)pulse + 2, 0, 360, blend_color(accent, bg, 0.4f));
    canvas->drawArc(cx, header_y, (int)pulse + 2, (int)pulse, 0, 360, accent);
    
    // Inner spinner
    float spin_angle = anim_time * 60.0f; // degrees
    canvas->drawArc(cx, header_y, (int)pulse - 2, (int)pulse - 4, (int)spin_angle, (int)spin_angle + 120, theme_get_text());
    canvas->drawArc(cx, header_y, (int)pulse - 2, (int)pulse - 4, (int)spin_angle + 180, (int)spin_angle + 300, theme_get_text());

    // Center core
    canvas->fillCircle(cx, header_y, 4, blend_color(accent, bg, 0.6f + 0.4f * sinf(anim_time * 3.0f)));

    int list_y = 45;
    canvas->setFont(&fonts::Font0); // 6x8
    canvas->setTextDatum(textdatum_t::top_center);

    if (current_state == STATE_MENU) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString("- SETTINGS -", cx, list_y);
        list_y += 20;
        
        canvas->setTextDatum(textdatum_t::middle_center);
        const char* options[] = {"SYSTEM INFO", "THEME SETUP", "BRIGHTNESS"};
        
        for (int i = 0; i < 3; i++) {
            int item_y = list_y + i * 22;
            if (i == menu_cursor) {
                // Professional selection highlight pill
                canvas->fillRoundRect(10, item_y - 9, canvas->width()-20, 18, 9, blend_color(accent, bg, 0.2f));
                canvas->drawRoundRect(10, item_y - 9, canvas->width()-20, 18, 9, accent);
                canvas->setTextColor(theme_get_text());
                
                // Left/Right selection bouncing indicators
                int bounce = (int)(2.0f * sinf(anim_time * 4.0f));
                canvas->drawString(">", 18 + bounce, item_y);
                canvas->drawString("<", canvas->width() - 18 - bounce, item_y);
            } else {
                canvas->setTextColor(theme_get_text_dim());
            }
            canvas->drawString(options[i], cx, item_y);
        }
        
    } else if (current_state == STATE_SYS_INFO) {
        canvas->setTextColor(accent);
        canvas->drawString("SYSTEM CORE", cx, list_y);
        list_y += 15;
        
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        
        char buf[32];
        canvas->setTextColor(theme_get_text());
        canvas->setTextDatum(textdatum_t::top_center);
        
        // Premium grouped info box
        canvas->drawRoundRect(8, list_y, canvas->width()-16, 68, 4, blend_color(accent, bg, 0.3f));
        
        list_y += 6;
        snprintf(buf, sizeof(buf), "CPU: ESP32-S%d", (chip_info.model == CHIP_ESP32S3) ? 3 : 2);
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "RAM: %lu KB FREE", (unsigned long)(free_heap / 1024));
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "NVM: %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "OS: %s", idf_version);
        canvas->drawString(buf, cx, list_y); list_y += 14;
        
    } else if (current_state == STATE_THEMES) {
        canvas->setTextColor(accent);
        canvas->drawString("ACTIVE THEME", cx, list_y);
        list_y += 20;
        
        canvas->setTextDatum(textdatum_t::middle_center);
        
        // Draw Left/Right carousel arrows
        int bounce = (int)(2.0f * sinf(anim_time * 4.0f));
        canvas->setTextColor(theme_get_text());
        canvas->drawString("<", 15 - bounce, list_y + 8);
        canvas->drawString(">", canvas->width() - 15 + bounce, list_y + 8);

        // Highlight box for current selection
        canvas->fillRoundRect(25, list_y - 2, canvas->width()-50, 20, 4, blend_color(accent, bg, 0.15f));
        canvas->drawRoundRect(25, list_y - 2, canvas->width()-50, 20, 4, accent);
        
        canvas->setTextColor(theme_get_text());
        canvas->drawString(theme_get_name((theme_type_t)theme_cursor), cx, list_y + 8);
        
        list_y += 35;
        // Mini preview swatches
        canvas->fillCircle(cx - 15, list_y, 6, theme_get_bg());
        canvas->drawCircle(cx - 15, list_y, 6, theme_get_text_dim());
        
        canvas->fillCircle(cx, list_y, 6, theme_get_accent());
        
        canvas->fillCircle(cx + 15, list_y, 6, theme_get_text());
        
    } else if (current_state == STATE_BRIGHTNESS) {
        canvas->setTextColor(accent);
        canvas->drawString("DISPLAY LUMA", cx, list_y);
        list_y += 25;
        
        int bar_w = 100;
        int bar_h = 8;
        int bar_x = cx - bar_w / 2;
        int bar_y = list_y;
        
        // Cyberpunk Track
        canvas->drawRect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, theme_get_text_dim());
        
        int fill_w = (temp_brightness * bar_w) / 255;
        if (fill_w < 2) fill_w = 2;
        
        // Glowing fill
        canvas->fillRect(bar_x, bar_y, fill_w, bar_h, accent);
        
        // L/R indicators
        int bounce = (int)(2.0f * sinf(anim_time * 4.0f));
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("-", 10 - bounce/2, bar_y + 4);
        canvas->drawString("+", canvas->width() - 10 + bounce/2, bar_y + 4);
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (temp_brightness * 100) / 255);
        canvas->setTextColor(theme_get_text());
        canvas->drawString(buf, cx, bar_y + 22);
    }

    // Footer
    canvas->setTextColor(blend_color(theme_get_text_dim(), bg, 0.5f));
    canvas->setTextDatum(textdatum_t::bottom_center);
    if(current_state == STATE_MENU) {
        canvas->drawString("L: EXIT   R/SEL: OK", cx, canvas->height() - 2);
    } else {
        canvas->drawString("L: BACK   R/SEL: OK", cx, canvas->height() - 2);
    }
}
