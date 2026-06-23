#include "unity.h"
#include "sys_config.h"

void test_ui_message_struct_size(void) {
    TEST_ASSERT_LESS_OR_EQUAL(128, sizeof(ui_message_t));
}

void test_ui_message_msg_buffer(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(64, sizeof(((ui_message_t*)0)->msg));
}

void test_ui_message_ip_buffer(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(16, sizeof(((ui_message_t*)0)->ip_addr));
}

void test_ota_magic_byte(void) {
    TEST_ASSERT_EQUAL_HEX8(0xE9, APP_OTA_ESP32_MAGIC);
}

void test_ota_size_limits(void) {
    TEST_ASSERT_GREATER_THAN(0, APP_OTA_MIN_IMAGE_SIZE);
    TEST_ASSERT_GREATER_THAN(APP_OTA_MIN_IMAGE_SIZE, APP_OTA_MAX_IMAGE_SIZE);
}

void test_nes_stack_size(void) {
    TEST_ASSERT_EQUAL(48 * 1024, NES_EMU_TASK_STACK);
}

void test_ai_constants(void) {
    TEST_ASSERT_GREATER_THAN(0, AI_LUA_TIMEOUT_MS);
    TEST_ASSERT_GREATER_THAN(0, AI_LUA_HOOK_INTERVAL);
    TEST_ASSERT_GREATER_THAN(0, AI_LUA_MAX_SCRIPT_BYTES);
    TEST_ASSERT_GREATER_THAN(0, AI_STREAM_BUF_SIZE);
}

void test_game_store_limits(void) {
    TEST_ASSERT_GREATER_THAN(0, GAME_STORE_ROM_MIN_SIZE);
    TEST_ASSERT_GREATER_THAN(GAME_STORE_ROM_MIN_SIZE, GAME_STORE_ROM_MAX_SIZE);
    TEST_ASSERT_GREATER_THAN(0, GAME_STORE_MAX_GAMES);
}

void test_boot_magic_constants(void) {
    TEST_ASSERT_NOT_EQUAL(BOOT_FORCE_LAUNCHER_MAGIC, BOOT_ALLOW_OTA1_MAGIC);
}

void test_ntp_epoch_bounds(void) {
    TEST_ASSERT_LESS_THAN(NTP_EPOCH_MAX, NTP_EPOCH_MIN);
}

void test_weather_constants(void) {
    TEST_ASSERT_GREATER_THAN(0, WEATHER_REFRESH_INTERVAL_S);
    TEST_ASSERT_GREATER_THAN(WEATHER_REFRESH_INTERVAL_S, WEATHER_STALE_THRESHOLD_S);
}

void test_http_constants(void) {
    TEST_ASSERT_GREATER_THAN(0, APP_HTTP_PORT);
    TEST_ASSERT_GREATER_THAN(0, APP_HTTP_MAX_SOCKETS);
    TEST_ASSERT_GREATER_THAN(0, APP_OTA_RECV_BUF_SIZE);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_message_struct_size);
    RUN_TEST(test_ui_message_msg_buffer);
    RUN_TEST(test_ui_message_ip_buffer);
    RUN_TEST(test_ota_magic_byte);
    RUN_TEST(test_ota_size_limits);
    RUN_TEST(test_nes_stack_size);
    RUN_TEST(test_ai_constants);
    RUN_TEST(test_game_store_limits);
    RUN_TEST(test_boot_magic_constants);
    RUN_TEST(test_ntp_epoch_bounds);
    RUN_TEST(test_weather_constants);
    RUN_TEST(test_http_constants);
    return UNITY_END();
}
