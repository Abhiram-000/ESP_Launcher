/**
 * @file App_AICode.h
 * @brief Professional Wi-Fi/AI Code utility for ESP32 Launcher.
 */
#pragma once

#include "IApp.h"
#include "../services/wifi_mgr.h"
#include <string>
#include <vector>
#include <stdint.h>

class App_AICode : public IApp {
public:
    App_AICode();
    virtual ~App_AICode();

    void init() override;
    void exit() override;
    void loop() override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

private:
    enum State {
        STATE_CHECK,
        STATE_SCANNING,
        STATE_SCAN_RESULTS,
        STATE_PASSWORD,
        STATE_CONNECTING,
        STATE_CONNECTED,
        STATE_ERROR
    };

    State current_state;
    float anim_time;
    
    // Scanner
    std::vector<wifi_scan_info_t> networks;
    int scroll_idx;
    int selected_network_idx;
    
    // Keyboard
    std::string password;
    int kb_row;
    int kb_col;
    bool visible_pass;
    bool caps_lock;
    
    // Connection
    std::string target_ssid;
    uint32_t conn_start_ms;
    
    void draw_header(LGFX_Sprite* canvas, const char* title);
    void draw_keyboard(LGFX_Sprite* canvas);
    void draw_scanner(LGFX_Sprite* canvas);
    void draw_status_check(LGFX_Sprite* canvas);
    void draw_connecting(LGFX_Sprite* canvas);
    void draw_connected(LGFX_Sprite* canvas);
    void draw_error(LGFX_Sprite* canvas);
    
    uint16_t blend_color(uint16_t fg, uint16_t bg, float alpha);
};
