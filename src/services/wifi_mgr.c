/**
 * @file wifi_mgr.c
 * @brief Unified Wi-Fi Manager (AP + STA).
 *
 * Reconnect policy: a link that has been established (or restored from NVS at
 * boot) is maintained with bounded exponential backoff. The previous handler
 * re-issued esp_wifi_connect() unconditionally on every disconnect, which
 * looped forever while the configured AP was absent and — critically — kept
 * the STA permanently mid-connect. esp_wifi_scan_start() fails with
 * ESP_ERR_WIFI_STATE in exactly that state, so opening the network list could
 * spin forever. See _try_start_scan() and the disconnect handler.
 */
#include "wifi_mgr.h"
#include "dns_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "../system/sys_mgr.h"
#include "../config/sys_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "WIFI_MGR";

static bool s_sta_connected = false;
static char s_sta_ip[16] = "0.0.0.0";
static char s_sta_ssid[33] = {0};
static char s_sta_pass[65] = {0};
static bool s_scan_done = false;
static SemaphoreHandle_t s_mutex = NULL;

/* ── Reconnect / scan coordination ───────────────────────────────────────
 * Touched from the esp_event task, the esp_timer task and the UI task.
 * The mutex guards the fields it already guards; the policy flags below are
 * single-word volatiles whose worst-case race is one redundant retry, which
 * the driver rejects harmlessly. */
static volatile bool s_maintain_link = false;      /* auto-reconnect enabled  */
static volatile uint8_t s_retry_count = 0;         /* backoff table index     */
static volatile bool s_scan_active = false;        /* a scan is wanted/running*/
static volatile bool s_scan_pending = false;       /* waiting for abort event */
static volatile uint8_t s_scan_retry_count = 0;    /* scan-start attempts     */
static volatile int64_t s_auto_block_until_us = 0; /* reconnect suppression   */
static wifi_err_reason_t s_last_fail_reason = WIFI_REASON_UNSPECIFIED;

static esp_timer_handle_t s_reconnect_timer = NULL;
static esp_timer_handle_t s_scan_retry_timer = NULL;

/* Backoff between auto-reconnect attempts, indexed by s_retry_count; past the
 * table it repeats WIFI_MGR_RECONNECT_MAX_DELAY_MS. Long gaps keep the STA
 * idle (so user scans/connects are never blocked) while still rejoining a
 * returning AP within a minute. */
static const uint32_t RECONNECT_BACKOFF_MS[] = {
    300, 1500, 4000, 8000, 16000, 30000,
};
#define RECONNECT_BACKOFF_STEPS \
    (sizeof(RECONNECT_BACKOFF_MS) / sizeof(RECONNECT_BACKOFF_MS[0]))

static void _lock(void) {
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
    }
}

static void _unlock(void) {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/* ── Scan start, with abort-and-retry ──────────────────────────────────────
 * esp_wifi_scan_start() is rejected with ESP_ERR_WIFI_STATE whenever the STA
 * is mid-connect. This used to be fatal to the whole flow: the caller waited
 * for a SCAN_DONE that could never arrive. _try_start_scan() now treats that
 * error as "busy" — it aborts the offending connect and converges through
 * two independent paths: the abort's DISCONNECTED event re-kicks the scan
 * from a guaranteed-idle state, and a bounded esp_timer retry covers the case
 * where that event never fires. Any other error gives up by marking the scan
 * done, so no caller can hang on a dead scan. */
static void _try_start_scan(void);

static void _scan_retry_cb(void* arg) {
    (void)arg;
    if (!s_scan_active) return;
    _lock();
    bool done = s_scan_done;
    _unlock();
    if (done) return;
    _try_start_scan();
}

static void _try_start_scan(void) {
    wifi_scan_config_t cfg = {0};
    cfg.show_hidden = false;

    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err == ESP_OK) {
        s_scan_retry_count = 0;
        return;
    }

    if (err == ESP_ERR_WIFI_STATE && s_scan_retry_count < WIFI_MGR_SCAN_RETRY_MAX) {
        /* STA is mid-connect: abort it. The suppression window keeps the
         * abort's own DISCONNECTED event from instantly re-connecting.
         * Marking the scan pending lets that same event re-kick the scan the
         * moment the abort completes — the fastest convergence path — while
         * the timer below covers the case where the event never fires. */
        s_scan_retry_count++;
        s_scan_pending = true;
        ESP_LOGI(TAG, "scan busy (connect in flight), aborting + retry %u/%u",
                 s_scan_retry_count, WIFI_MGR_SCAN_RETRY_MAX);
        s_auto_block_until_us = esp_timer_get_time() +
                                (int64_t)WIFI_MGR_AUTO_BLOCK_MS * 1000LL;
        (void)esp_wifi_disconnect();
        if (s_scan_retry_timer) {
            (void)esp_timer_start_once(s_scan_retry_timer,
                                       (uint64_t)WIFI_MGR_SCAN_RETRY_MS * 1000ULL);
        }
        return;
    }

    /* Unrecoverable, or out of retries: release the UI rather than hang it. */
    ESP_LOGW(TAG, "scan start abandoned: %s", esp_err_to_name(err));
    s_scan_active = false;
    s_scan_pending = false;
    _lock();
    s_scan_done = true;
    _unlock();
}

/* ── Delayed auto-reconnect ────────────────────────────────────────────────
 * Runs from the esp_timer task; the disconnect event handler cannot sleep,
 * so backoff is expressed as a one-shot timer instead. */
static void _reconnect_cb(void* arg) {
    (void)arg;
    if (!s_maintain_link || s_scan_active || s_scan_pending) return;
    _lock();
    bool connected = s_sta_connected;
    _unlock();
    if (connected) return;
    ESP_LOGI(TAG, "auto-reconnect (backoff)");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "auto-reconnect: %s", esp_err_to_name(err));
    }
}

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
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* evt = (wifi_event_sta_disconnected_t*) event_data;
                _lock();
                s_sta_connected = false;
                strncpy(s_sta_ip, "0.0.0.0", sizeof(s_sta_ip) - 1);
                s_sta_ip[sizeof(s_sta_ip) - 1] = '\0';
                s_last_fail_reason = evt->reason;
                _unlock();
                ESP_LOGI(TAG, "STA Disconnected (reason %d)", evt->reason);

                /* A scan asked for this abort — the STA is idle now, so this
                 * is the one moment the scan is guaranteed to be accepted. */
                if (s_scan_pending) {
                    s_scan_pending = false;
                    _try_start_scan();
                    break;
                }
                if (s_scan_active) break;   /* mid-scan: never fight it */

                /* Bounded backoff auto-reconnect. The old code re-issued
                 * esp_wifi_connect() here unconditionally, which looped
                 * forever while the AP was absent and permanently kept the
                 * STA mid-connect — the direct cause of scans never
                 * starting and the UI spinner never ending. */
                if (!s_maintain_link) break;
                if (esp_timer_get_time() < s_auto_block_until_us) break;

                uint32_t idx = (s_retry_count < RECONNECT_BACKOFF_STEPS)
                             ? s_retry_count : (RECONNECT_BACKOFF_STEPS - 1);
                uint32_t delay_ms = (s_retry_count >= RECONNECT_BACKOFF_STEPS)
                                  ? WIFI_MGR_RECONNECT_MAX_DELAY_MS
                                  : RECONNECT_BACKOFF_MS[idx];
                s_retry_count++;
                ESP_LOGI(TAG, "auto-reconnect in %lu ms (attempt %u)",
                         (unsigned long)delay_ms, s_retry_count);
                if (s_reconnect_timer) {
                    (void)esp_timer_stop(s_reconnect_timer);
                    (void)esp_timer_start_once(s_reconnect_timer,
                                               (uint64_t)delay_ms * 1000ULL);
                }
                break;
            }
            case WIFI_EVENT_SCAN_DONE:
                _lock();
                s_scan_done = true;
                _unlock();
                s_scan_active = false;
                ESP_LOGI(TAG, "STA Scan Complete");
                /* If a backoff reconnect was paused by this scan and the link
                 * is still down, resume the cadence from here. */
                if (s_maintain_link && !s_sta_connected && s_reconnect_timer) {
                    (void)esp_timer_start_once(s_reconnect_timer,
                                               (uint64_t)WIFI_MGR_RECONNECT_MAX_DELAY_MS * 1000ULL);
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        _lock();
        esp_ip4addr_ntoa(&event->ip_info.ip, s_sta_ip, sizeof(s_sta_ip));
        s_sta_connected = true;
        _unlock();
        ESP_LOGI(TAG, "STA Got IP: %s", s_sta_ip);

        /* Link established: reset the backoff and cancel any pending retry. */
        s_retry_count = 0;
        if (s_reconnect_timer) (void)esp_timer_stop(s_reconnect_timer);

        // Save to NVS on successful connection
        nvs_handle_t h;
        if (nvs_open("wifi_creds", NVS_READWRITE, &h) == ESP_OK) {
            _lock();
            esp_err_t err1 = nvs_set_str(h, "ssid", s_sta_ssid);
            esp_err_t err2 = nvs_set_str(h, "pass", s_sta_pass);
            _unlock();
            if (err1 != ESP_OK) {
                ESP_LOGW(TAG, "NVS set ssid failed: %s", esp_err_to_name(err1));
            }
            if (err2 != ESP_OK) {
                ESP_LOGW(TAG, "NVS set pass failed: %s", esp_err_to_name(err2));
            }
            esp_err_t err3 = nvs_commit(h);
            if (err3 != ESP_OK) {
                ESP_LOGW(TAG, "NVS commit failed: %s", esp_err_to_name(err3));
            }
            nvs_close(h);
            ESP_LOGI(TAG, "STA Credentials saved to NVS");
        }
    }
}

void wifi_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing Unified Wi-Fi Manager (APSTA)");

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        configASSERT(s_mutex);
    }

    /* One-shot policy timers. Created once; started/stopped by the reconnect
     * logic. Safe from any task — callbacks only post driver commands. */
    if (!s_reconnect_timer) {
        const esp_timer_create_args_t t = {
            .callback = _reconnect_cb,
            .name = "wifi_reconn",
        };
        ESP_ERROR_CHECK(esp_timer_create(&t, &s_reconnect_timer));
    }
    if (!s_scan_retry_timer) {
        const esp_timer_create_args_t t = {
            .callback = _scan_retry_cb,
            .name = "wifi_scan_r",
        };
        ESP_ERROR_CHECK(esp_timer_create(&t, &s_scan_retry_timer));
    }

    // Initialize networking stack (idempotent)
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
    }

    // Create default netifs only if they don't exist
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == NULL) {
        esp_netif_t* ap = esp_netif_create_default_wifi_ap();
        if (!ap) ESP_LOGE(TAG, "Failed to create AP netif");
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_t* sta = esp_netif_create_default_wifi_sta();
        if (!sta) ESP_LOGE(TAG, "Failed to create STA netif");
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

    // Start Captive Portal DNS Server
    dns_server_start();

    // Auto-reconnect from NVS
    nvs_handle_t h;
    if (nvs_open("wifi_creds", NVS_READONLY, &h) == ESP_OK) {
        _lock();
        size_t s_len = sizeof(s_sta_ssid), p_len = sizeof(s_sta_pass);
        if (nvs_get_str(h, "ssid", s_sta_ssid, &s_len) == ESP_OK &&
            nvs_get_str(h, "pass", s_sta_pass, &p_len) == ESP_OK) {
            ESP_LOGI(TAG, "NVS: Found saved credentials for %s. Connecting...", s_sta_ssid);
            /* Restore counts as an established intent: it gets the same
             * bounded backoff as a dropped link, not the old infinite loop. */
            s_maintain_link = true;
            s_retry_count = 0;
            wifi_config_t sta_cfg = {0};
            strncpy((char*)sta_cfg.sta.ssid, s_sta_ssid, sizeof(sta_cfg.sta.ssid)-1);
            strncpy((char*)sta_cfg.sta.password, s_sta_pass, sizeof(sta_cfg.sta.password)-1);
            _unlock();
            esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "NVS restore set_config failed: %s", esp_err_to_name(err));
            }
            err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "NVS restore connect failed: %s", esp_err_to_name(err));
            }
        } else {
            _unlock();
        }
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Wi-Fi Unified initialized. Mode: APSTA");
}

void wifi_mgr_scan_start(void) {
    bool connected;
    _lock();
    s_scan_done = false;
    connected = s_sta_connected;
    _unlock();
    s_scan_active = true;
    s_scan_pending = false;
    s_scan_retry_count = 0;

    /* If the STA is disconnected but a backoff reconnect is pending, cancel
     * it: a scan and a connect attempt cannot coexist, and the connect would
     * win the radio and starve the scan. */
    if (!connected && s_reconnect_timer) {
        (void)esp_timer_stop(s_reconnect_timer);
    }

    _try_start_scan();
    /* If _try_start_scan hit ESP_ERR_WIFI_STATE it has aborted the in-flight
     * connect and armed the retry path; the scan will start either from the
     * abort's DISCONNECTED event or from the esp_timer fallback. Either way
     * the UI is guaranteed a SCAN_DONE (or a done-marker on give-up). */
}

bool wifi_mgr_is_scan_done(void) {
    _lock();
    bool done = s_scan_done;
    _unlock();
    return done;
}

int wifi_mgr_get_scan_results(wifi_scan_info_t* results, int max_results) {
    if (!results || max_results <= 0) return 0;

    uint16_t number = (uint16_t)(max_results < MAX_WIFI_NETWORKS ? max_results : MAX_WIFI_NETWORKS);
    /* WHY static: ap_info[MAX_WIFI_NETWORKS] is ~1KB of wifi_ap_record_t —
     * over the 512B stack budget (the old comment claiming it fit was wrong).
     * Sole caller today is the Core-1 UI WifiScanner component; the module
     * mutex serializes access defensively in case a second task ever calls. */
    static wifi_ap_record_t ap_info[MAX_WIFI_NETWORKS];
    _lock();
    if (esp_wifi_scan_get_ap_records(&number, ap_info) != ESP_OK) { _unlock(); return 0; }
    for (int i = 0; i < number; i++) {
        strncpy(results[i].ssid, (char*)ap_info[i].ssid, sizeof(results[i].ssid)-1);
        /* wifi_ap_record_t.ssid is 32 bytes and NOT guaranteed NUL-terminated;
         * a 32-character SSID would leave byte 32 of this 33-byte field
         * holding whatever the previous scan left there. */
        results[i].ssid[sizeof(results[i].ssid)-1] = '\0';
        results[i].rssi = ap_info[i].rssi;
        results[i].authmode = ap_info[i].authmode;
    }
    _unlock();
    return number;
}

void wifi_mgr_connect(const char* ssid, const char* password) {
    if (!ssid || ssid[0] == '\0') return;

    _lock();
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid)-1);
    s_sta_ssid[sizeof(s_sta_ssid)-1] = '\0';
    strncpy(s_sta_pass, password ? password : "", sizeof(s_sta_pass)-1);
    s_sta_pass[sizeof(s_sta_pass)-1] = '\0';
    s_last_fail_reason = WIFI_REASON_UNSPECIFIED;
    _unlock();

    /* Fresh user intent: this becomes the link we maintain. */
    s_maintain_link = true;
    s_retry_count = 0;
    if (s_reconnect_timer) (void)esp_timer_stop(s_reconnect_timer);
    if (s_scan_active) {
        s_scan_active = false;
        s_scan_pending = false;
    }

    /* Abort any in-flight auto-connect: esp_wifi_connect() returns
     * ESP_ERR_WIFI_STATE while one is running, which would silently drop the
     * user's request. The suppression window stops the abort's DISCONNECTED
     * event from racing back in with a reconnect of its own. */
    s_auto_block_until_us = esp_timer_get_time() +
                            (int64_t)WIFI_MGR_AUTO_BLOCK_MS * 1000LL;
    (void)esp_wifi_disconnect();

    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid)-1);
    if (password) {
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password)-1);
    }
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Connect set_config failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        /* Most likely the abort above is still settling in the driver. One
         * short deferred retry applies the same config a moment later from a
         * clean state, so the user's request cannot be lost to the race. */
        ESP_LOGW(TAG, "Connect busy (%s) — retrying shortly", esp_err_to_name(err));
        if (s_reconnect_timer) {
            (void)esp_timer_start_once(s_reconnect_timer, 400 * 1000ULL);
        }
    }
}

void wifi_mgr_disconnect(void) {
    /* User-initiated stop: kill the maintenance policy entirely so no backoff
     * timer resurrects the link behind their back. */
    s_maintain_link = false;
    if (s_reconnect_timer) (void)esp_timer_stop(s_reconnect_timer);
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Disconnect failed: %s", esp_err_to_name(err));
    }
}

wifi_err_reason_t wifi_mgr_get_last_fail_reason(void) {
    _lock();
    wifi_err_reason_t r = s_last_fail_reason;
    _unlock();
    return r;
}

bool wifi_mgr_is_connected(void) {
    _lock();
    bool connected = s_sta_connected;
    _unlock();
    return connected;
}

const char* wifi_mgr_get_connected_ssid(void) {
    static char s_copy[33];
    _lock();
    strncpy(s_copy, s_sta_ssid, sizeof(s_copy) - 1);
    s_copy[sizeof(s_copy) - 1] = '\0';
    _unlock();
    return s_copy;
}

const char* wifi_mgr_get_ip(void) {
    static char s_ip_copy[16];
    _lock();
    strncpy(s_ip_copy, s_sta_ip, sizeof(s_ip_copy) - 1);
    s_ip_copy[sizeof(s_ip_copy) - 1] = '\0';
    _unlock();
    return s_ip_copy;
}

int8_t wifi_mgr_get_rssi(void) {
    if (!s_sta_connected) return -100;
    wifi_ap_record_t ap_info = {};
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) return -100;
    return ap_info.rssi;
}
