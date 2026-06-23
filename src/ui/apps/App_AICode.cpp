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
    "Chat with AI", "Lua IDE", "Scripts", "Marketplace", "Settings"
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
    : _screen(Screen::WIFI_CHECK), _anim_time(0),
      _menu_sel(0),
      _chat_select_sel(0),
      _llm_busy(false), _llm_done(false),
      _state_mutex(nullptr),
      _cached_llm_busy(false),
      _cached_lua_result({}),
      _cached_mkt_tool_count(0),
      _cached_mkt_refreshing(false),
      _mkt_pending_refresh(false),
      _llm_extract_pending(false),
      _ide_cursor(0), _ide_scroll_line(0), _ide_dirty(false),
      _script_sel(0),
      _lua_done(false),
      _gen_scroll(0), _gen_action_sel(0)
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
    _anim_time     = 0;
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
    _gen_code      = "";
    _gen_filename  = "";
    _gen_scroll    = 0;
    _gen_action_sel = 0;
    _mkt_pending_refresh = false;
    _llm_extract_pending = false;

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
            _llm_extract_pending = true;
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "_mark_llm_finished: mutex timeout");
            _llm_busy = false;
            _llm_done = true;
            _llm_extract_pending = true;
        }
    } else {
        _llm_busy = false;
        _llm_done = true;
        _llm_extract_pending = true;
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
    _anim_time += 0.025f;
    _wifi_scanner.loop();

    // Cache shared state under mutex for render helpers (no mutex in render path)
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _cached_chat_display = _chat_display;
            _cached_llm_busy = _llm_busy;
            _cached_lua_result = _lua_result;

            // Cache marketplace state
            _cached_mkt_tool_count = _mkt_tool_count;
            if (_mkt_tool_count > 0) {
                memcpy(_cached_mkt_tools, _mkt_tools,
                       _mkt_tool_count * sizeof(lua_tool_entry_t));
            }
            _cached_mkt_refreshing = _mkt_refreshing;
            strlcpy(_cached_mkt_status_msg, _mkt_status_msg,
                    sizeof(_cached_mkt_status_msg));

            if (_lua_done) {
                _lua_done = false;
                _screen = Screen::LUA_RESULT;
            }
            if (_llm_extract_pending) {
                _llm_extract_pending = false;
                _llm_done = false;
                // Extract Lua code under mutex — safe for std::string access
                _extract_lua_code(_llm_reply_acc);
                if (!_gen_code.empty() && _screen == Screen::CHAT) {
                    _gen_action_sel = 0;
                    _gen_scroll = 0;
                    _screen = Screen::CODE_GENERATED;
                }
            } else if (_llm_done) {
                _llm_done = false;
            }
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "loop: mutex timeout");
        }
    }

    // Clamp code scroll to valid range
    if (!_gen_code.empty() && _screen == Screen::CODE_GENERATED) {
        int total_lines = 0;
        for (char ch : _gen_code) { if (ch == '\n') total_lines++; }
        if (total_lines < 12) total_lines = 12;
        int max_scroll = total_lines - 12;
        if (max_scroll < 0) max_scroll = 0;
        if (_gen_scroll > max_scroll) _gen_scroll = max_scroll;
        if (_gen_scroll < 0) _gen_scroll = 0;
    }

    // Clamp marketplace selection to valid range (I1 fix: move from render to loop)
    if (_screen == Screen::MARKETPLACE_DETAIL && _mkt_sel >= _mkt_tool_count) {
        _screen = Screen::MARKETPLACE;
    }

    // Deferred marketplace refresh (I2: avoid task creation in handleInput)
    if (_mkt_pending_refresh) {
        _mkt_pending_refresh = false;
        _refresh_tool_catalog();
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
        case Screen::CODE_GENERATED:  draw_code_generated(c); break;
        case Screen::IDE_EDITOR:
        case Screen::IDE_KEYBOARD:    draw_ide(c);           break;
        case Screen::SCRIPT_LIST:     draw_script_list(c);   break;
        case Screen::LUA_RUNNING:     draw_lua_running(c);   break;
        case Screen::LUA_RESULT:      draw_lua_result(c);    break;
        case Screen::SETTINGS:        draw_settings(c);      break;
        case Screen::MARKETPLACE:        draw_marketplace(c);        break;
        case Screen::MARKETPLACE_DETAIL: draw_marketplace_detail(c); break;
        case Screen::MARKETPLACE_INSTALL:draw_marketplace_install(c); break;
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
                if (_menu_sel == 3) { _mkt_sel = 0; _mkt_scroll = 0; _screen = Screen::MARKETPLACE; _mkt_pending_refresh = true; }
                if (_menu_sel == 4) { _screen = Screen::SETTINGS; }
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

        case Screen::CODE_GENERATED:
            if (ev == BTN_LEFT) { _screen = Screen::CHAT; _gen_code = ""; return; }
            if (ev == BTN_UP   && _gen_action_sel > 0) _gen_action_sel--;
            if (ev == BTN_DOWN && _gen_action_sel < 3) _gen_action_sel++;
            if (ev == BTN_SELECT) {
                if (_gen_action_sel == 0) { _flash_generated_code(); }      /* Flash */
                if (_gen_action_sel == 1) {                                 /* Edit */
                    _ide_buf = _gen_code;
                    _ide_filename = _gen_filename;
                    _ide_cursor = 0;
                    _ide_scroll_line = 0;
                    _ide_dirty = true;
                    _screen = Screen::IDE_EDITOR;
                }
                if (_gen_action_sel == 2) {                                 /* Save */
                    std::string path = std::string(AI_SCRIPTS_DIR "/") + _gen_filename;
                    std::string tmp_path = path + ".tmp";
                    FILE* f = fopen(tmp_path.c_str(), "w");
                    if (f) {
                        size_t written = fwrite(_gen_code.c_str(), 1, _gen_code.size(), f);
                        fclose(f);
                        if (written != _gen_code.size()) {
                            ESP_LOGE(TAG, "fwrite short for %s", _gen_filename.c_str());
                            unlink(tmp_path.c_str());
                        } else if (rename(tmp_path.c_str(), path.c_str()) != 0) {
                            ESP_LOGE(TAG, "rename failed: %s", path.c_str());
                            unlink(tmp_path.c_str());
                        }
                    }
                    _refresh_scripts();
                }
                if (_gen_action_sel == 3) { _screen = Screen::CHAT; _gen_code = ""; }
            }
            break;

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

        case Screen::MARKETPLACE:
        case Screen::MARKETPLACE_DETAIL:
        case Screen::MARKETPLACE_INSTALL:
            handle_marketplace_input(ev);
            break;

        default: break;
    }
}

// ─── Lua Tool Marketplace ────────────────────────────────────────────────
void App_AICode::_refresh_tool_catalog() {
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _mkt_refreshing = true;
            _mkt_tool_count = 0;
            memset(_mkt_status_msg, 0, sizeof(_mkt_status_msg));
            strlcpy(_mkt_status_msg, "Refreshing...", sizeof(_mkt_status_msg));
            xSemaphoreGive(_state_mutex);
        }
    }

    /* Run catalog fetch in background — pass this via task arg, not static */
    xTaskCreatePinnedToCore([](void* arg) {
        auto* app = (App_AICode*)arg;
        lua_tool_err_t err = lua_tool_store_refresh_catalog();
        if (app->_state_mutex) {
            if (xSemaphoreTake(app->_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (err == LUA_TOOL_ERR_OK) {
                    app->_mkt_tool_count = lua_tool_store_get_catalog(
                        app->_mkt_tools, LUA_TOOL_MAX_TOOLS);
                    memset(app->_mkt_status_msg, 0, sizeof(app->_mkt_status_msg));
                } else {
                    snprintf(app->_mkt_status_msg, sizeof(app->_mkt_status_msg),
                        "Error: %d", err);
                }
                app->_mkt_refreshing = false;
                xSemaphoreGive(app->_state_mutex);
            } else {
                ESP_LOGW(TAG, "mkt_refresh: mutex timeout");
                app->_mkt_refreshing = false;
            }
        }
        vTaskDelete(NULL);
    }, "mkt_refresh", 4096, this, 3, NULL, 0);
}

void App_AICode::_install_tool(const char* id) {
    if (!id || !wifi_mgr_is_connected()) return;
    _screen = Screen::MARKETPLACE_INSTALL;
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            strlcpy(_mkt_status_msg, "Installing...", sizeof(_mkt_status_msg));
            xSemaphoreGive(_state_mutex);
        }
    }

    /* Pass ID via task argument — lambda can't capture stack vars */
    struct InstallArg { App_AICode* self; char id[32]; };
    InstallArg* arg = (InstallArg*)malloc(sizeof(InstallArg));
    if (!arg) return;
    arg->self = this;
    strlcpy(arg->id, id, sizeof(arg->id));

    xTaskCreatePinnedToCore([](void* a) {
        InstallArg* ia = (InstallArg*)a;
        lua_tool_err_t err = lua_tool_store_install(ia->id);
        if (ia->self->_state_mutex) {
            if (xSemaphoreTake(ia->self->_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (err == LUA_TOOL_ERR_OK) {
                    strlcpy(ia->self->_mkt_status_msg, "Installed!",
                            sizeof(ia->self->_mkt_status_msg));
                    ia->self->_mkt_tool_count = lua_tool_store_get_catalog(
                        ia->self->_mkt_tools, LUA_TOOL_MAX_TOOLS);
                } else {
                    snprintf(ia->self->_mkt_status_msg, sizeof(ia->self->_mkt_status_msg),
                        "Error: %d", err);
                }
                ia->self->_screen = Screen::MARKETPLACE;
                xSemaphoreGive(ia->self->_state_mutex);
            } else {
                ESP_LOGW(TAG, "mkt_install: mutex timeout");
                ia->self->_screen = Screen::MARKETPLACE;
            }
        }
        free(ia);
        vTaskDelete(NULL);
    }, "mkt_install", 4096, arg, 3, NULL, 0);
}

void App_AICode::_uninstall_tool(const char* id) {
    if (!id) return;
    lua_tool_store_uninstall(id);
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _mkt_tool_count = lua_tool_store_get_catalog(
                _mkt_tools, LUA_TOOL_MAX_TOOLS);
            _screen = Screen::MARKETPLACE;
            xSemaphoreGive(_state_mutex);
        }
    }
}

void App_AICode::_run_installed_tool(const char* id) {
    if (!id) return;
    char path[128];
    if (lua_tool_store_get_script_path(id, path, sizeof(path)) == LUA_TOOL_ERR_OK) {
        _run_lua_script(std::string(path));
    }
}

void App_AICode::handle_marketplace_input(input_event_t ev) {
    // Read cached marketplace state — safe without mutex (copied in loop())
    int tool_count = _cached_mkt_tool_count;
    const lua_tool_entry_t* tools = _cached_mkt_tools;

    switch (_screen) {
        case Screen::MARKETPLACE:
            if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; return; }
            if (ev == BTN_UP   && _mkt_sel > 0) _mkt_sel--;
            if (ev == BTN_DOWN && _mkt_sel < tool_count - 1) _mkt_sel++;
            if (ev == BTN_RIGHT) { _mkt_pending_refresh = true; return; }
            if (ev == BTN_SELECT && tool_count > 0) {
                _mkt_detail_sel = 0;
                _mkt_scroll = 0;
                _screen = Screen::MARKETPLACE_DETAIL;
            }
            break;

        case Screen::MARKETPLACE_DETAIL:
            if (ev == BTN_LEFT) { _screen = Screen::MARKETPLACE; return; }
            if (ev == BTN_UP   && _mkt_detail_sel > 0) _mkt_detail_sel--;
            if (ev == BTN_DOWN && _mkt_detail_sel < 2) _mkt_detail_sel++;
            if (ev == BTN_SELECT) {
                if (_mkt_sel < tool_count) {
                    const auto& tool = tools[_mkt_sel];
                    if (_mkt_detail_sel == 0) {
                        if (tool.is_installed) {
                            _run_installed_tool(tool.id);
                        } else {
                            _install_tool(tool.id);
                        }
                    } else if (_mkt_detail_sel == 1) {
                        if (tool.is_installed) {
                            _uninstall_tool(tool.id);
                        } else {
                            _screen = Screen::MARKETPLACE;
                        }
                    } else {
                        _screen = Screen::MARKETPLACE;
                    }
                }
            }
            break;

        case Screen::MARKETPLACE_INSTALL:
            if (ev == BTN_LEFT) { lua_tool_store_abort(); _screen = Screen::MARKETPLACE; return; }
            break;

        default: break;
    }
}
