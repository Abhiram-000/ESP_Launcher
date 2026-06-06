/**
 * @file App_Pomodoro.cpp
 * @brief Premium Pomodoro Timer UI Implementation
 */

#include "App_Pomodoro.h"
#include "../theme_mgr.h"
#include "../../services/idle_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

extern void switch_to_grid();

static const char* TAG = "APP_POMO";

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

App_Pomodoro::App_Pomodoro()
    : _screen(SCREEN_MODE_SELECT), _prev_screen(SCREEN_MODE_SELECT),
      _anim_time(0), _transition_progress(0), _transitioning(false),
      _mode_cursor(1), _mode_scroll_offset(0),
      _circle_pulse(0), _particle_phase(0), _last_seconds(0),
      _just_completed(false), _completion_anim(0),
      _stats_scroll(0), _settings_cursor(0), _editing_value(false),
      _confirm_choice(0), _cx(80), _cy(64), _w(160), _h(128)
{
    memset(&_status, 0, sizeof(_status));
    memset(&_edit_config, 0, sizeof(_edit_config));
    memset(_mode_card_pulse, 0, sizeof(_mode_card_pulse));
    memset(_stat_bar_anims, 0, sizeof(_stat_bar_anims));
}

App_Pomodoro::~App_Pomodoro() {}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void App_Pomodoro::init() {
    pomodoro_service_init();
    idle_mgr_disable();  // Prevent sleep during productivity sessions
    
    _screen = SCREEN_MODE_SELECT;
    _anim_time = 0;
    _mode_cursor = 1;  // Start on Standard
    _transitioning = false;
    
    pomodoro_get_status(&_status);
    
    ESP_LOGI(TAG, "Pomodoro app initialized");
}

void App_Pomodoro::exit() {
    idle_mgr_enable();
    ESP_LOGI(TAG, "Pomodoro app exited");
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

uint16_t App_Pomodoro::_blend_color(uint16_t c1, uint16_t c2, float ratio) {
    if (ratio <= 0.0f) return c1;
    if (ratio >= 1.0f) return c2;
    return color_blend(c1, c2, ratio);
}

void App_Pomodoro::_format_time(uint32_t seconds, char* buf, size_t sz) {
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    
    if (h > 0) {
        snprintf(buf, sz, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else {
        snprintf(buf, sz, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
    }
}

uint16_t App_Pomodoro::_session_color(pomo_session_type_t type) {
    switch (type) {
        case POMO_SESSION_FOCUS:       return theme_get_fail();
        case POMO_SESSION_SHORT_BREAK: return theme_get_success();
        case POMO_SESSION_LONG_BREAK:  return theme_get_accent();
        default: return theme_get_accent();
    }
}

void App_Pomodoro::_change_screen(Screen new_screen) {
    _prev_screen = _screen;
    _screen = new_screen;
    _transitioning = true;
    _transition_progress = 0;
}

void App_Pomodoro::_update_animations() {
    _anim_time += 0.025f;
    
    // Transition animation
    if (_transitioning) {
        _transition_progress += 0.1f;
        if (_transition_progress >= 1.0f) {
            _transition_progress = 1.0f;
            _transitioning = false;
        }
    }
    
    // Mode card pulses
    for (int i = 0; i < POMO_MODE_COUNT; i++) {
        float target = (i == _mode_cursor) ? 1.0f : 0.0f;
        _mode_card_pulse[i] += (target - _mode_card_pulse[i]) * 0.15f;
    }
    
    // Smooth scroll for mode carousel
    float target_scroll = (float)_mode_cursor;
    _mode_scroll_offset += (target_scroll - _mode_scroll_offset) * 0.2f;
    
    // Timer animations
    _circle_pulse = 0.85f + 0.15f * sinf(_anim_time * 2.0f);
    _particle_phase = fmodf(_anim_time * 0.5f, 6.28318f);
    
    // Completion animation
    if (_just_completed) {
        _completion_anim += 0.05f;
        if (_completion_anim >= 1.0f) {
            _just_completed = false;
            _completion_anim = 0;
        }
    }
    
    // Stats bar animations
    for (int i = 0; i < 7; i++) {
        if (_stat_bar_anims[i] < 1.0f) {
            _stat_bar_anims[i] += 0.03f;
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────────────────────

void App_Pomodoro::loop() {
    _update_animations();
    pomodoro_get_status(&_status);
    
    // Detect completion
    if (_status.state == POMO_STATE_COMPLETED && !_just_completed) {
        _just_completed = true;
        _completion_anim = 0;
    }
    
    // Layout cache — set from canvas in render()
    _cx = _w / 2;
    _cy = _h / 2;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Handling
// ─────────────────────────────────────────────────────────────────────────────

void App_Pomodoro::handleInput(input_event_t event) {
    if (_transitioning) return;
    
    switch (_screen) {
        case SCREEN_MODE_SELECT:
            if (event == BTN_LEFT) {
                switch_to_grid();
            } else if (event == BTN_UP && _mode_cursor > 0) {
                _mode_cursor--;
            } else if (event == BTN_DOWN && _mode_cursor < POMO_MODE_COUNT - 1) {
                _mode_cursor++;
            } else if (event == BTN_SELECT) {
                pomodoro_start_session((pomo_mode_t)_mode_cursor, POMO_SESSION_FOCUS);
                _change_screen(SCREEN_TIMER);
            } else if (event == BTN_RIGHT) {
                _change_screen(SCREEN_STATS);
            }
            break;
            
        case SCREEN_TIMER:
            if (event == BTN_LEFT) {
                if (_status.state == POMO_STATE_IDLE) {
                    _change_screen(SCREEN_MODE_SELECT);
                } else {
                    pomodoro_stop();
                    _change_screen(SCREEN_MODE_SELECT);
                }
            } else if (event == BTN_SELECT) {
                if (_status.state == POMO_STATE_RUNNING) {
                    pomodoro_pause();
                } else if (_status.state == POMO_STATE_PAUSED) {
                    pomodoro_resume();
                } else if (_status.state == POMO_STATE_COMPLETED) {
                    pomodoro_skip();
                }
            } else if (event == BTN_UP) {
                pomodoro_adjust_time(60);
            } else if (event == BTN_DOWN) {
                pomodoro_adjust_time(-60);
            } else if (event == BTN_RIGHT) {
                _confirm_choice = 0;
                _change_screen(SCREEN_SKIP_CONFIRM);
            }
            break;
            
        case SCREEN_STATS:
            if (event == BTN_LEFT) {
                _change_screen(SCREEN_MODE_SELECT);
            } else if (event == BTN_RIGHT) {
                _change_screen(SCREEN_SETTINGS);
            } else if (event == BTN_UP && _stats_scroll > 0) {
                _stats_scroll--;
            } else if (event == BTN_DOWN && _stats_scroll < 3) {
                _stats_scroll++;
            }
            break;
            
        case SCREEN_SETTINGS:
            if (event == BTN_LEFT) {
                _change_screen(SCREEN_STATS);
            } else if (event == BTN_UP && _settings_cursor > 0) {
                _settings_cursor--;
            } else if (event == BTN_DOWN && _settings_cursor < 4) {
                _settings_cursor++;
            } else if (event == BTN_SELECT) {
                if (_settings_cursor == 0) {
                    bool current = _status.auto_progress;
                    pomodoro_set_auto_progress(!current);
                } else if (_settings_cursor == 4) {
                    pomodoro_clear_all_stats();
                }
            }
            break;
            
        case SCREEN_SKIP_CONFIRM:
            if (event == BTN_LEFT || event == BTN_RIGHT) {
                _confirm_choice = 1 - _confirm_choice;
            } else if (event == BTN_SELECT) {
                if (_confirm_choice == 1) {
                    pomodoro_skip();
                }
                _change_screen(SCREEN_TIMER);
            }
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual Components
// ─────────────────────────────────────────────────────────────────────────────

void App_Pomodoro::_draw_circular_progress(LGFX_Sprite* canvas, int cx, int cy, int radius, float progress) {
    uint16_t session_col = _session_color(_status.session_type);
    uint16_t bg = theme_get_bg();
    
    // Outer glow ring
    float pulse = _circle_pulse;
    int glow_r = radius + 3 + (int)(2 * pulse);
    canvas->drawCircle(cx, cy, glow_r, _blend_color(session_col, bg, 0.3f));
    canvas->drawCircle(cx, cy, glow_r - 1, _blend_color(session_col, bg, 0.15f));
    
    // Background track
    canvas->drawCircle(cx, cy, radius, _blend_color(theme_get_text_dim(), bg, 0.2f));
    canvas->drawCircle(cx, cy, radius - 1, _blend_color(theme_get_text_dim(), bg, 0.1f));
    
    // Progress arc (smooth)
    if (progress > 0.001f) {
        int angle_end = (int)(progress * 360.0f);
        for (int w = 0; w < 4; w++) {
            int r = radius - w;
            uint16_t col = _blend_color(session_col, bg, w * 0.15f);
            canvas->drawArc(cx, cy, r, r - 1, 270, 270 + angle_end, col);
        }
    }
    
    // Particle effects along arc
    if (_status.state == POMO_STATE_RUNNING) {
        for (int i = 0; i < 3; i++) {
            float particle_angle = _particle_phase + i * 2.0f;
            float particle_prog = fmodf(particle_angle / 6.28318f, progress);
            float angle_rad = (particle_prog * 360.0f - 90.0f) * 0.0174533f;
            int px = cx + (int)(radius * cosf(angle_rad));
            int py = cy + (int)(radius * sinf(angle_rad));
            canvas->fillCircle(px, py, 2, session_col);
            canvas->fillCircle(px, py, 1, theme_get_text());
        }
    }
    
    // Center icon (session type indicator)
    _draw_session_indicator(canvas, _status.session_type, cx, cy - 8, true);
}

void App_Pomodoro::_draw_time_display(LGFX_Sprite* canvas, uint32_t seconds, int cx, int cy) {
    char time_buf[16];
    _format_time(seconds, time_buf, sizeof(time_buf));
    
    // Multi-pass shadow for depth (like weather app)
    canvas->setFont(&fonts::Font2);
    canvas->setTextDatum(textdatum_t::middle_center);
    
    uint16_t shadow = _blend_color(theme_get_text(), theme_get_bg(), 0.35f);
    canvas->setTextColor(shadow);
    canvas->drawString(time_buf, cx + 1, cy + 1);
    canvas->drawString(time_buf, cx - 1, cy);
    canvas->drawString(time_buf, cx, cy + 1);
    
    // Main text - bright
    canvas->setTextColor(theme_get_text());
    canvas->drawString(time_buf, cx, cy);
}

void App_Pomodoro::_draw_time_display_large(LGFX_Sprite* canvas, uint32_t seconds, int cx, int cy, bool paused) {
    uint32_t mins = seconds / 60;
    uint32_t secs = seconds % 60;
    
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%lu:%02lu", (unsigned long)mins, (unsigned long)secs);
    
    canvas->setFont(&fonts::Font2);
    canvas->setTextSize(3.0f, 3.0f);  // 3x scale for large display
    canvas->setTextDatum(textdatum_t::middle_center);
    
    if (paused) {
        // Hollow outline for paused state
        uint16_t bg = theme_get_bg();
        canvas->setTextColor(bg);
        canvas->drawString(time_buf, cx, cy);
        
        uint16_t outline = theme_get_text();
        canvas->setTextColor(outline);
        canvas->drawString(time_buf, cx - 2, cy);
        canvas->drawString(time_buf, cx + 2, cy);
        canvas->drawString(time_buf, cx, cy - 2);
        canvas->drawString(time_buf, cx, cy + 2);
    } else {
        // Solid bold with shadow for running state
        uint16_t shadow = _blend_color(theme_get_text(), theme_get_bg(), 0.2f);
        canvas->setTextColor(shadow);
        canvas->drawString(time_buf, cx + 2, cy + 2);
        
        canvas->setTextColor(theme_get_text());
        canvas->drawString(time_buf, cx, cy);
        canvas->drawString(time_buf, cx + 1, cy);
        canvas->drawString(time_buf, cx, cy + 1);
        canvas->drawString(time_buf, cx + 1, cy + 1);
    }
    
    canvas->setTextSize(1.0f, 1.0f);
}

void App_Pomodoro::_draw_clean_cycle_dots(LGFX_Sprite* canvas) {
    if (_status.session_type == POMO_SESSION_LONG_BREAK) return;
    
    pomo_mode_config_t cfg;
    pomodoro_get_mode_config(_status.current_mode, &cfg);
    
    // Modern progress bar style
    int bar_w = 80;
    int bar_h = 6;
    int bar_x = _cx - bar_w / 2;
    int bar_y = _h - 20;
    
    // Background track
    canvas->fillRoundRect(bar_x, bar_y, bar_w, bar_h, 3, 
        _blend_color(theme_get_text_dim(), theme_get_bg(), 0.2f));
    
    // Filled progress
    if (_status.cycles_completed > 0) {
        float progress = (float)_status.cycles_completed / (float)cfg.cycles_until_long_break;
        int fill_w = (int)(bar_w * progress);
        canvas->fillRoundRect(bar_x, bar_y, fill_w, bar_h, 3, theme_get_accent());
    }
    
    // Segment dividers
    int seg_w = bar_w / cfg.cycles_until_long_break;
    for (int i = 1; i < cfg.cycles_until_long_break; i++) {
        int div_x = bar_x + i * seg_w;
        canvas->drawFastVLine(div_x, bar_y, bar_h, theme_get_bg());
    }
    
    // Label
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(theme_get_text_dim());
    char label[16];
    snprintf(label, sizeof(label), "%d/%d Cycles", _status.cycles_completed, cfg.cycles_until_long_break);
    canvas->drawString(label, _cx, bar_y - 10);
}

void App_Pomodoro::_draw_session_indicator(LGFX_Sprite* canvas, pomo_session_type_t type, int x, int y, bool active) {
    uint16_t col = active ? _session_color(type) : theme_get_text_dim();
    
    // Simple icon representation
    switch (type) {
        case POMO_SESSION_FOCUS:
            // Target/crosshair icon
            canvas->drawCircle(x, y, 6, col);
            canvas->drawFastHLine(x - 8, y, 16, col);
            canvas->drawFastVLine(x, y - 8, 16, col);
            break;
        case POMO_SESSION_SHORT_BREAK:
            // Coffee cup
            canvas->drawRect(x - 4, y - 2, 8, 6, col);
            canvas->drawFastHLine(x - 5, y + 4, 10, col);
            canvas->drawLine(x - 2, y - 4, x, y - 6, col);
            canvas->drawLine(x + 2, y - 4, x, y - 6, col);
            break;
        case POMO_SESSION_LONG_BREAK:
            // Bed/rest icon
            canvas->drawRect(x - 6, y - 2, 12, 4, col);
            canvas->drawFastVLine(x - 6, y + 2, 3, col);
            canvas->drawFastVLine(x + 6, y + 2, 3, col);
            break;
    }
}

void App_Pomodoro::_draw_mode_card(LGFX_Sprite* canvas, pomo_mode_t mode, int x, int y, bool selected) {
    pomo_mode_config_t cfg;
    pomodoro_get_mode_config(mode, &cfg);
    
    int w = 120, h = 42;
    float pulse = _mode_card_pulse[mode];
    
    // Outer glow for selected card
    if (selected) {
        uint16_t glow = _blend_color(theme_get_accent(), theme_get_bg(), 0.25f * pulse);
        canvas->fillRoundRect(x - 1, y - 1, w + 2, h + 2, 7, glow);
    }
    
    // Card background
    uint16_t card_bg = _blend_color(theme_get_accent(), theme_get_bg(), selected ? 0.18f : 0.08f);
    canvas->fillRoundRect(x, y, w, h, 6, card_bg);
    
    // Double border for depth
    uint16_t border1 = _blend_color(theme_get_accent(), theme_get_text(), selected ? 0.85f : 0.35f);
    uint16_t border2 = _blend_color(theme_get_accent(), theme_get_bg(), selected ? 0.6f : 0.2f);
    canvas->drawRoundRect(x, y, w, h, 6, border1);
    canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, border2);
    
    // Mode name - higher position
    canvas->setFont(&fonts::Font2);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(selected ? theme_get_text() : theme_get_text_dim());
    canvas->drawString(pomodoro_mode_name(mode), x + w/2, y + 3);
    
    // Duration info - compact single line
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(theme_get_text_dim());
    
    char info[24];
    snprintf(info, sizeof(info), "%u/%u/%u • %ux",
             cfg.focus_minutes, cfg.short_break_minutes, 
             cfg.long_break_minutes, cfg.cycles_until_long_break);
    canvas->drawString(info, x + w/2, y + h - 10);
}

void App_Pomodoro::_draw_stat_bar(LGFX_Sprite* canvas, const char* label, uint32_t value, uint32_t max, int y, float anim) {
    int label_w = 36;
    int bar_x = label_w + 2;
    int bar_w = _w - bar_x - 24;
    int bar_h = 8;
    
    // Label
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString(label, 4, y + bar_h/2);
    
    // Bar track
    canvas->fillRoundRect(bar_x, y, bar_w, bar_h, 3, _blend_color(theme_get_text_dim(), theme_get_bg(), 0.1f));
    
    // Fill with gradient effect
    if (max > 0 && value > 0) {
        float ratio = (float)value / (float)max;
        if (ratio > 1.0f) ratio = 1.0f;
        int fill_w = (int)(bar_w * ratio * anim);
        if (fill_w > 2) {
            // Double-pass for glow
            canvas->fillRoundRect(bar_x, y, fill_w, bar_h, 3, 
                _blend_color(theme_get_accent(), theme_get_bg(), 0.3f));
            if (fill_w > 4) {
                canvas->fillRoundRect(bar_x + 1, y + 1, fill_w - 2, bar_h - 2, 2, 
                    _blend_color(theme_get_accent(), theme_get_text(), 0.8f));
            }
        }
    }
    
    // Value
    canvas->setTextDatum(textdatum_t::middle_right);
    canvas->setTextColor(theme_get_text());
    char val_str[12];
    snprintf(val_str, sizeof(val_str), "%lu", (unsigned long)value);
    canvas->drawString(val_str, _w - 4, y + bar_h/2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen Renderers
// ─────────────────────────────────────────────────────────────────────────────

void App_Pomodoro::_render_mode_select(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    
    // Minimal title
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("Focus Mode", _cx, 6);
    
    // Horizontal carousel
    int card_w = _w * 4 / 10, card_h = _h * 9 / 16;
    int center_x = _cx;
    int start_y = _h / 5 + 2;
    
    for (int i = 0; i < POMO_MODE_COUNT; i++) {
        float offset = (float)i - _mode_scroll_offset;
        int x = center_x + (int)(offset * 80) - card_w/2;
        
        if (x < -card_w || x > _w) continue;
        
        bool sel = (i == _mode_cursor);
        float pulse = _mode_card_pulse[i];
        float scale = sel ? 1.0f : 0.85f;
        
        int adj_w = (int)(card_w * scale);
        int adj_h = (int)(card_h * scale);
        int adj_x = x + (card_w - adj_w) / 2;
        int adj_y = start_y + (card_h - adj_h) / 2;
        
        // Shadow
        if (sel) canvas->fillRoundRect(adj_x + 2, adj_y + 2, adj_w, adj_h, 8, theme_get_bg());
        
        // Card
        uint16_t bg = sel ? _blend_color(theme_get_bg(), theme_get_accent(), 0.92f) : _blend_color(theme_get_bg(), theme_get_accent(), 0.97f);
        canvas->fillRoundRect(adj_x, adj_y, adj_w, adj_h, 8, bg);
        
        // Border
        if (sel) {
            uint16_t brd = _blend_color(theme_get_accent(), theme_get_text(), 0.7f + 0.3f * pulse);
            canvas->drawRoundRect(adj_x, adj_y, adj_w, adj_h, 8, brd);
        }
        
        // Name
        const char* name = "";
        uint32_t dur = 0;
        switch (i) {
            case POMO_MODE_QUICK: name = "Quick"; dur = 15; break;
            case POMO_MODE_STANDARD: name = "Standard"; dur = 25; break;
            case POMO_MODE_EXTENDED: name = "Extended"; dur = 50; break;
            case POMO_MODE_CUSTOM: {
                name = "Custom";
                pomo_mode_config_t cfg;
                pomodoro_get_mode_config(POMO_MODE_CUSTOM, &cfg);
                dur = cfg.focus_minutes;
                break;
            }
        }
        
        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::top_center);
        canvas->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        canvas->drawString(name, x + card_w/2, adj_y + 8);
        
        // Big number
        char num[8];
        snprintf(num, sizeof(num), "%lu", (unsigned long)dur);
        canvas->setFont(&fonts::Font2);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setTextColor(sel ? theme_get_accent() : theme_get_text_dim());
        canvas->drawString(num, x + card_w/2, adj_y + adj_h/2 + 4);
        
        // "min"
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString("min", x + card_w/2, adj_y + adj_h - 14);
    }
    
    // Footer
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("L/R=Mode  SEL=Start  U=Stats", _cx, _h - 2);
}

void App_Pomodoro::_render_timer(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    
    // Session type - minimalist
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(_session_color(_status.session_type));
    canvas->drawString(pomodoro_session_name(_status.session_type), _cx, 6);
    
    // HUGE time display - main focus
    bool paused = (_status.state == POMO_STATE_PAUSED);
    _draw_time_display_large(canvas, _status.remaining_seconds, _cx, _cy, paused);
    
    // Clean cycle indicator below
    _draw_clean_cycle_dots(canvas);
    
    // Minimal footer
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(theme_get_text_dim());
    
    if (_status.state == POMO_STATE_COMPLETED) {
        canvas->setTextColor(theme_get_success());
        canvas->drawString("Complete! SEL=Next", _cx, _h - 2);
    } else if (paused) {
        canvas->drawString("L=Back  SEL=Resume  R=Skip", _cx, _h - 2);
    } else {
        canvas->drawString("L=Back  SEL=Pause  U/D=Adjust", _cx, _h - 2);
    }
}

void App_Pomodoro::_render_stats(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    theme_draw_wallpaper(canvas, _anim_time);
    
    // Header with shadow
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(_blend_color(theme_get_accent(), theme_get_bg(), 0.3f));
    canvas->drawString("STATISTICS", _cx + 1, 5);
    canvas->setTextColor(theme_get_accent());
    canvas->drawString("STATISTICS", _cx, 4);
    
    // Separator
    canvas->drawFastHLine(_w / 8, 14, _w - _w / 4, _blend_color(theme_get_accent(), theme_get_bg(), 0.7f));
    
    int y = _h / 6 + 2;
    
    // Total focus time (hours)
    uint32_t hours = _status.stats.total_focus_seconds / 3600;
    _draw_stat_bar(canvas, "Hours", hours, hours + 10, y, _stat_bar_anims[0]);
    y += 16;
    
    // Sessions completed
    _draw_stat_bar(canvas, "Sessions", _status.stats.total_sessions_completed, 
                   _status.stats.total_sessions_completed + 5, y, _stat_bar_anims[1]);
    y += 16;
    
    // Today
    _draw_stat_bar(canvas, "Today", _status.stats.sessions_today, 12, y, _stat_bar_anims[2]);
    y += 16;
    
    // Current streak
    _draw_stat_bar(canvas, "Streak", _status.stats.current_streak, 30, y, _stat_bar_anims[3]);
    y += 16;
    
    // Best streak
    _draw_stat_bar(canvas, "Best", _status.stats.longest_streak, 30, y, _stat_bar_anims[4]);
    y += 18;
    
    // Motivational badge
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setFont(&fonts::Font0);
    
    const char* msg = "Start your journey!";
    uint16_t msg_col = theme_get_text_dim();
    
    if (_status.stats.current_streak >= 7) {
        msg = "EPIC STREAK!";
        msg_col = theme_get_success();
    } else if (_status.stats.current_streak >= 3) {
        msg = "Keep it up!";
        msg_col = theme_get_accent();
    } else if (_status.stats.total_sessions_completed > 0) {
        msg = "Stay focused!";
        msg_col = theme_get_text();
    }
    
    canvas->setTextColor(msg_col);
    canvas->drawString(msg, _cx, y + 4);
    
    // Footer
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("L=Back  R=Settings", _cx, _h - 2);
}

void App_Pomodoro::_render_settings(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    theme_draw_wallpaper(canvas, _anim_time);
    
    // Header
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(theme_get_accent());
    canvas->drawString("SETTINGS", _cx, 4);
    canvas->drawFastHLine(_w / 8, 14, _w - _w / 4, theme_get_accent());
    
    const char* items[] = {
        "Auto Progress",
        "Edit Custom Mode",
        "Reset Daily Stats",
        "Clear All Stats"
    };
    
    int y = _h / 6 + 2;
    for (int i = 0; i < 4; i++) {
        bool sel = (i == _settings_cursor);
        
        if (sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(8, y - 1, _w - 16, 16, 4, 
                _blend_color(theme_get_accent(), theme_get_bg(), 0.2f * pulse));
            canvas->drawRoundRect(8, y - 1, _w - 16, 16, 4, 
                _blend_color(theme_get_accent(), theme_get_text(), pulse));
        }
        
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        canvas->drawString(items[i], 14, y + 7);
        
        // Show state for auto-progress
        if (i == 0) {
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->drawString(_status.auto_progress ? "ON" : "OFF", _w - 14, y + 7);
        }
        
        y += 20;
    }
    
    // Footer
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("L=Back  SEL=Toggle", _cx, _h - 2);
}

void App_Pomodoro::_render_skip_confirm(LGFX_Sprite* canvas) {
    // Draw blurred background instead of recursive render
    canvas->fillSprite(theme_get_bg());
    theme_draw_wallpaper(canvas, _anim_time * 0.5f);
    
    // Dim overlay
    for (int y = 0; y < _h; y += 2) {
        canvas->drawFastHLine(0, y, _w, _blend_color(theme_get_bg(), theme_get_text_dim(), 0.5f));
    }
    
    // Modal panel
    int panel_w = 120, panel_h = 54;
    int panel_x = (_w - panel_w) / 2;
    int panel_y = (_h - panel_h) / 2;
    
    // Shadow
    canvas->fillRoundRect(panel_x + 2, panel_y + 2, panel_w, panel_h, 8, theme_get_bg());
    
    // Panel background
    uint16_t panel_bg = _blend_color(theme_get_bg(), theme_get_text_dim(), 0.15f);
    canvas->fillRoundRect(panel_x, panel_y, panel_w, panel_h, 8, panel_bg);
    
    // Double border
    canvas->drawRoundRect(panel_x, panel_y, panel_w, panel_h, 8, theme_get_accent());
    canvas->drawRoundRect(panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2, 7, 
        _blend_color(theme_get_accent(), theme_get_bg(), 0.5f));
    
    // Title
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(theme_get_text());
    canvas->drawString("Skip session?", panel_x + panel_w/2, panel_y + 6);
    
    // Buttons - side by side
    int btn_w = 48, btn_h = 20;
    int btn_y = panel_y + 26;
    
    for (int i = 0; i < 2; i++) {
        int btn_x = panel_x + 10 + i * 52;
        bool sel = (_confirm_choice == i);
        
        if (sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 6.0f);
            canvas->fillRoundRect(btn_x, btn_y, btn_w, btn_h, 5, 
                _blend_color(theme_get_accent(), theme_get_bg(), 0.25f));
            canvas->drawRoundRect(btn_x, btn_y, btn_w, btn_h, 5, 
                _blend_color(theme_get_accent(), theme_get_text(), pulse));
        } else {
            canvas->drawRoundRect(btn_x, btn_y, btn_w, btn_h, 5, theme_get_text_dim());
        }
        
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        canvas->drawString(i == 0 ? "NO" : "YES", btn_x + btn_w/2, btn_y + btn_h/2);
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// Main Render
// ─────────────────────────────────────────────────────────────────────────────

void App_Pomodoro::render(LGFX_Sprite* canvas) {
    // Update dimensions from actual canvas for resolution independence
    _w = canvas->width();
    _h = canvas->height();
    _cx = _w / 2;
    _cy = _h / 2;

    if (_transitioning) {
        // Slide transition effect
        float t = _transition_progress;
        float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // Cubic ease-out
        
        int offset = (int)(eased * _w);
        bool going_forward = (_screen > _prev_screen);
        
        // Create temporary sprites for transition
        LGFX_Sprite old_screen(canvas);
        LGFX_Sprite new_screen(canvas);
        old_screen.setPsram(true);
        new_screen.setPsram(true);
        old_screen.setColorDepth(16);
        new_screen.setColorDepth(16);
        
        if (old_screen.createSprite(_w, _h) && new_screen.createSprite(_w, _h)) {
            // Render both screens
            switch (_prev_screen) {
                case SCREEN_MODE_SELECT:  _render_mode_select(&old_screen); break;
                case SCREEN_TIMER:        _render_timer(&old_screen); break;
                case SCREEN_STATS:        _render_stats(&old_screen); break;
                case SCREEN_SETTINGS:     _render_settings(&old_screen); break;
                case SCREEN_SKIP_CONFIRM: _render_skip_confirm(&old_screen); break;
            }
            
            switch (_screen) {
                case SCREEN_MODE_SELECT:  _render_mode_select(&new_screen); break;
                case SCREEN_TIMER:        _render_timer(&new_screen); break;
                case SCREEN_STATS:        _render_stats(&new_screen); break;
                case SCREEN_SETTINGS:     _render_settings(&new_screen); break;
                case SCREEN_SKIP_CONFIRM: _render_skip_confirm(&new_screen); break;
            }
            
            // Composite transition
            canvas->fillSprite(theme_get_bg());
            
            if (going_forward) {
                old_screen.pushSprite(canvas, -offset, 0);
                new_screen.pushSprite(canvas, _w - offset, 0);
            } else {
                old_screen.pushSprite(canvas, offset, 0);
                new_screen.pushSprite(canvas, -_w + offset, 0);
            }
            
            old_screen.deleteSprite();
            new_screen.deleteSprite();
        } else {
            // Fallback: direct render if sprite allocation fails
            switch (_screen) {
                case SCREEN_MODE_SELECT:  _render_mode_select(canvas); break;
                case SCREEN_TIMER:        _render_timer(canvas); break;
                case SCREEN_STATS:        _render_stats(canvas); break;
                case SCREEN_SETTINGS:     _render_settings(canvas); break;
                case SCREEN_SKIP_CONFIRM: _render_skip_confirm(canvas); break;
            }
        }
    } else {
        // Normal render
        switch (_screen) {
            case SCREEN_MODE_SELECT:  _render_mode_select(canvas); break;
            case SCREEN_TIMER:        _render_timer(canvas); break;
            case SCREEN_STATS:        _render_stats(canvas); break;
            case SCREEN_SETTINGS:     _render_settings(canvas); break;
            case SCREEN_SKIP_CONFIRM: _render_skip_confirm(canvas); break;
        }
    }
}
