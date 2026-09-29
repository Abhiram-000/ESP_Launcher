#pragma once

#include "IApp.h"
#include <stdint.h>
#include <stdbool.h>

class App_Torch : public IApp {
private:
    /* ── State machine ────────────────────────────────────────────── */
    enum State : uint8_t {
        STATE_MAIN = 0,
        STATE_MODES,
        STATE_BRIGHTNESS,
        STATE_CUSTOM_LIST,
        STATE_CUSTOM_EDIT,
        STATE_ADVANCED
    };

    /* ── Built-in light modes (12) ────────────────────────────────── */
    enum Mode : uint8_t {
        MODE_WHITE = 0,
        MODE_RED_BEACON,
        MODE_GREEN_BEACON,
        MODE_BLUE_BEACON,
        MODE_RAINBOW,
        MODE_CYBER_PULSE,
        MODE_SOS,
        MODE_APEX_SIGNAL,
        MODE_STROBE,
        MODE_POLICE,
        MODE_CANDLE,
        MODE_DISCO,
        MODE_COUNT
    };

    /* ── Custom mode data ─────────────────────────────────────────── */
    struct custom_step_t {
        uint8_t  r;
        uint8_t  g;
        uint8_t  b;
        uint16_t duration_ms;   /* 100–5000 ms */
    };

    struct custom_mode_t {
        char             name[8];         /* auto-generated 3-letter */
        custom_step_t    steps[8];
        uint8_t          step_count;      /* 1–8 */
        bool             repeat;          /* true = loop */
        bool             active;          /* false = empty slot */
    };

    /* ── Advanced settings ────────────────────────────────────────── */
    struct torch_settings_t {
        uint8_t  max_brightness;    /* 128–255, caps LED ceiling */
        uint32_t auto_off_ms;       /* 0 = disabled; else preset */
        bool     strobe_safe;       /* true = limit strobe ≤10 Hz */
    };

    /* ── Random 3-letter name pool ────────────────────────────────── */
    static const char* RANDOM_NAMES[32];

    /* ── Core state ───────────────────────────────────────────────── */
    State           _state;
    State           _prev_state;
    float           _anim_time;

    /* ── Main screen ──────────────────────────────────────────────── */
    Mode            _current_mode;
    bool            _led_on;
    float           _pulse_size;

    /* ── Mode selector ────────────────────────────────────────────── */
    int32_t         _mode_cursor;
    int32_t         _mode_scroll;

    /* ── Brightness ───────────────────────────────────────────────── */
    uint8_t         _brightness;        /* 0–255 */
    int32_t         _brightness_preset; /* -1=custom, 0=25%, 1=50%, 2=75%, 3=100% */

    /* ── Custom modes ─────────────────────────────────────────────── */
    custom_mode_t   _custom_modes[4];
    int32_t         _custom_cursor;     /* slot index in list */
    int32_t         _custom_edit_slot;  /* slot being edited */
    int32_t         _step_cursor;       /* step index in editor */
    int32_t         _edit_component;    /* 0=R, 1=G, 2=B, 3=Duration, 4=Repeat */

    /* ── Advanced settings ────────────────────────────────────────── */
    torch_settings_t _settings;
    int32_t          _adv_cursor;

    /* ── NVS persistence ──────────────────────────────────────────── */
    void _save_settings();
    void _load_settings();
    void _save_custom_modes();
    void _load_custom_modes();

    /* ── Helpers ──────────────────────────────────────────────────── */
    void     _update_led();
    void     _hsv_to_rgb(float h, float s, float v,
                         uint8_t& r, uint8_t& g, uint8_t& b);
    uint16_t _get_mode_color(Mode m, float t);
    void     _pick_random_name(char* buf, size_t len);

    /* ── Input handlers ───────────────────────────────────────────── */
    void _handle_main(input_event_t event);
    void _handle_modes(input_event_t event);
    void _handle_brightness(input_event_t event);
    void _handle_custom_list(input_event_t event);
    void _handle_custom_edit(input_event_t event);
    void _handle_advanced(input_event_t event);

    /* ── Renderers ────────────────────────────────────────────────── */
    void _render_main(lgfx::v1::LGFX_Sprite* canvas);
    void _render_modes(lgfx::v1::LGFX_Sprite* canvas);
    void _render_brightness(lgfx::v1::LGFX_Sprite* canvas);
    void _render_custom_list(lgfx::v1::LGFX_Sprite* canvas);
    void _render_custom_edit(lgfx::v1::LGFX_Sprite* canvas);
    void _render_advanced(lgfx::v1::LGFX_Sprite* canvas);

public:
    App_Torch();
    ~App_Torch() override;

    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;
};
