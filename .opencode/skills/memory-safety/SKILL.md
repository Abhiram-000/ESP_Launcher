---
name: memory-safety
description: ESP32-S3 memory management rules — PSRAM allocation, ISR safety, stack budgets, heap monitoring
---

## What I do

I enforce memory safety rules from AGENTS.md Part II Section 5 and Part VI. Load me when allocating memory, managing buffers, or debugging memory issues.

## When to use me

- Allocating buffers (heap or PSRAM)
- Debugging memory leaks or crashes
- Setting up task stacks
- Working with LovyanGFX sprites
- Monitoring memory health

## PSRAM Allocation Rule

ALL allocations >1KB MUST use PSRAM. No exceptions.

```c
// CORRECT
uint8_t* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed: %zu bytes", size);
    return ERR_OOM;
}

// WRONG — will be rejected
uint8_t* buf = malloc(size);  // REJECTED for >1KB
```

## LovyanGFX Sprites

```cpp
sprite.setPsram(true);
sprite.createSprite(w, h);
```

Always set PSRAM before creating the sprite.

## ISR Memory Rules

- NO allocation in ISR context — ever
- NO stack allocations >512 bytes
- `alloca()` in variable-input paths → FORBIDDEN
- Static allocation preferred over heap

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

### Thresholds

| Metric | Floor |
|--------|-------|
| Free heap | 20KB |
| Free PSRAM (NES) | 500KB |
| Stack high-water mark | 512B free |

## Allocation Patterns

### Pre-allocate at init
```c
static uint8_t* s_buffer = NULL;

esp_err_t module_init(void) {
    s_buffer = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buffer) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
```

### Free at deinit
```c
void module_deinit(void) {
    free(s_buffer);
    s_buffer = NULL;
}
```

### No dynamic allocation in steady-state
If you find yourself calling malloc in a loop or periodic function, pre-allocate at init instead.

## Forbidden

- `malloc()` >1KB without PSRAM flags
- `alloca()` in variable-input paths
- Stack allocations >512 bytes
- Unchecked allocation return values
- Dynamic allocation in steady-state code
