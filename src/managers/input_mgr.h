#pragma once

#include <stdbool.h>

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

/**
 * @brief Inject an event into the input queue from a task context.
 * @param ev The button event to inject.
 */
void input_mgr_inject_event(input_event_t ev);

/**
 * @brief Check if a button's physical GPIO pin is currently held (active low).
 * @param btn The button to check.
 * @return true if the pin is low (button pressed), false otherwise.
 */
bool input_mgr_is_button_held(input_event_t btn);

#ifdef __cplusplus
}
#endif
