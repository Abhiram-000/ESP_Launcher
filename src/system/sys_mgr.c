#include "sys_mgr.h"
#include "esp_log.h"
#include "../utils/error_handling.h"
#include <string.h>

static const char *TAG = "SYS_MGR";
static QueueHandle_t s_ui_queue = NULL;

void sys_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing System Manager");
    
    s_ui_queue = xQueueCreate(APP_UI_QUEUE_SIZE, sizeof(ui_message_t));
    if (s_ui_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI queue - system cannot continue");
        abort();
    }
    
    ESP_LOGI(TAG, "System Manager initialized successfully");
}

QueueHandle_t sys_mgr_get_ui_queue(void) {
    return s_ui_queue;
}

void sys_mgr_send_ui_msg(ota_state_t state, float progress, float kbps, const char* msg) {
    if (s_ui_queue == NULL) {
        ESP_LOGW(TAG, "UI queue not initialized, dropping message");
        return;
    }

    ui_message_t ui_msg;
    memset(&ui_msg, 0, sizeof(ui_msg));
    ui_msg.state    = state;
    ui_msg.progress = progress;
    ui_msg.kbps     = kbps;

    if (msg != NULL) {
        strlcpy(ui_msg.msg, msg, sizeof(ui_msg.msg));
    }

    if (xQueueSend(s_ui_queue, &ui_msg, 0) != pdTRUE) {
        ui_message_t dropped_msg;
        (void)xQueueReceive(s_ui_queue, &dropped_msg, 0);
        if (xQueueSend(s_ui_queue, &ui_msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "UI queue full, latest message dropped");
        }
    }
}
