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
#include "../config/web_remote_ui.h"
#include "../system/sys_mgr.h"
#include "../managers/input_mgr.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
static const char* s_captive_ip = "192.168.4.1";

/* Receive buffer — allocated once on the stack of upload_post_handler.
 * 8 KB gives a good balance: fewer flash-write calls, fits in stack. */
static uint8_t s_recv_buf[APP_OTA_RECV_BUF_SIZE];

/* --------------------------------------------------------------------------
 * Helper: send a JSON error response
 * -------------------------------------------------------------------------- */
static esp_err_t _send_json_error(httpd_req_t *req,
                                  httpd_err_code_t code,
                                  const char *error_msg) {
    char body[256];
    char safe_msg[128];
    size_t j = 0;
    for (size_t i = 0; error_msg[i] && j < sizeof(safe_msg) - 1; i++) {
        if (error_msg[i] == '"') safe_msg[j++] = '\'';
        else if (error_msg[i] == '\\') safe_msg[j++] = '\\';
        else safe_msg[j++] = error_msg[i];
    }
    safe_msg[j] = '\0';
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", safe_msg);
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
             "\"ip\":\"%s\""
             "}",
             (ci.model == CHIP_ESP32S3) ? 3 : 2,
             ci.cores,
             (unsigned long)(flash_size / (1024 * 1024)),
             esp_get_idf_version(),
             (unsigned long)(esp_get_free_heap_size() / 1024),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             APP_WIFI_SSID,
             s_captive_ip);

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
                                "File exceeds OTA partition size (4 MB)");
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
    const int MAX_TIMEOUT_RETRIES = 200;  /* 200 × 50 ms = 10 s max stall */

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
            vTaskDelay(pdMS_TO_TICKS(50));
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
 * GET /remote — Serve the Virtual Gamepad Web UI
 * -------------------------------------------------------------------------- */
static esp_err_t remote_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, WEB_REMOTE_UI_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /remote/input — Handle query parameters for virtual key injection
 * -------------------------------------------------------------------------- */
static esp_err_t remote_input_get_handler(httpd_req_t *req) {
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf && httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char btn_val[16];
            if (httpd_query_key_value(buf, "btn", btn_val, sizeof(btn_val)) == ESP_OK) {
                input_event_t ev = BTN_NONE;
                if (strcmp(btn_val, "up") == 0) ev = BTN_UP;
                else if (strcmp(btn_val, "down") == 0) ev = BTN_DOWN;
                else if (strcmp(btn_val, "left") == 0) ev = BTN_LEFT;
                else if (strcmp(btn_val, "right") == 0) ev = BTN_RIGHT;
                else if (strcmp(btn_val, "select") == 0) ev = BTN_SELECT;

                if (ev != BTN_NONE) {
                    input_mgr_inject_event(ev);
                }
            }
        }
        if (buf) heap_caps_free(buf);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /error_404 — Captive Portal Redirection Handler for any unregistered URIs
 * -------------------------------------------------------------------------- */
static esp_err_t captive_portal_redirect_handler(httpd_req_t *req, httpd_err_code_t error) {
    ESP_LOGI(TAG, "Captive portal redirect: %s (error %d)", req->uri, error);
    httpd_resp_set_status(req, "302 Found");
    char location[64];
    snprintf(location, sizeof(location), "http://%s/", s_captive_ip);
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    
    char *redirect_html = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!redirect_html) {
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    snprintf(redirect_html, 2048,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
        "<title>ESP32 Launcher Portal</title><style>"
        "body{background:#08090d;color:#f0f3f8;font-family:system-ui,-apple-system,sans-serif;"
        "display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;margin:0;overflow:hidden;}"
        ".container{text-align:center;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);"
        "border-radius:16px;padding:32px;box-shadow:0 8px 32px 0 rgba(0,0,0,0.37);backdrop-filter:blur(8px);width:280px;}"
        "h2{color:#00f2fe;margin:0 0 8px;font-weight:600;letter-spacing:1px;text-shadow:0 0 10px rgba(0,242,254,0.3);}"
        "p{color:#8a99ad;font-size:13px;margin:0 0 24px;}"
        ".spinner{border:3px solid rgba(255,255,255,0.05);border-top:3px solid #00f2fe;border-radius:50%;"
        "width:32px;height:32px;animation:spin 1s linear infinite;margin:0 auto;box-shadow:0 0 15px rgba(0,242,254,0.2);}"
        "@keyframes spin{0%%{transform:rotate(0deg);}100%%{transform:rotate(360deg);}}"
        "</style><meta http-equiv=\"refresh\" content=\"0; url=http://%s/\"></head>"
        "<body><div class=\"container\"><h2>ESP32 PORTAL</h2><p>Initializing Wireless Interface...</p>"
        "<div class=\"spinner\"></div></div><script>window.location.href=\"http://%s/\";</script>"
        "</body></html>",
        s_captive_ip, s_captive_ip);

    httpd_resp_send(req, redirect_html, HTTPD_RESP_USE_STRLEN);
    heap_caps_free(redirect_html);
    return ESP_OK;
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
    /* 12 routes registered — default is only 8 */
    config.max_uri_handlers = 16;
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
        { .uri = "/",             .method = HTTP_GET,  .handler = index_get_handler  },
        { .uri = "/status",       .method = HTTP_GET,  .handler = status_get_handler },
        { .uri = "/info",         .method = HTTP_GET,  .handler = info_get_handler   },
        { .uri = "/upload",       .method = HTTP_POST, .handler = upload_post_handler },
        { .uri = "/remote",       .method = HTTP_GET,  .handler = remote_get_handler },
        { .uri = "/remote/input", .method = HTTP_GET,  .handler = remote_input_get_handler },
    };

    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    // Register custom 404 handler for Captive Portal redirects
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_redirect_handler);

    ESP_LOGI(TAG, "HTTP server ready — %d routes registered + captive portal redirect handler active", (int)(sizeof(routes)/sizeof(routes[0])));
}
