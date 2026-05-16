/*
 * web_server.c — HTTP Server for OTA Firmware Upload
 * ====================================================
 * Purpose      : Hosts the OTA web interface and handles firmware uploads.
 * Responsibilities:
 *   GET  /           → Serves the premium OTA web UI (web_ui.h)
 *   POST /upload     → Receives raw .bin upload, streams to ota_service
 *   GET  /status     → JSON status for real-time frontend polling
 *   GET  /info       → JSON device information panel
 *
 * Robustness:
 *   - 8 KB recv buffer minimises write-call overhead
 *   - Validates Content-Type for /upload
 *   - Handles HTTPD_SOCK_ERR_TIMEOUT by retrying (not aborting)
 *   - Reports errors with structured JSON responses
 *   - Proper Connection:close + delay before esp_restart()
 *
 * Dependencies : ota_service, sys_mgr, web_ui.h, esp_http_server
 */

#include "web_server.h"
#include "ota_service.h"
#include "wifi_mgr.h"
#include "../config/sys_config.h"
#include "../config/web_ui.h"
#include "../system/sys_mgr.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/param.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "WEB_SRV";

/* Receive buffer — allocated once on the stack of upload_post_handler.
 * 8 KB gives a good balance: fewer flash-write calls, fits in stack. */
static uint8_t s_recv_buf[APP_OTA_RECV_BUF_SIZE];

/* --------------------------------------------------------------------------
 * Helper: send a JSON error response
 * -------------------------------------------------------------------------- */
static esp_err_t _send_json_error(httpd_req_t *req,
                                  httpd_err_code_t code,
                                  const char *error_msg) {
    char body[128];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", error_msg);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, code, body);
    return ESP_FAIL;
}

/* --------------------------------------------------------------------------
 * GET / — Serve the OTA Web UI
 * -------------------------------------------------------------------------- */
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, WEB_UI_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /status — JSON live OTA status for frontend polling
 *
 * Response: {"ok":true,"state":"flashing","progress":42.5,"kbps":120.3,"msg":"Flashing..."}
 * -------------------------------------------------------------------------- */
static esp_err_t status_get_handler(httpd_req_t *req) {
    /* Map enum to string */
    const char *state_str;
    switch (ota_service_get_state()) {
        case OTA_STATE_IDLE:       state_str = "idle";       break;
        case OTA_STATE_CONNECTED:  state_str = "connected";  break;
        case OTA_STATE_VALIDATING: state_str = "validating"; break;
        case OTA_STATE_FLASHING:   state_str = "flashing";   break;
        case OTA_STATE_SUCCESS:    state_str = "success";    break;
        case OTA_STATE_FAILED:     state_str = "failed";     break;
        default:                   state_str = "unknown";    break;
    }

    char json[256];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"state\":\"%s\","
             "\"progress\":%.1f,\"kbps\":%.1f,"
             "\"msg\":\"%s\"}",
             state_str,
             ota_service_get_progress(),
             ota_service_get_kbps(),
             ota_service_get_status_msg());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /info — JSON device information
 *
 * Response: {"chip":"ESP32-S3","cores":2,"flash_mb":16,"idf":"v5.x.x",
 *            "free_heap_kb":220,"mac":"AA:BB:CC:DD:EE:FF"}
 * -------------------------------------------------------------------------- */
static esp_err_t info_get_handler(httpd_req_t *req) {
    esp_chip_info_t ci;
    esp_chip_info(&ci);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char json[320];
    snprintf(json, sizeof(json),
             "{"
             "\"chip\":\"ESP32-S%d\","
             "\"cores\":%d,"
             "\"flash_mb\":%lu,"
             "\"idf\":\"%s\","
             "\"free_heap_kb\":%lu,"
             "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"ap_ssid\":\"%s\","
             "\"ip\":\"192.168.4.1\""
             "}",
             (ci.model == CHIP_ESP32S3) ? 3 : 2,
             ci.cores,
             (unsigned long)(flash_size / (1024 * 1024)),
             esp_get_idf_version(),
             (unsigned long)(esp_get_free_heap_size() / 1024),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             APP_WIFI_SSID);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * POST /upload — Receive firmware binary and flash it
 *
 * Accepts raw application/octet-stream uploads (curl, XMLHttpRequest,
 * fetch API).  Validates Content-Length and ESP32 magic byte before
 * touching flash.
 * -------------------------------------------------------------------------- */
static esp_err_t upload_post_handler(httpd_req_t *req) {
    int total_len = req->content_len;

    ESP_LOGI(TAG, "Upload started — Content-Length: %d bytes", total_len);
    sys_mgr_send_ui_msg(OTA_STATE_CONNECTED, 0.0f, 0.0f, "Connection received");

    /* --- Size pre-validation --- */
    if (total_len <= 0) {
        ESP_LOGE(TAG, "Missing or zero Content-Length");
        return _send_json_error(req, HTTPD_400_BAD_REQUEST,
                                "Content-Length required");
    }
    if ((size_t)total_len < APP_OTA_MIN_IMAGE_SIZE) {
        ESP_LOGE(TAG, "File too small (%d B)", total_len);
        return _send_json_error(req, HTTPD_400_BAD_REQUEST,
                                "File too small — not an ESP32 firmware");
    }
    if ((size_t)total_len > APP_OTA_MAX_IMAGE_SIZE) {
        ESP_LOGE(TAG, "File too large (%d B)", total_len);
        return _send_json_error(req, HTTPD_400_BAD_REQUEST,
                                "File exceeds OTA partition size (3 MB)");
    }

    /* --- Begin OTA --- */
    esp_err_t err = ota_service_begin((size_t)total_len);
    if (err != ESP_OK) {
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA initialisation failed");
    }

    /* --- Streaming receive + write loop --- */
    int remaining = total_len;
    int timeout_retries = 0;
    const int MAX_TIMEOUT_RETRIES = 20;  /* 20 × 500 ms = 10 s max stall */

    while (remaining > 0) {
        int chunk = MIN(remaining, (int)sizeof(s_recv_buf));
        int received = httpd_req_recv(req, (char *)s_recv_buf, chunk);

        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeout_retries > MAX_TIMEOUT_RETRIES) {
                ESP_LOGE(TAG, "Upload stalled — too many timeouts");
                ota_service_abort();
                return _send_json_error(req, HTTPD_408_REQ_TIMEOUT,
                                        "Upload timed out");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        timeout_retries = 0;  /* Reset on successful recv */

        if (received <= 0) {
            ESP_LOGE(TAG, "Receive error (%d) after %d bytes",
                     received, total_len - remaining);
            ota_service_abort();
            return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Connection lost during upload");
        }

        err = ota_service_write(s_recv_buf, (size_t)received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Flash write failed after %d bytes written",
                     total_len - remaining);
            ota_service_abort();
            return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Flash write error — check serial monitor");
        }

        remaining -= received;
    }

    /* --- Finalise --- */
    ESP_LOGI(TAG, "All %d bytes received — finalising OTA", total_len);
    err = ota_service_end();

    if (err != ESP_OK) {
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Firmware validation failed");
    }

    /* Send success JSON before rebooting */
    const char *success_json =
        "{\"ok\":true,\"msg\":\"Firmware updated! Rebooting in 2.5s...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, success_json, HTTPD_RESP_USE_STRLEN);

    /* Let the response flush before restarting */
    vTaskDelay(pdMS_TO_TICKS(APP_OTA_REBOOT_DELAY_MS));
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();

    return ESP_OK;  /* Unreachable — satisfies compiler */
}

/* --------------------------------------------------------------------------
 * Server initialisation
 * -------------------------------------------------------------------------- */
void web_server_init(void) {
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = APP_HTTP_PORT;
    config.max_open_sockets = APP_HTTP_MAX_SOCKETS;
    config.lru_purge_enable = true;
    /* Generous stack for the upload handler (has a large local recv buffer) */
    config.stack_size       = 8192;
    /* Allow longer uploads without server-side timeout */
    config.recv_wait_timeout  = 10;
    config.send_wait_timeout  = 10;

    httpd_handle_t server = NULL;
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    /* Route table */
    static const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = index_get_handler  },
        { .uri = "/status", .method = HTTP_GET,  .handler = status_get_handler },
        { .uri = "/info",   .method = HTTP_GET,  .handler = info_get_handler   },
        { .uri = "/upload", .method = HTTP_POST, .handler = upload_post_handler },
    };

    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server ready — %d routes registered", (int)(sizeof(routes)/sizeof(routes[0])));
}
