/*
 * App_OTA.cpp — OTA Firmware Update TFT Application
 * ===================================================
 * Renders a rich animated OTA screen on the ST7735S (128×160, landscape):
 *   - Animated liquid-fill tank shows flash progress
 *   - Phase dot indicator (Idle → Connected → Flashing → Validating → Done)
 *   - KB/s data rate + elapsed time display during transfers
 *   - Dynamic Wi-Fi AP + IP info panel
 *   - Shockwave ring effect on success/fail
 */

#include "App_OTA.h"
#include "../theme_mgr.h"
#include "esp_log.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern void switch_to_grid();

/* Hardcoded background for the liquid tank — matches the Cyberpunk theme */
#define COLOR_BG  0x0821u

App_OTA::App_OTA()
    : smooth_progress(0), last_progress(0), activity(0),
      anim_time(0), shockwave_r(0),
      display_kbps(0), start_tick_ms(0), elapsed_ms(0) {
    local_msg.state    = OTA_STATE_IDLE;
    local_msg.progress = 0.0f;
    local_msg.kbps     = 0.0f;
    local_msg.msg[0]   = '\0';
}

App_OTA::~App_OTA() {}

void App_OTA::init() {
    smooth_progress = 0;
    last_progress   = 0;
    activity        = 0;
    shockwave_r     = 0;
    display_kbps    = 0;
    start_tick_ms   = 0;
    elapsed_ms      = 0;
    anim_time       = 0;
    local_msg.state    = OTA_STATE_IDLE;
    local_msg.progress = 0.0f;
    local_msg.kbps     = 0.0f;
    snprintf(local_msg.msg, sizeof(local_msg.msg), "Awaiting Upload");
}

void App_OTA::exit() {}

void App_OTA::loop() {
    /* Drain queue — take newest message (non-blocking) */
    QueueHandle_t q = sys_mgr_get_ui_queue();
    ui_message_t tmp;
    while (xQueueReceive(q, &tmp, 0) == pdTRUE) {
        local_msg = tmp;
        if (local_msg.state == OTA_STATE_FLASHING && start_tick_ms == 0) {
            start_tick_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        if (local_msg.state == OTA_STATE_SUCCESS ||
            local_msg.state == OTA_STATE_FAILED) {
            shockwave_r  = 0.0f;
        }
    }

    /* Smooth progress interpolation */
    if (local_msg.progress == 0 && smooth_progress > 90.0f) {
        smooth_progress = 0.0f;
        last_progress   = 0.0f;
    } else {
        smooth_progress += (local_msg.progress - smooth_progress) * 0.14f;
    }

    /* Wave activity driver */
    float velocity = fmaxf(0.0f, smooth_progress - last_progress);
    last_progress = smooth_progress;
    switch (local_msg.state) {
        case OTA_STATE_FLASHING:   activity += (velocity * 3.0f - activity) * 0.3f; break;
        case OTA_STATE_IDLE:       activity = 0.10f; break;
        case OTA_STATE_CONNECTED:  activity = 0.55f; break;
        case OTA_STATE_VALIDATING: activity += (0.8f - activity) * 0.1f; break;
        default:                   activity *= 0.92f; break;
    }

    /* Smoothed KB/s */
    display_kbps += (local_msg.kbps - display_kbps) * 0.25f;

    /* Elapsed time */
    if (local_msg.state == OTA_STATE_FLASHING && start_tick_ms > 0) {
        elapsed_ms = xTaskGetTickCount() * portTICK_PERIOD_MS - start_tick_ms;
    }

    anim_time += 0.05f;
}

void App_OTA::handleInput(input_event_t event) {
    if (event == BTN_LEFT && local_msg.state == OTA_STATE_IDLE) {
        switch_to_grid();
    }
}

/* --------------------------------------------------------------------------
 * Color blending
 * -------------------------------------------------------------------------- */
uint16_t App_OTA::blend_color(uint16_t fg, uint16_t bg, float alpha) {
    if (alpha <= 0.0f) return bg;
    if (alpha >= 1.0f) return fg;
    uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
    uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
    return ((uint16_t)(r2 + (r1 - r2) * alpha) << 11) |
           ((uint16_t)(g2 + (g1 - g2) * alpha) << 5) |
           (uint16_t)(b2 + (b1 - b2) * alpha);
}

/* --------------------------------------------------------------------------
 * Animated liquid tank
 * -------------------------------------------------------------------------- */
void App_OTA::draw_liquid_tank(LGFX_Sprite* canvas,
                                int cx, int cy, int radius,
                                float progress, float time,
                                float wave_activity, uint16_t color_water) {
    canvas->fillCircle(cx, cy, radius, canvas->color565(10, 14, 22));

    float target_y = (float)radius - (2.0f * (float)radius * (progress / 100.0f));

    for (int x = -radius; x <= radius; x++) {
        int y_bound = (int)sqrtf((float)(radius * radius - x * x));
        float amp   = 1.0f + 6.0f * wave_activity;
        float wy    = target_y
                    + amp * sinf(time * 3.5f + x * 0.18f)
                    + (amp * 0.45f) * sinf(time * 5.2f - x * 0.28f);
        int sy = (int)wy;
        if (sy < -y_bound) sy = -y_bound;
        if (sy >  y_bound) continue;

        /* Water column */
        canvas->drawFastVLine(cx + x, cy + sy, y_bound - sy + 1, color_water);

        /* Surface highlight */
        canvas->drawPixel(cx + x, cy + sy,
            blend_color(canvas->color565(220, 240, 255), color_water, 0.75f));
    }

    /* Outer bezel rings */
    uint16_t bezel_dark  = canvas->color565(10, 16, 26);
    uint16_t bezel_ring  = canvas->color565(30, 45, 65);
    uint16_t bezel_shine = canvas->color565(80, 120, 160);
    canvas->drawArc(cx, cy, radius + 4, radius + 1, 0, 360, bezel_ring);
    canvas->drawArc(cx, cy, radius + 5, radius + 4, 0, 360, bezel_dark);
    /* Specular highlight — top-left arc */
    canvas->drawArc(cx, cy, radius + 3, radius + 2, 200, 260, bezel_shine);
}

/* --------------------------------------------------------------------------
 * 5-phase dot indicator below the tank
 * -------------------------------------------------------------------------- */
void App_OTA::draw_phase_dots(LGFX_Sprite* canvas, int cx, int bottom_y) {
    struct { ota_state_t st; const char* lbl; } phases[] = {
        {OTA_STATE_IDLE,       "WAIT"},
        {OTA_STATE_CONNECTED,  "CONN"},
        {OTA_STATE_FLASHING,   "FLASH"},
        {OTA_STATE_VALIDATING, "VRFY"},
        {OTA_STATE_SUCCESS,    "DONE"},
    };
    const int N = 5;
    const int dot_r = 3, spacing = 20;
    int sx = cx - (N - 1) * spacing / 2;

    for (int i = 0; i < N; i++) {
        int dx = sx + i * spacing;
        bool is_current = (local_msg.state == phases[i].st) ||
                          (i == N - 1 && local_msg.state == OTA_STATE_FAILED);
        bool is_past = (int)local_msg.state > (int)phases[i].st &&
                       local_msg.state != OTA_STATE_FAILED;

        uint16_t col;
        if (is_current && local_msg.state == OTA_STATE_FAILED) {
            col = theme_get_fail();
        } else if (is_current) {
            /* Animated active dot */
            float alpha = 0.5f + 0.5f * sinf(anim_time * 4.0f);
            col = blend_color(theme_get_accent(),
                              canvas->color565(15, 25, 40), alpha);
        } else if (is_past) {
            col = theme_get_success();
        } else {
            col = canvas->color565(20, 30, 45);
        }

        canvas->fillCircle(dx, bottom_y - 6, dot_r, col);

        /* Connector line between dots */
        if (i < N - 1) {
            uint16_t lc = is_past ? theme_get_success()
                                  : canvas->color565(18, 28, 42);
            canvas->drawFastHLine(dx + dot_r + 1, bottom_y - 6,
                                  spacing - 2 * dot_r - 2, lc);
        }
    }
}

/* --------------------------------------------------------------------------
 * Stats row: KB/s and elapsed time
 * -------------------------------------------------------------------------- */
void App_OTA::draw_stats_row(LGFX_Sprite* canvas, int cx, int y) {
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_left);

    if (local_msg.state == OTA_STATE_FLASHING ||
        local_msg.state == OTA_STATE_VALIDATING) {

        char buf[24];
        canvas->setTextColor(theme_get_text_dim());

        /* KB/s */
        if (display_kbps > 0.5f) {
            snprintf(buf, sizeof(buf), "%.0fKB/s", display_kbps);
            canvas->drawString(buf, 4, y);
        }

        /* Elapsed time */
        uint32_t sec = elapsed_ms / 1000;
        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(sec / 60),
                 (unsigned long)(sec % 60));
        canvas->setTextDatum(textdatum_t::middle_right);
        canvas->drawString(buf, canvas->width() - 4, y);
    }
}

/* --------------------------------------------------------------------------
 * Main render
 * -------------------------------------------------------------------------- */
void App_OTA::render(LGFX_Sprite* canvas) {
    const int W  = canvas->width();
    const int H  = canvas->height();
    const int cx = W / 2;

    uint16_t dynamic_bg = theme_get_bg();
    canvas->fillSprite(dynamic_bg);

    /* ---- Header ---- */
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("OTA FIRMWARE UPDATE", cx, 3);

    /* ---- Determine tank level & colour per state ---- */
    uint16_t state_color    = theme_get_accent();
    float    display_level  = 12.0f;

    switch (local_msg.state) {
        case OTA_STATE_IDLE:
            state_color   = theme_get_text_dim();
            display_level = 12.0f + 3.0f * sinf(anim_time * 1.2f);
            break;
        case OTA_STATE_CONNECTED:
            state_color   = canvas->color565(160, 60, 255);
            display_level = 28.0f + 6.0f * sinf(anim_time * 2.5f);
            break;
        case OTA_STATE_FLASHING:
        case OTA_STATE_VALIDATING:
            state_color   = theme_get_accent();
            display_level = smooth_progress;
            break;
        case OTA_STATE_SUCCESS:
            state_color   = theme_get_success();
            display_level = 100.0f;
            break;
        case OTA_STATE_FAILED:
            state_color   = theme_get_fail();
            display_level = smooth_progress;
            break;
        default:
            break;
    }

    /* ---- Tank (centre of screen, offset up to leave room below) ---- */
    const int tank_cy = H / 2 - 8;
    draw_liquid_tank(canvas, cx, tank_cy, 30, display_level,
                     anim_time, activity, state_color);

    /* ---- Percent label inside tank ---- */
    if (local_msg.state == OTA_STATE_FLASHING ||
        local_msg.state == OTA_STATE_VALIDATING) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", (int)smooth_progress);
        canvas->setFont(&fonts::Font2);
        uint16_t txt_col = (display_level > 50.0f)
                            ? canvas->color565(0, 0, 0)
                            : state_color;
        canvas->setTextDatum(textdatum_t::middle_center);
        /* Subtle shadow */
        canvas->setTextColor(dynamic_bg);
        canvas->drawString(pct, cx + 1, tank_cy + 1);
        canvas->setTextColor(txt_col);
        canvas->drawString(pct, cx, tank_cy);
    }

    /* ---- Shockwave (success / fail) ---- */
    if (local_msg.state == OTA_STATE_SUCCESS ||
        local_msg.state == OTA_STATE_FAILED) {
        shockwave_r += 2.8f;
        if (shockwave_r < 100.0f) {
            float a = 1.0f - (shockwave_r / 100.0f);
            canvas->drawCircle(cx, tank_cy, (int)shockwave_r,
                               blend_color(state_color, dynamic_bg, a));
            canvas->drawCircle(cx, tank_cy, (int)(shockwave_r - 2),
                               blend_color(state_color, dynamic_bg, a * 0.35f));
        }
        /* State label */
        canvas->setFont(&fonts::Font2);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setTextColor(state_color);
        canvas->drawString(
            local_msg.state == OTA_STATE_SUCCESS ? "DONE" : "ERR",
            cx, tank_cy);
    }

    /* ---- Phase dot indicator ---- */
    draw_phase_dots(canvas, cx, H - 30);

    /* ---- Stats row (KB/s + elapsed) ---- */
    draw_stats_row(canvas, cx, H - 22);

    /* ---- Status message ---- */
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(state_color);
    canvas->drawString(local_msg.msg, cx, H - 12);

    /* ---- Network info footer ---- */
    if (local_msg.state == OTA_STATE_IDLE ||
        local_msg.state == OTA_STATE_CONNECTED) {
        canvas->setTextColor(theme_get_text_dim());
        char ap_str[28];
        snprintf(ap_str, sizeof(ap_str), "AP: %.20s", APP_WIFI_SSID);
        canvas->drawString(ap_str, cx, H - 4);
    } else if (local_msg.state != OTA_STATE_FLASHING) {
        canvas->setTextColor(theme_get_success());
        canvas->drawString("192.168.4.1", cx, H - 4);
    }
}
