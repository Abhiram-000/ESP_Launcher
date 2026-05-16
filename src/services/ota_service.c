/*
 * ota_service.c — OTA Firmware Update Service Implementation
 * ===========================================================
 * Purpose      : Manages the full OTA flash-write lifecycle with binary
 *                validation, progress tracking, and throughput measurement.
 *
 * Compatible with .bin files from:
 *   - ESP-IDF  : idf.py build  → build/<project>.bin
 *   - PlatformIO: pio run       → .pio/build/<env>/<project>.bin
 *   - Arduino  : Sketch→Export → <sketch>.ino.bin
 *   All tools produce the same ESP32 image layout (magic byte 0xE9).
 */

#include "ota_service.h"
#include "../system/sys_mgr.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "OTA_SVC";

/* --------------------------------------------------------------------------
 * Internal state — all static (no heap allocation)
 * -------------------------------------------------------------------------- */

typedef struct {
    esp_ota_handle_t  handle;
    const esp_partition_t *partition;

    size_t   expected_size;     /* Content-Length from HTTP header        */
    size_t   total_written;     /* Running total of bytes flushed to flash */

    bool     header_validated;  /* True after first chunk's magic checked  */
    bool     active;            /* True between begin() and end()/abort()  */

    /* Throughput tracking (exponential moving average) */
    int64_t  last_tick_us;      /* esp_timer_get_time() at last chunk      */
    float    kbps_ema;          /* KB/s, EMA with α = 0.3                  */

    /* Public-facing state for /status endpoint */
    ota_state_t  state;
    char         status_msg[64];
} ota_ctx_t;

static ota_ctx_t s_ctx;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void _set_state(ota_state_t state, float progress, const char *msg) {
    s_ctx.state = state;
    strncpy(s_ctx.status_msg, msg, sizeof(s_ctx.status_msg) - 1);
    s_ctx.status_msg[sizeof(s_ctx.status_msg) - 1] = '\0';
    sys_mgr_send_ui_msg(state, progress, s_ctx.kbps_ema, msg);
}

static void _reset_ctx(void) {
    s_ctx.handle           = 0;
    s_ctx.partition        = NULL;
    s_ctx.expected_size    = 0;
    s_ctx.total_written    = 0;
    s_ctx.header_validated = false;
    s_ctx.active           = false;
    s_ctx.last_tick_us     = 0;
    s_ctx.kbps_ema         = 0.0f;
}

/* --------------------------------------------------------------------------
 * Lifecycle API
 * -------------------------------------------------------------------------- */

esp_err_t ota_service_begin(size_t image_size) {
    /* Reject obviously invalid sizes */
    if (image_size > 0 && image_size < APP_OTA_MIN_IMAGE_SIZE) {
        ESP_LOGE(TAG, "Image too small (%zu B) — likely not a firmware binary", image_size);
        _set_state(OTA_STATE_FAILED, 0.0f, "File too small!");
        return ESP_ERR_INVALID_SIZE;
    }
    if (image_size > APP_OTA_MAX_IMAGE_SIZE) {
        ESP_LOGE(TAG, "Image too large (%zu B) — exceeds partition size", image_size);
        _set_state(OTA_STATE_FAILED, 0.0f, "File too large!");
        return ESP_ERR_INVALID_SIZE;
    }

    _reset_ctx();
    s_ctx.expected_size = image_size;

    ESP_LOGI(TAG, "OTA begin — expected: %zu bytes", image_size);

    /* Log boot partition mismatch (informational only — common after OTA) */
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running    = esp_ota_get_running_partition();
    if (configured != running) {
        ESP_LOGW(TAG, "Boot partition (0x%08x) differs from running (0x%08x) — post-OTA normal",
                 (unsigned)configured->address, (unsigned)running->address);
    }
    ESP_LOGI(TAG, "Running: type=%d subtype=%d @ 0x%08x",
             running->type, running->subtype, (unsigned)running->address);

    /* Locate the next OTA slot */
    s_ctx.partition = esp_ota_get_next_update_partition(NULL);
    if (s_ctx.partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found — check partitions.csv");
        _set_state(OTA_STATE_FAILED, 0.0f, "No OTA partition!");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Target: subtype=%d @ 0x%08x (%.1f MB)",
             s_ctx.partition->subtype,
             (unsigned)s_ctx.partition->address,
             s_ctx.partition->size / (1024.0f * 1024.0f));

    _set_state(OTA_STATE_FLASHING, 0.0f, "Erasing flash...");

    esp_err_t err = esp_ota_begin(s_ctx.partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        _set_state(OTA_STATE_FAILED, 0.0f, "Erase failed!");
        _reset_ctx();
        return err;
    }

    s_ctx.active       = true;
    s_ctx.last_tick_us = esp_timer_get_time();
    _set_state(OTA_STATE_FLASHING, 0.0f, "Receiving...");
    return ESP_OK;
}

esp_err_t ota_service_write(const void *data, size_t length) {
    if (!s_ctx.active || s_ctx.handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* ---- Validate ESP32 binary magic on first chunk ---- */
    if (!s_ctx.header_validated) {
        if (length < 4) {
            ESP_LOGE(TAG, "First chunk too small (%zu B) to contain ESP32 image header", length);
            _set_state(OTA_STATE_FAILED, 0.0f, "Invalid binary!");
            return ESP_ERR_INVALID_ARG;
        }
        const uint8_t *hdr = (const uint8_t *)data;
        if (hdr[0] != APP_OTA_ESP32_MAGIC) {
            ESP_LOGE(TAG,
                     "Binary magic mismatch: got 0x%02X, expected 0x%02X.\n"
                     "  Make sure you upload the raw .bin (not .elf, .zip, or sketch folder).\n"
                     "  ESP-IDF: build/<proj>.bin | Arduino: Sketch→Export | PlatformIO: .pio/build/<env>/<proj>.bin",
                     hdr[0], APP_OTA_ESP32_MAGIC);
            _set_state(OTA_STATE_FAILED, 0.0f, "Not an ESP32 binary!");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Binary magic OK (0xE9). Chip type byte: 0x%02X Flash: 0x%02X", hdr[2], hdr[3]);
        s_ctx.header_validated = true;
    }

    /* ---- Write chunk to flash ---- */
    esp_err_t err = esp_ota_write(s_ctx.handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        _set_state(OTA_STATE_FAILED, 0.0f, "Flash write error!");
        return err;
    }
    s_ctx.total_written += length;

    /* ---- Throughput EMA update ---- */
    int64_t now_us    = esp_timer_get_time();
    int64_t delta_us  = now_us - s_ctx.last_tick_us;
    s_ctx.last_tick_us = now_us;
    if (delta_us > 0) {
        float chunk_kbps = ((float)length / 1024.0f) / ((float)delta_us / 1e6f);
        s_ctx.kbps_ema   = s_ctx.kbps_ema * 0.7f + chunk_kbps * 0.3f;
    }

    /* ---- Progress notification ---- */
    float progress = 0.0f;
    if (s_ctx.expected_size > 0) {
        progress = ((float)s_ctx.total_written / (float)s_ctx.expected_size) * 100.0f;
        if (progress > 100.0f) progress = 100.0f;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Flashing... %.0f%%", progress);
    sys_mgr_send_ui_msg(OTA_STATE_FLASHING, progress, s_ctx.kbps_ema, msg);

    return ESP_OK;
}

esp_err_t ota_service_end(void) {
    if (!s_ctx.active || s_ctx.handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Finalizing OTA — %zu of %zu bytes written",
             s_ctx.total_written, s_ctx.expected_size);
    _set_state(OTA_STATE_VALIDATING, 100.0f, "Validating image...");

    esp_err_t err = esp_ota_end(s_ctx.handle);
    s_ctx.handle = 0;   /* Prevent reuse regardless of outcome */

    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed — binary may be truncated or corrupt");
            _set_state(OTA_STATE_FAILED, 0.0f, "Image corrupt!");
        } else {
            ESP_LOGE(TAG, "esp_ota_end error: %s", esp_err_to_name(err));
            _set_state(OTA_STATE_FAILED, 0.0f, "Finalize error!");
        }
        _reset_ctx();
        return err;
    }

    err = esp_ota_set_boot_partition(s_ctx.partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition error: %s", esp_err_to_name(err));
        _set_state(OTA_STATE_FAILED, 0.0f, "Boot update failed!");
        _reset_ctx();
        return err;
    }

    ESP_LOGI(TAG, "OTA SUCCESS — new boot partition set. System will reboot.");
    _set_state(OTA_STATE_SUCCESS, 100.0f, "Update OK! Rebooting...");
    s_ctx.active = false;
    return ESP_OK;
}

void ota_service_abort(void) {
    if (s_ctx.handle != 0) {
        esp_ota_abort(s_ctx.handle);
        ESP_LOGW(TAG, "OTA aborted after %zu bytes written", s_ctx.total_written);
    }
    _set_state(OTA_STATE_FAILED, 0.0f, "Upload aborted");
    _reset_ctx();
}

/* --------------------------------------------------------------------------
 * Live state accessors (for /status JSON endpoint)
 * -------------------------------------------------------------------------- */

float ota_service_get_progress(void) {
    if (s_ctx.expected_size == 0) return 0.0f;
    float p = ((float)s_ctx.total_written / (float)s_ctx.expected_size) * 100.0f;
    return (p > 100.0f) ? 100.0f : p;
}

float ota_service_get_kbps(void) {
    return s_ctx.kbps_ema;
}

ota_state_t ota_service_get_state(void) {
    return s_ctx.state;
}

const char* ota_service_get_status_msg(void) {
    return s_ctx.status_msg;
}
