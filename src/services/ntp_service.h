/*
 * ntp_service.h — NTP Time Service with Three-Layer Persistence
 * ==============================================================
 * Layer 1: NTP sync (exact, online)
 * Layer 2: NVS + esp_timer delta (1 min accuracy, offline)
 * Layer 3: Continuous re-sync (hourly correction)
 *
 * Dependencies: esp_sntp, nvs, esp_timer
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NTP_ERR_OK = 0,
    NTP_ERR_NOT_SYNCED,
    NTP_ERR_WIFI_NOT_CONNECTED,
    NTP_ERR_NVS_ERROR,
} ntp_err_t;

/**
 * @brief Initialize SNTP client with multiple servers and NVS persistence.
 * 
 * Starts SNTP with pool.ntp.org, time.google.com, time.windows.com.
 * Attempts to restore time from NVS if available.
 * 
 * @return NTP_ERR_OK on success, error code otherwise.
 */
ntp_err_t ntp_service_init(void);

/**
 * @brief Force immediate NTP sync (non-blocking).
 * 
 * Triggers an SNTP sync request. Check ntp_service_is_synced() after
 * a few seconds to see if it succeeded.
 * 
 * @return NTP_ERR_OK if request sent, error code otherwise.
 */
ntp_err_t ntp_service_sync(void);

/**
 * @brief Check if NTP has successfully synced at least once since boot.
 * 
 * @return true if synced, false otherwise.
 */
bool ntp_service_is_synced(void);

/**
 * @brief Get current UTC epoch time in seconds.
 * 
 * Returns 0 if time has never been set (neither NTP nor NVS restore).
 * 
 * @return Unix timestamp (seconds since 1970-01-01 00:00:00 UTC), or 0 if unknown.
 */
time_t ntp_service_get_time(void);

/**
 * @brief Get local time with timezone applied.
 * 
 * @param out Pointer to struct tm to populate.
 */
void ntp_service_get_local_time(struct tm* out);

/**
 * @brief Save current time to NVS for persistence across reboots.
 * 
 * Saves epoch_seconds + esp_timer_get_time() to reconstruct time on next boot.
 * Call this periodically (every 60s) and before deep sleep.
 */
void ntp_service_save_to_nvs(void);

/**
 * @brief Restore time from NVS on boot (before NTP sync).
 * 
 * Computes elapsed time since last save using esp_timer delta.
 * Validates restored time (sanity checks) before applying.
 * 
 * @return true if time was restored and valid, false otherwise.
 */
bool ntp_service_restore_from_nvs(void);

/**
 * @brief Set timezone using POSIX TZ string.
 * 
 * Examples:
 *   "IST-5:30"                         (India Standard, no DST)
 *   "EST5EDT,M3.2.0,M11.1.0"           (US Eastern with DST)
 *   "CET-1CEST,M3.5.0,M10.5.0/3"       (Central Europe with DST)
 * 
 * @param posix_tz POSIX timezone string (NOT IANA name).
 * @return NTP_ERR_OK on success.
 */
ntp_err_t ntp_service_set_timezone(const char* posix_tz);

#ifdef __cplusplus
}
#endif
