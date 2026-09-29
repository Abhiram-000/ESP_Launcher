#pragma once

#include "IApp.h"

#define MAX_TOTAL_APPS 12

struct AppEntry {
    const char*      name;
    const uint16_t*  icon_rgb565;
    uint16_t*        cached_icon32;
    IApp*            app_instance;
    const char*      desc;      // Brief description (tile view)
    const char*      category;  // Category label
};

typedef enum {
    LAUNCHER_CAROUSEL = 0,
    LAUNCHER_TILE     = 1,
    LAUNCHER_GRID     = 2
} launcher_theme_t;

class App_Grid : public IApp {
private:
    AppEntry apps[MAX_TOTAL_APPS];
    uint8_t _app_count;
    uint8_t _selected_index;
    uint8_t _current_page;  // For grid theme pagination
    float _anim_time;
    float _current_scroll;
    float _target_scroll;

    LGFX_Sprite* _sp64;
    LGFX_Sprite* _sp32;

    input_event_t _held_btn;
    int _held_delay;
    int _held_repeat;

    launcher_theme_t _launcher_theme;

    void _move(input_event_t dir);
    void _render_carousel(LGFX_Sprite* canvas);
    void _render_tile(LGFX_Sprite* canvas);
    void _render_grid(LGFX_Sprite* canvas);

public:
    App_Grid();
    ~App_Grid() override;

    void registerApp(const char* name, const uint16_t* icon, IApp* app, const char* desc, const char* category);

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
    
    launcher_theme_t getLauncherTheme() const { return _launcher_theme; }
    void setLauncherTheme(launcher_theme_t theme);
};
