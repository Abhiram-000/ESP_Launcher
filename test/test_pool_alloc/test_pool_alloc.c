#include "unity.h"
#include <string.h>
#include "pool_allocator.h"
#include "../../src/utils/pool_allocator.c"

#define TEST_POOL_SIZE 1024
#define TEST_BLOCK_SIZE 32

static uint8_t test_memory[TEST_POOL_SIZE];
static pool_allocator_t pool;

void setUp(void) {
    memset(test_memory, 0, sizeof(test_memory));
    pool_init(&pool, test_memory, TEST_POOL_SIZE, TEST_BLOCK_SIZE);
}

void tearDown(void) {}

void test_pool_init_success(void) {
    TEST_ASSERT_NOT_NULL(pool.pool_memory);
    TEST_ASSERT_NOT_NULL(pool.free_bitmap);
    TEST_ASSERT_GREATER_THAN(0, pool.block_count);
    TEST_ASSERT_EQUAL(0, pool.blocks_used);
}

void test_pool_init_null_pool(void) {
    TEST_ASSERT_FALSE(pool_init(NULL, test_memory, TEST_POOL_SIZE, TEST_BLOCK_SIZE));
}

void test_pool_init_null_memory(void) {
    TEST_ASSERT_FALSE(pool_init(&pool, NULL, TEST_POOL_SIZE, TEST_BLOCK_SIZE));
}

void test_pool_init_zero_block_size(void) {
    TEST_ASSERT_FALSE(pool_init(&pool, test_memory, TEST_POOL_SIZE, 0));
}

void test_pool_alloc_returns_non_null(void) {
    void* ptr = pool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(ptr);
}

void test_pool_alloc_increments_used(void) {
    pool_alloc(&pool);
    TEST_ASSERT_EQUAL(1, pool_get_used_count(&pool));
}

void test_pool_alloc_multiple(void) {
    void* p1 = pool_alloc(&pool);
    void* p2 = pool_alloc(&pool);
    void* p3 = pool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_NOT_NULL(p3);
    TEST_ASSERT_EQUAL(3, pool_get_used_count(&pool));
}

void test_pool_alloc_unique_pointers(void) {
    void* p1 = pool_alloc(&pool);
    void* p2 = pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL(p1, p2);
}

void test_pool_free_decrements_used(void) {
    void* ptr = pool_alloc(&pool);
    TEST_ASSERT_EQUAL(1, pool_get_used_count(&pool));
    pool_free(&pool, ptr);
    TEST_ASSERT_EQUAL(0, pool_get_used_count(&pool));
}

void test_pool_free_null(void) {
    pool_free(&pool, NULL);
    TEST_ASSERT_EQUAL(0, pool_get_used_count(&pool));
}

void test_pool_free_reusable(void) {
    void* ptr = pool_alloc(&pool);
    pool_free(&pool, ptr);
    void* ptr2 = pool_alloc(&pool);
    TEST_ASSERT_EQUAL(ptr, ptr2);
}

void test_pool_exhaustion(void) {
    uint16_t count = pool.block_count;
    void* ptrs[256];
    for (uint16_t i = 0; i < count && i < 256; i++) {
        ptrs[i] = pool_alloc(&pool);
    }
    void* overflow = pool_alloc(&pool);
    TEST_ASSERT_NULL(overflow);
}

void test_pool_reset(void) {
    pool_alloc(&pool);
    pool_alloc(&pool);
    pool_reset(&pool);
    TEST_ASSERT_EQUAL(0, pool_get_used_count(&pool));
    TEST_ASSERT_EQUAL(pool.block_count, pool_get_free_count(&pool));
}

void test_pool_usage_percent(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, pool_get_usage_percent(&pool));
    pool_alloc(&pool);
    float expected = 100.0f / pool.block_count;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected, pool_get_usage_percent(&pool));
}

void test_pool_null_safety(void) {
    TEST_ASSERT_EQUAL(0, pool_get_used_count(NULL));
    TEST_ASSERT_EQUAL(0, pool_get_free_count(NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, pool_get_usage_percent(NULL));
    pool_reset(NULL);
}

void test_pool_free_out_of_range(void) {
    void* fake_ptr = (void*)0xDEADBEEF;
    uint16_t before = pool_get_used_count(&pool);
    pool_free(&pool, fake_ptr);
    TEST_ASSERT_EQUAL(before, pool_get_used_count(&pool));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pool_init_success);
    RUN_TEST(test_pool_init_null_pool);
    RUN_TEST(test_pool_init_null_memory);
    RUN_TEST(test_pool_init_zero_block_size);
    RUN_TEST(test_pool_alloc_returns_non_null);
    RUN_TEST(test_pool_alloc_increments_used);
    RUN_TEST(test_pool_alloc_multiple);
    RUN_TEST(test_pool_alloc_unique_pointers);
    RUN_TEST(test_pool_free_decrements_used);
    RUN_TEST(test_pool_free_null);
    RUN_TEST(test_pool_free_reusable);
    RUN_TEST(test_pool_exhaustion);
    RUN_TEST(test_pool_reset);
    RUN_TEST(test_pool_usage_percent);
    RUN_TEST(test_pool_null_safety);
    RUN_TEST(test_pool_free_out_of_range);
    return UNITY_END();
}
