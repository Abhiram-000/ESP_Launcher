/**
 * @file App_Pomodoro.h
 * @brief Visually Stunning Pomodoro Timer with Premium UI/UX
 * 
 * Screen Flow:
 *   MODE_SELECT → TIMER_RUNNING ⇄ PAUSED → STATS → SETTINGS
 *                      ↓
 *                 SKIP_CONFIRM
 * 
 * Visual Features:
 * - Animated circular progress with particle effects
 * - Smooth mode carousel with preview cards
 * - Statistics dashboard with trends
 * - Customizable timers with real-time preview
 * - Fluid transitions and micro-interactions
 */

#pragma once

#include "IApp.h"
#include "../../services/pomodoro_service.h"

class App_Pomodoro : public IApp {
private:
    enum Screen {
        SCREEN_MODE_SELECT,   // Choose timer mode (Quick/Standard/Extended/Custom)
        SCREEN_TIMER,         // Main timer display with circular progress
        SCREEN_STATS,         // Statistics dashboard
        SCREEN_SETTINGS,      // Customize durations, auto-progress
        SCREEN_SKIP_CONFIRM   // Confirmation dialog
    };
    
    Screen      _screen;
    Screen      _prev_screen;
    float       _anim_time;
    float       _transition_progress;
    bool        _transitioning;
    
    // Mode selection state
    int         _mode_cursor;
    float       _mode_scroll_offset;
    float       _mode_card_pulse[POMO_MODE_COUNT];
    
    // Timer display state
    pomo_status_t _status;
    float       _circle_pulse;
    float       _particle_phase;
    uint16_t    _last_seconds;
    bool        _just_completed;
    float       _completion_anim;
    
    // Statistics screen
    int         _stats_scroll;
    float       _stat_bar_anims[7];
    
    // Settings screen
    int         _settings_cursor;
    bool        _editing_value;
    pomo_mode_config_t _edit_config;
    
    // Skip confirmation
    int         _confirm_choice;  // 0=No, 1=Yes
    
    // Cached layout
    int32_t     _cx, _cy, _w, _h;

public:
    App_Pomodoro();
    ~App_Pomodoro() override;
    
    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

private:
    void _change_screen(Screen new_screen);
    void _update_animations();
    
    // Screen renderers
    void _render_mode_select(LGFX_Sprite* canvas);
    void _render_timer(LGFX_Sprite* canvas);
    void _render_stats(LGFX_Sprite* canvas);
    void _render_settings(LGFX_Sprite* canvas);
    void _render_skip_confirm(LGFX_Sprite* canvas);
    
    // Visual components
    void _draw_circular_progress(LGFX_Sprite* canvas, int cx, int cy, int radius, float progress);
    void _draw_mode_card(LGFX_Sprite* canvas, pomo_mode_t mode, int x, int y, bool selected);
    void _draw_time_display(LGFX_Sprite* canvas, uint32_t seconds, int cx, int cy);
    void _draw_time_display_large(LGFX_Sprite* canvas, uint32_t seconds, int cx, int cy, bool paused);
    void _draw_clean_cycle_dots(LGFX_Sprite* canvas);
    void _draw_stat_bar(LGFX_Sprite* canvas, const char* label, uint32_t value, uint32_t max, int y, float anim);
    void _draw_session_indicator(LGFX_Sprite* canvas, pomo_session_type_t type, int x, int y, bool active);
    
    // Helpers
    uint16_t _blend_color(uint16_t c1, uint16_t c2, float ratio);
    void _format_time(uint32_t seconds, char* buf, size_t sz);
    uint16_t _session_color(pomo_session_type_t type);
};
