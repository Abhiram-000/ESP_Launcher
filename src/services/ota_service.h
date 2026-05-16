/*
 * ota_service.h — OTA Firmware Update Service Interface
 * ======================================================
 * Purpose      : Exposes a clean C API for the HTTP layer to drive OTA flashing.
 * Responsibilities:
 *   - Validate incoming ESP32 binary headers before touching flash
 *   - Stream data to the OTA partition in chunks
 *   - Track progress, throughput (KB/s), and elapsed time
 *   - Expose live state for the /status polling endpoint
 * Dependencies : ESP-IDF esp_ota_ops, sys_mgr (UI notifications)
 *
 * Compatible with binaries produced by:
 *   - ESP-IDF (pio/idf.py build)
 *   - Arduino IDE (Sketch > Export Compiled Binary)
 *   - PlatformIO (pio run)
 *   All produce the same ESP32 image format (magic = 0xE9).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "../config/sys_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

/**
 * @brief  Begin an OTA update session.
 *
 * Locates the next OTA partition, begins erasing it, and resets all
 * internal counters.  Must be called before any ota_service_write() calls.
 *
 * @param  image_size  Total expected image size in bytes.
 *                     Pass 0 if unknown (disables % progress tracking).
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t ota_service_begin(size_t image_size);

/**
 * @brief  Write a chunk of firmware data to the OTA partition.
 *
 * On the very first call the function validates the ESP32 binary magic byte
 * (0xE9) and rejects non-firmware data immediately, before writing anything
 * to flash. This catches accidental uploads of zip files, ELF binaries, etc.
 *
 * @param  data    Pointer to chunk buffer.
 * @param  length  Byte count of this chunk.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (bad magic), or flash write error.
 */
esp_err_t ota_service_write(const void *data, size_t length);

/**
 * @brief  Finalize the OTA update.
 *
 * Validates the written image via ESP-IDF's internal SHA256 check, then
 * marks the new partition as the boot target.  After this returns ESP_OK
 * the caller should trigger a reboot.
 *
 * @return ESP_OK on success.
 */
esp_err_t ota_service_end(void);

/**
 * @brief  Abort an in-progress OTA update without touching the boot partition.
 *
 * Safe to call even if ota_service_begin() was never called or already ended.
 */
void ota_service_abort(void);

/* --------------------------------------------------------------------------
 * Live State (for /status polling endpoint)
 * -------------------------------------------------------------------------- */

/**
 * @brief  Returns the current OTA progress (0–100).
 *         Returns 0 if no update is in progress or image_size was 0.
 */
float ota_service_get_progress(void);

/**
 * @brief  Returns the current transfer rate in KB/s (exponential moving avg).
 */
float ota_service_get_kbps(void);

/**
 * @brief  Returns the current OTA state for JSON serialisation.
 */
ota_state_t ota_service_get_state(void);

/**
 * @brief  Returns a human-readable status string for the current state.
 */
const char* ota_service_get_status_msg(void);

#ifdef __cplusplus
}
#endif
