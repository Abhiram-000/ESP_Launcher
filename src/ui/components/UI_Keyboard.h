#pragma once
#include <stdint.h>
#include <functional>
#include "../../include/LGFX_Config.h"
#include "../../managers/input_mgr.h"

// On-screen QWERTY keyboard overlay.
//   3 letter rows + 1 function row (CAPS / SYM / SPACE / BK / CLR / OK).
//   Numbers and punctuation live in the SYM layer, keeping every layout to a
//   clean 4 rows that fit a 160x128 panel with room for the footer.
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

    int _cursor_x;        // letter rows: column 0..9 ; function row: button 0..5
    int _cursor_y;        // 0..2 = letter rows, 3 = function row
    int _mode;            // 0=lowercase, 1=UPPERCASE, 2=symbols

    std::function<void(const char*)> _on_complete;
    std::function<void()> _on_cancel;

    void _add_char(char c);
    void _backspace();
    void _clear();
    void _commit();
    void _cancel();

    int  _row_max_x(int row) const;   // highest valid cursor_x for a row
    void _clamp_cursor();             // keep cursor_x inside the active row
    void _activate_current();         // act on the key under the cursor

    const char* _get_key_label(int x, int y) const;
    char        _get_key_char(int x, int y) const;
};
