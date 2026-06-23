---
name: memory-safety
description: ESP32-S3 memory management — PSRAM allocation, ISR safety, stack budgets, heap monitoring. Load when allocating memory, managing buffers, or debugging memory issues.
---

# Memory Safety

Enforce AGENTS.md Part II Section 5 and Part VI. PSRAM is mandatory for large allocations.

## PSRAM Allocation — ALL Buffers >1KB

```c
uint8_t* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed: %zu bytes", size);
    return ERR_OOM;
}
```

- `malloc()` for >1KB → REJECTED
- No allocation in ISRs
- No stack allocations >512 bytes
- `alloca()` in variable-input paths → FORBIDDEN

## LovyanGFX Sprites

```cpp
sprite.setPsram(true);  // ALWAYS before createSprite
sprite.createSprite(w, h);
```

## Stack Budgets

| Task | Stack Size |
|------|-----------|
| NES emulator | 48KB minimum |
| UI task | 8KB |
| HTTP handlers | 8KB |
| Lua/LLM workers | 16KB |

Profile with `uxTaskGetStackHighWaterMark()` — never guess.

## Memory Health

```c
ESP_LOGI(TAG, "Heap: %lu KB | PSRAM: %lu KB | Stack: %u B",
    esp_get_free_heap_size() / 1024,
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
    (unsigned)uxTaskGetStackHighWaterMark(NULL));
```

| Metric | Floor |
|--------|-------|
| Free heap | 20KB |
| Free PSRAM (NES) | 500KB |
| Stack high-water mark | 512B free |

## Allocation Patterns

Pre-allocate at init, free at deinit. No dynamic allocation in steady-state.
