# CONFIGURE_DEPENDS: without it CMake evaluates the glob once at configure
# time, so adding a new .c/.cpp later does not trigger a rebuild until the
# build directory is reconfigured. That silently ships a stale image.
file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS "*.c" "*.cpp")
idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "." "config" "system" "services" "ui" "hal" "utils"
                    REQUIRES nofrendo joltwallet__littlefs esp_http_client esp-tls driver bootloader_support bt)

# Pentest service: override libnet80211.a's internal frame sanity check
# via our strong definition (src/hal/wsl_bypass.c) so deauth/disassoc
# frames transmit. -z muldefs lets the linker accept both definitions;
# ours wins because main-component objects precede the esp_wifi archive.
# (--wrap is ineffective here: caller and callee share one archive member,
# so the reference is never undefined.) No vendor binaries are modified.
target_link_libraries(${COMPONENT_LIB} PRIVATE
    "-Wl,-z,muldefs")

# Disable false positive warning in ESP-IDF I2C driver
idf_build_set_property(COMPILE_OPTIONS "-Wno-maybe-uninitialized" APPEND)