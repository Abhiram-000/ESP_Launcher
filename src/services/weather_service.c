/*
 * weather_service.c — Weather Service Implementation
 * ===================================================
 * Core features:
 *   1. IP geolocation via ip-api.com (HTTP, no key, 45 req/min)
 *   2. Weather via Open-Meteo (HTTPS, no key, 10K calls/day)
 *   3. IANA→POSIX timezone conversion (embedded lookup table)
 *   4. LittleFS caching (location + weather)
 *   5. Async worker task (non-blocking UI)
 */
#include "weather_service.h"
#include "wifi_mgr.h"
#include "ntp_service.h"
#include "idle_mgr.h"
#include "../config/sys_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

static const char* TAG = "WEATHER_SVC";

// Mutex-protected status shared between Core 0 (worker) and Core 1 (UI)
static weather_status_t s_status;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_worker_task = NULL;
static volatile bool s_abort_requested = false;

// Manual location override (persisted to NVS)
static bool s_has_manual_location = false;
static weather_location_t s_manual_location = {0};

/* HTTP response receive context.
 * WHY per-call instead of a file-scope global: four helpers used to share one
 * static buffer. weather_service_set_city() runs on the UI task (Core 1) while
 * the worker fetches on Core 0, so both could heap_caps_free() the same
 * pointer — a double free / use-after-free on a device with no MMU. Each
 * caller now owns a stack-local context passed to esp_http_client through
 * config.user_data, which makes the whole fetch path reentrant with no lock. */
#define HTTP_RECV_BUF_SIZE 4096
typedef struct {
    char* buf;
    int   len;
} http_rx_t;

static bool _http_rx_init(http_rx_t* rx) {
    rx->buf = heap_caps_malloc(HTTP_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx->buf) {
        ESP_LOGE(TAG, "HTTP buffer alloc failed (%d bytes)", HTTP_RECV_BUF_SIZE);
        return false;
    }
    rx->len = 0;
    memset(rx->buf, 0, HTTP_RECV_BUF_SIZE);
    return true;
}

static void _http_rx_free(http_rx_t* rx) {
    if (rx->buf) {
        heap_caps_free(rx->buf);
        rx->buf = NULL;
    }
    rx->len = 0;
}

// Helper macros
#define LOCK()   do { if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) { ESP_LOGW(TAG, "Mutex timeout"); } } while(0)
#define UNLOCK() if (s_mutex) xSemaphoreGive(s_mutex)

// IANA to POSIX timezone mapping (top 50 common zones, ~3KB)
typedef struct {
    const char* iana;
    const char* posix;
} tz_map_t;

static const tz_map_t TZ_MAP[] = {
    {"Asia/Kolkata", "IST-5:30"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "CST6CDT,M4.1.0,M10.5.0"},
    {"America/Sao_Paulo", "<-03>3"},
    {"America/Argentina/Buenos_Aires", "<-03>3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Moscow", "MSK-3"},
    {"Europe/Istanbul", "<+03>-3"},
    {"Asia/Dubai", "<+04>-4"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Singapore", "<+08>-8"},
    {"Asia/Tokyo", "JST-9"},
    {"Asia/Seoul", "KST-9"},
    {"Asia/Bangkok", "<+07>-7"},
    {"Asia/Jakarta", "WIB-7"},
    {"Asia/Manila", "PST-8"},
    {"Asia/Karachi", "PKT-5"},
    {"Asia/Dhaka", "<+06>-6"},
    {"Asia/Tehran", "<+0330>-3:30"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"},
    {"Australia/Perth", "AWST-8"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Africa/Cairo", "EET-2"},
    {"Africa/Johannesburg", "SAST-2"},
    {"Africa/Lagos", "WAT-1"},
    {"Africa/Nairobi", "EAT-3"},
    {"UTC", "UTC0"},
    {"GMT", "GMT0"},
    {NULL, NULL}
};

static const char* _iana_to_posix(const char* iana) {
    if (!iana) return "UTC0";
    for (int i = 0; TZ_MAP[i].iana != NULL; i++) {
        if (strcmp(iana, TZ_MAP[i].iana) == 0) {
            return TZ_MAP[i].posix;
        }
    }
    ESP_LOGW(TAG, "No POSIX mapping for %s, using UTC0", iana);
    return "UTC0";
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    http_rx_t* rx = (http_rx_t*)evt->user_data;
    if (!rx || !rx->buf) return ESP_OK;

    /* Reserve one byte for the NUL terminator so cJSON always sees a string. */
    if (rx->len + evt->data_len < HTTP_RECV_BUF_SIZE) {
        memcpy(rx->buf + rx->len, evt->data, evt->data_len);
        rx->len += evt->data_len;
        rx->buf[rx->len] = '\0';
    }
    return ESP_OK;
}

static weather_err_t _fetch_ip_geolocation(weather_location_t* loc) {
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
    ESP_LOGI(TAG, "Fetching IP geolocation from ip-api.com");
    
    http_rx_t rx = {0};
    if (!_http_rx_init(&rx)) return WEATHER_ERR_NETWORK;
    
    esp_http_client_config_t config = {
        .url = WEATHER_IP_GEO_URL,
        .event_handler = _http_event_handler,
        .user_data = &rx,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "IP geolocation failed: %s, status=%d", esp_err_to_name(err), status);
        _http_rx_free(&rx);
        return WEATHER_ERR_NETWORK;
    }
    
    // Parse JSON
    cJSON* root = cJSON_Parse(rx.buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* status_field = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(status_field) || strcmp(status_field->valuestring, "success") != 0) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Geolocation API returned error");
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* city = cJSON_GetObjectItem(root, "city");
    cJSON* lat = cJSON_GetObjectItem(root, "lat");
    cJSON* lon = cJSON_GetObjectItem(root, "lon");
    cJSON* tz = cJSON_GetObjectItem(root, "timezone");
    cJSON* country = cJSON_GetObjectItem(root, "country");
    
    if (city && lat && lon && tz) {
        strncpy(loc->city_name, city->valuestring, sizeof(loc->city_name) - 1);
        loc->city_name[sizeof(loc->city_name) - 1] = '\0';
        loc->latitude = lat->valuedouble;
        loc->longitude = lon->valuedouble;
        strncpy(loc->timezone_iana, tz->valuestring, sizeof(loc->timezone_iana) - 1);
        loc->timezone_iana[sizeof(loc->timezone_iana) - 1] = '\0';
        if (country) {
            strncpy(loc->country, country->valuestring, sizeof(loc->country) - 1);
            loc->country[sizeof(loc->country) - 1] = '\0';
        }
        
        // Convert IANA to POSIX
        const char* posix = _iana_to_posix(loc->timezone_iana);
        strncpy(loc->timezone_posix, posix, sizeof(loc->timezone_posix) - 1);
        loc->timezone_posix[sizeof(loc->timezone_posix) - 1] = '\0';
        
        ESP_LOGI(TAG, "Location: %s (%.2f, %.2f), TZ: %s → %s",
                 loc->city_name, loc->latitude, loc->longitude,
                 loc->timezone_iana, loc->timezone_posix);
    } else {
        cJSON_Delete(root);
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON_Delete(root);
    _http_rx_free(&rx);
    return WEATHER_ERR_OK;
}

/**
 * @brief Reverse geocode IP coordinates to actual city name via OpenStreetMap Nominatim.
 *        Free, no API key, requires User-Agent header.
 *        Refines the city name from IP geolocation (which maps to ISP location).
 */
static weather_err_t _reverse_geocode_city(weather_location_t* loc) {
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
    char url[256];
    snprintf(url, sizeof(url),
        "%s?lat=%.4f&lon=%.4f&format=json&zoom=10&addressdetails=1",
        WEATHER_REVERSE_GEO_URL, loc->latitude, loc->longitude);
    
    ESP_LOGI(TAG, "Reverse geocoding for (%.4f, %.4f)", loc->latitude, loc->longitude);
    
    http_rx_t rx = {0};
    if (!_http_rx_init(&rx)) return WEATHER_ERR_NETWORK;
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &rx,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // Nominatim requires a descriptive User-Agent
    esp_http_client_set_header(client, "User-Agent", "ESP32-Launcher-Weather/2.0");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Reverse geocode failed: %s, status=%d — keeping IP city", esp_err_to_name(err), status);
        _http_rx_free(&rx);
        return WEATHER_ERR_NETWORK;
    }
    
    cJSON* root = cJSON_Parse(rx.buf);
    if (!root) {
        ESP_LOGW(TAG, "Reverse geocode JSON parse failed");
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    // Try address.city first, fall back to address.town, then address.village
    cJSON* address = cJSON_GetObjectItem(root, "address");
    const char* new_city = NULL;
    if (address) {
        cJSON* city = cJSON_GetObjectItem(address, "city");
        cJSON* town = cJSON_GetObjectItem(address, "town");
        cJSON* village = cJSON_GetObjectItem(address, "village");
        if (city && cJSON_IsString(city)) new_city = city->valuestring;
        else if (town && cJSON_IsString(town)) new_city = town->valuestring;
        else if (village && cJSON_IsString(village)) new_city = village->valuestring;
    }
    
    if (new_city && new_city[0] != '\0') {
        ESP_LOGI(TAG, "Reverse geocode: %s → %s", loc->city_name, new_city);
        strncpy(loc->city_name, new_city, sizeof(loc->city_name) - 1);
        loc->city_name[sizeof(loc->city_name) - 1] = '\0';
    } else {
        ESP_LOGW(TAG, "Reverse geocode: no city in response, keeping '%s'", loc->city_name);
    }
    
    cJSON_Delete(root);
    _http_rx_free(&rx);
    return WEATHER_ERR_OK;
}

static weather_err_t _fetch_weather(const weather_location_t* loc, weather_current_t* wx) {
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
    char url[512];
    snprintf(url, sizeof(url),
        "%s?latitude=%.4f&longitude=%.4f"
        "&current=apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
        "&timezone=auto",
        WEATHER_FORECAST_URL, loc->latitude, loc->longitude);
    
    ESP_LOGI(TAG, "Fetching weather from Open-Meteo");
    
    http_rx_t rx = {0};
    if (!_http_rx_init(&rx)) return WEATHER_ERR_NETWORK;
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &rx,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Weather fetch failed: %s, status=%d", esp_err_to_name(err), status);
        _http_rx_free(&rx);
        return WEATHER_ERR_NETWORK;
    }
    
    // Parse JSON
    cJSON* root = cJSON_Parse(rx.buf);
    if (!root) {
        ESP_LOGE(TAG, "Weather JSON parse failed");
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* current = cJSON_GetObjectItem(root, "current");
    if (!current) {
        cJSON_Delete(root);
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* temp = cJSON_GetObjectItem(current, "apparent_temperature");
    cJSON* hum = cJSON_GetObjectItem(current, "relative_humidity_2m");
    cJSON* code = cJSON_GetObjectItem(current, "weather_code");
    cJSON* wind = cJSON_GetObjectItem(current, "wind_speed_10m");
    cJSON* day = cJSON_GetObjectItem(current, "is_day");
    
    if (temp && hum && code && wind && day) {
        wx->temperature = (float)temp->valuedouble;
        wx->humidity = hum->valueint;
        wx->weather_code = code->valueint;
        wx->wind_speed = (float)wind->valuedouble;
        wx->is_day = day->valueint;
        
        ESP_LOGI(TAG, "Weather: %.1f°C, %d%%, wind %.1f km/h, code=%d",
                 wx->temperature, wx->humidity, wx->wind_speed, wx->weather_code);
    } else {
        cJSON_Delete(root);
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON_Delete(root);
    _http_rx_free(&rx);
    return WEATHER_ERR_OK;
}

static void _save_manual_location_to_nvs(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("wx_loc", NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err)); return; }
    uint8_t flag = s_has_manual_location ? 1 : 0;
    nvs_set_u8(h, "manual", flag);
    if (flag) {
        nvs_set_blob(h, "loc", &s_manual_location, sizeof(s_manual_location));
    } else {
        nvs_erase_key(h, "loc");
    }
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Manual location %s to NVS", flag ? "saved" : "cleared");
}

static void _load_manual_location_from_nvs(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("wx_loc", NVS_READONLY, &h);
    if (err != ESP_OK) return;
    uint8_t flag = 0;
    nvs_get_u8(h, "manual", &flag);
    if (flag) {
        size_t sz = sizeof(s_manual_location);
        err = nvs_get_blob(h, "loc", &s_manual_location, &sz);
        if (err == ESP_OK && sz == sizeof(s_manual_location)) {
            s_has_manual_location = true;
            ESP_LOGI(TAG, "Loaded manual location: %s", s_manual_location.city_name);
        }
    }
    nvs_close(h);
}

static void _save_to_cache(const weather_location_t* loc, const weather_current_t* wx) {
    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", WEATHER_CONFIG_PATH);
    FILE* f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open cache file for writing");
        return;
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "city", loc->city_name);
    cJSON_AddNumberToObject(root, "lat", loc->latitude);
    cJSON_AddNumberToObject(root, "lon", loc->longitude);
    cJSON_AddStringToObject(root, "tz_iana", loc->timezone_iana);
    cJSON_AddStringToObject(root, "tz_posix", loc->timezone_posix);
    cJSON_AddStringToObject(root, "country", loc->country);
    cJSON_AddNumberToObject(root, "temperature", wx->temperature);
    cJSON_AddNumberToObject(root, "humidity", wx->humidity);
    cJSON_AddNumberToObject(root, "weather_code", wx->weather_code);
    cJSON_AddNumberToObject(root, "wind_speed", wx->wind_speed);
    cJSON_AddNumberToObject(root, "is_day", wx->is_day);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    char* json_str = cJSON_Print(root);
    if (json_str) {
        fputs(json_str, f);
        free(json_str);
    }
    cJSON_Delete(root);
    fclose(f);
    if (rename(tmp_path, WEATHER_CONFIG_PATH) != 0) {
        ESP_LOGE(TAG, "Failed to rename cache file: %s -> %s", tmp_path, WEATHER_CONFIG_PATH);
        unlink(tmp_path);
    } else {
        ESP_LOGI(TAG, "Weather cached to %s", WEATHER_CONFIG_PATH);
    }
}

static bool _load_from_cache(weather_location_t* loc, weather_current_t* wx, int64_t* timestamp) {
    FILE* f = fopen(WEATHER_CONFIG_PATH, "r");
    if (!f) return false;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > 4096) {
        fclose(f);
        return false;
    }
    
    char* buffer = heap_caps_malloc(fsize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        fclose(f);
        return false;
    }
    
    fread(buffer, 1, fsize, f);
    buffer[fsize] = '\0';
    fclose(f);
    
    cJSON* root = cJSON_Parse(buffer);
    heap_caps_free(buffer);
    
    if (!root) return false;
    
    cJSON* city = cJSON_GetObjectItem(root, "city");
    cJSON* lat = cJSON_GetObjectItem(root, "lat");
    cJSON* lon = cJSON_GetObjectItem(root, "lon");
    cJSON* tz_iana = cJSON_GetObjectItem(root, "tz_iana");
    cJSON* tz_posix = cJSON_GetObjectItem(root, "tz_posix");
    cJSON* country = cJSON_GetObjectItem(root, "country");
    cJSON* temp = cJSON_GetObjectItem(root, "temperature");
    cJSON* hum = cJSON_GetObjectItem(root, "humidity");
    cJSON* code = cJSON_GetObjectItem(root, "weather_code");
    cJSON* wind = cJSON_GetObjectItem(root, "wind_speed");
    cJSON* day = cJSON_GetObjectItem(root, "is_day");
    cJSON* ts = cJSON_GetObjectItem(root, "timestamp");
    
    bool valid = city && lat && lon && tz_iana && tz_posix && temp && hum && code && wind && day && ts;
    
    if (valid) {
        strncpy(loc->city_name, city->valuestring, sizeof(loc->city_name) - 1);
        loc->city_name[sizeof(loc->city_name) - 1] = '\0';
        loc->latitude = lat->valuedouble;
        loc->longitude = lon->valuedouble;
        strncpy(loc->timezone_iana, tz_iana->valuestring, sizeof(loc->timezone_iana) - 1);
        loc->timezone_iana[sizeof(loc->timezone_iana) - 1] = '\0';
        strncpy(loc->timezone_posix, tz_posix->valuestring, sizeof(loc->timezone_posix) - 1);
        loc->timezone_posix[sizeof(loc->timezone_posix) - 1] = '\0';
        if (country) {
            strncpy(loc->country, country->valuestring, sizeof(loc->country) - 1);
            loc->country[sizeof(loc->country) - 1] = '\0';
        }
        
        wx->temperature = (float)temp->valuedouble;
        wx->humidity = hum->valueint;
        wx->weather_code = code->valueint;
        wx->wind_speed = (float)wind->valuedouble;
        wx->is_day = day->valueint;
        
        *timestamp = (int64_t)ts->valuedouble;
        
        ESP_LOGI(TAG, "Cache loaded: %s, %.1f°C", loc->city_name, wx->temperature);
    }
    
    cJSON_Delete(root);
    return valid;
}

static void _worker_task(void* arg) {
    ESP_LOGI(TAG, "Weather worker task started on Core %d", xPortGetCoreID());
    
    LOCK();
    s_status.is_busy = true;
    strncpy(s_status.status_msg, "Fetching location...", sizeof(s_status.status_msg) - 1);
    s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
    UNLOCK();
    
    weather_location_t loc = {0};
    weather_current_t wx = {0};
    
    // Step 1: Get location — use manual override if set, otherwise IP geolocation
    weather_err_t err;
    if (s_has_manual_location) {
        LOCK();
        loc = s_manual_location;
        UNLOCK();
        ESP_LOGI(TAG, "Using manual location: %s (%.4f, %.4f)", loc.city_name, loc.latitude, loc.longitude);
        err = WEATHER_ERR_OK;
    } else {
        err = _fetch_ip_geolocation(&loc);
        if (err != WEATHER_ERR_OK && !s_abort_requested) {
            LOCK();
            strncpy(s_status.status_msg, "Location lookup failed", sizeof(s_status.status_msg) - 1);
            s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
            UNLOCK();
            ESP_LOGW(TAG, "Geolocation failed, skipping weather fetch");
            goto cleanup;
        }
        
        if (s_abort_requested) goto cleanup;
        
        // Step 1b: Refine city name via reverse geocoding (best-effort)
        _reverse_geocode_city(&loc);
    }
    
    if (s_abort_requested) goto cleanup;
    
    // Step 2: Get weather
    LOCK();
    strncpy(s_status.status_msg, "Fetching weather...", sizeof(s_status.status_msg) - 1);
    s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
    UNLOCK();
    
    if (err == WEATHER_ERR_OK) {
        err = _fetch_weather(&loc, &wx);
        if (err != WEATHER_ERR_OK) {
            LOCK();
            strncpy(s_status.status_msg, "Weather fetch failed", sizeof(s_status.status_msg) - 1);
            s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
            UNLOCK();
        }
    }
    
    if (s_abort_requested) goto cleanup;
    
    // Step 3: Update global status
    if (err == WEATHER_ERR_OK) {
        LOCK();
        s_status.location = loc;
        s_status.current = wx;
        s_status.has_data = true;
        s_status.is_stale = false;
        s_status.last_update_time = time(NULL);
        strncpy(s_status.status_msg, "Updated", sizeof(s_status.status_msg) - 1);
        s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
        UNLOCK();
        
        // Save to cache
        _save_to_cache(&loc, &wx);
        
        // Set timezone
        ntp_service_set_timezone(loc.timezone_posix);
    }
    
cleanup:
    LOCK();
    s_status.is_busy = false;
    UNLOCK();
    
    s_abort_requested = false;
    s_worker_task = NULL;
    /* WHY: every worker exit funnels through this label, so this is the one
     * place the idle hold taken in weather_service_refresh() can be released.
     * Without it the device would never sleep again after one refresh. */
    idle_mgr_enable();
    ESP_LOGI(TAG, "Weather worker task completed");
    vTaskDelete(NULL);
}

weather_err_t weather_service_init(void) {
    ESP_LOGI(TAG, "Initializing weather service");
    
    // Create mutex
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return WEATHER_ERR_NETWORK;
        }
    }
    
    // Initialize status
    memset(&s_status, 0, sizeof(s_status));
    strncpy(s_status.status_msg, "Not updated", sizeof(s_status.status_msg) - 1);
    s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
    
    // Load manual location override from NVS
    _load_manual_location_from_nvs();
    
    // Load from cache
    int64_t cache_timestamp = 0;
    if (_load_from_cache(&s_status.location, &s_status.current, &cache_timestamp)) {
        s_status.has_data = true;
        s_status.last_update_time = cache_timestamp;
        
        // Check if stale (>2 hours)
        time_t now = time(NULL);
        if (now - cache_timestamp > WEATHER_STALE_THRESHOLD_S) {
            s_status.is_stale = true;
            ESP_LOGW(TAG, "Cached weather is stale");
        }
        
        // Apply timezone from cache
        ntp_service_set_timezone(s_status.location.timezone_posix);
    }
    
    ESP_LOGI(TAG, "Weather service initialized");
    return WEATHER_ERR_OK;
}

weather_err_t weather_service_refresh(void) {
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;

    /* WHY the busy flag is claimed under the mutex *before* the task is
     * created: previously is_busy was read unlocked and only set once the new
     * worker had already started, so two rapid callers could both pass the
     * check and both call xTaskCreatePinnedToCore(). The second creation
     * overwrote s_worker_task, orphaning the first handle and leaving two
     * workers racing on the same status struct. */
    LOCK();
    if (s_status.is_busy) {
        UNLOCK();
        return WEATHER_ERR_BUSY;
    }
    s_status.is_busy = true;
    UNLOCK();

    /* WHY: a refresh can take up to 3 sequential HTTP calls at 10 s each.
     * The idle timer fires at 50 s, so without this hold the device would
     * deep-sleep mid-fetch and silently discard the result. */
    idle_mgr_disable();

    s_abort_requested = false;

    BaseType_t ret = xTaskCreatePinnedToCore(
        _worker_task,
        "weather_worker",
        WEATHER_TASK_STACK,
        NULL,
        WEATHER_TASK_PRIORITY,
        &s_worker_task,
        WEATHER_TASK_CORE
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task");
        LOCK();
        s_status.is_busy = false;
        strncpy(s_status.status_msg, "Refresh failed to start", sizeof(s_status.status_msg) - 1);
        s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
        UNLOCK();
        /* Balance the hold taken above — no worker exists to release it. */
        idle_mgr_enable();
        return WEATHER_ERR_NETWORK;
    }

    return WEATHER_ERR_OK;
}

weather_err_t weather_service_set_location(const char* city_name, float lat, float lon, const char* tz_iana) {
    /* WHY: callers pass through from App_ClockWeather; a NULL here would reach
     * strncpy() and fault the CPU outright on a no-MMU target. */
    if (!city_name || !tz_iana) {
        ESP_LOGE(TAG, "set_location: city_name or tz_iana is NULL");
        return WEATHER_ERR_PARSE;
    }

    LOCK();
    strncpy(s_status.location.city_name, city_name, sizeof(s_status.location.city_name) - 1);
    s_status.location.city_name[sizeof(s_status.location.city_name) - 1] = '\0';
    s_status.location.latitude = lat;
    s_status.location.longitude = lon;
    strncpy(s_status.location.timezone_iana, tz_iana, sizeof(s_status.location.timezone_iana) - 1);
    s_status.location.timezone_iana[sizeof(s_status.location.timezone_iana) - 1] = '\0';
    
    const char* posix = _iana_to_posix(tz_iana);
    strncpy(s_status.location.timezone_posix, posix, sizeof(s_status.location.timezone_posix) - 1);
    s_status.location.timezone_posix[sizeof(s_status.location.timezone_posix) - 1] = '\0';
    
    // Store as manual override
    s_has_manual_location = true;
    strncpy(s_manual_location.city_name, city_name, sizeof(s_manual_location.city_name) - 1);
    s_manual_location.city_name[sizeof(s_manual_location.city_name) - 1] = '\0';
    s_manual_location.latitude = lat;
    s_manual_location.longitude = lon;
    strncpy(s_manual_location.timezone_iana, tz_iana, sizeof(s_manual_location.timezone_iana) - 1);
    s_manual_location.timezone_iana[sizeof(s_manual_location.timezone_iana) - 1] = '\0';
    strncpy(s_manual_location.timezone_posix, posix, sizeof(s_manual_location.timezone_posix) - 1);
    s_manual_location.timezone_posix[sizeof(s_manual_location.timezone_posix) - 1] = '\0';
    UNLOCK();
    
    _save_manual_location_to_nvs();
    ntp_service_set_timezone(posix);
    ESP_LOGI(TAG, "Location set manually: %s (%.4f, %.4f)", city_name, lat, lon);
    
    return WEATHER_ERR_OK;
}

weather_err_t weather_service_clear_manual_location(void) {
    LOCK();
    s_has_manual_location = false;
    memset(&s_manual_location, 0, sizeof(s_manual_location));
    UNLOCK();
    _save_manual_location_to_nvs();
    ESP_LOGI(TAG, "Manual location cleared — will use IP geolocation");
    return WEATHER_ERR_OK;
}

bool weather_service_has_manual_location(void) {
    /* WHY locked: s_has_manual_location is written under s_mutex by
     * set_location()/clear_manual_location() (Core 0) and read here from the
     * UI task on Core 1. */
    bool has;
    LOCK();
    has = s_has_manual_location;
    UNLOCK();
    return has;
}

weather_err_t weather_service_set_city(const char* city_name) {
    if (!city_name || city_name[0] == '\0') return WEATHER_ERR_PARSE;
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
    // Geocode city name via Open-Meteo Geocoding API (free, no key)
    char url[256];
    snprintf(url, sizeof(url),
        "%s?name=%s&count=1&language=en&format=json",
        WEATHER_GEOCODING_URL, city_name);
    
    ESP_LOGI(TAG, "Geocoding city: %s", city_name);
    
    http_rx_t rx = {0};
    if (!_http_rx_init(&rx)) return WEATHER_ERR_NETWORK;
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &rx,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Geocoding failed: %s, status=%d", esp_err_to_name(err), status);
        _http_rx_free(&rx);
        return WEATHER_ERR_NETWORK;
    }
    
    cJSON* root = cJSON_Parse(rx.buf);
    if (!root) {
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* results = cJSON_GetObjectItem(root, "results");
    if (!results || !cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
        ESP_LOGW(TAG, "No geocoding results for '%s'", city_name);
        cJSON_Delete(root);
        _http_rx_free(&rx);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* first = cJSON_GetArrayItem(results, 0);
    cJSON* lat = cJSON_GetObjectItem(first, "latitude");
    cJSON* lon = cJSON_GetObjectItem(first, "longitude");
    cJSON* name = cJSON_GetObjectItem(first, "name");
    cJSON* tz = cJSON_GetObjectItem(first, "timezone");
    
    if (lat && lon && name) {
        float latitude = (float)lat->valuedouble;
        float longitude = (float)lon->valuedouble;
        const char* display_name = cJSON_IsString(name) ? name->valuestring : city_name;
        const char* timezone = (tz && cJSON_IsString(tz)) ? tz->valuestring : "UTC";

        weather_err_t set_err = weather_service_set_location(display_name, latitude, longitude, timezone);
        ESP_LOGI(TAG, "Geocoded '%s' → %s (%.4f, %.4f), TZ: %s",
                 city_name, display_name, latitude, longitude, timezone);

        cJSON_Delete(root);
        _http_rx_free(&rx);
        /* WHY propagate: the caller must be able to tell that the city was
         * actually applied. Returning OK on a rejected lookup would leave the
         * UI showing an unchanged location with no error indication. */
        return set_err;
    } else {
        ESP_LOGW(TAG, "Geocoding response missing fields for '%s'", city_name);
    }
    
    cJSON_Delete(root);
    _http_rx_free(&rx);
    return WEATHER_ERR_PARSE;
}

weather_err_t weather_service_get_location(weather_location_t* out) {
    if (!out) return WEATHER_ERR_PARSE;
    LOCK();
    *out = s_status.location;
    UNLOCK();
    return WEATHER_ERR_OK;
}

void weather_service_get_status(weather_status_t* out) {
    if (!out) return;
    LOCK();
    *out = s_status;
    UNLOCK();
}

bool weather_service_is_busy(void) {
    bool busy;
    LOCK();
    busy = s_status.is_busy;
    UNLOCK();
    return busy;
}

weather_err_t weather_service_abort(void) {
    if (!s_abort_requested && !s_worker_task) {
        return WEATHER_ERR_OK; /* nothing in flight */
    }

    s_abort_requested = true;
    ESP_LOGI(TAG, "Abort requested — waiting for worker to self-terminate");

    /* WHY the bounded wait: the worker clears s_worker_task immediately before
     * vTaskDelete(NULL). Blocking here guarantees that when this returns, no
     * worker can still be mutating s_status — without it, an immediate
     * refresh() would race the dying worker. Bounded because the worker may be
     * parked inside a blocking esp_http_client_perform() (up to
     * WEATHER_HTTP_TIMEOUT_MS) and cannot be force-killed. */
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(WEATHER_ABORT_WAIT_MS);
    while (s_worker_task != NULL && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_worker_task != NULL) {
        /* Honest failure: the caller must know the worker is still alive, or it
         * may assume the network stack has quiesced when it has not. */
        ESP_LOGW(TAG, "Worker still alive after %d ms — abort is pending",
                 WEATHER_ABORT_WAIT_MS);
        return WEATHER_ERR_BUSY;
    }

    ESP_LOGI(TAG, "Worker terminated cleanly");
    return WEATHER_ERR_OK;
}

const char* weather_code_to_description(int code) {
    if (code == 0) return "Clear";
    if (code >= 1 && code <= 3) return "Partly Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 85 && code <= 86) return "Snow Showers";
    if (code >= 95) return "Thunderstorm";
    return "Unknown";
}

const char* weather_code_to_icon(int code) {
    if (code == 0) return "☀";
    if (code >= 1 && code <= 3) return "⛅";
    if (code == 45 || code == 48) return "🌫";
    if (code >= 51 && code <= 57) return "🌦";
    if (code >= 61 && code <= 67) return "🌧";
    if (code >= 71 && code <= 77) return "❄";
    if (code >= 80 && code <= 82) return "🌧";
    if (code >= 85 && code <= 86) return "❄";
    if (code >= 95) return "⛈";
    return "?";
}
