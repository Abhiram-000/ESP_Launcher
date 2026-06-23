---
name: freertos-patterns
description: FreeRTOS patterns for ESP32-S3 — ISR discipline, task architecture, mutex usage, inter-task communication. Load when writing ISR handlers, creating tasks, or designing concurrency.
---

# FreeRTOS Patterns

Enforce AGENTS.md Part II Section 6. ISR safety is non-negotiable.

## ISR Pattern — Mandatory

```c
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &arg, &woken);
    if (woken) portYIELD_FROM_ISR();
}
```

**ISR Rules:**
- `IRAM_ATTR` on ALL ISR functions and functions called from ISR
- `DRAM_ATTR` on ALL data accessed from ISRs
- `volatile` on ALL flags shared between tasks and ISRs
- Only ISR-safe primitives: `xQueueSendFromISR`, `xTaskNotifyFromISR`, `xSemaphoreGiveFromISR`
- ISRs complete in <50µs

## Mutex Pattern — Always Timeout

```c
if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Mutex timeout");
    return ERR_BUSY;
}
// ... critical section ...
xSemaphoreGive(s_mutex);
```

- `portMAX_DELAY` on application mutexes → REJECTED
- Protect `s_status` on every read and write

## Task Architecture

| Task | Core | Stack | Notes |
|------|------|-------|-------|
| Display/UI | 1 | 8KB | Never blocks |
| Input ISR | 1 | — | ISR → queue |
| Services | 0 | 8-16KB | Network/storage I/O |

## Task Rules

- Core affinity explicit — never rely on scheduler default
- Worker tasks self-delete: `vTaskDelete(NULL)` — external delete FORBIDDEN
- `vTaskDelay()` in render/driver paths → FORBIDDEN. Use `vTaskDelayUntil()`

## Service Worker Pattern

```c
static void worker_task(void* arg) {
    while (!s_abort_flag) {
        // Work...
        // Check abort_flag every iteration
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);  // MUST self-delete
}
```

## Event Group Pattern

```c
EventBits_t bits = xEventGroupWaitBits(s_events,
    EVT_DONE | EVT_FAILED,
    pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));  // Never portMAX_DELAY
```
