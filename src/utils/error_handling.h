#pragma once

#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced error checking with logging
#define ESP_ERROR_CHECK_LOG(x, tag, msg) do { \
    esp_err_t err_rc_ = (x); \
    if (unlikely(err_rc_ != ESP_OK)) { \
        ESP_LOGE(tag, "%s failed: %s (0x%x) at %s:%d", \
                 msg, esp_err_to_name(err_rc_), err_rc_, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

// Error checking with return on failure
#define ESP_ERROR_RETURN(x, tag, msg, ret_val) do { \
    esp_err_t err_rc_ = (x); \
    if (unlikely(err_rc_ != ESP_OK)) { \
        ESP_LOGE(tag, "%s failed: %s (0x%x)", msg, esp_err_to_name(err_rc_), err_rc_); \
        return ret_val; \
    } \
} while(0)

// Error checking with warning (non-fatal)
#define ESP_ERROR_WARN(x, tag, msg) do { \
    esp_err_t err_rc_ = (x); \
    if (unlikely(err_rc_ != ESP_OK)) { \
        ESP_LOGW(tag, "%s failed: %s (0x%x)", msg, esp_err_to_name(err_rc_), err_rc_); \
    } \
} while(0)

// Safe wrapper for ESP-IDF calls with automatic logging
static inline esp_err_t esp_safe_call(esp_err_t err, const char* tag, const char* func_name) {
    if (err != ESP_OK) {
        ESP_LOGE(tag, "%s failed: %s (0x%x)", func_name, esp_err_to_name(err), err);
    }
    return err;
}

#ifdef __cplusplus
}
#endif
