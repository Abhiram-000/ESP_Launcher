/**
 * @file pomodoro_service.c
 * @brief Pomodoro Timer Service Implementation
 */

#include "pomodoro_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

static const char* TAG = "POMO_SVC";

// NVS keys
#define NVS_NAMESPACE       "pomodoro"
#define NVS_KEY_STATS       "stats"
#define NVS_KEY_CUSTOM_CFG  "custom_cfg"

// Mode presets
static const pomo_mode_config_t MODE_PRESETS[POMO_MODE_COUNT] = {
    [POMO_MODE_QUICK]    = { 15,  3, 10, 4 },
    [POMO_MODE_STANDARD] = { 25,  5, 15, 4 },
    [POMO_MODE_EXTENDED] = { 50, 10, 30, 4 },
    [POMO_MODE_CUSTOM]   = { 25,  5, 15, 4 }  // Default, overwritten from NVS
};

static const char* MODE_NAMES[POMO_MODE_COUNT] = {
    "Quick", "Standard", "Extended", "Custom"
};

static const char* SESSION_NAMES[3] = {
    "FOCUS", "SHORT BREAK", "LONG BREAK"
};

// State
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static pomo_mode_config_t s_custom_config;

static struct {
    pomo_state_t state;
    pomo_mode_t mode;
    pomo_session_type_t session_type;
    uint32_t remaining_seconds;
    uint32_t total_seconds;
    uint8_t cycles_completed;
    bool auto_progress;
    uint64_t last_update_us;  // esp_timer timestamp of last tick
} s_timer;

static pomo_statistics_t s_stats;

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

static uint64_t _get_time_us(void) {
    return esp_timer_get_time();
}

static void _save_stats_to_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_STATS, &s_stats, sizeof(s_stats));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void _load_stats_from_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_stats);
        if (nvs_get_blob(h, NVS_KEY_STATS, &s_stats, &sz) != ESP_OK) {
            memset(&s_stats, 0, sizeof(s_stats));
        }
        nvs_close(h);
    } else {
        memset(&s_stats, 0, sizeof(s_stats));
    }
}

static void _save_custom_config_to_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_CUSTOM_CFG, &s_custom_config, sizeof(s_custom_config));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void _load_custom_config_from_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_custom_config);
        if (nvs_get_blob(h, NVS_KEY_CUSTOM_CFG, &s_custom_config, &sz) != ESP_OK) {
            s_custom_config = MODE_PRESETS[POMO_MODE_CUSTOM];
        }
        nvs_close(h);
    } else {
        s_custom_config = MODE_PRESETS[POMO_MODE_CUSTOM];
    }
}

static void _check_daily_reset(void) {
    time_t now;
    time(&now);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    
    struct tm last_tm;
    localtime_r(&s_stats.last_reset_date, &last_tm);
    
    // Different day?
    if (now_tm.tm_yday != last_tm.tm_yday || now_tm.tm_year != last_tm.tm_year) {
        // Check if we maintained streak (within 48h window for flexibility)
        if (s_stats.last_session_timestamp > 0) {
            uint32_t gap = (uint32_t)(now - s_stats.last_session_timestamp);
            if (gap > 48 * 3600) {
                s_stats.current_streak = 0;  // Streak broken
            }
        }
        
        s_stats.sessions_today = 0;
        s_stats.last_reset_date = now;
        _save_stats_to_nvs();
        ESP_LOGI(TAG, "Daily stats reset (new day)");
    }
}

static void _record_session_complete(bool already_locked) {
    if (!already_locked) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Mutex timeout");
            return;
        }
    }
    
    if (s_timer.session_type == POMO_SESSION_FOCUS) {
        s_stats.total_focus_seconds += s_timer.total_seconds;
        s_stats.total_sessions_completed++;
        s_stats.sessions_today++;
        
        time_t now;
        time(&now);
        s_stats.last_session_timestamp = (uint32_t)now;
        
        // Streak logic: if today's first session, increment streak
        if (s_stats.sessions_today == 1) {
            s_stats.current_streak++;
            if (s_stats.current_streak > s_stats.longest_streak) {
                s_stats.longest_streak = s_stats.current_streak;
            }
        }
        
        _save_stats_to_nvs();
        ESP_LOGI(TAG, "Focus session completed: %lus | Total: %lu sessions, %lu hours",
                 (unsigned long)s_timer.total_seconds,
                 (unsigned long)s_stats.total_sessions_completed,
                 (unsigned long)(s_stats.total_focus_seconds / 3600));
    }
    
    if (!already_locked) {
        xSemaphoreGive(s_mutex);
    }
}

static pomo_session_type_t _next_session_type(void) {
    const pomo_mode_config_t* cfg = (s_timer.mode == POMO_MODE_CUSTOM) 
        ? &s_custom_config 
        : &MODE_PRESETS[s_timer.mode];
    
    if (s_timer.session_type == POMO_SESSION_FOCUS) {
        s_timer.cycles_completed++;
        if (s_timer.cycles_completed >= cfg->cycles_until_long_break) {
            s_timer.cycles_completed = 0;
            return POMO_SESSION_LONG_BREAK;
        }
        return POMO_SESSION_SHORT_BREAK;
    }
    return POMO_SESSION_FOCUS;
}

static uint32_t _get_session_duration(pomo_mode_t mode, pomo_session_type_t type) {
    const pomo_mode_config_t* cfg = (mode == POMO_MODE_CUSTOM) 
        ? &s_custom_config 
        : &MODE_PRESETS[mode];
    
    switch (type) {
        case POMO_SESSION_FOCUS:       return cfg->focus_minutes * 60;
        case POMO_SESSION_SHORT_BREAK: return cfg->short_break_minutes * 60;
        case POMO_SESSION_LONG_BREAK:  return cfg->long_break_minutes * 60;
        default: return 0;
    }
}

static void _start_session_internal(pomo_mode_t mode, pomo_session_type_t type) {
    s_timer.state = POMO_STATE_RUNNING;
    s_timer.mode = mode;
    s_timer.session_type = type;
    s_timer.total_seconds = _get_session_duration(mode, type);
    s_timer.remaining_seconds = s_timer.total_seconds;
    s_timer.last_update_us = _get_time_us();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API Implementation
// ─────────────────────────────────────────────────────────────────────────────

pomo_err_t pomodoro_service_init(void) {
    if (s_initialized) return POMO_ERR_OK;
    
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Mutex creation failed");
        return POMO_ERR_STORAGE;
    }
    
    _load_stats_from_nvs();
    _load_custom_config_from_nvs();
    _check_daily_reset();
    
    memset(&s_timer, 0, sizeof(s_timer));
    s_timer.state = POMO_STATE_IDLE;
    s_timer.auto_progress = true;
    
    s_initialized = true;
    ESP_LOGI(TAG, "Pomodoro service initialized | Total: %lu sessions, %lu hours",
             (unsigned long)s_stats.total_sessions_completed,
             (unsigned long)(s_stats.total_focus_seconds / 3600));
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_start_session(pomo_mode_t mode, pomo_session_type_t session_type) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    if (mode >= POMO_MODE_COUNT) return POMO_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    _check_daily_reset();
    _start_session_internal(mode, session_type);
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Session started: %s | %s | %lus",
             MODE_NAMES[mode], SESSION_NAMES[session_type],
             (unsigned long)s_timer.total_seconds);
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_pause(void) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    if (s_timer.state == POMO_STATE_RUNNING) {
        s_timer.state = POMO_STATE_PAUSED;
        ESP_LOGI(TAG, "Timer paused");
    }
    xSemaphoreGive(s_mutex);
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_resume(void) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    if (s_timer.state == POMO_STATE_PAUSED) {
        s_timer.state = POMO_STATE_RUNNING;
        s_timer.last_update_us = _get_time_us();
        ESP_LOGI(TAG, "Timer resumed");
    }
    xSemaphoreGive(s_mutex);
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_skip(void) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    if (s_timer.state == POMO_STATE_IDLE) {
        xSemaphoreGive(s_mutex);
        return POMO_ERR_INVALID_ARG;
    }
    
    // Mark session as completed (for stats if focus) - already holding mutex
    _record_session_complete(true);
    
    // Progress to next session
    pomo_session_type_t next = _next_session_type();
    _start_session_internal(s_timer.mode, next);
    
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Session skipped → %s", SESSION_NAMES[next]);
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_stop(void) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    s_timer.state = POMO_STATE_IDLE;
    s_timer.remaining_seconds = 0;
    s_timer.cycles_completed = 0;
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Timer stopped");
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_adjust_time(int32_t delta_seconds) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    if (s_timer.state == POMO_STATE_IDLE) {
        xSemaphoreGive(s_mutex);
        return POMO_ERR_INVALID_ARG;
    }
    
    int32_t new_time = (int32_t)s_timer.remaining_seconds + delta_seconds;
    if (new_time < 1) new_time = 1;
    if (new_time > 7200) new_time = 7200;  // Max 2 hours
    
    s_timer.remaining_seconds = (uint32_t)new_time;
    xSemaphoreGive(s_mutex);
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_set_auto_progress(bool enabled) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    s_timer.auto_progress = enabled;
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Auto-progression %s", enabled ? "ENABLED" : "DISABLED");
    return POMO_ERR_OK;
}

void pomodoro_get_mode_config(pomo_mode_t mode, pomo_mode_config_t* out) {
    if (!out || mode >= POMO_MODE_COUNT) return;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }
    if (mode == POMO_MODE_CUSTOM) {
        *out = s_custom_config;
    } else {
        *out = MODE_PRESETS[mode];
    }
    xSemaphoreGive(s_mutex);
}

pomo_err_t pomodoro_set_custom_config(const pomo_mode_config_t* config) {
    if (!s_initialized || !config) return POMO_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    s_custom_config = *config;
    _save_custom_config_to_nvs();
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Custom config saved: %u/%u/%u, cycles=%u",
             config->focus_minutes, config->short_break_minutes,
             config->long_break_minutes, config->cycles_until_long_break);
    return POMO_ERR_OK;
}

void pomodoro_get_status(pomo_status_t* out) {
    if (!out) return;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }
    
    // Update timer if running
    if (s_timer.state == POMO_STATE_RUNNING) {
        uint64_t now_us = _get_time_us();
        uint64_t elapsed_us = now_us - s_timer.last_update_us;
        uint32_t elapsed_sec = (uint32_t)(elapsed_us / 1000000);
        
        if (elapsed_sec > 0) {
            if (s_timer.remaining_seconds > elapsed_sec) {
                s_timer.remaining_seconds -= elapsed_sec;
            } else {
                s_timer.remaining_seconds = 0;
                s_timer.state = POMO_STATE_COMPLETED;
                _record_session_complete(true);  // Already holding mutex
                
                // Auto-progress if enabled
                if (s_timer.auto_progress) {
                    pomo_session_type_t next = _next_session_type();
                    _start_session_internal(s_timer.mode, next);
                    ESP_LOGI(TAG, "Auto-progressed to %s", SESSION_NAMES[next]);
                }
            }
            s_timer.last_update_us = now_us;
        }
    }
    
    out->state = s_timer.state;
    out->current_mode = s_timer.mode;
    out->session_type = s_timer.session_type;
    out->remaining_seconds = s_timer.remaining_seconds;
    out->total_seconds = s_timer.total_seconds;
    out->cycles_completed = s_timer.cycles_completed;
    out->auto_progress = s_timer.auto_progress;
    out->stats = s_stats;
    
    xSemaphoreGive(s_mutex);
}

pomo_err_t pomodoro_reset_daily_stats(void) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    s_stats.sessions_today = 0;
    time(&s_stats.last_reset_date);
    _save_stats_to_nvs();
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Daily stats reset");
    return POMO_ERR_OK;
}

pomo_err_t pomodoro_clear_all_stats(void) {
    if (!s_initialized) return POMO_ERR_NOT_INIT;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return POMO_ERR_NOT_INIT;
    }
    memset(&s_stats, 0, sizeof(s_stats));
    _save_stats_to_nvs();
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "All statistics cleared");
    return POMO_ERR_OK;
}

const char* pomodoro_session_name(pomo_session_type_t type) {
    if (type < 3) return SESSION_NAMES[type];
    return "UNKNOWN";
}

const char* pomodoro_mode_name(pomo_mode_t mode) {
    if (mode < POMO_MODE_COUNT) return MODE_NAMES[mode];
    return "UNKNOWN";
}
