#include "theme_mgr.h"
#include <math.h>
#include "nvs_flash.h"
#include "nvs.h"

typedef struct {
    uint16_t bg;
    uint16_t accent;
    uint16_t text;
    uint16_t text_dim;
    uint16_t success;
    uint16_t fail;
    const char* name;
} theme_palette_t;

// LovyanGFX color565 values
static const theme_palette_t s_themes[THEME_COUNT] = {
    // 0: Dark Cyberpunk (Deep obsidian, cyan, neon pink accents)
    {0x0821, 0x03BF, 0xFFFF, 0x6398, 0x67E6, 0xF947, "Cyberpunk"},
    // 1: Light Minimal (Pure crisp white wrapper with deep ink text)
    {0xFFFF, 0x03FF, 0x0000, 0x8410, 0x24C6, 0xD000, "Light Minimal"},
    // 2: Hacker Matrix (Classic pitch black console with terminal green)
    {0x0000, 0x07E0, 0x07E0, 0x03E0, 0x07E0, 0xF800, "Matrix Hacker"},
    // 3: Retro Outrun (Sunset neon grids, purple, deep orange)
    {0x1004, 0xFC00, 0xFFFF, 0xFC10, 0x07FF, 0xF800, "Outrun Retro"},
    // 4: Deep Space (Endless dark blue, starlight white/gold)
    {0x0002, 0xFDE0, 0xFFFF, 0x7BEF, 0x07E0, 0xF800, "Deep Space"}
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
