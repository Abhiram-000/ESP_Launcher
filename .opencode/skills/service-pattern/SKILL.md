---
name: service-pattern
description: ESP32-S3 service implementation pattern — seven invariants, worker task lifecycle, mutex-protected status, abort mechanism. Load when creating or modifying services.
---

# Service Pattern

Every service follows this exact pattern. Violations are rejected.

## Public API Contract

```c
my_service_err_t my_service_init(void);
my_service_err_t my_service_start_operation(const char* param);  // non-blocking
my_service_err_t my_service_abort(void);
void             my_service_get_status(my_service_status_t* out); // mutex copy
bool             my_service_is_busy(void);
```

## Seven Invariants — All Mandatory

1. `s_mutex` protects `s_status` on every read and write
2. Worker task pinned to **Core 0** — all network/storage I/O on Core 0
3. Abort uses `volatile bool s_abort_flag` — cooperative, checked every iteration
4. Worker task always calls `vTaskDelete(NULL)` — external delete forbidden
5. Public API is non-blocking — UI polls `get_status()` every frame
6. `status_msg` is human-readable, safe for direct UI display
7. Event group signals completion: `EVT_DONE` / `EVT_FAILED` bits

## Template

```c
#include "my_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

static const char* TAG = "MY_SERVICE";

#define EVT_DONE   BIT0
#define EVT_FAILED BIT1

static SemaphoreHandle_t       s_mutex       = NULL;
static my_service_status_t     s_status      = {};
static TaskHandle_t            s_task_handle = NULL;
static volatile bool           s_abort_flag  = false;
static EventGroupHandle_t      s_events      = NULL;

static void worker_task(void* arg) {
    const char* param = (const char*)arg;

    while (!s_abort_flag) {
        // --- Do work here ---
        // Check s_abort_flag every iteration
        // Update s_status under mutex

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_status.is_busy = true;
            strlcpy(s_status.status_msg, "Working...", sizeof(s_status.status_msg));
            xSemaphoreGive(s_mutex);
        }

        // When done:
        xEventGroupSetBits(s_events, EVT_DONE);
        break;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status.is_busy = false;
        xSemaphoreGive(s_mutex);
    }

    s_task_handle = NULL;
    vTaskDelete(NULL);  // MUST self-delete
}

esp_err_t my_service_init(void) {
    s_mutex  = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_mutex || !s_events) {
        ESP_LOGE(TAG, "Failed to create sync primitives");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

my_service_err_t my_service_start_operation(const char* param) {
    if (my_service_is_busy()) return MY_SERVICE_ERR_BUSY;

    s_abort_flag = false;
    xEventGroupClearBits(s_events, EVT_DONE | EVT_FAILED);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status.is_busy = true;
        s_status.error_code = 0;
        strlcpy(s_status.status_msg, "Starting...", sizeof(s_status.status_msg));
        xSemaphoreGive(s_mutex);
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        worker_task, "my_svc", 8192, (void*)param, 5, &s_task_handle, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed");
        s_status.is_busy = false;
        return MY_SERVICE_ERR_NO_MEM;
    }
    return MY_SERVICE_OK;
}

my_service_err_t my_service_abort(void) {
    s_abort_flag = true;
    if (s_task_handle) {
        // Wait up to 600ms for self-termination
        for (int i = 0; i < 60; i++) {
            if (!s_task_handle) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return MY_SERVICE_OK;
}

void my_service_get_status(my_service_status_t* out) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_status;
        xSemaphoreGive(s_mutex);
    }
}

bool my_service_is_busy(void) {
    return s_status.is_busy;
}
```

## Error Enum Pattern

```c
typedef enum {
    MY_SERVICE_OK            = 0,
    MY_SERVICE_ERR_BUSY      = 1,
    MY_SERVICE_ERR_NO_MEM    = 2,
    MY_SERVICE_ERR_IO        = 3,
    MY_SERVICE_ERR_TIMEOUT   = 4,
} my_service_err_t;
```

## Status Struct Pattern

```c
typedef struct {
    bool    is_busy;
    int     error_code;
    char    status_msg[64];
    float   progress;  // 0-100, optional
} my_service_status_t;
```
