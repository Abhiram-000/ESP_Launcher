/**
 * @file idle_mgr.h
 * @brief Automatic Deep Sleep and Inactivity Manager for ESP32 Launcher.
 * 
 * Responsibilities:
 * - Track user inactivity (50s threshold).
 * - Manage 10s pre-sleep countdown.
 * - Safely enter deep sleep with SELECT (GPIO 16) wake-up.
 */

#ifndef IDLE_MGR_H
#define IDLE_MGR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IDLE_STATE_ACTIVE,      /* Normal operation */
    IDLE_STATE_COUNTDOWN,   /* 10s countdown before sleep */
    IDLE_STATE_SLEEP        /* Entering deep sleep */
} idle_state_t;

/**
 * @brief Initialize the idle manager.
 */
void idle_mgr_init(void);

/**
 * @brief Reset the inactivity timer. 
 * Call this whenever any button is pressed.
 */
void idle_mgr_feed(void);

/**
 * @brief Periodically check timeouts and manage state.
 */
void idle_mgr_process(void);

/**
 * @brief Get current system idle state.
 */
idle_state_t idle_mgr_get_state(void);

/**
 * @brief Get remaining seconds during countdown (10 to 1).
 */
int idle_mgr_get_countdown_remaining(void);

#ifdef __cplusplus
}
#endif

#endif // IDLE_MGR_H
