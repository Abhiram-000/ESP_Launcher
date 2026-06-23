# ARCHITECTURE.md — ESP32-S3 Launcher Module Map

> **Purpose:** Reference for AI agents to understand module relationships, dependencies, and integration points without reading every file. Read this BEFORE inspecting individual source files.

---

## Directory Structure

```
src/
├── platform/          Hardware abstraction constants (board.h)
├── hal/               Thin ESP-IDF driver wrappers (Core 0)
│   ├── hal_gpio.c/h
│   ├── hal_spi.c/h
│   └── hal_display.c/h
├── drivers/           Peripheral drivers (Core 0)
│   └── led_driver.c/h
├── managers/          Input handling (Core 1)
│   └── input_mgr.c/h
├── services/          Business logic — network, storage, AI (Core 0)
│   ├── wifi_mgr.c/h
│   ├── ota_service.c/h
│   ├── web_server.c/h
│   ├── dns_server.c/h
│   ├── ntp_service.c/h
│   ├── weather_service.c/h
│   ├── hack_store.c/h
│   ├── game_store.c/h
│   ├── led_service.c/h
│   ├── idle_mgr.c/h
│   ├── pomodoro_service.c/h
│   ├── lua_engine.cpp/h
│   ├── lua_tool_store.h
│   ├── lua_bindings_hw.cpp
│   └── llm_client.c/h
├── system/            Orchestration, boot, event routing
│   ├── sys_mgr.c/h
│   └── boot_ctrl.cpp/h
├── ui/                Rendering, input dispatch, apps (Core 1)
│   ├── ui_task.cpp/h
│   ├── theme_mgr.cpp/h
│   ├── components/    Reusable UI widgets
│   │   ├── UI_Keyboard.cpp/h
│   │   └── UI_WifiScanner.cpp/h
│   └── apps/          Individual applications
│       ├── IApp.h            ← Interface contract
│       ├── App_Grid.cpp/h    ← Launcher home screen
│       ├── App_OTA.cpp/h
│       ├── App_Settings.cpp/h
│       ├── App_NES.cpp/h     ← Dynamic (isDynamic()=true)
│       ├── App_GameStore.cpp/h
│       ├── App_HackStore.cpp/h
│       ├── App_Torch.cpp/h
│       ├── App_ClockWeather.cpp/h
│       ├── App_Pomodoro.cpp/h
│       └── App_AICode.cpp/h
├── config/            All compile-time constants
│   ├── sys_config.h   ← Master config
│   ├── web_ui.h
│   ├── web_chat_ui.h
│   ├── web_remote_ui.h
│   └── ai_sys_prompt.h
├── utils/             Shared utilities
│   ├── error_handling.h
│   ├── color_utils.h
│   ├── compile_checks.h
│   ├── display_hal.h
│   ├── perf_monitor.h
│   ├── pool_allocator.c/h
│   └── dirty_rect.c/h
└── assets/icons/      Generated icon C arrays
```

---

## Layer Dependency Model (Strict)

```
┌─────────────────────────────────────────────────┐
│                   UI LAYER                       │
│  ui_task, theme_mgr, apps, components            │
│  Core 1 — NEVER blocks, polls services           │
├─────────────────────────────────────────────────┤
│                SYSTEM LAYER                      │
│  sys_mgr, boot_ctrl                              │
│  Orchestrates init, event routing                 │
├─────────────────────────────────────────────────┤
│               SERVICES LAYER                     │
│  wifi_mgr, ota_service, web_server, lua_engine,  │
│  llm_client, ntp, weather, game_store, hack_store │
│  Core 0 — business logic, network, storage        │
├─────────────────────────────────────────────────┤
│                 HAL LAYER                        │
│  hal_gpio, hal_spi, hal_display, led_driver      │
│  Thin wrappers over ESP-IDF drivers only          │
└─────────────────────────────────────────────────┘
```

**Rule:** Upper layers depend on lower layers. Lower layers NEVER call upper layers.
**Exception:** System manager UI message queue (Core 0 → Core 1) for state updates.

---

## Module Dependency Graph

### Apps → Services

| App | Depends On |
|-----|-----------|
| App_Grid | (none — entry point) |
| App_OTA | ota_service, wifi_mgr |
| App_Settings | wifi_mgr, led_service, idle_mgr |
| App_NES | game_store |
| App_GameStore | game_store |
| App_HackStore | hack_store, ota_service, boot_ctrl |
| App_Torch | led_driver |
| App_ClockWeather | weather_service, ntp_service |
| App_Pomodoro | pomodoro_service |
| App_AICode | lua_engine, llm_client |

### Services → HAL

| Service | Depends On |
|---------|-----------|
| wifi_mgr | (ESP-IDF WiFi API) |
| ota_service | (ESP-IDF OTA API) |
| web_server | wifi_mgr |
| lua_engine | led_driver (GPIO access) |
| led_service | led_driver |

### System → Everything

| Module | Depends On |
|--------|-----------|
| sys_mgr | All services (init sequence), input_mgr, ui_task |
| boot_ctrl | (ESP-IDF RTC reset API) |

---

## Task Architecture

| Task | Core | Stack | Priority | Notes |
|------|------|-------|----------|-------|
| UI task | 1 | 8KB | High | Rendering + input dispatch, never blocks |
| Input ISR | 1 | — | High | GPIO ISR → queue to input_mgr |
| NES emulator | 0 | 48KB | 5 | Heap-allocated, self-deleting |
| Lua worker | 0 | 16KB | 4 | Script execution with 5s watchdog |
| LLM worker | 0 | 16KB | 4 | HTTP streaming to Cerebras API |
| Weather worker | 0 | 8KB | 5 | Geo + forecast fetch |
| HTTP server | 0 | 8KB | 5 | OTA upload handler |
| DNS server | 0 | 4KB | 3 | Captive portal |
| NTP sync | 0 | 4KB | 2 | Time synchronization |
| LED service | 0 | 4KB | 2 | WS2812 control |

---

## Inter-Task Communication

```
GPIO ISR ──ISR Queue──→ input_mgr ──UI Queue──→ ui_task
                                                     │
ota_service ──UI Queue──→ ui_task ◄──UI Queue── sys_mgr
                                                     │
All services ──Mutex──→ get_status() ◄──Poll── App::loop()
```

**Pattern:** Services write to `s_status` under mutex. Apps poll `get_status()` in `loop()`. Render reads cached values — no locking in render path.

---

## Key Interfaces

### IApp (src/ui/apps/IApp.h)
```cpp
class IApp {
    virtual void init() = 0;                    // Called on foreground
    virtual void exit() = 0;                    // Called on background
    virtual void loop() = 0;                    // Non-blocking, every frame
    virtual void render(LGFX_Sprite* canvas) = 0; // Draw to canvas
    virtual void handleInput(input_event_t event) = 0;
    virtual bool isDynamic() const { return false; } // true = heap-allocated
};
```

### Service Pattern (all services follow this)
```c
esp_err_t service_init(void);
esp_err_t service_start(const char* param);   // Non-blocking
esp_err_t service_abort(void);
void      service_get_status(service_status_t* out); // Mutex copy
bool      service_is_busy(void);
```

### input_event_t (src/managers/input_mgr.h)
```c
typedef enum {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT
} input_event_t;
```

---

## NVS Namespaces

| Namespace | Keys | Owner |
|-----------|------|-------|
| `ntp_time` | `epoch`, `timer_us` | ntp_service |
| `ai_cfg` | `api_key`, `provider` | llm_client |

---

## LittleFS Layout

```
/storage/
├── roms/              NES ROM files (game_store)
├── catalog.json       Game catalog cache
├── ai/
│   ├── config.json    AI assistant config
│   ├── history.jsonl  Chat history
│   ├── sys_prompt.txt System prompt for LLM
│   ├── scripts/       Lua scripts
│   └── lua_tools_catalog.json  Tool marketplace cache
```

---

## Partition Table

| Name | Type | SubType | Offset | Size | Purpose |
|------|------|---------|--------|------|---------|
| ota_0 | app | ota_0 | 0x10000 | 3MB | Launcher (always boots first) |
| ota_1 | app | ota_1 | 0x310000 | 3MB | New firmware (rollback target) |
| nvs | data | nvs | 0x610000 | 64KB | Non-volatile storage |
| games | data | spiffs | 0x620000 | ~3.4MB | NES ROMs + AI data |

---

## Critical Invariants Quick Reference

1. Launcher boots first (ota_0 forced by bootloader hook)
2. OTA rollback active — must call validation before watchdog
3. UI task never blocks — all I/O on Core 0
4. NES stack >= 48KB
5. Lua sandboxed — AI data dir only, GPIO blocked, 5s watchdog
6. Atomic writes — `.tmp` → `rename()` for LittleFS
7. Partition changes require full re-flash
8. App_NES is the only dynamic app
9. Open-access auth (intentional for dev phase)
10. ST7735S +4 canvas height offset (panel-specific)
