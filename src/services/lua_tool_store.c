/*
 * lua_tool_store.c — Lua Tool Marketplace Implementation
 * ========================================================
 * Pattern: Same as game_store.c — HTTP fetch catalog, parse cJSON,
 *          download scripts to LittleFS, track installs in NVS.
 *
 * Catalog JSON format:
 *   { "catalog_version": "1.0", "tools": [ { id, name, version, ... } ] }
 *
 * Installed tools tracked in NVS namespace "lua_tools", key "installed"
 * as a JSON array of tool IDs.
 */
#include "lua_tool_store.h"
#include "sys_config.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>

static const char* TAG = "LUA_TOOL";

/* ── Internal state ─────────────────────────────────────────────────── */
static lua_tool_status_t s_status;
static lua_tool_entry_t s_catalog[LUA_TOOL_MAX_TOOLS];
static int s_catalog_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_worker_handle = NULL;
static volatile bool s_abort_flag = false;

static EventGroupHandle_t s_events = NULL;
#define EVT_CATALOG_DONE  (1 << 0)
#define EVT_CATALOG_FAIL  (1 << 1)
#define EVT_DL_DONE       (1 << 2)
#define EVT_DL_FAIL       (1 << 3)

/* ── Helpers ────────────────────────────────────────────────────────── */
static void _lock(void) {
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
    }
}

static void _unlock(void) {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

static void _set_busy(bool busy, const char* msg) {
    _lock();
    s_status.is_busy = busy;
    if (msg) {
        strncpy(s_status.status_msg, msg, sizeof(s_status.status_msg) - 1);
        s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
    }
    _unlock();
}

static void _set_error(lua_tool_err_t err, const char* msg) {
    _lock();
    s_status.is_busy = false;
    s_status.last_error = err;
    if (msg) {
        strncpy(s_status.status_msg, msg, sizeof(s_status.status_msg) - 1);
        s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
    }
    _unlock();
}

/* ── NVS: installed tools tracking ─────────────────────────────────── */
static void _nvs_load_installed(cJSON* arr) {
    nvs_handle_t h;
    if (nvs_open("lua_tools", NVS_READONLY, &h) != ESP_OK) return;

    /* Static buffer avoids 1024B stack allocation — NVS reads are short-lived */
    static char s_nvs_buf[1024];
    memset(s_nvs_buf, 0, sizeof(s_nvs_buf));
    size_t len = sizeof(s_nvs_buf);
    if (nvs_get_str(h, "installed", s_nvs_buf, &len) == ESP_OK && s_nvs_buf[0]) {
        cJSON* saved = cJSON_Parse(s_nvs_buf);
        if (saved && cJSON_IsArray(saved)) {
            int n = cJSON_GetArraySize(saved);
            for (int i = 0; i < n && i < LUA_TOOL_MAX_TOOLS; i++) {
                cJSON* item = cJSON_GetArrayItem(saved, i);
                if (cJSON_IsString(item)) {
                    cJSON_AddItemToArray(arr, cJSON_Duplicate(item, 1));
                }
            }
            cJSON_Delete(saved);
        }
    }
    nvs_close(h);
}

static void _nvs_save_installed(cJSON* arr) {
    nvs_handle_t h;
    if (nvs_open("lua_tools", NVS_READWRITE, &h) != ESP_OK) return;

    char* json_str = cJSON_PrintUnformatted(arr);
    if (json_str) {
        nvs_set_str(h, "installed", json_str);
        nvs_commit(h);
        cJSON_free(json_str);
    }
    nvs_close(h);
}

/* ── HTTP event handler ─────────────────────────────────────────────── */
static char* s_http_buf = NULL;
static int s_http_len = 0;

static esp_err_t _http_event(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && s_http_buf) {
        if (s_http_len + evt->data_len < LUA_TOOL_MAX_SIZE) {
            memcpy(s_http_buf + s_http_len, evt->data, evt->data_len);
            s_http_len += evt->data_len;
            s_http_buf[s_http_len] = '\0';
        }
    }
    return ESP_OK;
}

/* ── Catalog fetch task (runs on Core 0) ────────────────────────────── */
static void _catalog_task(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "Fetching catalog from: %s", LUA_TOOL_CATALOG_URL);

    if (!wifi_mgr_is_connected()) {
        _set_error(LUA_TOOL_ERR_NO_WIFI, "No WiFi");
        xEventGroupSetBits(s_events, EVT_CATALOG_FAIL);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_http_buf = heap_caps_malloc(LUA_TOOL_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_http_buf) {
        _set_error(LUA_TOOL_ERR_NO_SPACE, "OOM for catalog");
        xEventGroupSetBits(s_events, EVT_CATALOG_FAIL);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    s_http_len = 0;

    esp_http_client_config_t cfg = {
        .url = LUA_TOOL_CATALOG_URL,
        .event_handler = _http_event,
        .timeout_ms = LUA_TOOL_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Catalog fetch failed: %s, status=%d", esp_err_to_name(err), status);
        heap_caps_free(s_http_buf);
        s_http_buf = NULL;
        _set_error(LUA_TOOL_ERR_NETWORK, "Catalog fetch failed");
        xEventGroupSetBits(s_events, EVT_CATALOG_FAIL);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Parse catalog JSON */
    cJSON* root = cJSON_Parse(s_http_buf);
    heap_caps_free(s_http_buf);
    s_http_buf = NULL;

    if (!root) {
        _set_error(LUA_TOOL_ERR_BAD_METADATA, "Invalid catalog JSON");
        xEventGroupSetBits(s_events, EVT_CATALOG_FAIL);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    cJSON* tools = cJSON_GetObjectItem(root, "tools");
    if (!tools || !cJSON_IsArray(tools)) {
        cJSON_Delete(root);
        _set_error(LUA_TOOL_ERR_BAD_METADATA, "Missing 'tools' array");
        xEventGroupSetBits(s_events, EVT_CATALOG_FAIL);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Load installed list */
    cJSON* installed_list = cJSON_CreateArray();
    _nvs_load_installed(installed_list);

    /* Parse tools */
    _lock();
    s_catalog_count = 0;
    int n = cJSON_GetArraySize(tools);
    for (int i = 0; i < n && s_catalog_count < LUA_TOOL_MAX_TOOLS; i++) {
        cJSON* item = cJSON_GetArrayItem(tools, i);
        lua_tool_entry_t* e = &s_catalog[s_catalog_count];

        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* name = cJSON_GetObjectItem(item, "name");
        cJSON* version = cJSON_GetObjectItem(item, "version");
        cJSON* desc = cJSON_GetObjectItem(item, "description");
        cJSON* author = cJSON_GetObjectItem(item, "author");
        cJSON* url = cJSON_GetObjectItem(item, "url");
        cJSON* size = cJSON_GetObjectItem(item, "size_bytes");

        if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsString(url)) continue;

        memset(e, 0, sizeof(*e));
        strncpy(e->id, id->valuestring, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
        strncpy(e->name, name->valuestring, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        if (cJSON_IsString(version)) {
            strncpy(e->version, version->valuestring, sizeof(e->version) - 1);
            e->version[sizeof(e->version) - 1] = '\0';
        }
        if (cJSON_IsString(desc)) {
            strncpy(e->description, desc->valuestring, sizeof(e->description) - 1);
            e->description[sizeof(e->description) - 1] = '\0';
        }
        if (cJSON_IsString(author)) {
            strncpy(e->author, author->valuestring, sizeof(e->author) - 1);
            e->author[sizeof(e->author) - 1] = '\0';
        }
        strncpy(e->url, url->valuestring, sizeof(e->url) - 1);
        e->url[sizeof(e->url) - 1] = '\0';
        if (cJSON_IsNumber(size)) e->size_bytes = (uint32_t)size->valueint;

        /* Check if installed */
        int installed_count = cJSON_GetArraySize(installed_list);
        for (int j = 0; j < installed_count; j++) {
            cJSON* inst_id = cJSON_GetArrayItem(installed_list, j);
            if (cJSON_IsString(inst_id) && strcmp(inst_id->valuestring, e->id) == 0) {
                e->is_installed = true;
                break;
            }
        }

        s_catalog_count++;
    }
    _unlock();

    cJSON_Delete(installed_list);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Catalog loaded: %d tools", s_catalog_count);
    _set_busy(false, "Catalog loaded");
    xEventGroupSetBits(s_events, EVT_CATALOG_DONE);
    s_worker_handle = NULL;
    vTaskDelete(NULL);
}

/* ── Download task (runs on Core 0) ─────────────────────────────────── */
typedef struct {
    char id[32];
    char url[LUA_TOOL_MAX_URL];
    char name[LUA_TOOL_MAX_NAME];
} dl_args_t;

static void _download_task(void* arg) {
    dl_args_t* args = (dl_args_t*)arg;
    ESP_LOGI(TAG, "Downloading tool: %s from %s", args->id, args->url);

    s_http_buf = heap_caps_malloc(LUA_TOOL_DL_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_http_buf) {
        _set_error(LUA_TOOL_ERR_NO_SPACE, "OOM for download");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Build output path */
    char path[128];
    snprintf(path, sizeof(path), "%s/%s.lua", LUA_TOOL_SCRIPTS_DIR, args->id);

    esp_http_client_config_t cfg = {
        .url = args->url,
        .event_handler = _http_event,
        .timeout_ms = LUA_TOOL_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };

    s_http_len = 0;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    /* First pass: get content length */
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        heap_caps_free(s_http_buf);
        s_http_buf = NULL;
        _set_error(LUA_TOOL_ERR_NETWORK, "Connection failed");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);

    if (content_length > (int)LUA_TOOL_MAX_SIZE) {
        ESP_LOGE(TAG, "Script too large: %d bytes", content_length);
        heap_caps_free(s_http_buf);
        s_http_buf = NULL;
        _set_error(LUA_TOOL_ERR_TOO_LARGE, "Script too large");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Second pass: download */
    s_http_len = 0;
    err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Download failed: %s, status=%d", esp_err_to_name(err), status);
        heap_caps_free(s_http_buf);
        s_http_buf = NULL;
        _set_error(LUA_TOOL_ERR_NETWORK, "Download failed");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Write to LittleFS — atomic .tmp→rename pattern (Critical Invariant #6) */
    char tmp_path[144];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE* f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", tmp_path);
        heap_caps_free(s_http_buf);
        s_http_buf = NULL;
        _set_error(LUA_TOOL_ERR_FS_ERROR, "Write failed");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    size_t written = fwrite(s_http_buf, 1, s_http_len, f);
    fclose(f);
    heap_caps_free(s_http_buf);
    s_http_buf = NULL;

    if (written != (size_t)s_http_len) {
        ESP_LOGE(TAG, "fwrite short: %zu vs %d", written, s_http_len);
        unlink(tmp_path);
        _set_error(LUA_TOOL_ERR_FS_ERROR, "Write failed");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s → %s", tmp_path, path);
        unlink(tmp_path);
        _set_error(LUA_TOOL_ERR_FS_ERROR, "Write failed");
        xEventGroupSetBits(s_events, EVT_DL_FAIL);
        free(args);
        s_worker_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Update installed list in NVS */
    cJSON* installed = cJSON_CreateArray();
    _nvs_load_installed(installed);

    /* Check if already in list */
    bool found = false;
    int cnt = cJSON_GetArraySize(installed);
    for (int i = 0; i < cnt; i++) {
        cJSON* item = cJSON_GetArrayItem(installed, i);
        if (cJSON_IsString(item) && strcmp(item->valuestring, args->id) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        cJSON_AddItemToArray(installed, cJSON_CreateString(args->id));
    }
    _nvs_save_installed(installed);
    cJSON_Delete(installed);

    /* Update catalog entry */
    _lock();
    for (int i = 0; i < s_catalog_count; i++) {
        if (strcmp(s_catalog[i].id, args->id) == 0) {
            s_catalog[i].is_installed = true;
            break;
        }
    }
    _unlock();

    ESP_LOGI(TAG, "Tool installed: %s → %s", args->id, path);
    _set_busy(false, "Installed");
    xEventGroupSetBits(s_events, EVT_DL_DONE);
    free(args);
    s_worker_handle = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ─────────────────────────────────────────────────────── */

lua_tool_err_t lua_tool_store_init(void) {
    ESP_LOGI(TAG, "Initializing Lua Tool Marketplace");

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return LUA_TOOL_ERR_FS_ERROR;

    s_events = xEventGroupCreate();
    if (!s_events) return LUA_TOOL_ERR_FS_ERROR;

    memset(&s_status, 0, sizeof(s_status));
    memset(s_catalog, 0, sizeof(s_catalog));
    s_catalog_count = 0;

    /* Ensure scripts directory exists */
    struct stat st;
    if (stat(LUA_TOOL_SCRIPTS_DIR, &st) != 0) {
        mkdir(LUA_TOOL_SCRIPTS_DIR, 0755);
    }

    ESP_LOGI(TAG, "Lua Tool Marketplace initialized");
    return LUA_TOOL_ERR_OK;
}

lua_tool_err_t lua_tool_store_refresh_catalog(void) {
    if (s_status.is_busy) return LUA_TOOL_ERR_ABORTED;
    if (!wifi_mgr_is_connected()) return LUA_TOOL_ERR_NO_WIFI;

    s_abort_flag = false;
    _set_busy(true, "Refreshing catalog...");

    BaseType_t rc = xTaskCreatePinnedToCore(
        _catalog_task, "tool_cat", 8192, NULL, 4, &s_worker_handle, 0);
    if (rc != pdPASS) {
        _set_error(LUA_TOOL_ERR_NETWORK, "Task creation failed");
        return LUA_TOOL_ERR_NETWORK;
    }

    /* Wait for completion */
    EventBits_t bits = xEventGroupWaitBits(s_events,
        EVT_CATALOG_DONE | EVT_CATALOG_FAIL,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(20000));

    if (bits & EVT_CATALOG_FAIL) return s_status.last_error;
    return LUA_TOOL_ERR_OK;
}

int lua_tool_store_get_catalog(lua_tool_entry_t* out, int max_count) {
    if (!out || max_count <= 0) return 0;
    _lock();
    int n = (s_catalog_count < max_count) ? s_catalog_count : max_count;
    memcpy(out, s_catalog, n * sizeof(lua_tool_entry_t));
    _unlock();
    return n;
}

lua_tool_err_t lua_tool_store_install(const char* id) {
    if (s_status.is_busy) return LUA_TOOL_ERR_ABORTED;
    if (!id || !id[0]) return LUA_TOOL_ERR_BAD_METADATA;
    if (!wifi_mgr_is_connected()) return LUA_TOOL_ERR_NO_WIFI;

    /* Find tool in catalog */
    const lua_tool_entry_t* tool = NULL;
    _lock();
    for (int i = 0; i < s_catalog_count; i++) {
        if (strcmp(s_catalog[i].id, id) == 0) {
            tool = &s_catalog[i];
            break;
        }
    }
    _unlock();

    if (!tool) return LUA_TOOL_ERR_BAD_METADATA;
    if (tool->is_installed) return LUA_TOOL_ERR_ALREADY_INSTALLED;

    s_abort_flag = false;
    _lock();
    s_status.progress_pct = 0;
    s_status.last_error = LUA_TOOL_ERR_OK;
    strncpy(s_status.active_tool_id, id, sizeof(s_status.active_tool_id) - 1);
    s_status.active_tool_id[sizeof(s_status.active_tool_id) - 1] = '\0';
    _unlock();
    _set_busy(true, "Downloading...");

    dl_args_t* args = malloc(sizeof(dl_args_t));
    if (!args) return LUA_TOOL_ERR_NO_SPACE;
    strncpy(args->id, id, sizeof(args->id) - 1);
    args->id[sizeof(args->id) - 1] = '\0';
    strncpy(args->url, tool->url, sizeof(args->url) - 1);
    args->url[sizeof(args->url) - 1] = '\0';
    strncpy(args->name, tool->name, sizeof(args->name) - 1);
    args->name[sizeof(args->name) - 1] = '\0';

    BaseType_t rc = xTaskCreatePinnedToCore(
        _download_task, "tool_dl", 8192, args, 4, &s_worker_handle, 0);
    if (rc != pdPASS) {
        free(args);
        _set_error(LUA_TOOL_ERR_NETWORK, "Task creation failed");
        return LUA_TOOL_ERR_NETWORK;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events,
        EVT_DL_DONE | EVT_DL_FAIL,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & EVT_DL_FAIL) return s_status.last_error;
    return LUA_TOOL_ERR_OK;
}

lua_tool_err_t lua_tool_store_uninstall(const char* id) {
    if (!id || !id[0]) return LUA_TOOL_ERR_BAD_METADATA;

    /* Delete file */
    char path[128];
    snprintf(path, sizeof(path), "%s/%s.lua", LUA_TOOL_SCRIPTS_DIR, id);
    remove(path);

    /* Update NVS */
    cJSON* installed = cJSON_CreateArray();
    _nvs_load_installed(installed);

    cJSON* new_list = cJSON_CreateArray();
    int cnt = cJSON_GetArraySize(installed);
    for (int i = 0; i < cnt; i++) {
        cJSON* item = cJSON_GetArrayItem(installed, i);
        if (cJSON_IsString(item) && strcmp(item->valuestring, id) != 0) {
            cJSON_AddItemToArray(new_list, cJSON_Duplicate(item, 1));
        }
    }
    _nvs_save_installed(new_list);
    cJSON_Delete(installed);
    cJSON_Delete(new_list);

    /* Update catalog entry */
    _lock();
    for (int i = 0; i < s_catalog_count; i++) {
        if (strcmp(s_catalog[i].id, id) == 0) {
            s_catalog[i].is_installed = false;
            break;
        }
    }
    _unlock();

    ESP_LOGI(TAG, "Tool uninstalled: %s", id);
    return LUA_TOOL_ERR_OK;
}

lua_tool_err_t lua_tool_store_abort(void) {
    s_abort_flag = true;
    _set_busy(false, "Aborted");
    return LUA_TOOL_ERR_OK;
}

void lua_tool_store_get_status(lua_tool_status_t* out) {
    if (!out) return;
    _lock();
    *out = s_status;
    _unlock();
}

bool lua_tool_store_is_installed(const char* id) {
    if (!id) return false;
    _lock();
    for (int i = 0; i < s_catalog_count; i++) {
        if (strcmp(s_catalog[i].id, id) == 0) {
            bool result = s_catalog[i].is_installed;
            _unlock();
            return result;
        }
    }
    _unlock();
    return false;
}

lua_tool_err_t lua_tool_store_get_script_path(const char* id, char* path_out, size_t len) {
    if (!id || !path_out || len == 0) return LUA_TOOL_ERR_BAD_METADATA;
    snprintf(path_out, len, "%s/%s.lua", LUA_TOOL_SCRIPTS_DIR, id);

    struct stat st;
    if (stat(path_out, &st) != 0) return LUA_TOOL_ERR_FS_ERROR;
    return LUA_TOOL_ERR_OK;
}
