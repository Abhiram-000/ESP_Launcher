/*
 * Fixed-block pool allocator over caller-provided memory.
 *
 * THREAD-SAFETY: a pool_allocator_t instance is not thread-safe. All calls
 * on one instance (alloc/free/reset/stats) must come from a single task or
 * be serialized externally by the owner — pool_allocator.c takes no locks.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void* pool_memory;
    uint8_t* free_bitmap;
    uint16_t block_size;
    uint16_t block_count;
    uint16_t blocks_used;
} pool_allocator_t;

// Initialize pool allocator with pre-allocated memory
bool pool_init(pool_allocator_t* pool, void* memory, size_t memory_size, size_t block_size);

// Allocate a block from the pool
void* pool_alloc(pool_allocator_t* pool);

// Free a block back to the pool
void pool_free(pool_allocator_t* pool, void* ptr);

// Get pool statistics
uint16_t pool_get_used_count(const pool_allocator_t* pool);
uint16_t pool_get_free_count(const pool_allocator_t* pool);
float pool_get_usage_percent(const pool_allocator_t* pool);

// Reset pool (mark all blocks as free)
void pool_reset(pool_allocator_t* pool);

#ifdef __cplusplus
}
#endif
