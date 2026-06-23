---
name: app-template
description: Complete IApp implementation template — header, init/exit/loop/render/handleInput, all four UI states, input feedback, animation patterns. Load when creating new apps.
---

# App Template

Complete copy-paste-ready template. Follow this exactly.

## Header Pattern

```cpp
#pragma once
#include "IApp.h"

class App_Example : public IApp {
    float  _anim_time  = 0.0f;
    int    _sel_idx    = 0;
    int    _scroll     = 0;
    bool   _is_loading = false;
    bool   _has_error  = false;
    int    _flash_frames = 0;
    char   _status[64] = {};
    // Service status cache (copied under mutex in loop(), read in render())
    service_status_t _svc_status = {};

public:
    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas)     override;
    void handleInput(input_event_t event) override;
};
```

## Implementation Pattern

```cpp
#include "App_Example.h"
#include "ui/theme_mgr.h"
#include "services/my_service.h"

void App_Example::init() {
    _anim_time = 0.0f;
    _sel_idx = 0;
    _is_loading = false;
    _has_error = false;
    _flash_frames = 0;
    _status[0] = '\0';
}

void App_Example::exit() {
    // Cleanup if needed
}

void App_Example::loop() {
    _anim_time += 0.025f;  // 1/40fps — frame-rate independent

    if (_is_loading) {
        service_status_t st;
        service_get_status(&st);  // Mutex-protected copy
        _is_loading = st.is_busy;
        _has_error = (st.error_code != 0);
        strlcpy(_status, st.status_msg, sizeof(_status));
        _svc_status = st;
    }
}

void App_Example::handleInput(input_event_t event) {
    switch (event) {
        case BTN_LEFT:
            switch_to_grid();  // MANDATORY — every app
            return;
        case BTN_UP:
            if (_sel_idx > 0) { _sel_idx--; _flash_frames = 3; }
            break;
        case BTN_DOWN:
            _sel_idx++; _flash_frames = 3;
            break;
        case BTN_SELECT:
            if (!_is_loading) {
                _is_loading = true;
                _anim_time = 0.0f;
                _flash_frames = 3;
                service_start("param");
            }
            break;
        default: break;
    }
}

void App_Example::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());  // ALWAYS first

    // Input feedback flash
    if (_flash_frames > 0) {
        _flash_frames--;
        canvas->fillSprite(theme_get_accent());
        return;
    }

    const int32_t W  = canvas->width();
    const int32_t H  = canvas->height();
    const int32_t CX = W / 2;
    const int32_t CY = H / 2;
    const int32_t MARGIN = W / 20;
    const int32_t HEADER_H = H / 9;
    const int32_t FOOTER_Y = H - 3;

    // --- Loading state ---
    if (_is_loading) {
        for (int i = 0; i < 8; i++) {
            float a = _anim_time * 4.0f + i * 0.785f;
            canvas->fillCircle(
                CX + (int)(cosf(a) * 10),
                CY + (int)(sinf(a) * 10),
                2, theme_get_accent());
        }
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString(_status[0] ? _status : "Loading...", CX, CY + 18);
        return;
    }

    // --- Error state ---
    if (_has_error) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("Error", CX, CY - 8);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString(_status, CX, CY + 4);
        canvas->drawString("SEL=Retry  L=Back", CX, CY + 16);
        return;
    }

    // --- Empty state ---
    if (_item_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("Nothing here yet.", CX, CY);
        return;
    }

    // --- Normal state ---
    // Header
    canvas->setTextColor(theme_get_text());
    canvas->setFont(H >= 240 ? &fonts::Font2 : &fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString("Title", CX, HEADER_H / 2);

    // List
    const int32_t ITEM_H = 12;
    for (int i = _scroll; i < _item_count; i++) {
        int32_t y = HEADER_H + 2 + (i - _scroll) * ITEM_H;
        if (y + ITEM_H > FOOTER_Y) break;
        bool sel = (i == _sel_idx);
        canvas->setTextColor(sel ? theme_get_bg() : theme_get_text());
        if (sel) {
            canvas->fillRoundRect(MARGIN, y - 1, W - 2 * MARGIN, ITEM_H + 1,
                                  2, theme_get_accent());
        }
        canvas->drawString(_items[i], MARGIN + 3, y + ITEM_H / 2);
    }

    // Footer
    canvas->setTextColor(theme_get_text_dim());
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("L=Back  SEL=Action", CX, FOOTER_Y);
}
```

## Registration

After creating the app, register in `ui_task.cpp`:
1. Include the header
2. Add to app list/array
3. Create 64x64 icon PNG
4. Run `python3 convert_icons.py icons/ src/assets/icons/`
5. `pio run` must pass
