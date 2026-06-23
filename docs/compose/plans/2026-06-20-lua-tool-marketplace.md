# Lua Tool Marketplace — Implementation Plan

> **For agentic workers:** Use compose:subagent or compose:execute to implement this plan task-by-task.

**Goal:** Add a Lua Tool Marketplace as a sub-option within App_AICode, letting users browse, download, and run Lua tools from a GitHub-hosted catalog — similar to how Game Store downloads NES ROMs and Hack Store downloads firmware.

**Architecture:** A new `lua_tool_store` service (C, Core 0) handles catalog fetching, download, and install. App_AICode gets new screens for browsing and running tools. The catalog is a JSON file on GitHub (same pattern as game_store and hack_store). Tools are Lua scripts saved to `/storage/ai/scripts/` and executed by the existing LuaEngine.

**Tech Stack:** ESP-IDF, cJSON, esp_http_client, Lua 5.4, LovyanGFX, FreeRTOS, LittleFS

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `src/services/lua_tool_store.h` | **Create** | Public API: catalog, install, uninstall, run |
| `src/services/lua_tool_store.c` | **Create** | Implementation: HTTP fetch, JSON parse, LittleFS write |
| `src/config/sys_config.h` | **Modify** | Add `LUA_TOOL_*` constants |
| `src/ui/apps/App_AICode.h` | **Modify** | Add marketplace screens, tool store state |
| `src/ui/apps/App_AICode.cpp` | **Modify** | Add marketplace menu item, screen handlers |
| `src/ui/apps/App_AICode_ui.cpp` | **Modify** | Add marketplace rendering functions |
| `src/CMakeLists.txt` | **Verify** | Ensure `lua_tool_store.c` is picked up by GLOB |

---

## Task 1: Define Constants in sys_config.h

**Covers:** System configuration for the tool marketplace

**Files:**
- Modify: `src/config/sys_config.h`

- [ ] **Step 1: Add Lua Tool Store constants**

Add after the `/* --- Hack Store --- */` section (around line 203):

```c
/* --------------------------------------------------------------------------
 * Lua Tool Marketplace — Cloud script store for App_AICode
 * -------------------------------------------------------------------------- */
#define LUA_TOOL_CATALOG_URL \
    "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools_catalog.json"

#define LUA_TOOL_SCRIPTS_DIR     "/storage/ai/scripts"
#define LUA_TOOL_CATALOG_LOCAL   "/storage/ai/lua_tools_catalog.json"
#define LUA_TOOL_MAX_TOOLS       20
#define LUA_TOOL_MAX_NAME        32
#define LUA_TOOL_MAX_DESC        128
#define LUA_TOOL_MAX_URL         192
#define LUA_TOOL_MAX_AUTHOR      32
#define LUA_TOOL_MAX_VERSION     12
#define LUA_TOOL_DL_CHUNK_SIZE   (4 * 1024)
#define LUA_TOOL_HTTP_TIMEOUT_MS 10000
#define LUA_TOOL_MAX_SIZE        (16 * 1024)  /* 16KB max per script */
```

- [ ] **Step 2: Verify compilation**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds (constants only, no logic yet)

---

## Task 2: Create lua_tool_store.h

**Covers:** Public API contract for the Lua tool marketplace service

**Files:**
- Create: `src/services/lua_tool_store.h`

- [ ] **Step 1: Write the header**

```c
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
```

- [ ] **Step 2: Verify compilation**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds (header only)

---

## Task 3: Implement lua_tool_store.c

**Covers:** Catalog fetch, download, install, uninstall, NVS tracking

**Files:**
- Create: `src/services/lua_tool_store.c`

- [ ] **Step 1: Write the implementation**

Key implementation details:
- Use `esp_http_client` for HTTP GET (same pattern as `game_store.c`)
- Parse catalog JSON with cJSON (same pattern as `game_store.c`)
- Save scripts to `/storage/ai/scripts/<id>.lua` using LittleFS
- Track installed tools in NVS namespace `"lua_tools"` with key `"installed"` as a JSON array
- Worker task on Core 0 for network operations
- Mutex-protected status struct for UI polling
- Cooperative abort via `volatile bool s_abort_flag`

The implementation follows the exact same pattern as `game_store.c`:
1. `init()` — mount LittleFS, load installed list from NVS
2. `refresh_catalog()` — HTTP GET catalog URL, parse JSON, store in memory
3. `install(id)` — download script from URL, save to LittleFS, update NVS
4. `uninstall(id)` — delete file from LittleFS, update NVS
5. `get_status()` — mutex-protected copy for UI polling

- [ ] **Step 2: Verify compilation**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds

---

## Task 4: Update App_AICode.h — Add Marketplace State

**Covers:** UI state machine for marketplace screens

**Files:**
- Modify: `src/ui/apps/App_AICode.h`

- [ ] **Step 1: Add marketplace screens to the Screen enum**

In the `Screen` enum, add after `SETTINGS`:

```cpp
        MARKETPLACE,        /* Browse Lua tool catalog              */
        MARKETPLACE_DETAIL, /* Tool detail: description + actions  */
        MARKETPLACE_INSTALL,/* Installing tool (progress)          */
```

- [ ] **Step 2: Add marketplace member variables**

Add after the `/* ── Settings ── */` section:

```cpp
    /* ── Lua Tool Marketplace ──────────────────────────────────────── */
    int _mkt_sel;                          /* Selected tool index       */
    int _mkt_scroll;                       /* Scroll offset             */
    int _mkt_tool_count;                   /* Number of tools in catalog*/
    lua_tool_entry_t _mkt_tools[LUA_TOOL_MAX_TOOLS]; /* Cached catalog */
    lua_tool_entry_t _mkt_detail;          /* Currently viewed tool     */
    bool _mkt_refreshing;                  /* Catalog refresh in progress*/
    char _mkt_status_msg[64];              /* Status message            */
```

- [ ] **Step 3: Add marketplace rendering functions**

Add after `draw_settings`:

```cpp
    void draw_marketplace(LGFX_Sprite* c);
    void draw_marketplace_detail(LGFX_Sprite* c);
    void draw_marketplace_install(LGFX_Sprite* c);
```

- [ ] **Step 4: Add marketplace input handler**

Add after `handle_script_list_input`:

```cpp
    void handle_marketplace_input(input_event_t ev);
```

- [ ] **Step 5: Add marketplace action helpers**

Add after `_publish_lua_result`:

```cpp
    void _refresh_tool_catalog();
    void _install_tool(const char* id);
    void _uninstall_tool(const char* id);
    void _run_installed_tool(const char* id);
```

- [ ] **Step 6: Add includes**

Add at top of header:

```cpp
#include "../../services/lua_tool_store.h"
```

- [ ] **Step 7: Verify compilation**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds (may have unused variable warnings, that's fine)

---

## Task 5: Update App_AICode.cpp — Marketplace Logic

**Covers:** Menu integration, screen transitions, input handling

**Files:**
- Modify: `src/ui/apps/App_AICode.cpp`

- [ ] **Step 1: Update MENU_ITEMS and MENU_LABELS**

Change:
```cpp
static constexpr int MENU_ITEMS = 4;
```
To:
```cpp
static constexpr int MENU_ITEMS = 5;
```

Change MENU_LABELS:
```cpp
const char* App_AICode::MENU_LABELS[App_AICode::MENU_ITEMS] = {
    "Chat with AI", "Lua IDE", "Scripts", "Marketplace", "Settings"
};
```

- [ ] **Step 2: Add marketplace initialization in init()**

In `App_AICode::init()`, after existing initialization, add:
```cpp
    _mkt_sel = 0;
    _mkt_scroll = 0;
    _mkt_tool_count = 0;
    _mkt_refreshing = false;
    memset(_mkt_status_msg, 0, sizeof(_mkt_status_msg));
```

- [ ] **Step 3: Add marketplace screen transitions in handleInput()**

In the `MAIN_MENU` case of `handleInput()`, add handling for the new "Marketplace" menu item (index 3). When SELECT is pressed on "Marketplace":
```cpp
    case 3:  /* Marketplace */
        _screen = Screen::MARKETPLACE;
        _mkt_sel = 0;
        _mkt_scroll = 0;
        _refresh_tool_catalog();
        break;
```

Note: The existing "Settings" was index 3, now it's index 4. Update the Settings case accordingly.

- [ ] **Step 4: Add marketplace input handler**

Implement `handle_marketplace_input()`:
- BTN_UP/DOWN: scroll through tool list
- BTN_SELECT: open tool detail / install / uninstall
- BTN_LEFT: go back to MAIN_MENU
- BTN_RIGHT: refresh catalog

- [ ] **Step 5: Add marketplace action helpers**

Implement `_refresh_tool_catalog()`: calls `lua_tool_store_refresh_catalog()` in a background task, copies results to `_mkt_tools[]`.

Implement `_install_tool(id)`: calls `lua_tool_store_install(id)`, shows progress screen.

Implement `_uninstall_tool(id)`: calls `lua_tool_store_uninstall(id)`.

Implement `_run_installed_tool(id)`: gets script path, sets `_ide_filename`, transitions to `LUA_RUNNING`.

- [ ] **Step 6: Add loop() processing for marketplace**

In `App_AICode::loop()`, add marketplace status polling:
```cpp
    if (_screen == Screen::MARKETPLACE_INSTALL) {
        lua_tool_status_t st;
        lua_tool_store_get_status(&st);
        if (!st.is_busy) {
            if (st.last_error == LUA_TOOL_ERR_OK) {
                snprintf(_mkt_status_msg, sizeof(_mkt_status_msg), "Installed!");
            } else {
                snprintf(_mkt_status_msg, sizeof(_mkt_status_msg), "Error: %d", st.last_error);
            }
            _screen = Screen::MARKETPLACE;
        }
    }
```

- [ ] **Step 7: Route input to marketplace handler**

In the main `handleInput()` switch, add cases for the new screens:
```cpp
    case Screen::MARKETPLACE:
    case Screen::MARKETPLACE_DETAIL:
    case Screen::MARKETPLACE_INSTALL:
        handle_marketplace_input(ev);
        break;
```

- [ ] **Step 8: Verify compilation**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds

---

## Task 6: Update App_AICode_ui.cpp — Marketplace Rendering

**Covers:** Visual rendering of marketplace screens

**Files:**
- Modify: `src/ui/apps/App_AICode_ui.cpp`

- [ ] **Step 1: Implement draw_marketplace()**

Render the tool catalog list:
- Header: "Tool Marketplace"
- List of tools with name, version, installed status
- Selected item highlighted with accent color
- Footer: "SEL=Detail L=Back R=Refresh"
- Use proportional layout (canvas width/height)
- Show "Refreshing..." if `_mkt_refreshing` is true
- Show "No tools found" if `_mkt_tool_count` is 0

- [ ] **Step 2: Implement draw_marketplace_detail()**

Render tool detail view:
- Header: tool name
- Description text (word-wrapped)
- Author and version
- Size information
- Action buttons: [Install] / [Run] / [Uninstall] / [Back]
- If installed: show "Installed v1.0" badge
- If not installed: show "Download (2.1 KB)" button

- [ ] **Step 3: Implement draw_marketplace_install()**

Render installation progress:
- Animated spinner
- "Installing [tool name]..."
- Progress percentage if available
- "Press L to cancel"

- [ ] **Step 4: Add marketplace to draw routing**

In the main render dispatcher, add:
```cpp
    case Screen::MARKETPLACE:        draw_marketplace(c); break;
    case Screen::MARKETPLACE_DETAIL: draw_marketplace_detail(c); break;
    case Screen::MARKETPLACE_INSTALL:draw_marketplace_install(c); break;
```

- [ ] **Step 5: Verify compilation and visual check**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds

---

## Task 7: Create Lua Tools Catalog JSON

**Covers:** The GitHub-hosted catalog that users download from

**Files:**
- Create: `lua_tools_catalog.json` (for GitHub repo)

- [ ] **Step 1: Write the catalog JSON**

```json
{
    "catalog_version": "1.0",
    "tools": [
        {
            "id": "wifi_scanner",
            "name": "WiFi Scanner",
            "version": "1.0.0",
            "description": "Scan nearby WiFi networks with RSSI, channel, and encryption info",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/wifi_scanner.lua",
            "size_bytes": 1024,
            "tags": ["wifi", "network", "scanner"]
        },
        {
            "id": "i2c_scanner",
            "name": "I2C Bus Scanner",
            "version": "1.0.0",
            "description": "Scan I2C bus and identify connected devices by address",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/i2c_scanner.lua",
            "size_bytes": 768,
            "tags": ["hardware", "i2c", "debugging"]
        },
        {
            "id": "gpio_monitor",
            "name": "GPIO Monitor",
            "version": "1.0.0",
            "description": "Real-time GPIO state monitor with pin toggling",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/gpio_monitor.lua",
            "size_bytes": 512,
            "tags": ["gpio", "hardware", "debugging"]
        },
        {
            "id": "ble_scanner",
            "name": "BLE Scanner",
            "version": "1.0.0",
            "description": "Scan for Bluetooth Low Energy devices and services",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/ble_scanner.lua",
            "size_bytes": 1280,
            "tags": ["bluetooth", "ble", "scanner"]
        },
        {
            "id": "adc_oscilloscope",
            "name": "ADC Oscilloscope",
            "version": "1.0.0",
            "description": "Sample ADC pin and display waveform on screen",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/adc_oscilloscope.lua",
            "size_bytes": 2048,
            "tags": ["adc", "oscilloscope", "signal"]
        },
        {
            "id": "led_patterns",
            "name": "LED Patterns",
            "version": "1.0.0",
            "description": "Run rainbow, breathing, and custom LED patterns on NeoPixel",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/led_patterns.lua",
            "size_bytes": 1536,
            "tags": ["led", "neopixel", "effects"]
        },
        {
            "id": "system_info",
            "name": "System Info",
            "version": "1.0.0",
            "description": "Display system info: CPU, heap, PSRAM, WiFi, uptime",
            "author": "Abhiram",
            "url": "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/lua_tools/system_info.lua",
            "size_bytes": 640,
            "tags": ["system", "info", "diagnostics"]
        }
    ]
}
```

This file goes to the user's GitHub Store repo alongside the existing `games_catalog.json` and `firmware_catalog.json`.

---

## Task 8: Create Pre-built Lua Tools

**Covers:** The actual Lua scripts that users download

**Files:**
- Create: `lua_tools/wifi_scanner.lua`
- Create: `lua_tools/i2c_scanner.lua`
- Create: `lua_tools/gpio_monitor.lua`
- Create: `lua_tools/ble_scanner.lua`
- Create: `lua_tools/adc_oscilloscope.lua`
- Create: `lua_tools/led_patterns.lua`
- Create: `lua_tools/system_info.lua`

Each tool follows this pattern:
```lua
-- Tool: <name>
-- Author: <author>
-- Description: <description>
-- Version: <version>

-- Use existing Lua bindings (sys, gpio, i2c, wifi, disp, etc.)
-- Print output via sys.print()
-- Use sys.delay() for timing
-- Access hardware via hardware binding APIs

function run()
    -- Tool logic here
    sys.print("Tool running...")
    -- ...
end

run()
```

These files go to the GitHub Store repo under `lua_tools/` directory.

---

## Task 9: Wire lua_tool_store.c into Build

**Covers:** Build system integration

**Files:**
- Verify: `src/CMakeLists.txt`

- [ ] **Step 1: Verify GLOB picks up new file**

The existing `CMakeLists.txt` uses:
```cmake
FILE(GLOB_RECURSE app_sources "*.c" "*.cpp")
```

This automatically picks up `lua_tool_store.c`. No changes needed.

- [ ] **Step 2: Add init call in main.cpp**

In `src/main.cpp`, add after `hack_store_init()`:
```c
    // 5d. Initialize Lua Tool Marketplace
    lua_tool_err_t lt_ret = lua_tool_store_init();
    if (lt_ret != LUA_TOOL_ERR_OK) {
        ESP_LOGW(TAG, "Lua tool store init warning: %d", lt_ret);
    }
```

Add include:
```c
#include "services/lua_tool_store.h"
```

- [ ] **Step 3: Verify full build**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: Build succeeds with no errors

---

## Task 10: End-to-End Verification

**Covers:** Full integration test

- [ ] **Step 1: Build firmware**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 2: Run native tests**

Run: `pio test -e native`
Expected: 62/62 PASS (no regressions)

- [ ] **Step 3: Manual verification checklist**

On hardware:
1. Open App_AICode → Main Menu shows 5 items including "Marketplace"
2. Select "Marketplace" → shows tool catalog (or "No tools" if offline)
3. Select a tool → shows detail view with description
4. Press Install → downloads script to `/storage/ai/scripts/`
5. Go to Scripts → new tool appears in list
6. Run the tool → Lua engine executes it
7. Back to Marketplace → tool shows as "Installed"
8. Uninstall → tool removed from scripts list

---

## Edge Cases Handled

| Edge Case | Handling |
|-----------|----------|
| No WiFi | Show "Connect WiFi first" message, prompt scan |
| Catalog fetch fails | Show cached catalog if available, else "No catalog" |
| Script too large (>16KB) | Reject with "Script too large" error |
| Download interrupted | Delete partial file, show error |
| Already installed | Show "Installed" badge, option to reinstall/update |
| Filesystem full | Check free space before download, show "No space" |
| Invalid Lua syntax | LuaEngine catches at runtime, shows error in result screen |
| Catalog JSON malformed | Log error, show "Catalog error" message |
| Concurrent operations | Mutex-protected status, only one operation at a time |
| User cancels download | Cooperative abort flag, clean cleanup |
