#include "pool_allocator.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "POOL_ALLOC";

#define BITMAP_BYTE_COUNT(blocks) (((blocks) + 7) / 8)

static inline bool bitmap_get(const uint8_t* bitmap, uint16_t index) {
    return (bitmap[index / 8] & (1 << (index % 8))) != 0;
}

static inline void bitmap_set(uint8_t* bitmap, uint16_t index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

static inline void bitmap_clear(uint8_t* bitmap, uint16_t index) {
    bitmap[index / 8] &= ~(1 << (index % 8));
}

bool pool_init(pool_allocator_t* pool, void* memory, size_t memory_size, size_t block_size) {
    if (!pool || !memory || block_size == 0) return false;
    
    // Calculate how many blocks fit
    size_t bitmap_size = BITMAP_BYTE_COUNT(memory_size / block_size);
    size_t usable_size = memory_size - bitmap_size;
    uint16_t block_count = usable_size / block_size;
    
    if (block_count == 0) return false;
    
    pool->pool_memory = (uint8_t*)memory + bitmap_size;
    pool->free_bitmap = (uint8_t*)memory;
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->blocks_used = 0;
    
    // Initialize bitmap (all free)
    memset(pool->free_bitmap, 0, bitmap_size);
    
    return true;
}

void* pool_alloc(pool_allocator_t* pool) {
    if (!pool || pool->blocks_used >= pool->block_count) return NULL;
    
    // Find first free block
    for (uint16_t i = 0; i < pool->block_count; i++) {
        if (!bitmap_get(pool->free_bitmap, i)) {
            bitmap_set(pool->free_bitmap, i);
            pool->blocks_used++;
            return (uint8_t*)pool->pool_memory + (i * pool->block_size);
        }
    }
    
    return NULL;
}

void pool_free(pool_allocator_t* pool, void* ptr) {
    if (!pool || !ptr) return;
    
    // Calculate block index
    ptrdiff_t offset = (uint8_t*)ptr - (uint8_t*)pool->pool_memory;
    if (offset < 0 || offset >= (ptrdiff_t)(pool->block_count * pool->block_size)) {
        return; // Invalid pointer
    }
    
    uint16_t index = offset / pool->block_size;
    if (index >= pool->block_count) return;
    
    if (bitmap_get(pool->free_bitmap, index)) {
        bitmap_clear(pool->free_bitmap, index);
        pool->blocks_used--;
    }
}

uint16_t pool_get_used_count(const pool_allocator_t* pool) {
    return pool ? pool->blocks_used : 0;
}

uint16_t pool_get_free_count(const pool_allocator_t* pool) {
    return pool ? (pool->block_count - pool->blocks_used) : 0;
}

float pool_get_usage_percent(const pool_allocator_t* pool) {
    if (!pool || pool->block_count == 0) return 0.0f;
    return (float)pool->blocks_used / (float)pool->block_count * 100.0f;
}

void pool_reset(pool_allocator_t* pool) {
    if (!pool) return;
    size_t bitmap_size = BITMAP_BYTE_COUNT(pool->block_count);
    memset(pool->free_bitmap, 0, bitmap_size);
    pool->blocks_used = 0;
}
