#include "App_Torch.h"
#include "../theme_mgr.h"
#include "../../drivers/led_driver.h"
#include <math.h>
#include <stdio.h>

extern void switch_to_grid();

App_Torch::App_Torch() {
    current_mode = MODE_WHITE;
    led_on = false;
    anim_time = 0.0f;
    pulse_size = 1.0f;
    transition_val = 0.0f;
}

App_Torch::~App_Torch() {}

void App_Torch::init() {
    led_driver_init();
    current_mode = MODE_WHITE;
    led_on = false;
    anim_time = 0.0f;
    pulse_size = 1.0f;
    transition_val = 0.0f;
    update_led();
}

void App_Torch::exit() {
    led_driver_off();
}

void App_Torch::hsv_to_rgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (s == 0.0f) {
        r = g = b = (uint8_t)(v * 255.0f);
        return;
    }
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    h /= 60.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    
    float rf = 0, gf = 0, bf = 0;
    switch (i) {
        case 0: rf = v; gf = t; bf = p; break;
        case 1: rf = q; gf = v; bf = p; break;
        case 2: rf = p; gf = v; bf = t; break;
        case 3: rf = p; gf = q; bf = v; break;
        case 4: rf = t; gf = p; bf = v; break;
        default: rf = v; gf = p; bf = q; break;
    }
    r = (uint8_t)(rf * 255.0f);
    g = (uint8_t)(gf * 255.0f);
    b = (uint8_t)(bf * 255.0f);
}

void App_Torch::update_led() {
    if (!led_on) {
        led_driver_off();
        return;
    }

    if (current_mode == MODE_WHITE) {
        led_driver_set_rgb(255, 255, 255);
    } else if (current_mode == MODE_RED_BEACON) {
        led_driver_set_rgb(255, 0, 0);
    } else if (current_mode == MODE_GREEN_BEACON) {
        led_driver_set_rgb(0, 255, 0);
    } else if (current_mode == MODE_BLUE_BEACON) {
        led_driver_set_rgb(0, 0, 255);
    } else if (current_mode == MODE_RAINBOW) {
        uint8_t r, g, b;
        hsv_to_rgb(anim_time * 60.0f, 1.0f, 1.0f, r, g, b);
        led_driver_set_rgb(r, g, b);
    } else if (current_mode == MODE_CYBER_PULSE) {
        uint16_t acc = theme_get_accent();
        uint8_t r = (acc >> 11) & 0x1F;
        uint8_t g = (acc >> 5) & 0x3F;
        uint8_t b = acc & 0x1F;
        float intensity = (sinf(anim_time * 3.0f) + 1.0f) * 0.5f; 
        led_driver_set_rgb((uint8_t)(r * 8.0f * intensity), 
                           (uint8_t)(g * 4.0f * intensity), 
                           (uint8_t)(b * 8.0f * intensity));
    } else if (current_mode == MODE_SOS) {
        // Standard Morse SOS: ... --- ... (fast dots, slow dashes)
        float t = fmodf(anim_time * 2.0f, 32.0f); 
        bool light_on = false;
        
        if (t < 6.0f) light_on = (fmodf(t, 2.0f) < 1.0f); // 3 dots
        else if (t >= 8.0f && t < 20.0f) light_on = (fmodf(t - 8.0f, 4.0f) < 3.0f); // 3 dashes
        else if (t >= 22.0f && t < 28.0f) light_on = (fmodf(t - 22.0f, 2.0f) < 1.0f); // 3 dots
        
        if (light_on) led_driver_set_rgb(255, 255, 255);
        else led_driver_set_rgb(0, 0, 0);
    } else if (current_mode == MODE_APEX_SIGNAL) {
        // Track/Traffic racing signal light
        float t = fmodf(anim_time, 4.0f);
        if (t < 1.0f) led_driver_set_rgb(255, 0, 0); // Red
        else if (t < 2.0f) led_driver_set_rgb(255, 120, 0); // Amber
        else if (t < 3.0f) led_driver_set_rgb(0, 255, 0); // Green
        else led_driver_set_rgb(0, 0, 0); // Off step
    }
}

void App_Torch::loop() {
    anim_time += 0.05f;
    pulse_size += (1.0f - pulse_size) * 0.15f; 
    transition_val += (0.0f - transition_val) * 0.2f;

    if (led_on) { // Need to update every frame for animations
        update_led();
    }
}

void App_Torch::handleInput(input_event_t event) {
    if (event == BTN_LEFT) {
        switch_to_grid();
        return;
    }
    
    if (event == BTN_SELECT) {
        led_on = !led_on;
        pulse_size = 1.4f; 
        update_led();
        return;
    }

    if (event == BTN_RIGHT || event == BTN_DOWN) {
        current_mode = (Mode)((current_mode + 1) % MODE_COUNT);
        transition_val = 15.0f; 
        pulse_size = 1.2f;
        update_led();
    } else if (event == BTN_UP) {
        if (current_mode == 0) current_mode = (Mode)(MODE_COUNT - 1);
        else current_mode = (Mode)(current_mode - 1);
        transition_val = -15.0f;
        pulse_size = 1.2f;
        update_led();
    }
}

static uint16_t blend_color(uint16_t fg, uint16_t bg, float alpha) {
    if (alpha <= 0.0f) return bg;
    if (alpha >= 1.0f) return fg;
    uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
    uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
    return ((uint16_t)(r2 + (r1 - r2) * alpha) << 11) |
           ((uint16_t)(g2 + (g1 - g2) * alpha) << 5) |
           (uint16_t)(b2 + (b1 - b2) * alpha);
}

void App_Torch::render(LGFX_Sprite* canvas) {
    uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);
    
    int cx = canvas->width() / 2;
    int cy = canvas->height() / 2 - 18; // Shifted UP to prevent overlap
    
    canvas->setFont(&fonts::Font0); // 6x8
    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->drawString("- TORCH UI -", cx, 4);

    uint16_t orb_color = theme_get_text_dim(); 
    if (led_on) {
        if (current_mode == MODE_WHITE) orb_color = canvas->color565(255, 255, 255);
        else if (current_mode == MODE_RED_BEACON) orb_color = canvas->color565(255, 50, 50);
        else if (current_mode == MODE_GREEN_BEACON) orb_color = canvas->color565(50, 255, 50);
        else if (current_mode == MODE_BLUE_BEACON) orb_color = canvas->color565(50, 100, 255);
        else if (current_mode == MODE_RAINBOW) {
            uint8_t r, g, b;
            hsv_to_rgb(anim_time * 60.0f, 1.0f, 1.0f, r, g, b);
            orb_color = canvas->color565(r, g, b);
        }
        else if (current_mode == MODE_CYBER_PULSE) {
            float intensity = (sinf(anim_time * 3.0f) + 1.0f) * 0.5f;
            orb_color = blend_color(theme_get_accent(), bg, 0.4f + intensity * 0.6f);
        }
        else if (current_mode == MODE_SOS) {
            float t = fmodf(anim_time * 2.0f, 32.0f);
            bool on = false;
            if (t < 6.0f) on = (fmodf(t, 2.0f) < 1.0f);
            else if (t >= 8.0f && t < 20.0f) on = (fmodf(t - 8.0f, 4.0f) < 3.0f);
            else if (t >= 22.0f && t < 28.0f) on = (fmodf(t - 22.0f, 2.0f) < 1.0f);
            orb_color = on ? canvas->color565(255, 255, 255) : theme_get_text_dim();
        }
        else if (current_mode == MODE_APEX_SIGNAL) {
            float t = fmodf(anim_time, 4.0f);
            if (t < 1.0f) orb_color = canvas->color565(255, 0, 0); // Red
            else if (t < 2.0f) orb_color = canvas->color565(255, 120, 0); // Amber
            else if (t < 3.0f) orb_color = canvas->color565(0, 255, 0); // Green
            else orb_color = theme_get_text_dim();
        }
    }

    float dynamic_r = 24.0f * pulse_size;
    
    if (led_on) {
        float glow_pulse = 2.0f * sinf(anim_time * 4.0f);
        canvas->fillCircle(cx, cy, (int)(dynamic_r + 8 + glow_pulse), blend_color(orb_color, bg, 0.15f));
        canvas->fillCircle(cx, cy, (int)(dynamic_r + 4 + glow_pulse/2), blend_color(orb_color, bg, 0.35f));
    }
    
    canvas->fillCircle(cx, cy, (int)dynamic_r, blend_color(orb_color, bg, led_on ? 0.9f : 0.3f));
    canvas->drawCircle(cx, cy, (int)dynamic_r, led_on ? 0xFFFF : theme_get_text_dim());
    
    // 3D Glass Highlight
    canvas->fillCircle(cx - (int)(dynamic_r * 0.3f), cy - (int)(dynamic_r * 0.3f), (int)(dynamic_r * 0.3f), blend_color(0xFFFF, orb_color, led_on ? 0.6f : 0.2f));

    // Mode Label
    int label_y = cy + 36;
    const char* mode_names[] = {
        "WHITE", "RED BCN", "GRN BCN", "BLU BCN", "RAINBOW", "CYBER", "SOS ALERT", "APEX R5"
    };
    
    // Bouncing arrows
    int bounce = (int)(2.0f * sinf(anim_time * 5.0f));
    canvas->setTextColor(theme_get_accent());
    canvas->drawString("<", 12 - bounce, label_y);
    canvas->drawString(">", canvas->width() - 12 + bounce, label_y);
    
    canvas->setTextColor(led_on ? theme_get_text() : theme_get_text_dim());
    canvas->drawString(mode_names[current_mode], cx + (int)transition_val, label_y);

    // Pill UI element
    int pill_w = 40;
    int pill_h = 12;
    int pill_x = cx - pill_w/2;
    int pill_y = label_y + 12;
    uint16_t pill_bg = led_on ? theme_get_success() : blend_color(theme_get_text_dim(), bg, 0.3f);
    
    canvas->fillRoundRect(pill_x, pill_y, pill_w, pill_h, 6, pill_bg);
    canvas->setTextColor(led_on ? 0x0000 : theme_get_text()); 
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString(led_on ? "ON" : "OFF", cx, pill_y + pill_h/2);

    // Footer
    canvas->setTextColor(blend_color(theme_get_text_dim(), bg, 0.5f));
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("L: EXT  SEL: ON  R: MOD", cx, canvas->height() - 2);
}
