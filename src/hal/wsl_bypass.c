/*
 * wsl_bypass.c — frame sanity-check override for security-audit tooling
 * ====================================================================
 * WHY THIS EXISTS:
 * Espressif's closed-source libnet80211.a filters raw frames passed to
 * esp_wifi_80211_tx() through ieee80211_raw_frame_sanity_check(), which
 * silently rejects deauthentication (0xC0) and disassociation (0xA0)
 * management frames. Legitimate WiFi-resilience testing (Spacehuhn /
 * Marauder / GhostESP class tools) requires injecting those frames.
 *
 * MECHANISM (and why not ld --wrap):
 * The caller (esp_wifi_80211_tx) and the check live in the SAME archive
 * member (ieee80211_output.o), so the reference is never "undefined" at
 * link time and GNU ld --wrap legally does nothing. Instead we provide a
 * strong duplicate definition and link with -Wl,-z,muldefs (set in
 * src/CMakeLists.txt). Main-component objects precede the esp_wifi
 * archive in link order, so OUR definition wins global symbol
 * arbitration and the library's relocations bind here. This is the
 * field-proven approach used by Bruce firmware / esp32-wifi-penetration-
 * tool; no vendor binary is modified.
 *
 * Verified on ESP-IDF 5.5.2 (esp32s3): relocation types R_XTENSA_32 +
 * R_XTENSA_ASM_EXPAND against this symbol inside ieee80211_output.o.
 */
#include <stdint.h>

__attribute__((used))
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3)
{
    (void)arg;
    (void)arg2;
    (void)arg3;
    return 0;   /* 0 = frame passed sanity check → transmit */
}
