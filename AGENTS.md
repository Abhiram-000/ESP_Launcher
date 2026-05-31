# AGENTS.md — ESP32-S3 Custom Modular Launcher

> **Purpose:** This file is the single source of truth for any AI agent or IDE working on this codebase. Read it completely before generating any code. Every instruction here is non-negotiable and derived from the actual shipped code.

---

## 1. PROJECT IDENTITY

**Name:** ESP32-S3 Custom Modular Launcher
**Platform:** ESP32-S3-N16R8 (dual-core Xtensa LX7, 240MHz, 16MB Flash, 8MB PSRAM Octal)
**Framework:** ESP-IDF v5.x via PlatformIO (`framework = espidf`)
**Language:** C17 for HAL/services/drivers, C++17 for UI/apps/engine
**Display:** ST7735S 160×128 TFT, SPI2, DMA-backed via LovyanGFX v1.1.12+
**RTOS:** FreeRTOS (built into ESP-IDF, 1000Hz tick)

**What this project IS:**
A bootable launcher that always starts first on every reset. It shows a 5-second animated splash screen, then either stays in the Launcher or hands off to user firmware (ota_1). While in the Launcher, it provides: OTA firmware updates, a cloud NES game store with emulator, a Lua-scriptable AI assistant with LLM integration, a torch/flashlight app, system settings, and a firmware store for alternate ROMs.

**What this project IS NOT:**
This is NOT an Arduino project. This is NOT a generic ESP32 template. Every pattern in this codebase is custom and intentional. Do not replace it with stock ESP-IDF examples or Arduino APIs.

---

## 2. ARCHITECTURE — LAYER MODEL

```
┌─────────────────────────────────────────────────────────┐
│                     UI Layer (Core 1)                    │
│  ui_task.cpp → IApp interface → App_* classes            │
│  theme_mgr, UI_Keyboard, UI_WifiScanner (overlays)      │
├─────────────────────────────────────────────────────────┤
│                   Services Layer (Core 0)                │
│  wifi_mgr, web_server, ota_service, game_store,         │
│  hack_store, lua_engine, llm_client, idle_mgr, dns_server│
├─────────────────────────────────────────────────────────┤
│                 System Layer                             │
│  sys_mgr (UI queue), boot_ctrl (boot decision)          │
├─────────────────────────────────────────────────────────┤
│                 Manager Layer                            │
│  input_mgr (ISR-driven GPIO buttons)                    │
├─────────────────────────────────────────────────────────┤
│               HAL / Drivers Layer                        │
│  hal_gpio, hal_spi, hal_display, led_driver (WS2812)    │
├─────────────────────────────────────────────────────────┤
│             Bootloader Components                        │
│  boot_gpio_hook.c (custom second-stage hook)            │
├─────────────────────────────────────────────────────────┤
│               External Components                        │
│  nofrendo (NES emulator), lua54, esp_littlefs           │
└─────────────────────────────────────────────────────────┘
```

**Dependency rule:** Upper layers may depend on lower layers. Never the reverse. Services never call UI directly. UI never calls HAL directly. The only exception is `sys_mgr_send_ui_msg()` which sends from services → UI via a FreeRTOS queue.

---

## 3. FILE MAP — EVERY FILE AND ITS ROLE

### Root
| File | Purpose |
|------|---------|
| `CMakeLists.txt` | ESP-IDF project root (name: `st7735_idf_demo`) |
| `platformio.ini` | PlatformIO config: ESP32-S3, 16MB flash, LittleFS, LovyanGFX, icon pipeline |
| `partitions.csv` | nvs, otadata, phy, ota_0 (3MB), ota_1 (3MB), games/LittleFS (4MB) |
| `sdkconfig.defaults` | PSRAM, OTA rollback, bootloader GPIO hook, flash size, optimization |
| `convert_icons.py` | PNG → RGB565 C array converter (pre-build step) |

### `src/platform/`
| File | Purpose |
|------|---------|
| `board.h` | **SINGLE SOURCE OF TRUTH FOR ALL PIN NUMBERS.** Display driver name, dimensions, input GPIOs. Never hardcode pins elsewhere. |

### `src/config/`
| File | Purpose |
|------|---------|
| `sys_config.h` | **ALL compile-time constants.** Wi-Fi, HTTP, OTA, Game Store, AI, NES, UI queue size. Every magic number lives here. |
| `web_ui.h` | OTA firmware upload HTML/CSS/JS (served from flash) |
| `web_remote_ui.h` | Virtual gamepad HTML/CSS/JS |
| `web_chat_ui.h` | AI Chat + Lua IDE + Remote SPA (758 lines — largest single asset) |

### `src/system/`
| File | Purpose |
|------|---------|
| `sys_mgr.c/.h` | UI message queue (`xQueueCreate`). `sys_mgr_send_ui_msg()` for OTA→UI notifications. Queue depth: `APP_UI_QUEUE_SIZE` (10). |
| `boot_ctrl.cpp/.h` | Boot decision: RTC flag check → NVS boot-loop guard → ota_1 validation → 4s animated splash → SELECT poll → ota_0/ota_1 switch. Uses LovyanGFX (C++ only). |

### `src/managers/`
| File | Purpose |
|------|---------|
| `input_mgr.c/.h` | 5 GPIO buttons, ISR-driven with debounce (30ms). Queue-based event delivery. `input_mgr_get_event()` is non-blocking. `input_mgr_inject_event()` for web remote. |

### `src/hal/`
| File | Purpose |
|------|---------|
| `hal_gpio.c/.h` | GPIO input/output, ISR service install, ISR handler add. Thin wrappers over ESP-IDF `driver/gpio.h`. |
| `hal_spi.c/.h` | SPI bus init, device init, transmit. **NOTE: Uses blocking `spi_device_transmit` — acceptable for non-UI paths only.** |
| `hal_display.c/.h` | Backlight PWM via LEDC. `push_rect`/`fill_rect` are stubs — actual display goes through LovyanGFX directly. |

### `src/drivers/`
| File | Purpose |
|------|---------|
| `led_driver.c/.h` | WS2812 NeoPixel on GPIO 48 via RMT. `led_driver_init()`, `led_driver_set_rgb(r,g,b)`, `led_driver_off()`. |

### `src/services/`
| File | Purpose |
|------|---------|
| `wifi_mgr.c/.h` | APSTA mode. AP on 192.168.4.1 (captive portal). STA with NVS credential persistence. Async scan. |
| `web_server.c/.h` | 12 HTTP routes + captive portal 404. Stack: 8KB. Handles OTA upload, virtual gamepad, AI chat IDE, Lua IDE. |
| `ota_service.c/.h` | ESP-IDF OTA lifecycle. Magic byte validation. EMA throughput tracking. Progress via `sys_mgr_send_ui_msg()`. |
| `game_store.c/.h` | LittleFS-backed NES ROM store. Cloud catalog from GitHub. CRC32 validation. Atomic writes (temp→rename). Cooperative abort. Auto-discover untracked ROMs. |
| `hack_store.c/.h` | Firmware catalog from GitHub. OTA to ota_1. RTC flag handoff (`BOOT_ALLOW_OTA1_MAGIC`). NVS tracking of installed firmware ID. |
| `lua_engine.cpp/.h` | Lua 5.4.8 in PSRAM. Watchdog hook (500 instruction interval). Safe libs only. Singleton pattern. Hardware bindings. |
| `lua_bindings_hw.cpp` | GPIO, I2C, SPI (stub), UART, ADC, PWM, FS (sandboxed to `/storage/ai/`), net (HTTP fetch), disp (stub). |
| `llm_client.c/.h` | OpenAI-compatible streaming SSE. Cerebras API (gpt-oss-120b). JSONL history with auto-trim (12KB context). NVS API key. |
| `idle_mgr.c/.h` | 50s idle → 10s countdown → deep sleep. GPIO 16 (SELECT) ext0 wake. |
| `dns_server.c/.h` | Lightweight UDP DNS on port 53. Resolves all to 192.168.4.1. |

### `src/ui/`
| File | Purpose |
|------|---------|
| `ui_task.cpp/.h` | Main UI task on Core 1 at 40fps. Double-buffered PSRAM canvas (160×132). App switching with slide transitions. |
| `theme_mgr.cpp/.h` | 5 themes (Cyberpunk, Light Minimal, Matrix Hacker, Outrun Retro, Deep Space). Smooth RGB lerp. NVS-persisted. Parallax grid wallpaper. |

### `src/ui/apps/`
| File | Purpose |
|------|---------|
| `IApp.h` | **Interface:** `init()`, `exit()`, `loop()`, `render(LGFX_Sprite*)`, `handleInput(input_event_t)`, `isDynamic()`. |
| `App_Grid.cpp/.h` | Main menu: 3×3 icon grid, spring-physics cursor, parallax wallpaper, dot page indicators. Max 12 apps, 9/page. |
| `App_OTA.cpp/.h` | Liquid-fill tank animation, 5-phase dots, KB/s stats, shockwave ring on success/fail. |
| `App_Settings.cpp/.h` | System info, theme carousel, brightness slider. Animated transitions between sub-screens. |
| `App_Torch.cpp/.h` | 8 LED modes with animated orb, 3D glass highlight, mode labels. |
| `App_AICode.cpp/.h` | AI assistant: chat (LLM streaming), Lua IDE, script manager, API key entry. Uses overlay components. |
| `App_AICode_ui.cpp` | Draw functions and action helpers for App_AICode (split to keep files under 300 lines). |
| `App_GameStore.cpp/.h` | NES ROM store: Local list → cloud → detail → download/install/delete. Space warning with delete candidates. |
| `App_NES.cpp/.h` | NES emulator: Double-buffer PSRAM, emulator task on Core 0, mapper validation, SELECT chord system. `isDynamic()=true`. |
| `App_HackStore.cpp/.h` | Firmware store: Download alternate FW to ota_1, boot it. RTC flag handoff. |

### `src/ui/components/`
| File | Purpose |
|------|---------|
| `UI_Keyboard.cpp/.h` | QWERTY overlay (4×10 grid, 3 modes). Callback-based. Reused by App_AICode and UI_WifiScanner. |
| `UI_WifiScanner.cpp/.h` | Wi-Fi scan/list/password/connect overlay. Uses UI_Keyboard for password. 10s timeout. |

### `src/utils/`
| File | Purpose |
|------|---------|
| `pool_allocator.c/.h` | Bitmap-based fixed-size block allocator. **Currently unused — exists for future use.** |
| `dirty_rect.c/.h` | Dirty-rect tracking with overlap merge. **Currently unused — exists for future optimization.** |
| `error_handling.h` | `ESP_ERROR_CHECK_LOG`, `ESP_ERROR_RETURN`, `ESP_ERROR_WARN` macros. |
| `compile_checks.h` | `_Static_assert` wrappers, `safe_memcpy`, `safe_strncpy`. |

### `bootloader_components/boot_gpio_hook/`
| File | Purpose |
|------|---------|
| `boot_gpio_hook.c` | Custom second-stage bootloader hook. **ALWAYS forces ota_0** via direct ota_data flash write. Detects SELECT held → sets `BOOT_ALLOW_OTA1_MAGIC` or `BOOT_FORCE_LAUNCHER_MAGIC` in RTC retain. Exception: if `BOOT_ALLOW_OTA1_MAGIC` is set, skips forcing ota_0. |

### `components/`
| Directory | Purpose |
|-----------|---------|
| `nofrendo/` | NES emulator (6502 CPU, PPU, APU, 65+ MMC mappers). Runs on Core 0. 48KB stack. |
| `lua54/` | Lua 5.4.8 source with custom PSRAM allocator config. |
| `esp_littlefs/` | LittleFS filesystem component (git submodule). |

---

## 4. BOOT SEQUENCE — EXACT ORDER

```
POWER ON / RESET
    │
    ▼
bootloader_after_init()  [boot_gpio_hook.c]
    ├─ GPIO16 (SELECT) debounce
    ├─ Check RTC for BOOT_ALLOW_OTA1_MAGIC
    │   └─ If set → clear flag → let ota_1 boot (user firmware)
    ├─ force_ota_0_boot() → erase ota_data sector 1, write seq=1 to sector 0
    └─ If SELECT held → set BOOT_FORCE_LAUNCHER_MAGIC in RTC retain
    │
    ▼
app_main()  [main.cpp]
    ├─ nvs_flash_init() → erase+reinit on corruption
    ├─ esp_event_loop_create_default()
    ├─ sys_mgr_init() → create UI queue (depth 10)
    ├─ boot_ctrl_check()
    │   ├─ Read RTC flag → if BOOT_FORCE_LAUNCHER_MAGIC → stay in Launcher
    │   ├─ Confirm running from ota_0
    │   ├─ NVS boot-loop guard (max 3 boots before forcing Launcher)
    │   ├─ Validate ota_1 image (esp_ota_get_state or magic byte check)
    │   ├─ Check launcher preference history (skip after 255 consecutive)
    │   ├─ Init display → draw animated splash for 4 seconds
    │   │   └─ Poll GPIO16 every 10ms with 50ms debounce
    │   ├─ SELECT pressed → stay in Launcher
    │   └─ Timeout → esp_ota_set_boot_partition(ota_1) → set BOOT_ALLOW_OTA1_MAGIC → esp_restart()
    │
    ├─ input_mgr_init() → ISR-driven GPIO buttons
    ├─ game_store_init() → mount LittleFS, load catalog, auto-discover ROMs
    ├─ ui_task_init() → spawn UI task on Core 1 (40fps)
    ├─ wifi_mgr_init() → APSTA mode, captive portal DNS, auto-reconnect from NVS
    ├─ hack_store_init() → load installed firmware ID from NVS
    ├─ web_server_init() → 12 HTTP routes + captive portal
    ├─ idle_mgr_init() → 50s idle timeout
    ├─ boot_ctrl_mark_valid() → esp_ota_mark_app_valid_cancel_rollback()
    │
    └─ while(1) { idle_mgr_process(); vTaskDelay(100ms); }
```

---

## 5. NON-NEGOTIABLE CODING RULES

### 5.1 Memory
- **ALL large allocations (>1KB) MUST use PSRAM:** `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`
- **Never use `malloc()` in steady-state.** Pre-allocate at init.
- **Never allocate in ISR context.** ISR handlers may only call `xQueueSendFromISR`, `xTaskNotifyFromISR`, `xSemaphoreGiveFromISR`.
- **FreeRTOS stacks:** Profiled, not guessed. NES emu = 48KB, UI = 8KB, HTTP handlers = 8KB, Lua/LLM workers = 16KB.
- **`alloca()` is forbidden** in any code path that could receive large inputs (Lua bindings, HTTP handlers).

### 5.2 Concurrency
- **ISRs must complete in <50µs.** Defer everything else.
- **`IRAM_ATTR`** on all ISR functions. **`DRAM_ATTR`** on all data accessed from ISRs.
- **Mutexes with timeout** — never `portMAX_DELAY` on application mutexes (RTOS internals are exempt).
- **`volatile`** on all flags shared between tasks and ISRs or between tasks without mutex.
- **Single-word flags** use `_Atomic` or `stdatomic.h` where possible.
- **`vTaskDelay` is forbidden** in render and driver paths. Use `vTaskDelayUntil` for frame timing.
- **Never use `vTaskDelete(external_handle)`** — it leaks buffers and sockets. Tasks must self-delete via `vTaskDelete(NULL)`.

### 5.3 Display
- **LovyanGFX is the ONLY display API.** Never use raw SPI for display operations.
- **Double-buffer:** Canvas (PSRAM sprite) → `pushSprite` to TFT. Never draw directly to TFT.
- **40fps target** via `vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(25))`.
- **PSRAM sprites** for all large allocations: `sprite.setPsram(true)`.
- **Canvas size:** `tft.width()` × `tft.height() + 4` (the +4 is for ST7735 vertical offset compensation).

### 5.4 Error Handling
- **Every ESP-IDF call's return value is checked.** No exceptions.
- **Use `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`** with the module's `TAG` constant.
- **Use `ESP_ERROR_WARN`** for non-fatal errors that should not abort.
- **Use `ESP_ERROR_CHECK`** only for truly unrecoverable init failures.
- **Never use `abort()`** except in `sys_mgr_init` (queue creation failure = system cannot continue).

### 5.5 Input Handling
- **GPIO pins:** Defined in `board.h` ONLY. Never hardcode pin numbers.
- **Button events:** `input_event_t` enum (BTN_NONE, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT).
- **Non-blocking:** `input_mgr_get_event()` returns `BTN_NONE` if no event. Never block on input.
- **Debounce:** 30ms in ISR. Additional app-level debounce if needed.
- **SELECT chord system (NES):** Tap (<200ms) = A button. Hold ≥200ms + direction = Start/Select/Save/Load.

### 5.6 File System
- **LittleFS mount point:** `/storage` (partition label: `games`).
- **ROM directory:** `/storage/roms/`
- **AI directory:** `/storage/ai/` with subdirs `scripts/`.
- **Atomic writes:** Write to `.tmp` file, then `rename()`. Never write directly to the target file.
- **Lua sandbox:** Scripts may ONLY access `/storage/ai/` and subdirectories.

### 5.7 Network
- **AP mode:** SSID = `ESP32_OTA_Launcher`, open (no password), channel 1, max 4 connections.
- **STA mode:** Credentials saved to NVS (`wifi_creds` namespace). Auto-reconnect on boot.
- **Captive portal:** DNS server resolves ALL queries to 192.168.4.1. 404 handler redirects to `/chat`.
- **HTTPS:** Use `esp_crt_bundle_attach` for certificate validation.
- **HTTP timeouts:** 10s for most operations, 30s for LLM streaming.

### 5.8 Naming Conventions
- **C files:** `snake_case.c/.h`
- **C++ classes:** `PascalCase` (e.g., `App_Grid`, `LuaEngine`, `UI_Keyboard`)
- **Member variables:** `_underscore_prefix` (e.g., `_rom_path`, `_emu_task`)
- **Static/file-scope:** `s_` prefix (e.g., `s_tft`, `s_canvas`, `s_input_queue`)
- **Constants:** `ALL_CAPS` in `#define`, `ALL_CAPS` or `PascalCase` in enums
- **Tags:** `static const char* TAG = "MODULE_NAME";` at top of every .c/.cpp file
- **Error enums:** `MODULE_ERR_OK`, `MODULE_ERR_SPECIFIC` (e.g., `GAME_STORE_ERR_NO_WIFI`)

### 5.9 Code Organization
- **Max 300 lines per file.** Split large files (see `App_AICode.cpp` + `App_AICode_ui.cpp`).
- **No `#define` for constants** — use `enum` or `static const` in C++, `#define` only in `sys_config.h`.
- **No placeholder code** — `// TODO: implement` is forbidden. Either implement it or remove it.
- **No comments unless explaining WHY, not WHAT.** The code should be self-documenting.
- **`const`-correct everything.** `static` on all file-scope symbols not in the public header.
- **Fixed-width types** in driver/HAL code: `uint8_t`, `int32_t`, `uint16_t`. Never bare `int`.

---

## 6. HOW TO ADD A NEW APP

### Step 1: Create the app class

```cpp
// src/ui/apps/App_MyNewApp.h
#pragma once
#include "IApp.h"

class App_MyNewApp : public IApp {
private:
    float _anim_time;
    // ... your state variables

public:
    App_MyNewApp();
    ~App_MyNewApp() override;

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
    
    // Only override if this app is heap-allocated (like App_NES)
    // bool isDynamic() const override { return true; }
};
```

### Step 2: Create the implementation

```cpp
// src/ui/apps/App_MyNewApp.cpp
#include "App_MyNewApp.h"
#include "../theme_mgr.h"
#include "esp_log.h"

extern void switch_to_grid();  // Always declare this

static const char* TAG = "APP_MYNEWAPP";

App_MyNewApp::App_MyNewApp() : _anim_time(0) {}
App_MyNewApp::~App_MyNewApp() {}

void App_MyNewApp::init() {
    _anim_time = 0;
    // Initialize your resources here
}

void App_MyNewApp::exit() {
    // Clean up resources here
    // Do NOT call vTaskDelete on other tasks
}

void App_MyNewApp::loop() {
    _anim_time += 0.05f;  // ~40fps increment
    // Update animations, poll async operations
}

void App_MyNewApp::handleInput(input_event_t event) {
    if (event == BTN_LEFT) {
        switch_to_grid();  // Always provide a back navigation
        return;
    }
    // Handle other buttons
}

void App_MyNewApp::render(LGFX_Sprite* canvas) {
    // ALWAYS fill background first
    canvas->fillSprite(theme_get_bg());
    
    // Use theme colors, never hardcoded values
    uint16_t accent = theme_get_accent();
    uint16_t text = theme_get_text();
    uint16_t dim = theme_get_text_dim();
    
    // Draw your UI here
    // Use canvas->setFont(&fonts::Font0) for 6x8 text
    // Use canvas->setTextDatum() before drawString()
    
    // Footer hint
    canvas->setTextColor(dim);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("L=Back", canvas->width()/2, canvas->height()-2);
}
```

### Step 3: Register in ui_task.cpp

```cpp
// In ui_task.cpp, add:
#include "apps/App_MyNewApp.h"

// Add static instance (before ui_task_runner):
static App_MyNewApp app_mynewapp;

// In ui_task_runner(), after other registerApp calls:
app_grid.registerApp("My New App", icon_mynewapp, &app_mynewapp);
```

### Step 4: Create the icon

Place a 64×64 PNG in `icons/`. Run `python convert_icons.py` to generate `src/assets/icons/mynewapp.h`. The icon array will be `static const uint16_t icon_mynewapp[4096]`.

### Step 5: Add to CMakeLists.txt (if new .cpp file)

The `src/CMakeLists.txt` uses `FILE(GLOB_RECURSE app_sources "*.c" "*.cpp")` — new files are auto-discovered. No manual editing needed.

---

## 7. HOW TO ADD A NEW SERVICE

### Pattern: Service with async task + status polling

```c
// src/services/my_service.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MY_SVC_ERR_OK = 0,
    MY_SVC_ERR_NO_WIFI,
    MY_SVC_ERR_NETWORK,
    MY_SVC_ERR_FS_ERROR,
    MY_SVC_ERR_ABORTED,
} my_service_err_t;

typedef struct {
    bool is_busy;
    my_service_err_t last_error;
    uint8_t progress_pct;
    // ... additional status fields
} my_service_status_t;

my_service_err_t my_service_init(void);
my_service_err_t my_service_start_operation(const char* param);
my_service_err_t my_service_abort(void);
void my_service_get_status(my_service_status_t* out);

#ifdef __cplusplus
}
#endif
```

```c
// src/services/my_service.c
#include "my_service.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

static const char* TAG = "MyService";

static my_service_status_t s_status;
static SemaphoreHandle_t s_mutex = NULL;
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_task_handle = NULL;
static volatile bool s_abort_requested = false;

#define EVT_DONE (1 << 0)
#define EVT_FAIL (1 << 1)

static void _lock(void) { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void _unlock(void) { if (s_mutex) xSemaphoreGive(s_mutex); }

static void _set_busy(bool busy) { _lock(); s_status.is_busy = busy; _unlock(); }
static void _set_error(my_service_err_t err) { _lock(); s_status.last_error = err; _unlock(); }

my_service_err_t my_service_init(void) {
    memset(&s_status, 0, sizeof(s_status));
    s_mutex = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    configASSERT(s_mutex && s_events);
    return MY_SVC_ERR_OK;
}

static void _worker_task(void* arg) {
    const char* param = (const char*)arg;
    
    if (!wifi_mgr_is_connected()) {
        _set_error(MY_SVC_ERR_NO_WIFI);
        goto done;
    }
    
    // Do work here, polling s_abort_requested in loops
    // if (s_abort_requested) { _set_error(MY_SVC_ERR_ABORTED); break; }
    
    xEventGroupSetBits(s_events, EVT_DONE);
    
done:
    _set_busy(false);
    s_abort_requested = false;
    s_task_handle = NULL;
    vTaskDelete(NULL);  // ALWAYS self-delete
}

my_service_err_t my_service_start_operation(const char* param) {
    if (s_status.is_busy) return MY_SVC_ERR_ABORTED;
    
    xEventGroupClearBits(s_events, EVT_DONE | EVT_FAIL);
    s_abort_requested = false;
    _set_busy(true);
    _set_error(MY_SVC_ERR_OK);
    
    BaseType_t rc = xTaskCreatePinnedToCore(
        _worker_task, "my_svc", 8192, (void*)param, 5, &s_task_handle, 0);
    
    if (rc != pdPASS) {
        s_task_handle = NULL;
        _set_busy(false);
        return MY_SVC_ERR_NETWORK;
    }
    return MY_SVC_ERR_OK;  // Non-blocking
}

my_service_err_t my_service_abort(void) {
    if (!s_status.is_busy || !s_task_handle) return MY_SVC_ERR_OK;
    s_abort_requested = true;
    // Wait up to 600ms for task to self-terminate
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (s_task_handle == NULL) return MY_SVC_ERR_OK;
    }
    return MY_SVC_ERR_ABORTED;
}

void my_service_get_status(my_service_status_t* out) {
    _lock();
    *out = s_status;
    _unlock();
}
```

### Key patterns:
1. **Mutex** on all shared state
2. **Event group** for async completion signaling
3. **`volatile bool s_abort_requested`** for cooperative cancellation
4. **Task self-deletes** via `vTaskDelete(NULL)` — never external delete
5. **Non-blocking public API** — UI polls `get_status()`
6. **Core 0** for network tasks, **Core 1** for UI tasks

---

## 8. HOW TO ADD A NEW WEB ENDPOINT

```c
// In web_server.c, add to the routes[] array:

static esp_err_t my_endpoint_handler(httpd_req_t *req) {
    // Parse query string if needed
    char query[256] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[64];
        if (httpd_query_key_value(query, "key", param, sizeof(param)) == ESP_OK) {
            // Use param
        }
    }
    
    // Build response
    char json[256];
    snprintf(json, sizeof(json), "{\"ok\":true,\"data\":\"...\"}");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// Register in web_server_init():
static const httpd_uri_t routes[] = {
    // ... existing routes ...
    { .uri = "/my-endpoint", .method = HTTP_GET, .handler = my_endpoint_handler },
};
```

### Rules for web endpoints:
- **Stack:** Handler runs on the HTTP server task (8KB stack). Keep local buffers small.
- **PSRAM:** Use `malloc` for buffers >1KB, they'll go to PSRAM automatically.
- **CORS:** Always set `Access-Control-Allow-Origin: *`.
- **Caching:** Always set `Cache-Control: no-store` for dynamic endpoints.
- **JSON:** Use `snprintf` to build JSON. No external JSON library needed for simple responses.
- **SSE streaming:** Use `httpd_resp_send_chunk` for streaming (see `chat_send_get_handler`).

---

## 9. HOW TO ADD A NEW LUA HARDWARE BINDING

```cpp
// In lua_bindings_hw.cpp, add:

static int l_my peripheral_read(lua_State* L) {
    int pin = luaL_checkinteger(L, 1);
    if (!_pin_allowed(pin)) { lua_pushnil(L); return 1; }
    
    // Do hardware operation
    int value = /* read from hardware */;
    
    lua_pushinteger(L, value);
    return 1;
}

void LuaEngine::_bind_my_peripheral(lua_State* L) {
    static const luaL_Reg m[] = {
        {"read", l_my_peripheral_read},
        {"write", l_my_peripheral_write},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m);
    lua_setglobal(L, "my_peripheral");
}

// Register in _register_bindings():
void LuaEngine::_register_bindings() {
    // ... existing bindings ...
    _bind_my_peripheral(m_L);
}
```

### Rules for Lua bindings:
- **Check `_pin_allowed()`** for any GPIO operation — flash pins (6-13) are blocked.
- **Use `alloca()` only for small, bounded reads** (<128 bytes). For larger reads, use PSRAM.
- **Never block for long** — Lua has a 5s watchdog timeout.
- **Sandbox filesystem access** to `/storage/ai/` only.
- **Lua scripts run on Core 0** (AI_WORKER_TASK_CORE).

---

## 10. HOW TO DEBUG

### Serial Monitor
```bash
pio device monitor --filter esp32_exception_decoder --filter colorize --filter time
```

### Key Log Tags
| Tag | Module |
|-----|--------|
| `MAIN` | Application entry point |
| `BOOT_CTRL` | Boot decision controller |
| `SYS_MGR` | System manager, UI queue |
| `INPUT_MGR` | GPIO button input |
| `UI_TASK` | Main UI render loop |
| `WIFI_MGR` | Wi-Fi manager |
| `WEB_SRV` | HTTP server |
| `OTA_SVC` | OTA firmware update |
| `GameStore` | NES ROM store |
| `HackStore` | Firmware store |
| `LuaEngine` | Lua runtime |
| `LuaBind` | Lua hardware bindings |
| `LLM` | Cloud LLM client |
| `IDLE_MGR` | Deep sleep manager |
| `DNS_SRV` | Captive portal DNS |
| `LED_DRIVER` | WS2812 NeoPixel |

### Common Debug Scenarios

**OTA upload fails:**
1. Check serial for `OTA_SVC` tag — look for magic byte mismatch
2. Verify file is raw `.bin` (not `.elf`, `.zip`, `.hex`)
3. Check `Content-Length` matches actual file size
4. Verify partition table has ota_0/ota_1 with enough space

**NES emulator crashes:**
1. Check `App_NES` tag — look for mapper unsupported error
2. Verify ROM is valid iNES format (magic: `NES\x1A`)
3. Check stack size — 48KB is minimum safe value
4. Look for `min free stack` warning in serial output

**Wi-Fi won't connect:**
1. Check `WIFI_MGR` tag for event logs
2. Verify SSID/password in NVS (`wifi_creds` namespace)
3. Check if AP is running (`WIFI_MODE_APSTA`)
4. Verify captive portal DNS is resolving

**Lua script crashes:**
1. Check `LuaEngine` tag for compile/runtime errors
2. Script timeout after 5s — check for infinite loops
3. Verify script doesn't access blocked GPIO pins
4. Check filesystem sandbox — scripts can only access `/storage/ai/`

**Display not working:**
1. Verify SPI pins in `LGFX_Config.h` match your hardware
2. Check LovyanGFX initialization in `ui_task.cpp`
3. Verify backlight PWM on GPIO 4
4. Check for `tft.init()` failure in serial

### Memory Debug
```c
// Add to any loop to monitor memory:
ESP_LOGI(TAG, "Free heap: %lu KB, Free PSRAM: %lu KB",
         (unsigned long)(esp_get_free_heap_size() / 1024),
         (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
```

### Stack Debug
```c
// Check minimum free stack for any task:
ESP_LOGI(TAG, "Min free stack: %u bytes",
         (unsigned)uxTaskGetStackHighWaterMark(NULL));
```

---

## 11. PARTITION TABLE

```
# Name,   Type, SubType, Offset,   Size,     Notes
nvs,      data, nvs,     ,         0x4000,   16KB — Wi-Fi cal, NVS keys
otadata,  data, ota,     ,         0x2000,   8KB  — OTA state (2 sectors)
phy_init, data, phy,     ,         0x1000,   4KB  — PHY calibration
ota_0,    app,  ota_0,   0x10000,  0x300000, 3MB  — Launcher (always boots first)
ota_1,    app,  ota_1,   0x310000, 0x300000, 3MB  — User firmware / Hack Store target
games,    data, littlefs, 0x610000, 0x400000, 4MB  — ROMs, AI scripts, catalogs
```

---

## 12. NVS NAMESPACES

| Namespace | Keys | Purpose |
|-----------|------|---------|
| `ui_cfg` | `brightness` (u8), `theme` (u8) | UI preferences |
| `wifi_creds` | `ssid` (str), `pass` (str) | Saved Wi-Fi credentials |
| `boot_ctrl` | `boot_cnt` (u8), `pref_cnt` (u8) | Boot-loop guard, launcher preference |
| `ai_cfg` | `api_key` (str), `provider` (str) | LLM API key and provider |
| `hack_store` | `installed_id` (str) | Currently installed firmware ID |

---

## 13. EXTERNAL DEPENDENCIES

| Dependency | Version | Purpose |
|------------|---------|---------|
| LovyanGFX | ^1.1.12 | Display driver, sprites, DMA |
| nofrendo | (bundled) | NES emulator |
| Lua | 5.4.8 (bundled) | Scripting runtime |
| esp_littlefs | (submodule) | LittleFS filesystem |
| cJSON | (ESP-IDF built-in) | JSON parsing |
| esp_http_client | (ESP-IDF built-in) | HTTP client |
| esp_crt_bundle | (ESP-IDF built-in) | TLS certificate bundle |
| led_strip | (ESP-IDF built-in) | WS2812 NeoPixel driver |

---

## 14. BUILD COMMANDS

```bash
# Build
pio run

# Build + Flash
pio run -t upload

# Build + Flash + Monitor
pio run -t upload && pio device monitor

# Flash LittleFS data partition
pio run -t uploadfs

# Build + Flash all (firmware + LittleFS)
./flash_all.sh

# Clean build
pio run -t clean

# Update icon assets
python convert_icons.py
```

---

## 15. CRITICAL INvariants — NEVER BREAK THESE

1. **Launcher ALWAYS boots first.** The bootloader hook forces ota_0 on every reset. Only the Launcher can decide to hand off to ota_1.

2. **OTA rollback is enabled.** If the new firmware crashes before `boot_ctrl_mark_valid()`, the bootloader reverts to the previous good partition.

3. **The UI task NEVER blocks.** All network operations run on Core 0 tasks. The UI task on Core 1 only polls status flags and renders.

4. **NES emulator has 48KB stack.** Reducing this causes stack overflow crashes with MMC3 mappers and PPU operations.

5. **Lua scripts are sandboxed.** They can only access `/storage/ai/` filesystem and non-flash GPIO pins. They have a 5-second execution timeout.

6. **Web server has no authentication.** Anyone on the AP can upload firmware. This is by design for development — add auth before production deployment.

7. **The `+4` height on canvas sprites** compensates for ST7735 vertical offset. Never remove it without testing on actual hardware.

8. **`boot_ctrl.cpp` is C++ only** because it uses LovyanGFX for the splash screen. The header uses `extern "C"` for C compatibility.

9. **App_NES is heap-allocated** (`isDynamic()=true`). It is the ONLY app that uses this pattern. `switch_to_app()` deletes the old dynamic app AFTER the transition completes.

10. **The icon pipeline runs before every build** via `extra_scripts = pre:convert_icons.py`. Icons are 64×64 PNG → 64×64 RGB565 C arrays → downsampled to 32×32 at runtime by App_Grid.

---

## 16. THINGS THAT WILL GET YOUR CODE REJECTED

- Using `vTaskDelay()` in render or driver paths
- Using `malloc()` in ISR context
- Using `spi_device_transmit()` (blocking) in UI thread
- Hardcoding GPIO pin numbers instead of using `board.h`
- Adding `// TODO` comments instead of implementing
- Creating files >300 lines without architectural justification
- Using bare `int` in driver/HAL code (use `uint8_t`, `int32_t`, etc.)
- Using `#define` for constants outside of `sys_config.h`
- Not checking ESP-IDF return values
- Using `portMAX_DELAY` on application mutexes
- Calling `vTaskDelete()` on another task's handle
- Adding comments that describe WHAT the code does instead of WHY
- Using Arduino APIs (`digitalWrite`, `analogRead`, etc.)
- Using `new`/`delete` without PSRAM awareness
- Not providing a back navigation (BTN_LEFT → `switch_to_grid()`) in apps
- Not using theme colors (`theme_get_*()`) in UI rendering
- Not filling the background before rendering in `render()`
- Allocating large buffers on the stack (>512 bytes)

---

## 17. PERFORMANCE BUDGETS

| Metric | Budget | Measurement |
|--------|--------|-------------|
| UI frame time | 25ms (40fps) | `vTaskDelayUntil` in `ui_task_runner` |
| ISR execution | <50µs | Profiling with GPIO toggle |
| Boot splash | 4000ms | `BC_SPLASH_DURATION_MS` in `boot_ctrl.cpp` |
| OTA upload | 3MB max | `APP_OTA_MAX_IMAGE_SIZE` in `sys_config.h` |
| Lua script | 5000ms max | `AI_LUA_TIMEOUT_MS` in `sys_config.h` |
| LLM response | 30s timeout | `AI_HTTP_RX_TIMEOUT_MS` in `sys_config.h` |
| Wi-Fi scan | 10s timeout | `APP_WIFI_SCAN_TIMEOUT_MS` in `sys_config.h` |
| Deep sleep idle | 50s + 10s countdown | `idle_mgr.c` constants |
| NES emulator stack | 48KB | `NES_EMU_TASK_STACK` in `sys_config.h` |
| Lua worker stack | 16KB | `AI_WORKER_TASK_STACK` in `sys_config.h` |

---

## 18. SECURITY CONSIDERATIONS

- **Web server has no auth.** Anyone on the AP can flash firmware, run Lua scripts, or access the AI chat. This is intentional for development.
- **API key stored in NVS** (plaintext). Not encrypted. Consider `CONFIG_NVS_ENCRYPTION=y` for production.
- **Lua filesystem sandbox** restricts to `/storage/ai/`. Enforced in `lua_bindings_hw.cpp:257-259`.
- **Lua GPIO blacklist** blocks flash pins (6-13). Enforced in `lua_bindings_hw.cpp:34-39`.
- **Lua `net.fetch`** has no URL restriction. Scripts can call any HTTP endpoint.
- **OTA magic byte validation** prevents accidental uploads of non-firmware files.
- **Boot-loop guard** prevents bricking from bad firmware (max 3 boots before forcing Launcher).

---

*This document is the authority. If any code contradicts this document, this document is correct until explicitly updated.*
