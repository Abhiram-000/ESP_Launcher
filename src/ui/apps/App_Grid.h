#pragma once

#include "IApp.h"

#define MAX_APPS_PER_PAGE 9
#define MAX_TOTAL_APPS 12

struct AppEntry {
    const char*      name;
    const uint16_t*  icon_rgb565;
    uint16_t*        cached_icon32;
    IApp*            app_instance;
};

class App_Grid : public IApp {
private:
    AppEntry apps[MAX_TOTAL_APPS];
    uint8_t app_count;
    uint8_t selected_index;
    float anim_time;

    input_event_t _held_btn;
    int _held_frames;
    static constexpr int HOLD_DELAY = 12;
    static constexpr int HOLD_REPEAT = 3;

public:
    App_Grid();
    ~App_Grid() override;

    void registerApp(const char* name, const uint16_t* icon, IApp* app);

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
