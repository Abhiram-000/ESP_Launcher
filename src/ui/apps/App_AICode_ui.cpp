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
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = 18;
    int y = HEADER_H + 20;

    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);

    if (wifi_mgr_is_connected()) {
        c->setTextColor(theme_get_success());
        c->drawString("Wi-Fi Connected", CX, y);
        y += 16;
        c->setTextColor(theme_get_text_dim());
        c->drawString(wifi_mgr_get_connected_ssid(), CX, y);
        y += 20;
        c->drawString("SELECT = Continue", CX, y);
    } else {
        c->setTextColor(theme_get_text());
        c->drawString("No Wi-Fi", CX, y);
        y += 20;
        c->setTextColor(theme_get_text_dim());
        c->drawString("SELECT = Scan & Connect", CX, y);
    }

    c->setTextDatum(textdatum_t::bottom_center);
    c->setTextColor(theme_get_text_dim());
    c->drawString("L=Back", CX, H - 2);
}

// ─── API key entry screen (keyboard overlay is triggered by handleInput) ──
void App_AICode::draw_api_key_entry(LGFX_Sprite* c) {
    draw_header(c, "CEREBRAS API KEY");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = 18;
    int y = HEADER_H + 16;

    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);

    char key[128] = {};
    bool has_key = llm_client_get_api_key(key, sizeof(key)) && key[0];

    if (has_key) {
        c->setTextColor(theme_get_success());
        c->drawString("Key saved.", CX, y);
        y += 20;
        c->setTextColor(theme_get_text_dim());
        c->drawString("SEL=Change", CX, y);
        y += 12;
        c->drawString("L=Continue", CX, y);
    } else {
        c->setTextColor(theme_get_text());
        c->drawString("No API key set.", CX, y);
        y += 20;
        c->setTextColor(theme_get_text_dim());
        c->drawString("SELECT = Type key", CX, y);
        y += 16;
        c->drawString("L=Skip", CX, y);
    }
}

void App_AICode::draw_main_menu(LGFX_Sprite* c) {
    draw_header(c, "AI ASSISTANT");
    const int32_t W  = c->width();
    const int32_t H  = c->height();
    const int32_t MARGIN = 6;
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 12;
    const int32_t CONTENT_H = H - HEADER_H - FOOTER_H;
    const int32_t ITEM_H = CONTENT_H / MENU_ITEMS;

    for (int i = 0; i < MENU_ITEMS; i++) {
        bool sel = i == _menu_sel;
        const int32_t iy = HEADER_H + i * ITEM_H;
        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(_anim_time * 4.0f);
            c->fillRoundRect(MARGIN, iy + 1, W - MARGIN * 2, ITEM_H - 3, 4,
                color_blend(theme_get_accent(), theme_get_bg(), 0.3f * pulse + 0.2f));
            c->drawRoundRect(MARGIN, iy + 1, W - MARGIN * 2, ITEM_H - 3, 4, theme_get_accent());
        } else {
            c->drawFastHLine(MARGIN + 8, iy + ITEM_H - 1, W - MARGIN * 2 - 16,
                color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
        }
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_left);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->drawString(MENU_LABELS[i], MARGIN + 8, iy + ITEM_H / 2);
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("L=Back", W / 2, H - 2);
}

void App_AICode::draw_chat_select(LGFX_Sprite* c) {
    draw_header(c, "SELECT INPUT");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 6;
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 12;
    const int32_t CONTENT_H = H - HEADER_H - FOOTER_H;
    const int32_t ITEM_H = CONTENT_H / 2;
    const int32_t GAP = 4;

    const char* options[] = {"Inbuilt Keyboard", "From Phone"};
    const char* subs[]    = {"Type on device",   "Connect via WiFi"};

    for (int i = 0; i < 2; i++) {
        bool sel = i == _chat_select_sel;
        const int32_t y = HEADER_H + i * ITEM_H + GAP / 2;
        const int32_t card_h = ITEM_H - GAP;

        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(_anim_time * 4.0f);
            c->fillRoundRect(MARGIN, y, W - MARGIN * 2, card_h, 5,
                color_blend(theme_get_accent(), theme_get_bg(), 0.3f * pulse + 0.2f));
            c->drawRoundRect(MARGIN, y, W - MARGIN * 2, card_h, 5, theme_get_accent());
        } else {
            c->fillRoundRect(MARGIN, y, W - MARGIN * 2, card_h, 5,
                color_blend(theme_get_text_dim(), theme_get_bg(), 0.08f));
            c->drawRoundRect(MARGIN, y, W - MARGIN * 2, card_h, 5,
                color_blend(theme_get_text_dim(), theme_get_bg(), 0.3f));
        }

        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::top_left);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->drawString(options[i], MARGIN + 6, y + 6);
        c->setTextColor(sel ?
            color_blend(theme_get_text(), theme_get_accent(), 0.4f) :
            theme_get_text_dim());
        c->drawString(subs[i], MARGIN + 6, y + 18);
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SEL=Confirm  L=Back", W / 2, H - 2);
}

void App_AICode::draw_chat_phone(LGFX_Sprite* c) {
    draw_header(c, "PHONE CHAT");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 6;
    const int32_t HEADER_H = 18;
    const int32_t BOX_H = 20;
    int y = HEADER_H + 8;

    c->setFont(&fonts::Font0);

    /* "Connect to:" label */
    c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);
    c->drawString("Connect phone to:", MARGIN, y);
    y += 12;

    /* SSID box */
    c->fillRoundRect(MARGIN, y, W - MARGIN * 2, BOX_H, 4,
                     color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
    c->drawRoundRect(MARGIN, y, W - MARGIN * 2, BOX_H, 4, theme_get_accent());
    c->setTextColor(theme_get_accent());
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString(APP_WIFI_SSID, W / 2, y + BOX_H / 2);
    y += BOX_H + 8;

    /* "Open browser:" label */
    c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);
    c->drawString("Open browser at:", MARGIN, y);
    y += 12;

    /* URL box */
    c->fillRoundRect(MARGIN, y, W - MARGIN * 2, BOX_H, 4,
                     color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
    c->drawRoundRect(MARGIN, y, W - MARGIN * 2, BOX_H, 4, theme_get_accent());
    c->setTextColor(theme_get_accent());
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString("192.168.4.1/chat", W / 2, y + BOX_H / 2);
    y += BOX_H + 10;

    /* Status indicator */
    float pulse = 0.6f + 0.4f * sinf(_anim_time * 5.0f);
    c->fillCircle(MARGIN + 4, y + 4, 4,
                  color_blend(theme_get_success(), theme_get_bg(), pulse));
    c->setTextColor(theme_get_text_dim());
    c->setTextDatum(textdatum_t::middle_left);
    c->drawString("Hosting portal...", MARGIN + 14, y + 4);

    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Back", W / 2, H - 2);
}

void App_AICode::draw_chat(LGFX_Sprite* c) {
    draw_header(c, "AI CHAT");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 4;
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 12;
    const int32_t LINE_H = 10;
    const int32_t MAX_CHARS = (W - MARGIN * 2) / 6;
    const int32_t CONTENT_BOTTOM = H - FOOTER_H;

    c->setFont(&fonts::Font0);
    c->setTextColor(theme_get_text());
    c->setTextDatum(textdatum_t::top_left);

    const std::string& chat_display = _cached_chat_display;
    bool llm_busy = _cached_llm_busy;
    const char* txt = chat_display.c_str();
    int txt_len = (int)chat_display.size();

    /* Calculate how many lines fit */
    int visible_lines = (CONTENT_BOTTOM - HEADER_H) / LINE_H;
    if (visible_lines < 1) visible_lines = 1;

    /* Count total lines */
    int total_lines = 0;
    for (int i = 0; i < txt_len; i++) {
        if (txt[i] == '\n') total_lines++;
        else if (i == 0 || txt[i - 1] == '\n') total_lines++;
    }
    if (total_lines < 1) total_lines = 1;

    /* Scroll to show latest content */
    int scroll_line = total_lines - visible_lines;
    if (scroll_line < 0) scroll_line = 0;

    /* Draw visible lines */
    int y = HEADER_H + 2;
    int line_idx = 0;
    int char_idx = 0;
    bool drawing = (scroll_line == 0);

    while (char_idx < txt_len && y < CONTENT_BOTTOM) {
        char row[24] = {};
        int rp = 0;

        /* Read one line */
        while (char_idx < txt_len && txt[char_idx] != '\n' && rp < MAX_CHARS) {
            row[rp++] = txt[char_idx++];
        }
        while (char_idx < txt_len && txt[char_idx] != '\n') char_idx++;
        if (char_idx < txt_len) char_idx++; /* skip newline */

        if (drawing) {
            row[rp] = '\0';
            c->drawString(row, MARGIN, y);
            y += LINE_H;
        }

        line_idx++;
        if (!drawing && line_idx >= scroll_line) drawing = true;
    }

    /* Loading indicator */
    if (llm_busy) {
        float a = fmodf(_anim_time * 5.0f, 360.0f);
        c->drawArc(W - 10, CONTENT_BOTTOM - 4, 5, 3, (int)a, (int)a + 120, theme_get_accent());
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString(llm_busy ? "Responding..." : "SEL=Type  L=Back", W / 2, H - 2);
}

void App_AICode::draw_ide(LGFX_Sprite* c) {
    draw_header(c, _ide_filename.c_str());
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 4;
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 12;
    const int32_t LH = 10;
    const int32_t MAX_CHARS = (W - MARGIN * 2) / 6;
    const int32_t CONTENT_BOTTOM = H - FOOTER_H;

    c->setFont(&fonts::Font0);

    const char* src = _ide_buf.c_str();
    int len = _ide_buf.size();
    int cur_line = 0;
    for (int i = 0; i < _ide_cursor; i++) if (src[i] == '\n') cur_line++;

    int y = HEADER_H + 2;
    int i = 0, line = 0;

    while (i <= len && y < CONTENT_BOTTOM) {
        if (line < _ide_scroll_line) {
            while (i <= len && src[i] != '\n') i++;
            i++; line++; continue;
        }
        char row[24] = {};
        int rp = 0;
        while (i <= len && src[i] != '\n' && rp < MAX_CHARS) row[rp++] = src[i++];
        while (i <= len && src[i] != '\n') i++;
        i++;

        bool is_cur = (line == cur_line);
        if (is_cur) {
            c->fillRoundRect(MARGIN, y - 1, W - MARGIN * 2, LH + 1, 2,
                color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
        }
        c->setTextColor(is_cur ? theme_get_text() : theme_get_text_dim());
        c->setTextDatum(textdatum_t::top_left);
        c->drawString(row, MARGIN, y);
        y += LH;
        line++;
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString(_ide_dirty ? "SEL=Edit R=Save D=Run L=Back" : "SEL=Edit D=Run L=Back",
        W / 2, H - 2);
}

void App_AICode::draw_script_list(LGFX_Sprite* c) {
    draw_header(c, "SCRIPTS");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 6;
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 12;
    const int32_t ITEM_H = 14;

    if (_scripts.empty()) {
        c->setTextColor(theme_get_text_dim());
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString("No scripts yet", W / 2, H / 2 - 6);
        c->drawString("Write code in IDE", W / 2, H / 2 + 6);
        return;
    }

    int visible = (H - HEADER_H - FOOTER_H) / ITEM_H;
    if (visible < 1) visible = 1;
    if (_script_sel < 0) _script_sel = 0;
    if (_script_sel >= (int)_scripts.size()) _script_sel = (int)_scripts.size() - 1;

    int scroll = _script_sel - visible / 2;
    if (scroll < 0) scroll = 0;
    if (scroll + visible > (int)_scripts.size()) scroll = (int)_scripts.size() - visible;
    if (scroll < 0) scroll = 0;

    for (int i = scroll; i < (int)_scripts.size() && i < scroll + visible; i++) {
        int32_t y = HEADER_H + (i - scroll) * ITEM_H;
        bool sel = i == _script_sel;
        if (sel) {
            c->fillRoundRect(MARGIN, y, W - MARGIN * 2, ITEM_H - 2, 3,
                color_blend(theme_get_accent(), theme_get_bg(), 0.25f));
        }
        c->setFont(&fonts::Font0);
        c->setTextColor(sel ? theme_get_text() : theme_get_text_dim());
        c->setTextDatum(textdatum_t::middle_left);
        c->drawString(_scripts[i].c_str(), MARGIN + 4, y + ITEM_H / 2 - 1);
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SEL=Run  R=Edit  L=Back", W / 2, H - 2);
}

void App_AICode::draw_lua_running(LGFX_Sprite* c) {
    draw_header(c, "RUNNING");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t HEADER_H = 18;
    int cy = HEADER_H + (H - HEADER_H) / 2 - 6;

    float a = fmodf(_anim_time * 5.0f, 360.0f);
    c->drawArc(CX, cy, 16, 12, (int)a, (int)a + 120, theme_get_accent());

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString("Executing...", CX, cy + 22);
    c->drawString("Check serial monitor", CX, cy + 34);
}

void App_AICode::draw_lua_result(LGFX_Sprite* c) {
    lua_engine_result_t lua_result = _cached_lua_result;
    bool ok = lua_result.status == LUA_ENGINE_OK;
    draw_header(c, ok ? "SCRIPT OK" : "SCRIPT ERROR");

    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = 6;
    const int32_t HEADER_H = 18;

    uint16_t col = ok ? theme_get_success() : theme_get_fail();
    c->setFont(&fonts::Font0);
    c->setTextColor(col);
    c->setTextDatum(textdatum_t::top_left);

    int y = HEADER_H + 12;
    if (ok) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Done in %lums", (unsigned long)lua_result.elapsed_ms);
        c->drawString(buf, MARGIN, y);
    } else {
        /* Word-wrap error message */
        const int MAX_CHARS = (W - MARGIN * 2) / 6;
        const char* p = lua_result.error;
        while (*p && y < H - 20) {
            int chars = 0;
            const char* ls = p;
            while (*p && *p != '\n' && chars < MAX_CHARS) { chars++; p++; }
            c->drawString(std::string(ls, chars).c_str(), MARGIN, y);
            y += 12;
            if (*p == '\n') p++;
        }
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SELECT=Back", CX, H - 2);
}

void App_AICode::draw_settings(LGFX_Sprite* c) {
    draw_header(c, "SETTINGS");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 8;
    const int32_t HEADER_H = 18;
    int y = HEADER_H + 12;

    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::top_left);

    /* Model */
    c->setTextColor(theme_get_text());
    c->drawString("Model", MARGIN, y);
    y += 12;
    c->setTextColor(theme_get_accent());
    c->drawString(AI_LLM_MODEL_DEFAULT, MARGIN + 4, y);
    y += 16;

    /* API Key */
    c->setTextColor(theme_get_text());
    c->drawString("API Key", MARGIN, y);
    y += 12;
    char k[128] = {};
    if (llm_client_get_api_key(k, sizeof(k)) && k[0]) {
        c->setTextColor(theme_get_success());
        c->drawString("sk-...", MARGIN + 4, y);
    } else {
        c->setTextColor(theme_get_fail());
        c->drawString("Not set", MARGIN + 4, y);
    }
    y += 20;

    /* Prompt */
    c->setTextColor(theme_get_text_dim());
    c->drawString("SEL = Change Key", MARGIN, y);

    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("L=Back", W / 2, H - 2);
}

// ─── CODE_GENERATED screen — review extracted code + Flash action ─────────
void App_AICode::draw_code_generated(LGFX_Sprite* c) {
    draw_header(c, "AI GENERATED CODE");
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t MARGIN = 6;
    const int32_t LH = 9;
    const int32_t CODE_Y = 18;
    const int32_t BTN_H = 16;
    const int32_t FOOTER_GAP = 4;
    const int32_t CODE_BOTTOM = H - BTN_H - FOOTER_GAP * 2 - 10;

    /* Code area background */
    c->fillRoundRect(MARGIN, CODE_Y, W - MARGIN * 2, CODE_BOTTOM - CODE_Y, 3,
                     color_blend(theme_get_text_dim(), theme_get_bg(), 0.08f));

    /* Draw code lines (scrollable) */
    const char* src = _gen_code.c_str();
    int len = (int)_gen_code.size();
    int y = CODE_Y + 2;
    int i = 0;

    int skip = _gen_scroll;
    while (i < len && skip > 0) {
        if (src[i] == '\n') skip--;
        i++;
    }

    while (i <= len && y < CODE_BOTTOM - 2) {
        char row[24] = {};
        int rp = 0;
        while (i <= len && src[i] != '\n' && rp < 22) row[rp++] = src[i++];
        while (i <= len && src[i] != '\n') i++;
        i++;
        row[rp] = '\0';

        c->setFont(&fonts::Font0);
        c->setTextColor(theme_get_accent());
        c->setTextDatum(textdatum_t::top_left);
        c->drawString(row, MARGIN + 2, y);
        y += LH;
    }

    /* Filename above buttons */
    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_left);
    c->drawString(_gen_filename.c_str(), MARGIN, H - BTN_H - FOOTER_GAP);

    /* Action buttons — anchored at bottom */
    const char* actions[] = {"Flash", "Edit", "Save", "Back"};
    int gap = 3;
    int btn_w = (W - MARGIN * 2 - gap * 3) / 4;
    int btn_y = H - BTN_H;

    for (int a = 0; a < 4; a++) {
        int bx = MARGIN + a * (btn_w + gap);
        bool sel = (a == _gen_action_sel);
        if (sel) {
            c->fillRoundRect(bx, btn_y, btn_w, BTN_H, 3, theme_get_accent());
            c->setTextColor(theme_get_bg());
        } else {
            c->drawRoundRect(bx, btn_y, btn_w, BTN_H, 3, theme_get_text_dim());
            c->setTextColor(theme_get_text_dim());
        }
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(actions[a], bx + btn_w / 2, btn_y + BTN_H / 2);
    }
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
    const int32_t MARGIN = 6;
    const int32_t HEADER_H = 18;
    const int32_t FOOTER_H = 12;
    const int32_t ITEM_H = 20;

    draw_header(c, "Tool Marketplace");

    if (_cached_mkt_refreshing) {
        int dots = ((int)(_anim_time * 3.0f)) % 4;
        char msg[32];
        snprintf(msg, sizeof(msg), "Loading%s", std::string(dots, '.').c_str());
        c->setTextColor(theme_get_text_dim());
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(msg, CX, H / 2 - 6);
        c->setTextColor(theme_get_text_dim());
        c->drawString("Check serial monitor", CX, H / 2 + 8);
        return;
    }

    if (_cached_mkt_tool_count == 0) {
        c->setTextColor(theme_get_text_dim());
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString("No tools found", CX, H / 2 - 6);
        c->drawString("R=Refresh", CX, H / 2 + 8);
        return;
    }

    int visible = (H - HEADER_H - FOOTER_H) / ITEM_H;
    if (visible < 1) visible = 1;

    if (_mkt_sel < _mkt_scroll) _mkt_scroll = _mkt_sel;
    if (_mkt_sel >= _mkt_scroll + visible) _mkt_scroll = _mkt_sel - visible + 1;

    for (int i = _mkt_scroll; i < _cached_mkt_tool_count && i < _mkt_scroll + visible; i++) {
        int32_t y = HEADER_H + (i - _mkt_scroll) * ITEM_H;
        bool sel = (i == _mkt_sel);
        const auto& tool = _cached_mkt_tools[i];

        if (sel) {
            c->fillRoundRect(MARGIN, y, W - MARGIN * 2 - 6, ITEM_H - 2, 3, theme_get_accent());
        } else {
            c->drawFastHLine(MARGIN, y + ITEM_H - 2, W - MARGIN * 2 - 6,
                color_blend(theme_get_text_dim(), theme_get_bg(), 0.2f));
        }

        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::top_left);
        c->setTextColor(sel ? theme_get_bg() : theme_get_text());
        c->drawString(tool.name, MARGIN + 4, y + 2);

        char info[48];
        if (tool.is_installed) {
            snprintf(info, sizeof(info), "v%s  Installed", tool.version);
            c->setTextColor(sel ? theme_get_bg() : theme_get_success());
        } else {
            snprintf(info, sizeof(info), "v%s", tool.version);
            c->setTextColor(sel ? theme_get_bg() : theme_get_text_dim());
        }
        c->drawString(info, MARGIN + 4, y + 11);
    }

    /* Scroll bar */
    if (_cached_mkt_tool_count > visible) {
        int32_t sb_x = W - 4;
        int32_t sb_track_top = HEADER_H;
        int32_t sb_track_h = H - HEADER_H - FOOTER_H;
        int32_t sb_h = (sb_track_h * visible) / _cached_mkt_tool_count;
        if (sb_h < 8) sb_h = 8;
        int32_t sb_y = sb_track_top + (sb_track_h - sb_h) * _mkt_scroll /
            (_cached_mkt_tool_count - visible);
        c->fillRoundRect(sb_x, sb_track_top, 3, sb_track_h, 1,
            color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
        c->fillRoundRect(sb_x, sb_y, 3, sb_h, 1,
            color_blend(theme_get_accent(), theme_get_bg(), 0.6f));
    }

    c->setTextColor(theme_get_text_dim());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::bottom_center);
    c->drawString("SEL=Open  L=Back", CX, H - 2);
}

void App_AICode::draw_marketplace_detail(LGFX_Sprite* c) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = 6;
    const int32_t FOOTER_H = 18;

    int tool_count = _cached_mkt_tool_count;
    if (_mkt_sel >= tool_count || tool_count == 0) {
        draw_header(c, "No Tool");
        c->setTextColor(theme_get_text_dim());
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString("No tool selected", CX, H / 2);
        c->drawString("L=Back", CX, H / 2 + 14);
        return;
    }
    const auto& tool = _cached_mkt_tools[_mkt_sel];

    draw_header(c, tool.name);

    /* Info row: version + author + size — one line */
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::top_left);
    int y = 20;

    c->setTextColor(theme_get_text_dim());
    char info_line[64];
    if (tool.size_bytes > 1024) {
        snprintf(info_line, sizeof(info_line), "v%s  %.1fKB  %s",
            tool.version, tool.size_bytes / 1024.0f, tool.author);
    } else {
        snprintf(info_line, sizeof(info_line), "v%s  %uB  %s",
            tool.version, tool.size_bytes, tool.author);
    }
    c->drawString(info_line, MARGIN, y);
    y += 11;

    /* Separator */
    c->drawFastHLine(MARGIN, y, W - MARGIN * 2,
        color_blend(theme_get_text_dim(), theme_get_bg(), 0.3f));
    y += 4;

    /* Description with proper word wrap */
    c->setTextColor(theme_get_text());
    const int MAX_CHARS = (W - MARGIN * 2) / 6;
    const char* p = tool.description;
    while (*p && y < H - FOOTER_H) {
        /* Skip leading spaces */
        while (*p == ' ') p++;
        if (!*p || *p == '\n') { if (*p == '\n') p++; continue; }

        /* Count chars for this line */
        int chars = 0;
        const char* line_start = p;
        while (*p && *p != '\n' && chars < MAX_CHARS) { chars++; p++; }

        /* Try to break at space for word wrap */
        if (*p && *p != '\n' && chars >= MAX_CHARS) {
            const char* sp = p;
            while (sp > line_start && *sp != ' ') sp--;
            if (sp > line_start) {
                chars = (int)(sp - line_start);
                p = sp + 1;
            }
        }

        c->drawString(std::string(line_start, chars).c_str(), MARGIN, y);
        y += 10;
        if (*p == '\n') p++;
    }

    /* Action buttons — anchored to bottom */
    const char* actions[3];
    int num_actions;
    if (tool.is_installed) {
        actions[0] = "Run"; actions[1] = "Del"; actions[2] = "Back";
        num_actions = 3;
    } else {
        actions[0] = "Install"; actions[1] = "Back";
        num_actions = 2;
    }

    int btn_y = H - FOOTER_H;
    int btn_area_w = W - MARGIN * 2;
    int gap = 4;
    int bw = (btn_area_w - gap * (num_actions - 1)) / num_actions;

    for (int i = 0; i < num_actions; i++) {
        bool sel = (i == _mkt_detail_sel);
        int bx = MARGIN + i * (bw + gap);

        if (sel) {
            c->fillRoundRect(bx, btn_y, bw, 14, 3, theme_get_accent());
            c->setTextColor(theme_get_bg());
        } else {
            c->drawRoundRect(bx, btn_y, bw, 14, 3, theme_get_text_dim());
            c->setTextColor(theme_get_text());
        }
        c->setFont(&fonts::Font0);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(actions[i], bx + bw / 2, btn_y + 7);
    }
}

void App_AICode::draw_marketplace_install(LGFX_Sprite* c) {
    const int32_t W = c->width();
    const int32_t H = c->height();
    const int32_t CX = W / 2;

    draw_header(c, "Installing...");

    const int32_t HEADER_H = 18;
    int cy = HEADER_H + (H - HEADER_H) / 2;

    /* Spinner */
    float angle = _anim_time * 6.0f;
    for (int i = 0; i < 8; i++) {
        float a = angle + i * 0.785f;
        int r = 12;
        int sx = CX + (int)(cosf(a) * r);
        int sy = cy + (int)(sinf(a) * r);
        float brightness = (sinf(a) + 1.0f) * 0.5f;
        uint16_t col = color_blend(theme_get_accent(), theme_get_bg(), brightness);
        c->fillCircle(sx, sy, 2, col);
    }

    /* Status text */
    c->setTextColor(theme_get_text());
    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString(_cached_mkt_status_msg, CX, cy + 20);

    c->setTextColor(theme_get_text_dim());
    c->drawString("Check serial monitor", CX, cy + 32);

    c->drawString("L=Cancel", CX, H - 4);
}
