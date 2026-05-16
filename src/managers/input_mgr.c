#include "input_mgr.h"
#include "esp_log.h"
#include "../services/idle_mgr.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "../platform/board.h"

static const char *TAG = "INPUT_MGR";

static QueueHandle_t s_input_queue = NULL;

typedef struct {
    gpio_num_t pin;
    input_event_t event;
    uint32_t last_isr_time;
} btn_state_t;

static btn_state_t s_buttons[5] = {
    {(gpio_num_t)LAUNCHER_PIN_UP,     BTN_UP,     0},
    {(gpio_num_t)LAUNCHER_PIN_DOWN,   BTN_DOWN,   0},
    {(gpio_num_t)LAUNCHER_PIN_LEFT,   BTN_LEFT,   0},
    {(gpio_num_t)LAUNCHER_PIN_RIGHT,  BTN_RIGHT,  0},
    {(gpio_num_t)LAUNCHER_PIN_SELECT, BTN_SELECT, 0}
};

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    btn_state_t* btn = (btn_state_t*) arg;
    uint32_t now = esp_timer_get_time() / 1000; // ms
    
    // Simple 30ms Debounce
    if (now - btn->last_isr_time > 30) {
        btn->last_isr_time = now;
        
        // falling edge -> press
        if (gpio_get_level(btn->pin) == 0) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            input_event_t ev = btn->event;
            xQueueSendFromISR(s_input_queue, &ev, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

void input_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing Input Manager (5 Buttons, ISR-based)");

    s_input_queue = xQueueCreate(10, sizeof(input_event_t));

    uint64_t pin_mask = (1ULL << LAUNCHER_PIN_UP) | (1ULL << LAUNCHER_PIN_DOWN) | 
                        (1ULL << LAUNCHER_PIN_LEFT) | (1ULL << LAUNCHER_PIN_RIGHT) | 
                        (1ULL << LAUNCHER_PIN_SELECT);

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE // trigger on press
    };
    gpio_config(&io_conf);

    // Install ISR service if not already installed
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
    }

    for (int i = 0; i < 5; i++) {
        gpio_isr_handler_add(s_buttons[i].pin, gpio_isr_handler, (void*)&s_buttons[i]);
    }
}

input_event_t input_mgr_get_event(void) {
    if (!s_input_queue) return BTN_NONE;
    
    input_event_t ev;
    if (xQueueReceive(s_input_queue, &ev, 0) == pdTRUE) {
        idle_mgr_feed(); // Reset sleep timer on any valid processed event
        return ev;
    }
    return BTN_NONE;
}
