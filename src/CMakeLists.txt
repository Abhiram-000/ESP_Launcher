FILE(GLOB_RECURSE app_sources "*.c" "*.cpp")
idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "." "config" "system" "services" "ui" "hal" "utils"
                    REQUIRES nofrendo esp_littlefs esp_http_client esp-tls driver bootloader_support)

# Disable false positive warning in ESP-IDF I2C driver
idf_build_set_property(COMPILE_OPTIONS "-Wno-maybe-uninitialized" APPEND)