#include "sys_mgr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SYS_MGR";
static QueueHandle_t s_ui_queue = NULL;

void sys_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing System Manager (ESP-IDF Native)");
    
    s_ui_queue = xQueueCreate(APP_UI_QUEUE_SIZE, sizeof(ui_message_t));
    if (s_ui_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI queue!");
    }
}

QueueHandle_t sys_mgr_get_ui_queue(void) {
    return s_ui_queue;
}

void sys_mgr_send_ui_msg(ota_state_t state, float progress, float kbps, const char* msg) {
    if (s_ui_queue == NULL) return;

    ui_message_t ui_msg;
    memset(&ui_msg, 0, sizeof(ui_msg)); // Crucial for new fields
    ui_msg.state    = state;
    ui_msg.progress = progress;
    ui_msg.kbps     = kbps;

    if (msg != NULL) {
        strncpy(ui_msg.msg, msg, sizeof(ui_msg.msg) - 1);
        ui_msg.msg[sizeof(ui_msg.msg) - 1] = '\0';
    }

    xQueueSend(s_ui_queue, &ui_msg, pdMS_TO_TICKS(10));
}

