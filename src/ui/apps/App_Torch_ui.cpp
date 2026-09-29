/*
 * App_Torch_ui.cpp — Draw functions for App_Torch
 * Split from App_Torch.cpp to stay under 1000-line limit.
 */
#include "App_Torch.h"
#include "../theme_mgr.h"
#include "../../utils/color_utils.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdio.h>


extern const char* MODE_NAMES[];

#include "../ui_task.h"

void App_Torch::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());
    switch (_state) {
        case STATE_MAIN:        _render_main(canvas); break;
        case STATE_MODES:       _render_modes(canvas); break;
        case STATE_BRIGHTNESS:  _render_brightness(canvas); break;
        case STATE_CUSTOM_LIST: _render_custom_list(canvas); break;
        case STATE_CUSTOM_EDIT: _render_custom_edit(canvas); break;
        case STATE_ADVANCED:    _render_advanced(canvas); break;
    }
}

void App_Torch::_render_main(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;

    /* ── Enhanced Layout ─────────────────────────────────────────── */
    const int32_t BAR_W   = W / 13;    // ~12@160, scales with screen
    const int32_t BAR_X   = MARGIN + 4;
    const int32_t BAR_TOP = H * 17 / 100;  // ~22@128, scales with screen
    const int32_t BAR_BOT = H - H / 6;     // ~107@128, scales with screen
    const int32_t BAR_H   = BAR_BOT - BAR_TOP;

    const int32_t ORB_AREA_L = BAR_X + BAR_W + 12;
    const int32_t ORB_AREA_R = W - MARGIN;
    int32_t orb_cx = ORB_AREA_L + (ORB_AREA_R - ORB_AREA_L) / 2;
    int32_t orb_cy = BAR_TOP + BAR_H / 2;
    int32_t orb_r  = (W < H ? W : H) / 7;  // ~23@160, scales with screen

    /* ── Percentage above bar with glow ──────────────────────────── */
    int32_t pct = (int32_t)((float)_brightness * 100.0f / 255.0f);
    char pct_buf[8];
    snprintf(pct_buf, sizeof(pct_buf), "%ld%%", (long)pct);
    canvas->setFont(&fonts::Font0);

    if (_led_on) {
        float glow_pulse = 0.3f + 0.15f * sinf(_anim_time * 3.0f);
        canvas->setTextColor(color_blend(theme_get_accent(), theme_get_bg(), glow_pulse));
        canvas->setTextDatum(textdatum_t::bottom_center);
        canvas->drawString(pct_buf, BAR_X + BAR_W / 2, BAR_TOP - 3);
    }
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString(pct_buf, BAR_X + BAR_W / 2, BAR_TOP - 4);

    /* ── Enhanced intensity bar with gradient ────────────────────── */
    uint16_t mode_color = _get_mode_color(_current_mode, _anim_time);

    if (_led_on) {
        float aura = 0.08f + 0.04f * sinf(_anim_time * 2.2f);
        canvas->fillRoundRect(BAR_X - 4, BAR_TOP - 4, BAR_W + 8, BAR_H + 8, 6,
                              color_blend(mode_color, theme_get_bg(), aura));
    }

    canvas->fillRoundRect(BAR_X, BAR_TOP, BAR_W, BAR_H, 4,
                          color_blend(theme_get_text_dim(), theme_get_bg(), 0.2f));
    canvas->fillRoundRect(BAR_X + 1, BAR_TOP + 1, BAR_W - 2, BAR_H - 2, 3,
                          color_blend(theme_get_text_dim(), theme_get_bg(), 0.08f));

    int32_t fill_h = (int32_t)((float)BAR_H * (float)_brightness / 255.0f);
    if (fill_h > 0) {
        int32_t fill_y = BAR_BOT - fill_h;

        for (int32_t i = 0; i < fill_h; i += 2) {
            int32_t y = BAR_BOT - i - 1;
            float gradient = (float)i / (float)fill_h;
            uint16_t seg_color = color_blend(mode_color, theme_get_accent(), gradient * 0.6f);
            canvas->drawFastHLine(BAR_X + 2, y, BAR_W - 4, seg_color);
        }

        canvas->fillRoundRect(BAR_X + 2, fill_y, BAR_W - 4, 3, 1, theme_get_text());

        if (_led_on) {
            float shimmer = 0.4f + 0.2f * sinf(_anim_time * 4.5f);
            canvas->fillRoundRect(BAR_X + 3, fill_y + 1, BAR_W - 6, 1, 0,
                                  color_blend(theme_get_text(), mode_color, shimmer));
        }
    }

    int32_t ind_y = BAR_BOT - fill_h;
    if (_led_on) {
        float pulse = 1.0f + 0.3f * sinf(_anim_time * 5.0f);
        canvas->fillCircle(BAR_X + BAR_W / 2, ind_y, (int32_t)(6 * pulse),
                          color_blend(mode_color, theme_get_bg(), 0.15f));
    }
    canvas->fillCircle(BAR_X + BAR_W / 2, ind_y, 5, theme_get_text());
    canvas->fillCircle(BAR_X + BAR_W / 2, ind_y, 3, mode_color);
    canvas->drawCircle(BAR_X + BAR_W / 2, ind_y, 5, theme_get_accent());

    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(color_blend(theme_get_text_dim(), theme_get_bg(), 0.3f));
    canvas->setTextDatum(textdatum_t::middle_center);
    int32_t bounce = (int32_t)(1.5f * sinf(_anim_time * 3.0f));
    canvas->drawString("^", BAR_X + BAR_W / 2, BAR_TOP - 12 - bounce);
    canvas->drawString("v", BAR_X + BAR_W / 2, BAR_BOT + 11 + bounce);

    /* ── Header ──────────────────────────────────────────────────── */
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("TORCH", MARGIN, 2);

    canvas->setTextDatum(textdatum_t::top_right);
    canvas->setTextColor(_led_on ? theme_get_success() : theme_get_text_dim());
    canvas->drawString(_led_on ? "ON" : "OFF", W - MARGIN, 2);

    /* ── Enhanced Orb with multi-layer glow ──────────────────────── */
    if (_led_on) {
        float pulse1 = sinf(_anim_time * 2.8f);
        float pulse2 = sinf(_anim_time * 3.5f + 1.0f);

        canvas->fillCircle(orb_cx, orb_cy, orb_r + 18 + (int32_t)(pulse1 * 3),
                          color_blend(mode_color, theme_get_bg(), 0.06f));
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 14 + (int32_t)(pulse2 * 2),
                          color_blend(mode_color, theme_get_bg(), 0.12f));
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 9 + (int32_t)(pulse1 * 1.5f),
                          color_blend(mode_color, theme_get_bg(), 0.22f));
        canvas->fillCircle(orb_cx, orb_cy, orb_r + 4,
                          color_blend(mode_color, theme_get_bg(), 0.35f));
    }

    float fill_alpha = _led_on ? 0.95f : 0.18f;
    canvas->fillCircle(orb_cx, orb_cy, orb_r, color_blend(mode_color, theme_get_bg(), fill_alpha));

    if (_led_on) {
        canvas->drawCircle(orb_cx, orb_cy, orb_r + 1,
                          color_blend(theme_get_text(), mode_color, 0.7f));
        canvas->drawCircle(orb_cx, orb_cy, orb_r, mode_color);
    } else {
        canvas->drawCircle(orb_cx, orb_cy, orb_r, theme_get_text_dim());
    }

    int32_t hl_x = orb_cx - (int32_t)(orb_r * 0.35f);
    int32_t hl_y = orb_cy - (int32_t)(orb_r * 0.35f);
    int32_t hl_r = (int32_t)(orb_r * 0.35f);
    canvas->fillCircle(hl_x, hl_y, hl_r,
                      color_blend(theme_get_text(), mode_color, _led_on ? 0.65f : 0.08f));
    canvas->fillCircle(hl_x - 1, hl_y - 1, hl_r / 2,
                      color_blend(theme_get_text(), theme_get_bg(), _led_on ? 0.85f : 0.15f));

    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(_led_on ? theme_get_text() : theme_get_text_dim());
    canvas->drawString(MODE_NAMES[_current_mode], orb_cx, orb_cy + orb_r + 10);

    /* ── Footer ──────────────────────────────────────────────────── */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("SEL:ON  R:MOD", CX, H - 2);
}

void App_Torch::_render_modes(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ITEM_H = 18;
    const int32_t HEADER_H = 14;
    const int32_t FOOTER_H = 12;
    const int32_t LIST_Y = HEADER_H;
    const int32_t LIST_BOT = H - FOOTER_H;
    const int32_t VISIBLE_H = LIST_BOT - LIST_Y;
    const int32_t MAX_VISIBLE = VISIBLE_H / ITEM_H;

    if (_mode_cursor < _mode_scroll) {
        _mode_scroll = _mode_cursor;
    }
    if (_mode_cursor >= _mode_scroll + MAX_VISIBLE) {
        _mode_scroll = _mode_cursor - MAX_VISIBLE + 1;
    }

    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("MODES", MARGIN, 2);

    if (_mode_scroll > 0) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::top_center);
        int32_t bounce = (int32_t)(1.5f * sinf(_anim_time * 5.0f));
        canvas->drawString("^", CX, LIST_Y - 2 - bounce);
    }

    /* Navigation pseudo-entries are drawn with the accent colour; only real
     * modes have a computed colour dot. */
    static const char* MODE_EXTRA_NAMES[3] = { "BRIGHTNESS", "CUSTOM", "SETTINGS" };

    uint16_t preview_color = (_mode_cursor < MODE_COUNT)
        ? _get_mode_color((Mode)_mode_cursor, _anim_time)
        : theme_get_accent();

    for (int32_t i = 0; i < MAX_VISIBLE; i++) {
        int32_t idx = _mode_scroll + i;
        if (idx >= MODE_LIST_TOTAL) break;

        int32_t y = LIST_Y + i * ITEM_H;
        if (y + ITEM_H > LIST_BOT) break;

        bool sel = (idx == _mode_cursor);
        bool is_extra = (idx >= MODE_COUNT);
        uint16_t dot_color = is_extra ? theme_get_accent()
                                      : _get_mode_color((Mode)idx, _anim_time);

        if (sel) {
            float pulse = 0.75f + 0.25f * sinf(_anim_time * 4.5f);
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(preview_color, theme_get_bg(), 0.12f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(preview_color, theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());

            int32_t arrow_bounce = (int32_t)(2.5f * sinf(_anim_time * 7.0f));
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->setTextColor(preview_color);
            canvas->drawString(">", W - MARGIN - 5 + arrow_bounce, y + ITEM_H / 2);
        } else {
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.04f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.12f));
            canvas->setTextColor(theme_get_text_dim());
        }

        if (sel) {
            float glow = 0.2f + 0.15f * sinf(_anim_time * 4.0f);
            canvas->fillCircle(MARGIN + 14, y + ITEM_H / 2, 5,
                              color_blend(dot_color, theme_get_bg(), glow));
        }
        canvas->fillCircle(MARGIN + 14, y + ITEM_H / 2, 4, dot_color);
        canvas->drawCircle(MARGIN + 14, y + ITEM_H / 2, 4,
                          sel ? theme_get_text() : theme_get_text_dim());

        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->drawString(is_extra ? MODE_EXTRA_NAMES[idx - MODE_COUNT]
                                    : MODE_NAMES[idx],
                           MARGIN + 26, y + ITEM_H / 2);
    }

    if (_mode_scroll + MAX_VISIBLE < MODE_LIST_TOTAL) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::bottom_center);
        int32_t bounce = (int32_t)(1.5f * sinf(_anim_time * 5.0f));
        canvas->drawString("v", CX, LIST_BOT + 2 + bounce);
    }

    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("SEL:Open  L:Back", CX, H - 2);
}

void App_Torch::_render_brightness(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("BRIGHTNESS", MARGIN, 2);

    /* Large percentage */
    int32_t pct = (int32_t)((float)_brightness * 100.0f / 255.0f);
    char pct_buf[8];
    snprintf(pct_buf, sizeof(pct_buf), "%ld%%", (long)pct);
    canvas->setFont(&fonts::Font2);
    canvas->setTextColor(theme_get_text());
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString(pct_buf, CX, 30);

    /* Slider bar */
    int32_t bar_w = W * 70 / 100;
    int32_t bar_h = 8;
    int32_t bar_x = CX - bar_w / 2;
    int32_t bar_y = H * 34 / 100;  // ~44@128, scales with screen

    canvas->fillRoundRect(bar_x, bar_y, bar_w, bar_h, 4, theme_get_text_dim());
    int32_t fill_w = (int32_t)((float)bar_w * (float)_brightness / 255.0f);
    if (fill_w > 0) {
        canvas->fillRoundRect(bar_x, bar_y, fill_w, bar_h, 4, theme_get_accent());
        canvas->fillRoundRect(bar_x + fill_w - 2, bar_y + 1, 3, bar_h - 2, 1, theme_get_text());
    }

    /* Preset boxes */
    const char* preset_labels[] = {"25%", "50%", "75%", "100%"};
    const int32_t preset_vals[] = {64, 128, 192, 255};
    const int32_t BOX_W = W / 5;
    const int32_t BOX_H = H * 11 / 100;  // ~14@128, scales with screen
    const int32_t BOX_Y = H * 48 / 100;  // ~62@128, scales with screen
    const int32_t BOX_SPACING = (W - 2 * MARGIN - 4 * BOX_W) / 3;

    for (int32_t i = 0; i < 4; i++) {
        int32_t bx = MARGIN + i * (BOX_W + BOX_SPACING);
        bool match = (_brightness == preset_vals[i]);

        if (match) {
            canvas->fillRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4, theme_get_accent());
            canvas->drawRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4, theme_get_text());
            canvas->setTextColor(theme_get_bg());
        } else {
            canvas->fillRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.08f));
            canvas->drawRoundRect(bx, BOX_Y, BOX_W, BOX_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.2f));
            canvas->setTextColor(theme_get_text_dim());
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString(preset_labels[i], bx + BOX_W / 2, BOX_Y + BOX_H / 2);
    }

    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString("< L/R >", CX, BOX_Y + BOX_H + 8);

    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("SEL:OK  L:Back", CX, H - 2);
}

void App_Torch::_render_custom_list(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ITEM_H = 22;
    const int32_t LIST_Y = 16;

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("CUSTOM", MARGIN, 2);

    for (int32_t i = 0; i < 4; i++) {
        int32_t y = LIST_Y + i * (ITEM_H + 2);
        bool sel = (i == _custom_cursor);
        bool active = _custom_modes[i].active;

        if (sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_accent(), theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
            int32_t bounce = (int32_t)(2.0f * sinf(_anim_time * 6.0f));
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->setTextColor(theme_get_accent());
            canvas->drawString(">", W - MARGIN - 4 + bounce, y + ITEM_H / 2);
        } else {
            canvas->fillRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.06f));
            canvas->drawRoundRect(MARGIN, y, W - 2 * MARGIN, ITEM_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
            canvas->setTextColor(active ? theme_get_text() : theme_get_text_dim());
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        if (active) {
            canvas->drawString(_custom_modes[i].name, MARGIN + 8, y + ITEM_H / 2);
            int32_t dots_x = MARGIN + 40;
            for (int32_t s = 0; s < 8; s++) {
                uint16_t dot_col = (s < _custom_modes[i].step_count) ?
                    theme_get_accent() : theme_get_text_dim();
                canvas->fillCircle(dots_x + s * 6, y + ITEM_H / 2,
                                   (s < _custom_modes[i].step_count) ? 2 : 1, dot_col);
            }
            const char* rep = _custom_modes[i].repeat ? "LOOP" : "ONCE";
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->drawString(rep, W - MARGIN - 8, y + ITEM_H / 2);
        } else {
            canvas->drawString("--- empty ---", MARGIN + 8, y + ITEM_H / 2);
        }
    }

    /* Footer */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("R:New  SEL:Edit  L:Back", CX, H - 2);
}

void App_Torch::_render_custom_edit(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ROW_H = 16;
    const int32_t SLIDER_W = W * 40 / 100;
    const int32_t CONTENT_BOT = H - 16;  /* stop above footer */

    custom_mode_t& cm = _custom_modes[_custom_edit_slot];

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    char title[16];
    snprintf(title, sizeof(title), "EDIT: %s", cm.name);
    canvas->drawString(title, MARGIN, 2);

    /* Step indicator */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::top_center);
    char step_buf[24];
    snprintf(step_buf, sizeof(step_buf), "Step %ld/%d", (long)(_step_cursor + 1), cm.step_count);
    canvas->drawString(step_buf, CX, 14);

    /* Component rows */
    const char* labels[] = {"R", "G", "B", "Dur", "Rep"};
    uint16_t colors[] = {theme_get_fail(), theme_get_success(), theme_get_accent(),
                         theme_get_text(), theme_get_text_dim()};

    custom_step_t& step = cm.steps[_step_cursor];
    int32_t values[5] = {step.r, step.g, step.b, step.duration_ms, (int32_t)cm.repeat};
    int32_t max_vals[5] = {255, 255, 255, 5000, 1};

    int32_t row_y = 24;
    for (int32_t i = 0; i < 5; i++) {
        if (row_y + ROW_H > CONTENT_BOT) break;
        bool is_sel = (i == _edit_component);

        if (is_sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_accent(), theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
        } else {
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.06f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 4,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
            canvas->setTextColor(colors[i]);
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->drawString(labels[i], MARGIN + 4, row_y + ROW_H / 2);

        if (i < 4) {
            int32_t slider_x = MARGIN + 22;
            int32_t slider_y = row_y + ROW_H / 2 - 2;
            canvas->fillRoundRect(slider_x, slider_y, SLIDER_W, 4, 2, theme_get_text_dim());
            int32_t fill = (int32_t)((float)SLIDER_W * (float)values[i] / (float)max_vals[i]);
            if (fill > 0) {
                canvas->fillRoundRect(slider_x, slider_y, fill, 4, 2,
                                      is_sel ? theme_get_text() : colors[i]);
            }
            char val_buf[12];
            if (i == 3) snprintf(val_buf, sizeof(val_buf), "%ldms", (long)values[i]);
            else snprintf(val_buf, sizeof(val_buf), "%ld", (long)values[i]);
            canvas->setTextDatum(textdatum_t::middle_right);
            canvas->drawString(val_buf, W - MARGIN - 4, row_y + ROW_H / 2);
        } else {
            const char* rep_text = cm.repeat ? "LOOP" : "ONCE";
            canvas->drawString(rep_text, MARGIN + 22, row_y + ROW_H / 2);
        }
        row_y += ROW_H + 2;
    }

    /* Step dots — only if room */
    if (row_y + 8 <= CONTENT_BOT) {
        for (int32_t s = 0; s < cm.step_count; s++) {
            uint16_t dot_col = (s == _step_cursor) ? theme_get_accent() : theme_get_text_dim();
            int32_t dot_x = CX - (cm.step_count * 7) / 2 + s * 7;
            canvas->fillCircle(dot_x, row_y + 4, (s == _step_cursor) ? 3 : 2, dot_col);
        }
    }

    /* Footer */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    /* LEFT is intercepted globally as "unwind/back", so it is not a value
     * control here — the hint must not claim it is. */
    canvas->drawString("R:Next U/D:Step SEL:Edit L:Back", CX, H - 2);
}

void App_Torch::_render_advanced(LGFX_Sprite* canvas) {
    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t MARGIN = W / 20;
    const int32_t ROW_H = 24;

    /* Header */
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(theme_get_accent());
    canvas->setTextDatum(textdatum_t::top_left);
    canvas->drawString("ADVANCED", MARGIN, 2);

    struct { const char* label; const char* value; } items[3];
    items[0].label = "Max Brightness";
    char mb_buf[8];
    snprintf(mb_buf, sizeof(mb_buf), "%d", _settings.max_brightness);
    items[0].value = mb_buf;
    items[1].label = "Auto-Off";
    if (_settings.auto_off_ms == 0) items[1].value = "OFF";
    else if (_settings.auto_off_ms == 30000) items[1].value = "30s";
    else if (_settings.auto_off_ms == 60000) items[1].value = "1m";
    else if (_settings.auto_off_ms == 120000) items[1].value = "2m";
    else items[1].value = "5m";
    items[2].label = "Strobe Safety";
    items[2].value = _settings.strobe_safe ? "ON" : "OFF";

    int32_t row_y = 16;
    for (int32_t i = 0; i < 3; i++) {
        bool is_sel = (i == _adv_cursor);

        if (is_sel) {
            float pulse = 0.7f + 0.3f * sinf(_anim_time * 4.0f);
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_accent(), theme_get_bg(), 0.15f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_accent(), theme_get_text(), pulse));
            canvas->setTextColor(theme_get_text());
        } else {
            canvas->fillRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.06f));
            canvas->drawRoundRect(MARGIN, row_y, W - 2 * MARGIN, ROW_H, 5,
                                  color_blend(theme_get_text_dim(), theme_get_bg(), 0.15f));
            canvas->setTextColor(theme_get_text_dim());
        }

        canvas->setFont(&fonts::Font0);
        canvas->setTextDatum(textdatum_t::middle_left);
        canvas->drawString(items[i].label, MARGIN + 8, row_y + ROW_H / 2 - 3);
        canvas->setTextDatum(textdatum_t::middle_right);
        canvas->drawString(items[i].value, W - MARGIN - 8, row_y + ROW_H / 2 - 3);

        if (i == 0) {
            int32_t bar_y = row_y + ROW_H / 2 + 3;
            int32_t bar_w = W * 50 / 100;
            canvas->fillRoundRect(CX - bar_w / 2, bar_y, bar_w, 3, 1, theme_get_text_dim());
            int32_t fill = (int32_t)((float)bar_w * (float)_settings.max_brightness / 255.0f);
            if (fill > 0) {
                canvas->fillRoundRect(CX - bar_w / 2, bar_y, fill, 3, 1,
                                      is_sel ? theme_get_text() : theme_get_accent());
            }
        }
        row_y += ROW_H + 4;
    }

    /* Footer */
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    /* LEFT is intercepted globally as "back to main" — only RIGHT changes. */
    canvas->drawString("R:Change U/D:Row SEL:Save L:Back", CX, H - 2);
}
