/**
 * @file pomodoro_service.h
 * @brief Production-Grade Pomodoro Timer Service with Statistics & Persistence
 * 
 * Features:
 * - Multiple timer modes (Quick, Standard, Extended, Custom)
 * - Auto-progression through work/break cycles
 * - Comprehensive statistics tracking
 * - NVS-backed persistence (statistics + custom settings)
 * - Pause/Resume/Skip with confirmation
 * - Dynamic time adjustment during session
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Type Definitions
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    POMO_ERR_OK = 0,
    POMO_ERR_NOT_INIT,
    POMO_ERR_INVALID_ARG,
    POMO_ERR_STORAGE,
    POMO_ERR_BUSY
} pomo_err_t;

typedef enum {
    POMO_MODE_QUICK = 0,     // 15min work, 3min short, 10min long
    POMO_MODE_STANDARD,      // 25min work, 5min short, 15min long
    POMO_MODE_EXTENDED,      // 50min work, 10min short, 30min long
    POMO_MODE_CUSTOM,        // User-defined durations
    POMO_MODE_COUNT
} pomo_mode_t;

typedef enum {
    POMO_SESSION_FOCUS = 0,
    POMO_SESSION_SHORT_BREAK,
    POMO_SESSION_LONG_BREAK
} pomo_session_type_t;

typedef enum {
    POMO_STATE_IDLE = 0,      // Not started
    POMO_STATE_RUNNING,       // Timer active
    POMO_STATE_PAUSED,        // Paused by user
    POMO_STATE_COMPLETED      // Session just completed (awaiting progression)
} pomo_state_t;

typedef struct {
    uint16_t focus_minutes;
    uint16_t short_break_minutes;
    uint16_t long_break_minutes;
    uint8_t  cycles_until_long_break;  // Default: 4
} pomo_mode_config_t;

typedef struct {
    uint32_t total_focus_seconds;       // Lifetime focus time
    uint32_t total_sessions_completed;  // Lifetime session count
    uint16_t sessions_today;            // Daily counter (reset at midnight)
    uint16_t current_streak;            // Consecutive days with ≥1 session
    uint16_t longest_streak;            // All-time best streak
    uint32_t last_session_timestamp;    // Unix epoch of last session end
    time_t   last_reset_date;           // Date of last daily reset (midnight check)
} pomo_statistics_t;

typedef struct {
    pomo_state_t        state;
    pomo_mode_t         current_mode;
    pomo_session_type_t session_type;
    uint32_t            remaining_seconds;
    uint32_t            total_seconds;       // For progress calculation
    uint8_t             cycles_completed;    // Current cycle count (resets after long break)
    bool                auto_progress;       // Auto-start next session
    pomo_statistics_t   stats;
} pomo_status_t;

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Initialize Pomodoro service (load stats from NVS)
 */
pomo_err_t pomodoro_service_init(void);

/**
 * @brief Start a session with the specified mode
 * @param mode Timer mode to use
 * @param session_type Initial session type (usually FOCUS)
 */
pomo_err_t pomodoro_start_session(pomo_mode_t mode, pomo_session_type_t session_type);

/**
 * @brief Pause the current session
 */
pomo_err_t pomodoro_pause(void);

/**
 * @brief Resume a paused session
 */
pomo_err_t pomodoro_resume(void);

/**
 * @brief Skip to next session (requires user confirmation in UI layer)
 */
pomo_err_t pomodoro_skip(void);

/**
 * @brief Stop current session and return to idle
 */
pomo_err_t pomodoro_stop(void);

/**
 * @brief Adjust remaining time by delta (can be positive or negative)
 * @param delta_seconds Seconds to add (positive) or subtract (negative)
 */
pomo_err_t pomodoro_adjust_time(int32_t delta_seconds);

/**
 * @brief Enable/disable auto-progression
 */
pomo_err_t pomodoro_set_auto_progress(bool enabled);

/**
 * @brief Get current mode configuration (for UI display)
 */
void pomodoro_get_mode_config(pomo_mode_t mode, pomo_mode_config_t* out);

/**
 * @brief Set custom mode configuration (persisted to NVS)
 */
pomo_err_t pomodoro_set_custom_config(const pomo_mode_config_t* config);

/**
 * @brief Get comprehensive status (thread-safe, mutex-protected)
 */
void pomodoro_get_status(pomo_status_t* out);

/**
 * @brief Reset daily statistics (for manual midnight reset if needed)
 */
pomo_err_t pomodoro_reset_daily_stats(void);

/**
 * @brief Clear all statistics (factory reset)
 */
pomo_err_t pomodoro_clear_all_stats(void);

/**
 * @brief Get human-readable session type name
 */
const char* pomodoro_session_name(pomo_session_type_t type);

/**
 * @brief Get human-readable mode name
 */
const char* pomodoro_mode_name(pomo_mode_t mode);

#ifdef __cplusplus
}
#endif
