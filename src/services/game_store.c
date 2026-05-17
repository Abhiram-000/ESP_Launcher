#include "game_store.h"
#include "sys_config.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_rom_crc.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>


static const char* TAG = "GameStore";

static game_store_status_t gs_status;
static game_entry_t local_catalog[GAME_STORE_MAX_GAMES];
static int local_catalog_count = 0;

static EventGroupHandle_t gs_event_group = NULL;
#define GS_EVT_CATALOG_DONE (1 << 0)
#define GS_EVT_CATALOG_FAIL (1 << 1)
#define GS_EVT_DL_DONE      (1 << 2)
#define GS_EVT_DL_FAIL      (1 << 3)
#define GS_EVT_ABORT        (1 << 4)

static TaskHandle_t current_task_handle = NULL;

/* --------------------------------------------------------------------------
 * LittleFS Lifecycle
 * -------------------------------------------------------------------------- */
static game_store_err_t _gs_fs_mount(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = GAME_STORE_MOUNT_POINT,
        .partition_label = GAME_STORE_PARTITION_LABEL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount or format SPIFFS: %s", esp_err_to_name(ret));
        return GAME_STORE_ERR_FS_ERROR;
    }

    ESP_LOGI(TAG, "SPIFFS mounted at %s", GAME_STORE_MOUNT_POINT);
    return GAME_STORE_ERR_OK;
}

static void _gs_ensure_dirs(void) {
    // SPIFFS uses a flat namespace — no real mkdir needed.
    // Files with path-like names (e.g. /games/roms/x.nes) work directly.
    // We still attempt mkdir so the code works if the FS is ever swapped to LittleFS.
    struct stat st;
    if (stat(GAME_STORE_MOUNT_POINT, &st) != 0) {
        mkdir(GAME_STORE_MOUNT_POINT, 0777); // no-op on SPIFFS, fine
    }
    if (stat(GAME_STORE_ROMS_DIR, &st) != 0) {
        mkdir(GAME_STORE_ROMS_DIR, 0777); // no-op on SPIFFS, fine
    }
}

static void _gs_cleanup_temp_files(void) {
    DIR *dir = opendir(GAME_STORE_ROMS_DIR);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".tmp") == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", GAME_STORE_ROMS_DIR, entry->d_name);
            ESP_LOGW(TAG, "Cleaning up temp file: %s", path);
            unlink(path);
        }
    }
    closedir(dir);
}

/* --------------------------------------------------------------------------
 * Local Catalog Management
 * -------------------------------------------------------------------------- */
static void _gs_local_catalog_load(void) {
    local_catalog_count = 0;
    
    FILE *f = fopen(GAME_STORE_CATALOG_LOCAL, "r");
    if (!f) {
        ESP_LOGI(TAG, "No local catalog found.");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return;
    }

    char *json_data = malloc(size + 1);
    if (!json_data) {
        fclose(f);
        return;
    }

    fread(json_data, 1, size, f);
    json_data[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_data);
    free(json_data);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse local catalog");
        return;
    }

    cJSON *games = cJSON_GetObjectItem(root, "games");
    if (cJSON_IsArray(games)) {
        cJSON *game;
        cJSON_ArrayForEach(game, games) {
            if (local_catalog_count >= GAME_STORE_MAX_GAMES) break;
            
            game_entry_t *entry = &local_catalog[local_catalog_count];
            memset(entry, 0, sizeof(game_entry_t));
            
            cJSON *id = cJSON_GetObjectItem(game, "id");
            cJSON *name = cJSON_GetObjectItem(game, "name");
            cJSON *version = cJSON_GetObjectItem(game, "version");
            cJSON *genre = cJSON_GetObjectItem(game, "genre");
            cJSON *desc = cJSON_GetObjectItem(game, "description");
            cJSON *size_bytes = cJSON_GetObjectItem(game, "size_bytes");
            cJSON *crc32 = cJSON_GetObjectItem(game, "crc32");
            cJSON *url = cJSON_GetObjectItem(game, "url");
            cJSON *mapper = cJSON_GetObjectItem(game, "mapper");

            if (cJSON_IsString(id)) strncpy(entry->id, id->valuestring, sizeof(entry->id)-1);
            if (cJSON_IsString(name)) strncpy(entry->name, name->valuestring, sizeof(entry->name)-1);
            if (cJSON_IsString(version)) strncpy(entry->version, version->valuestring, sizeof(entry->version)-1);
            if (cJSON_IsString(genre)) strncpy(entry->genre, genre->valuestring, sizeof(entry->genre)-1);
            if (cJSON_IsString(desc)) strncpy(entry->description, desc->valuestring, sizeof(entry->description)-1);
            if (cJSON_IsNumber(size_bytes)) entry->size_bytes = size_bytes->valueint;
            if (cJSON_IsString(crc32)) entry->crc32 = strtoul(crc32->valuestring, NULL, 16);
            if (cJSON_IsString(url)) strncpy(entry->url, url->valuestring, sizeof(entry->url)-1);
            if (cJSON_IsNumber(mapper)) entry->mapper = mapper->valueint;
            
            entry->is_installed = true; // Local means installed
            entry->has_update = false;
            
            local_catalog_count++;
        }
    }
    cJSON_Delete(root);
}

static game_store_err_t _gs_local_catalog_save(game_entry_t* catalog, int count) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    
    cJSON *games = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "games", games);

    for (int i = 0; i < count; i++) {
        if (!catalog[i].is_installed) continue; // Only save installed games

        cJSON *game = cJSON_CreateObject();
        cJSON_AddStringToObject(game, "id", catalog[i].id);
        cJSON_AddStringToObject(game, "name", catalog[i].name);
        cJSON_AddStringToObject(game, "version", catalog[i].version);
        cJSON_AddStringToObject(game, "genre", catalog[i].genre);
        cJSON_AddStringToObject(game, "description", catalog[i].description);
        cJSON_AddNumberToObject(game, "size_bytes", catalog[i].size_bytes);
        
        char crc_str[9];
        snprintf(crc_str, sizeof(crc_str), "%08lX", catalog[i].crc32);
        cJSON_AddStringToObject(game, "crc32", crc_str);
        
        cJSON_AddStringToObject(game, "url", catalog[i].url);
        cJSON_AddNumberToObject(game, "mapper", catalog[i].mapper);
        
        cJSON_AddItemToArray(games, game);
    }

    char *json_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_data) return GAME_STORE_ERR_FS_ERROR;

    // Atomic write
    char temp_path[128];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", GAME_STORE_CATALOG_LOCAL);
    
    FILE *f = fopen(temp_path, "w");
    if (!f) {
        free(json_data);
        return GAME_STORE_ERR_FS_ERROR;
    }
    
    fwrite(json_data, 1, strlen(json_data), f);
    fclose(f);
    free(json_data);
    
    if (rename(temp_path, GAME_STORE_CATALOG_LOCAL) != 0) {
        unlink(temp_path);
        return GAME_STORE_ERR_FS_ERROR;
    }

    return GAME_STORE_ERR_OK;
}

static game_entry_t* _gs_local_catalog_find(const char* id);  /* forward decl */

/* Scan /games/roms/ for any .nes files not already in local_catalog.
 * Registers them with is_installed=true so they show up in MyGames immediately,
 * even if catalog.json is missing or was flashed before the ROM was added. */
static void _gs_autodiscover_roms(void) {
    DIR *dir = opendir(GAME_STORE_ROMS_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Auto-discover: cannot open %s", GAME_STORE_ROMS_DIR);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *fname = entry->d_name;
        size_t len = strlen(fname);

        /* Only handle .nes files, skip .tmp */
        if (len <= 4 || strcmp(fname + len - 4, ".nes") != 0) continue;

        /* Derive ID: strip the .nes suffix */
        char id[32];
        size_t id_len = (len - 4 < sizeof(id) - 1) ? (len - 4) : (sizeof(id) - 1);
        strncpy(id, fname, id_len);
        id[id_len] = '\0';

        /* Already in catalog — skip */
        if (_gs_local_catalog_find(id)) continue;

        /* Add a new entry */
        if (local_catalog_count >= GAME_STORE_MAX_GAMES) break;
        game_entry_t *e = &local_catalog[local_catalog_count++];
        memset(e, 0, sizeof(game_entry_t));
        strncpy(e->id,      id, sizeof(e->id) - 1);
        strncpy(e->name,    id, sizeof(e->name) - 1);  /* ID as display name */
        strncpy(e->version, "1.0", sizeof(e->version) - 1);
        strncpy(e->genre,   "Unknown", sizeof(e->genre) - 1);
        e->is_installed = true;

        /* Fill size from stat */
        char full_path[280];
        snprintf(full_path, sizeof(full_path), "%s/%s", GAME_STORE_ROMS_DIR, fname);
        struct stat st;
        if (stat(full_path, &st) == 0) e->size_bytes = (uint32_t)st.st_size;

        ESP_LOGI(TAG, "Auto-discovered ROM: %s (%lu bytes)", id, (unsigned long)e->size_bytes);
    }
    closedir(dir);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
game_store_err_t game_store_init(void) {
    memset(&gs_status, 0, sizeof(gs_status));
    if (gs_event_group == NULL) {
        gs_event_group = xEventGroupCreate();
    }
    
    game_store_err_t err = _gs_fs_mount();
    if (err != GAME_STORE_ERR_OK) return err;

    _gs_ensure_dirs();
    _gs_cleanup_temp_files();
    _gs_local_catalog_load();    /* Load catalog.json first */
    _gs_autodiscover_roms();     /* Then add any ROMs not in catalog */

    ESP_LOGI(TAG, "Game Store ready — %d game(s) in catalog", local_catalog_count);
    return GAME_STORE_ERR_OK;
}


const game_store_status_t* game_store_get_status(void) {
    return &gs_status;
}

/* Actual definition (forward-declared above for autodiscover) */
static game_entry_t* _gs_local_catalog_find(const char* id) {
    for (int i = 0; i < local_catalog_count; i++) {
        if (strcmp(local_catalog[i].id, id) == 0) {
            return &local_catalog[i];
        }
    }
    return NULL;
}

int game_store_get_catalog(game_entry_t* out, int max_count) {
    int count = local_catalog_count;
    if (count > max_count) count = max_count;
    memcpy(out, local_catalog, count * sizeof(game_entry_t));
    return count;
}

bool game_store_get_rom_path(const char* id, char* path_out, size_t len) {
    if (!_gs_local_catalog_find(id)) return false;
    snprintf(path_out, len, "%s/%s.nes", GAME_STORE_ROMS_DIR, id);
    struct stat st;
    return (stat(path_out, &st) == 0);
}

size_t game_store_get_free_bytes(void) {
    size_t total, used;
    if (esp_spiffs_info(GAME_STORE_PARTITION_LABEL, &total, &used) == ESP_OK) {
        return total - used;
    }
    return 0;
}

size_t game_store_get_total_bytes(void) {
    size_t total, used;
    if (esp_spiffs_info(GAME_STORE_PARTITION_LABEL, &total, &used) == ESP_OK) {
        return total;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * HTTP Event Handler for Download Progress
 * -------------------------------------------------------------------------- */
static esp_err_t _http_event_handle(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            if (strcasecmp(evt->header_key, "Content-Length") == 0) {
                gs_status.bytes_total = atoi(evt->header_value);
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (gs_status.bytes_total > 0) {
                gs_status.bytes_done += evt->data_len;
                gs_status.progress_pct = (gs_status.bytes_done * 100) / gs_status.bytes_total;
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
static void _gs_cloud_fetch_task(void *pvParameters) {
    gs_status.is_busy = true;
    gs_status.last_error = GAME_STORE_ERR_OK;
    
    if (!wifi_mgr_is_connected()) {
        gs_status.last_error = GAME_STORE_ERR_NO_WIFI;
        gs_status.is_busy = false;
        xEventGroupSetBits(gs_event_group, GS_EVT_CATALOG_FAIL);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = GAME_STORE_CLOUD_CATALOG_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = GAME_STORE_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char *buffer = heap_caps_malloc(32 * 1024, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for catalog");
        gs_status.last_error = GAME_STORE_ERR_FS_ERROR;
        esp_http_client_cleanup(client);
        gs_status.is_busy = false;
        xEventGroupSetBits(gs_event_group, GS_EVT_CATALOG_FAIL);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        gs_status.last_error = GAME_STORE_ERR_NETWORK;
        goto fetch_cleanup;
    }

    esp_http_client_fetch_headers(client);
    int read_len = 0;
    int total_read = 0;
    
    while ((read_len = esp_http_client_read(client, buffer + total_read, 32768 - total_read - 1)) > 0) {
        total_read += read_len;
    }
    buffer[total_read] = '\0';
    
    if (total_read <= 0) {
        gs_status.last_error = GAME_STORE_ERR_NETWORK;
        goto fetch_cleanup;
    }

    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        gs_status.last_error = GAME_STORE_ERR_BAD_METADATA;
        goto fetch_cleanup;
    }

    // Merge with local catalog (simplified: just append if not found, or update if found)
    // For a real implementation, you would check has_update here.
    cJSON *games = cJSON_GetObjectItem(root, "games");
    if (cJSON_IsArray(games)) {
        cJSON *game;
        cJSON_ArrayForEach(game, games) {
            cJSON *id_item = cJSON_GetObjectItem(game, "id");
            if (!cJSON_IsString(id_item)) continue;
            
            const char* gid = id_item->valuestring;
            game_entry_t *entry = _gs_local_catalog_find(gid);
            
            // Just a simple parsing logic for demonstration
            if (!entry && local_catalog_count < GAME_STORE_MAX_GAMES) {
                entry = &local_catalog[local_catalog_count++];
                memset(entry, 0, sizeof(game_entry_t));
                strncpy(entry->id, gid, sizeof(entry->id)-1);
                entry->is_installed = false;
            }
            
            if (entry) {
                cJSON *name = cJSON_GetObjectItem(game, "name");
                cJSON *version = cJSON_GetObjectItem(game, "version");
                cJSON *genre = cJSON_GetObjectItem(game, "genre");
                cJSON *desc = cJSON_GetObjectItem(game, "description");
                cJSON *size_bytes = cJSON_GetObjectItem(game, "size_bytes");
                cJSON *crc32 = cJSON_GetObjectItem(game, "crc32");
                cJSON *url = cJSON_GetObjectItem(game, "url");
                cJSON *mapper = cJSON_GetObjectItem(game, "mapper");

                if (cJSON_IsString(name)) strncpy(entry->name, name->valuestring, sizeof(entry->name)-1);
                if (cJSON_IsString(version)) strncpy(entry->version, version->valuestring, sizeof(entry->version)-1);
                if (cJSON_IsString(genre)) strncpy(entry->genre, genre->valuestring, sizeof(entry->genre)-1);
                if (cJSON_IsString(desc)) strncpy(entry->description, desc->valuestring, sizeof(entry->description)-1);
                if (cJSON_IsNumber(size_bytes)) entry->size_bytes = size_bytes->valueint;
                if (cJSON_IsString(crc32)) entry->crc32 = strtoul(crc32->valuestring, NULL, 16);
                if (cJSON_IsString(url)) strncpy(entry->url, url->valuestring, sizeof(entry->url)-1);
                if (cJSON_IsNumber(mapper)) entry->mapper = mapper->valueint;
            }
        }
    }
    cJSON_Delete(root);
    
    // Save merged catalog
    _gs_local_catalog_save(local_catalog, local_catalog_count);
    
    xEventGroupSetBits(gs_event_group, GS_EVT_CATALOG_DONE);

fetch_cleanup:
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    gs_status.is_busy = false;
    if (gs_status.last_error != GAME_STORE_ERR_OK) {
        xEventGroupSetBits(gs_event_group, GS_EVT_CATALOG_FAIL);
    }
    current_task_handle = NULL;
    vTaskDelete(NULL);
}

game_store_err_t game_store_refresh_catalog(void) {
    if (gs_status.is_busy) return GAME_STORE_ERR_ABORTED;
    
    xEventGroupClearBits(gs_event_group, GS_EVT_CATALOG_DONE | GS_EVT_CATALOG_FAIL);
    xTaskCreatePinnedToCore(_gs_cloud_fetch_task, "gs_cat", 8192, NULL, 5, &current_task_handle, 0);
    
    EventBits_t bits = xEventGroupWaitBits(gs_event_group, GS_EVT_CATALOG_DONE | GS_EVT_CATALOG_FAIL, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (bits & GS_EVT_CATALOG_DONE) return GAME_STORE_ERR_OK;
    return gs_status.last_error;
}

/* --------------------------------------------------------------------------
 * ROM Download Task
 * -------------------------------------------------------------------------- */
static void _gs_download_task(void *pvParameters) {
    const char *id = (const char*)pvParameters;
    gs_status.is_busy = true;
    gs_status.last_error = GAME_STORE_ERR_OK;
    gs_status.progress_pct = 0;
    gs_status.bytes_done = 0;
    gs_status.bytes_total = 0;
    strncpy(gs_status.active_game_id, id, sizeof(gs_status.active_game_id)-1);
    
    game_entry_t *entry = _gs_local_catalog_find(id);
    if (!entry) {
        gs_status.last_error = GAME_STORE_ERR_BAD_METADATA;
        goto dl_abort;
    }

    if (!wifi_mgr_is_connected()) {
        gs_status.last_error = GAME_STORE_ERR_NO_WIFI;
        goto dl_abort;
    }

    if (game_store_get_free_bytes() < entry->size_bytes + (entry->size_bytes / 10)) {
        gs_status.last_error = GAME_STORE_ERR_NO_SPACE;
        goto dl_abort;
    }

    {
        char temp_path[128];
        snprintf(temp_path, sizeof(temp_path), "%s/%s.tmp", GAME_STORE_ROMS_DIR, id);
        FILE *f = fopen(temp_path, "wb");
        if (!f) {
            gs_status.last_error = GAME_STORE_ERR_FS_ERROR;
            goto dl_abort;
        }

        esp_http_client_config_t config = {
            .url = entry->url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = GAME_STORE_HTTP_TIMEOUT_MS,
            .event_handler = _http_event_handle,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        char *buffer = heap_caps_malloc(GAME_STORE_DL_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
        if (!buffer) {
            fclose(f);
            unlink(temp_path);
            esp_http_client_cleanup(client);
            gs_status.last_error = GAME_STORE_ERR_FS_ERROR;
            goto dl_abort;
        }

        if (esp_http_client_open(client, 0) != ESP_OK) {
            free(buffer);
            fclose(f);
            unlink(temp_path);
            esp_http_client_cleanup(client);
            gs_status.last_error = GAME_STORE_ERR_NETWORK;
            goto dl_abort;
        }
        
        esp_http_client_fetch_headers(client);
        int read_len;
        uint32_t crc = 0xFFFFFFFF;
        
        while ((read_len = esp_http_client_read(client, buffer, GAME_STORE_DL_CHUNK_SIZE)) > 0) {
            if (!wifi_mgr_is_connected()) {
                gs_status.last_error = GAME_STORE_ERR_NO_WIFI;
                break;
            }
            fwrite(buffer, 1, read_len, f);
            crc = ~esp_rom_crc32_le(~crc, (uint8_t *)buffer, read_len);
        }
        
        fclose(f);
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (gs_status.last_error != GAME_STORE_ERR_OK || read_len < 0) {
            unlink(temp_path);
            if (gs_status.last_error == GAME_STORE_ERR_OK) gs_status.last_error = GAME_STORE_ERR_NETWORK;
            goto dl_abort;
        }

        // Validate CRC32
        if (crc != entry->crc32 && entry->crc32 != 0) {
            unlink(temp_path);
            gs_status.last_error = GAME_STORE_ERR_CORRUPT_ROM;
            goto dl_abort;
        }

        // Rename atomic
        char final_path[128];
        snprintf(final_path, sizeof(final_path), "%s/%s.nes", GAME_STORE_ROMS_DIR, id);
        if (rename(temp_path, final_path) != 0) {
            unlink(temp_path);
            gs_status.last_error = GAME_STORE_ERR_FS_ERROR;
            goto dl_abort;
        }

        entry->is_installed = true;
        _gs_local_catalog_save(local_catalog, local_catalog_count);
        xEventGroupSetBits(gs_event_group, GS_EVT_DL_DONE);
        gs_status.is_busy = false;
        current_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

dl_abort:
    gs_status.is_busy = false;
    xEventGroupSetBits(gs_event_group, GS_EVT_DL_FAIL);
    current_task_handle = NULL;
    vTaskDelete(NULL);
}

game_store_err_t game_store_install(const char* id) {
    if (gs_status.is_busy) return GAME_STORE_ERR_ABORTED;
    
    xEventGroupClearBits(gs_event_group, GS_EVT_DL_DONE | GS_EVT_DL_FAIL);
    xTaskCreatePinnedToCore(_gs_download_task, "gs_dl", 8192, (void*)id, 5, &current_task_handle, 0);
    
    // We could make this non-blocking by not waiting for bits, but the interface says it's blocking
    EventBits_t bits = xEventGroupWaitBits(gs_event_group, GS_EVT_DL_DONE | GS_EVT_DL_FAIL, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (bits & GS_EVT_DL_DONE) return GAME_STORE_ERR_OK;
    return gs_status.last_error;
}

game_store_err_t game_store_delete(const char* id) {
    if (gs_status.is_busy) return GAME_STORE_ERR_ABORTED;
    
    game_entry_t *entry = _gs_local_catalog_find(id);
    if (!entry) return GAME_STORE_ERR_BAD_METADATA;

    char final_path[128];
    snprintf(final_path, sizeof(final_path), "%s/%s.nes", GAME_STORE_ROMS_DIR, id);
    unlink(final_path);
    
    entry->is_installed = false;
    _gs_local_catalog_save(local_catalog, local_catalog_count);
    return GAME_STORE_ERR_OK;
}

game_store_err_t game_store_abort(void) {
    if (gs_status.is_busy && current_task_handle) {
        vTaskDelete(current_task_handle);
        current_task_handle = NULL;
        gs_status.is_busy = false;
        gs_status.last_error = GAME_STORE_ERR_ABORTED;
        
        // Clean up temp file if active
        if (gs_status.active_game_id[0] != '\0') {
            char temp_path[128];
            snprintf(temp_path, sizeof(temp_path), "%s/%s.tmp", GAME_STORE_ROMS_DIR, gs_status.active_game_id);
            unlink(temp_path);
        }
    }
    return GAME_STORE_ERR_OK;
}

