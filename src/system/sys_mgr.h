#pragma once

#include "sys_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the core system services (queues, basic hardware).
 */
void sys_mgr_init(void);

/**
 * @brief Get the handle to the UI message queue.
 */
QueueHandle_t sys_mgr_get_ui_queue(void);

/**
 * @brief Send a formatted OTA status message to the UI task via queue.
 * @param state    New OTA state.
 * @param progress Completion percentage (0–100).
 * @param kbps     Transfer rate in KB/s (pass 0 if unknown).
 * @param msg      Human-readable status string (max 63 chars).
 */
void sys_mgr_send_ui_msg(ota_state_t state, float progress, float kbps, const char* msg);

#ifdef __cplusplus
}
#endif
