/*
 * boot_ctrl.c — Application-Level Boot Decision Controller
 * =========================================================
 * Purpose      : Determines whether to stay in Launcher (ota_0) or switch
 *                to user firmware (ota_1) immediately on every Launcher boot.
 *
 * Boot flow:
 *   0. Check RTC flag set by bootloader hook (SELECT held at reset).
 *      If set — clear flag, stay in Launcher unconditionally.
 *   1. Confirm we are running from ota_0. If not, return immediately.
 *   2. NVS boot-loop guard: if crash-rebooted > 3x, force Launcher.
 *   3. Check if ota_1 holds a valid, flashed image. If not, return.
 *   4. Init display and draw countdown splash for 4 seconds.
 *   5. Poll SELECT button (GPIO 16) every 10 ms.
 *      → Pressed:   return (Launcher starts normally).
 *      → Timeout:   esp_ota_set_boot_partition(ota_1) → esp_restart().
 *
 * RTC Flag:    boot_gpio_hook (bootloader) sets s_boot_force_launcher_flag
 *              in RTC_NOINIT memory when SELECT is held at reset.
 *              We share the same extern symbol — the linker resolves both
 *              to the same RTC_NOINIT_ATTR variable.
 *
 * Safety:
 *   - If display init fails, the countdown runs blind (GPIO still polled).
 *   - boot_ctrl_mark_valid() must be called after Launcher fully boots.
 *   - Works with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y.
 *
 * Dependencies : esp_ota_ops, nvs_flash, nvs, driver/gpio, LGFX (LovyanGFX)
 */

#include "boot_ctrl.h"
#include "../config/sys_config.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/* Include LGFX inside a C++ guard block because LovyanGFX is C++ */
#ifdef __cplusplus
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "../../include/LGFX_Config.h"
#endif

static const char *TAG = "BOOT_CTRL";

/* ---- Configuration ---- */
#include "../platform/board.h"
#define BC_SELECT_GPIO        LAUNCHER_PIN_SELECT /* Must match board.h */
#define BC_SPLASH_DURATION_MS  4000   /* How long to show the splash screen        */
#define BC_POLL_INTERVAL_MS    10     /* GPIO poll granularity                     */
#define BC_DEBOUNCE_MS         50     /* Required consecutive LOW time for confirm */
#define BC_BOOT_LOOP_MAX       3      /* Max boot-loop count before forcing Launcher */
#define BC_LAUNCHER_PREF_SKIP  2     /* After this many consecutive SELECT presses, skip countdown */
#define BC_NVS_NAMESPACE       "boot_ctrl"
#define BC_NVS_KEY_BOOTCNT     "boot_cnt"
#define BC_NVS_KEY_PREFCNT     "pref_cnt"  /* Consecutive launcher-preference counter */

/*
 * Shared RTC_NOINIT variable with the bootloader hook.
 * The bootloader sets this to BOOT_FORCE_LAUNCHER_MAGIC when SELECT is held
 * at reset. We clear it after reading to prevent stale triggers.
 * Must remain as RTC_NOINIT_ATTR so linker places it in RTC slow memory,
 * which survives normal and watchdog resets.
 */
#define BOOT_FORCE_LAUNCHER_MAGIC  0xA5B4C3D2U
RTC_NOINIT_ATTR uint32_t s_boot_force_launcher_flag;

/* ---- Internal helpers ---- */

/*
 * Validate that ota_1 contains a properly flashed and valid image.
 * Uses esp_ota_get_state_partition() as recommended — more reliable than
 * get_partition_description() which can't detect partial writes.
 */
static bool user_fw_is_valid(void) {
    const esp_partition_t *ota1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (ota1 == NULL) {
        ESP_LOGW(TAG, "ota_1 partition not found in table");
        return false;
    }

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(ota1, &state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot read ota_1 state: %s — assuming invalid", esp_err_to_name(err));
        return false;
    }

    switch (state) {
        case ESP_OTA_IMG_VALID:
        case ESP_OTA_IMG_PENDING_VERIFY:
        case ESP_OTA_IMG_NEW:
            ESP_LOGI(TAG, "ota_1 state: %d (valid for boot)", state);
            return true;
        case ESP_OTA_IMG_INVALID:
        case ESP_OTA_IMG_ABORTED:
        case ESP_OTA_IMG_UNDEFINED:
        default:
            ESP_LOGW(TAG, "ota_1 state: %d (not suitable for boot)", state);
            return false;
    }
}

/* ---- NVS boot-loop guard ---- */

static uint8_t get_and_increment_boot_count(void) {
    nvs_handle_t h;
    uint8_t cnt = 0;
    if (nvs_open(BC_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u8(h, BC_NVS_KEY_BOOTCNT, &cnt);
        cnt++;
        nvs_set_u8(h, BC_NVS_KEY_BOOTCNT, cnt);
        nvs_commit(h);
        nvs_close(h);
    }
    return cnt;
}

static void reset_boot_count(void) {
    nvs_handle_t h;
    if (nvs_open(BC_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, BC_NVS_KEY_BOOTCNT, 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- Launcher preference counter (tracks consecutive "stay in Launcher" choices) ---- */

/*
 * Returns the CURRENT value before incrementing.
 * On return value >= BC_LAUNCHER_PREF_SKIP, caller should skip the countdown.
 */
static uint8_t get_launcher_pref_count(void) {
    nvs_handle_t h;
    uint8_t cnt = 0;
    if (nvs_open(BC_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, BC_NVS_KEY_PREFCNT, &cnt);
        nvs_close(h);
    }
    return cnt;
}

static void increment_launcher_pref_count(void) {
    nvs_handle_t h;
    uint8_t cnt = 0;
    if (nvs_open(BC_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u8(h, BC_NVS_KEY_PREFCNT, &cnt);
        if (cnt < 255) cnt++;
        nvs_set_u8(h, BC_NVS_KEY_PREFCNT, cnt);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Launcher pref count now: %d (skip threshold: %d)", cnt, BC_LAUNCHER_PREF_SKIP);
}

static void reset_launcher_pref_count(void) {
    nvs_handle_t h;
    if (nvs_open(BC_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, BC_NVS_KEY_PREFCNT, 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Launcher pref count reset (user firmware launched)");
}

/* ---- GPIO (SELECT button) ---- */

static void gpio_select_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << BC_SELECT_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/* Returns true when a debounced LOW is confirmed */
static bool select_button_pressed(void) {
    if (gpio_get_level((gpio_num_t)BC_SELECT_GPIO) != 0) return false;
    /* Held LOW — verify for debounce window */
    int samples = BC_DEBOUNCE_MS / BC_POLL_INTERVAL_MS;
    for (int i = 0; i < samples; i++) {
        vTaskDelay(pdMS_TO_TICKS(BC_POLL_INTERVAL_MS));
        if (gpio_get_level((gpio_num_t)BC_SELECT_GPIO) != 0) return false; /* Noise — reject */
    }
    return true;
}

/* ---- Display splash helpers (C++ context required for LGFX) ---- */

#ifdef __cplusplus

static LGFX_ESP32S3_ST7735 *s_tft = nullptr;
static LGFX_Sprite          *s_canvas = nullptr;
static bool s_display_ok = false;

static bool display_init_for_splash(void) {
    s_tft    = new LGFX_ESP32S3_ST7735();
    s_canvas = new LGFX_Sprite(s_tft);

    s_tft->init();
    s_tft->setRotation(1);
    s_tft->setBrightness(200);

    s_canvas->setPsram(true); /* Allocate in 8MB PSRAM to save internal memory */
    s_canvas->createSprite(s_tft->width(), s_tft->height() + 4);
    s_canvas->setColorDepth(16);
    return true;
}

static void display_deinit_splash(void) {
    if (s_canvas) { s_canvas->deleteSprite(); delete s_canvas; s_canvas = nullptr; }
    if (s_tft)    { delete s_tft; s_tft = nullptr; }
}

static void draw_splash(float elapsed_ms, float total_ms) {
    if (!s_tft || !s_canvas) return;

    const int W  = s_canvas->width();
    const int H  = s_canvas->height();
    const int cx = W / 2;
    const int cy = H / 2 - 6;

    float frac   = elapsed_ms / total_ms;        /* 0.0 → 1.0 */
    float remain = (total_ms - elapsed_ms) / 1000.0f;  /* seconds left */

    /* ---- Background ---- */
    s_canvas->fillSprite(s_canvas->color565(4, 6, 16));  /* Deep navy */

    /* ---- Dot-grid wallpaper ---- */
    uint16_t dot_col = s_canvas->color565(12, 18, 40);
    for (int y = 8; y < H; y += 16)
        for (int x = 8; x < W; x += 16)
            s_canvas->drawPixel(x, y, dot_col);

    /* ---- Countdown arc (shrinks clockwise, starts full) ---- */
    float arc_deg   = 360.0f * (1.0f - frac); /* Full arc → 0 */
    uint16_t arc_col = (arc_deg > 60.0f) ? s_canvas->color565(0, 190, 255) :
                       (arc_deg > 20.0f) ? s_canvas->color565(255, 180, 0) :
                                           s_canvas->color565(255, 60, 60);

    s_canvas->drawArc(cx, cy, 28, 24, -90, -90 + (int)arc_deg, arc_col);
    s_canvas->drawArc(cx, cy, 23, 22, -90, -90 + (int)arc_deg,
        s_canvas->color565(0, 80, 120)); /* Inner dim trail */

    /* ---- Center logo ---- */
    s_canvas->setFont(&fonts::Font2);
    s_canvas->setTextDatum(textdatum_t::middle_center);
    s_canvas->setTextColor(s_canvas->color565(0, 190, 255));
    s_canvas->drawString("ESP32", cx, cy);

    /* ---- Seconds remaining ---- */
    s_canvas->setFont(&fonts::Font0);
    s_canvas->setTextColor(s_canvas->color565(180, 180, 180));
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1fs", remain > 0.0f ? remain : 0.0f);
    s_canvas->drawString(buf, cx, cy + 20);

    /* ---- Instructions ---- */
    s_canvas->setTextColor(s_canvas->color565(80, 100, 140));
    s_canvas->setTextDatum(textdatum_t::top_center);
    s_canvas->drawString("Hold SELECT", cx, 4);
    s_canvas->drawString("to stay in Launcher", cx, 14);

    s_canvas->setTextDatum(textdatum_t::bottom_center);
    s_canvas->setTextColor(s_canvas->color565(50, 70, 100));
    s_canvas->drawString("Release = User FW", cx, H - 2);

    s_canvas->pushSprite(0, -4);
}

#endif /* __cplusplus */

/* ---- Public API ---- */

void boot_ctrl_check(void) {
    /*
     * Step 0: Check if the bootloader GPIO hook detected SELECT held at reset.
     * If so, the user explicitly wants the Launcher — return immediately.
     * This is the hardware return path: User FW → (SELECT+RESET) → Launcher.
     */
    if (s_boot_force_launcher_flag == BOOT_FORCE_LAUNCHER_MAGIC) {
        s_boot_force_launcher_flag = 0; /* Consume the flag — prevent stale trigger */
        ESP_LOGI(TAG, "RTC flag set by bootloader hook — SELECT was held at reset, forcing Launcher");
        reset_boot_count();
        return;
    }
    s_boot_force_launcher_flag = 0; /* Clear any garbage value from power-on */

    /* Step 1: Confirm we are in Launcher (ota_0). */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        ESP_LOGI(TAG, "Not running from ota_0 — boot_ctrl not applicable");
        return;
    }
    ESP_LOGI(TAG, "Running from ota_0 (Launcher) — boot_ctrl active");

    /* Step 2: NVS boot-loop guard — must be done BEFORE NVS subsystem is used elsewhere. */
    uint8_t boot_count = get_and_increment_boot_count();
    ESP_LOGI(TAG, "Launcher boot count (raw): %d", boot_count);

    if (boot_count > BC_BOOT_LOOP_MAX) {
        ESP_LOGW(TAG, "Boot loop detected (count=%d > %d) — forcing Launcher, skipping FW switch",
                 boot_count, BC_BOOT_LOOP_MAX);
        reset_boot_count();
        return; /* Stay in Launcher regardless */
    }

    /* Step 3: Validate ota_1. */
    if (!user_fw_is_valid()) {
        ESP_LOGI(TAG, "No valid user firmware in ota_1 — staying in Launcher");
        reset_boot_count();
        return;
    }

    /* Step 4: Check launcher preference history.
     * If the user has chosen Launcher >= BC_LAUNCHER_PREF_SKIP times in a row,
     * skip the 4-second countdown and stay in Launcher immediately.
     */
    uint8_t pref_cnt = get_launcher_pref_count();
    if (pref_cnt >= BC_LAUNCHER_PREF_SKIP) {
        ESP_LOGI(TAG, "Launcher preferred %d times in a row — skipping countdown, staying in Launcher", pref_cnt);
        reset_boot_count();
        increment_launcher_pref_count(); /* Keep incrementing so it stays in skip mode */
        return;
    }
    ESP_LOGI(TAG, "Launcher pref count: %d / %d before skip kicks in", pref_cnt, BC_LAUNCHER_PREF_SKIP);

    /* Step 5: Configure SELECT GPIO. */
    gpio_select_init();

    /* Step 6: Init display (best-effort — don't block if it fails). */
#ifdef __cplusplus
    s_display_ok = display_init_for_splash();
    if (!s_display_ok) {
        ESP_LOGW(TAG, "Display init failed — running blind countdown");
    }
#endif

    /* Step 7: 4-second countdown loop. */
    ESP_LOGI(TAG, "Boot splash — waiting %d ms for SELECT", BC_SPLASH_DURATION_MS);

    int64_t start_us       = esp_timer_get_time();
    int64_t duration_us    = (int64_t)BC_SPLASH_DURATION_MS * 1000;
    bool    select_held    = false;

    while (true) {
        int64_t elapsed_us  = esp_timer_get_time() - start_us;
        float elapsed_ms = (float)elapsed_us / 1000.0f;

#ifdef __cplusplus
        if (s_display_ok) {
            draw_splash(elapsed_ms, (float)BC_SPLASH_DURATION_MS);
        }
#endif
        if (select_button_pressed()) {
            select_held = true;
            ESP_LOGI(TAG, "SELECT pressed — staying in Launcher");
            break;
        }

        if (elapsed_us >= duration_us) {
            ESP_LOGI(TAG, "Timeout — switching to user firmware (ota_1)");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(BC_POLL_INTERVAL_MS));
    }

#ifdef __cplusplus
    display_deinit_splash();
#endif

    if (select_held) {
        reset_boot_count();
        increment_launcher_pref_count(); /* User chose Launcher again — track it */
        return; /* Continue Launcher startup normally */
    }

    /* Step 7: Switch boot target to ota_1 and reboot. */
    const esp_partition_t *ota1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

    if (ota1 == NULL) {
        ESP_LOGE(TAG, "ota_1 vanished — cannot switch, staying in Launcher");
        reset_boot_count();
        return;
    }

    esp_err_t err = esp_ota_set_boot_partition(ota1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s — staying in Launcher",
                 esp_err_to_name(err));
        reset_boot_count();
        return;
    }

    ESP_LOGI(TAG, "Boot redirected to ota_1. Restarting now...");
    reset_launcher_pref_count(); /* User is going to user firmware — reset preference streak */
    vTaskDelay(pdMS_TO_TICKS(100)); /* Let log flush */
    esp_restart();
    /* Unreachable */
}

void boot_ctrl_mark_valid(void) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Launcher marked as valid (rollback cancelled)");
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        /* Rollback not enabled — this is fine */
    } else {
        ESP_LOGW(TAG, "mark_app_valid error: %s", esp_err_to_name(err));
    }
    reset_boot_count();  /* Successful boot — reset loop guard */
}
