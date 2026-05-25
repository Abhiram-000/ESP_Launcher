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

#define CATALOG_URL "https://raw.githubusercontent.com/Abhiram9641/ESP_HackStore/main/catalog.json"
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

    if (!wifi_mgr_is_connected()) {
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
        ESP_LOGE(TAG, "Failed to alloc buffer for catalog");
        esp_http_client_cleanup(client);
        hs_status.last_error = HACK_STORE_ERR_FLASHING;
        goto fetch_fail;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        hs_status.last_error = HACK_STORE_ERR_NETWORK;
        free(buffer);
        esp_http_client_cleanup(client);
        goto fetch_fail;
    }

    esp_http_client_fetch_headers(client);
    int read_len = 0;
    int total_read = 0;
    while ((read_len = esp_http_client_read(client, buffer + total_read, 16384 - total_read - 1)) > 0) {
        total_read += read_len;
    }
    buffer[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read <= 0) {
        hs_status.last_error = HACK_STORE_ERR_NETWORK;
        free(buffer);
        goto fetch_fail;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        hs_status.last_error = HACK_STORE_ERR_BAD_METADATA;
        goto fetch_fail;
    }

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
        }
    }
    cJSON_Delete(root);

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
    }
    _load_installed_id();
    ESP_LOGI(TAG, "HackStore initialized. Installed ID: %s", hs_installed_id);
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
    if (hs_status.is_busy) return HACK_STORE_ERR_ABORTED;
    
    xEventGroupClearBits(hs_event_group, HS_EVT_CATALOG_DONE | HS_EVT_CATALOG_FAIL);
    xTaskCreatePinnedToCore(_cloud_fetch_task, "hs_cat", 8192, NULL, 5, &current_task_handle, 0);
    
    EventBits_t bits = xEventGroupWaitBits(hs_event_group, HS_EVT_CATALOG_DONE | HS_EVT_CATALOG_FAIL, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (bits & HS_EVT_CATALOG_DONE) return HACK_STORE_ERR_OK;
    return hs_status.last_error;
}

hack_store_err_t hack_store_install(const char* id) {
    // If it's already installed, just boot it!
    if (strcmp(hs_installed_id, id) == 0) {
        const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
        if (ota1 && esp_ota_set_boot_partition(ota1) == ESP_OK) {
            ESP_LOGI(TAG, "Already installed. Rebooting to %s", id);
            esp_restart();
        }
        return HACK_STORE_ERR_PARTITION;
    }

    if (hs_status.is_busy) return HACK_STORE_ERR_ABORTED;
    
    xEventGroupClearBits(hs_event_group, HS_EVT_DL_DONE | HS_EVT_DL_FAIL);
    xTaskCreatePinnedToCore(_ota_download_task, "hs_dl", 8192, (void*)id, 5, &current_task_handle, 0);
    
    return HACK_STORE_ERR_OK; // Non-blocking return for UI to poll
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
