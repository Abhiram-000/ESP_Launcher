/**
 * @file idle_mgr.c
 * @brief Automatic Deep Sleep Logic and System Shutdown.
 */

#include "idle_mgr.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/rtc_io.h"
#include "../config/sys_config.h"

static const char *TAG = "IDLE_MGR";

#define IDLE_TIMEOUT_MS      50000  /* 50 seconds */
#define COUNTDOWN_TIMEOUT_MS 10000  /* 10 seconds */
#define WAKE_GPIO            16      /* SELECT button */

static uint64_t s_last_activity_ms = 0;
static idle_state_t s_state = IDLE_STATE_ACTIVE;

static uint64_t get_now_ms(void) {
    return esp_timer_get_time() / 1000;
}

void idle_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing Idle Manager (50s Idle / 10s Countdown)");
    s_last_activity_ms = get_now_ms();
    s_state = IDLE_STATE_ACTIVE;
}

void idle_mgr_feed(void) {
    s_last_activity_ms = get_now_ms();
    if (s_state != IDLE_STATE_ACTIVE) {
        ESP_LOGI(TAG, "System Woken (Inactivity reset)");
        s_state = IDLE_STATE_ACTIVE;
    }
}

static void enter_deep_sleep(void) {
    ESP_LOGI(TAG, "Stopping services for safe shutdown...");
    
    // 1. Stop Wi-Fi gracefully
    esp_wifi_stop();
    esp_wifi_deinit();
    
    // 2. Commit NVS
    nvs_flash_deinit();

    ESP_LOGI(TAG, "Entering Deep Sleep... Wake source: SELECT button (GPIO %d)", WAKE_GPIO);
    
    // 3. Configure Wake Source: SELECT button (LOW trigger)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_GPIO, 0); // 0 = Wake on LOW
    
    // 4. Ensure internal pull-up remains active during sleep to prevent false wakeups
    rtc_gpio_pullup_en((gpio_num_t)WAKE_GPIO);
    rtc_gpio_pulldown_dis((gpio_num_t)WAKE_GPIO);

    // 5. Go to sleep
    esp_deep_sleep_start();
}

void idle_mgr_process(void) {
    uint64_t now = get_now_ms();
    uint64_t elapsed = now - s_last_activity_ms;

    switch (s_state) {
        case IDLE_STATE_ACTIVE:
            if (elapsed > IDLE_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Inactivity detected (>%d s). Starting countdown...", IDLE_TIMEOUT_MS/1000);
                s_state = IDLE_STATE_COUNTDOWN;
            }
            break;

        case IDLE_STATE_COUNTDOWN:
            if (elapsed > (IDLE_TIMEOUT_MS + COUNTDOWN_TIMEOUT_MS)) {
                ESP_LOGI(TAG, "Countdown expired. Shutting down.");
                s_state = IDLE_STATE_SLEEP;
                enter_deep_sleep();
            }
            break;

        case IDLE_STATE_SLEEP:
            /* Already transitioning or asleep */
            break;
    }
}

idle_state_t idle_mgr_get_state(void) {
    return s_state;
}

int idle_mgr_get_countdown_remaining(void) {
    if (s_state != IDLE_STATE_COUNTDOWN) return 0;
    
    uint64_t now = get_now_ms();
    uint64_t elapsed = now - s_last_activity_ms;
    int remaining = (int)((IDLE_TIMEOUT_MS + COUNTDOWN_TIMEOUT_MS - elapsed) / 1000);
    
    return (remaining < 0) ? 0 : remaining;
}
