#include "App_Settings.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "../../services/idle_mgr.h"
#include "../ui_task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <math.h>

extern void switch_to_grid();

static const char* TAG = "APP_SETTINGS";

// Sleep timeout options (in milliseconds)
static const uint32_t SLEEP_OPTIONS[] = {
    30000,      // 30 seconds
    60000,      // 1 minute
    0,          // OFF (disabled)
    3600000,    // 1 hour
    7200000     // 2 hours
};

static const char* SLEEP_LABELS[] = {
    "30 SECONDS",
    "1 MINUTE",
    "OFF",
    "1 HOUR",
    "2 HOURS"
};

#define SLEEP_OPTION_COUNT 5
#define MENU_ITEMS 6
#define MENU_VISIBLE_ITEMS 3

App_Settings::App_Settings() {
    current_state = STATE_MENU;
    previous_state = STATE_MENU;
    is_transitioning = false;
    transition_progress = 0.0f;
    menu_cursor = 0;
    menu_scroll_offset = 0;
    theme_cursor = 0;
    launcher_theme_cursor = 0;
    temp_brightness = 255;
    sleep_config_cursor = 0;
    free_heap = 0;
    flash_size = 0;
    idf_version = nullptr;
    anim_time = 0.0f;
    _wifi_scanner_active = false;
}

App_Settings::~App_Settings() {}

void App_Settings::init() {
    current_state = STATE_MENU;
    previous_state = STATE_MENU;
    is_transitioning = false;
    transition_progress = 0.0f;
    
    menu_cursor = 0;
    menu_scroll_offset = 0;
    theme_cursor = theme_get_current();
    launcher_theme_cursor = ui_get_launcher_theme();
    temp_brightness = ui_get_brightness();
    sleep_config_cursor = _get_current_sleep_config();
    anim_time = 0.0f;
    _wifi_scanner_active = false;
    
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
    
    // CRITICAL: Call WiFi scanner loop when active
    if (_wifi_scanner_active) {
        _wifi_scanner.loop();
    }
    
    // Auto-scroll menu
    if (current_state == STATE_MENU && !is_transitioning) {
        int target_offset = 0;
        if (menu_cursor >= MENU_VISIBLE_ITEMS) {
            target_offset = menu_cursor - MENU_VISIBLE_ITEMS + 1;
        }
        
        // Smooth scroll
        if (menu_scroll_offset < target_offset) {
            menu_scroll_offset++;
        } else if (menu_scroll_offset > target_offset) {
            menu_scroll_offset--;
        }
    }
}

void App_Settings::_change_state(State new_state) {
    previous_state = current_state;
    current_state = new_state;
    is_transitioning = true;
    transition_progress = 0.0f;
}

int App_Settings::_get_current_sleep_config() {
    nvs_handle_t nvs_h;
    int result = 2; // Default to OFF
    
    if (nvs_open("idle_cfg", NVS_READONLY, &nvs_h) == ESP_OK) {
        uint32_t timeout_ms = 0;
        if (nvs_get_u32(nvs_h, "timeout_ms", &timeout_ms) == ESP_OK) {
            // Find matching option
            for (int i = 0; i < SLEEP_OPTION_COUNT; i++) {
                if (SLEEP_OPTIONS[i] == timeout_ms) {
                    result = i;
                    break;
                }
            }
        }
        nvs_close(nvs_h);
    }
    
    return result;
}

void App_Settings::_apply_sleep_config(int option) {
    if (option < 0 || option >= SLEEP_OPTION_COUNT) return;
    
    nvs_handle_t nvs_h;
    if (nvs_open("idle_cfg", NVS_READWRITE, &nvs_h) == ESP_OK) {
        uint32_t timeout_ms = SLEEP_OPTIONS[option];
        nvs_set_u32(nvs_h, "timeout_ms", timeout_ms);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
        
        // Note: The idle_mgr would need to be updated to read this value on init
        // For now, this saves the preference for next boot
        ESP_LOGI(TAG, "Sleep config saved: %s (%lu ms)", SLEEP_LABELS[option], (unsigned long)timeout_ms);
    }
}

void App_Settings::handleInput(input_event_t event) {
    // WiFi scanner overlay has priority
    if (_wifi_scanner_active) {
        _wifi_scanner.handleInput(event);
        return;
    }
    
    if (is_transitioning) return; // Block input during animation

    if (current_state == STATE_MENU) {
        if (event == BTN_UP) {
            if (menu_cursor > 0) menu_cursor--;
        } else if (event == BTN_DOWN) {
            if (menu_cursor < MENU_ITEMS - 1) menu_cursor++;
        } else if (event == BTN_LEFT) {
            switch_to_grid(); // Exit Settings
        } else if (event == BTN_SELECT || event == BTN_RIGHT) {
            if (menu_cursor == 0) _change_state(STATE_SYS_INFO);
            else if (menu_cursor == 1) _change_state(STATE_THEMES);
            else if (menu_cursor == 2) _change_state(STATE_LAUNCHER_THEME);
            else if (menu_cursor == 3) _change_state(STATE_BRIGHTNESS);
            else if (menu_cursor == 4) _change_state(STATE_SLEEP_CONFIG);
            else if (menu_cursor == 5) {
                // WiFi Connect - open scanner overlay
                _wifi_scanner_active = true;
                _wifi_scanner.begin(
                    [this](bool success) {
                        // On complete (success or cancel)
                        this->_wifi_scanner_active = false;
                        if (success) {
                            ESP_LOGI(TAG, "WiFi connected successfully");
                        } else {
                            ESP_LOGI(TAG, "WiFi connection cancelled or failed");
                        }
                    }
                );
            }
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
    else if (current_state == STATE_SLEEP_CONFIG) {
        if (event == BTN_UP || event == BTN_LEFT) {
            if (sleep_config_cursor > 0) sleep_config_cursor--;
        } else if (event == BTN_DOWN || event == BTN_RIGHT) {
            if (sleep_config_cursor < SLEEP_OPTION_COUNT - 1) sleep_config_cursor++;
        } else if (event == BTN_SELECT) {
            _apply_sleep_config(sleep_config_cursor);
            _change_state(STATE_MENU);
        }
    }
    else if (current_state == STATE_LAUNCHER_THEME) {
        if (event == BTN_UP || event == BTN_LEFT) {
            if (launcher_theme_cursor > 0) launcher_theme_cursor--;
            ui_set_launcher_theme((uint8_t)launcher_theme_cursor);
        } else if (event == BTN_DOWN || event == BTN_RIGHT) {
            if (launcher_theme_cursor < 1) launcher_theme_cursor++;  // Only 2 themes
            ui_set_launcher_theme((uint8_t)launcher_theme_cursor);
        } else if (event == BTN_SELECT) {
            _change_state(STATE_MENU);
        }
    }
}

void App_Settings::_draw_state_content(LGFX_Sprite* canvas, State state, int x_offset) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    int cx = W / 2 + x_offset;
    uint16_t bg = theme_get_bg();
    uint16_t accent = theme_get_accent();
    
    int list_y = H / 4;
    canvas->setFont(&fonts::Font0); // 6x8
    
    if (state == STATE_MENU) {
        const char* options[] = {
            "SYSTEM INFO",
            "THEME SETUP",
            "LAUNCHER STYLE",
            "BRIGHTNESS",
            "SLEEP CONFIG",
            "CONNECT WIFI"
        };
        
        // Scrollable menu with visible window
        for (int i = 0; i < MENU_ITEMS; i++) {
            int display_index = i - menu_scroll_offset;
            if (display_index < 0 || display_index >= MENU_VISIBLE_ITEMS) continue;
            
            int item_y = list_y + display_index * 26;
            
            // Draw rounded card with stunning effects
            if (i == menu_cursor) {
                float pulse = 0.7f + 0.3f * sinf(anim_time * 4.0f);
                float glow = 0.5f + 0.5f * sinf(anim_time * 3.0f);
                
                // Glowing background
                canvas->fillRoundRect(x_offset + 8, item_y - 1, canvas->width()-16, 24, 7, color_blend(accent, bg, 0.25f * glow));
                canvas->fillRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, color_blend(accent, bg, 0.2f));
                canvas->drawRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, color_blend(accent, theme_get_text(), pulse));
                canvas->setTextColor(theme_get_text());
                
                // Animated chevron with bounce
                int bounce = (int)(3.0f * sinf(anim_time * 6.0f));
                canvas->setTextDatum(textdatum_t::middle_right);
                canvas->setTextColor(accent);
                canvas->drawString(">", x_offset + canvas->width() - 14 + bounce, item_y + 11);
            } else {
                canvas->fillRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, color_blend(theme_get_text_dim(), bg, 0.08f));
                canvas->drawRoundRect(x_offset + 10, item_y, canvas->width()-20, 22, 6, color_blend(theme_get_text_dim(), bg, 0.25f));
                canvas->setTextColor(theme_get_text_dim());
            }
            
            canvas->setTextDatum(textdatum_t::middle_left);
            canvas->drawString(options[i], x_offset + 18, item_y + 11);
        }
        
        // Scroll indicators
        if (menu_scroll_offset > 0) {
            // Up arrow
            canvas->setTextColor(accent);
            canvas->setTextDatum(textdatum_t::top_center);
            int arrow_bounce = (int)(2.0f * sinf(anim_time * 5.0f));
            canvas->drawString("^", cx, list_y - 8 - arrow_bounce);
        }
        if (menu_scroll_offset < MENU_ITEMS - MENU_VISIBLE_ITEMS) {
            // Down arrow
            canvas->setTextColor(accent);
            canvas->setTextDatum(textdatum_t::bottom_center);
            int arrow_bounce = (int)(2.0f * sinf(anim_time * 5.0f));
            canvas->drawString("v", cx, list_y + MENU_VISIBLE_ITEMS * 26 + arrow_bounce);
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
        
        // Premium grouped info box with glow
        float glow = 0.5f + 0.5f * sinf(anim_time * 2.0f);
        canvas->fillRoundRect(x_offset + 8, list_y, canvas->width()-16, 68, 6, color_blend(theme_get_text_dim(), bg, 0.05f));
        canvas->drawRoundRect(x_offset + 8, list_y, canvas->width()-16, 68, 6, color_blend(accent, bg, 0.3f * glow));
        
        list_y += 6;
        snprintf(buf, sizeof(buf), "CPU: ESP32-S%d", (chip_info.model == CHIP_ESP32S3) ? 3 : 2);
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "RAM: %lu KB FREE", (unsigned long)(free_heap / 1024));
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "NVM: %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
        canvas->drawString(buf, cx, list_y); list_y += 14;

        snprintf(buf, sizeof(buf), "OS: %s", idf_version ? idf_version : "Unknown");
        canvas->drawString(buf, cx, list_y); list_y += 14;
        
    } else if (state == STATE_THEMES) {
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString("ACTIVE THEME", cx, list_y);
        list_y += 20;
        
        // Draw Left/Right carousel arrows with bounce
        int bounce = (int)(3.0f * sinf(anim_time * 5.0f));
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("<", x_offset + 15 - bounce, list_y + 10);
        canvas->drawString(">", x_offset + canvas->width() - 15 + bounce, list_y + 10);

        // Highlight box for current selection with pulse
        float pulse = 0.7f + 0.3f * sinf(anim_time * 4.0f);
        canvas->fillRoundRect(x_offset + 25, list_y, canvas->width()-50, 20, 6, color_blend(accent, bg, 0.15f));
        canvas->drawRoundRect(x_offset + 25, list_y, canvas->width()-50, 20, 6, color_blend(accent, theme_get_text(), pulse));
        
        canvas->setTextColor(theme_get_text());
        canvas->drawString(theme_get_name((theme_type_t)theme_cursor), cx, list_y + 10);
        
        list_y += 40;
        // Mini preview swatches with borders
        canvas->fillRoundRect(cx - 24, list_y - 8, 16, 16, 4, theme_get_bg());
        canvas->drawRoundRect(cx - 24, list_y - 8, 16, 16, 4, theme_get_text_dim());
        
        canvas->fillRoundRect(cx - 4, list_y - 8, 16, 16, 4, theme_get_accent());
        canvas->drawRoundRect(cx - 4, list_y - 8, 16, 16, 4, color_blend(theme_get_text(), theme_get_accent(), 0.2f));
        
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
        
        // Beautiful slider track with shadow
        canvas->fillRoundRect(bar_x - 3, bar_y - 3, bar_w + 6, bar_h + 6, 7, color_blend(theme_get_text_dim(), bg, 0.15f));
        canvas->fillRoundRect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 6, color_blend(theme_get_text_dim(), bg, 0.2f));
        canvas->drawRoundRect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 6, color_blend(theme_get_text_dim(), bg, 0.5f));
        
        int fill_w = (temp_brightness * bar_w) / 255;
        if (fill_w < 4) fill_w = 4;
        
        // Glowing fill with pulse
        float glow = 0.8f + 0.2f * sinf(anim_time * 4.0f);
        canvas->fillRoundRect(bar_x, bar_y, fill_w, bar_h, 4, color_blend(accent, theme_get_text(), glow));
        
        // L/R indicators with bounce
        int bounce = (int)(2.0f * sinf(anim_time * 5.0f));
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("-", x_offset + 10 - bounce/2, bar_y + 5);
        canvas->drawString("+", x_offset + canvas->width() - 10 + bounce/2, bar_y + 5);
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (temp_brightness * 100) / 255);
        canvas->setTextColor(theme_get_text());
        canvas->drawString(buf, cx, bar_y + 24);
        
    } else if (state == STATE_SLEEP_CONFIG) {
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString("SLEEP TIMEOUT", cx, list_y);
        list_y += 20;
        
        // Horizontal carousel with left/right arrows
        int bounce = (int)(3.0f * sinf(anim_time * 5.0f));
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::middle_center);
        
        // Left arrow (if not at first item)
        if (sleep_config_cursor > 0) {
            canvas->drawString("<", x_offset + 12 - bounce, list_y + 20);
        }
        
        // Right arrow (if not at last item)
        if (sleep_config_cursor < SLEEP_OPTION_COUNT - 1) {
            canvas->drawString(">", x_offset + canvas->width() - 12 + bounce, list_y + 20);
        }
        
        // Main display card for current selection
        float pulse = 0.7f + 0.3f * sinf(anim_time * 5.0f);
        float glow = 0.5f + 0.5f * sinf(anim_time * 3.0f);
        
        int card_w = 100;
        int card_h = 40;
        int card_x = cx - card_w/2;
        int card_y = list_y + 5;
        
        // Stunning glowing card
        canvas->fillRoundRect(card_x - 2, card_y - 2, card_w + 4, card_h + 4, 8, color_blend(accent, bg, 0.25f * glow));
        canvas->fillRoundRect(card_x, card_y, card_w, card_h, 7, color_blend(accent, bg, 0.18f));
        canvas->drawRoundRect(card_x, card_y, card_w, card_h, 7, color_blend(accent, theme_get_text(), pulse));
        
        // Display current selection
        canvas->setFont(&fonts::Font2);
        canvas->setTextColor(theme_get_text());
        canvas->setTextDatum(textdatum_t::middle_center);
        
        // Split label into two lines if needed
        const char* label = SLEEP_LABELS[sleep_config_cursor];
        if (strcmp(label, "30 SECONDS") == 0) {
            canvas->drawString("30", cx, card_y + card_h/2 - 6);
            canvas->setFont(&fonts::Font0);
            canvas->drawString("SECONDS", cx, card_y + card_h/2 + 6);
        } else if (strcmp(label, "1 MINUTE") == 0) {
            canvas->drawString("1", cx, card_y + card_h/2 - 6);
            canvas->setFont(&fonts::Font0);
            canvas->drawString("MINUTE", cx, card_y + card_h/2 + 6);
        } else if (strcmp(label, "OFF") == 0) {
            canvas->drawString("OFF", cx, card_y + card_h/2);
        } else if (strcmp(label, "1 HOUR") == 0) {
            canvas->drawString("1", cx, card_y + card_h/2 - 6);
            canvas->setFont(&fonts::Font0);
            canvas->drawString("HOUR", cx, card_y + card_h/2 + 6);
        } else if (strcmp(label, "2 HOURS") == 0) {
            canvas->drawString("2", cx, card_y + card_h/2 - 6);
            canvas->setFont(&fonts::Font0);
            canvas->drawString("HOURS", cx, card_y + card_h/2 + 6);
        }
        
        // Beautiful horizontal scroll bar
        int bar_y = card_y + card_h + 15;
        int bar_w = 100;
        int bar_h = 4;
        int bar_x = cx - bar_w/2;
        
        // Track
        canvas->fillRoundRect(bar_x, bar_y, bar_w, bar_h, 2, color_blend(theme_get_text_dim(), bg, 0.2f));
        
        // Indicator (shows position)
        int indicator_w = bar_w / SLEEP_OPTION_COUNT;
        int indicator_x = bar_x + (sleep_config_cursor * indicator_w);
        canvas->fillRoundRect(indicator_x, bar_y - 1, indicator_w, bar_h + 2, 2, color_blend(accent, theme_get_text(), pulse));
        
        // Dots below for each option (visual feedback)
        int dot_spacing = bar_w / (SLEEP_OPTION_COUNT - 1);
        for (int i = 0; i < SLEEP_OPTION_COUNT; i++) {
            int dot_x = bar_x + (i * dot_spacing);
            int dot_y = bar_y + 10;
            if (i == sleep_config_cursor) {
                canvas->fillCircle(dot_x, dot_y, 3, accent);
                canvas->drawCircle(dot_x, dot_y, 4, color_blend(accent, theme_get_text(), pulse));
            } else {
                canvas->fillCircle(dot_x, dot_y, 2, color_blend(theme_get_text_dim(), bg, 0.4f));
            }
        }
        
        // Hint text
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::bottom_center);
        canvas->drawString("L/R: Navigate  SEL: Apply", cx, bar_y + 25);
    } else if (state == STATE_LAUNCHER_THEME) {
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->drawString("LAUNCHER STYLE", cx, list_y);
        list_y += 20;
        
        // Left/Right carousel arrows with bounce
        int bounce = (int)(3.0f * sinf(anim_time * 5.0f));
        canvas->setTextColor(accent);
        canvas->setTextDatum(textdatum_t::middle_center);
        
        if (launcher_theme_cursor > 0) {
            canvas->drawString("<", x_offset + 12 - bounce, list_y + 20);
        }
        if (launcher_theme_cursor < 1) {
            canvas->drawString(">", x_offset + canvas->width() - 12 + bounce, list_y + 20);
        }
        
        // Main display card
        float pulse = 0.7f + 0.3f * sinf(anim_time * 5.0f);
        float glow = 0.5f + 0.5f * sinf(anim_time * 3.0f);
        
        int card_w = 100;
        int card_h = 40;
        int card_x = cx - card_w/2;
        int card_y = list_y + 5;
        
        // Glowing card
        canvas->fillRoundRect(card_x - 2, card_y - 2, card_w + 4, card_h + 4, 8, color_blend(accent, bg, 0.25f * glow));
        canvas->fillRoundRect(card_x, card_y, card_w, card_h, 7, color_blend(accent, bg, 0.18f));
        canvas->drawRoundRect(card_x, card_y, card_w, card_h, 7, color_blend(accent, theme_get_text(), pulse));
        
        // Display current selection
        canvas->setFont(&fonts::Font2);
        canvas->setTextColor(theme_get_text());
        canvas->setTextDatum(textdatum_t::middle_center);
        
        const char* launcher_names[] = {"CAROUSEL", "TILE"};
        canvas->drawString(launcher_names[launcher_theme_cursor], cx, card_y + card_h/2);
        
        // Indicator dots
        int bar_y = card_y + card_h + 15;
        int dot_spacing = 20;
        for (int i = 0; i < 2; i++) {
            int dot_x = cx - dot_spacing/2 + (i * dot_spacing);
            int dot_y = bar_y + 10;
            if (i == launcher_theme_cursor) {
                canvas->fillCircle(dot_x, dot_y, 3, accent);
                canvas->drawCircle(dot_x, dot_y, 4, color_blend(accent, theme_get_text(), pulse));
            } else {
                canvas->fillCircle(dot_x, dot_y, 2, color_blend(theme_get_text_dim(), bg, 0.4f));
            }
        }
        
        // Hint text
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::bottom_center);
        canvas->drawString("L/R: Switch  SEL: Apply", cx, bar_y + 22);
    }
}

void App_Settings::render(LGFX_Sprite* canvas) {
    uint16_t bg = theme_get_bg();
    theme_draw_wallpaper(canvas, anim_time);

    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    int cx = W / 2;
    int header_y = H / 10;

    // --- Stunning Modern Header ---
    uint16_t accent = theme_get_accent();
    
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(theme_get_text());
    canvas->drawString("DEVICE SETTINGS", cx, header_y);
    
    // Glowing separator line with pulse
    float pulse = 0.5f + 0.5f * sinf(anim_time * 3.0f);
    int line_w = 100 + (int)(20.0f * pulse);
    canvas->drawFastHLine(cx - line_w/2, header_y + 10, line_w, color_blend(accent, bg, 0.8f));
    canvas->drawFastHLine(cx - line_w/2 + 10, header_y + 11, line_w - 20, color_blend(accent, bg, 0.4f));

    // --- Content Area with Smooth Transitions ---
    canvas->setClipRect(0, H / 5, W, H - H / 3);

    if (is_transitioning) {
        float t = transition_progress;
        // Cubic ease out for smoothness
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

    // --- Elegant Footer ---
    canvas->setTextColor(color_blend(theme_get_text_dim(), bg, 0.5f));
    canvas->setTextDatum(textdatum_t::bottom_center);
    if(current_state == STATE_MENU) {
        canvas->drawString("L: EXIT   R/SEL: OK", cx, canvas->height() - 2);
    } else {
        canvas->drawString("L: BACK   R/SEL: OK", cx, canvas->height() - 2);
    }
    
    // WiFi scanner overlay (if active)
    if (_wifi_scanner_active) {
        _wifi_scanner.render(canvas);
    }
}
