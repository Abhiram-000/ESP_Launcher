#pragma once

#include "IApp.h"
#include "../../services/hack_store.h"

class App_HackStore : public IApp {
public:
    enum class Screen {
        LocalList,
        StoreList,
        GameDetail,
        Downloading,
        CloudLoading,
        Error,
        NoWifi
    };

    App_HackStore();
    ~App_HackStore() override;

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

private:
    Screen _screen;
    Screen _prev_screen;

    hack_entry_t _catalog[HACK_STORE_MAX_FIRMS];
    int _catalog_count;

    int _list_sel;
    int _list_scroll;
    int _detail_idx;

    hack_store_err_t _last_err;
    float _anim_time;
    float _spinner_angle;

    int _pending_install_idx;
    char _installed_id[32];

    void _refresh_catalog_local();
    void _begin_cloud_fetch();
    void _begin_install(int catalog_idx);
    
    void _draw_header(LGFX_Sprite* c, const char* title, uint16_t accent);
    void _draw_footer(LGFX_Sprite* c, const char* text);
    void _draw_game_row(LGFX_Sprite* c, int y, const hack_entry_t& e, bool selected);
    void _draw_spinner(LGFX_Sprite* c, int cx, int cy, int r);
    void _draw_progress_bar(LGFX_Sprite* c, int x, int y, int w, int h, int pct, uint16_t col);

    void _render_local_list(LGFX_Sprite* c);
    void _render_store_list(LGFX_Sprite* c);
    void _render_detail(LGFX_Sprite* c);
    void _render_downloading(LGFX_Sprite* c);
    void _render_cloud_loading(LGFX_Sprite* c);
    void _render_error(LGFX_Sprite* c);
    void _render_no_wifi(LGFX_Sprite* c);

    const char* _err_string(hack_store_err_t e);
};
