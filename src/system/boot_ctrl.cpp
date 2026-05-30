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
#include "bootloader_common.h"

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
#define BC_LAUNCHER_PREF_SKIP  255     /* After this many consecutive SELECT presses, skip countdown */
#define BC_NVS_NAMESPACE       "boot_ctrl"
#define BC_NVS_KEY_BOOTCNT     "boot_cnt"
#define BC_NVS_KEY_PREFCNT     "pref_cnt"  /* Consecutive launcher-preference counter */

/*
 * Bootloader–App communication via RTC retain memory.
 * The bootloader's GPIO hook stores BOOT_FORCE_LAUNCHER_MAGIC in
 * rtc_retain_mem.partition.offset when SELECT is held at reset.
 * We read it here and clear it after consuming.
 */
#define BOOT_FORCE_LAUNCHER_MAGIC  0xA5B4C3D2U

/*
 * "Boot ota_1" flag. Set by Launcher before restarting to ota_1.
 * The bootloader hook reads this flag and skips forcing ota_0,
 * allowing ota_1 to boot. This is how the 5-second splash screen
 * hands off to the installed firmware after timeout.
 */
#define BOOT_ALLOW_OTA1_MAGIC      0xB6C7D4E3U

/* ---- Internal helpers ---- */

/*
 * Validate that ota_1 contains a properly flashed and valid image.
 *
 * First tries esp_ota_get_state_partition() (preferred method).
 * If that fails (e.g. state was erased by bootloader hook), falls back
 * to reading the first byte and checking for ESP32 image magic (0xE9).
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
    if (err == ESP_OK) {
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

    ESP_LOGW(TAG, "Cannot read ota_1 state: %s — checking image directly", esp_err_to_name(err));

    uint8_t magic = 0;
    if (esp_partition_read(ota1, 0, &magic, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Cannot read ota_1 first byte — assuming invalid");
        return false;
    }

    if (magic == 0xE9) {
        ESP_LOGI(TAG, "ota_1 has valid ESP32 image (magic=0x%02X) — allowing boot", magic);
        return true;
    }

    ESP_LOGW(TAG, "ota_1 first byte: 0x%02X (expected 0xE9) — no valid image", magic);
    return false;
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

    const int W  = s_canvas->width();   /* 160 */
    const int H  = s_canvas->height();  /* 128 + 4 overshoot = 132 actual */
    const int cx = W / 2;              /* 80 */
    const int cy = 46;                 /* logo centre — upper-mid area */

    float frac   = elapsed_ms / total_ms;           /* 0.0 → 1.0 */
    float remain = (total_ms - elapsed_ms) / 1000.0f; /* seconds left */
    if (frac > 1.0f) frac = 1.0f;
    if (remain < 0.0f) remain = 0.0f;

    /* ─── Background ───────────────────────────────────────────────────── */
    s_canvas->fillSprite(s_canvas->color565(3, 4, 12));  /* Near-black navy */

    /* Subtle dot-grid wallpaper */
    const uint16_t dot_col = s_canvas->color565(10, 14, 32);
    for (int y = 8; y < H; y += 14)
        for (int x = 6; x < W; x += 14)
            s_canvas->drawPixel(x, y, dot_col);

    /* ─── Progress bar colours (cyan → amber → red as time runs out) ────── */
    uint16_t bar_col;
    if (frac < 0.60f)       bar_col = s_canvas->color565(0,   200, 255);  /* cyan  */
    else if (frac < 0.85f)  bar_col = s_canvas->color565(255, 180,   0);  /* amber */
    else                    bar_col = s_canvas->color565(255,  50,  40);  /* red   */

    /* ─── Neon progress bar ─────────────────────────────────────────────── */
    const int BAR_X = 12;
    const int BAR_Y = 100;
    const int BAR_W = W - 24;   /* 136 px wide */
    const int BAR_H = 7;
    const int fill  = (int)(frac * (float)(BAR_W - 2));

    /* Outer glow (1 px larger each side, low alpha via dimmed colour) */
    s_canvas->drawRect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2,
                       s_canvas->color565(0, 60, 90));
    /* Track */
    s_canvas->drawRect(BAR_X, BAR_Y, BAR_W, BAR_H,
                       s_canvas->color565(20, 30, 60));
    /* Fill */
    if (fill > 0) {
        s_canvas->fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, bar_col);

        /* Bright leading-edge pixel for a 'hot tip' effect */
        s_canvas->drawFastVLine(BAR_X + fill, BAR_Y + 1, BAR_H - 2,
                                s_canvas->color565(255, 255, 255));
    }

    /* ─── Particle sparks at the leading edge ───────────────────────────── */
    /* Uses a deterministic PRNG seeded on elapsed_ms bucket so sparks are
     * stable within a 50 ms window but change across frames.              */
    if (fill > 2) {
        const int spark_x0 = BAR_X + fill;
        const int spark_y0 = BAR_Y + BAR_H / 2;
        unsigned seed = (unsigned)(elapsed_ms / 50.0f);  /* changes every 50 ms */

        for (int s = 0; s < 6; s++) {
            /* lcg-style mini PRNG */
            seed = seed * 1664525u + 1013904223u;
            int dx = (int)((seed & 0x0F)) - 4;          /* -4 … +11 to the right */
            seed = seed * 1664525u + 1013904223u;
            int dy = (int)((seed & 0x07)) - 3;          /* -3 … +3 vertical */
            /* Bias sparks to fly right and slightly up */
            dx = (dx < 0) ? -dx / 2 : dx + 2;
            int px = spark_x0 + dx;
            int py = spark_y0 + dy;
            if (px >= 0 && px < W && py >= 0 && py < H) {
                /* Alternate between full bright and half-bright */
                uint16_t sc = (s & 1) ? bar_col : s_canvas->color565(255, 255, 255);
                s_canvas->drawPixel(px, py, sc);
            }
        }
    }

    /* ─── Logo: pulsing glow rings + inner arc spinner ─────────────────── */
    const float ring_r = 22.0f + 1.2f * sinf(elapsed_ms * 0.006f);
    const int   ri     = (int)ring_r;

    /* Outer soft glow */
    s_canvas->drawCircle(cx, cy, ri + 3, s_canvas->color565(0, 40, 70));
    s_canvas->drawCircle(cx, cy, ri + 2, s_canvas->color565(0, 80, 120));
    /* Main ring */
    s_canvas->drawCircle(cx, cy, ri,     bar_col);
    s_canvas->drawCircle(cx, cy, ri - 1, s_canvas->color565(0, 100, 150));

    /* Rotating spinner arcs (two opposing segments) */
    float spin = fmodf(elapsed_ms * 0.18f, 360.0f);
    s_canvas->drawArc(cx, cy, ri - 3, ri - 5,
                      (int)spin,       (int)spin + 100, bar_col);
    s_canvas->drawArc(cx, cy, ri - 3, ri - 5,
                      (int)spin + 180, (int)spin + 280, bar_col);

    /* Centre fill pulse */
    uint8_t core_b = (uint8_t)(80 + 60 * sinf(elapsed_ms * 0.008f));
    s_canvas->fillCircle(cx, cy, ri - 6, s_canvas->color565(0, core_b / 4, core_b));

    /* ─── Logo text ─────────────────────────────────────────────────────── */
    s_canvas->setFont(&fonts::Font2);
    s_canvas->setTextDatum(textdatum_t::middle_center);
    s_canvas->setTextColor(s_canvas->color565(220, 240, 255));
    s_canvas->drawString("ESP32", cx, cy);

    /* ─── Title & subtitle ──────────────────────────────────────────────── */
    s_canvas->setFont(&fonts::Font0);
    s_canvas->setTextDatum(textdatum_t::top_center);
    s_canvas->setTextColor(s_canvas->color565(0, 200, 255));
    s_canvas->drawString("LAUNCHER", cx, 5);

    s_canvas->setTextColor(s_canvas->color565(50, 80, 120));
    s_canvas->drawString("Custom Modular OS", cx, 16);

    /* ─── Countdown label above bar ─────────────────────────────────────── */
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1fs", remain);
    s_canvas->setTextDatum(textdatum_t::middle_right);
    s_canvas->setTextColor(bar_col);
    s_canvas->drawString(buf, BAR_X + BAR_W, BAR_Y - 5);

    /* ─── Footer hints ──────────────────────────────────────────────────── */
    s_canvas->setFont(&fonts::Font0);
    s_canvas->setTextDatum(textdatum_t::bottom_center);
    s_canvas->setTextColor(s_canvas->color565(0, 120, 180));
    s_canvas->drawString("SELECT = Stay in Launcher", cx, H - 5);

    s_canvas->pushSprite(0, -4);
}

#endif /* __cplusplus */

/* ---- Public API ---- */

void boot_ctrl_check(void) {
    /*
     * Step 0: Check if the bootloader GPIO hook detected SELECT held at reset.
     * The bootloader stores BOOT_FORCE_LAUNCHER_MAGIC in rtc_retain_mem.partition.offset.
     * If so, the user explicitly wants the Launcher — return immediately.
     * This is the hardware return path: User FW → (SELECT+RESET) → Launcher.
     */
    rtc_retain_mem_t *rtc_mem = bootloader_common_get_rtc_retain_mem();
    if (rtc_mem != NULL && rtc_mem->partition.offset == BOOT_FORCE_LAUNCHER_MAGIC) {
        bootloader_common_reset_rtc_retain_mem(); /* Consume the flag */
        ESP_LOGI(TAG, "RTC flag set by bootloader hook — SELECT was held at reset, forcing Launcher");
        reset_boot_count();
        return;
    }
    /* Ensure retain mem is clean for subsequent boots */
    if (rtc_mem != NULL) {
        bootloader_common_reset_rtc_retain_mem();
    }

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
    reset_launcher_pref_count();

    /*
     * Set "boot ota_1" flag in RTC retain memory before restarting.
     * The bootloader hook will detect this flag and skip forcing ota_0,
     * allowing ota_1 to boot. Without this flag, the hook would always
     * force ota_0 and the installed firmware would never boot.
     */
    esp_partition_pos_t allow_ota1 = {
        .offset = BOOT_ALLOW_OTA1_MAGIC,
        .size   = 0
    };
    bootloader_common_update_rtc_retain_mem(&allow_ota1, false);
    ESP_LOGI(TAG, "RTC flag set (BOOT_ALLOW_OTA1) — hook will let ota_1 boot");

    vTaskDelay(pdMS_TO_TICKS(100));
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
