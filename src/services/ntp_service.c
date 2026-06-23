/*
 * ntp_service.c — NTP Time Service Implementation
 * ================================================
 * Three-layer time accuracy:
 *   1. NTP sync (online, ±10-100ms)
 *   2. NVS + esp_timer delta (offline, ~1 min accuracy after days)
 *   3. Continuous hourly re-sync (online correction)
 */
#include "ntp_service.h"
#include "wifi_mgr.h"
#include "../config/sys_config.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char* TAG = "NTP_SVC";

static volatile bool s_ntp_synced = false;
static volatile bool s_initialized = false;

// Sanity bounds for restored time validation — defined in sys_config.h
// NTP_EPOCH_MIN, NTP_EPOCH_MAX, NTP_BUILD_MIN

// SNTP sync notification callback
static void _sntp_sync_time_cb(struct timeval *tv) {
    s_ntp_synced = true;
    ESP_LOGI(TAG, "NTP synced: %lld.%06ld", (long long)tv->tv_sec, tv->tv_usec);
    ntp_service_save_to_nvs();
}

ntp_err_t ntp_service_init(void) {
    if (s_initialized) return NTP_ERR_OK;
    
    ESP_LOGI(TAG, "Initializing NTP service with 3 servers");
    
    // Attempt to restore time from NVS first (instant, offline-capable)
    if (ntp_service_restore_from_nvs()) {
        ESP_LOGI(TAG, "Time restored from NVS");
    } else {
        ESP_LOGW(TAG, "No valid NVS time found, awaiting NTP sync");
    }
    
    // Configure SNTP with multiple servers
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER_1);
    esp_sntp_setservername(1, NTP_SERVER_2);
    esp_sntp_setservername(2, NTP_SERVER_3);

    // Set callback for sync notification
    sntp_set_time_sync_notification_cb(_sntp_sync_time_cb);

    // Start SNTP
    esp_sntp_init();
    
    s_initialized = true;
    ESP_LOGI(TAG, "SNTP started with 3 servers");
    return NTP_ERR_OK;
}

ntp_err_t ntp_service_sync(void) {
    if (!s_initialized) return NTP_ERR_NOT_SYNCED;
    if (!wifi_mgr_is_connected()) return NTP_ERR_WIFI_NOT_CONNECTED;
    
    // SNTP auto-syncs periodically based on CONFIG_LWIP_SNTP_UPDATE_DELAY
    ESP_LOGI(TAG, "NTP sync in progress (automatic)");
    return NTP_ERR_OK;
}

bool ntp_service_is_synced(void) {
    return s_ntp_synced;
}

time_t ntp_service_get_time(void) {
    return time(NULL);
}

void ntp_service_get_local_time(struct tm* out) {
    if (!out) return;
    time_t now = time(NULL);
    localtime_r(&now, out);
}

void ntp_service_save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NTP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    
    time_t now = time(NULL);
    int64_t timer_us = esp_timer_get_time();

    esp_err_t e1 = nvs_set_i64(handle, NTP_NVS_KEY_EPOCH,    (int64_t)now);
    esp_err_t e2 = nvs_set_i64(handle, NTP_NVS_KEY_TIMER_US, timer_us);
    esp_err_t e3 = (e1 == ESP_OK && e2 == ESP_OK) ? nvs_commit(handle) : ESP_FAIL;
    nvs_close(handle);

    if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK) {
        ESP_LOGW(TAG, "NVS write failed: set_epoch=%s set_timer=%s commit=%s",
                 esp_err_to_name(e1), esp_err_to_name(e2), esp_err_to_name(e3));
        return;
    }
    ESP_LOGD(TAG, "Time saved to NVS: epoch=%lld, timer_us=%lld", (long long)now, (long long)timer_us);
}

bool ntp_service_restore_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NTP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS open failed (expected on first boot): %s", esp_err_to_name(err));
        return false;
    }
    
    int64_t saved_epoch = 0;
    int64_t saved_timer_us = 0;
    
    err = nvs_get_i64(handle, NTP_NVS_KEY_EPOCH, &saved_epoch);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGD(TAG, "No saved epoch in NVS");
        return false;
    }
    
    err = nvs_get_i64(handle, NTP_NVS_KEY_TIMER_US, &saved_timer_us);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No saved timer_us in NVS");
        return false;
    }
    
    // Compute elapsed time since save
    int64_t current_timer_us = esp_timer_get_time();
    int64_t delta_us = current_timer_us - saved_timer_us;
    
    // Sanity check: delta should not be negative (timer reset) or absurdly large (>7 days)
    if (delta_us < 0 || delta_us > (7LL * 86400LL * 1000000LL)) {
        ESP_LOGW(TAG, "NVS delta out of range: %lld us, discarding", (long long)delta_us);
        return false;
    }
    
    time_t restored_time = (time_t)(saved_epoch + (delta_us / 1000000LL));
    
    // Validate restored time
    if (restored_time < NTP_EPOCH_MIN || restored_time > NTP_EPOCH_MAX) {
        ESP_LOGW(TAG, "Restored time out of bounds: %lld, discarding", (long long)restored_time);
        return false;
    }
    
    if (restored_time < NTP_BUILD_MIN) {
        ESP_LOGW(TAG, "Restored time before build date: %lld, discarding", (long long)restored_time);
        return false;
    }
    
    // Apply restored time
    struct timeval tv = {
        .tv_sec = restored_time,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    s_ntp_synced = true;
    
    ESP_LOGI(TAG, "Time restored: %lld (delta: %lld s)", (long long)restored_time, delta_us / 1000000LL);
    return true;
}

ntp_err_t ntp_service_set_timezone(const char* posix_tz) {
    if (!posix_tz || posix_tz[0] == '\0') {
        ESP_LOGW(TAG, "Empty timezone string");
        return NTP_ERR_NVS_ERROR;
    }
    
    setenv("TZ", posix_tz, 1);
    tzset();
    
    ESP_LOGI(TAG, "Timezone set: %s", posix_tz);
    
    // Save to NVS for persistence
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NTP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        esp_err_t e1 = nvs_set_str(handle, "tz_posix", posix_tz);
        esp_err_t e2 = (e1 == ESP_OK) ? nvs_commit(handle) : ESP_FAIL;
        nvs_close(handle);
        if (e1 != ESP_OK || e2 != ESP_OK) {
            ESP_LOGW(TAG, "NVS timezone write failed: set=%s commit=%s",
                     esp_err_to_name(e1), esp_err_to_name(e2));
        }
    }
    
    return NTP_ERR_OK;
}
