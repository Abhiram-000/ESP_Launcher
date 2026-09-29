/**
 * @file idle_mgr.c
 * @brief Automatic Deep Sleep Logic and System Shutdown.
 */

#include "idle_mgr.h"
#include "ntp_service.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/rtc_io.h"
#include "../config/sys_config.h"
#include "../platform/board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "IDLE_MGR";

/* Timing constants live in the central config header (AGENTS.md §9) so they can
 * be tuned without touching implementation files. */
#define IDLE_TIMEOUT_MS      IDLE_MGR_TIMEOUT_MS
#define COUNTDOWN_TIMEOUT_MS IDLE_MGR_COUNTDOWN_MS
#define WAKE_GPIO            LAUNCHER_PIN_SELECT

static uint64_t s_last_activity_ms = 0;
static idle_state_t s_state = IDLE_STATE_ACTIVE;
static SemaphoreHandle_t s_mutex = NULL;

/* WHY a count and not a bool: a plain flag let any single caller re-enable
 * sleep for every other holder. App_ClockWeather and App_Pomodoro both take a
 * hold, so whichever exited first would clear the flag and the other app could
 * still be mid-interaction when the device slept. A count makes release
 * symmetric with acquire, so the last holder out is the only one that
 * restores normal behaviour. */
static uint8_t s_hold_count = 0;

static uint64_t get_now_ms(void) {
    return esp_timer_get_time() / 1000;
}

void idle_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing Idle Manager (50s Idle / 10s Countdown)");
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);
    s_last_activity_ms = get_now_ms();
    s_state = IDLE_STATE_ACTIVE;
}

void idle_mgr_feed(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }
    s_last_activity_ms = get_now_ms();
    if (s_state != IDLE_STATE_ACTIVE) {
        ESP_LOGI(TAG, "System Woken (Inactivity reset)");
        s_state = IDLE_STATE_ACTIVE;
    }
    xSemaphoreGive(s_mutex);
}

static void enter_deep_sleep(void) {
    ESP_LOGI(TAG, "Stopping services for safe shutdown...");
    
    // 1. Stop Wi-Fi gracefully
    esp_err_t werr = esp_wifi_stop();
    if (werr != ESP_OK) ESP_LOGW(TAG, "WiFi stop: %s", esp_err_to_name(werr));
    esp_wifi_deinit();
    
    /* 2. Persist the clock before tearing down NVS.
     * WHY: the SNTP callback only saves on an actual sync, which happens
     * hourly. Without this the next cold boot restores a timestamp up to an
     * hour stale. nvs_flash_deinit() below closes the partition, so the save
     * must happen first. */
    ntp_service_save_to_nvs();

    // 3. Commit NVS
    nvs_flash_deinit();
    ESP_LOGI(TAG, "Entering Deep Sleep... Wake source: SELECT button (GPIO %d)", WAKE_GPIO);
    
    // 4. Configure Wake Source: SELECT button (LOW trigger)
    esp_err_t serr = esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_GPIO, 0);
    if (serr != ESP_OK) ESP_LOGE(TAG, "Sleep config failed: %s", esp_err_to_name(serr));
    
    // 5. Ensure internal pull-up remains active during sleep
    rtc_gpio_pullup_en((gpio_num_t)WAKE_GPIO);
    rtc_gpio_pulldown_dis((gpio_num_t)WAKE_GPIO);

    // 6. Go to sleep
    esp_deep_sleep_start();
}

void idle_mgr_process(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }
    
    // While any holder holds the idle lock, keep feeding the timer and stay active
    if (s_hold_count > 0) {
        s_last_activity_ms = get_now_ms();
        s_state = IDLE_STATE_ACTIVE;
        xSemaphoreGive(s_mutex);
        return;
    }
    
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
                xSemaphoreGive(s_mutex);
                enter_deep_sleep();
            }
            break;

        case IDLE_STATE_SLEEP:
            /* Already transitioning or asleep */
            break;
    }
    xSemaphoreGive(s_mutex);
}

idle_state_t idle_mgr_get_state(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return IDLE_STATE_ACTIVE;
    }
    idle_state_t state = s_state;
    xSemaphoreGive(s_mutex);
    return state;
}

int idle_mgr_get_countdown_remaining(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return 0;
    }
    if (s_state != IDLE_STATE_COUNTDOWN) {
        xSemaphoreGive(s_mutex);
        return 0;
    }
    
    uint64_t now = get_now_ms();
    uint64_t elapsed = now - s_last_activity_ms;
    int remaining = (int)((IDLE_TIMEOUT_MS + COUNTDOWN_TIMEOUT_MS - elapsed) / 1000);
    
    xSemaphoreGive(s_mutex);
    return (remaining < 0) ? 0 : remaining;
}

void idle_mgr_disable(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }
    /* Saturating increment — a runaway holder must not wrap to 0 and
     * accidentally re-enable sleep while callers believe it is still held. */
    if (s_hold_count < 0xFF) s_hold_count++;
    s_last_activity_ms = get_now_ms();
    s_state = IDLE_STATE_ACTIVE;
    ESP_LOGI(TAG, "Idle hold acquired (count=%u)", s_hold_count);
    xSemaphoreGive(s_mutex);
}

void idle_mgr_enable(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }
    if (s_hold_count > 0) {
        s_hold_count--;
        s_last_activity_ms = get_now_ms();
        s_state = IDLE_STATE_ACTIVE;
    } else {
        /* Unbalanced release. Log rather than clamp silently — it means a
         * caller enabled without a matching disable. */
        ESP_LOGW(TAG, "idle_mgr_enable() with no hold held — ignoring");
    }
    ESP_LOGI(TAG, "Idle hold released (count=%u)", s_hold_count);
    xSemaphoreGive(s_mutex);
}
