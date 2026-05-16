idf_component_register(
    SRCS "boot_gpio_hook.c"
    REQUIRES bootloader bootloader_support soc esp_rom
)

# This is the official ESP-IDF linker trick to force-include the hook symbols
# even though they are weak and may otherwise be optimized away.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_after_init")
