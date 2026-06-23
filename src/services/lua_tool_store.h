/*
 * lua_tool_store.h — Lua Tool Marketplace Service Interface
 * ==========================================================
 * Purpose: Browse, download, and manage Lua scripts from a GitHub-hosted
 *          catalog. Integrates with App_AICode as a sub-menu option.
 *
 * Pattern: Same as game_store.c and hack_store.c — HTTP fetch catalog,
 *          parse cJSON, download to LittleFS, track installs in NVS.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "../config/sys_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUA_TOOL_ERR_OK = 0,
    LUA_TOOL_ERR_NO_WIFI,
    LUA_TOOL_ERR_NETWORK,
    LUA_TOOL_ERR_BAD_METADATA,
    LUA_TOOL_ERR_NO_SPACE,
    LUA_TOOL_ERR_ALREADY_INSTALLED,
    LUA_TOOL_ERR_TOO_LARGE,
    LUA_TOOL_ERR_ABORTED,
    LUA_TOOL_ERR_FS_ERROR,
} lua_tool_err_t;

typedef struct {
    char id[32];
    char name[LUA_TOOL_MAX_NAME];
    char version[LUA_TOOL_MAX_VERSION];
    char description[LUA_TOOL_MAX_DESC];
    char author[LUA_TOOL_MAX_AUTHOR];
    char url[LUA_TOOL_MAX_URL];
    uint32_t size_bytes;
    bool is_installed;
    bool has_update;
} lua_tool_entry_t;

typedef struct {
    bool is_busy;
    lua_tool_err_t last_error;
    int progress_pct;
    char active_tool_id[32];
    char status_msg[64];
} lua_tool_status_t;

lua_tool_err_t lua_tool_store_init(void);
lua_tool_err_t lua_tool_store_refresh_catalog(void);
int            lua_tool_store_get_catalog(lua_tool_entry_t* out, int max_count);
lua_tool_err_t lua_tool_store_install(const char* id);
lua_tool_err_t lua_tool_store_uninstall(const char* id);
lua_tool_err_t lua_tool_store_abort(void);
void           lua_tool_store_get_status(lua_tool_status_t* out);
bool           lua_tool_store_is_installed(const char* id);
lua_tool_err_t lua_tool_store_get_script_path(const char* id, char* path_out, size_t len);

#ifdef __cplusplus
}
#endif
