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
    int menu_cursor;
    int theme_cursor;
    int temp_brightness;
    
    uint32_t free_heap;
    uint32_t flash_size;
    const char* idf_version;
    float anim_time;

public:
    App_Settings();
    ~App_Settings() override;

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
