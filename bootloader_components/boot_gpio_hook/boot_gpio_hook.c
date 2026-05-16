/*
 * boot_gpio_hook.c — Custom Second-Stage Bootloader GPIO Override
 * ================================================================
 * Purpose     : Check SELECT button (GPIO 16) immediately after hardware init.
 *               If held at reset, write a flag into RTC NOINIT memory.
 *               The application-layer boot_ctrl.c reads this flag and forces
 *               the boot to stay in Launcher (ota_0), overriding ota_data.
 *
 * Mechanism   : ESP-IDF 'bootloader_after_init' hook — called after basic
 *               hardware is configured but BEFORE partition selection.
 *               Only bare-metal register access and esp_rom_ APIs are safe.
 *               No FreeRTOS, no heap, no high-level GPIO driver.
 *
 * RTC Memory  : We store the flag in RTC_NOINIT memory which survives ALL
 *               resets (power glitch would naturally release the button).
 *               boot_ctrl.c clears the flag immediately after reading it.
 *
 * GPIO 16     : Uses IO_MUX_GPIO16_REG directly — the array GPIO_PIN_MUX_REG[]
 *               is NOT available in the ESP32-S3 bootloader environment.
 *               Active-LOW (button pulls to GND, internal pull-up enabled).
 *
 * Dependencies: soc (GPIO_IN_REG, IO_MUX_GPIO16_REG), esp_rom, esp_cpu
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/rtc_cntl_reg.h"

/* ---- Configuration ---- */
#define BOOT_SELECT_GPIO        16       /* Must match input_mgr.c and boot_ctrl.c */
#define BOOT_DEBOUNCE_SAMPLES   5        /* 5 × 10ms = 50ms debounce window        */
#define BOOT_SAMPLE_DELAY_US    10000   /* 10ms between each sample               */
#define BOOT_SETTLE_US          5000    /* 5ms settle after GPIO init             */

/*
 * Magic flag value written to RTC memory.
 * Chosen to be different from 0x00 (unset) and 0xFF (flash erase default).
 */
#define BOOT_FORCE_LAUNCHER_MAGIC  0xA5B4C3D2U

/*
 * RTC NOINIT memory — survives resets, cleared by power-off.
 * Placed in RTC_SLOW_MEM section; the linker script supports this in ESP-IDF.
 * The variable name must be unique to avoid collision with app symbols.
 */
static RTC_NOINIT_ATTR uint32_t s_boot_force_launcher_flag;

static const char *TAG = "BOOT_HOOK";

/*
 * Read GPIO 16 level using direct register access.
 * GPIO 16 is in the lower 32 GPIO bank (GPIO_IN_REG covers GPIO 0-31).
 */
static inline int boot_gpio16_read(void) {
    return (REG_READ(GPIO_IN_REG) >> BOOT_SELECT_GPIO) & 0x1;
}

/*
 * Configure GPIO 16 as input with internal pull-up.
 * IO_MUX_GPIO16_REG is the ESP32-S3 specific per-pad register for GPIO 16.
 *
 * We use the hardcoded register rather than GPIO_PIN_MUX_REG[pin] because
 * that array is a driver-layer construct not available in the bootloader.
 */
static void boot_gpio16_init_input_pullup(void) {
    /* Select GPIO function (MCU_SEL = 1 for GPIO matrix on ESP32-S3) */
    uint32_t val = REG_READ(IO_MUX_GPIO16_REG);
    /* Clear MCU_SEL bits [14:12], set to 1 (GPIO function) */
    val &= ~(0x7U << 12);
    val |=  (0x1U << 12);
    /* Enable pull-up (bit 8 = FUN_PU) */
    val |= (1U << 8);
    /* Disable pull-down (bit 9 = FUN_PD) */
    val &= ~(1U << 9);
    /* Disable input filter / enable input (bit 7 = IE) */
    val |= (1U << 7);
    REG_WRITE(IO_MUX_GPIO16_REG, val);

    /* Set GPIO 16 direction to INPUT by clearing its bit in GPIO_ENABLE_REG */
    REG_CLR_BIT(GPIO_ENABLE_REG, (1U << BOOT_SELECT_GPIO));
}

/*
 * bootloader_after_init — Official ESP-IDF weak hook.
 *
 * Called after bootloader has initialised clocks and flash SPI interface,
 * but BEFORE partition table is read and BEFORE boot partition selection.
 *
 * If SELECT is held, sets the RTC magic flag that boot_ctrl.c will read in
 * app_main to force Launcher regardless of ota_data content.
 */
void bootloader_after_init(void) {
    boot_gpio16_init_input_pullup();

    /* Allow the pin to settle (RC constant of pull-up + trace capacitance) */
    esp_rom_delay_us(BOOT_SETTLE_US);

    /* Debounce: require BOOT_DEBOUNCE_SAMPLES consecutive LOW samples */
    int low_count = 0;
    for (int i = 0; i < BOOT_DEBOUNCE_SAMPLES; i++) {
        if (boot_gpio16_read() == 0) {
            low_count++;
        }
        esp_rom_delay_us(BOOT_SAMPLE_DELAY_US);
    }

    bool select_held = (low_count >= BOOT_DEBOUNCE_SAMPLES);

    if (select_held) {
        /*
         * Write magic to RTC memory. This flag survives the reset but is
         * cleared by boot_ctrl_check() in app_main immediately after reading.
         *
         * boot_ctrl.c will see this flag and stay in Launcher regardless
         * of what ota_data says — this is the hardware return path from
         * user firmware back to Launcher.
         */
        s_boot_force_launcher_flag = BOOT_FORCE_LAUNCHER_MAGIC;
        ESP_LOGI(TAG, "SELECT held at reset — RTC flag set, Launcher will be forced");
    } else {
        /*
         * Clear stale flags from previous cycles.
         * Only set on a fresh deliberate press — not a watchdog reboot.
         */
        if (s_boot_force_launcher_flag == BOOT_FORCE_LAUNCHER_MAGIC) {
            /*
             * If it was set from a PREVIOUS boot and we got here without
             * the button pressed, it means the previous boot already consumed
             * it (or the app forgot to clear it). Clear it to be safe.
             */
            s_boot_force_launcher_flag = 0;
        }
    }
}
