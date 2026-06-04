/*
 * weather_service.h — Weather Service with IP Geolocation & Open-Meteo
 * =====================================================================
 * Features:
 *   - Auto-detect location from IP via ip-api.com (HTTP, no key)
 *   - Fetch weather from Open-Meteo (HTTPS, no key)
 *   - IANA → POSIX timezone conversion
 *   - LittleFS caching for offline operation
 *   - Async worker task pattern (non-blocking UI)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float  temperature;      // °C
    float  wind_speed;       // km/h
    int    humidity;         // %
    int    weather_code;     // WMO code (0-99)
    int    is_day;           // 1=day, 0=night
} weather_current_t;

typedef struct {
    char   city_name[48];
    float  latitude;
    float  longitude;
    char   timezone_iana[64];   // e.g., "Asia/Kolkata"
    char   timezone_posix[64];  // e.g., "IST-5:30"
    char   country[48];
} weather_location_t;

typedef struct {
    bool                is_busy;
    bool                has_data;
    bool                is_stale;           // true if data > 2 hours old
    weather_current_t   current;
    weather_location_t  location;
    char                status_msg[64];
    int64_t             last_update_time;   // Unix seconds
} weather_status_t;

typedef enum {
    WEATHER_ERR_OK = 0,
    WEATHER_ERR_NO_WIFI,
    WEATHER_ERR_NETWORK,
    WEATHER_ERR_NO_LOCATION,
    WEATHER_ERR_PARSE,
    WEATHER_ERR_BUSY,
    WEATHER_ERR_ABORTED,
} weather_err_t;

/**
 * @brief Initialize weather service.
 * 
 * Mounts LittleFS, loads cached location and weather data.
 * Does NOT start network operations — call weather_service_refresh().
 * 
 * @return WEATHER_ERR_OK on success.
 */
weather_err_t weather_service_init(void);

/**
 * @brief Refresh weather data (non-blocking).
 * 
 * Starts a worker task on Core 0 that:
 *   1. Detects location via ip-api.com (if not cached)
 *   2. Converts IANA timezone to POSIX
 *   3. Fetches weather from Open-Meteo
 *   4. Caches results to LittleFS
 * 
 * UI should poll weather_service_get_status() for completion.
 * 
 * @return WEATHER_ERR_OK if task started, error otherwise.
 */
weather_err_t weather_service_refresh(void);

/**
 * @brief Set location manually (for user override).
 * 
 * @param city_name Display name (e.g., "London").
 * @param lat Latitude.
 * @param lon Longitude.
 * @param tz_iana IANA timezone (e.g., "Europe/London").
 * @return WEATHER_ERR_OK on success.
 */
weather_err_t weather_service_set_location(const char* city_name, float lat, float lon, const char* tz_iana);

/**
 * @brief Get current location (from cache or last fetch).
 * 
 * @param out Pointer to location struct to populate.
 * @return WEATHER_ERR_OK if location available, error otherwise.
 */
weather_err_t weather_service_get_location(weather_location_t* out);

/**
 * @brief Get current weather service status (for UI polling).
 * 
 * Thread-safe. Called from UI task (Core 1) while worker runs on Core 0.
 * 
 * @param out Pointer to status struct to populate.
 */
void weather_service_get_status(weather_status_t* out);

/**
 * @brief Check if a refresh operation is in progress.
 * 
 * @return true if busy, false otherwise.
 */
bool weather_service_is_busy(void);

/**
 * @brief Abort ongoing refresh operation.
 * 
 * Cooperative abort — the worker task will self-terminate cleanly.
 * 
 * @return WEATHER_ERR_OK on success.
 */
weather_err_t weather_service_abort(void);

/**
 * @brief Map WMO weather code to human-readable description.
 * 
 * @param code WMO code (0-99).
 * @return Description string (e.g., "Clear", "Rain", "Snow").
 */
const char* weather_code_to_description(int code);

/**
 * @brief Map WMO weather code to icon character/emoji.
 * 
 * @param code WMO code (0-99).
 * @return Icon string (e.g., "☀", "🌧", "❄").
 */
const char* weather_code_to_icon(int code);

#ifdef __cplusplus
}
#endif
