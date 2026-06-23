#include "unity.h"
#include "color_utils.h"

void test_color_blend_full_fg(void) {
    uint16_t result = color_blend(0xFFFF, 0x0000, 1.0f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, result);
}

void test_color_blend_full_bg(void) {
    uint16_t result = color_blend(0xFFFF, 0x0000, 0.0f);
    TEST_ASSERT_EQUAL_HEX16(0x0000, result);
}

void test_color_blend_half(void) {
    uint16_t result = color_blend(0xF800, 0x001F, 0.5f);
    TEST_ASSERT_TRUE(result > 0x001F);
    TEST_ASSERT_TRUE(result < 0xF800);
}

void test_color_blend_over_one(void) {
    uint16_t result = color_blend(0xF800, 0x001F, 1.5f);
    TEST_ASSERT_EQUAL_HEX16(0xF800, result);
}

void test_color_blend_under_zero(void) {
    uint16_t result = color_blend(0xF800, 0x001F, -0.5f);
    TEST_ASSERT_EQUAL_HEX16(0x001F, result);
}

void test_color_blend_same_colors(void) {
    uint16_t result = color_blend(0x07E0, 0x07E0, 0.5f);
    TEST_ASSERT_EQUAL_HEX16(0x07E0, result);
}

void test_color_blend_pure_red(void) {
    uint16_t fg = 0xF800;
    uint16_t bg = 0x0000;
    uint16_t result = color_blend(fg, bg, 0.5f);
    TEST_ASSERT_TRUE(result > 0);
}

void test_color_blend_pure_green(void) {
    uint16_t fg = 0x07E0;
    uint16_t bg = 0x0000;
    uint16_t result = color_blend(fg, bg, 0.5f);
    TEST_ASSERT_TRUE(result > 0);
}

void test_color_blend_pure_blue(void) {
    uint16_t fg = 0x001F;
    uint16_t bg = 0x0000;
    uint16_t result = color_blend(fg, bg, 0.5f);
    TEST_ASSERT_TRUE(result > 0);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_color_blend_full_fg);
    RUN_TEST(test_color_blend_full_bg);
    RUN_TEST(test_color_blend_half);
    RUN_TEST(test_color_blend_over_one);
    RUN_TEST(test_color_blend_under_zero);
    RUN_TEST(test_color_blend_same_colors);
    RUN_TEST(test_color_blend_pure_red);
    RUN_TEST(test_color_blend_pure_green);
    RUN_TEST(test_color_blend_pure_blue);
    return UNITY_END();
}
