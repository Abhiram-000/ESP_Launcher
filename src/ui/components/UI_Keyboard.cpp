#include "UI_Keyboard.h"
#include "../theme_mgr.h"
#include <string.h>

// 4 rows, 10 cols grid layout
static const char* KB_LOWER[4][10] = {
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l","@"},
    {"UPR","z","x","c","v","b","n","m",",","."},
    {"SYM","!","?"," "," "," ","DEL","CAN","OK","OK"}
};

static const char* KB_UPPER[4][10] = {
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L","@"},
    {"LWR","Z","X","C","V","B","N","M",",","."},
    {"SYM","!","?"," "," "," ","DEL","CAN","OK","OK"}
};

static const char* KB_SYM[4][10] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"-","/",":",";","(",")","$","&","@","\""},
    {"LWR",".",",","?","!","'","+","*","=","%"},
    {"UPR","<",">"," "," "," ","DEL","CAN","OK","OK"}
};

UI_Keyboard::UI_Keyboard() : _active(false), _buf_len(0), _cursor_x(0), _cursor_y(0), _mode(0) {
    memset(_buffer, 0, sizeof(_buffer));
    memset(_title, 0, sizeof(_title));
}

UI_Keyboard::~UI_Keyboard() {}

void UI_Keyboard::begin(const char* title, std::function<void(const char*)> on_complete, std::function<void()> on_cancel) {
    _active = true;
    _on_complete = on_complete;
    _on_cancel = on_cancel;
    _cursor_x = 0;
    _cursor_y = 0;
    _mode = 0;
    _buf_len = 0;
    memset(_buffer, 0, sizeof(_buffer));
    strncpy(_title, title, sizeof(_title)-1);
}

void UI_Keyboard::_add_char(char c) {
    if (_buf_len < (int)sizeof(_buffer) - 1 && c != 0) {
        _buffer[_buf_len++] = c;
        _buffer[_buf_len] = '\0';
    }
}

void UI_Keyboard::_backspace() {
    if (_buf_len > 0) {
        _buffer[--_buf_len] = '\0';
    }
}

void UI_Keyboard::_commit() {
    _active = false;
    if (_on_complete) _on_complete(_buffer);
}

void UI_Keyboard::_cancel() {
    _active = false;
    if (_on_cancel) _on_cancel();
}

const char* UI_Keyboard::_get_key_label(int x, int y) {
    if (y < 0 || y > 3 || x < 0 || x > 9) return "";
    if (_mode == 0) return KB_LOWER[y][x];
    if (_mode == 1) return KB_UPPER[y][x];
    return KB_SYM[y][x];
}

char UI_Keyboard::_get_key_char(int x, int y) {
    const char* label = _get_key_label(x, y);
    if (strlen(label) == 1) return label[0];
    return 0;
}

void UI_Keyboard::handleInput(input_event_t ev) {
    if (!_active) return;

    if (ev == BTN_UP)    _cursor_y = (_cursor_y > 0) ? _cursor_y - 1 : 3;
    if (ev == BTN_DOWN)  _cursor_y = (_cursor_y < 3) ? _cursor_y + 1 : 0;
    if (ev == BTN_LEFT)  _cursor_x = (_cursor_x > 0) ? _cursor_x - 1 : 9;
    if (ev == BTN_RIGHT) _cursor_x = (_cursor_x < 9) ? _cursor_x + 1 : 0;

    if (ev == BTN_SELECT) {
        const char* label = _get_key_label(_cursor_x, _cursor_y);
        
        if (strcmp(label, "LWR") == 0) _mode = 0;
        else if (strcmp(label, "UPR") == 0) _mode = 1;
        else if (strcmp(label, "SYM") == 0) _mode = 2;
        else if (strcmp(label, "DEL") == 0) _backspace();
        else if (strcmp(label, "CAN") == 0) _cancel();
        else if (strcmp(label, "OK") == 0)  _commit();
        else if (strcmp(label, " ") == 0)   _add_char(' ');
        else {
            char c = _get_key_char(_cursor_x, _cursor_y);
            if (c != 0) _add_char(c);
        }
    }

    // Hardware shortcut: hold SELECT (handled as BTN_LEFT conceptually by some apps, but let's stick to on-screen CANCEL or maybe we map it? For now, we only have 4 buttons, so let's allow returning via an empty submit or if they press a 'cancel' shortcut)
    // We will let the app handle hardware cancel if needed, or they can use backspace to empty and hit OK.
}

static uint16_t blend(uint16_t fg, uint16_t bg, float alpha) {
    if (alpha <= 0.0f) return bg;
    if (alpha >= 1.0f) return fg;
    uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
    uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
    return ((uint16_t)(r2 + (r1 - r2) * alpha) << 11) |
           ((uint16_t)(g2 + (g1 - g2) * alpha) << 5) |
           (uint16_t)(b2 + (b1 - b2) * alpha);
}

void UI_Keyboard::render(LGFX_Sprite* canvas) {
    if (!_active) return;

    uint16_t bg = theme_get_bg();
    uint16_t accent = theme_get_accent();
    uint16_t text = theme_get_text();
    uint16_t dim = theme_get_text_dim();

    // Dark overlay background (glassmorphism feel)
    canvas->fillRect(0, 30, 160, 98, blend(bg, 0x0000, 0.9f));
    canvas->drawFastHLine(0, 30, 160, accent);

    // Title & Buffer
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(accent);
    canvas->drawString(_title, 4, 34);

    canvas->setTextColor(text);
    char disp[66];
    snprintf(disp, sizeof(disp), "%s_", _buffer);
    canvas->drawString(disp, 4, 46);

    // Render Grid
    int start_y = 62;
    int key_w = 14;
    int key_h = 14;
    int space_x = 2;
    int space_y = 2;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 10; x++) {
            const char* label = _get_key_label(x, y);
            
            // Skip rendering if it's the 2nd/3rd cell of a wide button (like SPACE or OK)
            if (x > 0 && strcmp(label, _get_key_label(x-1, y)) == 0) continue;

            // Calculate width based on repeating labels
            int span = 1;
            for (int sx = x + 1; sx < 10; sx++) {
                if (strcmp(label, _get_key_label(sx, y)) == 0) span++;
                else break;
            }

            int px = 2 + x * (key_w + space_x);
            int py = start_y + y * (key_h + space_y);
            int pw = (key_w * span) + (space_x * (span - 1));

            bool is_sel = false;
            for (int sx = x; sx < x + span; sx++) {
                if (_cursor_x == sx && _cursor_y == y) is_sel = true;
            }

            uint16_t k_bg = is_sel ? accent : blend(dim, bg, 0.15f);
            uint16_t k_fg = is_sel ? bg : text;

            if (strcmp(label, "OK") == 0) {
                k_bg = is_sel ? canvas->color565(80,220,100) : blend(canvas->color565(80,220,100), bg, 0.2f);
                k_fg = is_sel ? bg : canvas->color565(80,220,100);
            } else if (strcmp(label, "DEL") == 0) {
                k_bg = is_sel ? canvas->color565(220,70,70) : blend(canvas->color565(220,70,70), bg, 0.2f);
                k_fg = is_sel ? bg : canvas->color565(220,70,70);
            } else if (strcmp(label, "CAN") == 0) {
                k_bg = is_sel ? blend(dim, bg, 0.6f) : blend(dim, bg, 0.2f);
                k_fg = is_sel ? bg : text;
            } else if (strcmp(label, "LWR") == 0 || strcmp(label, "UPR") == 0 || strcmp(label, "SYM") == 0) {
                k_bg = is_sel ? accent : blend(accent, bg, 0.2f);
                k_fg = is_sel ? bg : accent;
            }

            canvas->fillRoundRect(px, py, pw, key_h, 2, k_bg);
            
            canvas->setTextDatum(textdatum_t::middle_center);
            canvas->setTextColor(k_fg);
            canvas->drawString(strcmp(label," ")==0 ? "SPACE" : label, px + pw/2, py + key_h/2);
        }
    }
}
