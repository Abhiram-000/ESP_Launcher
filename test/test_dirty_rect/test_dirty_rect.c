#include "unity.h"
#include <string.h>
#include "dirty_rect.h"
#include "../../src/utils/dirty_rect.c"

static dirty_rect_manager_t mgr;

void setUp(void) {
    dirty_rect_init(&mgr);
}

void tearDown(void) {}

void test_init_cleans_state(void) {
    TEST_ASSERT_FALSE(dirty_rect_is_dirty(&mgr));
    TEST_ASSERT_FALSE(dirty_rect_needs_full_redraw(&mgr));
    TEST_ASSERT_EQUAL(0, dirty_rect_get_count(&mgr));
}

void test_mark_dirty_increments_count(void) {
    dirty_rect_mark_dirty(&mgr, 10, 10, 50, 50);
    TEST_ASSERT_EQUAL(1, dirty_rect_get_count(&mgr));
    TEST_ASSERT_TRUE(dirty_rect_is_dirty(&mgr));
}

void test_mark_dirty_multiple(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 10, 10);
    dirty_rect_mark_dirty(&mgr, 20, 20, 30, 30);
    dirty_rect_mark_dirty(&mgr, 60, 60, 10, 10);
    TEST_ASSERT_EQUAL(3, dirty_rect_get_count(&mgr));
}

void test_mark_dirty_overflow_sets_full(void) {
    for (int i = 0; i < DIRTY_RECT_MAX; i++) {
        dirty_rect_mark_dirty(&mgr, i * 20, 0, 10, 10);
    }
    TEST_ASSERT_FALSE(dirty_rect_needs_full_redraw(&mgr));
    dirty_rect_mark_dirty(&mgr, 200, 0, 10, 10);
    TEST_ASSERT_TRUE(dirty_rect_needs_full_redraw(&mgr));
}

void test_mark_full(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 10, 10);
    dirty_rect_mark_full(&mgr);
    TEST_ASSERT_TRUE(dirty_rect_needs_full_redraw(&mgr));
    TEST_ASSERT_EQUAL(0, dirty_rect_get_count(&mgr));
}

void test_clear(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 10, 10);
    dirty_rect_clear(&mgr);
    TEST_ASSERT_FALSE(dirty_rect_is_dirty(&mgr));
    TEST_ASSERT_FALSE(dirty_rect_needs_full_redraw(&mgr));
    TEST_ASSERT_EQUAL(0, dirty_rect_get_count(&mgr));
}

void test_mark_dirty_ignores_zero_width(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 0, 10);
    TEST_ASSERT_EQUAL(0, dirty_rect_get_count(&mgr));
}

void test_mark_dirty_ignores_zero_height(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 10, 0);
    TEST_ASSERT_EQUAL(0, dirty_rect_get_count(&mgr));
}

void test_get_rect(void) {
    dirty_rect_mark_dirty(&mgr, 5, 10, 20, 30);
    const dirty_rect_t* r = dirty_rect_get(&mgr, 0);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(5, r->x);
    TEST_ASSERT_EQUAL(10, r->y);
    TEST_ASSERT_EQUAL(20, r->w);
    TEST_ASSERT_EQUAL(30, r->h);
}

void test_get_rect_out_of_bounds(void) {
    const dirty_rect_t* r = dirty_rect_get(&mgr, 0);
    TEST_ASSERT_NULL(r);
}

void test_optimize_merges_overlapping(void) {
    dirty_rect_mark_dirty(&mgr, 10, 10, 30, 30);
    dirty_rect_mark_dirty(&mgr, 20, 20, 30, 30);
    dirty_rect_optimize(&mgr);
    TEST_ASSERT_EQUAL(1, dirty_rect_get_count(&mgr));
    const dirty_rect_t* r = dirty_rect_get(&mgr, 0);
    TEST_ASSERT_EQUAL(10, r->x);
    TEST_ASSERT_EQUAL(10, r->y);
    TEST_ASSERT_EQUAL(40, r->w);
    TEST_ASSERT_EQUAL(40, r->h);
}

void test_optimize_keeps_separate(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 10, 10);
    dirty_rect_mark_dirty(&mgr, 100, 100, 10, 10);
    dirty_rect_optimize(&mgr);
    TEST_ASSERT_EQUAL(2, dirty_rect_get_count(&mgr));
}

void test_optimize_noop_for_single(void) {
    dirty_rect_mark_dirty(&mgr, 0, 0, 10, 10);
    dirty_rect_optimize(&mgr);
    TEST_ASSERT_EQUAL(1, dirty_rect_get_count(&mgr));
}

void test_optimize_noop_for_empty(void) {
    dirty_rect_optimize(&mgr);
    TEST_ASSERT_EQUAL(0, dirty_rect_get_count(&mgr));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_cleans_state);
    RUN_TEST(test_mark_dirty_increments_count);
    RUN_TEST(test_mark_dirty_multiple);
    RUN_TEST(test_mark_dirty_overflow_sets_full);
    RUN_TEST(test_mark_full);
    RUN_TEST(test_clear);
    RUN_TEST(test_mark_dirty_ignores_zero_width);
    RUN_TEST(test_mark_dirty_ignores_zero_height);
    RUN_TEST(test_get_rect);
    RUN_TEST(test_get_rect_out_of_bounds);
    RUN_TEST(test_optimize_merges_overlapping);
    RUN_TEST(test_optimize_keeps_separate);
    RUN_TEST(test_optimize_noop_for_single);
    RUN_TEST(test_optimize_noop_for_empty);
    return UNITY_END();
}
