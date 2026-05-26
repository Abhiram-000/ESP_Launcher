/*
 * hack_store.c — Backend service for downloading and flashing custom firmwares
 */
#include "hack_store.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG = "HackStore";

#define CATALOG_URL "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/firmware_catalog.json"
#define HTTP_TIMEOUT_MS 10000
#define DL_CHUNK_SIZE 8192

static hack_store_status_t hs_status;
static hack_entry_t hs_catalog[HACK_STORE_MAX_FIRMS];
static int hs_catalog_count = 0;
static char hs_installed_id[32] = {0};

static EventGroupHandle_t hs_event_group = NULL;
#define HS_EVT_CATALOG_DONE (1 << 0)
#define HS_EVT_CATALOG_FAIL (1 << 1)
#define HS_EVT_DL_DONE      (1 << 2)
#define HS_EVT_DL_FAIL      (1 << 3)

static TaskHandle_t current_task_handle = NULL;
static volatile bool s_abort_requested = false;

/* --------------------------------------------------------------------------
 * Internal NVS Helpers
 * -------------------------------------------------------------------------- */
static void _load_installed_id(void) {
    nvs_handle_t h;
    hs_installed_id[0] = '\0';
    if (nvs_open("hack_store", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(hs_installed_id);
        nvs_get_str(h, "installed_id", hs_installed_id, &len);
        nvs_close(h);
    }
}

static void _save_installed_id(const char* id) {
    nvs_handle_t h;
    if (nvs_open("hack_store", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "installed_id", id);
        nvs_commit(h);
        nvs_close(h);
    }
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
                hs_status.bytes_total = atoi(evt->header_value);
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (hs_status.bytes_total > 0) {
                hs_status.bytes_done += evt->data_len;
                hs_status.progress_pct = (hs_status.bytes_done * 100) / hs_status.bytes_total;
            }
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
    hs_status.is_busy = true;
    hs_status.last_error = HACK_STORE_ERR_OK;

    ESP_LOGI(TAG, "Starting cloud fetch from: %s", CATALOG_URL);
    if (!wifi_mgr_is_connected()) {
        ESP_LOGE(TAG, "Cloud fetch failed: No Wi-Fi connection");
        hs_status.last_error = HACK_STORE_ERR_NO_WIFI;
        goto fetch_fail;
    }

    esp_http_client_config_t config = {
        .url = CATALOG_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char *buffer = heap_caps_malloc(16 * 1024, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to alloc 16KB SPIRAM buffer for catalog");
        esp_http_client_cleanup(client);
        hs_status.last_error = HACK_STORE_ERR_FLASHING;
        goto fetch_fail;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        hs_status.last_error = HACK_STORE_ERR_NETWORK;
        free(buffer);
        esp_http_client_cleanup(client);
        goto fetch_fail;
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP fetch headers complete, status code: %d", status_code);

    int read_len = 0;
    int total_read = 0;
    while ((read_len = esp_http_client_read(client, buffer + total_read, 16384 - total_read - 1)) > 0) {
        total_read += read_len;
    }
    buffer[total_read] = '\0';
    
    ESP_LOGI(TAG, "Total bytes read: %d", total_read);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read <= 0 || status_code != 200) {
        ESP_LOGE(TAG, "Network error or bad status code. read=%d, status=%d", total_read, status_code);
        hs_status.last_error = HACK_STORE_ERR_NETWORK;
        free(buffer);
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
        free(buffer);
        hs_status.last_error = HACK_STORE_ERR_BAD_METADATA;
        goto fetch_fail;
    }
    
    free(buffer);

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

            if (cJSON_IsString(id)) strncpy(entry->id, id->valuestring, sizeof(entry->id)-1);
            if (cJSON_IsString(name)) strncpy(entry->name, name->valuestring, sizeof(entry->name)-1);
            if (cJSON_IsString(ver)) strncpy(entry->version, ver->valuestring, sizeof(entry->version)-1);
            if (cJSON_IsString(desc)) strncpy(entry->description, desc->valuestring, sizeof(entry->description)-1);
            if (cJSON_IsNumber(size)) entry->size_bytes = size->valueint;
            if (cJSON_IsString(url)) strncpy(entry->url, url->valuestring, sizeof(entry->url)-1);
            
            ESP_LOGI(TAG, "Parsed Firmware: %s (%lu bytes, id: %s)", entry->name, (unsigned long)entry->size_bytes, entry->id);
        }
    } else {
        ESP_LOGW(TAG, "No 'firmwares' array found in JSON.");
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Catalog fetch successful. Total items: %d", hs_catalog_count);
    hs_status.is_busy = false;
    current_task_handle = NULL;
    xEventGroupSetBits(hs_event_group, HS_EVT_CATALOG_DONE);
    vTaskDelete(NULL);
    return;

fetch_fail:
    hs_status.is_busy = false;
    current_task_handle = NULL;
    xEventGroupSetBits(hs_event_group, HS_EVT_CATALOG_FAIL);
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------------------
 * OTA Streaming Task
 * -------------------------------------------------------------------------- */
static void _ota_download_task(void *pvParameters) {
    const char *id = (const char*)pvParameters;
    hs_status.is_busy = true;
    hs_status.last_error = HACK_STORE_ERR_OK;
    hs_status.progress_pct = 0;
    hs_status.bytes_done = 0;
    hs_status.bytes_total = 0;
    strncpy(hs_status.active_id, id, sizeof(hs_status.active_id)-1);

    hack_entry_t *entry = _find_entry(id);
    if (!entry) {
        hs_status.last_error = HACK_STORE_ERR_BAD_METADATA;
        goto dl_fail;
    }

    if (!wifi_mgr_is_connected()) {
        hs_status.last_error = HACK_STORE_ERR_NO_WIFI;
        goto dl_fail;
    }

    // 1. Find OTA_1 partition explicitly
    const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (!ota1) {
        ESP_LOGE(TAG, "ota_1 partition not found");
        hs_status.last_error = HACK_STORE_ERR_PARTITION;
        goto dl_fail;
    }

    // 2. Setup HTTP Client
    esp_http_client_config_t config = {
        .url = entry->url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = _http_event_handle,
        .max_redirection_count = 3, // GitHub releases redirect to AWS
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char *buffer = heap_caps_malloc(DL_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        hs_status.last_error = HACK_STORE_ERR_FLASHING;
        esp_http_client_cleanup(client);
        goto dl_fail;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        hs_status.last_error = HACK_STORE_ERR_NETWORK;
        free(buffer);
        esp_http_client_cleanup(client);
        goto dl_fail;
    }
    
    esp_http_client_fetch_headers(client);

    // 3. Begin OTA
    esp_ota_handle_t update_handle = 0;
    if (esp_ota_begin(ota1, OTA_WITH_SEQUENTIAL_WRITES, &update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        hs_status.last_error = HACK_STORE_ERR_FLASHING;
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto dl_fail;
    }

    // 4. Stream data
    int read_len;
    while ((read_len = esp_http_client_read(client, buffer, DL_CHUNK_SIZE)) > 0) {
        if (s_abort_requested) {
            ESP_LOGW(TAG, "Aborted cooperatively");
            hs_status.last_error = HACK_STORE_ERR_ABORTED;
            esp_ota_abort(update_handle);
            break;
        }
        if (!wifi_mgr_is_connected()) {
            hs_status.last_error = HACK_STORE_ERR_NO_WIFI;
            esp_ota_abort(update_handle);
            break;
        }
        
        if (esp_ota_write(update_handle, buffer, read_len) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed");
            hs_status.last_error = HACK_STORE_ERR_FLASHING;
            esp_ota_abort(update_handle);
            break;
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // 5. Finalize
    if (hs_status.last_error == HACK_STORE_ERR_OK && read_len >= 0) {
        if (esp_ota_end(update_handle) == ESP_OK) {
            if (esp_ota_set_boot_partition(ota1) == ESP_OK) {
                _save_installed_id(entry->id);
                ESP_LOGI(TAG, "OTA Success, restarting to new firmware");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart(); // Boot into Marauder/Bruce immediately
                while(1) { vTaskDelay(10); } // Unreachable
            }
        }
        hs_status.last_error = HACK_STORE_ERR_FLASHING;
    }

dl_fail:
    hs_status.is_busy = false;
    current_task_handle = NULL;
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
    _load_installed_id();
    ESP_LOGI(TAG, "HackStore initialized. Installed ID: '%s'", hs_installed_id);
    ESP_LOGI(TAG, "Catalog URL: %s", CATALOG_URL);
    ESP_LOGI(TAG, "Free heap: %lu B, Free PSRAM: %lu B",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return HACK_STORE_ERR_OK;
}

const hack_store_status_t* hack_store_get_status(void) {
    return &hs_status;
}

int hack_store_get_catalog(hack_entry_t* out, int max_count) {
    int count = hs_catalog_count;
    if (count > max_count) count = max_count;
    memcpy(out, hs_catalog, count * sizeof(hack_entry_t));
    return count;
}

bool hack_store_get_installed_id(char* id_out, size_t len) {
    if (strlen(hs_installed_id) > 0) {
        strncpy(id_out, hs_installed_id, len);
        return true;
    }
    return false;
}

hack_store_err_t hack_store_refresh_catalog(void) {
    if (hs_status.is_busy) {
        ESP_LOGW(TAG, "refresh_catalog: already busy, skipping");
        return HACK_STORE_ERR_ABORTED;
    }

    ESP_LOGI(TAG, "refresh_catalog: starting fetch task...");
    xEventGroupClearBits(hs_event_group, HS_EVT_CATALOG_DONE | HS_EVT_CATALOG_FAIL);

    /*
     * Stack budget: TLS handshake (mbedtls) alone needs ~6KB. Add HTTP client
     * locals + cJSON parse stack → 12KB is the minimum safe size.
     * Pinned to Core 0 alongside the TCP/IP stack for best performance.
     */
    BaseType_t rc = xTaskCreatePinnedToCore(
        _cloud_fetch_task, "hs_cat",
        12288,          /* 12KB — TLS + HTTP + cJSON headroom */
        NULL, 5,
        &current_task_handle, 0);

    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hs_cat task (out of memory?)");
        hs_status.is_busy = false;
        return HACK_STORE_ERR_NETWORK;
    }

    /*
     * Wait up to 15 seconds.  portMAX_DELAY here is FORBIDDEN — it would
     * starve the Task Watchdog on the calling task (hs_fetch_ui).
     */
    const TickType_t CATALOG_TIMEOUT = pdMS_TO_TICKS(15000);
    EventBits_t bits = xEventGroupWaitBits(
        hs_event_group,
        HS_EVT_CATALOG_DONE | HS_EVT_CATALOG_FAIL,
        pdTRUE, pdFALSE,
        CATALOG_TIMEOUT);

    if (bits == 0) {
        /* Timed out — task is still running, flag it for cooperative abort */
        ESP_LOGE(TAG, "Catalog fetch timed out after 15s!");
        s_abort_requested = true;
        hs_status.last_error = HACK_STORE_ERR_NETWORK;
        hs_status.is_busy    = false;
        return HACK_STORE_ERR_NETWORK;
    }

    if (bits & HS_EVT_CATALOG_DONE) {
        ESP_LOGI(TAG, "Catalog fetch complete. %d items.", hs_catalog_count);
        return HACK_STORE_ERR_OK;
    }
    ESP_LOGE(TAG, "Catalog fetch failed with error: %d", (int)hs_status.last_error);
    return hs_status.last_error;
}

hack_store_err_t hack_store_install(const char* id) {
    // If it's already installed, just boot it!
    if (strcmp(hs_installed_id, id) == 0) {
        const esp_partition_t *ota1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
        if (ota1 && esp_ota_set_boot_partition(ota1) == ESP_OK) {
            ESP_LOGI(TAG, "Already installed. Rebooting to %s", id);
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
        return HACK_STORE_ERR_PARTITION;
    }

    if (hs_status.is_busy) return HACK_STORE_ERR_ABORTED;

    ESP_LOGI(TAG, "install: starting OTA download for '%s'", id);
    xEventGroupClearBits(hs_event_group, HS_EVT_DL_DONE | HS_EVT_DL_FAIL);

    /*
     * OTA task: 12KB stack — esp_ota_write uses internal buffers and OTA
     * validation adds stack depth on top of the HTTP read loop.
     * Pinned to Core 0 alongside the TCP/IP stack.
     */
    BaseType_t rc = xTaskCreatePinnedToCore(
        _ota_download_task, "hs_dl",
        12288, (void*)id, 5,
        &current_task_handle, 0);

    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hs_dl task (out of memory?)");
        return HACK_STORE_ERR_NETWORK;
    }

    return HACK_STORE_ERR_OK; /* Non-blocking — UI polls hack_store_get_status() */
}

hack_store_err_t hack_store_abort(void) {
    if (!hs_status.is_busy || !current_task_handle) {
        return HACK_STORE_ERR_OK;
    }

    s_abort_requested = true;
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (current_task_handle == NULL) break;
    }

    if (current_task_handle != NULL) {
        vTaskDelete(current_task_handle);
        current_task_handle = NULL;
    }

    s_abort_requested = false;
    hs_status.is_busy = false;
    hs_status.last_error = HACK_STORE_ERR_ABORTED;
    return HACK_STORE_ERR_OK;
}
