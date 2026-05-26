/*
 * App_AICode.h — AI Assistant Application (ESP-CLaW inspired)
 * ============================================================
 * Features:
 *   - Wi-Fi STA connection flow (reuses existing scan/password UI)
 *   - Chat UI with real-time token streaming from cloud LLM
 *   - On-device Lua IDE: text editor, syntax highlight, save/run
 *   - Script manager (list /games/ai/scripts/)
 *   - API key setup screen
 *   - Universal peripheral support via Lua HAL
 */
#pragma once

#include "IApp.h"
#include "../../config/sys_config.h"
#include "../../services/wifi_mgr.h"
#include "../../services/llm_client.h"
#include "../../services/lua_engine.h"
#include "../components/UI_WifiScanner.h"
#include "../components/UI_Keyboard.h"
#include <string>
#include <vector>
#include <stdint.h>

/* ── Maximum sizes (PSRAM-backed strings) ─────────────────────────────── */
#define AIAPP_MAX_INPUT_LEN   256
#define AIAPP_MAX_RESPONSE_LEN 4096
#define AIAPP_MAX_SCRIPT_LEN  (16 * 1024)
#define AIAPP_MAX_SCRIPTS     32
#define AIAPP_CHAT_LINES      6   /* Visible chat history lines on screen */

class App_AICode : public IApp {
public:
    App_AICode();
    ~App_AICode() override;

    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

    /* ── Top-level state machine ─────────────────────────────────────── */
    enum class Screen {
        WIFI_CHECK,       /* Check / prompt Wi-Fi scan via component     */
        API_KEY_ENTRY,    /* Enter / update OpenAI API key               */
        MAIN_MENU,        /* Top-level: Chat / IDE / Scripts / Settings  */
        CHAT_SELECT,      /* Select: Inbuilt Keyboard / From Phone       */
        CHAT_PHONE,       /* Info panel for Phone Chat                   */
        CHAT,             /* Chat view — streamed LLM response           */
        CHAT_INPUT,       /* Keyboard overlay for chat message           */
        IDE_EDITOR,       /* Lua script editor                          */
        IDE_KEYBOARD,     /* Keyboard overlay for IDE text entry         */
        SCRIPT_LIST,      /* Browse /games/ai/scripts/                   */
        LUA_RUNNING,      /* Script execution in progress               */
        LUA_RESULT,       /* Show script output / error                  */
        SETTINGS,         /* API key, model, theme                       */
    };

    Screen _screen;
    float  _anim;

    /* ── Reusable UI components (overlay) ───────────────────────────── */
    UI_WifiScanner _wifi_scanner;
    UI_Keyboard    _keyboard;

    /* ── API key entry ───────────────────────────────────────────────── */
    std::string _api_key_buf;

    /* ── Main menu ───────────────────────────────────────────────────── */
    int _menu_sel;
    static constexpr int MENU_ITEMS = 4;
    static const char* MENU_LABELS[MENU_ITEMS];

    /* ── Chat view ───────────────────────────────────────────────────── */
    int         _chat_select_sel;      /* Choice index (Inbuilt / Phone)   */
    std::string _chat_input;           /* Being typed by user              */
    std::string _chat_display;         /* Scrolling display buffer         */
    volatile bool _llm_busy;
    bool _llm_done;
    std::string _llm_reply_acc;        /* Accumulates streamed reply       */

    /* ── IDE / Editor ────────────────────────────────────────────────── */
    std::string  _ide_buf;             /* Script text                      */
    std::string  _ide_filename;        /* Filename (without path)          */
    int          _ide_cursor;          /* Byte offset of cursor            */
    int          _ide_scroll_line;     /* First visible line               */
    bool         _ide_dirty;           /* Unsaved changes                  */

    /* ── Script list ─────────────────────────────────────────────────── */
    std::vector<std::string> _scripts;
    int _script_sel;

    /* ── Lua result ──────────────────────────────────────────────────── */
    std::string _lua_output;
    lua_engine_result_t _lua_result;

    /* ── Rendering helpers ───────────────────────────────────────────── */
    uint16_t blend(uint16_t fg, uint16_t bg, float a);
    void draw_header(LGFX_Sprite* c, const char* title);
    void draw_wifi_check(LGFX_Sprite* c);
    void draw_api_key_entry(LGFX_Sprite* c);
    void draw_main_menu(LGFX_Sprite* c);
    void draw_chat_select(LGFX_Sprite* c);
    void draw_chat_phone(LGFX_Sprite* c);
    void draw_chat(LGFX_Sprite* c);
    void draw_ide(LGFX_Sprite* c);
    void draw_script_list(LGFX_Sprite* c);
    void draw_lua_running(LGFX_Sprite* c);
    void draw_lua_result(LGFX_Sprite* c);
    void draw_settings(LGFX_Sprite* c);

    /* ── Input helpers ───────────────────────────────────────────────── */
    void handle_script_list_input(input_event_t ev);

    /* ── Action helpers ──────────────────────────────────────────────── */
    void _ensure_ai_dirs();
    void _refresh_scripts();
    void _save_ide_file();
    void _run_lua_script(const std::string& path);
    void _send_chat_message();

    /* ── LLM streaming callback (static, passed to llm_client_send) ─── */
    static bool _on_llm_chunk(const char* token, size_t len, void* user);
};

