/*
 * sys_config.h — Centralised System Configuration
 * ================================================
 * Purpose    : Single source of truth for all compile-time parameters.
 * Scope      : Wi-Fi, HTTP, OTA, UI, power management constants.
 * Dependants : All layers — keep additions here, not buried in .c files.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Wi-Fi Access Point / Station
 * -------------------------------------------------------------------------- */
#define APP_WIFI_SSID           "ESP32_OTA_Launcher"
#define APP_WIFI_PASS           ""          /* Open AP — add password for security */
#define APP_WIFI_CHANNEL        1
#define APP_WIFI_MAX_CONN       4

/* Station Mode Config */
#define APP_WIFI_SCAN_TIMEOUT_MS    10000
#define APP_WIFI_CONN_TIMEOUT_MS    15000
#define MAX_WIFI_NETWORKS           12

/* --------------------------------------------------------------------------
 * HTTP Server
 * -------------------------------------------------------------------------- */
#define APP_HTTP_PORT           80

/* Maximum simultaneous HTTP connections (1 uploader + 2 pollers) */
#define APP_HTTP_MAX_SOCKETS    5

/* Recv buffer for OTA upload chunks (bytes). Larger = fewer flash-write calls.
 * Must fit in internal RAM alongside RTOS stacks. 8 KB is safe on ESP32-S3. */
#define APP_OTA_RECV_BUF_SIZE   (8 * 1024)

/* Minimum valid firmware image size in bytes.
 * A real ESP32 app is always > 64 KB. Reject anything smaller. */
#define APP_OTA_MIN_IMAGE_SIZE  (64 * 1024)

/* Maximum OTA image size — matches ota_0/ota_1 partition sizes (4 MB). */
#define APP_OTA_MAX_IMAGE_SIZE  (4 * 1024 * 1024)

/* Delay in ms after sending "success" HTTP response before esp_restart().
 * Gives the browser enough time to receive and display the success page. */
#define APP_OTA_REBOOT_DELAY_MS 2500

/* ESP32 firmware binary magic byte — same for ESP-IDF, Arduino, PlatformIO. */
#define APP_OTA_ESP32_MAGIC     0xE9u

/* --------------------------------------------------------------------------
 * Boot Control — RTC Retain Memory Magic Values
 * -------------------------------------------------------------------------- */
#define BOOT_FORCE_LAUNCHER_MAGIC  0xA5B4C3D2U
#define BOOT_ALLOW_OTA1_MAGIC      0xB6C7D4E3U

/* --------------------------------------------------------------------------
 * Game Store — Cloud ROM Downloader
 * -------------------------------------------------------------------------- */
#define GAME_STORE_PARTITION_LABEL  "games"
#define GAME_STORE_MOUNT_POINT      "/storage"
#define GAME_STORE_ROMS_DIR         "/storage/roms"
#define GAME_STORE_CATALOG_LOCAL    "/storage/catalog.json"
#define GAME_STORE_CLOUD_CATALOG_URL \
    "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/games_catalog.json"

/* Maximum number of games that can be stored locally. */
#define GAME_STORE_MAX_GAMES        15

/* Download chunk buffer size (in PSRAM). Larger = fewer fwrite() calls. */
#define GAME_STORE_DL_CHUNK_SIZE    (4 * 1024)

/* HTTP timeout in ms for catalog fetch and ROM download. */
#define GAME_STORE_HTTP_TIMEOUT_MS  10000

/* Minimum valid NES ROM size (INES header = 16 bytes, smallest real ROM = 8KB). */
#define GAME_STORE_ROM_MIN_SIZE     (8 * 1024)

/* Maximum ROM size we will accept (512KB). */
#define GAME_STORE_ROM_MAX_SIZE     (512 * 1024)

/* Magic bytes for INES ROM validation: 'NES\x1A' */
#define GAME_STORE_INES_MAGIC       {0x4E, 0x45, 0x53, 0x1A}

/* NES emulator task — must be generous; nofrendo PPU/CPU use deep call stacks.
 * 12KB causes guaranteed stack overflow. 48KB is safe with MMC3 + PPU. */
#define NES_EMU_TASK_STACK          (48 * 1024)
#define NES_EMU_TASK_PRIORITY       5
#define NES_EMU_TASK_CORE           0
/* Warn in serial log if free stack drops below this during emulation */
#define NES_EMU_TASK_STACK_WARN_KB  4

/* --------------------------------------------------------------------------
 * UI
 * -------------------------------------------------------------------------- */
/* Depth of the inter-task UI message queue (OTA → UI task). */
#define APP_UI_QUEUE_SIZE       10

/* --------------------------------------------------------------------------
 * NTP Time Service
 * -------------------------------------------------------------------------- */
#define NTP_SERVER_1                "pool.ntp.org"
#define NTP_SERVER_2                "time.google.com"
#define NTP_SERVER_3                "time.windows.com"
#define NTP_SYNC_INTERVAL_S         3600        /* Re-sync every 1 hour */
#define NTP_SAVE_INTERVAL_S         60          /* Save to NVS every 60s */
#define NTP_NVS_NAMESPACE           "ntp_time"
#define NTP_NVS_KEY_EPOCH           "epoch"
#define NTP_NVS_KEY_TIMER_US        "timer_us"

/* Sanity bounds for NVS-restored time validation */
#define NTP_EPOCH_MIN   946684800ULL   /* 2000-01-01 00:00:00 UTC — below this is invalid */
#define NTP_EPOCH_MAX  1893456000ULL   /* 2030-01-01 00:00:00 UTC — above this is invalid */
#define NTP_BUILD_MIN  1717200000ULL   /* Approx 2024-06-01 — before firmware build date */

/* --------------------------------------------------------------------------
 * Weather Service
 * -------------------------------------------------------------------------- */
#define WEATHER_HTTP_TIMEOUT_MS     10000
#define WEATHER_REFRESH_INTERVAL_S  1800        /* 30 minutes */
#define WEATHER_STALE_THRESHOLD_S   7200        /* 2 hours */
#define WEATHER_CONFIG_PATH         "/storage/ai/clock_weather.json"
#define WEATHER_IP_GEO_URL          "http://ip-api.com/json/?fields=status,city,lat,lon,timezone,offset,country"
#define WEATHER_FORECAST_URL        "https://api.open-meteo.com/v1/forecast"
#define WEATHER_GEOCODING_URL       "https://geocoding-api.open-meteo.com/v1/search"
#define WEATHER_REVERSE_GEO_URL     "https://nominatim.openstreetmap.org/reverse"
#define WEATHER_TASK_STACK          8192
#define WEATHER_TASK_PRIORITY       5
#define WEATHER_TASK_CORE           0

/* --------------------------------------------------------------------------
 * AI Assistant — Lua Engine, LLM Client, IDE
 * -------------------------------------------------------------------------- */

/* LittleFS directory layout */
#define AI_BASE_DIR             "/storage/ai"
#define AI_SCRIPTS_DIR          "/storage/ai/scripts"
#define AI_CONFIG_PATH          "/storage/ai/config.json"
#define AI_HISTORY_PATH         "/storage/ai/history.jsonl"
#define AI_SYSPROMPT_PATH       "/storage/ai/sys_prompt.txt"

/* NVS namespace for sensitive config (API keys stored separately from LittleFS) */
#define AI_NVS_NAMESPACE        "ai_cfg"
#define AI_NVS_KEY_APIKEY       "api_key"
#define AI_NVS_KEY_PROVIDER     "provider"

/* LLM API defaults (overridden by NVS at runtime) */
#define AI_LLM_DEFAULT_PROVIDER "cerebras"
#define AI_LLM_OPENAI_URL       "https://api.cerebras.ai/v1/chat/completions"
#define AI_LLM_MODEL_DEFAULT    "gpt-oss-120b"
#define AI_LLM_DEFAULT_API_KEY  "csk-mj8mx8wjdpdmknwnpymhwtmv9dct5pk9dxhtdnj3e6re6fc6"
#define AI_LLM_MAX_TOKENS       1024
#define AI_LLM_TEMPERATURE      0.7f

/* Token budget: trim history before exceeding this to avoid context overflow */
#define AI_CONTEXT_MAX_CHARS    12000   /* ~3 000 tokens at 4 chars/token avg  */

/* HTTP timeouts for LLM calls */
#define AI_HTTP_CONNECT_TIMEOUT_MS  10000
#define AI_HTTP_RX_TIMEOUT_MS       30000

/* Streaming response chunk buffer in PSRAM */
#define AI_STREAM_BUF_SIZE      (4 * 1024)

/* Lua runtime safety */
#define AI_LUA_TIMEOUT_MS       60000  /* Script force-aborted after 60 s       */
#define AI_LUA_HOOK_INTERVAL    500    /* Instruction-count hook granularity    */
#define AI_LUA_MAX_SCRIPT_BYTES (16 * 1024) /* Max .lua source size from LittleFS */

/* FreeRTOS worker task (runs on Core 0, same as networking + NES emu) */
#define AI_WORKER_TASK_STACK    (16 * 1024)
#define AI_WORKER_TASK_PRIORITY 4
#define AI_WORKER_TASK_CORE     0

/* --------------------------------------------------------------------------
 * Display Utilities
 * -------------------------------------------------------------------------- */
/* Named color constant for use as a dark overlay blend target — never use raw 0x0000. */
#define RGB565_BLACK  (0x0000u)

/* --------------------------------------------------------------------------
 * Hack Store — Custom Firmware Flasher
 * -------------------------------------------------------------------------- */
/* Delay (ms) between setting the OTA-1 RTC flag and calling esp_restart().
 * Ensures the RTOS can cleanly finish any in-flight socket operations. */
#define HS_PRE_REBOOT_DELAY_MS      100

/* Wi-Fi connect timeout (loop ticks in UI_WifiScanner at ~50 ms/tick). */
#define WIFI_SCANNER_CONNECT_TIMEOUT_MS  10000

/* Hack Store cloud catalog URL */
#define HS_CATALOG_URL \
    "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/firmware_catalog.json"

/* HTTP timeout and chunk size for Hack Store downloads */
#define HS_HTTP_TIMEOUT_MS          10000
#define HS_DL_CHUNK_SIZE            8192

/* Catalog fetch buffer size (16 KB in PSRAM) */
#define HS_CATALOG_BUFFER_SIZE      (16 * 1024)

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
#define LUA_TOOL_MAX_SIZE        (16 * 1024)

/* --------------------------------------------------------------------------
 * Input Manager — Button Debounce
 * -------------------------------------------------------------------------- */
/* Depth of the ISR→task button event queue (input_event_t elements). */
#define INPUT_QUEUE_SIZE            10

/* Suppress re-triggers within this window after an accepted ISR edge.
 * Contact bounce fires multiple edges per press; 30ms filters them. */
#define INPUT_DEBOUNCE_MS           30

/* Minimum interval between queued events for the same button — prevents a
 * single physical press from producing repeated events. */
#define INPUT_MIN_PRESS_MS          200

/* --------------------------------------------------------------------------
 * OTA / WiFi State Machine
 * -------------------------------------------------------------------------- */
typedef enum {
    OTA_STATE_IDLE        = 0,  /* Waiting for upload                        */
    OTA_STATE_CONNECTED   = 1,  /* Client connected, not yet uploading        */
    OTA_STATE_FLASHING    = 2,  /* Writing chunks to flash                   */
    OTA_STATE_VALIDATING  = 3,  /* esp_ota_end() verifying image integrity   */
    OTA_STATE_SUCCESS     = 4,  /* Boot partition updated, pending reboot    */
    OTA_STATE_FAILED      = 5,  /* Unrecoverable error                       */
    WIFI_STATE_SCANNING   = 6,  /* Scanning for networks                     */
    WIFI_STATE_CONNECTING = 7,  /* Connecting to AP                          */
    WIFI_STATE_SUCCESS    = 8,  /* Connected as STA                          */
    WIFI_STATE_DISCONNECTED = 9 /* Connection lost/failed                    */
} ota_state_t;

/* --------------------------------------------------------------------------
 * UI Message (inter-task, queue element)
 * -------------------------------------------------------------------------- */
typedef struct {
    ota_state_t state;
    float       progress;           /* 0–100 */
    float       kbps;               /* Transfer rate in KB/s, 0 if unknown */
    char        msg[64];
    uint16_t    rssi;               /* For WiFi status */
    char        ip_addr[16];        /* For WiFi status */
} ui_message_t;

/* Compile-time size validation */
#ifdef __cplusplus
static_assert(sizeof(ui_message_t) <= 128, "ui_message_t too large for queue");
#else
_Static_assert(sizeof(ui_message_t) <= 128, "ui_message_t too large for queue");
_Static_assert(sizeof(((ui_message_t*)0)->msg) >= 64, "msg buffer too small");
_Static_assert(sizeof(((ui_message_t*)0)->ip_addr) >= 16, "ip_addr buffer too small");
#endif

#ifdef __cplusplus
}
#endif
