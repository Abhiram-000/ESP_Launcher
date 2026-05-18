/*
 * App_AICode_ui.cpp — Draw functions and action helpers for App_AICode
 * Split from App_AICode.cpp for modularity and to stay within file size limits.
 */
#include "App_AICode.h"
#include "../theme_mgr.h"
#include "../../services/lua_engine.h"
#include "../../services/llm_client.h"
#include "../../services/wifi_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/stat.h"
#include "sys/dirent.h"
#include <math.h>
#include <string.h>

extern void switch_to_grid();

// ─── Utility ─────────────────────────────────────────────────────────────
uint16_t App_AICode::blend(uint16_t fg, uint16_t bg, float a) {
    if (a <= 0) return bg;
    if (a >= 1) return fg;
    uint8_t r1=(fg>>11)&0x1F,g1=(fg>>5)&0x3F,b1=fg&0x1F;
    uint8_t r2=(bg>>11)&0x1F,g2=(bg>>5)&0x3F,b2=bg&0x1F;
    return ((uint16_t)(r2+(r1-r2)*a)<<11)|((uint16_t)(g2+(g1-g2)*a)<<5)|(uint16_t)(b2+(b1-b2)*a);
}

void App_AICode::draw_header(LGFX_Sprite* c, const char* title) {
    uint16_t acc = theme_get_accent();
    c->setFont(&fonts::Font0); c->setTextColor(acc);
    c->setTextDatum(textdatum_t::top_center);
    c->drawString(title, c->width()/2, 3);
    c->drawFastHLine(8, 14, c->width()-16, acc);
}

// ─── Virtual keyboard (shared for Chat, IDE, API key) ────────────────────
static const char* KB_ROWS[] = { "1234567890", "qwertyuiop", "asdfghjkl.", "[zxcvbnm<", "S>C" };
static const char* KB_SYM[]  = { "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL_", "[ZXCVBNM<", "S>C" };

void App_AICode::draw_keyboard(LGFX_Sprite* c, std::string& target, bool allow_newline) {
    int W = c->width(), kh = 14, ky = c->height() - 5 * (kh + 2) - 4;

    if (_screen == Screen::CHAT_INPUT || _screen == Screen::IDE_KEYBOARD) {
        int py = ky - 16;
        c->fillRect(4, py, W - 8, 14, blend(theme_get_accent(), theme_get_bg(), 0.08f));
        c->drawRect(4, py, W - 8, 14, theme_get_accent());
        
        std::string preview = target;
        if (preview.empty()) {
            c->setFont(&fonts::Font0);
            c->setTextColor(theme_get_text_dim());
            c->setTextDatum(textdatum_t::middle_left);
            c->drawString("Type here...", 8, py + 7);
        } else {
            if (preview.length() > 18) {
                preview = "..." + preview.substr(preview.length() - 15);
            }
            if ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) % 1000 < 500) {
                preview += "|";
            }
            c->setFont(&fonts::Font0);
            c->setTextColor(theme_get_text());
            c->setTextDatum(textdatum_t::middle_left);
            c->drawString(preview.c_str(), 8, py + 7);
        }
    }

    const char** layout = _kb_sym ? KB_SYM : KB_ROWS;
    for (int r = 0; r < 5; r++) {
        int rl = strlen(layout[r]);
        int kw = (W - 8) / rl - 1;
        for (int col = 0; col < rl; col++) {
            int kx, w;
            if (r == 4) {
                w = (W - 12) / 3;
                kx = 4 + col * (w + 2);
            } else {
                w = kw;
                kx = 4 + col * (w + 1);
            }
            bool sel = (r == _kb_row && col == _kb_col);
            uint16_t bg = sel ? theme_get_accent() : blend(theme_get_text_dim(), theme_get_bg(), 0.2f);
            uint16_t fg = sel ? theme_get_bg() : theme_get_text();
            c->fillRoundRect(kx, ky + r*(kh+2), w, kh, 2, bg);
            c->setTextDatum(textdatum_t::middle_center); c->setTextColor(fg);
            char lbl[8] = { layout[r][col], 0 };
            if (lbl[0] == 'S') strcpy(lbl, "SPC");
            if (lbl[0] == '>') strcpy(lbl, "ENT");
            if (lbl[0] == 'C') strcpy(lbl, "CAN");
            if (lbl[0] == '<') strcpy(lbl, "DEL");
            if (lbl[0] == '[') strcpy(lbl, _kb_caps ? "CAP*" : "CAP");
            c->drawString(lbl, kx + w/2, ky + r*(kh+2) + kh/2);
        }
    }
}

void App_AICode::handle_keyboard_input(input_event_t ev, std::string& target, bool allow_newline) {
    const char** layout = _kb_sym ? KB_SYM : KB_ROWS;
    int rl = strlen(layout[_kb_row]);
    if (ev == BTN_UP)    { if (_kb_row > 0) { _kb_row--; if (_kb_col >= (int)strlen(layout[_kb_row])) _kb_col = strlen(layout[_kb_row])-1; } }
    if (ev == BTN_DOWN)  { if (_kb_row < 4) { _kb_row++; if (_kb_col >= (int)strlen(layout[_kb_row])) _kb_col = strlen(layout[_kb_row])-1; } }
    if (ev == BTN_LEFT)  { if (_kb_col > 0) _kb_col--; }
    if (ev == BTN_RIGHT) { if (_kb_col < rl - 1) _kb_col++; }
    if (ev == BTN_SELECT) {
        char k = layout[_kb_row][_kb_col];
        if (k == 'C') {
            if (_screen == Screen::WIFI_PASSWORD) _screen = Screen::WIFI_PICK;
            else if (_screen == Screen::API_KEY_ENTRY) _screen = Screen::MAIN_MENU;
            else if (_screen == Screen::CHAT_INPUT) _screen = Screen::CHAT;
            else if (_screen == Screen::IDE_KEYBOARD) _screen = Screen::IDE_EDITOR;
            return;
        }
        if (k == '<') { if (!target.empty()) target.pop_back(); }
        else if (k == 'S') target += ' ';
        else if (k == '>' && allow_newline) target += '\n';
        else if (k == '[') _kb_caps = !_kb_caps;
        else if (k != '>') {
            char c = (_kb_caps && k >= 'a' && k <= 'z') ? k - 32 : k;
            if (target.size() < AIAPP_MAX_INPUT_LEN) target += c;
        }
    }
    // WIFI_PASSWORD confirm
    if (_screen == Screen::WIFI_PASSWORD && ev == BTN_SELECT) {
        char k = layout[_kb_row][_kb_col];
        if (k == '>' && !_wifi_pass.empty()) {
            wifi_mgr_connect(_target_ssid.c_str(), _wifi_pass.c_str());
            _conn_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            _screen = Screen::WIFI_CONNECTING;
        }
    }
    // API key confirm
    if (_screen == Screen::API_KEY_ENTRY && ev == BTN_SELECT) {
        char k = layout[_kb_row][_kb_col];
        if (k == '>' && !_api_key_buf.empty()) {
            llm_client_set_api_key(_api_key_buf.c_str());
            _screen = Screen::MAIN_MENU;
        }
    }
}

// ─── Draw functions ───────────────────────────────────────────────────────
void App_AICode::draw_wifi_check(LGFX_Sprite* c) {
    draw_header(c, "AI ASSISTANT");
    int cx = c->width()/2, cy = c->height()/2;
    c->setTextDatum(textdatum_t::middle_center);
    if (wifi_mgr_is_connected()) {
        c->setTextColor(theme_get_success());
        c->drawString("Wi-Fi Connected", cx, cy - 10);
        c->setTextColor(theme_get_text_dim());
        c->drawString(wifi_mgr_get_connected_ssid(), cx, cy + 6);
    } else {
        c->setTextColor(theme_get_text());
        c->drawString("No Wi-Fi", cx, cy - 6);
        c->setTextColor(theme_get_text_dim());
        c->drawString("SELECT = Scan", cx, cy + 10);
    }
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::bottom_left);
    c->drawString("L=Back", 6, c->height() - 4);
}

void App_AICode::draw_wifi_scan(LGFX_Sprite* c) {
    draw_header(c, "SCANNING...");
    int cx = c->width()/2, cy = c->height()/2;
    float a = fmodf(_anim * 5.0f, 360.0f);
    c->drawArc(cx, cy, 14, 11, (int)a, (int)a + 90, theme_get_accent());
    c->drawArc(cx, cy, 14, 11, (int)a + 180, (int)a + 270, theme_get_accent());
}

void App_AICode::draw_wifi_pick(LGFX_Sprite* c) {
    draw_header(c, "SELECT NETWORK");
    int item_h = 16;
    int max_visible = (c->height() - 24) / item_h;
    if (max_visible <= 0) max_visible = 1;

    int start_idx = 0;
    if (_net_sel >= max_visible) {
        start_idx = _net_sel - max_visible + 1;
    }

    int y = 20;
    for (int i = start_idx; i < (int)_networks.size() && i < start_idx + max_visible; i++) {
        bool sel = i == _net_sel;
        if (sel) c->fillRoundRect(4, y, c->width()-8, item_h-2, 3, blend(theme_get_accent(), theme_get_bg(), 0.25f));
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_left);
        
        // Truncate SSID to fit comfortably on small screens without overlapping the RSSI dot
        char ssid_truncated[16];
        strncpy(ssid_truncated, _networks[i].ssid, sizeof(ssid_truncated)-1);
        ssid_truncated[sizeof(ssid_truncated)-1] = '\0';
        
        c->drawString(ssid_truncated, 10, y + item_h/2 - 1);
        
        uint16_t rssi_c = _networks[i].rssi > -60 ? theme_get_success() : theme_get_text_dim();
        c->fillCircle(c->width() - 12, y + item_h/2 - 1, 3, rssi_c);
        y += item_h;
    }
}

void App_AICode::draw_wifi_password(LGFX_Sprite* c) {
    draw_header(c, _target_ssid.c_str());
    c->drawRoundRect(6, 18, c->width()-12, 14, 3, theme_get_text_dim());
    c->setTextDatum(textdatum_t::middle_left);
    c->setTextColor(theme_get_text());
    std::string masked(_wifi_pass.size(), '*');
    c->drawString(masked.c_str(), 12, 25);
    draw_keyboard(c, _wifi_pass, false);
}

void App_AICode::draw_wifi_connecting(LGFX_Sprite* c) {
    draw_header(c, "CONNECTING...");
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(theme_get_text());
    c->drawString(_target_ssid.c_str(), c->width()/2, c->height()/2);
    float p = fmodf(_anim * 20.0f, 100.0f);
    int bw = 100, bx = (c->width()-bw)/2, by = c->height()/2 + 16;
    c->drawRect(bx-1, by-1, bw+2, 8, theme_get_text_dim());
    c->fillRect(bx, by, (int)(p*bw/100.0f), 6, theme_get_accent());
}

void App_AICode::draw_api_key_entry(LGFX_Sprite* c) {
    draw_header(c, "CEREBRAS API KEY");
    c->drawRoundRect(6, 18, c->width()-12, 14, 3, theme_get_accent());
    c->setTextDatum(textdatum_t::middle_left);
    c->setTextColor(theme_get_text());
    std::string masked(_api_key_buf.size(), '*');
    c->drawString(masked.c_str(), 10, 25);
    draw_keyboard(c, _api_key_buf, false);
}

void App_AICode::draw_main_menu(LGFX_Sprite* c) {
    draw_header(c, "AI ASSISTANT");
    int cy = 24, h = 22;
    for (int i = 0; i < MENU_ITEMS; i++) {
        bool sel = i == _menu_sel;
        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(_anim * 4.0f);
            c->fillRoundRect(8, cy + i*h, c->width()-16, h-2, 5,
                blend(theme_get_accent(), theme_get_bg(), 0.3f * pulse + 0.2f));
            c->drawRoundRect(8, cy + i*h, c->width()-16, h-2, 5, theme_get_accent());
        }
        c->setTextDatum(textdatum_t::middle_left);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->drawString(MENU_LABELS[i], 18, cy + i*h + h/2 - 1);
    }
    c->setTextDatum(textdatum_t::bottom_left);
    c->setTextColor(theme_get_text_dim());
    c->drawString("L=Launcher", 4, c->height()-2);
}

void App_AICode::draw_chat_select(LGFX_Sprite* c) {
    draw_header(c, "SELECT INPUT");
    int W = c->width();
    int cy = 30, h = 34;
    const char* options[] = {"Inbuilt Keyboard", "From Phone"};
    const char* subs[] = {"Type on device", "Connect via WiFi"};

    for (int i = 0; i < 2; i++) {
        bool sel = i == _chat_select_sel;
        int y = cy + i * (h + 8);
        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(_anim * 4.0f);
            c->fillRoundRect(8, y, W-16, h, 6,
                blend(theme_get_accent(), theme_get_bg(), 0.3f * pulse + 0.2f));
            c->drawRoundRect(8, y, W-16, h, 6, theme_get_accent());
        } else {
            c->fillRoundRect(8, y, W-16, h, 6, blend(theme_get_text_dim(), theme_get_bg(), 0.1f));
            c->drawRoundRect(8, y, W-16, h, 6, theme_get_text_dim());
        }

        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::top_left);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->drawString(options[i], 16, y + 4);

        c->setTextColor(theme_get_text_dim());
        c->drawString(subs[i], 16, y + 18);
    }
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Confirm  L=Back", W/2, c->height()-2);
}

void App_AICode::draw_chat_phone(LGFX_Sprite* c) {
    draw_header(c, "PHONE CHAT");
    int W = c->width();

    c->setFont(&fonts::Font0);
    c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);

    c->drawString("Connect your phone to:", 8, 20);

    // Neon AP card
    c->fillRoundRect(6, 32, W-12, 22, 4, blend(theme_get_accent(), theme_get_bg(), 0.15f));
    c->drawRoundRect(6, 32, W-12, 22, 4, theme_get_accent());
    c->setTextColor(theme_get_accent());
    c->drawString(APP_WIFI_SSID, 12, 38);

    c->setTextColor(theme_get_text());
    c->drawString("Then open browser at:", 8, 60);

    // Neon IP card
    c->fillRoundRect(6, 72, W-12, 22, 4, blend(theme_get_accent(), theme_get_bg(), 0.15f));
    c->drawRoundRect(6, 72, W-12, 22, 4, theme_get_accent());
    c->setTextColor(theme_get_accent());
    c->drawString("http://192.168.4.1/chat", 12, 78);

    // Status pulse
    float pulse = 0.6f + 0.4f * sinf(_anim * 5.0f);
    c->fillCircle(12, 108, 4, blend(theme_get_success(), theme_get_bg(), pulse));
    c->setTextColor(theme_get_text_dim());
    c->drawString("Hosting local portal...", 22, 104);

    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Back", W/2, c->height()-2);
}

void App_AICode::draw_chat(LGFX_Sprite* c) {
    draw_header(c, "AI CHAT");
    int W = c->width(), H = c->height();
    // Show last N chars of chat_display in scrolling area
    c->setFont(&fonts::Font0); c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);
    int y = 18, line_h = 10;
    const char* txt = _chat_display.c_str();
    int start = (int)_chat_display.size() - (H - 50) / line_h * 20;
    if (start < 0) start = 0;
    // Simple word-wrapped display
    char line[24] = {};
    int lp = 0;
    for (int i = start; txt[i] && y < H - 28; i++) {
        if (txt[i] == '\n' || lp >= 22) {
            line[lp] = '\0'; c->drawString(line, 4, y); y += line_h; lp = 0;
        } else line[lp++] = txt[i];
    }
    if (lp) { line[lp] = '\0'; c->drawString(line, 4, y); }

    if (_llm_busy) {
        float a = fmodf(_anim * 5.0f, 360.0f);
        c->drawArc(W - 12, H - 22, 6, 4, (int)a, (int)a+120, theme_get_accent());
    }
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString(_llm_busy ? "Responding..." : "SELECT=Type  L=Back", W/2, H-2);
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
        if (is_cur) c->fillRect(0, y, W, lh, blend(theme_get_accent(), theme_get_bg(), 0.15f));
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
        if (sel) c->fillRoundRect(4, y, c->width()-8, h-2, 3, blend(theme_get_accent(), theme_get_bg(), 0.25f));
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
    float a = fmodf(_anim * 5.0f, 360.0f);
    c->drawArc(c->width()/2, c->height()/2, 18, 14, (int)a, (int)a+120, theme_get_accent());
    c->setTextColor(theme_get_text_dim()); c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("Executing Lua script...", c->width()/2, c->height()-4);
}

void App_AICode::draw_lua_result(LGFX_Sprite* c) {
    bool ok = _lua_result.status == LUA_ENGINE_OK;
    draw_header(c, ok ? "SCRIPT OK" : "SCRIPT ERROR");
    uint16_t col = ok ? theme_get_success() : theme_get_fail();
    c->setFont(&fonts::Font0); c->setTextColor(col);
    c->setTextDatum(textdatum_t::top_left);
    int y = 20;
    const char* msg = ok ? ("Done in " + std::to_string(_lua_result.elapsed_ms) + "ms").c_str()
                         : _lua_result.error;
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

// ─── Input handlers ───────────────────────────────────────────────────────
void App_AICode::handle_wifi_pick_input(input_event_t ev) {
    if (ev == BTN_LEFT) { _screen = Screen::WIFI_CHECK; return; }
    if (ev == BTN_UP && _net_sel > 0) _net_sel--;
    if (ev == BTN_DOWN && _net_sel < (int)_networks.size()-1) _net_sel++;
    if (ev == BTN_SELECT) {
        _target_ssid = _networks[_net_sel].ssid;
        _wifi_pass   = "";
        _kb_row = 1; _kb_col = 0;
        _screen = Screen::WIFI_PASSWORD;
    }
}

void App_AICode::handle_chat_input(input_event_t ev) {
    const char** layout = _kb_sym ? KB_SYM : KB_ROWS;
    handle_keyboard_input(ev, _chat_input, false);
    // ENTER key = send message
    if (ev == BTN_SELECT && layout[_kb_row][_kb_col] == '>') {
        if (!_chat_input.empty() && !_llm_busy) {
            _send_chat_message();
            _screen = Screen::CHAT;
        }
    }
}

void App_AICode::handle_ide_input(input_event_t ev) {
    size_t before = _ide_buf.size();
    handle_keyboard_input(ev, _ide_buf, true);
    if (_ide_buf.size() != before) { _ide_dirty = true; }
}

void App_AICode::handle_script_list_input(input_event_t ev) {
    if (ev == BTN_LEFT) { _screen = Screen::MAIN_MENU; return; }
    if (ev == BTN_UP && _script_sel > 0) _script_sel--;
    if (ev == BTN_DOWN && _script_sel < (int)_scripts.size()-1) _script_sel++;
    if (ev == BTN_SELECT && !_scripts.empty()) {
        std::string path = std::string(AI_SCRIPTS_DIR "/") + _scripts[_script_sel];
        _run_lua_script(path);
    }
    if (ev == BTN_RIGHT && !_scripts.empty()) {
        // Load script into IDE
        std::string path = std::string(AI_SCRIPTS_DIR "/") + _scripts[_script_sel];
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            char* buf = new char[AI_LUA_MAX_SCRIPT_BYTES];
            size_t n = fread(buf, 1, AI_LUA_MAX_SCRIPT_BYTES-1, f); buf[n]='\0';
            _ide_buf = buf; delete[] buf; fclose(f);
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
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fwrite(_ide_buf.c_str(), 1, _ide_buf.size(), f);
    fclose(f); _ide_dirty = false;
}

// LLM send runs on the AI worker task so the UI doesn't block
struct _LLMSendArgs { App_AICode* self; char msg[AIAPP_MAX_INPUT_LEN]; };

static void _llm_task(void* arg) {
    auto* a = (_LLMSendArgs*)arg;
    llm_client_send(a->msg, App_AICode::_on_llm_chunk, a->self);
    a->self->_llm_busy = false;
    a->self->_llm_done = true;
    delete a;
    vTaskDelete(nullptr);
}

void App_AICode::_send_chat_message() {
    if (_chat_input.empty() || _llm_busy) return;
    _chat_display += "\n[You] " + _chat_input + "\n[AI] ";
    _llm_busy = true; _llm_done = false;
    auto* args = new _LLMSendArgs{};
    args->self = this;
    strncpy(args->msg, _chat_input.c_str(), AIAPP_MAX_INPUT_LEN - 1);
    _chat_input = "";
    xTaskCreatePinnedToCore(_llm_task, "llm_task", AI_WORKER_TASK_STACK,
                            args, AI_WORKER_TASK_PRIORITY, nullptr, AI_WORKER_TASK_CORE);
}

// Lua runs on worker task too so UI stays at 40fps
struct _LuaRunArgs { App_AICode* self; char path[128]; };

static void _lua_task(void* arg) {
    auto* a = (_LuaRunArgs*)arg;
    a->self->_lua_result = LuaEngine::instance().exec_file(a->path);
    a->self->_screen = App_AICode::Screen::LUA_RESULT;  // safe: atomic write
    delete a;
    vTaskDelete(nullptr);
}

void App_AICode::_run_lua_script(const std::string& path) {
    _screen = Screen::LUA_RUNNING;
    auto* args = new _LuaRunArgs{};
    args->self = this;
    strncpy(args->path, path.c_str(), 127);
    xTaskCreatePinnedToCore(_lua_task, "lua_task", AI_WORKER_TASK_STACK,
                            args, AI_WORKER_TASK_PRIORITY, nullptr, AI_WORKER_TASK_CORE);
}
