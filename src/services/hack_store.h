#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HACK_STORE_MAX_FIRMS 10

typedef enum {
    HACK_STORE_ERR_OK = 0,
    HACK_STORE_ERR_NO_WIFI,
    HACK_STORE_ERR_NETWORK,
    HACK_STORE_ERR_BAD_METADATA,
    HACK_STORE_ERR_PARTITION,
    HACK_STORE_ERR_FLASHING,
    HACK_STORE_ERR_ABORTED
} hack_store_err_t;

typedef struct {
    char id[32];
    char name[32];
    char version[16];
    char description[128];
    uint32_t size_bytes;
    char url[256];
} hack_entry_t;

typedef struct {
    bool is_busy;
    hack_store_err_t last_error;
    uint8_t progress_pct;
    size_t bytes_done;
    size_t bytes_total;
    char active_id[32];
} hack_store_status_t;

/**
 * @brief Initialize the Hack Store service.
 * Loads the currently installed firmware ID from NVS.
 */
hack_store_err_t hack_store_init(void);

/**
 * @brief Fetch the catalog from GitHub and parse it.
 * This is blocking.
 */
hack_store_err_t hack_store_refresh_catalog(void);

/**
 * @brief Get the parsed catalog.
 */
int hack_store_get_catalog(hack_entry_t* out, int max_count);

/**
 * @brief Get the ID of the currently installed hacking firmware in ota_1.
 */
bool hack_store_get_installed_id(char* id_out, size_t len);

/**
 * @brief Download and flash the selected firmware to ota_1.
 * @param id ID of the firmware in the catalog.
 * @return HACK_STORE_ERR_OK on success, after which the system will reboot.
 */
hack_store_err_t hack_store_install(const char* id);

/**
 * @brief Cancel an in-progress download/flash cooperatively.
 */
hack_store_err_t hack_store_abort(void);

/**
 * @brief Get live status of the service (for UI).
 */
void hack_store_get_status(hack_store_status_t* out);

#ifdef __cplusplus
}
#endif
