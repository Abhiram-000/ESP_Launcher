#include "App_GameStore.h"
#include "App_NES.h"
#include "../../ui/theme_mgr.h"
#include "../../services/wifi_mgr.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdio.h>


#include "../ui_task.h"

// ─────────────────────────────────────────────────────────────────────────
//  Colour palette (derived at runtime from theme_get_*)
// ─────────────────────────────────────────────────────────────────────────
static inline uint16_t C_ACCENT()  { return theme_get_accent(); }
static inline uint16_t C_TEXT()    { return theme_get_text(); }
static inline uint16_t C_DIM()     { return theme_get_text_dim(); }
static inline uint16_t C_BG()      { return theme_get_bg(); }
static inline uint16_t C_PANEL()   { return theme_get_bg(); }
// Semantic aliases — resolved from theme at call time, never cached as statics
static inline uint16_t C_SUCCESS() { return theme_get_success(); }
static inline uint16_t C_WARN()    { return theme_get_accent(); }
static inline uint16_t C_FAIL()    { return theme_get_fail(); }

// ─────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────
App_GameStore::App_GameStore()
    : _screen(Screen::LocalList), _prev_screen(Screen::LocalList),
      _catalog_count(0), _list_sel(0), _list_scroll(0),
      _detail_idx(0), _del_count(0), _del_sel(0),
      _last_err(GAME_STORE_ERR_OK),
      _anim_time(0.0f), _spinner_angle(0.0f),
      _nes_app(nullptr), _pending_install_idx(-1)
{
    memset(_catalog,  0, sizeof(_catalog));
    memset(_del_candidates, -1, sizeof(_del_candidates));
    memset(_install_id_buf, 0, sizeof(_install_id_buf));
}

App_GameStore::~App_GameStore() {
    if (_nes_app) { delete _nes_app; _nes_app = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────
//  IApp lifecycle
// ─────────────────────────────────────────────────────────────────────────
void App_GameStore::init() {
    _anim_time     = 0.0f;
    _spinner_angle = 0.0f;
    _list_sel      = 0;
    _list_scroll   = 0;

    _refresh_catalog_local();
    _screen = Screen::LocalList;
}

void App_GameStore::exit() {
    // NOTE: Do NOT call _nes_app->exit() here.
    // _launch_game passes 'nes' directly to switch_to_app, keeping _nes_app = nullptr
    // to prevent this exit() from deleting an object that init() hasn't called yet.
    // _nes_app is only valid AFTER the game store is re-entered via init().
    if (_nes_app) { _nes_app->exit(); delete _nes_app; _nes_app = nullptr; }
}

void App_GameStore::loop() {
    _anim_time     += 0.04f;
    _spinner_angle += 0.15f;
    if (_spinner_angle > 6.283f) _spinner_angle -= 6.283f;

    game_store_status_t st = {};
    game_store_get_status(&st);

    // Cache for render (no service calls in render path)
    _download_status = st;

    // Watch for completed downloads while on the downloading screen
    if (_screen == Screen::Downloading && !st.is_busy) {
        if (st.last_error == GAME_STORE_ERR_OK) {
            _refresh_catalog_local();
            _screen = Screen::LocalList;
        } else if (st.last_error == GAME_STORE_ERR_NO_SPACE) {
            _build_delete_candidates(_catalog[_pending_install_idx].size_bytes);
            _screen = Screen::SpaceWarn;
        } else {
            _last_err = st.last_error;
            _screen   = Screen::Error;
        }
    }

    // Watch cloud-loading completion
    if (_screen == Screen::CloudLoading && !st.is_busy) {
        if (st.last_error == GAME_STORE_ERR_OK) {
            _refresh_catalog_local();
            _screen = Screen::StoreList;
        } else {
            _last_err = st.last_error;
            _screen   = Screen::Error;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  handleInput
// ─────────────────────────────────────────────────────────────────────────
void App_GameStore::handleInput(input_event_t ev) {
    switch (_screen) {

    // ── Local List ─────────────────────────────────────────────────
    case Screen::LocalList: {
        // Count installed games
        int installed = 0;
        for (int i = 0; i < _catalog_count; i++)
            if (_catalog[i].is_installed) installed++;

        if (ev == BTN_UP && _list_sel > 0) {
            _list_sel--;
            if (_list_sel < _list_scroll) _list_scroll--;
        }
        if (ev == BTN_DOWN && _list_sel < installed - 1) {
            _list_sel++;
            if (_list_sel >= _list_scroll + 4) _list_scroll++;
        }
        if (ev == BTN_SELECT) {
            // Find the installed game at _list_sel
            int n = 0;
            for (int i = 0; i < _catalog_count; i++) {
                if (_catalog[i].is_installed) {
                    if (n == _list_sel) { _launch_game(i); return; }
                    n++;
                }
            }
        }
        if (ev == BTN_RIGHT) {
            // Switch to store
            if (!wifi_mgr_is_connected()) { _screen = Screen::NoWifi; return; }
            _list_sel = 0; _list_scroll = 0;
            _begin_cloud_fetch();
        }
        if (ev == BTN_LEFT) {
            switch_to_grid();
        }
        break;
    }

    // ── Store List ─────────────────────────────────────────────────
    case Screen::StoreList: {
        if (ev == BTN_UP && _list_sel > 0) {
            _list_sel--;
            if (_list_sel < _list_scroll) _list_scroll--;
        }
        if (ev == BTN_DOWN && _list_sel < _catalog_count - 1) {
            _list_sel++;
            if (_list_sel >= _list_scroll + 4) _list_scroll++;
        }
        if (ev == BTN_SELECT) {
            _detail_idx = _list_sel;
            _screen = Screen::GameDetail;
        }
        if (ev == BTN_LEFT) {
            _list_sel = 0; _list_scroll = 0;
            _screen = Screen::LocalList;
        }
        break;
    }

    // ── Game Detail ────────────────────────────────────────────────
    case Screen::GameDetail: {
        if (ev == BTN_LEFT)  { _screen = (_prev_screen == Screen::StoreList) ? Screen::StoreList : Screen::LocalList; }
        if (ev == BTN_SELECT) {
            const game_entry_t& g = _catalog[_detail_idx];
            if (g.is_installed) {
                // Launch
                _launch_game(_detail_idx);
            } else {
                // Install
                _begin_install(_detail_idx);
            }
        }
        if (ev == BTN_DOWN) {
            // Delete (if installed)
            const game_entry_t& g = _catalog[_detail_idx];
            if (g.is_installed) {
                game_store_delete(g.id);
                _refresh_catalog_local();
                _screen = Screen::LocalList;
            }
        }
        break;
    }

    // ── Space Warn ─────────────────────────────────────────────────
    case Screen::SpaceWarn: {
        if (ev == BTN_UP && _del_sel > 0) _del_sel--;
        if (ev == BTN_DOWN && _del_sel < _del_count - 1) _del_sel++;
        if (ev == BTN_SELECT) {
            int target = _del_candidates[_del_sel];
            if (target >= 0) {
                game_store_delete(_catalog[target].id);
                _refresh_catalog_local();
                _begin_install(_pending_install_idx);
            }
        }
        if (ev == BTN_LEFT) { _screen = Screen::StoreList; }
        break;
    }

    // ── Error / NoWifi / CloudLoading / Downloading ────────────────
    case Screen::Error:
    case Screen::NoWifi:
        if (ev == BTN_LEFT) { _screen = Screen::LocalList; }
        if (ev == BTN_SELECT && _screen == Screen::Error) {
            // Retry: go back to store or local
            _screen = Screen::LocalList;
        }
        break;

    case Screen::Downloading:
        if (ev == BTN_SELECT) {
            game_store_abort();
            _screen = Screen::LocalList;
        }
        break;

    default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  render dispatcher
// ─────────────────────────────────────────────────────────────────────────
void App_GameStore::render(LGFX_Sprite* c) {
    c->fillSprite(C_BG());
    switch (_screen) {
        case Screen::LocalList:    _render_local_list(c);    break;
        case Screen::StoreList:    _render_store_list(c);    break;
        case Screen::GameDetail:   _render_detail(c);        break;
        case Screen::Downloading:  _render_downloading(c);   break;
        case Screen::SpaceWarn:    _render_space_warn(c);    break;
        case Screen::CloudLoading: _render_cloud_loading(c); break;
        case Screen::Error:        _render_error(c);         break;
        case Screen::NoWifi:       _render_no_wifi(c);       break;
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  Render helpers
// ─────────────────────────────────────────────────────────────────────────
void App_GameStore::_draw_header(LGFX_Sprite* c, const char* title, uint16_t accent) {
    const int32_t W = c->width();
    const int32_t HEADER_H = c->height() / 9;
    c->fillRect(0, 0, W, HEADER_H, C_PANEL());
    c->drawFastHLine(0, HEADER_H, W, accent);
    c->setTextDatum(textdatum_t::middle_left);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    c->drawString(title, W / 40, HEADER_H / 2);
}

void App_GameStore::_draw_footer(LGFX_Sprite* c, const char* text) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t FOOTER_Y = H - 8;
    c->drawFastHLine(0, FOOTER_Y, W, C_DIM());
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_DIM());
    c->drawString(text, W / 2, FOOTER_Y + 4);
}

void App_GameStore::_draw_game_row(LGFX_Sprite* c, int y, const game_entry_t& e,
                                    bool selected, bool show_status)
{
    const int32_t W = c->width();
    const int32_t H = c->height();
    uint16_t bg = selected ? C_ACCENT() : C_PANEL();
    uint16_t fg = selected ? C_BG()     : C_TEXT();

    c->fillRoundRect(W / 80, y, W - W / 40, H / 6, 4, bg);

    c->setTextDatum(textdatum_t::middle_left);
    c->setFont(&fonts::Font0);
    c->setTextColor(fg);
    c->drawString(e.name, W / 20, y + H / 12);

    if (show_status) {
        if (e.is_installed && e.has_update) {
            c->setTextColor(C_WARN());
            c->drawString("[+]", W - W / 7, y + H / 12);
        } else if (e.is_installed) {
            c->setTextColor(C_SUCCESS());
            c->drawString("[v]", W - W / 7, y + H / 12);
        }
    }
}

void App_GameStore::_draw_spinner(LGFX_Sprite* c, int cx, int cy, int r) {
    for (int i = 0; i < 8; i++) {
        float a = _spinner_angle + i * (6.283f / 8);
        int x = cx + (int)(cosf(a) * r);
        int y = cy + (int)(sinf(a) * r);
        uint8_t alpha = (uint8_t)(255 * i / 8);
        uint16_t col = c->color565(alpha, alpha, alpha);
        c->fillCircle(x, y, 2, col);
    }
}

void App_GameStore::_draw_progress_bar(LGFX_Sprite* c, int x, int y, int w, int h,
                                        int pct, uint16_t col)
{
    c->drawRect(x, y, w, h, C_DIM());
    int fill = (w - 2) * pct / 100;
    if (fill > 0) c->fillRect(x + 1, y + 1, fill, h - 2, col);
}

// ─────────────────────────────────────────────────────────────────────────
//  Screen renderers
// ─────────────────────────────────────────────────────────────────────────
void App_GameStore::_render_local_list(LGFX_Sprite* c) {
    _draw_header(c, "MY GAMES", C_ACCENT());

    // Count installed
    int installed = 0;
    for (int i = 0; i < _catalog_count; i++)
        if (_catalog[i].is_installed) installed++;

    const int32_t W  = c->width();
    const int32_t H  = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = H / 9;
    const int32_t ROW_H  = H / 5;
    const int32_t ROW_Y0 = HEADER_H + 4;
    const int32_t FOOTER_Y = H - 10;

    if (installed == 0) {
        c->setTextDatum(textdatum_t::middle_center);
        c->setFont(&fonts::Font0);
        c->setTextColor(C_DIM());
        c->drawString("No games installed.", CX, H * 43 / 100);
        c->drawString("RIGHT -> Store",      CX, H * 55 / 100);
    } else {
        int row = 0, n = 0;
        for (int i = 0; i < _catalog_count && row < 4; i++) {
            if (!_catalog[i].is_installed) continue;
            if (n >= _list_scroll && row < 4) {
                _draw_game_row(c, ROW_Y0 + row * ROW_H, _catalog[i], (n == _list_sel), false);
                row++;
            }
            n++;
        }
    }

    // Free space bar
    size_t free_b  = game_store_get_free_bytes();
    size_t total_b = game_store_get_total_bytes();
    if (total_b > 0) {
        int pct = (int)(100 - (free_b * 100 / total_b));
        char buf[24];
        snprintf(buf, sizeof(buf), "%dKB free", (int)(free_b / 1024));
        c->setTextDatum(textdatum_t::middle_left);
        c->setFont(&fonts::Font0);
        c->setTextColor(C_DIM());
        c->drawString(buf, W / 40, FOOTER_Y);
        _draw_progress_bar(c, W * 44 / 100, FOOTER_Y - 4, W * 54 / 100, H / 16, pct, C_ACCENT());
    }

    _draw_footer(c, "SEL=Launch  R=Store  L=Back");
}

void App_GameStore::_render_store_list(LGFX_Sprite* c) {
    _draw_header(c, "GAME STORE", C_WARN());
    const int32_t HEADER_H = c->height() / 9;
    const int32_t ROW_H  = c->height() / 5;
    const int32_t ROW_Y0 = HEADER_H + 4;

    for (int row = 0; row < 4; row++) {
        int i = _list_scroll + row;
        if (i >= _catalog_count) break;
        _draw_game_row(c, ROW_Y0 + row * ROW_H, _catalog[i], (i == _list_sel), true);
    }

    _draw_footer(c, "SEL=Detail  L=MyGames");
}

void App_GameStore::_render_detail(LGFX_Sprite* c) {
    if (_detail_idx < 0 || _detail_idx >= _catalog_count) return;
    const game_entry_t& g = _catalog[_detail_idx];

    _draw_header(c, g.name, C_ACCENT());

    c->setTextDatum(textdatum_t::top_left);
    c->setFont(&fonts::Font0);

    const int32_t W  = c->width();
    const int32_t H  = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = H / 9;
    const int32_t META_Y   = HEADER_H + 4;
    const int32_t DESC_Y   = META_Y + H / 14;
    const int32_t BTN_Y    = H * 70 / 100;
    const int32_t BTN_H    = H / 7;
    const int32_t MARGIN   = W / 40;

    c->setTextColor(C_DIM());
    c->drawString(g.genre, MARGIN, META_Y);

    char sz[32];
    snprintf(sz, sizeof(sz), "%dKB  v%s", (int)(g.size_bytes / 1024), g.version);
    c->drawString(sz, CX, META_Y);

    c->setTextColor(C_TEXT());
    char desc[129];
    strncpy(desc, g.description, 128); desc[128] = '\0';
    for (int line = 0; line < 3; line++) {
        int start = line * 26;
        if (start >= (int)strlen(g.description)) break;
        char chunk[27]; strncpy(chunk, g.description + start, 26); chunk[26] = '\0';
        c->drawString(chunk, MARGIN, DESC_Y + line * (H / 13));
    }

    if (g.is_installed) {
        const int32_t BTN_W = (W - MARGIN * 3) / 2;
        c->fillRoundRect(MARGIN,           BTN_Y, BTN_W, BTN_H, 4, C_SUCCESS());
        c->fillRoundRect(MARGIN * 2 + BTN_W, BTN_Y, BTN_W, BTN_H, 4, C_FAIL());
        c->setTextDatum(textdatum_t::middle_center);
        c->setTextColor(C_BG());
        c->drawString("PLAY",   MARGIN + BTN_W / 2,           BTN_Y + BTN_H / 2);
        c->drawString("DELETE", MARGIN * 2 + BTN_W + BTN_W / 2, BTN_Y + BTN_H / 2);
        c->setTextColor(C_DIM());
        c->setFont(&fonts::Font0);
        c->drawString("SEL=Play  DOWN=Delete", CX, BTN_Y + BTN_H + H / 22);
    } else {
        const int32_t BTN_W = W / 2;
        c->fillRoundRect((W - BTN_W) / 2, BTN_Y, BTN_W, BTN_H, 4, C_ACCENT());
        c->setTextDatum(textdatum_t::middle_center);
        c->setTextColor(C_BG());
        c->drawString(g.has_update ? "UPDATE" : "INSTALL", CX, BTN_Y + BTN_H / 2);
        c->setTextColor(C_DIM());
        c->setFont(&fonts::Font0);
        c->drawString("SEL=Install  L=Back", CX, BTN_Y + BTN_H + H / 22);
    }

    _draw_footer(c, "L=Back");
}

void App_GameStore::_render_downloading(LGFX_Sprite* c) {
    // Read cached status — no service calls in render path (cached in loop())
    const game_store_status_t& st = _download_status;

    _draw_header(c, "DOWNLOADING", C_ACCENT());

    const int32_t W  = c->width();
    const int32_t H  = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = H / 9;
    const int32_t MARGIN   = W / 20;
    const int32_t NAME_Y   = HEADER_H + H / 6;
    const int32_t BAR_Y    = H * 36 / 100;
    const int32_t BAR_H    = H / 6;
    const int32_t PCT_Y    = BAR_Y + BAR_H + H / 14;
    const int32_t KB_Y     = PCT_Y + H / 10;

    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    if (_pending_install_idx >= 0)
        c->drawString(_catalog[_pending_install_idx].name, CX, NAME_Y);

    _draw_progress_bar(c, MARGIN, BAR_Y, W - MARGIN * 2, BAR_H, st.progress_pct, C_ACCENT());

    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", st.progress_pct);
    c->setTextColor(C_TEXT());
    c->drawString(pct_str, CX, PCT_Y);

    char kb[32];
    snprintf(kb, sizeof(kb), "%dKB / %dKB",
             st.bytes_done / 1024, st.bytes_total / 1024);
    c->setTextColor(C_DIM());
    c->drawString(kb, CX, KB_Y);

    _draw_footer(c, "SEL=Cancel");
}

void App_GameStore::_render_space_warn(LGFX_Sprite* c) {
    _draw_header(c, "NOT ENOUGH SPACE", C_WARN());

    const int32_t W  = c->width();
    const int32_t H  = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = H / 9;
    const int32_t MSG_Y    = HEADER_H + H / 14;
    const int32_t ROW_Y0   = MSG_Y + H / 9;
    const int32_t ROW_H    = H / 6;

    c->setTextDatum(textdatum_t::top_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    c->drawString("Delete a game to free space:", CX, MSG_Y);

    for (int row = 0; row < _del_count && row < 4; row++) {
        int ci = _del_candidates[row];
        if (ci < 0) continue;
        _draw_game_row(c, ROW_Y0 + row * ROW_H, _catalog[ci], (row == _del_sel), false);
    }

    _draw_footer(c, "SEL=Delete  L=Back");
}

void App_GameStore::_render_cloud_loading(LGFX_Sprite* c) {
    _draw_header(c, "CONNECTING", C_ACCENT());
    const int32_t CX = c->width() / 2;
    const int32_t H  = c->height();
    _draw_spinner(c, CX, H / 2, H / 7);
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_DIM());
    c->drawString("Fetching catalog...", CX, H * 74 / 100);
}

void App_GameStore::_render_error(LGFX_Sprite* c) {
    _draw_header(c, "ERROR", C_FAIL());
    const int32_t CX = c->width() / 2;
    const int32_t H  = c->height();
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_FAIL());
    c->drawString(_err_string(_last_err), CX, H * 39 / 100);
    c->setTextColor(C_DIM());
    c->drawString("SEL=Retry  L=Back",    CX, H * 63 / 100);
}

void App_GameStore::_render_no_wifi(LGFX_Sprite* c) {
    _draw_header(c, "NO WI-FI", C_WARN());
    const int32_t CX = c->width() / 2;
    const int32_t H  = c->height();
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    c->drawString("Connect to Wi-Fi first.", CX, H * 39 / 100);
    c->setTextColor(C_DIM());
    c->drawString("Use Settings -> Wi-Fi",  CX, H * 51 / 100);
    c->drawString("L=Back",                 CX, H * 70 / 100);
}

// ─────────────────────────────────────────────────────────────────────────
//  Business logic
// ─────────────────────────────────────────────────────────────────────────
void App_GameStore::_refresh_catalog_local() {
    _catalog_count = game_store_get_catalog(_catalog, GAME_STORE_MAX_GAMES);
    if (_list_sel >= _catalog_count && _catalog_count > 0)
        _list_sel = _catalog_count - 1;
}

void App_GameStore::_begin_cloud_fetch() {
    _screen = Screen::CloudLoading;

    // Kick off refresh in a FreeRTOS task so we don't block the UI task
    xTaskCreate([](void* arg) {
        game_store_refresh_catalog();
        vTaskDelete(NULL);
    }, "gs_fetch_ui", 6144, nullptr, 4, nullptr);
}

void App_GameStore::_begin_install(int catalog_idx) {
    if (catalog_idx < 0 || catalog_idx >= _catalog_count) return;
    _pending_install_idx = catalog_idx;

    size_t free_b = game_store_get_free_bytes();
    uint32_t need  = _catalog[catalog_idx].size_bytes;
    if (free_b < (size_t)(need + need / 10)) {
        _build_delete_candidates(need);
        _screen = Screen::SpaceWarn;
        return;
    }

    _screen = Screen::Downloading;
    /* Copy the game ID into a stable buffer so the async task has a valid
     * pointer even if the app is destroyed while the download is in flight. */
    strncpy(_install_id_buf, _catalog[catalog_idx].id, sizeof(_install_id_buf) - 1);
    _install_id_buf[sizeof(_install_id_buf) - 1] = '\0';

    // Copy ID to static storage for the task (avoids dangling pointer into app member)
    static char s_task_id_buf[32];
    strlcpy(s_task_id_buf, _install_id_buf, sizeof(s_task_id_buf));

    xTaskCreate([](void* arg) {
        game_store_install((const char*)arg);
        vTaskDelete(NULL);
    }, "gs_install", 8192, (void*)s_task_id_buf, 4, nullptr);
}

void App_GameStore::_build_delete_candidates(uint32_t needed_bytes) {
    _del_count = 0;
    _del_sel   = 0;
    (void)needed_bytes;
    for (int i = 0; i < _catalog_count; i++) {
        if (_catalog[i].is_installed && i != _pending_install_idx) {
            _del_candidates[_del_count++] = i;
        }
    }
}

void App_GameStore::_launch_game(int catalog_idx) {
    if (catalog_idx < 0 || catalog_idx >= _catalog_count) return;
    const game_entry_t& g = _catalog[catalog_idx];
    if (!g.is_installed) return;

    char path[256];
    if (!game_store_get_rom_path(g.id, path, sizeof(path))) {
        _last_err = GAME_STORE_ERR_FS_ERROR;
        _screen   = Screen::Error;
        return;
    }

    if (_nes_app) { _nes_app->exit(); delete _nes_app; _nes_app = nullptr; }

    // CRITICAL FIX: Use local pointer 'nes', NOT _nes_app.
    // switch_to_app() calls App_GameStore::exit() BEFORE App_NES::init().
    // If _nes_app pointed to the new object, exit() would delete it before init() runs
    // causing an InstructionFetchError (use-after-free through vtable).
    App_NES* nes = new App_NES();
    nes->setRomPath(path);
    nes->onEmulatorExit = []() {
        switch_to_grid();
    };

    // _nes_app stays nullptr — exit() cannot delete 'nes'.
    switch_to_app(nes);
    // 'nes' is now current_app and running its emulator task.
}

void App_GameStore::onNesExit() {
    if (_nes_app) { _nes_app->exit(); delete _nes_app; _nes_app = nullptr; }
    _screen = Screen::LocalList;
    switch_to_app(this);   // Re-enter the game store
}

const char* App_GameStore::_err_string(game_store_err_t e) {
    switch (e) {
        case GAME_STORE_ERR_NO_WIFI:        return "No Wi-Fi connection";
        case GAME_STORE_ERR_NETWORK:        return "Network error";
        case GAME_STORE_ERR_BAD_METADATA:   return "Invalid catalog data";
        case GAME_STORE_ERR_NO_SPACE:       return "No storage space";
        case GAME_STORE_ERR_ALREADY_INSTALLED: return "Already installed";
        case GAME_STORE_ERR_CORRUPT_ROM:    return "ROM checksum failed";
        case GAME_STORE_ERR_FS_ERROR:       return "Filesystem error";
        case GAME_STORE_ERR_ABORTED:        return "Cancelled";
        default:                            return "Unknown error";
    }
}
