#include "UI_Keyboard.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "UI_KBD";

// ── QWERTY lowercase (mode 0) ─────────────────────────────────────────────
// Row 0 = number row, Row 4 = function row with wide SPACE
static const char* KB_LOWER[5][10] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l",nullptr},
    {"z","x","c","v","b","n","m",",",".","?"},
    {"CAP","BK","SP","SP","SP","OK","CL","SYM",nullptr,nullptr}
};

// ── QWERTY UPPERCASE (mode 1) ──── number row becomes symbols ────────────
static const char* KB_UPPER[5][10] = {
    {"!","@","#","$","%","^","&","*","(",")"},
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L",nullptr},
    {"Z","X","C","V","B","N","M",",",".","?"},
    {"CAP","BK","SP","SP","SP","OK","CL","SYM",nullptr,nullptr}
};

// ── Symbols (mode 2) ─────────────────────────────────────────────────────
static const char* KB_SYM[5][10] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"-","/",":",";","(",")","$","&","@","\""},
    {"[","]","{","}","<",">","#","%","+",nullptr},
    {"=","~","|","\\","^","_","`","!",nullptr,nullptr},
    {"CAP","BK","SP","SP","SP","OK","CL","SYM",nullptr,nullptr}
};

UI_Keyboard::UI_Keyboard()
    : _active(false), _buf_len(0), _cursor_x(0), _cursor_y(0),
      _mode(0), _text_scroll_off(0) {
    memset(_buffer, 0, sizeof(_buffer));
    memset(_title, 0, sizeof(_title));
}

UI_Keyboard::~UI_Keyboard() {}

void UI_Keyboard::begin(const char* title,
                         std::function<void(const char*)> on_complete,
                         std::function<void()> on_cancel) {
    _active = true;
    _on_complete = on_complete;
    _on_cancel = on_cancel;
    _cursor_x = 0;
    _cursor_y = 1;
    _mode = 0;
    _buf_len = 0;
    _text_scroll_off = 0;
    memset(_buffer, 0, sizeof(_buffer));
    strncpy(_title, title, sizeof(_title) - 1);
}

void UI_Keyboard::_add_char(char c) {
    if (_buf_len >= 0 && _buf_len < (int)sizeof(_buffer) - 1 && c != 0) {
        _buffer[_buf_len++] = c;
        _buffer[_buf_len] = '\0';
        if (_buf_len - _text_scroll_off > 26) {
            _text_scroll_off = _buf_len - 26;
        }
    }
}

void UI_Keyboard::_backspace() {
    if (_buf_len > 0) {
        _buffer[--_buf_len] = '\0';
        if (_text_scroll_off > 0 && _text_scroll_off >= _buf_len - 25) {
            _text_scroll_off = (_buf_len > 25) ? _buf_len - 25 : 0;
        }
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
    if (y < 0 || y > 4 || x < 0 || x > 9) return "";
    const char* label = nullptr;
    if (_mode == 0)      label = KB_LOWER[y][x];
    else if (_mode == 1) label = KB_UPPER[y][x];
    else                 label = KB_SYM[y][x];
    return label ? label : "";
}

char UI_Keyboard::_get_key_char(int x, int y) {
    const char* label = _get_key_label(x, y);
    if (label && strlen(label) == 1) return label[0];
    return 0;
}

void UI_Keyboard::handleInput(input_event_t ev) {
    if (!_active) return;

    if (ev == BTN_UP)    _cursor_y = (_cursor_y > 0) ? _cursor_y - 1 : 4;
    if (ev == BTN_DOWN)  _cursor_y = (_cursor_y < 4) ? _cursor_y + 1 : 0;

    if (ev == BTN_LEFT) {
        if (_cursor_y == 4) {
            // Function row: skip empty cells and wide SPACE columns
            if (_cursor_x == 7)      _cursor_x = 6;
            else if (_cursor_x == 6) _cursor_x = 5;
            else if (_cursor_x == 5) _cursor_x = 2;
            else if (_cursor_x == 2) _cursor_x = 1;
            else if (_cursor_x == 1) _cursor_x = 0;
            else                     _cursor_x = 7;
        } else {
            _cursor_x = (_cursor_x > 0) ? _cursor_x - 1 : 9;
        }
    }
    if (ev == BTN_RIGHT) {
        if (_cursor_y == 4) {
            if (_cursor_x == 0)      _cursor_x = 1;
            else if (_cursor_x == 1) _cursor_x = 2;
            else if (_cursor_x == 2) _cursor_x = 5;
            else if (_cursor_x == 5) _cursor_x = 6;
            else if (_cursor_x == 6) _cursor_x = 7;
            else                     _cursor_x = 0;
        } else {
            _cursor_x = (_cursor_x < 9) ? _cursor_x + 1 : 0;
        }
    }

    // Wrap cursor away from null cells in rows 2 and 3
    if (_cursor_y == 2 && _cursor_x == 9) _cursor_x = 8;
    if (_cursor_y == 3 && _cursor_x >= 9) _cursor_x = 8;

    if (ev == BTN_SELECT) {
        // Wide SPACE: any of cols 2-5 on row 4 triggers space
        if (_cursor_y == 4 && _cursor_x >= 2 && _cursor_x <= 4) {
            _add_char(' ');
        }
        else if (_cursor_y == 4) {
            const char* label = _get_key_label(_cursor_x, _cursor_y);
            if (strcmp(label, "CAP") == 0) {
                if (_mode == 2) _mode = 0;
                else            _mode = !_mode;
            }
            else if (strcmp(label, "SYM") == 0) {
                _mode = (_mode == 2) ? 0 : 2;
            }
            else if (strcmp(label, "BK") == 0)   _backspace();
            else if (strcmp(label, "OK") == 0)    _commit();
            else if (strcmp(label, "CL") == 0) {
                _buf_len = 0;
                _text_scroll_off = 0;
                memset(_buffer, 0, sizeof(_buffer));
            }
        }
        else {
            char c = _get_key_char(_cursor_x, _cursor_y);
            if (c != 0) _add_char(c);
        }
    }

    if (ev == BTN_LEFT && _buf_len == 0 && _on_cancel) _cancel();
}

void UI_Keyboard::render(LGFX_Sprite* canvas) {
    if (!_active) return;

    uint16_t bg     = theme_get_bg();
    uint16_t accent = theme_get_accent();
    uint16_t txt    = theme_get_text();
    uint16_t dim    = theme_get_text_dim();

    // ── Full-screen dark overlay ───────────────────────────────────
    canvas->fillRect(0, 0, 160, 128, color_blend(bg, 0x0000, 0.93f));

    // ── Title bar ──────────────────────────────────────────────────
    canvas->fillRect(0, 0, 160, 14, color_blend(bg, 0x0000, 0.93f));
    canvas->drawFastHLine(0, 14, 160, accent);
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(accent);
    canvas->drawString(_title, 4, 7);

    canvas->setTextDatum(textdatum_t::middle_right);
    canvas->setTextColor(dim);
    if (_mode == 0)      canvas->drawString("abc", 156, 7);
    else if (_mode == 1) canvas->drawString("ABC", 156, 7);
    else                 canvas->drawString("123", 156, 7);

    // ── Text input box ─────────────────────────────────────────────
    int box_x = 2, box_y = 18, box_w = 156, box_h = 16;
    canvas->drawRoundRect(box_x, box_y, box_w, box_h, 3, dim);

    char vis[27];
    int vis_len = 0;
    for (int i = _text_scroll_off; i < _buf_len && vis_len < 26; i++) {
        vis[vis_len++] = _buffer[i];
    }
    vis[vis_len] = '\0';

    canvas->setFont(&fonts::Font2);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(txt);
    canvas->drawString(vis, box_x + 3, box_y + box_h / 2);

    int cursor_px = box_x + 3 + vis_len * 16;
    if (cursor_px < box_x + box_w - 3) {
        canvas->fillRect(cursor_px, box_y + 2, 2, box_h - 4, txt);
    }

    // ── Hint line ──────────────────────────────────────────────────
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(dim);
    if (_mode == 2)      canvas->drawString("SYMBOLS", 4, 38);
    else if (_mode == 1) canvas->drawString("CAPS LOCK", 4, 38);
    else                 canvas->drawString("TYPE AND PRESS OK", 4, 38);

    // ── Keyboard grid ──────────────────────────────────────────────
    const int COLS = 10;
    const int ROWS = 5;
    const int key_w = 14;
    const int key_h = 13;
    const int gap   = 1;
    const int grid_w = COLS * key_w + (COLS - 1) * gap;
    const int grid_x = (160 - grid_w) / 2;
    const int grid_y = 44;

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            // Skip rendering spacebar continuation cells
            if (row == 4 && col >= 3 && col <= 4) continue;

            const char* label = _get_key_label(col, row);
            if (!label || !label[0]) continue;

            int px = grid_x + col * (key_w + gap);
            int py = grid_y + row * (key_h + gap);
            int pw = key_w;

            // Wide SPACE spans cols 2-4
            bool is_space = (row == 4 && col == 2);
            if (is_space) {
                pw = key_w * 3 + gap * 2;
            }

            bool is_sel = false;
            if (is_space) {
                is_sel = (_cursor_y == 4 && _cursor_x >= 2 && _cursor_x <= 4);
            } else {
                is_sel = (col == _cursor_x && row == _cursor_y);
            }

            bool is_caps = (strcmp(label, "CAP") == 0);
            bool is_bk   = (strcmp(label, "BK") == 0);
            bool is_ok   = (strcmp(label, "OK") == 0);
            bool is_cl   = (strcmp(label, "CL") == 0);
            bool is_sym  = (strcmp(label, "SYM") == 0);
            bool is_sp   = (strcmp(label, "SP") == 0);
            bool is_single_char = (strlen(label) == 1);

            uint16_t k_bg = bg;
            uint16_t k_fg = txt;
            uint16_t k_border = dim;

            if (is_sel) {
                if (is_ok) {
                    k_bg = canvas->color565(80, 220, 100);
                    k_fg = bg;
                } else if (is_bk) {
                    k_bg = canvas->color565(220, 70, 70);
                    k_fg = bg;
                } else if (is_cl) {
                    k_bg = canvas->color565(220, 70, 70);
                    k_fg = bg;
                } else if (is_caps) {
                    k_bg = (_mode == 1) ? canvas->color565(100, 180, 255) : accent;
                    k_fg = bg;
                } else if (is_sym) {
                    k_bg = canvas->color565(180, 120, 255);
                    k_fg = bg;
                } else if (is_space) {
                    k_bg = accent;
                    k_fg = bg;
                } else {
                    k_bg = accent;
                    k_fg = bg;
                }
                k_border = k_bg;
            } else {
                if (is_caps && _mode == 1)
                    k_border = canvas->color565(100, 180, 255);
                else if (is_sym && _mode == 2)
                    k_border = canvas->color565(180, 120, 255);
            }

            canvas->drawRoundRect(px, py, pw, key_h, 2, k_border);
            if (is_sel) {
                canvas->fillRoundRect(px + 1, py + 1, pw - 2, key_h - 2, 2, k_bg);
            }

            // Single chars use Font2 (16x16) to fill/overflow the key
            // Multi-char labels use Font0 (6x8) to fit
            if (is_single_char) {
                canvas->setFont(&fonts::Font2);
                canvas->setTextDatum(textdatum_t::middle_center);
                canvas->setTextColor(k_fg);
                canvas->drawString(label, px + pw / 2, py + key_h / 2);
            } else {
                canvas->setFont(&fonts::Font0);
                canvas->setTextDatum(textdatum_t::middle_center);
                canvas->setTextColor(k_fg);
                canvas->drawString(label, px + pw / 2, py + key_h / 2);
            }
        }
    }

    // ── Footer ─────────────────────────────────────────────────────
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(dim);
    canvas->drawString("Move   Select   Hold BK Back", 80, 126);
}
