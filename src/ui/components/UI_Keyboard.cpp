#include "UI_Keyboard.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "../../config/sys_config.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "UI_KBD";

// ── Letter rows ───────────────────────────────────────────────────────────
// Only the 3 alpha rows live here; the 4th row is the function row, drawn
// and navigated separately (see FN_* below). Row 1 (asdf…) is one key short,
// so column 9 is a nullptr filler that the cursor skips.
static const char* KB_LOWER[3][10] = {
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l",nullptr},
    {"z","x","c","v","b","n","m",",",".","?"},
};

static const char* KB_UPPER[3][10] = {
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L",nullptr},
    {"Z","X","C","V","B","N","M",",",".","?"},
};

// SYM layer: number row on top, common symbols below.
static const char* KB_SYM[3][10] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"!","@","#","$","%","&","*","-","+",nullptr},
    {"(",")","/",":",";","'","\"",",",".","?"},
};

// ── Function row (row 3) ──────────────────────────────────────────────────
enum { FN_CAPS = 0, FN_SYM, FN_SPACE, FN_BK, FN_CLR, FN_OK, FN_COUNT };
static const char* FN_LABELS[FN_COUNT] = { "CAP", "SYM", "SPACE", "BK", "CLR", "OK" };
// Relative widths of the function keys; summed and scaled to the grid width.
static const int   FN_WEIGHT[FN_COUNT] = {  22,    22,    38,      20,   20,    22 };

UI_Keyboard::UI_Keyboard()
    : _active(false), _buf_len(0), _cursor_x(0), _cursor_y(0), _mode(0) {
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
    _cursor_y = 0;
    _mode = 0;
    _buf_len = 0;
    memset(_buffer, 0, sizeof(_buffer));
    strncpy(_title, title ? title : "", sizeof(_title) - 1);
    _title[sizeof(_title) - 1] = '\0';
}

// ── Buffer ops ────────────────────────────────────────────────────────────
void UI_Keyboard::_add_char(char c) {
    if (c != 0 && _buf_len < (int)sizeof(_buffer) - 1) {
        _buffer[_buf_len++] = c;
        _buffer[_buf_len] = '\0';
    }
}

void UI_Keyboard::_backspace() {
    if (_buf_len > 0) _buffer[--_buf_len] = '\0';
}

void UI_Keyboard::_clear() {
    _buf_len = 0;
    _buffer[0] = '\0';
}

void UI_Keyboard::_commit() {
    _active = false;
    if (_on_complete) _on_complete(_buffer);
}

void UI_Keyboard::_cancel() {
    _active = false;
    if (_on_cancel) _on_cancel();
}

// ── Layout lookups ────────────────────────────────────────────────────────
const char* UI_Keyboard::_get_key_label(int x, int y) const {
    if (y < 0 || y > 2 || x < 0 || x > 9) return "";
    const char* label = (_mode == 0) ? KB_LOWER[y][x]
                      : (_mode == 1) ? KB_UPPER[y][x]
                                     : KB_SYM[y][x];
    return label ? label : "";
}

char UI_Keyboard::_get_key_char(int x, int y) const {
    const char* label = _get_key_label(x, y);
    return (label && strlen(label) == 1) ? label[0] : 0;
}

int UI_Keyboard::_row_max_x(int row) const {
    if (row == 3) return FN_COUNT - 1;   // function row: 0..5
    if (row == 1) return 8;              // asdf row is one key short
    return 9;                            // full 10-column alpha rows
}

void UI_Keyboard::_clamp_cursor() {
    int mx = _row_max_x(_cursor_y);
    if (_cursor_x > mx) _cursor_x = mx;
    if (_cursor_x < 0)  _cursor_x = 0;
}

// ── Input ─────────────────────────────────────────────────────────────────
void UI_Keyboard::_activate_current() {
    if (_cursor_y < 3) {
        char c = _get_key_char(_cursor_x, _cursor_y);
        if (c != 0) _add_char(c);
        return;
    }

    switch (_cursor_x) {
        case FN_CAPS:
            // Toggle case; from the symbol layer, drop back to lowercase.
            _mode = (_mode == 0) ? 1 : 0;
            break;
        case FN_SYM:
            _mode = (_mode == 2) ? 0 : 2;
            break;
        case FN_SPACE:
            _add_char(' ');
            break;
        case FN_BK:
            // Backspace, or exit the keyboard when there's nothing to delete.
            if (_buf_len > 0) _backspace();
            else              _cancel();
            break;
        case FN_CLR:
            _clear();
            break;
        case FN_OK:
            _commit();
            break;
        default: break;
    }
}

void UI_Keyboard::handleInput(input_event_t ev) {
    if (!_active) return;

    switch (ev) {
        case BTN_UP:
            _cursor_y = (_cursor_y > 0) ? _cursor_y - 1 : 3;
            _clamp_cursor();
            break;
        case BTN_DOWN:
            _cursor_y = (_cursor_y < 3) ? _cursor_y + 1 : 0;
            _clamp_cursor();
            break;
        case BTN_LEFT: {
            int mx = _row_max_x(_cursor_y);
            _cursor_x = (_cursor_x > 0) ? _cursor_x - 1 : mx;
            break;
        }
        case BTN_RIGHT: {
            int mx = _row_max_x(_cursor_y);
            _cursor_x = (_cursor_x < mx) ? _cursor_x + 1 : 0;
            break;
        }
        case BTN_SELECT:
            _activate_current();
            break;
        default: break;
    }
}

// ── Render ────────────────────────────────────────────────────────────────
void UI_Keyboard::render(LGFX_Sprite* canvas) {
    if (!_active) return;

    const int W = canvas->width();
    const int H = canvas->height();

    uint16_t bg     = theme_get_bg();
    uint16_t accent = theme_get_accent();
    uint16_t txt    = theme_get_text();
    uint16_t dim    = theme_get_text_dim();
    uint16_t panel  = color_blend(bg, RGB565_BLACK, 0.93f);

    // Full-screen dim backdrop
    canvas->fillRect(0, 0, W, H, panel);

    // ── Title bar ──────────────────────────────────────────────────────────
    const int TITLE_H = 13;
    canvas->drawFastHLine(0, TITLE_H, W, accent);
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(accent);
    canvas->drawString(_title, 4, TITLE_H / 2);

    canvas->setTextDatum(textdatum_t::middle_right);
    canvas->setTextColor(dim);
    const char* mode_tag = (_mode == 0) ? "abc" : (_mode == 1) ? "ABC" : "#@1";
    canvas->drawString(mode_tag, W - 4, TITLE_H / 2);

    // ── Text input box ──────────────────────────────────────────────────────
    const int box_x = 3, box_y = 16, box_w = W - 6, box_h = 17;
    canvas->drawRoundRect(box_x, box_y, box_w, box_h, 3, dim);

    // Show the tail of the buffer that fits inside the box (auto-scroll).
    canvas->setFont(&fonts::Font2);
    const int avail = box_w - 10;
    int start = 0;
    while (start < _buf_len && canvas->textWidth(_buffer + start) > avail) start++;
    const char* vis = _buffer + start;

    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(txt);
    canvas->drawString(vis, box_x + 4, box_y + box_h / 2);

    int cursor_px = box_x + 4 + canvas->textWidth(vis);
    if (cursor_px < box_x + box_w - 3) {
        canvas->fillRect(cursor_px, box_y + 3, 2, box_h - 6, accent);
    }

    // ── Grid geometry ────────────────────────────────────────────────────────
    const int COLS    = 10;
    const int gap     = 1;
    const int vgap    = 2;
    const int grid_w  = W - 10;                       // 5px side margins
    const int grid_x  = (W - grid_w) / 2;
    const int key_w   = (grid_w - (COLS - 1) * gap) / COLS;
    const int grid_top = 37;
    const int footer_h = 11;                          // reserved at the bottom
    const int rows_h   = (H - footer_h) - grid_top;   // height for the 4 rows
    const int key_h    = (rows_h - 3 * vgap) / 4;

    // ── Letter rows (0..2) ───────────────────────────────────────────────────
    canvas->setTextDatum(textdatum_t::middle_center);
    for (int row = 0; row < 3; row++) {
        int py = grid_top + row * (key_h + vgap);
        for (int col = 0; col < COLS; col++) {
            const char* label = _get_key_label(col, row);
            if (!label || !label[0]) continue;

            int px = grid_x + col * (key_w + gap);
            bool sel = (row == _cursor_y && col == _cursor_x);

            uint16_t k_bg = sel ? accent : bg;
            uint16_t k_fg = sel ? bg     : txt;
            canvas->drawRoundRect(px, py, key_w, key_h, 2, sel ? accent : dim);
            if (sel) canvas->fillRoundRect(px + 1, py + 1, key_w - 2, key_h - 2, 2, k_bg);

            canvas->setFont(&fonts::Font2);
            canvas->setTextColor(k_fg);
            canvas->drawString(label, px + key_w / 2, py + key_h / 2 + 1);
        }
    }

    // ── Function row (row 3) ─────────────────────────────────────────────────
    int fn_total_weight = 0;
    for (int i = 0; i < FN_COUNT; i++) fn_total_weight += FN_WEIGHT[i];

    int fy = grid_top + 3 * (key_h + vgap);
    int fx = grid_x;
    for (int i = 0; i < FN_COUNT; i++) {
        // Last key absorbs rounding so the row ends flush with the grid.
        int fw = (i == FN_COUNT - 1)
                   ? (grid_x + grid_w) - fx
                   : (grid_w * FN_WEIGHT[i]) / fn_total_weight - gap;

        bool sel = (_cursor_y == 3 && _cursor_x == i);

        // Colour cues: OK=green, BK/CLR=red, active CAPS/SYM=accent border.
        uint16_t k_bg = bg, k_fg = txt, k_border = dim;
        if (i == FN_OK)                       k_border = theme_get_success();
        else if (i == FN_BK || i == FN_CLR)   k_border = theme_get_fail();
        if ((i == FN_CAPS && _mode == 1) ||
            (i == FN_SYM  && _mode == 2))      k_border = accent;

        if (sel) {
            k_bg = (i == FN_OK) ? theme_get_success()
                 : (i == FN_BK || i == FN_CLR) ? theme_get_fail()
                 : accent;
            k_fg = bg;
            k_border = k_bg;
        }

        canvas->drawRoundRect(fx, fy, fw, key_h, 2, k_border);
        if (sel) canvas->fillRoundRect(fx + 1, fy + 1, fw - 2, key_h - 2, 2, k_bg);

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setTextColor(k_fg);
        canvas->drawString(FN_LABELS[i], fx + fw / 2, fy + key_h / 2);

        fx += fw + gap;
    }

    // ── Footer hint ──────────────────────────────────────────────────────────
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->setTextColor(dim);
    canvas->drawString("Move  SEL pick  BK=Back", W / 2, H - 1);
}
