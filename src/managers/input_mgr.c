#include "input_mgr.h"
#include "esp_log.h"
#include "../services/idle_mgr.h"
#include "../hal/hal_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "../platform/board.h"

static const char *TAG = "INPUT_MGR";

#define INPUT_QUEUE_SIZE 10
#define DEBOUNCE_MS     30
#define MIN_PRESS_MS    200   /* Minimum interval between events for same button */

static QueueHandle_t s_input_queue = NULL;

typedef struct {
    gpio_num_t pin;
    input_event_t event;
    uint32_t last_isr_time;
    uint32_t last_event_time;  /* Timestamp of last event sent for this button */
} btn_state_t;

static btn_state_t s_buttons[5] = {
    {(gpio_num_t)LAUNCHER_PIN_UP,     BTN_UP,     0, 0},
    {(gpio_num_t)LAUNCHER_PIN_DOWN,   BTN_DOWN,   0, 0},
    {(gpio_num_t)LAUNCHER_PIN_LEFT,   BTN_LEFT,   0, 0},
    {(gpio_num_t)LAUNCHER_PIN_RIGHT,  BTN_RIGHT,  0, 0},
    {(gpio_num_t)LAUNCHER_PIN_SELECT, BTN_SELECT, 0, 0}
};

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    btn_state_t* btn = (btn_state_t*) arg;
    const uint32_t now = esp_timer_get_time() / 1000;

    if (now - btn->last_isr_time > DEBOUNCE_MS) {
        btn->last_isr_time = now;

        if (hal_gpio_get_level(btn->pin) == 0) {
            /* Require minimum interval between events for the same button.
             * Prevents noise-induced repeat triggers from a single physical press. */
            if (now - btn->last_event_time < MIN_PRESS_MS) {
                return;
            }
            btn->last_event_time = now;

            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            const input_event_t ev = btn->event;
            xQueueSendFromISR(s_input_queue, &ev, &xHigherPriorityTaskWoken);

            static const char* btn_names[] = {"UP","DOWN","LEFT","RIGHT","SELECT"};
            int idx = (int)(btn->event - BTN_UP);
            if (idx >= 0 && idx <= 4) {
                ESP_DRAM_LOGI("IN_ISR", ">> ISR fire: %s pin=%d level=%d q_spaces=%d",
                    btn_names[idx], btn->pin, hal_gpio_get_level(btn->pin),
                    (int)uxQueueSpacesAvailable(s_input_queue));
            }

            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

void input_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing Input Manager (HAL-based, ISR-driven)");

    s_input_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(input_event_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "Failed to create input queue");
        return;
    }

    esp_err_t err = hal_gpio_install_isr_service();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HAL GPIO ISR service failed: %s", esp_err_to_name(err));
        return;
    }

    for (int i = 0; i < 5; i++) {
        err = hal_gpio_add_isr_handler(s_buttons[i].pin, gpio_isr_handler, (void*)&s_buttons[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR for pin %d: %s", s_buttons[i].pin, esp_err_to_name(err));
        }
    }
}

input_event_t input_mgr_get_event(void) {
    if (!s_input_queue) return BTN_NONE;
    
    input_event_t ev;
    if (xQueueReceive(s_input_queue, &ev, 0) == pdTRUE) {
        idle_mgr_feed();
        static const char* ev_names[] = {"NONE","UP","DOWN","LEFT","RIGHT","SELECT"};
        int idx = (int)ev;
        if (idx >= 0 && idx <= 5) {
            ESP_LOGD(TAG, "Dequeued: %s q_remaining=%d",
                ev_names[idx], (int)uxQueueMessagesWaiting(s_input_queue));
        }
        return ev;
    }
    return BTN_NONE;
}

void input_mgr_inject_event(input_event_t ev) {
    if (s_input_queue && ev != BTN_NONE) {
        xQueueSend(s_input_queue, &ev, 0);
    }
}

bool input_mgr_is_button_held(input_event_t btn) {
    gpio_num_t pin;
    switch (btn) {
        case BTN_UP:     pin = (gpio_num_t)LAUNCHER_PIN_UP; break;
        case BTN_DOWN:   pin = (gpio_num_t)LAUNCHER_PIN_DOWN; break;
        case BTN_LEFT:   pin = (gpio_num_t)LAUNCHER_PIN_LEFT; break;
        case BTN_RIGHT:  pin = (gpio_num_t)LAUNCHER_PIN_RIGHT; break;
        case BTN_SELECT: pin = (gpio_num_t)LAUNCHER_PIN_SELECT; break;
        default: return false;
    }
    return hal_gpio_get_level(pin) == 0;
}
