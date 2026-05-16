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

/* Maximum OTA image size — matches ota_0/ota_1 partition sizes (3 MB). */
#define APP_OTA_MAX_IMAGE_SIZE  (3 * 1024 * 1024)

/* Delay in ms after sending "success" HTTP response before esp_restart().
 * Gives the browser enough time to receive and display the success page. */
#define APP_OTA_REBOOT_DELAY_MS 2500

/* ESP32 firmware binary magic byte — same for ESP-IDF, Arduino, PlatformIO. */
#define APP_OTA_ESP32_MAGIC     0xE9u

/* --------------------------------------------------------------------------
 * UI
 * -------------------------------------------------------------------------- */
/* Depth of the inter-task UI message queue (OTA → UI task). */
#define APP_UI_QUEUE_SIZE       10

/* --------------------------------------------------------------------------
 * OTA / WiFi State Machine
 * -------------------------------------------------------------------------- */
typedef enum {
    OTA_STATE_IDLE        = 0,  /* Waiting for upload */
    OTA_STATE_CONNECTED   = 1,  /* Client connected, not yet uploading */
    OTA_STATE_VALIDATING  = 2,  /* Checking binary header */
    OTA_STATE_FLASHING    = 3,  /* Writing to flash */
    OTA_STATE_SUCCESS     = 4,  /* Complete, pending reboot */
    OTA_STATE_FAILED      = 5,  /* Unrecoverable error */
    WIFI_STATE_SCANNING   = 6,  /* Scanning for networks */
    WIFI_STATE_CONNECTING = 7,  /* Connecting to AP */
    WIFI_STATE_SUCCESS    = 8,  /* Connected as STA */
    WIFI_STATE_DISCONNECTED = 9 /* Connection lost/failed */
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

#ifdef __cplusplus
}
#endif
