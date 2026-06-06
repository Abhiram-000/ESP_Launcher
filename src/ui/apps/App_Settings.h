#pragma once

#include "IApp.h"
#include "../components/UI_Keyboard.h"
#include "../components/UI_WifiScanner.h"
#include <stdint.h>
#include <stddef.h>

class App_Settings : public IApp {
private:
    enum State {
        STATE_MENU,
        STATE_SYS_INFO,
        STATE_THEMES,
        STATE_BRIGHTNESS,
        STATE_SLEEP_CONFIG,
        STATE_LAUNCHER_THEME,
        STATE_WIFI_CONNECT
    };

    State current_state;
    State previous_state;
    bool is_transitioning;
    float transition_progress; // 0.0 to 1.0

    int menu_cursor;
    int menu_scroll_offset; // For auto-scrolling
    int theme_cursor;
    int launcher_theme_cursor;
    int temp_brightness;
    int sleep_config_cursor;
    
    uint32_t free_heap;
    uint32_t flash_size;
    const char* idf_version;
    float anim_time;
    
    // WiFi scanner overlay
    UI_WifiScanner _wifi_scanner;
    bool _wifi_scanner_active;

    void _change_state(State new_state);
    void _draw_state_content(LGFX_Sprite* canvas, State state, int x_offset);
    void _apply_sleep_config(int option);
    int _get_current_sleep_config();

public:
    App_Settings();
    ~App_Settings() override;

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
