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

static const char* TAG = "App_AICode";

#include "../ui_task.h"

const char* App_AICode::MENU_LABELS[App_AICode::MENU_ITEMS] = {
    "Chat with AI", "Lua IDE", "Scripts", "Settings"
};

// ─── LLM streaming callback (called from background task) ─────────────────
bool App_AICode::_on_llm_chunk(const char* token, size_t len, void* user) {
    App_AICode* self = (App_AICode*)user;
    if (self->_state_mutex) {
        if (xSemaphoreTake(self->_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "LLM chunk: mutex timeout, skipping update");
            return true;
        }
    }
    self->_llm_reply_acc.append(token, len);
    // Append to display buffer (trim if too long)
    self->_chat_display.append(token, len);
    if (self->_chat_display.size() > AIAPP_MAX_RESPONSE_LEN)
        self->_chat_display.erase(0, self->_chat_display.size() - AIAPP_MAX_RESPONSE_LEN);
    if (self->_state_mutex) {
        xSemaphoreGive(self->_state_mutex);
    }
    return true;
}

// ─── Constructor ─────────────────────────────────────────────────────────
App_AICode::App_AICode()
    : _screen(Screen::WIFI_CHECK), _anim(0),
      _menu_sel(0),
      _chat_select_sel(0),
      _llm_busy(false), _llm_done(false),
      _state_mutex(nullptr),
      _cached_llm_busy(false),
      _cached_lua_result({}),
      _ide_cursor(0), _ide_scroll_line(0), _ide_dirty(false),
      _script_sel(0),
      _lua_done(false)
{}

App_AICode::~App_AICode() {
    LuaEngine::instance().shutdown();
    if (_state_mutex) {
        vSemaphoreDelete(_state_mutex);
        _state_mutex = nullptr;
    }
}

// ─── Lifecycle ────────────────────────────────────────────────────────────
void App_AICode::init() {
    _screen        = Screen::WIFI_CHECK;
    _anim          = 0;
    _menu_sel      = 0;
    _chat_select_sel = 0;
    _llm_busy      = false;
    _llm_done      = false;
    _chat_display  = "";
    _chat_input    = "";
    _ide_buf       = "";
    _ide_filename  = "untitled.lua";
    _ide_cursor    = 0;
    _ide_scroll_line = 0;
    _ide_dirty     = false;
    _lua_done      = false;

    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    configASSERT(_state_mutex != nullptr);

    _ensure_ai_dirs();
    llm_client_init();
    LuaEngine::instance().init();

    if (wifi_mgr_is_connected()) {
        char key[128] = {};
        if (llm_client_get_api_key(key, sizeof(key)) && key[0])
            _screen = Screen::MAIN_MENU;
        else
            _screen = Screen::API_KEY_ENTRY;
    }
}

void App_AICode::exit() {
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _llm_busy = false;
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "exit: mutex timeout");
            _llm_busy = false;
        }
    } else {
        _llm_busy = false;
    }
}

void App_AICode::_mark_llm_finished() {
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _llm_busy = false;
            _llm_done = true;
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "_mark_llm_finished: mutex timeout");
            _llm_busy = false;
            _llm_done = true;
        }
    } else {
        _llm_busy = false;
        _llm_done = true;
    }
}

void App_AICode::_publish_lua_result(const lua_engine_result_t& result) {
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _lua_result = result;
            _lua_done = true;
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "_publish_lua_result: mutex timeout");
            _lua_result = result;
            _lua_done = true;
        }
    } else {
        _lua_result = result;
        _lua_done = true;
    }
}

// ─── Loop ────────────────────────────────────────────────────────────────
void App_AICode::loop() {
    _anim += 0.05f;
    _wifi_scanner.loop();

    // Cache shared state under mutex for render helpers (no mutex in render path)
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _cached_chat_display = _chat_display;
            _cached_llm_busy = _llm_busy;
            _cached_lua_result = _lua_result;
            if (_lua_done) {
                _lua_done = false;
                _screen = Screen::LUA_RESULT;
            }
            _llm_done = false;
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "loop: mutex timeout");
        }
    }

    // After scanner completes connection, proceed to API key or main menu
    if (_screen == Screen::WIFI_CHECK && !_wifi_scanner.isActive()) {
        if (wifi_mgr_is_connected()) {
            char key[128] = {};
            llm_client_get_api_key(key, sizeof(key));
            _screen = key[0] ? Screen::MAIN_MENU : Screen::API_KEY_ENTRY;
        }
    }
}

// ─── Render dispatcher ───────────────────────────────────────────────────
void App_AICode::render(LGFX_Sprite* c) {
    c->fillSprite(theme_get_bg());
    switch (_screen) {
        case Screen::WIFI_CHECK:      draw_wifi_check(c);    break;
        case Screen::API_KEY_ENTRY:   draw_api_key_entry(c); break;
        case Screen::MAIN_MENU:       draw_main_menu(c);     break;
        case Screen::CHAT_SELECT:     draw_chat_select(c);   break;
        case Screen::CHAT_PHONE:      draw_chat_phone(c);    break;
        case Screen::CHAT:
        case Screen::CHAT_INPUT:      draw_chat(c);          break;
        case Screen::IDE_EDITOR:
        case Screen::IDE_KEYBOARD:    draw_ide(c);           break;
        case Screen::SCRIPT_LIST:     draw_script_list(c);   break;
        case Screen::LUA_RUNNING:     draw_lua_running(c);   break;
        case Screen::LUA_RESULT:      draw_lua_result(c);    break;
        case Screen::SETTINGS:        draw_settings(c);      break;
    }
    // Overlay components drawn last so they appear on top
    if (_wifi_scanner.isActive()) _wifi_scanner.render(c);
    if (_keyboard.isActive())     _keyboard.render(c);
}

// ─── Input dispatcher ────────────────────────────────────────────────────
void App_AICode::handleInput(input_event_t ev) {
    // Components intercept all input when active
    if (_wifi_scanner.isActive()) { _wifi_scanner.handleInput(ev); return; }
    if (_keyboard.isActive())     { _keyboard.handleInput(ev);     return; }

    switch (_screen) {
        case Screen::WIFI_CHECK:
            if (ev == BTN_LEFT) { switch_to_grid(); return; }
            if (ev == BTN_SELECT) {
                if (wifi_mgr_is_connected()) {
                    char key[128] = {};
                    llm_client_get_api_key(key, sizeof(key));
                    _screen = key[0] ? Screen::MAIN_MENU : Screen::API_KEY_ENTRY;
                } else {
                    _wifi_scanner.begin([this](bool success) {
                        if (success) {
                            char key[128] = {};
                            llm_client_get_api_key(key, sizeof(key));
                            this->_screen = key[0] ? Screen::MAIN_MENU : Screen::API_KEY_ENTRY;
                        }
                    });
                }
            }
            break;

        case Screen::API_KEY_ENTRY:
            if (ev == BTN_SELECT) {
                _keyboard.begin("API Key:",
                    [this](const char* val) {
                        llm_client_set_api_key(val);
                        this->_screen = Screen::MAIN_MENU;
                    },
                    [this]() { this->_screen = Screen::MAIN_MENU; }
                );
            }
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            break;

        case Screen::MAIN_MENU:
            if (ev == BTN_LEFT) { switch_to_grid(); return; }
            if (ev == BTN_UP   && _menu_sel > 0) _menu_sel--;
            if (ev == BTN_DOWN && _menu_sel < MENU_ITEMS - 1) _menu_sel++;
            if (ev == BTN_SELECT) {
                if (_menu_sel == 0) { _chat_select_sel = 0; _screen = Screen::CHAT_SELECT; }
                if (_menu_sel == 1) { _screen = Screen::IDE_EDITOR; }
                if (_menu_sel == 2) { _refresh_scripts(); _screen = Screen::SCRIPT_LIST; }
                if (_menu_sel == 3) { _screen = Screen::SETTINGS; }
            }
            break;

        case Screen::CHAT_SELECT:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            if (ev == BTN_UP && _chat_select_sel > 0) _chat_select_sel--;
            if (ev == BTN_DOWN && _chat_select_sel < 1) _chat_select_sel++;
            if (ev == BTN_SELECT) {
                if (_chat_select_sel == 0) {
                    _chat_display = "";
                    llm_client_clear_history();
                    _screen = Screen::CHAT;
                } else { _screen = Screen::CHAT_PHONE; }
            }
            break;

        case Screen::CHAT_PHONE:
            if (ev == BTN_LEFT || ev == BTN_SELECT) { _screen = Screen::CHAT_SELECT; }
            break;

        case Screen::CHAT:
            if (ev == BTN_LEFT) { _screen = Screen::CHAT_SELECT; }
            {
                bool llm_busy = _llm_busy;
                if (_state_mutex) {
                    if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        llm_busy = _llm_busy;
                        xSemaphoreGive(_state_mutex);
                    } else {
                        ESP_LOGW(TAG, "handleInput CHAT: mutex timeout");
                    }
                }
                if (ev == BTN_SELECT && !llm_busy) {
                _chat_input = "";
                _screen = Screen::CHAT_INPUT;
                _keyboard.begin("Message:",
                    [this](const char* msg) {
                        if (msg[0]) {
                            this->_chat_input = msg;
                            this->_send_chat_message();
                        }
                        this->_screen = Screen::CHAT;
                    },
                    [this]() { this->_screen = Screen::CHAT; }
                );
            }
            }
            break;

        case Screen::CHAT_INPUT: break; // keyboard handles it

        case Screen::IDE_EDITOR:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            if (ev == BTN_SELECT) {
                _screen = Screen::IDE_KEYBOARD;
                _keyboard.begin("Edit Script:",
                    [this](const char* text) {
                        this->_ide_buf = text;
                        this->_ide_dirty = true;
                        this->_screen = Screen::IDE_EDITOR;
                    },
                    [this]() { this->_screen = Screen::IDE_EDITOR; }
                );
            }
            if (ev == BTN_RIGHT && _ide_dirty) { _save_ide_file(); }
            if (ev == BTN_DOWN) { _run_lua_script(AI_SCRIPTS_DIR "/" + _ide_filename); }
            break;

        case Screen::IDE_KEYBOARD: break; // keyboard handles it

        case Screen::LUA_RESULT:
            if (ev == BTN_SELECT || ev == BTN_LEFT) { _screen = Screen::SCRIPT_LIST; }
            break;

        case Screen::SETTINGS:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; }
            if (ev == BTN_SELECT) {
                _keyboard.begin("New API Key:",
                    [this](const char* val) {
                        if (val[0]) llm_client_set_api_key(val);
                        this->_screen = Screen::SETTINGS;
                    },
                    [this]() { this->_screen = Screen::SETTINGS; }
                );
            }
            break;

        case Screen::SCRIPT_LIST: handle_script_list_input(ev); break;

        default: break;
    }
}
