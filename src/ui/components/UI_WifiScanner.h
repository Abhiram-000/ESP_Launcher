#pragma once
#include <stdint.h>
#include <functional>
#include "../../include/LGFX_Config.h"
#include "../../managers/input_mgr.h"
#include "../../services/wifi_mgr.h"
#include "../../config/sys_config.h"
#include "UI_Keyboard.h"

class UI_WifiScanner {
public:
    enum class State {
        Idle,
        Scanning,
        List,
        Password,
        Connecting,
        Failed,     /* connect attempt ended with a specific cause */
        Done
    };

    UI_WifiScanner();
    ~UI_WifiScanner();

    void begin(std::function<void(bool)> on_complete);
    void handleInput(input_event_t ev);
    void render(LGFX_Sprite* canvas);
    void loop(); // Needs to be called periodically to check async scan/connect status

    bool isActive() const { return _active; }

private:
    bool _active;
    State _state;
    float _anim_time;
    float _spinner_angle;
    
    wifi_scan_info_t _networks[MAX_WIFI_NETWORKS];
    int _network_count;
    int _list_sel;
    int _list_scroll;

    char _selected_ssid[33];
    char _password[64];
    bool _connect_success;
    uint32_t _connect_start_ms;
    uint32_t _scan_start_ms;      /* guards the scan against a lost event */
    const char* _fail_reason;     /* human text for the Failed screen     */

    UI_Keyboard _keyboard;
    std::function<void(bool)> _on_complete;

    void _draw_header(LGFX_Sprite* c, const char* title, uint16_t accent);
    void _draw_spinner(LGFX_Sprite* c, int cx, int cy, int r);
};
