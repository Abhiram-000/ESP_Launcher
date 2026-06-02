#pragma once
#include <stdint.h>
#include <functional>
#include "../../include/LGFX_Config.h"
#include "../../managers/input_mgr.h"

class UI_Keyboard {
public:
    UI_Keyboard();
    ~UI_Keyboard();

    void begin(const char* title, std::function<void(const char*)> on_complete, std::function<void()> on_cancel);
    void handleInput(input_event_t ev);
    void render(LGFX_Sprite* canvas);
    bool isActive() const { return _active; }

private:
    bool _active;
    char _buffer[64];
    int  _buf_len;
    char _title[32];

    int _cursor_x;
    int _cursor_y;
    int _mode;            // 0=lowercase QWERTY, 1=UPPERCASE QWERTY, 2=symbols
    int _text_scroll_off;

    std::function<void(const char*)> _on_complete;
    std::function<void()> _on_cancel;

    void _add_char(char c);
    void _backspace();
    void _commit();
    void _cancel();

    const char* _get_key_label(int x, int y);
    char        _get_key_char(int x, int y);
};
