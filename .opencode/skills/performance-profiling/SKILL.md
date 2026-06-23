---
name: performance-profiling
description: ESP32-S3 performance profiling — memory snapshots, stack analysis, frame timing, ISR latency, bottleneck identification. Load when debugging performance issues or optimizing.
---

# Performance Profiling

## Memory Snapshot

```c
// Add temporarily — REMOVE before commit
ESP_LOGI(TAG, "Heap: %lu KB | PSRAM: %lu KB | Stack: %u B",
    esp_get_free_heap_size() / 1024,
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
    (unsigned)uxTaskGetStackHighWaterMark(NULL));
```

## Stack High-Water Mark

```c
// Add at end of task function
ESP_LOGI(TAG, "Stack high water: %u bytes",
    (unsigned)uxTaskGetStackHighWaterMark(NULL));
```

## Frame Timing

```c
// In render() or loop()
int64_t start = esp_timer_get_time();
// ... render code ...
int64_t elapsed = esp_timer_get_time() - start;
if (elapsed > 25000) {  // >25ms = below 40fps
    ESP_LOGW(TAG, "Frame over budget: %lld us", elapsed);
}
```

## ISR Timing

```c
// In ISR
int64_t start = esp_timer_get_time();
// ... ISR code ...
int64_t elapsed = esp_timer_get_time() - start;
if (elapsed > 50) {  // >50µs
    ESP_LOGE(TAG, "ISR too slow: %lld us", elapsed);
}
```

## Health Thresholds

| Metric | Floor | Warning | Critical |
|--------|-------|---------|----------|
| Free heap | 20KB | <40KB | <20KB |
| Free PSRAM | 500KB | <1MB | <500KB |
| Stack headroom | 512B | <1KB | <512B |
| Frame time | 25ms | >20ms | >25ms |
| ISR time | 50µs | >30µs | >50µs |

## Common Bottlenecks

1. **Render path blocking** — never call I2C, SPI, HTTP in render()
2. **Mutex contention** — copy state under mutex at start of loop(), render reads cache
3. **PSRAM fragmentation** — pre-allocate at init, never malloc in steady-state
4. **Stack overflow** — profile with uxTaskGetStackHighWaterMark(), increase if <512B
5. **ISR too long** — defer work to task via queue, ISR only sends notification
