/*
 * boot_gpio_hook.c — Custom Second-Stage Bootloader GPIO Override
 * ================================================================
 * Purpose: Check SELECT button (GPIO 16) at boot. If held, force ota_0
 *          by writing ota_data AND setting RTC flag.
 *
 * Root cause this fixes:
 *   Old code only set an RTC flag but never changed ota_data.
 *   Bootloader still booted ota_1, Launcher never ran, flag went unread.
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

#define BOOT_FORCE_LAUNCHER_MAGIC  0xA5B4C3D2U

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
 * Strategy: Erase sector 1 (invalidates it), write valid entry to sector 0.
 * The bootloader picks sector 0 (only valid entry) → boots ota_0 (Launcher).
 *
 * CRC is computed by bootloader_common_ota_select_crc() which CRC32s only
 * the ota_seq field (4 bytes) with initial value 0xFFFFFFFF.
 * Validation: ota_seq != UINT32_MAX && ota_state != INVALID/ABORTED && crc matches.
 */
static bool force_ota_0_boot(void) {
    esp_rom_spiflash_result_t res;

    ESP_LOGI(TAG, "force_ota_0_boot: erasing sector 1 at 0x%X", OTA_DATA_OFFSET + OTA_DATA_SECTOR_SIZE);

    /* Erase sector 1 (makes it invalid / all-0xFF) */
    uint32_t sector1 = (OTA_DATA_OFFSET + OTA_DATA_SECTOR_SIZE) / OTA_DATA_SECTOR_SIZE;
    res = esp_rom_spiflash_erase_sector(sector1);
    if (res != ESP_ROM_SPIFLASH_RESULT_OK) {
        ESP_LOGE(TAG, "erase_sector(%lu) FAILED: %d", (unsigned long)sector1, (int)res);
        return false;
    }
    ESP_LOGI(TAG, "sector 1 erased OK");

    /* Build valid ota_data entry pointing to ota_0 */
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

    /* Verify: read back sector 0 and check */
    esp_ota_select_entry_t verify;
    memset(&verify, 0xFF, sizeof(verify));
    res = esp_rom_spiflash_read(OTA_DATA_OFFSET, (uint32_t *)&verify, sizeof(verify));
    if (res != ESP_ROM_SPIFLASH_RESULT_OK) {
        ESP_LOGW(TAG, "spiflash_read verify FAILED: %d (non-fatal, write may still be OK)", (int)res);
        return true;
    }

    if (verify.ota_seq != entry.ota_seq) {
        ESP_LOGE(TAG, "VERIFY MISMATCH: ota_seq wrote %lu, read back %lu",
                 (unsigned long)entry.ota_seq, (unsigned long)verify.ota_seq);
        return false;
    }
    if (verify.ota_state != entry.ota_state) {
        ESP_LOGE(TAG, "VERIFY MISMATCH: ota_state wrote %lu, read back %lu",
                 (unsigned long)entry.ota_state, (unsigned long)verify.ota_state);
        return false;
    }
    if (verify.crc != entry.crc) {
        ESP_LOGE(TAG, "VERIFY MISMATCH: crc wrote 0x%08lX, read back 0x%08lX",
                 (unsigned long)entry.crc, (unsigned long)verify.crc);
        return false;
    }

    ESP_LOGI(TAG, "VERIFY OK: ota_data sector 0 confirmed pointing to ota_0");
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

    if (select_held) {
        /* 1. Set RTC flag for boot_ctrl.c */
        esp_partition_pos_t fake_part = {
            .offset = BOOT_FORCE_LAUNCHER_MAGIC,
            .size   = 0
        };
        bootloader_common_update_rtc_retain_mem(&fake_part, false);
        ESP_LOGI(TAG, "RTC flag set (magic=0x%08lX)", (unsigned long)BOOT_FORCE_LAUNCHER_MAGIC);

        /* 2. Force ota_0 as boot partition */
        ESP_LOGI(TAG, "Forcing ota_0 boot partition...");
        bool ok = force_ota_0_boot();
        if (ok) {
            ESP_LOGI(TAG, "SUCCESS: Boot will go to ota_0 (Launcher)");
        } else {
            ESP_LOGE(TAG, "FAILED: ota_data write failed — bootloader may still boot ota_1");
        }

    } else {
        /* Clear stale RTC flag */
        rtc_retain_mem_t *mem = bootloader_common_get_rtc_retain_mem();
        if (mem != NULL && mem->partition.offset == BOOT_FORCE_LAUNCHER_MAGIC) {
            ESP_LOGI(TAG, "Clearing stale RTC flag from previous boot");
            bootloader_common_reset_rtc_retain_mem();
        }
    }
}
