/**
 * @file wifi_mgr.c
 * @brief Unified Wi_Fi Manager (AP + STA) for OTA and AI Code App.
 */
#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "../system/sys_mgr.h"
#include "../config/sys_config.h"

static const char *TAG = "WIFI_MGR";

static bool s_sta_connected = false;
static char s_sta_ip[16] = "0.0.0.0";
static char s_sta_ssid[33] = {0};
static char s_sta_pass[65] = {0};

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "AP: Station "MACSTR" joined", MAC2STR(event->mac));
                sys_mgr_send_ui_msg(OTA_STATE_CONNECTED, 0, 0, "Client Joined AP");
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGI(TAG, "AP: Station "MACSTR" left", MAC2STR(event->mac));
                sys_mgr_send_ui_msg(OTA_STATE_IDLE, 0, 0, "Awaiting AP Client");
                break;
            }
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA Started");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_sta_connected = false;
                ESP_LOGI(TAG, "STA Disconnected");
                sys_mgr_send_ui_msg(WIFI_STATE_DISCONNECTED, 0, 0, "WiFi Disconnected");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_sta_ip, sizeof(s_sta_ip));
        s_sta_connected = true;
        ESP_LOGI(TAG, "STA Got IP: %s", s_sta_ip);
        
        // Save to NVS on successful connection
        nvs_handle_t h;
        if (nvs_open("wifi_creds", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "ssid", s_sta_ssid);
            nvs_set_str(h, "pass", s_sta_pass);
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGI(TAG, "STA Credentials saved to NVS");
        }

        sys_mgr_send_ui_msg(WIFI_STATE_SUCCESS, 100, 0, "Connected");
    }
}

void wifi_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing Unified Wi-Fi Manager (APSTA)");

    // Initialize networking stack (idempotent)
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
    }

    // Create default netifs only if they don't exist
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == NULL) {
        esp_netif_create_default_wifi_ap();
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // AP Config
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, APP_WIFI_SSID, sizeof(ap_config.ap.ssid)-1);
    ap_config.ap.ssid_len = strlen(APP_WIFI_SSID);
    ap_config.ap.channel = APP_WIFI_CHANNEL;
    ap_config.ap.max_connection = APP_WIFI_MAX_CONN;
    ap_config.ap.authmode = (strlen(APP_WIFI_PASS) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    if (strlen(APP_WIFI_PASS) > 0) strncpy((char*)ap_config.ap.password, APP_WIFI_PASS, sizeof(ap_config.ap.password)-1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Auto-reconnect from NVS
    nvs_handle_t h;
    if (nvs_open("wifi_creds", NVS_READONLY, &h) == ESP_OK) {
        size_t s_len = sizeof(s_sta_ssid), p_len = sizeof(s_sta_pass);
        if (nvs_get_str(h, "ssid", s_sta_ssid, &s_len) == ESP_OK &&
            nvs_get_str(h, "pass", s_sta_pass, &p_len) == ESP_OK) {
            ESP_LOGI(TAG, "NVS: Found saved credentials for %s. Connecting...", s_sta_ssid);
            wifi_config_t sta_cfg = {0};
            strncpy((char*)sta_cfg.sta.ssid, s_sta_ssid, sizeof(sta_cfg.sta.ssid)-1);
            strncpy((char*)sta_cfg.sta.password, s_sta_pass, sizeof(sta_cfg.sta.password)-1);
            esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
            esp_wifi_connect();
        }
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Wi-Fi Unified initialized. Mode: APSTA");
}

void wifi_mgr_scan_start(void) {
    wifi_scan_config_t scan_config = { .show_hidden = false };
    esp_wifi_scan_start(&scan_config, false);
}

int wifi_mgr_get_scan_results(wifi_scan_info_t* results, int max_results) {
    uint16_t number = max_results;
    wifi_ap_record_t ap_info[max_results];
    if (esp_wifi_scan_get_ap_records(&number, ap_info) != ESP_OK) return 0;
    for (int i = 0; i < number; i++) {
        strncpy(results[i].ssid, (char*)ap_info[i].ssid, sizeof(results[i].ssid)-1);
        results[i].rssi = ap_info[i].rssi;
        results[i].authmode = ap_info[i].authmode;
    }
    return number;
}

void wifi_mgr_connect(const char* ssid, const char* password) {
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid)-1);
    strncpy(s_sta_pass, password, sizeof(s_sta_pass)-1);
    
    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid)-1);
    strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password)-1);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();
}

void wifi_mgr_disconnect(void) {
    esp_wifi_disconnect();
}

bool wifi_mgr_is_connected(void) {
    return s_sta_connected;
}

const char* wifi_mgr_get_connected_ssid(void) {
    return s_sta_ssid;
}
