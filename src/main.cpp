/**
 * @file main.cpp
 * @brief Entry point for the ESP32-S3 Custom Modular Launcher.
 * 
 * Responsibilities:
 * - Initialize core system managers (NVS, Boot Control).
 * - Orchestrate subsystem startup (Input, UI, Network).
 * - Manage safety markers for OTA rollback protection.
 * 
 * Dependencies:
 * - FreeRTOS (Task management)
 * - ESP-IDF (HAL and System services)
 * - Internal managers: sys_mgr, boot_ctrl, input_mgr, wifi_ap, web_server, ui_task
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/sys_mgr.h"
#include "system/boot_ctrl.h"
#include "managers/input_mgr.h"
#include "services/wifi_mgr.h"
#include "services/web_server.h"
#include "services/idle_mgr.h"
#include "ui/ui_task.h"

static const char* TAG = "MAIN";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "--- ESP32-S3 Custom Modular Launcher Startup ---");

    /*
     * STEP 0: Boot decision controller.
     */
    // 0. Initialize NVS (required for Wi-Fi and system state)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Initialize Event Loop (required for Wi-Fi events)
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sys_mgr_init();
    boot_ctrl_check();

    // 2. Start GPIO Hardware Pollers
    input_mgr_init();

    // 3. Start Heavy UI Renderer
    ui_task_init();

    // 4. Initialize Unified Wi-Fi (AP + STA)
    wifi_mgr_init();

    // 5. Start HTTP Server for OTA uploads
    web_server_init();

    // 6. Start Idle Manager
    idle_mgr_init();

    /*
     * STEP FINAL: Mark the Launcher as valid and reset boot-loop counter.
     * Required for CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE safety.
     */
    boot_ctrl_mark_valid();

    ESP_LOGI(TAG, "System Ready. Entering event loop.");

    // The main task can now sleep — everything else is event-driven
    while (1) {
        idle_mgr_process();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}