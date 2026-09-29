#include "theme_mgr.h"
#include <math.h>
#include "nvs_flash.h"
#include "nvs.h"
#include <LovyanGFX.hpp>
#include "esp_log.h"


typedef struct {
    uint16_t bg;
    uint16_t accent;
    uint16_t text;
    uint16_t text_dim;
    uint16_t success;
    uint16_t fail;
    const char* name;
} theme_palette_t;

// LovyanGFX RGB565 palette table.
// Only this table may contain raw RGB565 literals.  All app code MUST use theme_get_*() accessors.
// Column order MUST match theme_palette_t above: { bg, accent, text, text_dim, success, fail, name }.
// A previous revision of this comment read "bg / panel / text / text_dim / accent / fail",
// which mislabels fields 2 and 5. The initialiser is positional, so anyone editing
// this table by column name would have written accent into success, and vice versa.
static const theme_palette_t s_themes[THEME_COUNT] = {
    //    bg     accent  text    text_dim success  fail    name
    {0x0821, 0x03BF, 0xFFFF, 0x6398, 0x67E6, 0xF947, "Cyberpunk"},    // 0: Deep obsidian, blue accent, neon pink
    {0xFFFF, 0x03FF, 0x0000, 0x8410, 0x24C6, 0xD000, "Light Minimal"}, // 1: Crisp white, deep ink
    {0x0000, 0x07E0, 0x07E0, 0x03E0, 0x07E0, 0xF800, "Matrix Hacker"}, // 2: Pitch black, terminal green
    {0x1004, 0xFC00, 0xFFFF, 0xFC10, 0x07FF, 0xF800, "Outrun Retro"},  // 3: Sunset neon, orange accent
    {0x0002, 0xFDE0, 0xFFFF, 0x7BEF, 0x07E0, 0xF800, "Deep Space"},   // 4: Dark blue, starlight gold
};

typedef struct { float r, g, b; } rgb_f_t;

typedef struct {
    rgb_f_t bg;
    rgb_f_t accent;
    rgb_f_t text;
    rgb_f_t text_dim;
    rgb_f_t success;
    rgb_f_t fail;
} dynamic_palette_t;

static theme_type_t s_current_theme = THEME_DARK_CYBERPUNK;
static dynamic_palette_t s_dyn;

static rgb_f_t hex_to_rgb(uint16_t color) {
    rgb_f_t c;
    c.r = (float)((color >> 11) & 0x1F) * 8.0f;
    c.g = (float)((color >> 5) & 0x3F) * 4.0f;
    c.b = (float)(color & 0x1F) * 8.0f;
    return c;
}

static uint16_t rgb_to_hex(rgb_f_t c) {
    uint8_t r = (uint8_t)(c.r / 8.0f) & 0x1F;
    uint8_t g = (uint8_t)(c.g / 4.0f) & 0x3F;
    uint8_t b = (uint8_t)(c.b / 8.0f) & 0x1F;
    return (r << 11) | (g << 5) | b;
}

static void lerp_rgb(rgb_f_t* current, rgb_f_t target) {
    float spd = 0.12f; // Smooth fade transition (lower is slower)
    current->r += (target.r - current->r) * spd;
    current->g += (target.g - current->g) * spd;
    current->b += (target.b - current->b) * spd;
}

void theme_init(void) {
    uint8_t saved_theme = (uint8_t)THEME_DARK_CYBERPUNK;
    nvs_handle_t handle;
    if (nvs_open("ui_cfg", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, "theme", &saved_theme);
        nvs_close(handle);
    }
    if (saved_theme >= THEME_COUNT) saved_theme = THEME_DARK_CYBERPUNK;
    s_current_theme = (theme_type_t)saved_theme;

    theme_palette_t t = s_themes[s_current_theme];
    s_dyn.bg = hex_to_rgb(t.bg);
    s_dyn.accent = hex_to_rgb(t.accent);
    s_dyn.text = hex_to_rgb(t.text);
    s_dyn.text_dim = hex_to_rgb(t.text_dim);
    s_dyn.success = hex_to_rgb(t.success);
    s_dyn.fail = hex_to_rgb(t.fail);
}

void theme_set(theme_type_t theme) {
    if (theme < THEME_COUNT) {
        s_current_theme = theme;
        nvs_handle_t handle;
        if (nvs_open("ui_cfg", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_u8(handle, "theme", (uint8_t)theme);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
}

theme_type_t theme_get_current(void) {
    return s_current_theme;
}

const char* theme_get_name(theme_type_t theme) {
    if (theme < THEME_COUNT) return s_themes[theme].name;
    return "Unknown";
}

void theme_update(void) {
    theme_palette_t t = s_themes[s_current_theme];
    lerp_rgb(&s_dyn.bg, hex_to_rgb(t.bg));
    lerp_rgb(&s_dyn.accent, hex_to_rgb(t.accent));
    lerp_rgb(&s_dyn.text, hex_to_rgb(t.text));
    lerp_rgb(&s_dyn.text_dim, hex_to_rgb(t.text_dim));
    lerp_rgb(&s_dyn.success, hex_to_rgb(t.success));
    lerp_rgb(&s_dyn.fail, hex_to_rgb(t.fail));
}

uint16_t theme_get_bg(void) { return rgb_to_hex(s_dyn.bg); }
uint16_t theme_get_accent(void) { return rgb_to_hex(s_dyn.accent); }
uint16_t theme_get_text(void) { return rgb_to_hex(s_dyn.text); }
uint16_t theme_get_text_dim(void) { return rgb_to_hex(s_dyn.text_dim); }
uint16_t theme_get_success(void) { return rgb_to_hex(s_dyn.success); }
uint16_t theme_get_fail(void) { return rgb_to_hex(s_dyn.fail); }

void theme_draw_wallpaper(lgfx::v1::LGFX_Sprite* canvas, float time_offset) {
    uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);

    uint16_t accent = theme_get_accent();

    uint8_t ar = (accent >> 11) & 0x1F;
    uint8_t ag = (accent >> 5) & 0x3F;
    uint8_t ab = accent & 0x1F;

    uint8_t br = (bg >> 11) & 0x1F;
    uint8_t bg_g = (bg >> 5) & 0x3F;
    uint8_t bb = bg & 0x1F;

    auto blend = [&](float alpha) -> uint16_t {
        uint8_t r = br + (ar - br) * alpha;
        uint8_t g = bg_g + (ag - bg_g) * alpha;
        uint8_t b = bb + (ab - bb) * alpha;
        return (r << 11) | (g << 5) | b;
    };

    int w = canvas->width();
    int h = canvas->height();

    static const float s_phases[9] = {0.0f, 1.2f, 2.8f, 4.1f, 5.5f, 7.0f, 8.3f, 9.9f, 11.4f};
    static const float s_freqs1[9] = {0.019f, 0.024f, 0.016f, 0.028f, 0.021f, 0.014f, 0.026f, 0.018f, 0.023f};
    static const float s_freqs2[9] = {0.041f, 0.035f, 0.048f, 0.032f, 0.044f, 0.038f, 0.030f, 0.046f, 0.036f};
    static const float s_amps1[9]   = {18.0f, 24.0f, 14.0f, 28.0f, 20.0f, 12.0f, 26.0f, 16.0f, 22.0f};
    static const float s_amps2[9]   = {8.0f, 12.0f, 6.0f, 14.0f, 10.0f, 5.0f, 13.0f, 7.0f, 11.0f};

    for (int i = 0; i < 9; i++) {
        /* WHY time_offset is added to the phase: adding a constant to the
         * argument of sinf() shifts the wave along x, which reads as a
         * travelling wave. The parameter was accepted but never used, so the
         * wallpaper was byte-identical on every frame at all nine call sites
         * despite each passing its own animation clock. The vertical term adds
         * a slow independent drift so the motion does not read as one rigid
         * translation. Speeds are deliberately low: this is a background and
         * must not compete with foreground content. */
        float base_y = (h * (i + 1)) / 10.0f
                     + sinf(time_offset * 0.25f + s_phases[i]) * 2.5f;
        float phase = s_phases[i] + time_offset;
        float f1 = s_freqs1[i];
        float f2 = s_freqs2[i];
        float a1 = s_amps1[i];
        float a2 = s_amps2[i];

        float alpha_f = 0.18f + (i % 3) * 0.05f;
        uint16_t line_col = blend(alpha_f);

        int prev_x = 0;
        int prev_y = (int)(base_y + sinf(phase) * a1 * 0.3f);

        for (int x = 2; x < w; x += 2) {
            float fx = (float)x;
            int y = (int)(base_y
                + sinf(fx * f1 + phase) * a1
                + sinf(fx * f2 + phase * 1.7f + time_offset * 0.6f) * a2);

            canvas->drawLine(prev_x, prev_y, x, y, line_col);
            prev_x = x;
            prev_y = y;
        }
    }
}
