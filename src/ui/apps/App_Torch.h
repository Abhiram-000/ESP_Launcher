#pragma once

#include "IApp.h"
#include <stdint.h>

class App_Torch : public IApp {
private:
    enum Mode {
        MODE_WHITE = 0,
        MODE_RED_BEACON,
        MODE_GREEN_BEACON,
        MODE_BLUE_BEACON,
        MODE_RAINBOW,
        MODE_CYBER_PULSE,
        MODE_SOS,
        MODE_APEX_SIGNAL,
        MODE_COUNT
    };

    Mode current_mode;
    bool led_on;
    float anim_time;
    float pulse_size;
    float transition_val;

    void update_led();
    void hsv_to_rgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b);

public:
    App_Torch();
    ~App_Torch() override;

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
