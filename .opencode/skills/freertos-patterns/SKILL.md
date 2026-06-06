---
name: freertos-patterns
description: FreeRTOS patterns for ESP32-S3 — ISR discipline, task architecture, mutex usage, inter-task communication
---

## What I do

I enforce FreeRTOS patterns from AGENTS.md Part II Section 6. Load me when writing ISR handlers, creating tasks, using mutexes, or designing inter-task communication.

## When to use me

- Writing GPIO/interrupt handlers
- Creating FreeRTOS tasks
- Using mutexes, queues, event groups
- Designing producer-consumer patterns
- Debugging race conditions or deadlocks

## ISR Pattern

Every ISR MUST follow this template:

```c
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &arg, &woken);
    if (woken) portYIELD_FROM_ISR();
}
```

### ISR Rules
- `IRAM_ATTR` on ALL ISR functions
- `DRAM_ATTR` on ALL data accessed from ISRs
- `volatile` on ALL flags shared between tasks and ISRs
- Only ISR-safe primitives: `xQueueSendFromISR`, `xTaskNotifyFromISR`, `xSemaphoreGiveFromISR`
- ISRs complete in <50µs

## Mutex Pattern

```c
// Always with timeout
if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Mutex timeout");
    return ERR_BUSY;
}
// ... critical section ...
xSemaphoreGive(s_mutex);
```

### Mutex Rules
- Always timeout — never `portMAX_DELAY`
- Protect `s_status` on every read and write
- Copy shared state under mutex at start of loop(); render() reads cached values

## Task Architecture

| Task | Core | Priority | Notes |
|------|------|----------|-------|
| Display/UI renderer | Core 1 | High | Never blocks |
| Input handler | Core 1 | High | ISR-based |
| Network/Storage services | Core 0 | Medium | Worker tasks |
| Background/logging | Core 0 | Low | Non-critical |

### Task Rules
- Core affinity explicit — never rely on scheduler default
- Worker tasks always self-delete: `vTaskDelete(NULL)`
- External `vTaskDelete(handle)` → FORBIDDEN
- `vTaskDelay()` in render/driver paths → FORBIDDEN
- Use `vTaskDelayUntil()` for periodic tasks

## Service Worker Pattern

```c
static void worker_task(void* arg) {
    while (!s_abort_flag) {
        // Do work...
        // Check abort_flag every iteration
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);  // MUST self-delete
}
```

### Service Invariants
1. `s_mutex` protects `s_status`
2. Worker pinned to Core 0
3. Abort: `volatile bool s_abort_flag`, checked every iteration
4. Worker self-deletes
5. Public API non-blocking
6. `status_msg` human-readable
7. Event group: `EVT_DONE` / `EVT_FAILED` bits

## Event Group Pattern

```c
EventBits_t bits = xEventGroupWaitBits(s_events,
    EVT_DONE | EVT_FAILED,
    pdTRUE,        // clear on exit
    pdFALSE,       // wait for any
    pdMS_TO_TICKS(5000));  // timeout, never portMAX_DELAY
```

## Forbidden

- `vTaskDelay()` in render or driver paths
- `portMAX_DELAY` on application mutexes
- `vTaskDelete(external_handle)`
- Allocation in ISR context
- Blocking SPI/I2C in UI thread
- Polling loops instead of event/queue/notification
