#include "game_store.h"
#include "sys_config.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
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
#include <inttypes.h>


static const char* TAG = "GAME_STORE";
static const TickType_t GS_WAIT_TIMEOUT = pdMS_TO_TICKS(15000);
static const size_t GS_CATALOG_BUFFER_SIZE = 32 * 1024;

static game_store_status_t s_gs_status;
static game_entry_t s_local_catalog[GAME_STORE_MAX_GAMES];
static int s_local_catalog_count = 0;
static SemaphoreHandle_t s_gs_mutex = NULL;

static EventGroupHandle_t s_gs_event_group = NULL;
#define GS_EVT_CATALOG_DONE (1 << 0)
#define GS_EVT_CATALOG_FAIL (1 << 1)
#define GS_EVT_DL_DONE      (1 << 2)
#define GS_EVT_DL_FAIL      (1 << 3)
#define GS_EVT_ABORT        (1 << 4)

static TaskHandle_t s_current_task_handle = NULL;

/*
 * Cooperative abort flag.  Set by game_store_abort() on the UI task (Core 1).
 * Polled inside the download/fetch task (Core 0) on every chunk iteration.
 * The task is responsible for its own cleanup before calling vTaskDelete(NULL).
 * NEVER use vTaskDelete(external_handle) — it leaks buffers and sockets.
 */
static volatile bool s_abort_requested = false;

static void _gs_lock(void) {
    if (s_gs_mutex) {
        if (xSemaphoreTake(s_gs_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Mutex timeout");
        }
    }
}

static void _gs_unlock(void) {
    if (s_gs_mutex) {
        xSemaphoreGive(s_gs_mutex);
    }
}

static void _gs_status_snapshot(game_store_status_t *out) {
    if (!out) return;
    _gs_lock();
    *out = s_gs_status;
    _gs_unlock();
}

static void _gs_status_reset_transfer(const char *id) {
    _gs_lock();
    s_gs_status.last_error = GAME_STORE_ERR_OK;
    s_gs_status.progress_pct = 0;
    s_gs_status.bytes_done = 0;
    s_gs_status.bytes_total = 0;
    memset(s_gs_status.active_game_id, 0, sizeof(s_gs_status.active_game_id));
    if (id) {
        strncpy(s_gs_status.active_game_id, id, sizeof(s_gs_status.active_game_id) - 1);
        s_gs_status.active_game_id[sizeof(s_gs_status.active_game_id) - 1] = '\0';
    }
    _gs_unlock();
}

static void _gs_status_set_busy(bool busy) {
    _gs_lock();
    s_gs_status.is_busy = busy;
    _gs_unlock();
}

static void _gs_status_set_error(game_store_err_t err) {
    _gs_lock();
    s_gs_status.last_error = err;
    _gs_unlock();
}

/* --------------------------------------------------------------------------
 * LittleFS Lifecycle
 * -------------------------------------------------------------------------- */
static game_store_err_t _gs_fs_mount(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = GAME_STORE_MOUNT_POINT,
        .partition_label = GAME_STORE_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount or format LittleFS: %s", esp_err_to_name(ret));
        return GAME_STORE_ERR_FS_ERROR;
    }

    ESP_LOGI(TAG, "LittleFS mounted at %s", GAME_STORE_MOUNT_POINT);
    return GAME_STORE_ERR_OK;
}

static void _gs_ensure_dirs(void) {
    // LittleFS supports real directories.
    // Ensure the required directory structure exists.
    struct stat st;
    if (stat(GAME_STORE_MOUNT_POINT, &st) != 0) {
        mkdir(GAME_STORE_MOUNT_POINT, 0777);
    }
    if (stat("/storage/games", &st) != 0) {
        mkdir("/storage/games", 0777);
    }
    if (stat(GAME_STORE_ROMS_DIR, &st) != 0) {
        mkdir(GAME_STORE_ROMS_DIR, 0777);
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
    s_local_catalog_count = 0;
    
    ESP_LOGI(TAG, "Attempting to load local catalog from %s", GAME_STORE_CATALOG_LOCAL);
    FILE *f = fopen(GAME_STORE_CATALOG_LOCAL, "r");
    if (!f) {
        ESP_LOGW(TAG, "No local catalog found at %s. (This is normal on first boot or missing data).", GAME_STORE_CATALOG_LOCAL);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    ESP_LOGI(TAG, "Local catalog opened, size: %ld bytes", size);

    if (size <= 0) {
        fclose(f);
        return;
    }

    char *json_data = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json_data) {
        fclose(f);
        return;
    }

    size_t n = fread(json_data, 1, size, f);
    if (n != (size_t)size) {
        ESP_LOGW(TAG, "fread short: %zu vs %zu", n, (size_t)size);
    }
    json_data[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_data);
    heap_caps_free(json_data);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse local catalog");
        return;
    }

    cJSON *games = cJSON_GetObjectItem(root, "games");
    if (cJSON_IsArray(games)) {
        cJSON *game;
        cJSON_ArrayForEach(game, games) {
            if (s_local_catalog_count >= GAME_STORE_MAX_GAMES) break;
            
            game_entry_t *entry = &s_local_catalog[s_local_catalog_count];
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
            
            s_local_catalog_count++;
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
        snprintf(crc_str, sizeof(crc_str), "%08" PRIX32, catalog[i].crc32);
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
    
    size_t json_len = strlen(json_data);
    bool write_ok = (fwrite(json_data, 1, json_len, f) == json_len);
    fclose(f);
    free(json_data);

    if (!write_ok) {
        ESP_LOGE(TAG, "catalog fwrite failed");
        unlink(temp_path);
        return GAME_STORE_ERR_FS_ERROR;
    }
    if (rename(temp_path, GAME_STORE_CATALOG_LOCAL) != 0) {
        ESP_LOGE(TAG, "catalog rename failed");
        unlink(temp_path);
        return GAME_STORE_ERR_FS_ERROR;
    }

    return GAME_STORE_ERR_OK;
}

static game_entry_t* _gs_local_catalog_find(const char* id);  /* forward decl */

/* Scan /games/roms/ for any .nes files not already in s_local_catalog.
 * Registers them with is_installed=true so they show up in MyGames immediately,
 * even if catalog.json is missing or was flashed before the ROM was added. */
static void _gs_autodiscover_roms(void) {
    ESP_LOGI(TAG, "Auto-discovering ROMs in %s...", GAME_STORE_ROMS_DIR);
    DIR *dir = opendir(GAME_STORE_ROMS_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Auto-discover: cannot open directory %s. Check LittleFS layout.", GAME_STORE_ROMS_DIR);
        return;
    }

    struct dirent *entry;
    _gs_lock();
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
        if (s_local_catalog_count >= GAME_STORE_MAX_GAMES) break;
        game_entry_t *e = &s_local_catalog[s_local_catalog_count++];
        memset(e, 0, sizeof(game_entry_t));
        strncpy(e->id,      id, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
        strncpy(e->name,    id, sizeof(e->name) - 1);  /* ID as display name */
        e->name[sizeof(e->name) - 1] = '\0';
        strncpy(e->version, "1.0", sizeof(e->version) - 1);
        e->version[sizeof(e->version) - 1] = '\0';
        strncpy(e->genre,   "Unknown", sizeof(e->genre) - 1);
        e->genre[sizeof(e->genre) - 1] = '\0';
        e->is_installed = true;

        /* Fill size from stat */
        char full_path[280];
        snprintf(full_path, sizeof(full_path), "%s/%s", GAME_STORE_ROMS_DIR, fname);
        struct stat st;
        if (stat(full_path, &st) == 0) e->size_bytes = (uint32_t)st.st_size;

        ESP_LOGI(TAG, "Auto-discovered ROM: %s (%lu bytes)", id, (unsigned long)e->size_bytes);
    }
    _gs_unlock();
    closedir(dir);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
game_store_err_t game_store_init(void) {
    memset(&s_gs_status, 0, sizeof(s_gs_status));
    if (s_gs_event_group == NULL) {
        s_gs_event_group = xEventGroupCreate();
    }
    if (s_gs_mutex == NULL) {
        s_gs_mutex = xSemaphoreCreateMutex();
    }
    configASSERT(s_gs_event_group != NULL);
    configASSERT(s_gs_mutex != NULL);
    
    game_store_err_t err = _gs_fs_mount();
    if (err != GAME_STORE_ERR_OK) return err;

    _gs_ensure_dirs();
    _gs_cleanup_temp_files();
    _gs_local_catalog_load();    /* Load catalog.json first */
    _gs_autodiscover_roms();     /* Then add any ROMs not in catalog */

    /* ── Boot diagnostic: dump every file found on the partition ── */
    ESP_LOGI(TAG, "=== LittleFS scan at %s ===", GAME_STORE_MOUNT_POINT);
    DIR *droot = opendir(GAME_STORE_MOUNT_POINT);
    if (droot) {
        struct dirent *de;
        while ((de = readdir(droot)) != NULL) {
            char fp[300];
            snprintf(fp, sizeof(fp), "%s/%s", GAME_STORE_MOUNT_POINT, de->d_name);
            struct stat st;
            if (stat(fp, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    DIR *dsub = opendir(fp);
                    if (dsub) {
                        struct dirent *sub;
                    while ((sub = readdir(dsub)) != NULL) {
                        /* Static buffer avoids 560B stack allocation */
                        static char s_sfp[560];
                        snprintf(s_sfp, sizeof(s_sfp), "%s/%s", fp, sub->d_name);
                        struct stat sst;
                        if (stat(s_sfp, &sst) == 0)
                            ESP_LOGI(TAG, "  [FILE] %s (%ld B)", s_sfp, (long)sst.st_size);
                    }
                        closedir(dsub);
                    }
                } else {
                    ESP_LOGI(TAG, "  [FILE] %s (%ld B)", fp, (long)st.st_size);
                }
            }
        }
        closedir(droot);
    } else {
        ESP_LOGE(TAG, "  Cannot open %s — LittleFS mount failed!", GAME_STORE_MOUNT_POINT);
    }
    ESP_LOGI(TAG, "=== End scan — %d game(s) in catalog ===", s_local_catalog_count);
    return GAME_STORE_ERR_OK;
}


void game_store_get_status(game_store_status_t* out) {
    _gs_status_snapshot(out);
}

/* Actual definition (forward-declared above for autodiscover) */
static game_entry_t* _gs_local_catalog_find(const char* id) {
    for (int i = 0; i < s_local_catalog_count; i++) {
        if (strcmp(s_local_catalog[i].id, id) == 0) {
            return &s_local_catalog[i];
        }
    }
    return NULL;
}

int game_store_get_catalog(game_entry_t* out, int max_count) {
    if (!out || max_count <= 0) return 0;
    _gs_lock();
    int count = s_local_catalog_count;
    if (count > max_count) count = max_count;
    memcpy(out, s_local_catalog, count * sizeof(game_entry_t));
    _gs_unlock();
    return count;
}

bool game_store_get_rom_path(const char* id, char* path_out, size_t len) {
    if (!id || !path_out || len == 0) return false;
    _gs_lock();
    game_entry_t *entry = _gs_local_catalog_find(id);
    bool is_installed = (entry != NULL) && entry->is_installed;
    _gs_unlock();
    if (!is_installed) return false;
    snprintf(path_out, len, "%s/%s.nes", GAME_STORE_ROMS_DIR, id);
    struct stat st;
    return (stat(path_out, &st) == 0);
}

size_t game_store_get_free_bytes(void) {
    size_t total, used;
    if (esp_littlefs_info(GAME_STORE_PARTITION_LABEL, &total, &used) == ESP_OK) {
        return total - used;
    }
    return 0;
}

size_t game_store_get_total_bytes(void) {
    size_t total, used;
    if (esp_littlefs_info(GAME_STORE_PARTITION_LABEL, &total, &used) == ESP_OK) {
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
                _gs_lock();
                s_gs_status.bytes_total = atoi(evt->header_value);
                _gs_unlock();
            }
            break;
        case HTTP_EVENT_ON_DATA:
            _gs_lock();
            if (s_gs_status.bytes_total > 0) {
                s_gs_status.bytes_done += evt->data_len;
                s_gs_status.progress_pct = (s_gs_status.bytes_done * 100) / s_gs_status.bytes_total;
            }
            _gs_unlock();
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
    (void)pvParameters;
    _gs_status_set_error(GAME_STORE_ERR_OK);
    
    if (!wifi_mgr_is_connected()) {
        _gs_status_set_error(GAME_STORE_ERR_NO_WIFI);
        _gs_status_set_busy(false);
        s_current_task_handle = NULL;
        xEventGroupSetBits(s_gs_event_group, GS_EVT_CATALOG_FAIL);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = GAME_STORE_CLOUD_CATALOG_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = GAME_STORE_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char *buffer = heap_caps_malloc(GS_CATALOG_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for catalog");
        _gs_status_set_error(GAME_STORE_ERR_FS_ERROR);
        esp_http_client_cleanup(client);
        _gs_status_set_busy(false);
        s_current_task_handle = NULL;
        xEventGroupSetBits(s_gs_event_group, GS_EVT_CATALOG_FAIL);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        goto fetch_cleanup;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len < 0) {
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        goto fetch_cleanup;
    }
    if (content_len >= (int)GS_CATALOG_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Catalog too large: %d bytes", content_len);
        _gs_status_set_error(GAME_STORE_ERR_BAD_METADATA);
        goto fetch_cleanup;
    }

    int read_len = 0;
    int total_read = 0;
    
    while ((read_len = esp_http_client_read(client, buffer + total_read, GS_CATALOG_BUFFER_SIZE - total_read - 1)) > 0) {
        if (s_abort_requested) {
            _gs_status_set_error(GAME_STORE_ERR_ABORTED);
            break;
        }
        total_read += read_len;
        if (total_read >= (int)GS_CATALOG_BUFFER_SIZE - 1) {
            ESP_LOGE(TAG, "Catalog buffer exhausted");
            _gs_status_set_error(GAME_STORE_ERR_BAD_METADATA);
            break;
        }
    }
    buffer[total_read] = '\0';
    
    game_store_status_t status;
    _gs_status_snapshot(&status);
    if (read_len < 0 && status.last_error == GAME_STORE_ERR_OK) {
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        _gs_status_snapshot(&status);
    }

    if (status.last_error != GAME_STORE_ERR_OK) {
        goto fetch_cleanup;
    }

    if (total_read <= 0) {
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        goto fetch_cleanup;
    }

    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        _gs_status_set_error(GAME_STORE_ERR_BAD_METADATA);
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
            _gs_lock();
            game_entry_t *entry = _gs_local_catalog_find(gid);
            
            // Just a simple parsing logic for demonstration
            if (!entry && s_local_catalog_count < GAME_STORE_MAX_GAMES) {
                entry = &s_local_catalog[s_local_catalog_count++];
                memset(entry, 0, sizeof(game_entry_t));
                strncpy(entry->id, gid, sizeof(entry->id)-1);
                entry->id[sizeof(entry->id) - 1] = '\0';
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

                if (cJSON_IsString(name)) {
                    strncpy(entry->name, name->valuestring, sizeof(entry->name)-1);
                    entry->name[sizeof(entry->name)-1] = '\0';
                }
                if (cJSON_IsString(version)) {
                    strncpy(entry->version, version->valuestring, sizeof(entry->version)-1);
                    entry->version[sizeof(entry->version)-1] = '\0';
                }
                if (cJSON_IsString(genre)) {
                    strncpy(entry->genre, genre->valuestring, sizeof(entry->genre)-1);
                    entry->genre[sizeof(entry->genre)-1] = '\0';
                }
                if (cJSON_IsString(desc)) {
                    strncpy(entry->description, desc->valuestring, sizeof(entry->description)-1);
                    entry->description[sizeof(entry->description)-1] = '\0';
                }
                if (cJSON_IsNumber(size_bytes)) entry->size_bytes = size_bytes->valueint;
                if (cJSON_IsString(crc32)) entry->crc32 = strtoul(crc32->valuestring, NULL, 16);
                if (cJSON_IsString(url)) {
                    strncpy(entry->url, url->valuestring, sizeof(entry->url)-1);
                    entry->url[sizeof(entry->url)-1] = '\0';
                }
                if (cJSON_IsNumber(mapper)) entry->mapper = mapper->valueint;
            }
            _gs_unlock();
        }
    }
    cJSON_Delete(root);
    
    // Save merged catalog
    _gs_lock();
    game_store_err_t save_err = _gs_local_catalog_save(s_local_catalog, s_local_catalog_count);
    _gs_unlock();
    if (save_err != GAME_STORE_ERR_OK) {
        _gs_status_set_error(save_err);
        goto fetch_cleanup;
    }
    
    xEventGroupSetBits(s_gs_event_group, GS_EVT_CATALOG_DONE);

fetch_cleanup:
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    _gs_status_set_busy(false);
    game_store_status_t final_status;
    _gs_status_snapshot(&final_status);
    if (final_status.last_error != GAME_STORE_ERR_OK) {
        xEventGroupSetBits(s_gs_event_group, GS_EVT_CATALOG_FAIL);
    }
    s_abort_requested = false;
    s_current_task_handle = NULL;
    vTaskDelete(NULL);
}

game_store_err_t game_store_refresh_catalog(void) {
    game_store_status_t status;
    _gs_status_snapshot(&status);
    if (status.is_busy) return GAME_STORE_ERR_ABORTED;
    
    xEventGroupClearBits(s_gs_event_group, GS_EVT_CATALOG_DONE | GS_EVT_CATALOG_FAIL);
    s_abort_requested = false;
    _gs_status_set_busy(true);
    _gs_status_set_error(GAME_STORE_ERR_OK);
    BaseType_t rc = xTaskCreatePinnedToCore(_gs_cloud_fetch_task, "gs_cat", 8192, NULL, 5, &s_current_task_handle, 0);
    if (rc != pdPASS) {
        s_current_task_handle = NULL;
        _gs_status_set_busy(false);
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        return GAME_STORE_ERR_NETWORK;
    }
    
    EventBits_t bits = xEventGroupWaitBits(s_gs_event_group, GS_EVT_CATALOG_DONE | GS_EVT_CATALOG_FAIL, pdTRUE, pdFALSE, GS_WAIT_TIMEOUT);
    if (bits == 0) {
        s_abort_requested = true;
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        return GAME_STORE_ERR_NETWORK;
    }
    
    if (bits & GS_EVT_CATALOG_DONE) return GAME_STORE_ERR_OK;
    _gs_status_snapshot(&status);
    return status.last_error;
}

/* --------------------------------------------------------------------------
 * ROM Download Task
 * -------------------------------------------------------------------------- */
static void _gs_download_task(void *pvParameters) {
    const char *id = (const char*)pvParameters;
    game_store_status_t status;
    _gs_status_reset_transfer(id);

    char url_buf[256] = {0};
    uint32_t size_bytes = 0;
    uint32_t crc32 = 0;

    _gs_lock();
    game_entry_t *entry = _gs_local_catalog_find(id);
    if (entry) {
        strncpy(url_buf, entry->url, sizeof(url_buf) - 1);
        url_buf[sizeof(url_buf) - 1] = '\0';
        size_bytes = entry->size_bytes;
        crc32 = entry->crc32;
    }
    _gs_unlock();
    if (!entry) {
        _gs_status_set_error(GAME_STORE_ERR_BAD_METADATA);
        goto dl_abort;
    }

    if (!wifi_mgr_is_connected()) {
        _gs_status_set_error(GAME_STORE_ERR_NO_WIFI);
        goto dl_abort;
    }

    if (game_store_get_free_bytes() < size_bytes + (size_bytes / 10)) {
        _gs_status_set_error(GAME_STORE_ERR_NO_SPACE);
        goto dl_abort;
    }

    {
        char temp_path[128];
        snprintf(temp_path, sizeof(temp_path), "%s/%s.tmp", GAME_STORE_ROMS_DIR, id);
        FILE *f = fopen(temp_path, "wb");
        if (!f) {
            _gs_status_set_error(GAME_STORE_ERR_FS_ERROR);
            goto dl_abort;
        }

        esp_http_client_config_t config = {
            .url = url_buf,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = GAME_STORE_HTTP_TIMEOUT_MS,
            .event_handler = _http_event_handle,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        char *buffer = heap_caps_malloc(GAME_STORE_DL_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buffer) {
            fclose(f);
            unlink(temp_path);
            esp_http_client_cleanup(client);
            _gs_status_set_error(GAME_STORE_ERR_FS_ERROR);
            goto dl_abort;
        }

        if (esp_http_client_open(client, 0) != ESP_OK) {
            heap_caps_free(buffer);
            fclose(f);
            unlink(temp_path);
            esp_http_client_cleanup(client);
            _gs_status_set_error(GAME_STORE_ERR_NETWORK);
            goto dl_abort;
        }
        
        esp_http_client_fetch_headers(client);
        int read_len;
        uint32_t crc = 0xFFFFFFFF;
        
        while ((read_len = esp_http_client_read(client, buffer, GAME_STORE_DL_CHUNK_SIZE)) > 0) {
            /* ── Cooperative abort check ───────────────────────────── */
            if (s_abort_requested) {
                _gs_status_snapshot(&status);
                ESP_LOGW(TAG, "Download aborted cooperatively after %d bytes",
                         (int)status.bytes_done);
                _gs_status_set_error(GAME_STORE_ERR_ABORTED);
                break;
            }
            if (!wifi_mgr_is_connected()) {
                _gs_status_set_error(GAME_STORE_ERR_NO_WIFI);
                break;
            }
            if (fwrite(buffer, 1, read_len, f) != (size_t)read_len) {
                ESP_LOGE(TAG, "fwrite failed — filesystem full?");
                _gs_status_set_error(GAME_STORE_ERR_FS_ERROR);
                break;
            }
            crc = ~esp_rom_crc32_le(~crc, (uint8_t *)buffer, read_len);        }
        
        fclose(f);
        heap_caps_free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        _gs_status_snapshot(&status);
        if (status.last_error != GAME_STORE_ERR_OK || read_len < 0) {
            unlink(temp_path);
            if (status.last_error == GAME_STORE_ERR_OK) _gs_status_set_error(GAME_STORE_ERR_NETWORK);
            goto dl_abort;
        }

        // Validate CRC32
        if (crc != crc32 && crc32 != 0) {
            unlink(temp_path);
            _gs_status_set_error(GAME_STORE_ERR_CORRUPT_ROM);
            goto dl_abort;
        }

        // Rename atomic
        char final_path[128];
        snprintf(final_path, sizeof(final_path), "%s/%s.nes", GAME_STORE_ROMS_DIR, id);
        if (rename(temp_path, final_path) != 0) {
            unlink(temp_path);
            _gs_status_set_error(GAME_STORE_ERR_FS_ERROR);
            goto dl_abort;
        }

        _gs_lock();
        entry->is_installed = true;
        game_store_err_t save_err = _gs_local_catalog_save(s_local_catalog, s_local_catalog_count);
        _gs_unlock();
        if (save_err != GAME_STORE_ERR_OK) {
            _gs_status_set_error(save_err);
            goto dl_abort;
        }
        xEventGroupSetBits(s_gs_event_group, GS_EVT_DL_DONE);
        _gs_status_set_busy(false);
        s_abort_requested = false;
        s_current_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

dl_abort:
    _gs_status_set_busy(false);
    s_abort_requested = false;
    xEventGroupSetBits(s_gs_event_group, GS_EVT_DL_FAIL);
    s_current_task_handle = NULL;
    vTaskDelete(NULL);
}

game_store_err_t game_store_install(const char* id) {
    if (!id || id[0] == '\0') return GAME_STORE_ERR_BAD_METADATA;
    game_store_status_t status;
    _gs_status_snapshot(&status);
    if (status.is_busy) return GAME_STORE_ERR_ABORTED;
    
    xEventGroupClearBits(s_gs_event_group, GS_EVT_DL_DONE | GS_EVT_DL_FAIL);
    s_abort_requested = false;
    _gs_status_set_busy(true);
    _gs_status_reset_transfer(id);
    BaseType_t rc = xTaskCreatePinnedToCore(_gs_download_task, "gs_dl", 8192, (void*)id, 5, &s_current_task_handle, 0);
    if (rc != pdPASS) {
        s_current_task_handle = NULL;
        _gs_status_set_busy(false);
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        return GAME_STORE_ERR_NETWORK;
    }
    
    // We could make this non-blocking by not waiting for bits, but the interface says it's blocking
    EventBits_t bits = xEventGroupWaitBits(s_gs_event_group, GS_EVT_DL_DONE | GS_EVT_DL_FAIL, pdTRUE, pdFALSE, GS_WAIT_TIMEOUT);
    if (bits == 0) {
        s_abort_requested = true;
        _gs_status_set_error(GAME_STORE_ERR_NETWORK);
        return GAME_STORE_ERR_NETWORK;
    }
    
    if (bits & GS_EVT_DL_DONE) return GAME_STORE_ERR_OK;
    _gs_status_snapshot(&status);
    return status.last_error;
}

game_store_err_t game_store_delete(const char* id) {
    if (!id || id[0] == '\0') return GAME_STORE_ERR_BAD_METADATA;
    game_store_status_t status;
    _gs_status_snapshot(&status);
    if (status.is_busy) return GAME_STORE_ERR_ABORTED;
    
    _gs_lock();
    game_entry_t *entry = _gs_local_catalog_find(id);
    _gs_unlock();
    if (!entry) return GAME_STORE_ERR_BAD_METADATA;

    char final_path[128];
    snprintf(final_path, sizeof(final_path), "%s/%s.nes", GAME_STORE_ROMS_DIR, id);
    unlink(final_path);
    
    _gs_lock();
    entry->is_installed = false;
    game_store_err_t err = _gs_local_catalog_save(s_local_catalog, s_local_catalog_count);
    _gs_unlock();
    return err;
}

game_store_err_t game_store_abort(void) {
    game_store_status_t status;
    _gs_status_snapshot(&status);
    if (!status.is_busy || !s_current_task_handle) {
        return GAME_STORE_ERR_OK;  /* Nothing running — no-op */
    }

    ESP_LOGI(TAG, "Requesting cooperative download abort...");
    s_abort_requested = true;
    _gs_status_set_error(GAME_STORE_ERR_ABORTED);
    return GAME_STORE_ERR_OK;
}
