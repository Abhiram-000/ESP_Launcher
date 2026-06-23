/*
 * perf_monitor.h — Lightweight Performance Monitoring
 * ====================================================
 * Purpose: Non-intrusive frame timing and memory health monitoring.
 *          Zero overhead when PERF_MONITOR_ENABLED=0 (default in release).
 *
 * Usage:
 *   PERF_FRAME_START();
 *   // ... render frame ...
 *   PERF_FRAME_END();        // logs if frame > budget
 *   PERF_MEMORY_CHECK();     // logs heap/PSRAM/stack health
 */
#pragma once

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set to 0 for release builds (zero overhead) */
#ifndef PERF_MONITOR_ENABLED
#define PERF_MONITOR_ENABLED 0
#endif

/* Frame time budget in microseconds */
#ifndef PERF_FRAME_BUDGET_US
#define PERF_FRAME_BUDGET_US 25000  /* 25ms = 40fps */
#endif

/* Memory health thresholds */
#define PERF_HEAP_FLOOR_KB      20
#define PERF_PSRAM_FLOOR_KB     500
#define PERF_STACK_WARN_BYTES   512

#if PERF_MONITOR_ENABLED

typedef struct {
    int64_t  frame_start_us;
    int64_t  frame_worst_us;
    uint32_t frame_count;
    uint32_t frame_overruns;
} perf_state_t;

static perf_state_t s_perf = {0};

#define PERF_FRAME_START() do { \
    s_perf.frame_start_us = esp_timer_get_time(); \
} while(0)

#define PERF_FRAME_END() do { \
    int64_t elapsed = esp_timer_get_time() - s_perf.frame_start_us; \
    s_perf.frame_count++; \
    if (elapsed > s_perf.frame_worst_us) s_perf.frame_worst_us = elapsed; \
    if (elapsed > PERF_FRAME_BUDGET_US) { \
        s_perf.frame_overruns++; \
        ESP_LOGW("PERF", "Frame overrun: %lld us (budget %d), worst: %lld, overruns: %lu", \
                 elapsed, PERF_FRAME_BUDGET_US, s_perf.frame_worst_us, \
                 (unsigned long)s_perf.frame_overruns); \
    } \
} while(0)

#define PERF_MEMORY_CHECK() do { \
    size_t heap_kb = esp_get_free_heap_size() / 1024; \
    size_t psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024; \
    size_t stack_b = uxTaskGetStackHighWaterMark(NULL); \
    if (heap_kb < PERF_HEAP_FLOOR_KB) \
        ESP_LOGW("MEM", "Low heap: %zu KB (floor %d)", heap_kb, PERF_HEAP_FLOOR_KB); \
    if (psram_kb < PERF_PSRAM_FLOOR_KB) \
        ESP_LOGW("MEM", "Low PSRAM: %zu KB (floor %d)", psram_kb, PERF_PSRAM_FLOOR_KB); \
    if (stack_b < PERF_STACK_WARN_BYTES) \
        ESP_LOGW("MEM", "Low stack: %zu B (warn %d)", stack_b, PERF_STACK_WARN_BYTES); \
} while(0)

#define PERF_REPORT() do { \
    ESP_LOGI("PERF", "Frames: %lu, Overruns: %lu, Worst: %lld us", \
             (unsigned long)s_perf.frame_count, \
             (unsigned long)s_perf.frame_overruns, \
             s_perf.frame_worst_us); \
} while(0)

#else /* PERF_MONITOR_ENABLED == 0 — zero overhead */

#define PERF_FRAME_START()      ((void)0)
#define PERF_FRAME_END()        ((void)0)
#define PERF_MEMORY_CHECK()     ((void)0)
#define PERF_REPORT()           ((void)0)

#endif /* PERF_MONITOR_ENABLED */

#ifdef __cplusplus
}
#endif
