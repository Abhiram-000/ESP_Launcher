/*
 * App_AICode.h — AI IDE Application (ESP-CLaW inspired)
 * ============================================================
 * Features:
 *   - Wi-Fi STA connection flow (reuses existing scan/password UI)
 *   - Chat UI with real-time token streaming from cloud LLM
 *   - AI code generation with Flash action (save + run in one step)
 *   - On-device Lua IDE: text editor, syntax highlight, save/run
 *   - Script manager (list /storage/ai/scripts/)
 *   - API key setup screen
 *   - Universal peripheral support via Lua HAL
 */
#pragma once

#include "IApp.h"
#include "../../config/sys_config.h"
#include "../../services/wifi_mgr.h"
#include "../../services/llm_client.h"
#include "../../services/lua_engine.h"
#include "../../services/lua_tool_store.h"
#include "../components/UI_WifiScanner.h"
#include "../components/UI_Keyboard.h"
#include "freertos/semphr.h"
#include <string>
#include <vector>
#include <stdint.h>

/* ── Maximum sizes (PSRAM-backed strings) ─────────────────────────────── */
#define AIAPP_MAX_INPUT_LEN   256
#define AIAPP_MAX_RESPONSE_LEN 4096
#define AIAPP_MAX_SCRIPT_LEN  (16 * 1024)
#define AIAPP_MAX_SCRIPTS     32
#define AIAPP_CHAT_LINES      6   /* Visible chat history lines on screen */
#define AIAPP_MAX_CODE_EXTRACT (16 * 1024) /* Max extracted Lua code block */

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
        CODE_GENERATED,   /* AI generated code — review + Flash action   */
        IDE_EDITOR,       /* Lua script editor                          */
        IDE_KEYBOARD,     /* Keyboard overlay for IDE text entry         */
        SCRIPT_LIST,      /* Browse /storage/ai/scripts/                 */
        LUA_RUNNING,      /* Script execution in progress               */
        LUA_RESULT,       /* Show script output / error                  */
        SETTINGS,         /* API key, model, theme                       */
        MARKETPLACE,        /* Browse Lua tool catalog                    */
        MARKETPLACE_DETAIL, /* Tool detail: description + actions        */
        MARKETPLACE_INSTALL,/* Installing tool (progress)                */
    };

    Screen _screen;
    float  _anim_time;

    /* ── Reusable UI components (overlay) ───────────────────────────── */
    UI_WifiScanner _wifi_scanner;
    UI_Keyboard    _keyboard;

    /* ── API key entry ───────────────────────────────────────────────── */
    std::string _api_key_buf;

    /* ── Main menu ───────────────────────────────────────────────────── */
    int _menu_sel;
    static constexpr int MENU_ITEMS = 5;
    static const char* MENU_LABELS[MENU_ITEMS];

    /* ── Chat view ───────────────────────────────────────────────────── */
    int         _chat_select_sel;      /* Choice index (Inbuilt / Phone)   */
    std::string _chat_input;           /* Being typed by user              */
    std::string _chat_display;         /* Scrolling display buffer         */
    volatile bool _llm_busy;
    bool _llm_done;
    std::string _llm_reply_acc;        /* Accumulates streamed reply       */
    SemaphoreHandle_t _state_mutex;

    /* ── Cached state for render (copied under mutex in loop()) ────── */
    std::string _cached_chat_display;
    bool        _cached_llm_busy;
    lua_engine_result_t _cached_lua_result;

    /* ── Marketplace cached state (copied under mutex in loop()) ──── */
    int             _cached_mkt_tool_count;
    lua_tool_entry_t _cached_mkt_tools[LUA_TOOL_MAX_TOOLS];
    bool            _cached_mkt_refreshing;
    char            _cached_mkt_status_msg[64];
    bool            _mkt_pending_refresh;   /* deferred refresh flag */
    bool            _llm_extract_pending;   /* deferred code extraction flag */

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
    bool _lua_done;

    /* ── AI Code Generation ──────────────────────────────────────────── */
    std::string _gen_code;          /* Extracted Lua code from LLM reply  */
    std::string _gen_filename;      /* Auto-generated filename            */
    int         _gen_scroll;        /* Code viewer scroll position        */
    int         _gen_action_sel;    /* 0=Flash 1=Edit 2=Save 3=Back      */

    /* ── Rendering helpers ───────────────────────────────────────────── */
    void draw_header(LGFX_Sprite* c, const char* title);
    void draw_wifi_check(LGFX_Sprite* c);
    void draw_api_key_entry(LGFX_Sprite* c);
    void draw_main_menu(LGFX_Sprite* c);
    void draw_chat_select(LGFX_Sprite* c);
    void draw_chat_phone(LGFX_Sprite* c);
    void draw_chat(LGFX_Sprite* c);
    void draw_code_generated(LGFX_Sprite* c);
    void draw_ide(LGFX_Sprite* c);
    void draw_script_list(LGFX_Sprite* c);
    void draw_lua_running(LGFX_Sprite* c);
    void draw_lua_result(LGFX_Sprite* c);
    void draw_settings(LGFX_Sprite* c);
    void draw_marketplace(LGFX_Sprite* c);
    void draw_marketplace_detail(LGFX_Sprite* c);
    void draw_marketplace_install(LGFX_Sprite* c);

    /* ── Input helpers ───────────────────────────────────────────────── */
    void handle_script_list_input(input_event_t ev);
    void handle_marketplace_input(input_event_t ev);

    /* ── Action helpers ──────────────────────────────────────────────── */
    void _ensure_ai_dirs();
    void _refresh_scripts();
    void _save_ide_file();
    void _run_lua_script(const std::string& path);
    void _flash_generated_code();   /* Save + run extracted code in one step */
    void _extract_lua_code(const std::string& reply); /* Parse ```lua blocks */
    void _send_chat_message();
    void _mark_llm_finished();
    void _publish_lua_result(const lua_engine_result_t& result);

    /* ── Lua Tool Marketplace ──────────────────────────────────────── */
    int _mkt_sel;
    int _mkt_scroll;
    int _mkt_tool_count;
    lua_tool_entry_t _mkt_tools[LUA_TOOL_MAX_TOOLS];
    volatile bool _mkt_refreshing;
    char _mkt_status_msg[64];
    int _mkt_detail_sel;      /* 0=Install/Run 1=Uninstall 2=Back */
    void _refresh_tool_catalog();
    void _install_tool(const char* id);
    void _uninstall_tool(const char* id);
    void _run_installed_tool(const char* id);

    /* ── LLM streaming callback (static, passed to llm_client_send) ─── */
    static bool _on_llm_chunk(const char* token, size_t len, void* user);
};
