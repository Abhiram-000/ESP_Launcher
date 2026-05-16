#pragma once
/*
 * App_GameStore.h — Cloud NES ROM Store & Game Launcher
 * ======================================================
 * IApp that lets the user browse, download, and launch NES games.
 * Uses game_store service for all storage/network operations.
 */
#include "IApp.h"
#include "../../services/game_store.h"
#include "../../config/sys_config.h"

// Forward-declared here so we don't include App_NES.h (avoids pulling in
// heavy emulator headers into every translation unit that includes this file).
class App_NES;

class App_GameStore : public IApp {
public:
    // ─── Screen states ─────────────────────────────────────────────
    enum class Screen {
        LocalList,      // Installed games (home screen)
        StoreList,      // Available cloud games
        GameDetail,     // Single game: description + actions
        Downloading,    // Animated progress bar
        SpaceWarn,      // Not enough space – offer delete candidates
        CloudLoading,   // Spinner while fetching catalog
        Error,          // Error with retry / back
        NoWifi,         // Wi-Fi offline notice
    };

    App_GameStore();
    ~App_GameStore() override;

    void init()    override;
    void exit()    override;
    void loop()    override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

    // Called by App_NES when the user exits the emulator
    void onNesExit();

private:
    // ── State ───────────────────────────────────────────────────────
    Screen       _screen;
    Screen       _prev_screen;

    // Local merged catalog copy
    game_entry_t _catalog[GAME_STORE_MAX_GAMES];
    int          _catalog_count;
    int          _list_sel;      // Selected index in current list
    int          _list_scroll;   // First visible item

    // Detail / action target
    int          _detail_idx;    // Index into _catalog

    // Space-warning: list of delete candidates (indices into _catalog)
    int          _del_candidates[GAME_STORE_MAX_GAMES];
    int          _del_count;
    int          _del_sel;

    // Error state
    game_store_err_t _last_err;

    // Animation
    float        _anim_time;
    float        _spinner_angle;

    // NES sub-app (allocated when launching a game)
    App_NES*     _nes_app;

    // ── Render helpers ──────────────────────────────────────────────
    void _draw_header(LGFX_Sprite* c, const char* title, uint16_t accent);
    void _draw_footer(LGFX_Sprite* c, const char* text);
    void _draw_game_row(LGFX_Sprite* c, int y, const game_entry_t& e,
                        bool selected, bool show_status);
    void _draw_spinner(LGFX_Sprite* c, int cx, int cy, int r);
    void _draw_progress_bar(LGFX_Sprite* c, int x, int y, int w, int h,
                             int pct, uint16_t col);

    // ── Screen-specific renderers ───────────────────────────────────
    void _render_local_list(LGFX_Sprite* c);
    void _render_store_list(LGFX_Sprite* c);
    void _render_detail(LGFX_Sprite* c);
    void _render_downloading(LGFX_Sprite* c);
    void _render_space_warn(LGFX_Sprite* c);
    void _render_cloud_loading(LGFX_Sprite* c);
    void _render_error(LGFX_Sprite* c);
    void _render_no_wifi(LGFX_Sprite* c);

    // ── Business logic ──────────────────────────────────────────────
    void _refresh_catalog_local();
    void _begin_cloud_fetch();
    void _begin_install(int catalog_idx);
    void _build_delete_candidates(uint32_t needed_bytes);
    void _launch_game(int catalog_idx);
    const char* _err_string(game_store_err_t e);

    // Pending install index (set before entering SpaceWarn)
    int  _pending_install_idx;
};
