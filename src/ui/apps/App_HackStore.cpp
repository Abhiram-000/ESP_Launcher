#include "App_HackStore.h"
#include "../../ui/theme_mgr.h"
#include "../../services/wifi_mgr.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const char* TAG = "APP_HACK";

extern void switch_to_app(IApp* app);
extern void switch_to_grid();

static inline uint16_t C_ACCENT()  { return theme_get_accent(); }
static inline uint16_t C_TEXT()    { return theme_get_text(); }
static inline uint16_t C_DIM()     { return theme_get_text_dim(); }
static inline uint16_t C_BG()      { return theme_get_bg(); }
static inline uint16_t C_PANEL()   { return theme_get_bg(); }

static uint16_t COLOR_GREEN;
static uint16_t COLOR_ORANGE;
static uint16_t COLOR_RED;

App_HackStore::App_HackStore()
    : _screen(Screen::LocalList), _prev_screen(Screen::LocalList),
      _catalog_count(0), _list_sel(0), _list_scroll(0),
      _detail_idx(0), _last_err(HACK_STORE_ERR_OK),
      _anim_time(0.0f), _spinner_angle(0.0f),
      _pending_install_idx(-1),
      _fetch_epoch_launched(0), _fetch_epoch_confirmed(0),
      _install_epoch_launched(0), _install_epoch_confirmed(0)
{
    memset(_catalog,  0, sizeof(_catalog));
    memset(_installed_id, 0, sizeof(_installed_id));
    memset(_install_id_buf, 0, sizeof(_install_id_buf));
}

App_HackStore::~App_HackStore() {}

void App_HackStore::init() {
    COLOR_GREEN  = theme_get_success();
    COLOR_ORANGE = theme_get_accent();
    COLOR_RED    = theme_get_fail();

    _anim_time     = 0.0f;
    _spinner_angle = 0.0f;
    _list_sel      = 0;
    _list_scroll   = 0;

    _refresh_catalog_local();
    
    // Automatically jump to store if no local catalog, else show installed (if any)
    _screen = Screen::LocalList;
}

void App_HackStore::exit() {}

void App_HackStore::loop() {
    _anim_time     += 0.04f;
    _spinner_angle += 0.15f;
    if (_spinner_angle > 6.283f) _spinner_angle -= 6.283f;

    _wifi_scanner.loop();

    hack_store_status_t st = {};
    hack_store_get_status(&st);

    if (_screen == Screen::Downloading) {
        // Since is_busy is set to true synchronously before we transition to Downloading,
        // we can simply wait for it to become false to know the task is done.
        if (!st.is_busy) {
            if (st.last_error == HACK_STORE_ERR_OK) {
                ESP_LOGI(TAG, "Install finished successfully, rebooting.");
                _refresh_catalog_local();
                _screen = Screen::LocalList;
            } else {
                _last_err = st.last_error;
                ESP_LOGE(TAG, "Install failed with error code: %d (%s)", (int)_last_err, _err_string(_last_err));
                _screen   = Screen::Error;
            }
        }
    }

    if (_screen == Screen::CloudLoading) {
        if (!st.is_busy) {
            if (st.last_error == HACK_STORE_ERR_OK) {
                ESP_LOGI(TAG, "Cloud fetch finished successfully. Catalog updated.");
                _refresh_catalog_local();
                _screen = Screen::StoreList;
            } else {
                _last_err = st.last_error;
                ESP_LOGE(TAG, "Cloud fetch failed with error code: %d (%s)", (int)_last_err, _err_string(_last_err));
                _screen   = Screen::Error;
            }
        }
    }
}

void App_HackStore::handleInput(input_event_t ev) {
    if (_wifi_scanner.isActive()) {
        _wifi_scanner.handleInput(ev);
        return;
    }

    switch (_screen) {
    case Screen::LocalList: {
        bool has_installed = hack_store_get_installed_id(_installed_id, sizeof(_installed_id));
        
        if (ev == BTN_RIGHT) {
            if (!wifi_mgr_is_connected()) { _screen = Screen::NoWifi; return; }
            _list_sel = 0; _list_scroll = 0;
            _begin_cloud_fetch();
        }
        if (ev == BTN_LEFT) {
            switch_to_grid();
        }
        if (ev == BTN_SELECT && has_installed) {
            hack_store_install(_installed_id); // Boot it directly
        }
        break;
    }

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

    case Screen::GameDetail: {
        if (ev == BTN_LEFT)  { _screen = Screen::StoreList; }
        if (ev == BTN_SELECT) {
            _begin_install(_detail_idx);
        }
        break;
    }

    case Screen::Error:
        if (ev == BTN_LEFT) { _screen = Screen::LocalList; }
        if (ev == BTN_SELECT) { _screen = Screen::LocalList; }
        break;

    case Screen::NoWifi:
        if (ev == BTN_LEFT) { _screen = Screen::LocalList; }
        if (ev == BTN_RIGHT || ev == BTN_SELECT) {
            _wifi_scanner.begin([this](bool success) {
                if (success) {
                    this->_screen = Screen::LocalList;
                }
            });
        }
        break;

    case Screen::Downloading:
        if (ev == BTN_SELECT) {
            hack_store_abort();
            _screen = Screen::LocalList;
        }
        break;

    default: break;
    }
}

void App_HackStore::render(LGFX_Sprite* c) {
    c->fillSprite(C_BG());
    switch (_screen) {
        case Screen::LocalList:    _render_local_list(c);    break;
        case Screen::StoreList:    _render_store_list(c);    break;
        case Screen::GameDetail:   _render_detail(c);        break;
        case Screen::Downloading:  _render_downloading(c);   break;
        case Screen::CloudLoading: _render_cloud_loading(c); break;
        case Screen::Error:        _render_error(c);         break;
        case Screen::NoWifi:       _render_no_wifi(c);       break;
    }
    
    if (_wifi_scanner.isActive()) {
        _wifi_scanner.render(c);
    }
}

void App_HackStore::_draw_header(LGFX_Sprite* c, const char* title, uint16_t accent) {
    const int32_t W = c->width();
    const int32_t HEADER_H = c->height() / 9;
    c->fillRect(0, 0, W, HEADER_H, C_PANEL());
    c->drawFastHLine(0, HEADER_H, W, accent);
    c->setTextDatum(textdatum_t::middle_left);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    c->drawString(title, W / 40, HEADER_H / 2);
}

void App_HackStore::_draw_footer(LGFX_Sprite* c, const char* text) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t FOOTER_Y = H - 8;
    c->drawFastHLine(0, FOOTER_Y, W, C_DIM());
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_DIM());
    c->drawString(text, W / 2, FOOTER_Y + 4);
}

void App_HackStore::_draw_game_row(LGFX_Sprite* c, int y, const hack_entry_t& e, bool selected) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    uint16_t bg = selected ? C_ACCENT() : C_PANEL();
    uint16_t fg = selected ? C_BG()     : C_TEXT();

    c->fillRoundRect(W / 80, y, W - W / 40, H / 6, 4, bg);

    c->setTextDatum(textdatum_t::middle_left);
    c->setFont(&fonts::Font0);
    c->setTextColor(fg);
    c->drawString(e.name, W / 20, y + H / 12);
    
    if (strcmp(_installed_id, e.id) == 0) {
        c->setTextColor(COLOR_GREEN);
        c->drawString("[v]", W - W / 7, y + H / 12);
    }
}

void App_HackStore::_draw_spinner(LGFX_Sprite* c, int cx, int cy, int r) {
    for (int i = 0; i < 8; i++) {
        float a = _spinner_angle + i * (6.283f / 8);
        int x = cx + (int)(cosf(a) * r);
        int y = cy + (int)(sinf(a) * r);
        uint8_t alpha = (uint8_t)(255 * i / 8);
        uint16_t col = c->color565(alpha, alpha, alpha);
        c->fillCircle(x, y, 2, col);
    }
}

void App_HackStore::_draw_progress_bar(LGFX_Sprite* c, int x, int y, int w, int h, int pct, uint16_t col) {
    c->drawRect(x, y, w, h, C_DIM());
    int fill = (w - 2) * pct / 100;
    if (fill > 0) c->fillRect(x + 1, y + 1, fill, h - 2, col);
}

void App_HackStore::_render_local_list(LGFX_Sprite* c) {
    _draw_header(c, "MY HACKS", C_ACCENT());

    bool has_installed = hack_store_get_installed_id(_installed_id, sizeof(_installed_id));

    if (!has_installed) {
        c->setTextDatum(textdatum_t::middle_center);
        c->setFont(&fonts::Font0);
        c->setTextColor(C_DIM());
        c->drawString("No hacks installed.", 80, 55);
        c->drawString("RIGHT -> Store", 80, 70);
        _draw_footer(c, "R=Store  L=Back");
    } else {
        c->setTextDatum(textdatum_t::middle_center);
        c->setFont(&fonts::Font0);
        c->setTextColor(C_TEXT());
        c->drawString("Currently Installed:", 80, 30);
        
        c->setTextColor(COLOR_GREEN);
        c->drawString(_installed_id, 80, 50);

        c->setTextColor(C_DIM());
        c->drawString("Press SEL to Boot", 80, 80);
        
        _draw_footer(c, "SEL=Boot  R=Store  L=Back");
    }
}

void App_HackStore::_render_store_list(LGFX_Sprite* c) {
    _draw_header(c, "HACK STORE", COLOR_ORANGE);

    for (int row = 0; row < 4; row++) {
        int i = _list_scroll + row;
        if (i >= _catalog_count) break;
        _draw_game_row(c, 18 + row * 25, _catalog[i], (i == _list_sel));
    }

    _draw_footer(c, "SEL=Detail  L=MyHacks");
}

void App_HackStore::_render_detail(LGFX_Sprite* c) {
    if (_detail_idx < 0 || _detail_idx >= _catalog_count) return;
    const hack_entry_t& g = _catalog[_detail_idx];

    _draw_header(c, g.name, C_ACCENT());

    c->setTextDatum(textdatum_t::top_left);
    c->setFont(&fonts::Font0);

    c->setTextColor(C_DIM());
    c->drawString(g.version, 4, 18);

    char sz[32];
    snprintf(sz, sizeof(sz), "%dKB", (int)(g.size_bytes / 1024));
    c->drawString(sz, 80, 18);

    c->setTextColor(C_TEXT());
    char desc[129];
    strncpy(desc, g.description, 128); desc[128] = '\0';
    for (int line = 0; line < 4; line++) {
        int start = line * 26;
        if (start >= (int)strlen(g.description)) break;
        char chunk[27]; strncpy(chunk, g.description + start, 26); chunk[26] = '\0';
        c->drawString(chunk, 4, 30 + line * 10);
    }

    bool is_installed = (strcmp(_installed_id, g.id) == 0);

    c->fillRoundRect(44, 90, 72, 18, 4, C_ACCENT());
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(C_BG());
    c->drawString(is_installed ? "BOOT" : "FLASH", 80, 99);
    c->setTextColor(C_DIM());
    c->setFont(&fonts::Font0);
    c->drawString(is_installed ? "SEL=Boot  L=Back" : "SEL=Flash  L=Back", 80, 114);

    _draw_footer(c, "L=Back");
}

void App_HackStore::_render_downloading(LGFX_Sprite* c) {
    hack_store_status_t st = {};
    hack_store_get_status(&st);

    _draw_header(c, "FLASHING", COLOR_RED);

    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    if (_pending_install_idx >= 0)
        c->drawString(_catalog[_pending_install_idx].name, 80, 34);

    _draw_progress_bar(c, 8, 46, 144, 20, st.progress_pct, COLOR_RED);

    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", st.progress_pct);
    c->setTextColor(C_TEXT());
    c->drawString(pct_str, 80, 76);

    char kb[32];
    snprintf(kb, sizeof(kb), "%dKB / %dKB",
             (int)(st.bytes_done / 1024), (int)(st.bytes_total / 1024));
    c->setTextColor(C_DIM());
    c->drawString(kb, 80, 90);

    c->setTextColor(COLOR_ORANGE);
    c->drawString("DO NOT POWER OFF", 80, 105);

    _draw_footer(c, "SEL=Abort");
}

void App_HackStore::_render_cloud_loading(LGFX_Sprite* c) {
    _draw_header(c, "CONNECTING", C_ACCENT());
    _draw_spinner(c, 80, 64, 18);
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_DIM());
    c->drawString("Fetching catalog...", 80, 95);
}

void App_HackStore::_render_error(LGFX_Sprite* c) {
    _draw_header(c, "ERROR", COLOR_RED);
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(COLOR_RED);
    c->drawString(_err_string(_last_err), 80, 50);
    c->setTextColor(C_DIM());
    c->drawString("SEL=Retry  L=Back", 80, 80);
}

void App_HackStore::_render_no_wifi(LGFX_Sprite* c) {
    _draw_header(c, "NO WI-FI", COLOR_ORANGE);
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font0);
    c->setTextColor(C_TEXT());
    c->drawString("Connect to Wi-Fi first.", 80, 50);
    c->setTextColor(C_DIM());
    c->drawString("SEL/R=Scan  L=Back", 80, 75);
}

void App_HackStore::_refresh_catalog_local() {
    _catalog_count = hack_store_get_catalog(_catalog, HACK_STORE_MAX_FIRMS);
    if (_list_sel >= _catalog_count && _catalog_count > 0)
        _list_sel = _catalog_count - 1;
    hack_store_get_installed_id(_installed_id, sizeof(_installed_id));
}

void App_HackStore::_begin_cloud_fetch() {
    ESP_LOGI(TAG, "Initiating cloud fetch...");
    _screen = Screen::CloudLoading;
    _fetch_epoch_launched++;  // New operation — epoch advances; confirmed lags behind

    // Direct call. This is non-blocking now.
    hack_store_refresh_catalog();
}

void App_HackStore::_begin_install(int catalog_idx) {
    if (catalog_idx < 0 || catalog_idx >= _catalog_count) return;
    _pending_install_idx = catalog_idx;

    const char* gid = _catalog[catalog_idx].id;
    if (strcmp(_installed_id, gid) == 0) {
        // Boot immediately
        hack_store_install(gid);
        return;
    }

    ESP_LOGI(TAG, "Initiating install for ID: %s", gid);
    _screen = Screen::Downloading;
    _install_epoch_launched++;  // New operation — epoch advances

    /*
     * FIX: Copy the ID into _install_id_buf (member) so the pointer remains
     * valid for the entire lifetime of the OTA task, even if _catalog is
     * refreshed or the user navigates away.
     */
    strncpy(_install_id_buf, gid, sizeof(_install_id_buf) - 1);
    _install_id_buf[sizeof(_install_id_buf) - 1] = '\0';

    // Direct call. This is non-blocking now.
    hack_store_install(_install_id_buf);
}

const char* App_HackStore::_err_string(hack_store_err_t e) {
    switch (e) {
        case HACK_STORE_ERR_NO_WIFI:        return "No Wi-Fi";
        case HACK_STORE_ERR_NETWORK:        return "Network Error";
        case HACK_STORE_ERR_BAD_METADATA:   return "Bad Catalog";
        case HACK_STORE_ERR_PARTITION:      return "No ota_1 partition";
        case HACK_STORE_ERR_FLASHING:       return "Flash failed";
        case HACK_STORE_ERR_ABORTED:        return "Aborted";
        default:                            return "Unknown error";
    }
}
