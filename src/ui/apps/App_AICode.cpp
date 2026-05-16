#include "App_AICode.h"
#include "../../config/sys_config.h"
#include "../theme_mgr.h"
#include "../ui_task.h"
#include "esp_log.h"
#include "esp_timer.h" // For timing if needed, though lgfx::v1::millis used
#include <math.h>

extern void switch_to_grid();

// static const char* TAG = "APP_AICODE";

const char* kb_layout[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl!",
    "[zxcvbnm<",
    "S>" // S=Space, >=Enter
};

static const char* symbol_row = "!@#$%^&*()";

App_AICode::App_AICode() {
    current_state = STATE_CHECK;
    anim_time = 0;
    scroll_idx = 0;
    selected_network_idx = 0;
    kb_row = 0;
    kb_col = 0;
    visible_pass = true; // Default to visible as requested earlier
    caps_lock = false;
    conn_start_ms = 0;
}

App_AICode::~App_AICode() {}

void App_AICode::init() {
    current_state = STATE_CHECK;
    anim_time = 0;
    password = "";
    networks.clear();
    wifi_mgr_init();
}

void App_AICode::exit() {
    // Cleanup if needed
}

void App_AICode::loop() {
    anim_time += 0.05f;

    if (current_state == STATE_SCANNING) {
        // Wait for scan to complete
        wifi_scan_info_t temp[MAX_WIFI_NETWORKS];
        int count = wifi_mgr_get_scan_results(temp, MAX_WIFI_NETWORKS);
        if (count > 0) {
            networks.clear();
            for (int i = 0; i < count; i++) networks.push_back(temp[i]);
            current_state = STATE_SCAN_RESULTS;
            selected_network_idx = 0;
            scroll_idx = 0;
        }
    }

    if (current_state == STATE_CONNECTING) {
        if (wifi_mgr_is_connected()) {
            current_state = STATE_CONNECTED;
        } else if (lgfx::v1::millis() - conn_start_ms > APP_WIFI_CONN_TIMEOUT_MS) {
            current_state = STATE_ERROR;
        }
    }
}

void App_AICode::handleInput(input_event_t event) {
    if (event == BTN_LEFT && (current_state == STATE_SCAN_RESULTS || current_state == STATE_CHECK)) {
        switch_to_grid();
        return;
    }

    switch (current_state) {
        case STATE_CHECK:
            if (event == BTN_SELECT) {
                if (wifi_mgr_is_connected()) current_state = STATE_CONNECTED;
                else {
                    wifi_mgr_scan_start();
                    current_state = STATE_SCANNING;
                }
            }
            break;

        case STATE_SCAN_RESULTS:
            if (event == BTN_UP) {
                if (selected_network_idx > 0) selected_network_idx--;
            } else if (event == BTN_DOWN) {
                if (selected_network_idx < (int)networks.size() - 1) selected_network_idx++;
            } else if (event == BTN_SELECT) {
                target_ssid = networks[selected_network_idx].ssid;
                current_state = STATE_PASSWORD;
                kb_row = 0; kb_col = 0;
            }
            break;

        case STATE_PASSWORD: {
            int row_len = strlen(kb_layout[kb_row]);
            if (event == BTN_UP) { if (kb_row > 0) kb_row--; }
            else if (event == BTN_DOWN) { if (kb_row < 4) kb_row++; }
            
            // Re-clamp column for the new row
            row_len = strlen(kb_layout[kb_row]);
            if (kb_col >= row_len) kb_col = row_len - 1;

            if (event == BTN_LEFT) { if (kb_col > 0) kb_col--; }
            else if (event == BTN_RIGHT) { if (kb_col < row_len - 1) kb_col++; }
            else if (event == BTN_SELECT) {
                int r_len = strlen(kb_layout[kb_row]);
                if (kb_col >= r_len) kb_col = r_len - 1;
                char key = kb_layout[kb_row][kb_col];
                if (key == 'S') password += " "; 
                else if (key == '<') { if (!password.empty()) password.pop_back(); }
                else if (key == '>') {
                    if (!password.empty()) {
                        wifi_mgr_connect(target_ssid.c_str(), password.c_str());
                        conn_start_ms = lgfx::v1::millis();
                        current_state = STATE_CONNECTING;
                    }
                }
                else if (key == '[') caps_lock = !caps_lock;
                else if (key == '!') visible_pass = !visible_pass;
                else if (key != ' ') {
                    if (kb_row == 0 && caps_lock) key = symbol_row[kb_col];
                    else if (key >= 'a' && key <= 'z' && caps_lock) key -= 32;
                    password += key;
                }
            }
            break;
        }

        case STATE_CONNECTED:
        case STATE_ERROR:
            if (event == BTN_SELECT) switch_to_grid();
            break;
            
        default: break;
    }
}

void App_AICode::draw_header(LGFX_Sprite* canvas, const char* title) {
    uint16_t acc = theme_get_accent();
    canvas->setFont(&fonts::Font0); // 6x8
    canvas->setTextColor(acc);
    canvas->setTextDatum(textdatum_t::top_center);
    canvas->drawString(title, canvas->width()/2, 4);
    canvas->drawFastHLine(10, 16, canvas->width()-20, acc);
}

uint16_t App_AICode::blend_color(uint16_t fg, uint16_t bg, float alpha) {
    if (alpha <= 0.0f) return bg;
    if (alpha >= 1.0f) return fg;
    uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
    uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
    return ((uint16_t)(r2 + (r1 - r2) * alpha) << 11) |
           ((uint16_t)(g2 + (g1 - g2) * alpha) << 5) |
           (uint16_t)(b2 + (b1 - b2) * alpha);
}

void App_AICode::render(LGFX_Sprite* canvas) {
    uint16_t bg = theme_get_bg();
    canvas->fillSprite(bg);

    switch (current_state) {
        case STATE_CHECK: draw_status_check(canvas); break;
        case STATE_SCANNING: draw_status_check(canvas); break;
        case STATE_SCAN_RESULTS: draw_scanner(canvas); break;
        case STATE_PASSWORD: draw_keyboard(canvas); break;
        case STATE_CONNECTING: draw_connecting(canvas); break;
        case STATE_CONNECTED: draw_connected(canvas); break;
        case STATE_ERROR: draw_error(canvas); break;
    }
}

void App_AICode::draw_status_check(LGFX_Sprite* canvas) {
    draw_header(canvas, "AI CODE - WIFI");
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(theme_get_text());
    
    if (current_state == STATE_SCANNING) {
        canvas->drawString("SCANNING...", canvas->width()/2, canvas->height()/2);
        // Add a spinner
        float angle = anim_time * 6.0f;
        canvas->drawArc(canvas->width()/2, canvas->height()/2 + 20, 10, 8, (int)angle, (int)angle + 90, theme_get_accent());
    } else {
        if (wifi_mgr_is_connected()) {
            canvas->drawString("CONNECTED TO:", canvas->width()/2, canvas->height()/2 - 10);
            canvas->setTextColor(theme_get_accent());
            canvas->drawString(wifi_mgr_get_connected_ssid(), canvas->width()/2, canvas->height()/2 + 10);
        } else {
            canvas->drawString("NOT CONNECTED", canvas->width()/2, canvas->height()/2);
        }
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString("PRESS SELECT TO SCAN", canvas->width()/2, canvas->height() - 15);
    }
}

void App_AICode::draw_scanner(LGFX_Sprite* canvas) {
    draw_header(canvas, "SELECT NETWORK");
    int start_y = 25;
    int item_h = 18;
    
    for (int i = 0; i < (int)networks.size(); i++) {
        int y = start_y + i * item_h;
        if (i == selected_network_idx) {
            canvas->fillRoundRect(5, y-2, canvas->width()-10, item_h, 4, blend_color(theme_get_accent(), theme_get_bg(), 0.3f));
            canvas->drawRoundRect(5, y-2, canvas->width()-10, item_h, 4, theme_get_accent());
            canvas->setTextColor(theme_get_text());
        } else {
            canvas->setTextColor(theme_get_text_dim());
        }
        canvas->setTextDatum(textdatum_t::top_left);
        canvas->drawString(networks[i].ssid, 15, y);
        
        // RSSI dot
        uint16_t rssi_col = (networks[i].rssi > -60) ? theme_get_accent() : theme_get_text_dim();
        canvas->fillCircle(canvas->width() - 20, y + 5, 2, rssi_col);
    }
}

void App_AICode::draw_keyboard(LGFX_Sprite* canvas) {
    draw_header(canvas, target_ssid.c_str());
    
    // Pass display - Always clear text as requested
    canvas->drawRoundRect(10, 22, canvas->width()-20, 18, 4, theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::middle_left);
    canvas->setTextColor(theme_get_text());
    canvas->drawString(password.c_str(), 18, 31);
    
    // Cursor in password
    if ((int)(anim_time * 2) % 2 == 0) {
        int pw = canvas->textWidth(password.c_str());
        canvas->drawFastVLine(18 + pw, 26, 10, theme_get_accent());
    }

    int kb_y_start = 38;
    int key_w = 14;
    int key_h = 15;
    int spacing_x = 2;
    int spacing_y = 3;

    for (int r = 0; r < 5; r++) {
        int row_len = strlen(kb_layout[r]);
        int row_w = (r == 4) ? (80 + 40 + spacing_x) : (r == 3 ? (7 * 16 + 2 * 22 + 2 * 2) : (10 * 16 - 2));
        if (r == 3) row_w = 158; // Force to match top rows
        int sx = (canvas->width() - row_w) / 2;
        int sy = kb_y_start + r * (key_h + spacing_y);

        for (int c = 0; c < row_len; c++) {
            char k = kb_layout[r][c];
            int kw = (r == 4) ? (c == 0 ? 80 : 40) : ((r == 3 && (c == 0 || c == 8)) ? 22 : key_w);
            
            // Calc KX smarter for Row 3
            int kx;
            if (r == 4) {
                kx = (c == 1) ? (sx + 80 + spacing_x) : sx;
            } else if (r == 3) {
                if (c == 0) kx = sx;
                else if (c == 8) kx = sx + 22 + spacing_x + 7 * (key_w + spacing_x);
                else kx = sx + 22 + spacing_x + (c - 1) * (key_w + spacing_x);
            } else {
                kx = sx + c * (key_w + spacing_x);
            }
            
            bool is_selected = (r == kb_row && c == kb_col);
            uint16_t key_col = is_selected ? theme_get_accent() : blend_color(theme_get_text_dim(), theme_get_bg(), 0.15f);
            if (k == '[' && caps_lock) key_col = theme_get_accent(); // Caps lock active color
            uint16_t txt_col = (is_selected || (k == '[' && caps_lock)) ? theme_get_bg() : theme_get_text();

            canvas->fillRoundRect(kx, sy, kw, key_h, 3, key_col);
            if (!is_selected) canvas->drawRoundRect(kx, sy, kw, key_h, 3, theme_get_text_dim());
            
            canvas->setTextColor(txt_col);
            canvas->setTextDatum(textdatum_t::middle_center);
            
            char label[16] = {k, 0};
            if (r == 0 && caps_lock) label[0] = symbol_row[c];
            else if (k >= 'a' && k <= 'z' && caps_lock) label[0] -= 32;
            
            if (k == 'S') strcpy(label, "SPACE");
            if (k == '<') strcpy(label, "DEL");
            if (k == '>') strcpy(label, "ENTER");
            if (k == '[') strcpy(label, "CAPS");
            if (k == '!') strcpy(label, "V/H");
            if (k == ' ') strcpy(label, "");
            
            canvas->drawString(label, kx + kw/2, sy + key_h/2);
        }
    }
}

void App_AICode::draw_connecting(LGFX_Sprite* canvas) {
    draw_header(canvas, "CONNECTING...");
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(theme_get_text());
    canvas->drawString("PLEASE WAIT", canvas->width()/2, canvas->height()/2 - 10);
    
    // Loading bar
    int bw = 100;
    int bh = 6;
    int bx = (canvas->width() - bw) / 2;
    int by = canvas->height()/2 + 10;
    canvas->drawRect(bx-1, by-1, bw+2, bh+2, theme_get_text_dim());
    
    float p = fmodf(anim_time * 20.0f, 100.0f);
    canvas->fillRect(bx, by, (int)(p * bw / 100.0f), bh, theme_get_accent());
}

void App_AICode::draw_connected(LGFX_Sprite* canvas) {
    draw_header(canvas, "SUCCESS");
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->setTextColor(theme_get_accent());
    canvas->drawString("CONNECTED!", canvas->width()/2, canvas->height()/2 - 10);
    
    canvas->setTextColor(theme_get_text());
    canvas->drawString(target_ssid.c_str(), canvas->width()/2, canvas->height()/2 + 10);
    
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("PRESS SELECT TO OK", canvas->width()/2, canvas->height() - 15);
}

void App_AICode::draw_error(LGFX_Sprite* canvas) {
    draw_header(canvas, "FAILED");
    canvas->setTextColor(theme_get_fail());
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString("CONNECTION ERROR", canvas->width()/2, canvas->height()/2);
    
    canvas->setTextColor(theme_get_text_dim());
    canvas->drawString("CHECK PASSWORD", canvas->width()/2, canvas->height()/2 + 15);
}
