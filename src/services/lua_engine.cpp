/*
 * lua_engine.cpp — Lua 5.4.8 Runtime for ESP32-S3 Launcher
 * Part 1: Core engine, PSRAM allocator, watchdog hook, sys/disp/fs bindings
 */

// FreeRTOS must come before Lua includes to avoid type conflicts on Xtensa
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lua_engine.h"
#include "../config/sys_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "esp_http_client.h"
#include "../../include/LGFX_Config.h"

#include <stdio.h>
#include <string.h>
#include <string>

static const char* TAG = "LuaEngine";

// ─── Forward declaration of the global TFT used in bindings ─────────────────
extern LGFX_ESP32S3_ST7735 g_tft;  // defined in ui_task.cpp

// ─── Singleton ───────────────────────────────────────────────────────────────
LuaEngine& LuaEngine::instance() {
    static LuaEngine s;
    return s;
}

LuaEngine::LuaEngine()
    : m_L(nullptr), m_exec_mutex(nullptr), m_log_mutex(nullptr),
      _abort_requested(false), _start_ms(0) {}
LuaEngine::~LuaEngine() { shutdown(); }

// ─── PSRAM Allocator (satisfies LUA_ALLOC_FUNC) ──────────────────────────────
void* lua_psram_alloc(void* /*ud*/, void* ptr, size_t /*osize*/, size_t nsize) {
    if (nsize == 0) { heap_caps_free(ptr); return nullptr; }
    return heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ─── Watchdog / cooperative-yield hook ───────────────────────────────────────
void LuaEngine::_vm_hook(lua_State* L, lua_Debug* /*ar*/) {
    LuaEngine& eng = LuaEngine::instance();
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    vTaskDelay(0);  // yield to FreeRTOS scheduler
    if (eng._abort_requested || (now - eng._start_ms) > AI_LUA_TIMEOUT_MS) {
        eng._abort_requested = false;
        luaL_error(L, "script timeout or abort requested");
    }
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────
bool LuaEngine::init() {
    if (!m_exec_mutex) {
        m_exec_mutex = xSemaphoreCreateRecursiveMutex();
    }
    if (!m_log_mutex) {
        m_log_mutex = xSemaphoreCreateMutex();
    }
    if (!m_exec_mutex || !m_log_mutex) {
        ESP_LOGE(TAG, "LuaEngine mutex allocation failed");
        return false;
    }
    xSemaphoreTakeRecursive(m_exec_mutex, portMAX_DELAY);
    if (m_L) {
        xSemaphoreGiveRecursive(m_exec_mutex);
        return true;
    }
    m_L = lua_newstate(lua_psram_alloc, nullptr);
    if (!m_L) {
        xSemaphoreGiveRecursive(m_exec_mutex);
        ESP_LOGE(TAG, "lua_newstate failed — OOM in PSRAM");
        return false;
    }
    _open_safe_libs();
    _register_bindings();
    lua_sethook(m_L, _vm_hook, LUA_MASKCOUNT, AI_LUA_HOOK_INTERVAL);
    ESP_LOGI(TAG, "Lua 5.4.8 runtime ready (PSRAM)");
    xSemaphoreGiveRecursive(m_exec_mutex);
    return true;
}

void LuaEngine::shutdown() {
    if (m_exec_mutex) {
        xSemaphoreTakeRecursive(m_exec_mutex, portMAX_DELAY);
    }
    if (m_L) { lua_close(m_L); m_L = nullptr; }
    if (m_exec_mutex) {
        xSemaphoreGiveRecursive(m_exec_mutex);
        vSemaphoreDelete(m_exec_mutex);
        m_exec_mutex = nullptr;
    }
    if (m_log_mutex) {
        vSemaphoreDelete(m_log_mutex);
        m_log_mutex = nullptr;
    }
}

void LuaEngine::request_abort() { _abort_requested = true; }

// ─── Safe library whitelist (no io/os — those are replaced by our bindings) ──
void LuaEngine::_open_safe_libs() {
    static const luaL_Reg safe_libs[] = {
        { LUA_GNAME,       luaopen_base      },
        { LUA_TABLIBNAME,  luaopen_table     },
        { LUA_STRLIBNAME,  luaopen_string    },
        { LUA_MATHLIBNAME, luaopen_math      },
        { LUA_UTF8LIBNAME, luaopen_utf8      },
        { nullptr, nullptr }
    };
    for (const luaL_Reg* l = safe_libs; l->func; l++) {
        luaL_requiref(m_L, l->name, l->func, 1);
        lua_pop(m_L, 1);
    }
}

// ─── Execution ────────────────────────────────────────────────────────────────
lua_engine_result_t LuaEngine::exec_string(const char* code) {
    lua_engine_result_t r = {};
    if (!m_exec_mutex && !init()) { r.status = LUA_ENGINE_ERR_INIT; return r; }
    xSemaphoreTakeRecursive(m_exec_mutex, portMAX_DELAY);
    if (!m_L && !init()) { r.status = LUA_ENGINE_ERR_INIT; return r; }
    _start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    _abort_requested = false;
    if (luaL_loadstring(m_L, code) != LUA_OK) {
        r.status = LUA_ENGINE_ERR_COMPILE;
        const char* e = lua_tostring(m_L, -1);
        if (e) snprintf(r.error, sizeof(r.error), "%s", e);
        lua_settop(m_L, 0);
        xSemaphoreGiveRecursive(m_exec_mutex);
        return r;
    }
    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
        r.status = LUA_ENGINE_ERR_RUNTIME;
        const char* e = lua_tostring(m_L, -1);
        if (e) snprintf(r.error, sizeof(r.error), "%s", e);
        lua_settop(m_L, 0);
        xSemaphoreGiveRecursive(m_exec_mutex);
        return r;
    }
    r.status = LUA_ENGINE_OK;
    r.elapsed_ms = (uint32_t)(esp_timer_get_time() / 1000) - _start_ms;
    lua_gc(m_L, LUA_GCCOLLECT, 0);
    xSemaphoreGiveRecursive(m_exec_mutex);
    return r;
}

lua_engine_result_t LuaEngine::exec_file(const char* path) {
    lua_engine_result_t r = {};
    if (!m_exec_mutex && !init()) { r.status = LUA_ENGINE_ERR_INIT; return r; }
    xSemaphoreTakeRecursive(m_exec_mutex, portMAX_DELAY);
    FILE* f = fopen(path, "r");
    if (!f) {
        r.status = LUA_ENGINE_ERR_COMPILE;
        snprintf(r.error, sizeof(r.error), "cannot open: %s", path);
        xSemaphoreGiveRecursive(m_exec_mutex);
        return r;
    }
    char* buf = (char*)heap_caps_malloc(AI_LUA_MAX_SCRIPT_BYTES, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        r.status = LUA_ENGINE_ERR_INIT;
        xSemaphoreGiveRecursive(m_exec_mutex);
        return r;
    }
    size_t n = fread(buf, 1, AI_LUA_MAX_SCRIPT_BYTES - 1, f);
    buf[n] = '\0';
    fclose(f);
    r = exec_string(buf);
    heap_caps_free(buf);
    xSemaphoreGiveRecursive(m_exec_mutex);
    return r;
}

// ─── sys.* bindings ───────────────────────────────────────────────────────────
static int l_sys_delay(lua_State* L) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)luaL_checknumber(L, 1))); return 0;
}
static int l_sys_millis(lua_State* L) {
    lua_pushnumber(L, (lua_Number)(esp_timer_get_time() / 1000)); return 1;
}
static int l_sys_free_heap(lua_State* L) {
    lua_pushnumber(L, (lua_Number)heap_caps_get_free_size(MALLOC_CAP_SPIRAM)); return 1;
}
static std::string s_log_buffer;

void LuaEngine::clear_log_buffer() {
    if (m_log_mutex) {
        xSemaphoreTake(m_log_mutex, portMAX_DELAY);
    }
    s_log_buffer.clear();
    if (m_log_mutex) {
        xSemaphoreGive(m_log_mutex);
    }
}
void LuaEngine::append_log(const char* text) {
    if (m_log_mutex) {
        xSemaphoreTake(m_log_mutex, portMAX_DELAY);
    }
    if (s_log_buffer.size() + strlen(text) < 4096) {
        s_log_buffer += text;
        s_log_buffer += "\n";
    }
    if (m_log_mutex) {
        xSemaphoreGive(m_log_mutex);
    }
}
const char* LuaEngine::get_log_buffer() { return s_log_buffer.c_str(); }

static int l_sys_print(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    ESP_LOGI("LUA", "%s", msg);
    LuaEngine::instance().append_log(msg);
    return 0;
}

void LuaEngine::_bind_sys(lua_State* L) {
    static const luaL_Reg m[] = {
        {"delay",     l_sys_delay},
        {"millis",    l_sys_millis},
        {"free_heap", l_sys_free_heap},
        {"print",     l_sys_print},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "sys");
}

extern "C" {
    void lua_engine_clear_logs(void) {
        LuaEngine::instance().clear_log_buffer();
    }
    void lua_engine_append_log(const char* text) {
        LuaEngine::instance().append_log(text);
    }
    const char* lua_engine_get_logs(void) {
        return LuaEngine::instance().get_log_buffer();
    }
    int lua_engine_run_file(const char* path, char* err_out, int max_err_len, uint32_t* elapsed_ms) {
        lua_engine_result_t res = LuaEngine::instance().exec_file(path);
        if (err_out) {
            strncpy(err_out, res.error, max_err_len - 1);
            err_out[max_err_len - 1] = '\0';
        }
        if (elapsed_ms) *elapsed_ms = res.elapsed_ms;
        return (int)res.status;
    }
}
