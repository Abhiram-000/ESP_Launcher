#include "unity.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "compile_checks.h"

void test_static_assert_buffer_size_pass(void) {
    char buf[64];
    STATIC_ASSERT_BUFFER_SIZE(buf, 64);
    TEST_PASS();
}

void test_compile_checks_header_includes(void) {
    TEST_ASSERT_TRUE(1);
}

void test_bounds_check_does_not_abort_valid(void) {
    BOUNDS_CHECK(0, 10);
    BOUNDS_CHECK(9, 10);
    BOUNDS_CHECK(5, 10);
    TEST_PASS();
}

void test_safe_memcpy_basic(void) {
    char src[] = "hello";
    char dst[32] = {0};
    safe_memcpy(dst, sizeof(dst), src, sizeof(src));
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_memcpy_truncates(void) {
    char src[] = "hello world";
    char dst[5] = {0};
    safe_memcpy(dst, sizeof(dst), src, sizeof(src));
    TEST_ASSERT_EQUAL_INT('h', dst[0]);
    TEST_ASSERT_EQUAL_INT('e', dst[1]);
    TEST_ASSERT_EQUAL_INT('l', dst[2]);
    TEST_ASSERT_EQUAL_INT('l', dst[3]);
    TEST_ASSERT_EQUAL_INT('o', dst[4]);
}

void test_safe_memcpy_null_dst(void) {
    safe_memcpy(NULL, 0, "test", 4);
    TEST_PASS();
}

void test_safe_memcpy_null_src(void) {
    char dst[10] = {0};
    safe_memcpy(dst, sizeof(dst), NULL, 0);
    TEST_PASS();
}

void test_safe_strncpy_basic(void) {
    char dst[32] = {0};
    safe_strncpy(dst, sizeof(dst), "hello");
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_strncpy_truncates(void) {
    char dst[5] = {0};
    safe_strncpy(dst, sizeof(dst), "hello world");
    TEST_ASSERT_EQUAL_STRING("hell", dst);
    TEST_ASSERT_EQUAL('\0', dst[4]);
}

void test_safe_strncpy_null_dst(void) {
    safe_strncpy(NULL, 0, "test");
    TEST_PASS();
}

void test_safe_strncpy_zero_size(void) {
    char buf[10] = {0};
    safe_strncpy(buf, 0, "test");
    TEST_ASSERT_EQUAL('\0', buf[0]);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_static_assert_buffer_size_pass);
    RUN_TEST(test_compile_checks_header_includes);
    RUN_TEST(test_bounds_check_does_not_abort_valid);
    RUN_TEST(test_safe_memcpy_basic);
    RUN_TEST(test_safe_memcpy_truncates);
    RUN_TEST(test_safe_memcpy_null_dst);
    RUN_TEST(test_safe_memcpy_null_src);
    RUN_TEST(test_safe_strncpy_basic);
    RUN_TEST(test_safe_strncpy_truncates);
    RUN_TEST(test_safe_strncpy_null_dst);
    RUN_TEST(test_safe_strncpy_zero_size);
    return UNITY_END();
}
