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
 */
const char* wifi_mgr_get_connected_ssid(void);

#ifdef __cplusplus
}
#endif
