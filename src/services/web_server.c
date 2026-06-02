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
#include "../config/web_chat_ui.h"
#include "llm_client.h"
#include "../system/sys_mgr.h"
#include "../managers/input_mgr.h"
#include "esp_http_server.h"
#include <dirent.h>
#include <sys/stat.h>
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
        char *buf = malloc(buf_len);
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
        if (buf) free(buf);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

extern void lua_engine_clear_logs(void);
extern void lua_engine_append_log(const char* text);
extern const char* lua_engine_get_logs(void);
extern int lua_engine_run_file(const char* path, char* err_out, int max_err_len, uint32_t* elapsed_ms);

/* --------------------------------------------------------------------------
 * GET /chat — Serve the Unified AI Chat & IDE Web UI
 * -------------------------------------------------------------------------- */
static esp_err_t chat_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, WEB_CHAT_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─── SSE Chunk callback ────────────────────────────────────────────────── */
static bool _web_llm_chunk(const char* token, size_t len, void* user) {
    httpd_req_t* req = (httpd_req_t*)user;
    if (!token || len == 0) return true;

    char* esc = (char*)malloc(len * 2 + 1);
    if (!esc) return false;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (token[i] == '"' || token[i] == '\\') {
            esc[j++] = '\\';
            esc[j++] = token[i];
        } else if (token[i] == '\n') {
            esc[j++] = '\\';
            esc[j++] = 'n';
        } else if (token[i] == '\r') {
            esc[j++] = '\\';
            esc[j++] = 'r';
        } else if (token[i] == '\t') {
            esc[j++] = '\\';
            esc[j++] = 't';
        } else {
            esc[j++] = token[i];
        }
    }
    esc[j] = '\0';

    char chunk[1024];
    int n = snprintf(chunk, sizeof(chunk), "data: {\"token\":\"%s\"}\n\n", esc);
    free(esc);

    esp_err_t err = httpd_resp_send_chunk(req, chunk, n);
    return err == ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /chat/send — Handles SSE streaming user message prompts
 * -------------------------------------------------------------------------- */
static esp_err_t chat_send_get_handler(httpd_req_t *req) {
    char query[512] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing query string");
    }
    char msg[256] = {};
    if (httpd_query_key_value(query, "msg", msg, sizeof(msg)) != ESP_OK) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing msg parameter");
    }

    char decoded[256] = {};
    size_t dj = 0;
    size_t msg_len = strlen(msg);
    for (size_t i = 0; msg[i]; i++) {
        if (msg[i] == '%' && i + 2 < msg_len) {
            char hex[3] = { msg[i+1], msg[i+2], '\0' };
            decoded[dj++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (msg[i] == '+') {
            decoded[dj++] = ' ';
        } else {
            decoded[dj++] = msg[i];
        }
        if (dj >= sizeof(decoded) - 1) break;
    }
    decoded[dj] = '\0';

    httpd_resp_set_type(req, "text/event-stream; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    llm_client_send(decoded, _web_llm_chunk, req);

    httpd_resp_send_chunk(req, "data: [DONE]\n\n", 14);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /chat/ide/files — Lists all script files in the LittleFS scripts dir
 * -------------------------------------------------------------------------- */
static esp_err_t chat_ide_files_get_handler(httpd_req_t *req) {
    char* json = malloc(2048);
    if (!json) {
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    size_t pos = 0;
    int n = snprintf(json, 2048, "{\"files\":[");
    if (n > 0) pos += n;
    DIR* d = opendir(AI_SCRIPTS_DIR);
    if (d) {
        struct dirent* e;
        bool first = true;
        while ((e = readdir(d))) {
            if (strstr(e->d_name, ".lua")) {
                if (!first) {
                    if (pos < 2047) json[pos++] = ',';
                }
                size_t remaining = (pos < 2048) ? 2047 - pos : 0;
                n = snprintf(json + pos, remaining, "\"%s\"", e->d_name);
                if (n > 0) pos += n;
                first = false;
            }
        }
        closedir(d);
    }
    if (pos < 2048) json[pos++] = ']';
    if (pos < 2048) json[pos++] = '}';
    if (pos < 2048) json[pos] = '\0';
    else json[2047] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /chat/ide/load — Loads content of a script file
 * -------------------------------------------------------------------------- */
static esp_err_t chat_ide_load_get_handler(httpd_req_t *req) {
    char query[256] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing query");
    }
    char file[64] = {};
    if (httpd_query_key_value(query, "file", file, sizeof(file)) != ESP_OK) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
    }
    if (strstr(file, "..")) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Invalid file path");
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", AI_SCRIPTS_DIR, file);

    FILE* f = fopen(path, "r");
    if (!f) {
        return _send_json_error(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    char* buf = malloc(4096);
    if (!buf) {
        fclose(f);
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    size_t n = fread(buf, 1, 4095, f);
    buf[n] = '\0';
    fclose(f);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, n);
    free(buf);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * POST /chat/ide/save — Saves content of a script file
 * -------------------------------------------------------------------------- */
static esp_err_t chat_ide_save_post_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    char* buf = malloc(total_len + 1);
    if (!buf) {
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';

    char file[64] = {};
    httpd_query_key_value(buf, "file", file, sizeof(file));

    char* code_enc = malloc(total_len + 1);
    char* code = malloc(total_len + 1);
    if (!code_enc || !code) {
        free(code_enc);
        free(code);
        free(buf);
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    code[0] = '\0';
    if (httpd_query_key_value(buf, "code", code_enc, total_len) == ESP_OK) {
        size_t j = 0;
        size_t enc_len = strlen(code_enc);
        for (size_t i = 0; code_enc[i]; i++) {
            if (code_enc[i] == '%' && i + 2 < enc_len) {
                char hex[3] = { code_enc[i+1], code_enc[i+2], '\0' };
                code[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else if (code_enc[i] == '+') {
                code[j++] = ' ';
            } else {
                code[j++] = code_enc[i];
            }
        }
        code[j] = '\0';
    }
    free(code_enc);
    free(buf);

    if (!file[0]) {
        free(code);
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
    }
    if (strstr(file, "..")) {
        free(code);
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Invalid file path");
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", AI_SCRIPTS_DIR, file);
    FILE* f = fopen(path, "w");
    if (f) {
        fwrite(code, 1, strlen(code), f);
        fclose(f);
    }
    free(code);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /chat/ide/run — Executes a script file asynchronously & gets captured prints
 * -------------------------------------------------------------------------- */
static esp_err_t chat_ide_run_get_handler(httpd_req_t *req) {
    char query[256] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing query");
    }
    char file[64] = {};
    if (httpd_query_key_value(query, "file", file, sizeof(file)) != ESP_OK) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
    }
    if (strstr(file, "..")) {
        return _send_json_error(req, HTTPD_400_BAD_REQUEST, "Invalid file path");
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", AI_SCRIPTS_DIR, file);

    lua_engine_clear_logs();

    char err[128] = {};
    uint32_t elapsed = 0;
    int status = lua_engine_run_file(path, err, sizeof(err), &elapsed);

    const char* raw_logs = lua_engine_get_logs();
    size_t logs_len = strlen(raw_logs);
    char* esc_logs = malloc(logs_len * 2 + 1);
    if (!esc_logs) {
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    size_t j = 0;
    for (size_t i = 0; i < logs_len; i++) {
        if (raw_logs[i] == '"' || raw_logs[i] == '\\') {
            esc_logs[j++] = '\\';
            esc_logs[j++] = raw_logs[i];
        } else if (raw_logs[i] == '\n') {
            esc_logs[j++] = '\\';
            esc_logs[j++] = 'n';
        } else if (raw_logs[i] == '\r') {
            esc_logs[j++] = '\\';
            esc_logs[j++] = 'r';
        } else {
            esc_logs[j++] = raw_logs[i];
        }
    }
    esc_logs[j] = '\0';

    char* json = malloc(logs_len * 2 + 512);
    if (!json) {
        free(esc_logs);
        return _send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    snprintf(json, logs_len * 2 + 512,
        "{\"ok\":%s,\"status\":%d,\"elapsed_ms\":%d,\"error\":\"%s\",\"logs\":\"%s\"}",
        (status == 0) ? "true" : "false", status, (int)elapsed, err, esc_logs);

    free(esc_logs);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * GET /error_404 — Captive Portal Redirection Handler for any unregistered URIs
 * -------------------------------------------------------------------------- */
static esp_err_t captive_portal_redirect_handler(httpd_req_t *req, httpd_err_code_t error) {
    ESP_LOGI(TAG, "Captive portal redirect: %s (error %d)", req->uri, error);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/chat");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    
    static const char *redirect_html = 
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
        "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}"
        "</style><meta http-equiv=\"refresh\" content=\"0; url=http://192.168.4.1/chat\"></head>"
        "<body><div class=\"container\"><h2>ESP32 PORTAL</h2><p>Initializing Wireless Interface...</p>"
        "<div class=\"spinner\"></div></div><script>window.location.href=\"http://192.168.4.1/chat\";</script>"
        "</body></html>";

    httpd_resp_send(req, redirect_html, HTTPD_RESP_USE_STRLEN);
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
        { .uri = "/chat",          .method = HTTP_GET,  .handler = chat_get_handler          },
        { .uri = "/chat/send",     .method = HTTP_GET,  .handler = chat_send_get_handler     },
        { .uri = "/chat/ide/files",.method = HTTP_GET,  .handler = chat_ide_files_get_handler},
        { .uri = "/chat/ide/load", .method = HTTP_GET,  .handler = chat_ide_load_get_handler },
        { .uri = "/chat/ide/save", .method = HTTP_POST, .handler = chat_ide_save_post_handler},
        { .uri = "/chat/ide/run",  .method = HTTP_GET,  .handler = chat_ide_run_get_handler  },
    };

    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    // Register custom 404 handler for Captive Portal redirects
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_redirect_handler);

    ESP_LOGI(TAG, "HTTP server ready — %d routes registered + captive portal redirect handler active", (int)(sizeof(routes)/sizeof(routes[0])));
}
