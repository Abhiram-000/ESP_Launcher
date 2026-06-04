#pragma once

#include "IApp.h"

#define MAX_APPS_PER_PAGE 6
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
    int _held_delay;
    int _held_repeat;
    bool _move_pending;

    void _move(input_event_t dir);

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
