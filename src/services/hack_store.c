/*
 * hack_store.c — Backend service for downloading and flashing custom firmwares
 */
#include "hack_store.h"
#include "../config/sys_config.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "bootloader_common.h"
#include "esp_random.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "HACK_STORE";

static void _set_boot_allow_ota1_and_restart(void) {
    esp_partition_pos_t allow_ota1 = {
        .offset = BOOT_ALLOW_OTA1_MAGIC,
        .size   = 0
    };
    bootloader_common_update_rtc_retain_mem(&allow_ota1, false);
    ESP_LOGI(TAG, "RTC flag set (BOOT_ALLOW_OTA1) — hook will let ota_1 boot");
    vTaskDelay(pdMS_TO_TICKS(HS_PRE_REBOOT_DELAY_MS));
    esp_restart();
}

/**
 * @brief Validate that a URL is an absolute HTTP/HTTPS URL.
 * Returns false for empty strings, relative paths, or null — all of which
 * cause esp_http_client_open() to crash with a StoreProhibited exception.
 */
static bool _url_is_valid(const char *url) {
    if (!url || url[0] == '\0') return false;
    return (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static hack_store_status_t hs_status;
static hack_entry_t hs_catalog[HACK_STORE_MAX_FIRMS];
static int hs_catalog_count = 0;
static char hs_installed_id[32] = {0};
static SemaphoreHandle_t hs_mutex = NULL;

static EventGroupHandle_t hs_event_group = NULL;
#define HS_EVT_CATALOG_DONE (1 << 0)
#define HS_EVT_CATALOG_FAIL (1 << 1)
#define HS_EVT_DL_DONE      (1 << 2)
#define HS_EVT_DL_FAIL      (1 << 3)

static TaskHandle_t s_current_task_handle = NULL;
static volatile bool s_abort_requested = false;
static char s_ota_id_buf[32] = {0};  // Stable buffer for ID passed to OTA task

static void _hs_lock(void) {
    if (hs_mutex) {
        if (xSemaphoreTake(hs_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Mutex timeout");
        }
    }
}

static void _hs_unlock(void) {
    if (hs_mutex) {
        xSemaphoreGive(hs_mutex);
    }
}

static void _hs_status_snapshot(hack_store_status_t *out) {
    if (!out) return;
    _hs_lock();
    *out = hs_status;
    _hs_unlock();
}

static void _hs_status_set_busy(bool busy) {
    _hs_lock();
    hs_status.is_busy = busy;
    _hs_unlock();
}

static void _hs_status_set_error(hack_store_err_t err) {
    _hs_lock();
    hs_status.last_error = err;
    _hs_unlock();
}

static void _hs_status_reset_transfer(const char *id) {
    _hs_lock();
    hs_status.last_error = HACK_STORE_ERR_OK;
    hs_status.progress_pct = 0;
    hs_status.bytes_done = 0;
    hs_status.bytes_total = 0;
    memset(hs_status.active_id, 0, sizeof(hs_status.active_id));
    if (id) {
        strncpy(hs_status.active_id, id, sizeof(hs_status.active_id) - 1);
        hs_status.active_id[sizeof(hs_status.active_id) - 1] = '\0';
    }
    _hs_unlock();
}

/* --------------------------------------------------------------------------
 * Internal NVS Helpers
 * -------------------------------------------------------------------------- */
static void _load_installed_id(void) {
    nvs_handle_t h;
    _hs_lock();
    hs_installed_id[0] = '\0';
    if (nvs_open("hack_store", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(hs_installed_id);
        nvs_get_str(h, "installed_id", hs_installed_id, &len);
        nvs_close(h);
    }
    _hs_unlock();
}

static void _save_installed_id(const char* id) {
    nvs_handle_t h;
    if (nvs_open("hack_store", NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t err1 = nvs_set_str(h, "installed_id", id);
        esp_err_t err2 = nvs_commit(h);
        if (err1 != ESP_OK) {
            ESP_LOGW(TAG, "NVS set installed_id failed: %s", esp_err_to_name(err1));
        }
        if (err2 != ESP_OK) {
            ESP_LOGW(TAG, "NVS commit installed_id failed: %s", esp_err_to_name(err2));
        }
        nvs_close(h);
    }
    _hs_lock();
    strncpy(hs_installed_id, id, sizeof(hs_installed_id) - 1);
    hs_installed_id[sizeof(hs_installed_id) - 1] = '\0';
    hs_installed_id[sizeof(hs_installed_id) - 1] = '\0';
    _hs_unlock();
}

static hack_entry_t* _find_entry(const char* id) {
    for (int i = 0; i < hs_catalog_count; i++) {
        if (strcmp(hs_catalog[i].id, id) == 0) return &hs_catalog[i];
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * HTTP Event Handler for Download Progress
 * -------------------------------------------------------------------------- */
static esp_err_t _http_event_handle(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            if (strcasecmp(evt->header_key, "Content-Length") == 0) {
                _hs_lock();
                hs_status.bytes_total = atoi(evt->header_value);
                _hs_unlock();
            }
            break;
        case HTTP_EVENT_ON_DATA:
            _hs_lock();
            if (hs_status.bytes_total > 0) {
                hs_status.bytes_done += evt->data_len;
                hs_status.progress_pct = (hs_status.bytes_done * 100) / hs_status.bytes_total;
            }
            _hs_unlock();
            break;
        default:
            break;
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Cloud Catalog Task
 * -------------------------------------------------------------------------- */
static void _cloud_fetch_task(void *pvParameters) {
    (void)pvParameters;
    _hs_status_set_error(HACK_STORE_ERR_OK);

    ESP_LOGI(TAG, "Starting cloud fetch from: %s", HS_CATALOG_URL);
    if (!wifi_mgr_is_connected()) {
        ESP_LOGE(TAG, "Cloud fetch failed: No Wi-Fi connection");
        _hs_status_set_error(HACK_STORE_ERR_NO_WIFI);
        goto fetch_fail;
    }

    char fetch_url[256];
    snprintf(fetch_url, sizeof(fetch_url), "%s?t=%lu", HS_CATALOG_URL, (unsigned long)esp_random());
    
    esp_http_client_config_t config = {
        .url = fetch_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HS_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char *buffer = heap_caps_malloc(HS_CATALOG_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to alloc %u B SPIRAM buffer for catalog", HS_CATALOG_BUFFER_SIZE);
        esp_http_client_cleanup(client);
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        goto fetch_fail;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        heap_caps_free(buffer);
        esp_http_client_cleanup(client);
        goto fetch_fail;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP fetch headers complete, status code: %d", status_code);
    if (content_len < 0) {
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        heap_caps_free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto fetch_fail;
    }
    if (content_len >= (int)HS_CATALOG_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Firmware catalog too large: %d bytes", content_len);
        _hs_status_set_error(HACK_STORE_ERR_BAD_METADATA);
        heap_caps_free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto fetch_fail;
    }

    int read_len = 0;
    int total_read = 0;
    while ((read_len = esp_http_client_read(client, buffer + total_read,
                                            (int)HS_CATALOG_BUFFER_SIZE - total_read - 1)) > 0) {
        if (s_abort_requested) {
            _hs_status_set_error(HACK_STORE_ERR_ABORTED);
            break;
        }
        total_read += read_len;
        if (total_read >= (int)HS_CATALOG_BUFFER_SIZE - 1) {
            _hs_status_set_error(HACK_STORE_ERR_BAD_METADATA);
            break;
        }
    }
    buffer[total_read] = '\0';
    
    ESP_LOGI(TAG, "Total bytes read: %d", total_read);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    hack_store_status_t status;
    _hs_status_snapshot(&status);
    if (read_len < 0 && status.last_error == HACK_STORE_ERR_OK) {
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        _hs_status_snapshot(&status);
    }

    if (status.last_error != HACK_STORE_ERR_OK) {
        heap_caps_free(buffer);
        goto fetch_fail;
    }

    if (total_read <= 0 || status_code != 200) {
        ESP_LOGE(TAG, "Network error or bad status code. read=%d, status=%d", total_read, status_code);
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        heap_caps_free(buffer);
        goto fetch_fail;
    }

    ESP_LOGI(TAG, "Parsing JSON... (first 50 chars: %.50s)", buffer);
    cJSON *root = cJSON_Parse(buffer);
    
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON Parse Error before: %s", error_ptr);
        } else {
            ESP_LOGE(TAG, "JSON Parse Error (unknown)");
        }
        heap_caps_free(buffer);
        _hs_status_set_error(HACK_STORE_ERR_BAD_METADATA);
        goto fetch_fail;
    }
    
    heap_caps_free(buffer);

    _hs_lock();
    hs_catalog_count = 0;
    cJSON *firms = cJSON_GetObjectItem(root, "firmwares");
    if (cJSON_IsArray(firms)) {
        cJSON *f;
        cJSON_ArrayForEach(f, firms) {
            if (hs_catalog_count >= HACK_STORE_MAX_FIRMS) break;
            hack_entry_t *entry = &hs_catalog[hs_catalog_count++];
            memset(entry, 0, sizeof(hack_entry_t));

            cJSON *id = cJSON_GetObjectItem(f, "id");
            cJSON *name = cJSON_GetObjectItem(f, "name");
            cJSON *ver = cJSON_GetObjectItem(f, "version");
            cJSON *desc = cJSON_GetObjectItem(f, "description");
            cJSON *size = cJSON_GetObjectItem(f, "size_bytes");
            cJSON *url = cJSON_GetObjectItem(f, "url");

            if (cJSON_IsString(id)) {
                strncpy(entry->id, id->valuestring, sizeof(entry->id)-1);
                entry->id[sizeof(entry->id)-1] = '\0';
            }
            if (cJSON_IsString(name)) {
                strncpy(entry->name, name->valuestring, sizeof(entry->name)-1);
                entry->name[sizeof(entry->name)-1] = '\0';
            }
            if (cJSON_IsString(ver)) {
                strncpy(entry->version, ver->valuestring, sizeof(entry->version)-1);
                entry->version[sizeof(entry->version)-1] = '\0';
            }
            if (cJSON_IsString(desc)) {
                strncpy(entry->description, desc->valuestring, sizeof(entry->description)-1);
                entry->description[sizeof(entry->description)-1] = '\0';
            }
            if (cJSON_IsNumber(size)) entry->size_bytes = size->valueint;
            if (cJSON_IsString(url)) {
                strncpy(entry->url, url->valuestring, sizeof(entry->url)-1);
                entry->url[sizeof(entry->url)-1] = '\0';
            }
            
            ESP_LOGI(TAG, "Parsed Firmware: %s (%lu bytes, id: %s)", entry->name, (unsigned long)entry->size_bytes, entry->id);
        }
    } else {
        ESP_LOGW(TAG, "No 'firmwares' array found in JSON.");
    }
    int catalog_count = hs_catalog_count;
    _hs_unlock();
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Catalog fetch successful. Total items: %d", catalog_count);
    _hs_status_set_busy(false);
    s_abort_requested = false;
    s_current_task_handle = NULL;
    xEventGroupSetBits(hs_event_group, HS_EVT_CATALOG_DONE);
    vTaskDelete(NULL);
    return;

fetch_fail:
    _hs_status_set_busy(false);
    s_abort_requested = false;
    s_current_task_handle = NULL;
    xEventGroupSetBits(hs_event_group, HS_EVT_CATALOG_FAIL);
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------------------
 * OTA Streaming Task
 * -------------------------------------------------------------------------- */
static void _ota_download_task(void *pvParameters) {
    const char *id = (const char*)pvParameters;
    _hs_status_reset_transfer(id);

    /*
     * FIX: Copy entry URL under lock so we don't use a pointer into hs_catalog[]
     * after releasing the mutex (another task could refresh the catalog and
     * overwrite the entry).
     */
    char url_buf[256] = {0};
    _hs_lock();
    hack_entry_t *entry = _find_entry(id);
    if (entry) {
        strncpy(url_buf, entry->url, sizeof(url_buf) - 1);
        url_buf[sizeof(url_buf) - 1] = '\0';
    }
    _hs_unlock();

    if (!entry || !_url_is_valid(url_buf)) {
        ESP_LOGE(TAG, "Invalid or empty URL for id='%s': '%s'", id, url_buf);
        ESP_LOGE(TAG, "Update firmware_catalog.json on GitHub with a full https:// URL.");
        _hs_status_set_error(HACK_STORE_ERR_BAD_METADATA);
        goto dl_fail;
    }

    if (!wifi_mgr_is_connected()) {
        _hs_status_set_error(HACK_STORE_ERR_NO_WIFI);
        goto dl_fail;
    }

    // 1. Find OTA_1 partition explicitly
    const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (!ota1) {
        ESP_LOGE(TAG, "ota_1 partition not found");
        _hs_status_set_error(HACK_STORE_ERR_PARTITION);
        goto dl_fail;
    }

    // 2. Setup HTTP Client
    esp_http_client_config_t config = {
        .url = url_buf,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HS_HTTP_TIMEOUT_MS,
        .event_handler = _http_event_handle,
        .max_redirection_count = 3, // GitHub releases redirect to AWS
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char *buffer = heap_caps_malloc(HS_DL_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        _hs_status_set_error(HACK_STORE_ERR_FLASHING);
        esp_http_client_cleanup(client);
        goto dl_fail;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        heap_caps_free(buffer);
        esp_http_client_cleanup(client);
        goto dl_fail;
    }
    
    esp_http_client_fetch_headers(client);

    // 3. Begin OTA
    esp_ota_handle_t update_handle = 0;
    if (esp_ota_begin(ota1, OTA_WITH_SEQUENTIAL_WRITES, &update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        _hs_status_set_error(HACK_STORE_ERR_FLASHING);
        heap_caps_free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto dl_fail;
    }

    // 4. Stream data
    int read_len;
    while ((read_len = esp_http_client_read(client, buffer, HS_DL_CHUNK_SIZE)) > 0) {
        if (s_abort_requested) {
            ESP_LOGW(TAG, "Aborted cooperatively");
            _hs_status_set_error(HACK_STORE_ERR_ABORTED);
            esp_ota_abort(update_handle);
            break;
        }
        if (!wifi_mgr_is_connected()) {
            _hs_status_set_error(HACK_STORE_ERR_NO_WIFI);
            esp_ota_abort(update_handle);
            break;
        }
        
        if (esp_ota_write(update_handle, buffer, read_len) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed");
            _hs_status_set_error(HACK_STORE_ERR_FLASHING);
            esp_ota_abort(update_handle);
            break;
        }
    }

    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // 5. Finalize
    hack_store_status_t status;
    _hs_status_snapshot(&status);
    if (status.last_error == HACK_STORE_ERR_OK && read_len >= 0) {
        if (esp_ota_end(update_handle) == ESP_OK) {
            if (esp_ota_set_boot_partition(ota1) == ESP_OK) {
                _save_installed_id(id);
                ESP_LOGI(TAG, "OTA Success, restarting to new firmware");
                _set_boot_allow_ota1_and_restart();
                /* esp_restart() never returns — if it does, self-delete as fallback */
                vTaskDelete(NULL);
            }
        }
        _hs_status_set_error(HACK_STORE_ERR_FLASHING);
    }

dl_fail:
    _hs_status_set_busy(false);
    s_abort_requested = false;
    s_current_task_handle = NULL;
    xEventGroupSetBits(hs_event_group, HS_EVT_DL_FAIL);
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
hack_store_err_t hack_store_init(void) {
    memset(&hs_status, 0, sizeof(hs_status));
    if (hs_event_group == NULL) {
        hs_event_group = xEventGroupCreate();
        configASSERT(hs_event_group != NULL);
    }
    if (hs_mutex == NULL) {
        hs_mutex = xSemaphoreCreateMutex();
    }
    configASSERT(hs_mutex != NULL);
    _load_installed_id();
    ESP_LOGI(TAG, "HackStore initialized. Installed ID: '%s'", hs_installed_id);
    ESP_LOGI(TAG, "Catalog URL: %s", HS_CATALOG_URL);
    ESP_LOGI(TAG, "Free heap: %lu B, Free PSRAM: %lu B",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return HACK_STORE_ERR_OK;
}

void hack_store_get_status(hack_store_status_t* out) {
    _hs_status_snapshot(out);
}

int hack_store_get_catalog(hack_entry_t* out, int max_count) {
    if (!out || max_count <= 0) return 0;
    _hs_lock();
    int count = hs_catalog_count;
    if (count > max_count) count = max_count;
    memcpy(out, hs_catalog, count * sizeof(hack_entry_t));
    _hs_unlock();
    return count;
}

bool hack_store_get_installed_id(char* id_out, size_t len) {
    if (!id_out || len == 0) return false;
    _hs_lock();
    bool has_id = strlen(hs_installed_id) > 0;
    if (has_id) {
        strncpy(id_out, hs_installed_id, len - 1);
        id_out[len - 1] = '\0';
    }
    _hs_unlock();
    if (has_id) {
        return true;
    }
    return false;
}

hack_store_err_t hack_store_refresh_catalog(void) {
    hack_store_status_t status;
    _hs_status_snapshot(&status);
    if (status.is_busy) {
        ESP_LOGW(TAG, "refresh_catalog: already busy, skipping");
        return HACK_STORE_ERR_ABORTED;
    }

    ESP_LOGI(TAG, "refresh_catalog: starting fetch task...");
    xEventGroupClearBits(hs_event_group, HS_EVT_CATALOG_DONE | HS_EVT_CATALOG_FAIL);
    s_abort_requested = false;
    _hs_status_set_busy(true);
    _hs_status_set_error(HACK_STORE_ERR_OK);

    /*
     * Stack budget: TLS handshake (mbedtls) alone needs ~6KB. Add HTTP client
     * locals + cJSON parse stack → 12KB is the minimum safe size.
     * Pinned to Core 0 alongside the TCP/IP stack for best performance.
     */
    BaseType_t rc = xTaskCreatePinnedToCore(
        _cloud_fetch_task, "hs_cat",
        12288,          /* 12KB — TLS + HTTP + cJSON headroom */
        NULL, 5,
        &s_current_task_handle, 0);

    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hs_cat task (out of memory?)");
        s_current_task_handle = NULL;
        _hs_status_set_busy(false);
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        return HACK_STORE_ERR_NETWORK;
    }

    /*
     * Non-blocking: return immediately. The caller (UI task) must poll
     * hack_store_get_status() to detect completion. This avoids the need
     * for a wrapper task and eliminates all task-priority race conditions.
     */
    return HACK_STORE_ERR_OK;
}

hack_store_err_t hack_store_install(const char* id) {
    if (!id || id[0] == '\0') return HACK_STORE_ERR_BAD_METADATA;
    
    s_abort_requested = false;
    
    char installed_id[sizeof(hs_installed_id)] = {0};
    bool is_installed = hack_store_get_installed_id(installed_id, sizeof(installed_id));
    if (is_installed && strcmp(installed_id, id) == 0) {
        const esp_partition_t *ota1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
        if (ota1 && esp_ota_set_boot_partition(ota1) == ESP_OK) {
            ESP_LOGI(TAG, "Already installed. Rebooting to %s", id);
            _set_boot_allow_ota1_and_restart();
        }
        return HACK_STORE_ERR_PARTITION;
    }

    hack_store_status_t status;
    _hs_status_snapshot(&status);
    if (status.is_busy) return HACK_STORE_ERR_ABORTED;

    ESP_LOGI(TAG, "install: starting OTA download for '%s'", id);
    xEventGroupClearBits(hs_event_group, HS_EVT_DL_DONE | HS_EVT_DL_FAIL);
    s_abort_requested = false;
    _hs_status_set_busy(true);
    _hs_status_reset_transfer(id);

    /*
     * FIX: Copy id into a static buffer so the OTA task has a stable pointer.
     * The caller's `id` pointer (from App_HackStore::_install_id_buf) might be
     * valid, but we also protect against direct API callers whose buffer may
     * go out of scope.
     */
    strncpy(s_ota_id_buf, id, sizeof(s_ota_id_buf) - 1);
    s_ota_id_buf[sizeof(s_ota_id_buf) - 1] = '\0';
    s_ota_id_buf[sizeof(s_ota_id_buf) - 1] = '\0';

    /*
     * OTA task: 12KB stack — esp_ota_write uses internal buffers and OTA
     * validation adds stack depth on top of the HTTP read loop.
     * Pinned to Core 0 alongside the TCP/IP stack.
     */
    BaseType_t rc = xTaskCreatePinnedToCore(
        _ota_download_task, "hs_dl",
        12288, (void*)s_ota_id_buf, 5,
        &s_current_task_handle, 0);

    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hs_dl task (out of memory?)");
        s_current_task_handle = NULL;
        _hs_status_set_busy(false);
        _hs_status_set_error(HACK_STORE_ERR_NETWORK);
        return HACK_STORE_ERR_NETWORK;
    }

    return HACK_STORE_ERR_OK; /* Non-blocking — UI polls hack_store_get_status() */
}

hack_store_err_t hack_store_abort(void) {
    hack_store_status_t status;
    _hs_status_snapshot(&status);
    if (!status.is_busy || !s_current_task_handle) {
        return HACK_STORE_ERR_OK;
    }

    s_abort_requested = true;
    _hs_status_set_error(HACK_STORE_ERR_ABORTED);
    return HACK_STORE_ERR_OK;
}
