# Developer Quick Reference - Optimized Codebase

## HAL Usage

### GPIO
```c
#include "hal/hal_gpio.h"

// Initialize input with pullup
hal_gpio_init_input(GPIO_NUM_5, true);

// Initialize output
hal_gpio_init_output(GPIO_NUM_10);

// Read/write
int level = hal_gpio_get_level(GPIO_NUM_5);
hal_gpio_set_level(GPIO_NUM_10, 1);

// ISR setup
hal_gpio_install_isr_service();
hal_gpio_add_isr_handler(GPIO_NUM_5, my_isr, arg);
```

### SPI
```c
#include "hal/hal_spi.h"

hal_spi_bus_config_t bus_cfg = {
    .host = SPI2_HOST,
    .mosi_pin = 11,
    .sclk_pin = 12,
    .max_transfer_sz = 4096,
    .dma_chan = SPI_DMA_CH_AUTO
};
hal_spi_bus_init(&bus_cfg);

hal_spi_device_config_t dev_cfg = {
    .cs_pin = 8,
    .clock_speed_hz = 40000000,
    .mode = 0,
    .queue_size = 7
};
hal_spi_device_t device;
hal_spi_device_init(&device, &dev_cfg, SPI2_HOST);
```

## Error Handling

```c
#include "utils/error_handling.h"

// Fatal error (aborts on failure)
ESP_ERROR_CHECK_LOG(nvs_flash_init(), TAG, "NVS init");

// Return on error
ESP_ERROR_RETURN(wifi_init(), TAG, "WiFi init", ESP_FAIL);

// Warning only (non-fatal)
ESP_ERROR_WARN(optional_feature_init(), TAG, "Optional feature");

// Safe wrapper
esp_err_t err = esp_safe_call(some_function(), TAG, "some_function");
```

## Compile-Time Safety

```c
#include "utils/compile_checks.h"

// Buffer size validation
char buffer[64];
STATIC_ASSERT_BUFFER_SIZE(buffer, 64);

// Array size validation
int array[10];
STATIC_ASSERT_ARRAY_SIZE(array, 10);

// Struct size validation
STATIC_ASSERT_STRUCT_SIZE(my_struct_t, 128);

// Runtime bounds check
BOUNDS_CHECK(index, array_size);

// Safe string operations
safe_strncpy(dst, sizeof(dst), src);
safe_memcpy(dst, sizeof(dst), src, src_size);
```

## Dirty Rectangle Tracking

```c
#include "utils/dirty_rect.h"

dirty_rect_manager_t mgr;
dirty_rect_init(&mgr);

// Mark regions as dirty
dirty_rect_mark_dirty(&mgr, x, y, w, h);

// Optimize (merge overlapping rects)
dirty_rect_optimize(&mgr);

// Check if redraw needed
if (dirty_rect_is_dirty(&mgr)) {
    if (dirty_rect_needs_full_redraw(&mgr)) {
        // Full screen redraw
    } else {
        // Partial update
        for (uint8_t i = 0; i < dirty_rect_get_count(&mgr); i++) {
            const dirty_rect_t* rect = dirty_rect_get(&mgr, i);
            // Update only this rectangle
        }
    }
}

// Clear after rendering
dirty_rect_clear(&mgr);
```

## Pool Allocator

```c
#include "utils/pool_allocator.h"

// Allocate pool memory
static uint8_t pool_memory[4096];
pool_allocator_t pool;

// Initialize (32-byte blocks)
pool_init(&pool, pool_memory, sizeof(pool_memory), 32);

// Allocate/free
void* ptr = pool_alloc(&pool);
if (ptr) {
    // Use memory
    pool_free(&pool, ptr);
}

// Monitor usage
ESP_LOGI(TAG, "Pool usage: %.1f%%", pool_get_usage_percent(&pool));
ESP_LOGI(TAG, "Free blocks: %d", pool_get_free_count(&pool));
```

## Performance Best Practices

### 1. Use IRAM_ATTR for ISRs
```c
static void IRAM_ATTR my_isr_handler(void* arg) {
    // ISR code here
}
```

### 2. Const-correctness
```cpp
const uint16_t color = theme_get_bg();
constexpr int SIZE = 32;
```

### 3. Static allocation over dynamic
```cpp
// Bad
std::vector<Item> items;

// Good
Item items[MAX_ITEMS];
uint8_t item_count;
```

### 4. Cache-friendly access patterns
```c
// Sequential access (good)
for (int i = 0; i < count; i++) {
    process(array[i]);
}

// Random access (avoid if possible)
for (int i = 0; i < count; i++) {
    process(array[random_indices[i]]);
}
```

### 5. Minimize critical sections
```c
// Bad: Long critical section
taskENTER_CRITICAL();
do_lots_of_work();
taskEXIT_CRITICAL();

// Good: Minimal critical section
prepare_data();
taskENTER_CRITICAL();
update_shared_state();
taskEXIT_CRITICAL();
process_data();
```

## Memory Management

### Stack Usage
```c
// Check stack high water mark
UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Stack remaining: %d bytes", stack_remaining * 4);
```

### Heap Usage
```c
// Check free heap
size_t free_heap = esp_get_free_heap_size();
size_t min_free = esp_get_minimum_free_heap_size();
ESP_LOGI(TAG, "Free heap: %d, min: %d", free_heap, min_free);
```

### PSRAM Usage
```c
// Allocate in PSRAM
void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// Check PSRAM
size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
```

## Common Patterns

### Task Creation
```c
xTaskCreatePinnedToCore(
    task_function,
    "task_name",
    8192,           // Stack size
    NULL,           // Parameters
    5,              // Priority
    NULL,           // Task handle
    1               // Core (0 or 1)
);
```

### Queue Usage
```c
QueueHandle_t queue = xQueueCreate(10, sizeof(message_t));

// Send (with timeout)
if (xQueueSend(queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Queue full");
}

// Receive (non-blocking)
message_t msg;
if (xQueueReceive(queue, &msg, 0) == pdTRUE) {
    // Process message
}
```

### Timer Usage
```c
const TickType_t xFrameTime = pdMS_TO_TICKS(25);  // 40 FPS
TickType_t xLastWakeTime = xTaskGetTickCount();

while (1) {
    // Do work
    
    // Precise timing
    vTaskDelayUntil(&xLastWakeTime, xFrameTime);
}
```

## Debugging Tips

### Enable Verbose Logging
```c
esp_log_level_set(TAG, ESP_LOG_VERBOSE);
```

### Assert on Errors
```c
#define CONFIG_ASSERT_ON_ERROR 1
```

### Stack Overflow Detection
```ini
# sdkconfig
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
```

### Memory Debugging
```c
// Enable heap tracing
#include "esp_heap_trace.h"
heap_trace_start(HEAP_TRACE_LEAKS);
// ... run code ...
heap_trace_stop();
heap_trace_dump();
```

## Build Optimization

### Release Build
```ini
[env:release]
build_type = release
build_flags = 
    -O2
    -DNDEBUG
```

### Size Optimization
```ini
build_flags = 
    -Os
    -ffunction-sections
    -fdata-sections
```

### Speed Optimization
```ini
build_flags = 
    -O3
    -ffast-math
```

## Testing Checklist

- [ ] No compiler warnings with -Werror
- [ ] Stack high water mark > 1KB
- [ ] Heap doesn't grow unbounded
- [ ] No memory leaks (heap trace)
- [ ] Frame rate stable at 40 FPS
- [ ] ISR latency < 10μs
- [ ] Power consumption acceptable
- [ ] No watchdog timeouts

## Resources

- ESP-IDF Documentation: https://docs.espressif.com/projects/esp-idf/
- LovyanGFX: https://github.com/lovyan03/LovyanGFX
- FreeRTOS: https://www.freertos.org/

## Support

For issues or questions:
1. Check OPTIMIZATION_SUMMARY.md
2. Review HAL documentation in src/hal/
3. Check compile-time assertions
4. Enable verbose logging
5. Use heap/stack tracing tools
