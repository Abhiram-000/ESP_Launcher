/*
 * App_AICode_ui.cpp — Draw functions and action helpers for App_AICode
 * Old Wi-Fi scan/pick/password/connecting screens and the old virtual
 * keyboard have been REMOVED. They are replaced by the reusable
 * UI_WifiScanner and UI_Keyboard overlay components.
 */
#include "App_AICode.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "../../services/lua_engine.h"
#include "../../services/llm_client.h"
#include "../../services/wifi_mgr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include <string.h>

static const char* TAG = "App_AICode_ui";

#include "../ui_task.h"

void App_AICode::draw_header(LGFX_Sprite* c, const char* title) {
    uint16_t acc = theme_get_accent();
    c->setFont(&fonts::Font0); c->setTextColor(acc);
    c->setTextDatum(textdatum_t::top_center);
    c->drawString(title, c->width()/2, 3);
    c->drawFastHLine(8, 14, c->width()-16, acc);
}

// ─── Wi-Fi check screen (simplified — UI_WifiScanner overlay does the work) ──
void App_AICode::draw_wifi_check(LGFX_Sprite* c) {
    draw_header(c, "AI ASSISTANT");
    int cx = c->width()/2, cy = c->height()/2;
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);
    if (wifi_mgr_is_connected()) {
        c->setTextColor(theme_get_success());
        c->drawString("Wi-Fi Connected", cx, cy - 12);
        c->setTextColor(theme_get_text_dim());
        c->drawString(wifi_mgr_get_connected_ssid(), cx, cy + 2);
        c->drawString("SELECT = Continue", cx, cy + 18);
    } else {
        c->setTextColor(theme_get_text());
        c->drawString("No Wi-Fi", cx, cy - 10);
        c->setTextColor(theme_get_text_dim());
        c->drawString("SELECT = Scan & Connect", cx, cy + 8);
    }
    c->setTextDatum(textdatum_t::bottom_left);
    c->setTextColor(theme_get_text_dim());
    c->drawString("L=Back", 6, c->height() - 4);
}

// ─── API key entry screen (keyboard overlay is triggered by handleInput) ──
void App_AICode::draw_api_key_entry(LGFX_Sprite* c) {
    draw_header(c, "CEREBRAS API KEY");
    int cx = c->width()/2;
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);

    char key[128] = {};
    bool has_key = llm_client_get_api_key(key, sizeof(key)) && key[0];

    const int32_t H = c->height();
    if (has_key) {
        c->setTextColor(theme_get_success());
        c->drawString("Key saved.", cx, H * 30 / 100);
        c->setTextColor(theme_get_text_dim());
        c->drawString("SEL=Change  L=Continue", cx, H * 44 / 100);
    } else {
        c->setTextColor(theme_get_text());
        c->drawString("No API key set.", cx, H * 30 / 100);
        c->setTextColor(theme_get_text_dim());
        c->drawString("Press SELECT to type key.", cx, H * 44 / 100);
        c->drawString("L=Skip", cx, H * 58 / 100);
    }
}

void App_AICode::draw_main_menu(LGFX_Sprite* c) {
    draw_header(c, "AI ASSISTANT");
    const int32_t W  = c->width();
    const int32_t H  = c->height();
    const int32_t HEADER_H = H / 9;
    const int32_t ITEM_H   = H / 5;
    const int32_t MARGIN   = W / 20;
    const int32_t cy = HEADER_H + 4;
    for (int i = 0; i < MENU_ITEMS; i++) {
        bool sel = i == _menu_sel;
        const int32_t iy = cy + i * ITEM_H;
        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(_anim_time * 4.0f);
            c->fillRoundRect(MARGIN, iy, W - MARGIN * 2, ITEM_H - 2, 5,
                color_blend(theme_get_accent(), theme_get_bg(), 0.3f * pulse + 0.2f));
            c->drawRoundRect(MARGIN, iy, W - MARGIN * 2, ITEM_H - 2, 5, theme_get_accent());
        }
        c->setTextDatum(textdatum_t::middle_left);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->drawString(MENU_LABELS[i], MARGIN + W / 25, iy + ITEM_H / 2);
    }
    c->setTextDatum(textdatum_t::bottom_left);
    c->setTextColor(theme_get_text_dim());
    c->drawString("L=Launcher", 4, c->height()-2);
}

void App_AICode::draw_chat_select(LGFX_Sprite* c) {
    draw_header(c, "SELECT INPUT");
    const int32_t W      = c->width();
    const int32_t H      = c->height();
    const int32_t HEADER_H = H / 9;
    const int32_t MARGIN = W / 20;
    const int32_t ITEM_H = H * 27 / 100;
    const int32_t GAP    = H / 16;
    const int32_t cy     = HEADER_H + GAP;
    const char* options[] = {"Inbuilt Keyboard", "From Phone"};
    const char* subs[]    = {"Type on device",   "Connect via WiFi"};

    for (int i = 0; i < 2; i++) {
        bool sel = i == _chat_select_sel;
        const int32_t y = cy + i * (ITEM_H + GAP);
        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(_anim_time * 4.0f);
            c->fillRoundRect(MARGIN, y, W - MARGIN * 2, ITEM_H, 6,
                color_blend(theme_get_accent(), theme_get_bg(), 0.3f * pulse + 0.2f));
            c->drawRoundRect(MARGIN, y, W - MARGIN * 2, ITEM_H, 6, theme_get_accent());
        } else {
            c->fillRoundRect(MARGIN, y, W - MARGIN * 2, ITEM_H, 6,
                color_blend(theme_get_text_dim(), theme_get_bg(), 0.1f));
            c->drawRoundRect(MARGIN, y, W - MARGIN * 2, ITEM_H, 6, theme_get_text_dim());
        }
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::top_left);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->drawString(options[i], MARGIN + W / 30, y + ITEM_H / 5);
        c->setTextColor(theme_get_text_dim());
        c->drawString(subs[i],    MARGIN + W / 30, y + ITEM_H * 55 / 100);
    }
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Confirm  L=Back", W / 2, H - 2);
}

void App_AICode::draw_chat_phone(LGFX_Sprite* c) {
    draw_header(c, "PHONE CHAT");
    const int32_t W        = c->width();
    const int32_t H        = c->height();
    const int32_t HEADER_H = H / 9;
    const int32_t MARGIN   = W / 20;
    const int32_t BOX_H    = H / 6;
    const int32_t LBL_Y1   = HEADER_H + H / 16;
    const int32_t BOX_Y1   = LBL_Y1 + H / 10;
    const int32_t LBL_Y2   = BOX_Y1 + BOX_H + H / 16;
    const int32_t BOX_Y2   = LBL_Y2 + H / 10;
    const int32_t STATUS_Y = H - H / 12;

    c->setFont(&fonts::Font0);
    c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);
    c->drawString("Connect your phone to:", MARGIN, LBL_Y1);

    c->fillRoundRect(MARGIN - 2, BOX_Y1, W - MARGIN * 2 + 4, BOX_H, 4,
                     color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
    c->drawRoundRect(MARGIN - 2, BOX_Y1, W - MARGIN * 2 + 4, BOX_H, 4, theme_get_accent());
    c->setTextColor(theme_get_accent());
    c->drawString(APP_WIFI_SSID, MARGIN + 2, BOX_Y1 + BOX_H / 3);

    c->setTextColor(theme_get_text());
    c->drawString("Then open browser at:", MARGIN, LBL_Y2);

    c->fillRoundRect(MARGIN - 2, BOX_Y2, W - MARGIN * 2 + 4, BOX_H, 4,
                     color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
    c->drawRoundRect(MARGIN - 2, BOX_Y2, W - MARGIN * 2 + 4, BOX_H, 4, theme_get_accent());
    c->setTextColor(theme_get_accent());
    c->drawString("http://192.168.4.1/chat", MARGIN + 2, BOX_Y2 + BOX_H / 3);

    float pulse = 0.6f + 0.4f * sinf(_anim_time * 5.0f);
    c->fillCircle(MARGIN / 2, STATUS_Y, H / 30,
                  color_blend(theme_get_success(), theme_get_bg(), pulse));
    c->setTextColor(theme_get_text_dim());
    c->drawString("Hosting local portal...", MARGIN, STATUS_Y - H / 30);

    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Back", W/2, c->height()-2);
}

void App_AICode::draw_chat(LGFX_Sprite* c) {
    draw_header(c, "AI CHAT");
    int W = c->width(), H = c->height();
    c->setFont(&fonts::Font0); c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);
    int y = 18, line_h = 10;
    // Read cached state — no mutex in render path (cached in loop())
    const std::string& chat_display = _cached_chat_display;
    bool llm_busy = _cached_llm_busy;
    const char* txt = chat_display.c_str();
    int start = (int)chat_display.size() - (H - 50) / line_h * 20;
    if (start < 0) start = 0;
    char line[24] = {};
    int lp = 0;
    for (int i = start; txt[i] && y < H - 28; i++) {
        if (txt[i] == '\n' || lp >= 22) {
            line[lp] = '\0'; c->drawString(line, 4, y); y += line_h; lp = 0;
        } else line[lp++] = txt[i];
    }
    if (lp) { line[lp] = '\0'; c->drawString(line, 4, y); }

    if (llm_busy) {
        float a = fmodf(_anim_time * 5.0f, 360.0f);
        c->drawArc(W - 12, H - 22, 6, 4, (int)a, (int)a+120, theme_get_accent());
    }
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString(llm_busy ? "Responding..." : "SELECT=Type  L=Back", W/2, H-2);
}

void App_AICode::draw_ide(LGFX_Sprite* c) {
    draw_header(c, _ide_filename.c_str());
    int W = c->width(), H = c->height();
    c->setFont(&fonts::Font0);
    int lh = 10, y = 18, lnum = 0;
    const char* src = _ide_buf.c_str();
    int len = _ide_buf.size();
    int cur_line = 0;
    for (int i = 0; i < _ide_cursor; i++) if (src[i] == '\n') cur_line++;

    int i = 0, line = 0;
    while (i <= len && y < H - 20) {
        if (line < _ide_scroll_line) {
            while (i <= len && src[i] != '\n') i++;
            i++; line++; continue;
        }
        char row[24] = {}; int rp = 0;
        while (i <= len && src[i] != '\n' && rp < 22) row[rp++] = src[i++];
        while (i <= len && src[i] != '\n') i++;
        i++;
        bool is_cur = (line == cur_line);
        if (is_cur) c->fillRect(0, y, W, lh, color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
        c->setTextColor(is_cur ? theme_get_text() : theme_get_text_dim());
        c->setTextDatum(textdatum_t::top_left);
        c->drawString(row, 4, y);
        y += lh; line++; lnum++;
    }
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::bottom_left);
    c->drawString(_ide_dirty ? "SEL=Edit R=Save D=Run L=Back" : "SEL=Edit D=Run L=Back", 2, H-2);
}

void App_AICode::draw_script_list(LGFX_Sprite* c) {
    draw_header(c, "SCRIPTS");
    int y = 20, h = 16;
    if (_scripts.empty()) {
        c->setTextColor(theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString("No scripts yet", c->width()/2, c->height()/2);
        return;
    }
    for (int i = 0; i < (int)_scripts.size() && y < c->height()-4; i++, y += h) {
        bool sel = i == _script_sel;
        if (sel) c->fillRoundRect(4, y, c->width()-8, h-2, 3, color_blend(theme_get_accent(), theme_get_bg(), 0.25f));
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_left);
        c->drawString(_scripts[i].c_str(), 10, y + h/2 - 1);
    }
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SEL=Run  R=Edit  L=Back", c->width()/2, c->height()-2);
}

void App_AICode::draw_lua_running(LGFX_Sprite* c) {
    draw_header(c, "RUNNING");
    float a = fmodf(_anim_time * 5.0f, 360.0f);
    c->drawArc(c->width()/2, c->height()/2, 18, 14, (int)a, (int)a+120, theme_get_accent());
    c->setTextColor(theme_get_text_dim()); c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("Executing Lua script...", c->width()/2, c->height()-4);
}

void App_AICode::draw_lua_result(LGFX_Sprite* c) {
    // Read cached state — no mutex in render path (cached in loop())
    lua_engine_result_t lua_result = _cached_lua_result;
    bool ok = lua_result.status == LUA_ENGINE_OK;
    draw_header(c, ok ? "SCRIPT OK" : "SCRIPT ERROR");
    uint16_t col = ok ? theme_get_success() : theme_get_fail();
    c->setFont(&fonts::Font0); c->setTextColor(col);
    c->setTextDatum(textdatum_t::top_left);
    int y = 20;
    std::string ok_msg;
    const char* msg = nullptr;
    if (ok) {
        ok_msg = "Done in " + std::to_string(lua_result.elapsed_ms) + "ms";
        msg = ok_msg.c_str();
    } else {
        msg = lua_result.error;
    }
    c->drawString(msg, 4, y);
    c->setTextColor(theme_get_text_dim()); c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Back", c->width()/2, c->height()-2);
}

void App_AICode::draw_settings(LGFX_Sprite* c) {
    draw_header(c, "SETTINGS");
    c->setTextDatum(textdatum_t::middle_left);
    c->setTextColor(theme_get_text());
    c->drawString("Model: " AI_LLM_MODEL_DEFAULT, 8, 40);
    c->setTextColor(theme_get_text_dim());
    char key[12] = "Not set";
    char k[128] = {};
    if (llm_client_get_api_key(k, sizeof(k)) && k[0]) { strncpy(key, "sk-...", 8); }
    c->drawString(std::string("API: " + std::string(key)).c_str(), 8, 58);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SEL=Change Key  L=Back", c->width()/2, c->height()-2);
}

// ─── CODE_GENERATED screen — review extracted code + Flash action ─────────
void App_AICode::draw_code_generated(LGFX_Sprite* c) {
    draw_header(c, "AI GENERATED CODE");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = W / 20;
    const int32_t LH = 9;   /* line height for code */
    const int32_t CODE_Y = 18;
    const int32_t CODE_H = H - 44;  /* code area height */
    const int32_t ACTIONS_Y = H - 24;

    // Code area with subtle border
    c->fillRoundRect(MARGIN - 1, CODE_Y - 1, W - MARGIN * 2 + 2, CODE_H + 2, 3,
                     color_blend(theme_get_text_dim(), theme_get_bg(), 0.1f));

    // Draw code lines (scrollable)
    const char* src = _gen_code.c_str();
    int len = (int)_gen_code.size();
    int y = CODE_Y + 2;
    int line = 0;
    int i = 0;

    // Skip to scroll position
    int skip = _gen_scroll;
    while (i < len && skip > 0) {
        if (src[i] == '\n') skip--;
        i++;
    }

    // Draw visible lines
    while (i <= len && y < CODE_Y + CODE_H - 2) {
        char row[24] = {};
        int rp = 0;
        while (i <= len && src[i] != '\n' && rp < 22) row[rp++] = src[i++];
        while (i <= len && src[i] != '\n') i++;
        i++; // skip newline
        row[rp] = '\0';

        c->setFont(&fonts::Font0);
        c->setTextColor(theme_get_accent());
        c->setTextDatum(textdatum_t::top_left);
        c->drawString(row, MARGIN + 2, y);
        y += LH;
        line++;
    }

    // Action buttons row
    const char* actions[] = {"Flash", "Edit", "Save", "Back"};
    int btn_w = (W - MARGIN * 2 - 12) / 4;
    for (int a = 0; a < 4; a++) {
        int bx = MARGIN + a * (btn_w + 4);
        bool sel = (a == _gen_action_sel);
        if (sel) {
            c->fillRoundRect(bx, ACTIONS_Y, btn_w, 18, 4, theme_get_accent());
            c->setTextColor(theme_get_bg());
        } else {
            c->drawRoundRect(bx, ACTIONS_Y, btn_w, 18, 4, theme_get_text_dim());
            c->setTextColor(theme_get_text_dim());
        }
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(actions[a], bx + btn_w / 2, ACTIONS_Y + 9);
    }

    // Filename hint
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::bottom_left);
    c->drawString(_gen_filename.c_str(), MARGIN, ACTIONS_Y - 2);
}

// ─── Input handler ────────────────────────────────────────────────────────
void App_AICode::handle_script_list_input(input_event_t ev) {
    if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; return; }
    if (ev == BTN_UP && _script_sel > 0) _script_sel--;
    if (ev == BTN_DOWN && _script_sel < (int)_scripts.size()-1) _script_sel++;
    if (ev == BTN_SELECT && !_scripts.empty()) {
        std::string path = std::string(AI_SCRIPTS_DIR "/") + _scripts[_script_sel];
        _run_lua_script(path);
    }
    if (ev == BTN_RIGHT && !_scripts.empty()) {
        std::string path = std::string(AI_SCRIPTS_DIR "/") + _scripts[_script_sel];
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            char* buf = (char*)heap_caps_malloc(AI_LUA_MAX_SCRIPT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) { ESP_LOGE(TAG, "PSRAM alloc failed for script read"); fclose(f); return; }
            size_t n = fread(buf, 1, AI_LUA_MAX_SCRIPT_BYTES-1, f); buf[n]='\0';
            _ide_buf = buf; heap_caps_free(buf); fclose(f);
            _ide_filename = _scripts[_script_sel];
            _ide_cursor = 0; _ide_scroll_line = 0; _ide_dirty = false;
            _screen = Screen::IDE_EDITOR;
        }
    }
}

// ─── Action helpers ───────────────────────────────────────────────────────
void App_AICode::_ensure_ai_dirs() {
    mkdir(AI_BASE_DIR,    0755);
    mkdir(AI_SCRIPTS_DIR, 0755);
}

void App_AICode::_refresh_scripts() {
    _scripts.clear(); _script_sel = 0;
    DIR* d = opendir(AI_SCRIPTS_DIR);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && _scripts.size() < AIAPP_MAX_SCRIPTS) {
        if (strstr(e->d_name, ".lua")) _scripts.push_back(e->d_name);
    }
    closedir(d);
}

void App_AICode::_save_ide_file() {
    std::string path = std::string(AI_SCRIPTS_DIR "/") + _ide_filename;
    std::string tmp_path = path + ".tmp";
    FILE* f = fopen(tmp_path.c_str(), "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", tmp_path.c_str());
        return;
    }
    size_t written = fwrite(_ide_buf.c_str(), 1, _ide_buf.size(), f);
    fclose(f);
    if (written != _ide_buf.size()) {
        ESP_LOGE(TAG, "fwrite short: %zu vs %zu", written, _ide_buf.size());
        unlink(tmp_path.c_str());
        return;
    }
    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        ESP_LOGE(TAG, "rename failed: %s → %s", tmp_path.c_str(), path.c_str());
        unlink(tmp_path.c_str());
        return;
    }
    _ide_dirty = false;
}

// LLM send runs on the AI worker task so the UI doesn't block
struct _LLMSendArgs { App_AICode* self; char msg[AIAPP_MAX_INPUT_LEN]; };

static void _llm_task(void* arg) {
    auto* a = (_LLMSendArgs*)arg;
    llm_client_send(a->msg, App_AICode::_on_llm_chunk, a->self);
    a->self->_mark_llm_finished();
    delete a;
    vTaskDelete(nullptr);
}

void App_AICode::_send_chat_message() {
    if (_chat_input.empty()) return;
    if (_state_mutex) {
        if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (_llm_busy) {
                xSemaphoreGive(_state_mutex);
                return;
            }
            _chat_display += "\n[You] " + _chat_input + "\n[AI] ";
            _llm_busy = true;
            _llm_done = false;
            xSemaphoreGive(_state_mutex);
        } else {
            ESP_LOGW(TAG, "_send_chat_message: mutex timeout");
            return;
        }
    } else if (_llm_busy) {
        return;
    }
    auto* args = new _LLMSendArgs{};
    if (!args) {
        if (_state_mutex) {
            if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                _llm_busy = false;
                xSemaphoreGive(_state_mutex);
            } else {
                ESP_LOGW(TAG, "_send_chat_message alloc fail: mutex timeout");
                _llm_busy = false;
            }
        } else {
            _llm_busy = false;
        }
        return;
    }
    args->self = this;
    strncpy(args->msg, _chat_input.c_str(), AIAPP_MAX_INPUT_LEN - 1);
    args->msg[AIAPP_MAX_INPUT_LEN - 1] = '\0';
    _chat_input = "";
    BaseType_t rc = xTaskCreatePinnedToCore(_llm_task, "llm_task", AI_WORKER_TASK_STACK,
                                            args, AI_WORKER_TASK_PRIORITY, nullptr, AI_WORKER_TASK_CORE);
    if (rc != pdPASS) {
        delete args;
        if (_state_mutex) {
            if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                _llm_busy = false;
                _llm_done = true;
                xSemaphoreGive(_state_mutex);
            } else {
                ESP_LOGW(TAG, "_send_chat_message task fail: mutex timeout");
                _llm_busy = false;
                _llm_done = true;
            }
        } else {
            _llm_busy = false;
            _llm_done = true;
        }
    }
}

// Lua runs on worker task too so UI stays at 40fps
struct _LuaRunArgs { App_AICode* self; char path[128]; };

static void _lua_task(void* arg) {
    auto* a = (_LuaRunArgs*)arg;
    lua_engine_result_t result = LuaEngine::instance().exec_file(a->path);
    a->self->_publish_lua_result(result);
    delete a;
    vTaskDelete(nullptr);
}

void App_AICode::_run_lua_script(const std::string& path) {
    _screen = Screen::LUA_RUNNING;
    _lua_done = false;
    auto* args = new _LuaRunArgs{};
    if (!args) {
        _lua_result = {};
        _lua_result.status = LUA_ENGINE_ERR_INIT;
        snprintf(_lua_result.error, sizeof(_lua_result.error), "failed to start task");
        _screen = Screen::LUA_RESULT;
        return;
    }
    args->self = this;
    strncpy(args->path, path.c_str(), 127);
    args->path[127] = '\0';
    BaseType_t rc = xTaskCreatePinnedToCore(_lua_task, "lua_task", AI_WORKER_TASK_STACK,
                                            args, AI_WORKER_TASK_PRIORITY, nullptr, AI_WORKER_TASK_CORE);
    if (rc != pdPASS) {
        delete args;
        _lua_result = {};
        _lua_result.status = LUA_ENGINE_ERR_INIT;
        snprintf(_lua_result.error, sizeof(_lua_result.error), "failed to start task");
        _screen = Screen::LUA_RESULT;
    }
}

// ─── Extract Lua code blocks from LLM reply ──────────────────────────────
// Looks for ```lua ... ``` fences. If found, extracts the first block.
// If no fences found, checks if the entire reply looks like Lua code.
void App_AICode::_extract_lua_code(const std::string& reply) {
    _gen_code = "";
    _gen_filename = "";

    // Try to find ```lua ... ``` code fence
    size_t fence_start = reply.find("```lua");
    if (fence_start == std::string::npos) {
        fence_start = reply.find("```Lua");
    }
    if (fence_start == std::string::npos) {
        fence_start = reply.find("```");
    }

    if (fence_start != std::string::npos) {
        // Skip past the opening fence line
        size_t code_start = reply.find('\n', fence_start);
        if (code_start == std::string::npos) return;
        code_start++; // skip the newline

        // Find the closing fence
        size_t fence_end = reply.find("```", code_start);
        if (fence_end == std::string::npos) {
            // No closing fence — take everything after opening
            _gen_code = reply.substr(code_start);
        } else {
            _gen_code = reply.substr(code_start, fence_end - code_start);
        }
    }
    // No fallback heuristic — only extract when code fences are present

    // Trim leading/trailing whitespace from extracted code
    if (!_gen_code.empty()) {
        size_t start = _gen_code.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) _gen_code.erase(0, start);
        size_t end = _gen_code.find_last_not_of(" \t\n\r");
        if (end != std::string::npos) _gen_code.erase(end + 1);
    }

    // Generate filename from first comment or timestamp
    if (!_gen_code.empty()) {
        // Try to extract name from "-- Script: <name>" comment
        size_t name_pos = _gen_code.find("-- Script:");
        if (name_pos != std::string::npos) {
            size_t name_start = name_pos + 10;
            size_t name_end = _gen_code.find('\n', name_start);
            if (name_end == std::string::npos) name_end = _gen_code.size();
            std::string name = _gen_code.substr(name_start, name_end - name_start);
            size_t ns = name.find_first_not_of(" \t");
            size_t ne = name.find_last_not_of(" \t");
            if (ns != std::string::npos) name = name.substr(ns, ne - ns + 1);
            for (auto& ch : name) { if (ch == ' ' || ch == '/') ch = '_'; }
            if (name.size() > 20) name.erase(20);
            _gen_filename = name + ".lua";
        }
        if (_gen_filename.empty()) {
            uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000);
            char buf[32];
            snprintf(buf, sizeof(buf), "ai_%lu.lua", (unsigned long)ts);
            _gen_filename = buf;
        }
    }
}

// ─── Flash generated code — save + run in one step ───────────────────────
void App_AICode::_flash_generated_code() {
    if (_gen_code.empty()) return;

    // Save to scripts directory
    std::string path = std::string(AI_SCRIPTS_DIR "/") + _gen_filename;
    std::string tmp_path = path + ".tmp";
    FILE* f = fopen(tmp_path.c_str(), "w");
    if (!f) {
        _lua_result = {};
        _lua_result.status = LUA_ENGINE_ERR_INIT;
        snprintf(_lua_result.error, sizeof(_lua_result.error), "failed to save script");
        _screen = Screen::LUA_RESULT;
        return;
    }
    fwrite(_gen_code.c_str(), 1, _gen_code.size(), f);
    fclose(f);
    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        _lua_result = {};
        _lua_result.status = LUA_ENGINE_ERR_INIT;
        snprintf(_lua_result.error, sizeof(_lua_result.error), "failed to save script");
        _screen = Screen::LUA_RESULT;
        return;
    }
    _refresh_scripts();

    // Run it immediately
    _run_lua_script(path);
}

// ─── Lua Tool Marketplace Rendering ──────────────────────────────────────
void App_AICode::draw_marketplace(LGFX_Sprite* c) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    /* Each item: name (8px) + gap (2px) + version (8px) + padding (4px) = 22px min */
    const int32_t ITEM_H = (H / 6 < 22) ? 22 : H / 6;

    draw_header(c, "Tool Marketplace");

    if (_cached_mkt_refreshing) {
        /* Animated dots */
        int dots = ((int)(_anim_time * 3.0f)) % 4;
        char msg[32];
        snprintf(msg, sizeof(msg), "Loading%s", std::string(dots, '.').c_str());
        c->setTextColor(theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(msg, CX, H / 2);
        return;
    }

    if (_cached_mkt_tool_count == 0) {
        c->setTextColor(theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString("No tools found", CX, H / 3);
        c->drawString("R=Refresh L=Back", CX, H / 3 + 14);
        return;
    }

    /* Tool list */
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 14;
    int visible = (H - HEADER_H - FOOTER_H) / ITEM_H;
    if (visible < 1) visible = 1;

    /* Auto-scroll to keep selection visible */
    if (_mkt_sel < _mkt_scroll) _mkt_scroll = _mkt_sel;
    if (_mkt_sel >= _mkt_scroll + visible) _mkt_scroll = _mkt_sel - visible + 1;

    for (int i = _mkt_scroll; i < _cached_mkt_tool_count && i < _mkt_scroll + visible; i++) {
        int32_t y = HEADER_H + (i - _mkt_scroll) * ITEM_H;
        bool sel = (i == _mkt_sel);
        const auto& tool = _cached_mkt_tools[i];

        if (sel) {
            c->fillRoundRect(MARGIN - 2, y, W - 2 * MARGIN + 4, ITEM_H - 2, 3,
                theme_get_accent());
        }

        /* Tool name — first line */
        c->setTextColor(sel ? theme_get_bg() : theme_get_text());
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::top_left);
        c->drawString(tool.name, MARGIN + 2, y + 2);

        /* Version + installed badge — second line, clearly separated */
        char info[48];
        if (tool.is_installed) {
            snprintf(info, sizeof(info), "v%s [Installed]", tool.version);
            c->setTextColor(sel ? theme_get_bg() : theme_get_success());
        } else {
            snprintf(info, sizeof(info), "v%s", tool.version);
            c->setTextColor(sel ? theme_get_bg() : theme_get_text_dim());
        }
        c->drawString(info, MARGIN + 2, y + 12);
    }

    /* Footer */
    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SEL=Detail L=Back R=Refresh", CX, H - 2);
}

void App_AICode::draw_marketplace_detail(LGFX_Sprite* c) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;

    // Use cached marketplace state — no side effects in render
    int tool_count = _cached_mkt_tool_count;
    if (_mkt_sel >= tool_count || tool_count == 0) {
        draw_header(c, "No Tool");
        c->setTextColor(theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString("No tool selected", CX, H / 2);
        c->drawString("L=Back", CX, H / 2 + 14);
        return;
    }
    const auto& tool = _cached_mkt_tools[_mkt_sel];

    draw_header(c, tool.name);

    /* Description */
    c->setTextColor(theme_get_text());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::top_left);
    int y = 20;
    /* Simple word wrap */
    const char* p = tool.description;
    while (*p && y < H - 50) {
        int chars = 0;
        const char* line_start = p;
        while (*p && *p != '\n') { chars++; p++; }
        c->drawString(std::string(line_start, chars).c_str(), MARGIN, y);
        y += 12;
        if (*p == '\n') p++;
    }

    /* Author */
    y += 4;
    c->setTextColor(theme_get_text_dim());
    char author_buf[48];
    snprintf(author_buf, sizeof(author_buf), "By: %s", tool.author);
    c->drawString(author_buf, MARGIN, y);
    y += 12;

    /* Size */
    char size_buf[32];
    if (tool.size_bytes > 1024) {
        snprintf(size_buf, sizeof(size_buf), "Size: %.1f KB", tool.size_bytes / 1024.0f);
    } else {
        snprintf(size_buf, sizeof(size_buf), "Size: %u B", tool.size_bytes);
    }
    c->drawString(size_buf, MARGIN, y);
    y += 16;

    /* Action buttons */
    const char* actions[3];
    if (tool.is_installed) {
        actions[0] = "Run";
        actions[1] = "Uninstall";
    } else {
        actions[0] = "Install";
        actions[1] = "Back";
    }
    actions[2] = "Back";

    for (int i = 0; i < 3; i++) {
        bool sel = (i == _mkt_detail_sel);
        int bx = MARGIN + (i * (W - 2 * MARGIN) / 3);
        int bw = (W - 2 * MARGIN) / 3 - 4;

        if (sel) {
            c->fillRoundRect(bx, y, bw, 16, 3, theme_get_accent());
            c->setTextColor(theme_get_bg());
        } else {
            c->drawRoundRect(bx, y, bw, 16, 3, theme_get_text_dim());
            c->setTextColor(theme_get_text());
        }
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(actions[i], bx + bw / 2, y + 8);
    }
}

void App_AICode::draw_marketplace_install(LGFX_Sprite* c) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;

    draw_header(c, "Installing...");

    /* Spinner */
    float angle = _anim_time * 6.0f;
    for (int i = 0; i < 8; i++) {
        float a = angle + i * 0.785f;
        int r = 12;
        int sx = CX + (int)(cosf(a) * r);
        int sy = H / 2 + (int)(sinf(a) * r);
        float brightness = (sinf(a) + 1.0f) * 0.5f;
        uint16_t col = color_blend(theme_get_accent(), theme_get_bg(), brightness);
        c->fillCircle(sx, sy, 2, col);
    }

    /* Status text */
    c->setTextColor(theme_get_text());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString(_cached_mkt_status_msg, CX, H / 2 + 20);

    c->setTextColor(theme_get_text_dim());
    c->drawString("L=Cancel", CX, H - 10);
}
