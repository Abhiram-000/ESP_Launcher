#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_SELECT
} input_event_t;

/**
 * @brief Initialize the 5 GPIOs and the input polling task.
 */
void input_mgr_init(void);

/**
 * @brief Pops an event from the input queue. Non-blocking.
 * @return The button event, or BTN_NONE if no events are pending.
 */
input_event_t input_mgr_get_event(void);

#ifdef __cplusplus
}
#endif
