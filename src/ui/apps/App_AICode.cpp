#include "App_AICode.h"
#include "../theme_mgr.h"
#include "../../services/lua_engine.h"
#include "../../services/llm_client.h"
#include "../../services/wifi_mgr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/stat.h"
#include "sys/dirent.h"
#include <string.h>
#include <math.h>

extern void switch_to_grid();

const char* App_AICode::MENU_LABELS[App_AICode::MENU_ITEMS] = {
    "Chat with AI", "Lua IDE", "Scripts", "Settings"
};

// ─── LLM streaming callback (called from background task) ─────────────────
bool App_AICode::_on_llm_chunk(const char* token, size_t len, void* user) {
    App_AICode* self = (App_AICode*)user;
    self->_llm_reply_acc.append(token, len);
    // Append to display buffer (trim if too long)
    self->_chat_display.append(token, len);
    if (self->_chat_display.size() > AIAPP_MAX_RESPONSE_LEN)
        self->_chat_display.erase(0, self->_chat_display.size() - AIAPP_MAX_RESPONSE_LEN);
    return true;
}

// ─── Constructor ─────────────────────────────────────────────────────────
App_AICode::App_AICode()
    : _screen(Screen::WIFI_CHECK), _anim(0),
      _net_sel(0), _conn_start_ms(0),
      _menu_sel(0),
      _llm_busy(false), _llm_done(false),
      _ide_cursor(0), _ide_scroll_line(0), _ide_dirty(false),
      _script_sel(0),
      _kb_row(1), _kb_col(0), _kb_caps(false), _kb_sym(false)
{}

App_AICode::~App_AICode() {
    LuaEngine::instance().shutdown();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────
void App_AICode::init() {
    _screen        = Screen::WIFI_CHECK;
    _anim          = 0;
    _menu_sel      = 0;
    _llm_busy      = false;
    _llm_done      = false;
    _chat_display  = "";
    _chat_input    = "";
    _ide_buf       = "";
    _ide_filename  = "untitled.lua";
    _ide_cursor    = 0;
    _ide_scroll_line = 0;
    _ide_dirty     = false;
    _kb_row = 1; _kb_col = 0; _kb_caps = false; _kb_sym = false;

    _ensure_ai_dirs();
    llm_client_init();
    LuaEngine::instance().init();

    // If already connected, skip Wi-Fi flow
    if (wifi_mgr_is_connected()) {
        char key[128] = {};
        if (llm_client_get_api_key(key, sizeof(key)) && key[0])
            _screen = Screen::MAIN_MENU;
        else
            _screen = Screen::API_KEY_ENTRY;
    }
}

void App_AICode::exit() {
    _llm_busy = false;
}

// ─── Loop ────────────────────────────────────────────────────────────────
void App_AICode::loop() {
    _anim += 0.05f;

    if (_screen == Screen::WIFI_SCAN) {
        wifi_scan_info_t tmp[MAX_WIFI_NETWORKS];
        int n = wifi_mgr_get_scan_results(tmp, MAX_WIFI_NETWORKS);
        if (n > 0) {
            _networks.clear();
            for (int i = 0; i < n; i++) _networks.push_back(tmp[i]);
            _net_sel = 0;
            _screen  = Screen::WIFI_PICK;
        }
    }

    if (_screen == Screen::WIFI_CONNECTING) {
        if (wifi_mgr_is_connected()) {
            char key[128] = {};
            llm_client_get_api_key(key, sizeof(key));
            _screen = key[0] ? Screen::MAIN_MENU : Screen::API_KEY_ENTRY;
        } else if ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - _conn_start_ms > APP_WIFI_CONN_TIMEOUT_MS) {
            _screen = Screen::WIFI_CHECK;
        }
    }
}

// ─── Render dispatcher ───────────────────────────────────────────────────
void App_AICode::render(LGFX_Sprite* c) {
    c->fillSprite(theme_get_bg());
    switch (_screen) {
        case Screen::WIFI_CHECK:      draw_wifi_check(c);    break;
        case Screen::WIFI_SCAN:       draw_wifi_scan(c);     break;
        case Screen::WIFI_PICK:       draw_wifi_pick(c);     break;
        case Screen::WIFI_PASSWORD:   draw_wifi_password(c); break;
        case Screen::WIFI_CONNECTING: draw_wifi_connecting(c); break;
        case Screen::API_KEY_ENTRY:   draw_api_key_entry(c); break;
        case Screen::MAIN_MENU:       draw_main_menu(c);     break;
        case Screen::CHAT:            draw_chat(c);          break;
        case Screen::CHAT_INPUT:      draw_chat(c); draw_keyboard(c, _chat_input, false); break;
        case Screen::IDE_EDITOR:      draw_ide(c);           break;
        case Screen::IDE_KEYBOARD:    draw_ide(c); draw_keyboard(c, _ide_buf, true); break;
        case Screen::SCRIPT_LIST:     draw_script_list(c);   break;
        case Screen::LUA_RUNNING:     draw_lua_running(c);   break;
        case Screen::LUA_RESULT:      draw_lua_result(c);    break;
        case Screen::SETTINGS:        draw_settings(c);      break;
    }
}

// ─── Input dispatcher ────────────────────────────────────────────────────
void App_AICode::handleInput(input_event_t ev) {
    switch (_screen) {
        case Screen::WIFI_CHECK:
            if (ev == BTN_LEFT) { switch_to_grid(); return; }
            if (ev == BTN_SELECT) {
                if (wifi_mgr_is_connected()) {
                    char key[128] = {};
                    llm_client_get_api_key(key, sizeof(key));
                    _screen = key[0] ? Screen::MAIN_MENU : Screen::API_KEY_ENTRY;
                } else { wifi_mgr_scan_start(); _screen = Screen::WIFI_SCAN; }
            }
            break;

        case Screen::WIFI_PICK:       handle_wifi_pick_input(ev);   break;
        case Screen::WIFI_PASSWORD:   handle_keyboard_input(ev, _wifi_pass, false); break;
        case Screen::API_KEY_ENTRY:   handle_keyboard_input(ev, _api_key_buf, false); break;
        case Screen::CHAT_INPUT:      handle_chat_input(ev);         break;
        case Screen::IDE_KEYBOARD:    handle_ide_input(ev);          break;
        case Screen::SCRIPT_LIST:     handle_script_list_input(ev);  break;

        case Screen::MAIN_MENU:
            if (ev == BTN_LEFT) { switch_to_grid(); return; }
            if (ev == BTN_UP   && _menu_sel > 0) _menu_sel--;
            if (ev == BTN_DOWN && _menu_sel < MENU_ITEMS - 1) _menu_sel++;
            if (ev == BTN_SELECT) {
                if (_menu_sel == 0) { _chat_display = ""; _screen = Screen::CHAT; }
                if (_menu_sel == 1) { _screen = Screen::IDE_EDITOR; }
                if (_menu_sel == 2) { _refresh_scripts(); _screen = Screen::SCRIPT_LIST; }
                if (_menu_sel == 3) { _screen = Screen::SETTINGS; }
            }
            break;

        case Screen::CHAT:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            if (ev == BTN_SELECT && !_llm_busy) { _chat_input = ""; _screen = Screen::CHAT_INPUT; }
            break;

        case Screen::IDE_EDITOR:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            if (ev == BTN_SELECT) { _screen = Screen::IDE_KEYBOARD; }
            if (ev == BTN_RIGHT && _ide_dirty) { _save_ide_file(); }
            if (ev == BTN_DOWN) { _run_lua_script(AI_SCRIPTS_DIR "/" + _ide_filename); }
            break;

        case Screen::LUA_RESULT:
            if (ev == BTN_SELECT || ev == BTN_LEFT) { _screen = Screen::SCRIPT_LIST; }
            break;

        case Screen::SETTINGS:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            if (ev == BTN_SELECT) { _api_key_buf = ""; _screen = Screen::API_KEY_ENTRY; }
            break;

        default: break;
    }
}
