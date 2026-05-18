#pragma once

#include "IApp.h"
#include <stdint.h>
#include <stddef.h>

class App_Settings : public IApp {
private:
    enum State {
        STATE_MENU,
        STATE_SYS_INFO,
        STATE_THEMES,
        STATE_BRIGHTNESS
    };

    State current_state;
    State previous_state;
    bool is_transitioning;
    float transition_progress; // 0.0 to 1.0

    int menu_cursor;
    int theme_cursor;
    int temp_brightness;
    
    uint32_t free_heap;
    uint32_t flash_size;
    const char* idf_version;
    float anim_time;

    void _change_state(State new_state);
    void _draw_state_content(LGFX_Sprite* canvas, State state, int x_offset);

public:
    App_Settings();
    ~App_Settings() override;

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
