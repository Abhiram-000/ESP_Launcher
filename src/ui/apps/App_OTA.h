/*
 * App_OTA.h — OTA Firmware Update TFT Application
 * =================================================
 * Purpose      : Renders a rich, animated OTA flashing UI on the ST7735S.
 * Responsibilities:
 *   - Polls the sys_mgr UI queue for OTA progress updates
 *   - Renders an animated liquid-fill progress tank
 *   - Displays transfer rate (KB/s), elapsed time, and phase label
 *   - Shows Wi-Fi AP + IP info for the user to connect
 * Dependencies : IApp, sys_mgr, theme_mgr
 */
#pragma once

#include "IApp.h"
#include "../../config/sys_config.h"
#include "../../system/sys_mgr.h"

class App_OTA : public IApp {
private:
    /* Progress state */
    float smooth_progress;
    float last_progress;
    float activity;          /* Wave amplitude driver [0..1] */
    float anim_time;
    float shockwave_r;

    /* Throughput & time */
    float    display_kbps;   /* Smoothed KB/s from UI message */
    uint32_t start_tick_ms;  /* millis() at OTA_STATE_FLASHING entry */
    uint32_t elapsed_ms;     /* Running elapsed time */

    /* Latest UI message from OTA service */
    ui_message_t local_msg;

    /* ---------- Rendering helpers ---------- */
    uint16_t blend_color(uint16_t fg, uint16_t bg, float alpha);

    void draw_liquid_tank(LGFX_Sprite* canvas,
                          int cx, int cy, int radius,
                          float progress, float time,
                          float wave_activity, uint16_t color_water);

    void draw_phase_dots(LGFX_Sprite* canvas, int cx, int bottom_y);
    void draw_stats_row(LGFX_Sprite* canvas, int cx, int y);

public:
    App_OTA();
    ~App_OTA() override;

    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
