/*
 * lua_engine.h — Lua 5.4.8 Runtime Engine for ESP32-S3 Launcher
 * ==============================================================
 * Purpose:
 *   Wraps the Lua C-API into a C++ singleton that:
 *   - Creates and owns the lua_State in PSRAM.
 *   - Registers all hardware abstraction bindings (GPIO, I2C, SPI, etc.).
 *   - Provides cooperative task-yield via an instruction-count hook.
 *   - Runs scripts in protected mode (lua_pcall) so errors never crash.
 */
#pragma once

/* Use Lua's official C++ wrapper header */
#include "lua.hpp"
#include "freertos/semphr.h"

#include <stddef.h>
#include <stdint.h>

/* ── Status codes ─────────────────────────────────────────────────────────── */
typedef enum {
    LUA_ENGINE_OK          = 0,
    LUA_ENGINE_ERR_INIT    = 1,  /* Failed to create lua_State (OOM)         */
    LUA_ENGINE_ERR_COMPILE = 2,  /* Syntax error in script                   */
    LUA_ENGINE_ERR_RUNTIME = 3,  /* Runtime error during execution           */
    LUA_ENGINE_ERR_TIMEOUT = 4,  /* Watchdog: script ran too long            */
} lua_engine_status_t;

/* ── Max lengths ──────────────────────────────────────────────────────────── */
#define LUA_ENGINE_MAX_ERR_LEN  256   /* Error message buffer                  */
#define LUA_ENGINE_MAX_TIMEOUT_MS 5000 /* Scripts aborted after 5 s             */

/* ── Result struct returned to the caller ────────────────────────────────── */
typedef struct {
    lua_engine_status_t status;
    char                error[LUA_ENGINE_MAX_ERR_LEN];
    uint32_t            elapsed_ms;
} lua_engine_result_t;

#ifdef __cplusplus

/*
 * LuaEngine — Singleton class owning the Lua runtime.
 *
 * Usage:
 *   LuaEngine& eng = LuaEngine::instance();
 *   eng.init();
 *   lua_engine_result_t r = eng.exec_string("print('hello')");
 *   eng.shutdown();
 */
class LuaEngine {
public:
    static LuaEngine& instance();

    /* Lifecycle */
    bool              init();       /* Create state, open safe libs, bind hardware */
    void              shutdown();   /* Destroy lua_State, free PSRAM */
    bool              is_running() const { return m_L != nullptr; }

    /* Execution */
    lua_engine_result_t exec_string(const char* code);
    lua_engine_result_t exec_file(const char* path);   /* LittleFS path */

    /* Abort a running script (called from another task via atomic flag) */
    void request_abort();

    /* Captured log buffer for Web IDE console output */
    void        clear_log_buffer();
    void        append_log(const char* text);
    const char* get_log_buffer();

    /* PSRAM custom allocator — also used by the lua component's -include */
    static void* psram_alloc(void* ud, void* ptr, size_t osize, size_t nsize);

private:
    LuaEngine();
    ~LuaEngine();
    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    lua_State* m_L;
    SemaphoreHandle_t m_exec_mutex;
    SemaphoreHandle_t m_log_mutex;
    volatile bool _abort_requested;
    uint32_t  _start_ms;

    /* Internal helpers */
    void _open_safe_libs();
    void _register_bindings();
    bool _check_abort_hook();   /* Called by the Lua instruction count hook */

    /* FreeRTOS watchdog-safe instruction hook */
    static void _vm_hook(lua_State* L, lua_Debug* ar);

    /* ── Hardware binding registrars ───────────────────────────────────── */
    static void _bind_sys(lua_State* L);
    static void _bind_disp(lua_State* L);
    static void _bind_fs(lua_State* L);
    static void _bind_net(lua_State* L);
    static void _bind_gpio(lua_State* L);
    static void _bind_i2c(lua_State* L);
    static void _bind_spi(lua_State* L);
    static void _bind_uart(lua_State* L);
    static void _bind_adc(lua_State* L);
    static void _bind_pwm(lua_State* L);
};

#endif /* __cplusplus */
