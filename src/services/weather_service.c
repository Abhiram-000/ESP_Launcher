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
#include "../config/sys_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

// HTTP response buffer (PSRAM)
#define HTTP_RECV_BUF_SIZE 4096
static char* s_http_buffer = NULL;
static int s_http_buffer_len = 0;

// Helper macros
#define LOCK()   if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY)
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
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_http_buffer_len + evt->data_len < HTTP_RECV_BUF_SIZE) {
            memcpy(s_http_buffer + s_http_buffer_len, evt->data, evt->data_len);
            s_http_buffer_len += evt->data_len;
            s_http_buffer[s_http_buffer_len] = '\0';
        }
    }
    return ESP_OK;
}

static weather_err_t _fetch_ip_geolocation(weather_location_t* loc) {
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
    ESP_LOGI(TAG, "Fetching IP geolocation from ip-api.com");
    
    s_http_buffer_len = 0;
    memset(s_http_buffer, 0, HTTP_RECV_BUF_SIZE);
    
    esp_http_client_config_t config = {
        .url = WEATHER_IP_GEO_URL,
        .event_handler = _http_event_handler,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "IP geolocation failed: %s, status=%d", esp_err_to_name(err), status);
        return WEATHER_ERR_NETWORK;
    }
    
    // Parse JSON
    cJSON* root = cJSON_Parse(s_http_buffer);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* status_field = cJSON_GetObjectItem(root, "status");
    if (!status_field || strcmp(status_field->valuestring, "success") != 0) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Geolocation API returned error");
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* city = cJSON_GetObjectItem(root, "city");
    cJSON* lat = cJSON_GetObjectItem(root, "lat");
    cJSON* lon = cJSON_GetObjectItem(root, "lon");
    cJSON* tz = cJSON_GetObjectItem(root, "timezone");
    cJSON* country = cJSON_GetObjectItem(root, "country");
    
    if (city && lat && lon && tz) {
        strncpy(loc->city_name, city->valuestring, sizeof(loc->city_name) - 1);
        loc->latitude = lat->valuedouble;
        loc->longitude = lon->valuedouble;
        strncpy(loc->timezone_iana, tz->valuestring, sizeof(loc->timezone_iana) - 1);
        if (country) strncpy(loc->country, country->valuestring, sizeof(loc->country) - 1);
        
        // Convert IANA to POSIX
        const char* posix = _iana_to_posix(loc->timezone_iana);
        strncpy(loc->timezone_posix, posix, sizeof(loc->timezone_posix) - 1);
        
        ESP_LOGI(TAG, "Location: %s (%.2f, %.2f), TZ: %s → %s",
                 loc->city_name, loc->latitude, loc->longitude,
                 loc->timezone_iana, loc->timezone_posix);
    } else {
        cJSON_Delete(root);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON_Delete(root);
    return WEATHER_ERR_OK;
}

static weather_err_t _fetch_weather(const weather_location_t* loc, weather_current_t* wx) {
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
    char url[512];
    snprintf(url, sizeof(url),
        "%s?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
        "&timezone=auto",
        WEATHER_FORECAST_URL, loc->latitude, loc->longitude);
    
    ESP_LOGI(TAG, "Fetching weather from Open-Meteo");
    
    s_http_buffer_len = 0;
    memset(s_http_buffer, 0, HTTP_RECV_BUF_SIZE);
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
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
        return WEATHER_ERR_NETWORK;
    }
    
    // Parse JSON
    cJSON* root = cJSON_Parse(s_http_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Weather JSON parse failed");
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* current = cJSON_GetObjectItem(root, "current");
    if (!current) {
        cJSON_Delete(root);
        return WEATHER_ERR_PARSE;
    }
    
    cJSON* temp = cJSON_GetObjectItem(current, "temperature_2m");
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
        return WEATHER_ERR_PARSE;
    }
    
    cJSON_Delete(root);
    return WEATHER_ERR_OK;
}

static void _save_to_cache(const weather_location_t* loc, const weather_current_t* wx) {
    FILE* f = fopen(WEATHER_CONFIG_PATH, "w");
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
    
    ESP_LOGI(TAG, "Weather cached to %s", WEATHER_CONFIG_PATH);
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
    
    char* buffer = malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }
    
    fread(buffer, 1, fsize, f);
    buffer[fsize] = '\0';
    fclose(f);
    
    cJSON* root = cJSON_Parse(buffer);
    free(buffer);
    
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
        loc->latitude = lat->valuedouble;
        loc->longitude = lon->valuedouble;
        strncpy(loc->timezone_iana, tz_iana->valuestring, sizeof(loc->timezone_iana) - 1);
        strncpy(loc->timezone_posix, tz_posix->valuestring, sizeof(loc->timezone_posix) - 1);
        if (country) strncpy(loc->country, country->valuestring, sizeof(loc->country) - 1);
        
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
    UNLOCK();
    
    weather_location_t loc = {0};
    weather_current_t wx = {0};
    
    // Step 1: Get location
    weather_err_t err = _fetch_ip_geolocation(&loc);
    if (err != WEATHER_ERR_OK && !s_abort_requested) {
        LOCK();
        strncpy(s_status.status_msg, "Location fetch failed", sizeof(s_status.status_msg) - 1);
        UNLOCK();
        ESP_LOGW(TAG, "Using cached location");
    }
    
    if (s_abort_requested) goto cleanup;
    
    // Step 2: Get weather
    LOCK();
    strncpy(s_status.status_msg, "Fetching weather...", sizeof(s_status.status_msg) - 1);
    UNLOCK();
    
    if (err == WEATHER_ERR_OK) {
        err = _fetch_weather(&loc, &wx);
        if (err != WEATHER_ERR_OK) {
            LOCK();
            strncpy(s_status.status_msg, "Weather fetch failed", sizeof(s_status.status_msg) - 1);
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
    
    // Allocate HTTP buffer in PSRAM
    if (!s_http_buffer) {
        s_http_buffer = heap_caps_malloc(HTTP_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_http_buffer) {
            ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
            return WEATHER_ERR_NETWORK;
        }
    }
    
    // Initialize status
    memset(&s_status, 0, sizeof(s_status));
    strncpy(s_status.status_msg, "Not updated", sizeof(s_status.status_msg) - 1);
    
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
    if (s_status.is_busy) return WEATHER_ERR_BUSY;
    if (!wifi_mgr_is_connected()) return WEATHER_ERR_NO_WIFI;
    
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
        return WEATHER_ERR_NETWORK;
    }
    
    return WEATHER_ERR_OK;
}

weather_err_t weather_service_set_location(const char* city_name, float lat, float lon, const char* tz_iana) {
    LOCK();
    strncpy(s_status.location.city_name, city_name, sizeof(s_status.location.city_name) - 1);
    s_status.location.latitude = lat;
    s_status.location.longitude = lon;
    strncpy(s_status.location.timezone_iana, tz_iana, sizeof(s_status.location.timezone_iana) - 1);
    
    const char* posix = _iana_to_posix(tz_iana);
    strncpy(s_status.location.timezone_posix, posix, sizeof(s_status.location.timezone_posix) - 1);
    UNLOCK();
    
    ntp_service_set_timezone(posix);
    ESP_LOGI(TAG, "Location set manually: %s", city_name);
    
    return WEATHER_ERR_OK;
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
    s_abort_requested = true;
    ESP_LOGI(TAG, "Abort requested");
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
