/**
 * @file wifi_mgr.h
 * @brief Professional Wi-Fi Manager for Station and Scan operations.
 */
#pragma once

#include "esp_wifi.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_scan_info_t;

/**
 * @brief Initialize the Wi-Fi manager in STA mode.
 */
void wifi_mgr_init(void);

/**
 * @brief Trigger an asynchronous Wi-Fi scan.
 */
void wifi_mgr_scan_start(void);

/**
 * @brief Retrieve results of the last completed scan.
 * @return Number of networks found.
 */
int wifi_mgr_get_scan_results(wifi_scan_info_t* results, int max_results);

/**
 * @brief Check if the Wi-Fi scan is complete.
 */
bool wifi_mgr_is_scan_done(void);

/**
 * @brief Attempt to connect to a specific AP.
 */
void wifi_mgr_connect(const char* ssid, const char* password);

/**
 * @brief Disconnect from current AP.
 */
void wifi_mgr_disconnect(void);

/**
 * @brief Check if currently connected to an AP with an IP.
 */
bool wifi_mgr_is_connected(void);

/**
 * @brief Get current SSID if connected.
 * @return Pointer to an internal static copy, overwritten by the next call
 *         from any task. Single-consumer only — copy the string out
 *         immediately if more than one consumer needs it. Returns "" if
 *         not connected.
 */
const char* wifi_mgr_get_connected_ssid(void);

/**
 * @brief Get current STA IP address as a string (e.g. "192.168.1.42").
 * @return Pointer to static buffer, overwritten by the next call from any
 *         task. Single-consumer only — copy before reuse. Returns
 *         "0.0.0.0" if not connected.
 */
const char* wifi_mgr_get_ip(void);

/**
 * @brief Get RSSI (signal strength) of the currently connected AP.
 * @return RSSI in dBm (e.g. -62), or -100 if not connected.
 */
int8_t wifi_mgr_get_rssi(void);

#ifdef __cplusplus
}
#endif
