#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compile-time buffer size validation
#define STATIC_ASSERT_BUFFER_SIZE(buf, expected_size) \
    _Static_assert(sizeof(buf) >= (expected_size), "Buffer too small")

// Compile-time struct size validation
#define STATIC_ASSERT_STRUCT_SIZE(type, expected_size) \
    _Static_assert(sizeof(type) == (expected_size), "Struct size mismatch")

// Compile-time array bounds check
#define STATIC_ASSERT_ARRAY_SIZE(arr, expected_count) \
    _Static_assert((sizeof(arr) / sizeof((arr)[0])) >= (expected_count), "Array too small")

// Runtime bounds check with assertion
#define BOUNDS_CHECK(index, max) \
    do { \
        if ((index) >= (max)) { \
            ESP_LOGE("BOUNDS", "Index %d >= max %d at %s:%d", (int)(index), (int)(max), __FILE__, __LINE__); \
            abort(); \
        } \
    } while(0)

// Safe buffer copy with bounds check
static inline void safe_memcpy(void* dst, size_t dst_size, const void* src, size_t src_size) {
    size_t copy_size = (src_size < dst_size) ? src_size : dst_size;
    if (copy_size > 0 && dst && src) {
        memcpy(dst, src, copy_size);
    }
}

// Safe string copy with null termination guarantee
static inline void safe_strncpy(char* dst, size_t dst_size, const char* src) {
    if (dst_size > 0 && dst && src) {
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

#ifdef __cplusplus
}
#endif
