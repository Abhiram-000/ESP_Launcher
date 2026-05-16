/*
 * boot_ctrl.h — Application-Level Boot Decision Controller
 * =========================================================
 * Purpose      : Run as the FIRST thing in app_main when in Launcher (ota_0).
 *                Displays a 4-second splash with countdown arc, polls SELECT button.
 *                On timeout → validates and switches to ota_1 (user firmware).
 *
 * This file uses ONLY C-compatible declarations even though the .c
 * implementation uses C++ internally for LovyanGFX rendering.
 *
 * Dependencies : esp_ota_ops, nvs, driver/gpio, LovyanGFX (internal).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Execute boot decision logic.
 *
 * MUST be called AFTER sys_mgr_init() (which initialises NVS) and before
 * any other subsystem. This is a blocking call for up to 4 seconds.
 *
 * It is always safe to call — it returns immediately if:
 *   - Not running on ota_0 (Launcher partition)
 *   - No valid image exists on ota_1
 *   - Boot-loop counter exceeds threshold
 */
void boot_ctrl_check(void);

/**
 * @brief Mark current partition (Launcher) as valid and reset boot-loop counter.
 *
 * Call AFTER all subsystems have started successfully.
 * Required for CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE safety.
 * Safe to call even if rollback is disabled.
 */
void boot_ctrl_mark_valid(void);

#ifdef __cplusplus
}
#endif
