#include "App_Grid.h"
#include "../theme_mgr.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

extern void switch_to_app(IApp* app);

App_Grid::App_Grid() {
    selected_index = 0;
    cursor_x = 0;
    cursor_y = 0;
    target_cursor_x = 0;
    target_cursor_y = 0;
    cursor_vx = 0;
    cursor_vy = 0;
    cursor_w = 32;
    cursor_h = 32;
    target_cursor_w = 32;
    target_cursor_h = 32;
    cursor_vw = 0;
    cursor_vh = 0;
    anim_time = 0;
}

App_Grid::~App_Grid() {
    for (auto& entry : apps) {
        if (entry.cached_icon32) {
            delete[] entry.cached_icon32;
        }
    }
}

/**
 * @brief Nearest-neighbour 2× downsample: 64×64 RGB565 → 32×32 RGB565.
 *
 * Samples every other row and column from the source 64×64 array and
 * writes the result into a caller-provided 32×32 (1024-word) stack buffer.
 * No heap allocation. Runs in O(1024) iterations.
 *
 * @param src64   Source 64×64 RGB565 array (4096 words, flash-resident).
 * @param dst32   Destination 32×32 buffer (1024 words, caller-allocated).
 */
static void downsample_64_to_32(const uint16_t* src64, uint16_t* dst32) {
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            // Pick the pixel at (row*2, col*2) in the 64-wide source
            dst32[row * 32 + col] = src64[(row * 2) * 64 + (col * 2)];
        }
    }
}

void App_Grid::registerApp(const char* name, const uint16_t* icon, IApp* app) {
    AppEntry entry = {name, icon, nullptr, app};
    if (icon) {
        entry.cached_icon32 = new uint16_t[32 * 32];
        downsample_64_to_32(icon, entry.cached_icon32);
    }
    apps.push_back(entry);
}

void App_Grid::init() {
    anim_time = 0;
}

void App_Grid::exit() {
    // Graceful exit
}

void App_Grid::loop() {
    anim_time += 0.04f; 
    
    // First-order Easing (Lerp) for zero-bounce professional movement
    float ease = 0.22f; 
    cursor_x += (target_cursor_x - cursor_x) * ease;
    cursor_y += (target_cursor_y - cursor_y) * ease;
    cursor_w += (target_cursor_w - cursor_w) * ease;
    cursor_h += (target_cursor_h - cursor_h) * ease;

    // Reset velocities (no longer used in lerp, but kept for struct compatibility if needed)
    cursor_vx = 0; cursor_vy = 0; cursor_vw = 0; cursor_vh = 0;
}

void App_Grid::handleInput(input_event_t event) {
    if (apps.empty()) return;
    int num_apps = apps.size();

    if (event == BTN_RIGHT) {
        if (selected_index < num_apps - 1) selected_index++;
    } 
    else if (event == BTN_LEFT) {
        if (selected_index > 0) selected_index--;
    } 
    else if (event == BTN_DOWN) {
        if (selected_index + 3 < num_apps) selected_index += 3;
        else selected_index = num_apps - 1;
    } 
    else if (event == BTN_UP) {
        if (selected_index - 3 >= 0) selected_index -= 3;
        else selected_index = 0;
    } 
    else if (event == BTN_SELECT) {
        if (apps[selected_index].app_instance != nullptr) {
            switch_to_app(apps[selected_index].app_instance);
        }
    }
}

void App_Grid::render(LGFX_Sprite* canvas) {
    uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);

    // --- Premium Minimalist Static Dot Grid Wallpaper ---
    uint16_t dot_color = theme_get_text_dim();
    
    // Create a very subtle faded version of the text_dim color for the dots
    uint16_t dr = (dot_color >> 11) & 0x1F;
    uint16_t dg = (dot_color >> 5) & 0x3F;
    uint16_t db = dot_color & 0x1F;
    dr = dr / 2; 
    dg = dg / 2; 
    db = db / 2; 
    uint16_t dim_dot = (dr << 11) | (dg << 5) | db;
    
    // Draw dots every 16 pixels
    for (int y = 8; y < canvas->height(); y += 16) {
        for (int x = 8; x < canvas->width(); x += 16) {
            canvas->drawPixel(x, y, dim_dot);
        }
    }

    if (apps.empty()) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("NO APPS", canvas->width()/2, canvas->height()/2);
        return;
    }

    int page = selected_index / MAX_APPS_PER_PAGE;
    int start_idx = page * MAX_APPS_PER_PAGE;
    int end_idx = start_idx + MAX_APPS_PER_PAGE;
    if (end_idx > apps.size()) end_idx = apps.size();

    int sel_idx_on_page = selected_index % MAX_APPS_PER_PAGE;
    int sel_col = sel_idx_on_page % 3;
    int sel_row = sel_idx_on_page / 3;
    
    int icon_size = 32;
    int gap_x = 16;
    int gap_y = 10;
    
    int start_x = (canvas->width() - (3 * icon_size + 2 * gap_x)) / 2;
    int start_y = (canvas->height() - (3 * icon_size + 2 * gap_y)) / 2 + 5; // Lowered by 8px total for user-defined balance
    
    float base_target_x = start_x + sel_col * (icon_size + gap_x);
    float base_target_y = start_y + sel_row * (icon_size + gap_y);

    // Clean, direct targeting (removed jelly stretch)
    target_cursor_w = icon_size;
    target_cursor_h = icon_size;

    target_cursor_x = base_target_x - (target_cursor_w - icon_size) * 0.5f;
    target_cursor_y = base_target_y - (target_cursor_h - icon_size) * 0.5f;

    if (abs(cursor_x - base_target_x) > canvas->width() || cursor_w == 0) {
        cursor_x = base_target_x;
        cursor_y = base_target_y;
        cursor_w = icon_size;
        cursor_h = icon_size;
        target_cursor_x = base_target_x;
        target_cursor_y = base_target_y;
        target_cursor_w = icon_size;
        target_cursor_h = icon_size;
    }

    // ---------------------------------------------------------------------------
    // PREMIUM SELECTION INDICATOR
    // Layers (back to front), all minimal — the icon stays the hero:
    //   1. Soft glassy backdrop  — very subtle fill behind selected icon
    //   2. Diffuse outer glow   — 3 fading concentric rounded rects (bleed outward)
    //   3. Crisp inner border   — 1px accent line flush with the icon edge
    //   4. Animated corner ticks — 4 short L-shaped brackets that breathe
    // ---------------------------------------------------------------------------
    int   cx_draw = (int)cursor_x;
    int   cy_draw = (int)cursor_y;
    int   cw_draw = (int)cursor_w;
    int   ch_draw = (int)cursor_h;

    uint16_t acc = theme_get_accent();

    /* Decompose accent into R/G/B components for manual alpha blending */
    uint8_t ar = ((acc >> 11) & 0x1F) << 3;
    uint8_t ag = ((acc >> 5)  & 0x3F) << 2;
    uint8_t ab = (acc & 0x1F) << 3;

    /* Blend helper: mix accent with background at given alpha (0–255) */
    auto blend_with_bg = [&](uint8_t alpha) -> uint16_t {
        uint8_t br = ((bg >> 11) & 0x1F) << 3;
        uint8_t bg_g = ((bg >> 5) & 0x3F) << 2;
        uint8_t bg_b = (bg & 0x1F) << 3;
        uint8_t mr = br + ((int32_t)(ar - br) * alpha / 255);
        uint8_t mg = bg_g + ((int32_t)(ag - bg_g) * alpha / 255);
        uint8_t mb = bg_b + ((int32_t)(ab - bg_b) * alpha / 255);
        return canvas->color565(mr, mg, mb);
    };

    /* 1. Glassy backdrop — very dim fill so icon doesn't get washed out */
    canvas->fillRect(cx_draw - 4, cy_draw - 4, cw_draw + 8, ch_draw + 8,
                          blend_with_bg(30));

    /* 2. Diffuse outer glow — three rings, each 40% dimmer than the last */
    canvas->drawRect(cx_draw - 7, cy_draw - 7, cw_draw + 14, ch_draw + 14,
                          blend_with_bg(40));
    canvas->drawRect(cx_draw - 5, cy_draw - 5, cw_draw + 10, ch_draw + 10,
                          blend_with_bg(80));
    canvas->drawRect(cx_draw - 4, cy_draw - 4, cw_draw + 8,  ch_draw + 8,
                          blend_with_bg(130));

    /* 3. Crisp inner border — 1 pixel, full accent, radius = 0 */
    canvas->drawRect(cx_draw - 3, cy_draw - 3, cw_draw + 6, ch_draw + 6, acc);

    /* 4. Animated corner bracket ticks
     *    Each bracket = 2 short lines forming an "L" at each corner.
     *    Length pulses between 5 and 8 px with sin(anim_time * 2). */
    int tick = 5 + (int)(1.5f * (sinf(anim_time * 2.2f) * 0.5f + 0.5f) * 2.0f);
    int bx0 = cx_draw - 4, by0 = cy_draw - 4;
    int bx1 = cx_draw + cw_draw + 3, by1 = cy_draw + ch_draw + 3;

    /* Top-left */
    canvas->drawFastHLine(bx0,        by0,        tick, acc);
    canvas->drawFastVLine(bx0,        by0,        tick, acc);
    /* Top-right */
    canvas->drawFastHLine(bx1 - tick + 1, by0,   tick, acc);
    canvas->drawFastVLine(bx1,        by0,        tick, acc);
    /* Bottom-left */
    canvas->drawFastHLine(bx0,        by1,        tick, acc);
    canvas->drawFastVLine(bx0,        by1 - tick + 1, tick, acc);
    /* Bottom-right */
    canvas->drawFastHLine(bx1 - tick + 1, by1,   tick, acc);
    canvas->drawFastVLine(bx1,        by1 - tick + 1, tick, acc);

    /* 5. Accent pip — tiny glowing dot top-right of the selected icon that
     *    blinks in and out softly as a life indicator.            */
    float pulse = sinf(anim_time * 3.0f);
    if (pulse > 0.0f) {
        uint16_t pip = blend_with_bg((uint8_t)(pulse * 220));
        canvas->fillCircle(bx1 + 1, by0 - 1, 2, pip);
        canvas->fillCircle(bx1 + 1, by0 - 1, 1, acc);
    }

    for (int i = start_idx; i < end_idx; i++) {
        int idx = i % MAX_APPS_PER_PAGE;
        int x = start_x + (idx % 3) * (icon_size + gap_x);
        int y = start_y + (idx / 3) * (icon_size + gap_y);
        
        if (apps[i].cached_icon32 != nullptr) {
            // Use pre-downsampled 32x32 icon (cached on register)
            uint16_t* icon32 = apps[i].cached_icon32;
            
            if (i == selected_index) {
                // Professional Enlargement: Selected app grows smoothly
                // We temporary render the icon to a sprite to use pushRotateZoom.
                LGFX_Sprite temp_sp(canvas);
                temp_sp.setPsram(false); // Small sprite in internal RAM
                if (temp_sp.createSprite(32, 32)) {
                    temp_sp.pushImage(0, 0, 32, 32, icon32);
                    float zoom = (cursor_w / (float)icon_size) * 1.15f; 
                    temp_sp.pushRotateZoom(x + 16, y + 16, 0, zoom, zoom);
                    temp_sp.deleteSprite();
                } else {
                    canvas->pushImage(x, y, 32, 32, icon32); // Fallback
                }
            } else {
                canvas->pushImage(x, y, 32, 32, icon32);
            }
        } else {
            canvas->fillRect(x, y, 32, 32, canvas->color565(30, 40, 50));
        }
        
        if (i == selected_index) {
            canvas->setFont(&fonts::Font0); // 6x8
            canvas->setTextColor(theme_get_text());
            canvas->setTextDatum(textdatum_t::bottom_center);
            canvas->drawString(apps[i].name, canvas->width()/2, canvas->height() - 8);
        }
    }

    int total_pages = (apps.size() + MAX_APPS_PER_PAGE - 1) / MAX_APPS_PER_PAGE;
    if (total_pages > 1) {
        int dot_start_x = (canvas->width() - (total_pages * 8)) / 2;
        for (int p = 0; p < total_pages; p++) {
            uint16_t color = (p == page) ? theme_get_text() : theme_get_text_dim();
            canvas->fillCircle(dot_start_x + (p * 8) + 4, canvas->height() - 19, 2, color);
        }
    }
}
