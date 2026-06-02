/*
 * boot_gpio_hook.c — Custom Second-Stage Bootloader GPIO Override
 * ================================================================
 * Purpose: ALWAYS force ota_0 as boot partition, so the Launcher always
 *          boots first and shows the 5-second splash screen.
 *
 *          Exception: If the Launcher just timed out and wants to boot
 *          ota_1, it sets a "boot ota_1" flag in RTC retain memory.
 *          The hook detects this flag and skips forcing ota_0.
 *
 * Boot flow:
 *   RESET → hook forces ota_0 → Launcher → 5s splash
 *     ├── SELECT held → stay in Launcher (ota_0)
 *     └── Timeout → Launcher sets "boot ota_1" flag → restarts → ota_1 boots
 *
 *   From ota_1, RESET → hook forces ota_0 → Launcher → splash again
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_spiflash.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "bootloader_common.h"
#include "esp_flash_partitions.h"

#define BOOT_SELECT_GPIO        16
#define BOOT_DEBOUNCE_SAMPLES   5
#define BOOT_SAMPLE_DELAY_US    10000
#define BOOT_SETTLE_US          5000

/*
 * RTC retain memory magic values.
 * Stored in rtc_retain_mem_t.partition.offset to communicate between
 * the bootloader hook and the application (boot_ctrl.c).
 *
 * BOOT_FORCE_LAUNCHER_MAGIC: Hook sets this when SELECT is held at reset.
 *   boot_ctrl.c reads it and stays in Launcher (no countdown skip).
 *
 * BOOT_ALLOW_OTA1_MAGIC: Launcher sets this before restarting to ota_1.
 *   Hook reads it and skips forcing ota_0, allowing ota_1 to boot.
 */
/* Mirror of sys_config.h — bootloader component can't include src/ headers */
#define BOOT_FORCE_LAUNCHER_MAGIC  0xA5B4C3D2U
#define BOOT_ALLOW_OTA1_MAGIC      0xB6C7D4E3U

/* ota_data lives at 0xD000 (from partitions.csv). Two 4KB sectors. */
#define OTA_DATA_OFFSET         0xD000
#define OTA_DATA_SECTOR_SIZE    0x1000

/* esp_ota_img_states_t value (ESP_OTA_IMG_VALID = 2) */
#define OTA_STATE_VALID         2

static const char *TAG = "BOOT_HOOK";

static inline int boot_gpio16_read(void) {
    return (REG_READ(GPIO_IN_REG) >> BOOT_SELECT_GPIO) & 0x1;
}

static void boot_gpio16_init_input_pullup(void) {
    uint32_t val = REG_READ(IO_MUX_GPIO16_REG);
    val &= ~(0x7U << 12);  /* Clear MCU_SEL [14:12] */
    val |=  (0x1U << 12);  /* MCU_SEL = 1 → GPIO function */
    val |= (1U << 8);      /* FUN_PU = 1 → pull-up enable */
    val &= ~(1U << 7);     /* FUN_PD = 0 → pull-down disable */
    val |= (1U << 9);      /* FUN_IE = 1 → input enable */
    REG_WRITE(IO_MUX_GPIO16_REG, val);
    REG_CLR_BIT(GPIO_ENABLE_REG, (1U << BOOT_SELECT_GPIO));
}

/*
 * force_ota_0_boot — Write ota_data to force booting ota_0.
 *
 * Erases sector 1 (invalidates it), writes valid entry to sector 0.
 * Bootloader picks sector 0 (only valid entry) → boots ota_0 (Launcher).
 */
static bool force_ota_0_boot(void) {
    esp_rom_spiflash_result_t res;

    ESP_LOGI(TAG, "force_ota_0_boot: erasing sector 1 at 0x%X", OTA_DATA_OFFSET + OTA_DATA_SECTOR_SIZE);

    uint32_t sector1 = (OTA_DATA_OFFSET + OTA_DATA_SECTOR_SIZE) / OTA_DATA_SECTOR_SIZE;
    res = esp_rom_spiflash_erase_sector(sector1);
    if (res != ESP_ROM_SPIFLASH_RESULT_OK) {
        ESP_LOGE(TAG, "erase_sector(%lu) FAILED: %d", (unsigned long)sector1, (int)res);
        return false;
    }
    ESP_LOGI(TAG, "sector 1 erased OK");

    esp_ota_select_entry_t entry;
    memset(&entry, 0xFF, sizeof(entry));
    entry.ota_seq   = 1;
    entry.ota_state = OTA_STATE_VALID;
    entry.crc       = bootloader_common_ota_select_crc(&entry);

    ESP_LOGI(TAG, "force_ota_0_boot: writing sector 0 at 0x%X (seq=%lu, state=%lu, crc=0x%08lX)",
             OTA_DATA_OFFSET,
             (unsigned long)entry.ota_seq,
             (unsigned long)entry.ota_state,
             (unsigned long)entry.crc);

    res = esp_rom_spiflash_write(OTA_DATA_OFFSET, (const uint32_t *)&entry, sizeof(entry));
    if (res != ESP_ROM_SPIFLASH_RESULT_OK) {
        ESP_LOGE(TAG, "spiflash_write(0x%X) FAILED: %d", OTA_DATA_OFFSET, (int)res);
        return false;
    }
    ESP_LOGI(TAG, "sector 0 written OK (%u bytes)", (unsigned)sizeof(entry));

    /* Verify: read back and compare */
    esp_ota_select_entry_t verify;
    memset(&verify, 0xFF, sizeof(verify));
    res = esp_rom_spiflash_read(OTA_DATA_OFFSET, (uint32_t *)&verify, sizeof(verify));
    if (res != ESP_ROM_SPIFLASH_RESULT_OK) {
        ESP_LOGW(TAG, "spiflash_read verify FAILED: %d (non-fatal)", (int)res);
        return true;
    }

    if (verify.ota_seq != entry.ota_seq || verify.ota_state != entry.ota_state || verify.crc != entry.crc) {
        ESP_LOGE(TAG, "VERIFY MISMATCH");
        return false;
    }

    ESP_LOGI(TAG, "VERIFY OK: ota_data confirmed pointing to ota_0");
    return true;
}

void bootloader_after_init(void) {
    boot_gpio16_init_input_pullup();
    esp_rom_delay_us(BOOT_SETTLE_US);

    ESP_LOGI(TAG, "GPIO16 level after settle: %d (0=LOW/pressed)", boot_gpio16_read());

    int low_count = 0;
    for (int i = 0; i < BOOT_DEBOUNCE_SAMPLES; i++) {
        if (boot_gpio16_read() == 0) {
            low_count++;
        }
        esp_rom_delay_us(BOOT_SAMPLE_DELAY_US);
    }

    bool select_held = (low_count >= BOOT_DEBOUNCE_SAMPLES);
    ESP_LOGI(TAG, "Debounce: %d/%d LOW samples → SELECT %s",
             low_count, BOOT_DEBOUNCE_SAMPLES, select_held ? "HELD" : "not held");

    /*
     * Check if the Launcher set the "boot ota_1" flag.
     * This happens when the 5-second splash times out and the Launcher
     * wants to boot the installed firmware on ota_1.
     *
     * If the flag is set: clear it and return WITHOUT forcing ota_0.
     * The bootloader will read the existing ota_data (pointing to ota_1)
     * and boot the installed firmware.
     */
    rtc_retain_mem_t *mem = bootloader_common_get_rtc_retain_mem();
    if (mem != NULL && mem->partition.offset == BOOT_ALLOW_OTA1_MAGIC) {
        ESP_LOGI(TAG, "BOOT_ALLOW_OTA1 flag detected — clearing and letting ota_1 boot");
        bootloader_common_reset_rtc_retain_mem();
        return;
    }

    /*
     * Normal boot: ALWAYS force ota_0 (Launcher).
     * This ensures the Launcher always boots first and shows the
     * 5-second splash screen, giving the user a chance to decide.
     */
    ESP_LOGI(TAG, "Forcing ota_0 boot partition (splash screen will show)...");
    bool ok = force_ota_0_boot();
    if (ok) {
        ESP_LOGI(TAG, "SUCCESS: Boot will go to ota_0 (Launcher)");
    } else {
        ESP_LOGE(TAG, "FAILED: ota_data write failed — bootloader may still boot ota_1");
    }

    /*
     * If SELECT is held, set the "force launcher" flag for boot_ctrl.c.
     * This tells boot_ctrl.c to stay in Launcher immediately (skip countdown).
     */
    if (select_held) {
        esp_partition_pos_t fake_part = {
            .offset = BOOT_FORCE_LAUNCHER_MAGIC,
            .size   = 0
        };
        bootloader_common_update_rtc_retain_mem(&fake_part, false);
        ESP_LOGI(TAG, "RTC flag set (SELECT held) — Launcher will stay without countdown");
    }
}
