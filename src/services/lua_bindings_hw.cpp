/*
 * lua_bindings_hw.cpp — Hardware peripheral bindings for the Lua runtime
 * Part 2: gpio, i2c, spi, uart, adc, pwm, disp, fs, net
 *
 * These are the "Universal Peripheral" bindings described in the architecture.
 * The LLM can use any of these to drive arbitrary external hardware over
 * I2C/SPI/UART/GPIO without ever needing a C++ firmware update.
 */

// FreeRTOS must come first to avoid Xtensa type conflicts with Lua headers
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lua_engine.h"
#include "../config/sys_config.h"
#include "../platform/board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "../services/wifi_mgr.h"
#include "../drivers/led_driver.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>

static const char* TAG = "LuaBind";

// Blacklisted GPIO pins — the Lua sandbox cannot touch these
static const int s_blocked_pins[] = { 6, 7, 8, 9, 10, 11, 12, 13 }; // QSPI flash
static bool _pin_allowed(int pin) {
    for (size_t i = 0; i < sizeof(s_blocked_pins)/sizeof(s_blocked_pins[0]); i++)
        if (s_blocked_pins[i] == pin) return false;
    return pin >= 0 && pin <= 48;
}

// ─── gpio.* ──────────────────────────────────────────────────────────────────
static int l_gpio_mode(lua_State* L) {
    int pin  = luaL_checkinteger(L, 1);
    int mode = luaL_checkinteger(L, 2); // 0=input 1=output
    if (!_pin_allowed(pin)) { lua_pushboolean(L, 0); return 1; }
    gpio_set_direction((gpio_num_t)pin, mode ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    lua_pushboolean(L, 1); return 1;
}
static int l_gpio_write(lua_State* L) {
    int pin = luaL_checkinteger(L, 1);
    int val = luaL_checkinteger(L, 2);
    if (!_pin_allowed(pin)) return 0;
    gpio_set_level((gpio_num_t)pin, val); return 0;
}
static int l_gpio_read(lua_State* L) {
    int pin = luaL_checkinteger(L, 1);
    if (!_pin_allowed(pin)) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, gpio_get_level((gpio_num_t)pin)); return 1;
}

void LuaEngine::_bind_gpio(lua_State* L) {
    static const luaL_Reg m[] = {
        {"mode",  l_gpio_mode},
        {"write", l_gpio_write},
        {"read",  l_gpio_read},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "gpio");
}

static i2c_master_bus_handle_t s_i2c_bus[2] = {nullptr, nullptr};
static uint32_t s_i2c_freq[2] = {400000, 400000};

// WHY: i2c_master_transmit/receive take their timeout in MILLISECONDS and
// treat -1 as "wait forever". An unbounded wait lets a stuck bus hang the
// Lua worker until the 5s script watchdog kills the script mid-hardware-op;
// 100ms bounds a failed transfer far inside that budget while still
// tolerating slow clock-stretching devices.
static constexpr int LUA_I2C_XFER_TIMEOUT_MS = 100;

// ─── i2c.* ───────────────────────────────────────────────────────────────────
static int l_i2c_setup(lua_State* L) {
    int bus  = luaL_checkinteger(L, 1);
    if (bus < 0 || bus > 1) { lua_pushboolean(L, 0); return 1; }
    int sda  = luaL_checkinteger(L, 2);
    int scl  = luaL_checkinteger(L, 3);
    if (!_pin_allowed(sda) || !_pin_allowed(scl)) { lua_pushboolean(L, 0); return 1; }
    int freq = (int)luaL_optinteger(L, 4, 400000);
    
    if (!s_i2c_bus[bus]) {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = bus;
        bus_cfg.sda_io_num = (gpio_num_t)sda;
        bus_cfg.scl_io_num = (gpio_num_t)scl;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;
        
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus[bus]);
        if (err != ESP_OK) { lua_pushboolean(L, 0); return 1; }
    }
    s_i2c_freq[bus] = freq;
    lua_pushboolean(L, 1); return 1;
}

static int l_i2c_write(lua_State* L) {
    int bus  = luaL_checkinteger(L, 1);
    int addr = luaL_checkinteger(L, 2);
    size_t len; const char* data = luaL_checklstring(L, 3, &len);
    if (bus < 0 || bus > 1 || !s_i2c_bus[bus]) { lua_pushboolean(L, 0); return 1; }
    
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = s_i2c_freq[bus];
    
    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(s_i2c_bus[bus], &dev_cfg, &dev_handle) != ESP_OK) {
        lua_pushboolean(L, 0); return 1;
    }
    
    esp_err_t e = i2c_master_transmit(dev_handle, (const uint8_t*)data, len, LUA_I2C_XFER_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev_handle);
    lua_pushboolean(L, e == ESP_OK); return 1;
}

static int l_i2c_read(lua_State* L) {
    int bus  = luaL_checkinteger(L, 1);
    int addr = luaL_checkinteger(L, 2);
    int len  = luaL_checkinteger(L, 3);
    if (len <= 0 || len > 128 || bus < 0 || bus > 1 || !s_i2c_bus[bus]) { lua_pushnil(L); return 1; }
    
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = s_i2c_freq[bus];
    
    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(s_i2c_bus[bus], &dev_cfg, &dev_handle) != ESP_OK) {
        lua_pushnil(L); return 1;
    }
    
    uint8_t buf[128];
    esp_err_t e = i2c_master_receive(dev_handle, buf, len, LUA_I2C_XFER_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev_handle);
    
    if (e != ESP_OK) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)buf, len); return 1;
}

// i2c.probe(bus, addr) — attempt a 1-byte write to check if device ACKs
static int l_i2c_probe(lua_State* L) {
    int bus  = luaL_checkinteger(L, 1);
    int addr = luaL_checkinteger(L, 2);
    if (bus < 0 || bus > 1 || !s_i2c_bus[bus]) { lua_pushboolean(L, 0); return 1; }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = s_i2c_freq[bus];

    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(s_i2c_bus[bus], &dev_cfg, &dev_handle) != ESP_OK) {
        lua_pushboolean(L, 0); return 1;
    }

    uint8_t dummy = 0;
    esp_err_t e = i2c_master_transmit(dev_handle, &dummy, 1, LUA_I2C_XFER_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev_handle);

    lua_pushboolean(L, e == ESP_OK); return 1;
}

void LuaEngine::_bind_i2c(lua_State* L) {
    static const luaL_Reg m[] = {
        {"setup", l_i2c_setup},
        {"write", l_i2c_write},
        {"read",  l_i2c_read},
        {"probe", l_i2c_probe},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "i2c");
}

// ─── uart.* ──────────────────────────────────────────────────────────────────
static int l_uart_setup(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
    int tx   = luaL_checkinteger(L, 2);
    int rx   = luaL_checkinteger(L, 3);
    if (!_pin_allowed(tx) || !_pin_allowed(rx)) { lua_pushboolean(L, 0); return 1; }
    int baud = luaL_checkinteger(L, 4);
    uart_config_t cfg = {};
    cfg.baud_rate = baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_param_config((uart_port_t)port, &cfg);
    uart_set_pin((uart_port_t)port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    esp_err_t e = uart_driver_install((uart_port_t)port, 256, 0, 0, nullptr, 0);
    lua_pushboolean(L, e == ESP_OK || e == ESP_ERR_INVALID_STATE); return 1;
}
static int l_uart_write(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
    size_t len; const char* d = luaL_checklstring(L, 2, &len);
    uart_write_bytes((uart_port_t)port, d, len);
    return 0;
}
static int l_uart_read(lua_State* L) {
    int port    = luaL_checkinteger(L, 1);
    int max_len = luaL_checkinteger(L, 2);
    int timeout = (int)luaL_optinteger(L, 3, 100);
    if (max_len <= 0 || max_len > 256) { lua_pushnil(L); return 1; }
    uint8_t buf[256];
    int n = uart_read_bytes((uart_port_t)port, buf, max_len, pdMS_TO_TICKS(timeout));
    if (n <= 0) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)buf, n); return 1;
}

void LuaEngine::_bind_uart(lua_State* L) {
    static const luaL_Reg m[] = {
        {"setup", l_uart_setup},
        {"write", l_uart_write},
        {"read",  l_uart_read},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "uart");
}

// ─── adc.* ───────────────────────────────────────────────────────────────────
// WHY process-lifetime singleton: creating + deleting the ADC oneshot unit on
// every adc.read() call is heavy churn for scripts that sample in loops. This
// firmware claims ADC_UNIT_1 exclusively (no other module uses it), so the
// handle is created lazily on first use and intentionally never torn down —
// deleting it would buy nothing and risks racing an in-flight read. Bindings
// execute only on the Lua worker task, so no lock guards the init flag.
static adc_oneshot_unit_handle_t s_adc_unit = nullptr;

static int l_adc_read(lua_State* L) {
    int ch = luaL_checkinteger(L, 1);

    if (!s_adc_unit) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {};
        unit_cfg.unit_id = ADC_UNIT_1;
        if (adc_oneshot_new_unit(&unit_cfg, &s_adc_unit) != ESP_OK) {
            s_adc_unit = nullptr;
            ESP_LOGE(TAG, "ADC unit init failed");
            lua_pushnil(L); return 1;
        }
    }

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten   = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    int raw = 0;
    if (adc_oneshot_config_channel(s_adc_unit, (adc_channel_t)ch, &ch_cfg) != ESP_OK ||
        adc_oneshot_read(s_adc_unit, (adc_channel_t)ch, &raw) != ESP_OK) {
        lua_pushnil(L); return 1;
    }
    lua_pushinteger(L, raw); return 1;
}

void LuaEngine::_bind_adc(lua_State* L) {
    static const luaL_Reg m[] = {{"read", l_adc_read},{nullptr,nullptr}};
    luaL_newlib(L, m); lua_setglobal(L, "adc");
}

// ─── pwm.* ───────────────────────────────────────────────────────────────────
static int l_pwm_setup(lua_State* L) {
    int pin  = luaL_checkinteger(L, 1);
    int freq = luaL_checkinteger(L, 2);
    int ch   = luaL_optinteger(L, 3, 0);
    if (!_pin_allowed(pin)) { lua_pushboolean(L, 0); return 1; }
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = (uint32_t)freq;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t c = {};
    c.gpio_num = pin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = (ledc_channel_t)ch;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    c.hpoint = 0;
    ledc_channel_config(&c);
    lua_pushboolean(L, 1); return 1;
}
static int l_pwm_duty(lua_State* L) {
    int ch   = luaL_checkinteger(L, 1);
    int duty = luaL_checkinteger(L, 2);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    return 0;
}

void LuaEngine::_bind_pwm(lua_State* L) {
    static const luaL_Reg m[] = {{"setup",l_pwm_setup},{"set_duty",l_pwm_duty},{nullptr,nullptr}};
    luaL_newlib(L, m); lua_setglobal(L, "pwm");
}

// ─── fs.* ────────────────────────────────────────────────────────────────────
static bool _fs_path_allowed(const char* path) {
    if (!path) return false;
    if (strncmp(path, AI_BASE_DIR, strlen(AI_BASE_DIR)) != 0) return false;
    if (strstr(path, "..") != NULL) return false;
    return true;
}
static int l_fs_read(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!_fs_path_allowed(path)) { lua_pushnil(L); lua_pushstring(L, "access denied"); return 2; }
    FILE* f = fopen(path, "rb");
    if (!f) { lua_pushnil(L); lua_pushstring(L, "file not found"); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); lua_pushnil(L); return 1; }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
    lua_pushlstring(L, buf, sz); heap_caps_free(buf); return 1;
}
static int l_fs_write(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    size_t len; const char* data = luaL_checklstring(L, 2, &len);
    if (!_fs_path_allowed(path)) { lua_pushboolean(L, 0); return 1; }
    char tmp_path[128];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE* f = fopen(tmp_path, "wb");
    if (!f) { lua_pushboolean(L, 0); return 1; }
    bool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);
    if (!ok || rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        lua_pushboolean(L, 0); return 1;
    }
    lua_pushboolean(L, 1); return 1;
}
static int l_fs_list(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!_fs_path_allowed(path)) { lua_newtable(L); return 1; }
    DIR* d = opendir(path);
    lua_newtable(L);
    if (!d) return 1;
    struct dirent* e; int idx = 1;
    while ((e = readdir(d)) != nullptr) {
        lua_pushstring(L, e->d_name); lua_rawseti(L, -2, idx++);
    }
    closedir(d); return 1;
}

void LuaEngine::_bind_fs(lua_State* L) {
    static const luaL_Reg m[] = {
        {"read",  l_fs_read},
        {"write", l_fs_write},
        {"list",  l_fs_list},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "fs");
}

// ─── spi.* (stub — full init requires known MOSI/MISO/CLK) ──────────────────
void LuaEngine::_bind_spi(lua_State* L) {
    // SPI pin assignment is board-specific and not yet configurable from Lua.
    // Expose an empty table so scripts can check for availability.
    lua_newtable(L);
    lua_setglobal(L, "spi");
}

// ─── net.* ───────────────────────────────────────────────────────────────────
static int l_net_fetch(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    if (!url || url[0] == '\0') {
        lua_pushnil(L);
        lua_pushstring(L, "empty URL");
        return 2;
    }
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 10000;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    char* out = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int out_len = 0;
    if (out && esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        out_len = esp_http_client_read_response(h, out, 4095);
        if (out_len > 0) out[out_len] = '\0'; else out[0] = '\0';
    }
    esp_http_client_close(h); esp_http_client_cleanup(h);
    if (out && out_len > 0) lua_pushstring(L, out);
    else lua_pushnil(L);
    heap_caps_free(out); return 1;
}

void LuaEngine::_bind_net(lua_State* L) {
    static const luaL_Reg m[] = {{"fetch", l_net_fetch},{nullptr,nullptr}};
    luaL_newlib(L, m); lua_setglobal(L, "net");
}

// ─── disp.* (display bindings for Lua scripts) ─────────────────────────
// Lua scripts draw to a PSRAM framebuffer. The UI task pushes it to TFT
// when the LUA_RUNNING screen is active.

static uint16_t* s_lua_fb = nullptr;  // Lua framebuffer (RGB565)
static int s_lua_fb_w = 0;
static int s_lua_fb_h = 0;

// Helper: set pixel in framebuffer (bounds-checked)
static inline void _fb_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < s_lua_fb_w && y >= 0 && y < s_lua_fb_h && s_lua_fb) {
        s_lua_fb[y * s_lua_fb_w + x] = color;
    }
}

// Helper: fill rectangle in framebuffer
static inline void _fb_fill_rect(int rx, int ry, int rw, int rh, uint16_t color) {
    for (int y = ry; y < ry + rh && y < s_lua_fb_h; y++)
        for (int x = rx; x < rx + rw && x < s_lua_fb_w; x++)
            _fb_pixel(x, y, color);
}

// Helper: draw rectangle outline
static inline void _fb_draw_rect(int rx, int ry, int rw, int rh, uint16_t color) {
    for (int x = rx; x < rx + rw; x++) { _fb_pixel(x, ry, color); _fb_pixel(x, ry+rh-1, color); }
    for (int y = ry; y < ry + rh; y++) { _fb_pixel(rx, y, color); _fb_pixel(rx+rw-1, y, color); }
}

// disp.init() — allocate framebuffer
static int l_disp_init(lua_State* L) {
    if (s_lua_fb) return 0; // already init
    s_lua_fb_w = LAUNCHER_DISPLAY_W;
    s_lua_fb_h = LAUNCHER_DISPLAY_H;
    s_lua_fb = (uint16_t*)heap_caps_malloc(s_lua_fb_w * s_lua_fb_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_lua_fb) { lua_pushboolean(L, 0); return 1; }
    memset(s_lua_fb, 0, s_lua_fb_w * s_lua_fb_h * 2);
    lua_pushboolean(L, 1);
    return 1;
}

// disp.clear(color?) — fill screen
static int l_disp_clear(lua_State* L) {
    if (!s_lua_fb) return 0;
    uint16_t color = luaL_optinteger(L, 1, 0x0000);
    _fb_fill_rect(0, 0, s_lua_fb_w, s_lua_fb_h, color);
    return 0;
}

// disp.pixel(x, y, color)
static int l_disp_pixel(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    uint16_t color = luaL_checkinteger(L, 3);
    _fb_pixel(x, y, color);
    return 0;
}

// disp.rect(x, y, w, h, color) — outline
static int l_disp_rect(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    uint16_t color = luaL_checkinteger(L, 5);
    _fb_draw_rect(x, y, w, h, color);
    return 0;
}

// disp.fill(x, y, w, h, color) — filled rectangle
static int l_disp_fill(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    uint16_t color = luaL_checkinteger(L, 5);
    _fb_fill_rect(x, y, w, h, color);
    return 0;
}

/* 5x7 bitmap font — each character is 5 columns, 7 rows, stored as 5 bytes (MSB = top) */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 33 '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 34 '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 35 '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 36 '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 37 '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 38 '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 39 ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 40 '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 41 ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* 42 '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 43 '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 44 ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 45 '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 46 '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 47 '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 48 '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 49 '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 50 '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 51 '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 52 '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 53 '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 54 '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 55 '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 56 '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 57 '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 58 ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 59 ';' */
    {0x00,0x08,0x14,0x22,0x41}, /* 60 '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 61 '=' */
    {0x41,0x22,0x14,0x08,0x00}, /* 62 '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 63 '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 64 '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 65 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 66 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 67 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 68 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 69 'E' */
    {0x7F,0x09,0x09,0x01,0x01}, /* 70 'F' */
    {0x3E,0x41,0x41,0x51,0x32}, /* 71 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 72 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 73 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 74 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 75 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 76 'L' */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 77 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 78 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 79 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 80 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 81 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 82 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 83 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 84 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 85 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 86 'V' */
    {0x7F,0x20,0x18,0x20,0x7F}, /* 87 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 88 'X' */
    {0x03,0x04,0x78,0x04,0x03}, /* 89 'Y' */
    {0x61,0x51,0x49,0x49,0x46}, /* 90 'Z' */
    {0x00,0x00,0x7F,0x41,0x41}, /* 91 '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 92 '\' */
    {0x41,0x41,0x7F,0x00,0x00}, /* 93 ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 94 '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 95 '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 96 '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 97 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 98 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 99 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 100 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 101 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 102 'f' */
    {0x08,0x54,0x54,0x54,0x3C}, /* 103 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 104 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 105 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 106 'j' */
    {0x00,0x7F,0x10,0x28,0x44}, /* 107 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 108 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 109 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 110 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 111 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 112 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 113 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 114 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 115 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 116 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 117 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 118 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 119 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 120 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 121 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 122 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 123 '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 124 '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 125 '}' */
    {0x08,0x04,0x08,0x10,0x08}, /* 126 '~' */
};

// disp.text(x, y, str, color?, size?) — draw text with 5x7 bitmap font
static int l_disp_text(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char* str = luaL_checkstring(L, 3);
    uint16_t color = luaL_optinteger(L, 4, 0xFFFF);

    int cx = x;
    for (const char* p = str; *p; p++) {
        if (*p == '\n') { cx = x; y += 8; continue; }
        int ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) continue; /* skip non-printable */
        const uint8_t* glyph = font5x7[ch - 32];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x20 >> col)) {
                    _fb_pixel(cx + col, y + row, color);
                }
            }
        }
        cx += 6; /* 5px glyph + 1px spacing */
    }
    return 0;
}

// disp.line(x1, y1, x2, y2, color) — draw line
static int l_disp_line(lua_State* L) {
    int x1 = luaL_checkinteger(L, 1);
    int y1 = luaL_checkinteger(L, 2);
    int x2 = luaL_checkinteger(L, 3);
    int y2 = luaL_checkinteger(L, 4);
    uint16_t color = luaL_checkinteger(L, 5);
    // Bresenham's line algorithm
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        _fb_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
    return 0;
}

// disp.circle(cx, cy, r, color) — draw circle outline
static int l_disp_circle(lua_State* L) {
    int cx = luaL_checkinteger(L, 1);
    int cy = luaL_checkinteger(L, 2);
    int r = luaL_checkinteger(L, 3);
    uint16_t color = luaL_checkinteger(L, 4);
    // Midpoint circle algorithm
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        _fb_pixel(cx+x, cy+y, color); _fb_pixel(cx+y, cy+x, color);
        _fb_pixel(cx-y, cy+x, color); _fb_pixel(cx-x, cy+y, color);
        _fb_pixel(cx-x, cy-y, color); _fb_pixel(cx-y, cy-x, color);
        _fb_pixel(cx+y, cy-x, color); _fb_pixel(cx+x, cy-y, color);
        y++;
        if (err < 0) { err += 2*y + 1; }
        else { x--; err += 2*(y-x) + 1; }
    }
    return 0;
}

// disp.width() — return display width
static int l_disp_width(lua_State* L) {
    lua_pushinteger(L, s_lua_fb_w ? s_lua_fb_w : LAUNCHER_DISPLAY_W);
    return 1;
}

// disp.height() — return display height
static int l_disp_height(lua_State* L) {
    lua_pushinteger(L, s_lua_fb_h ? s_lua_fb_h : LAUNCHER_DISPLAY_H);
    return 1;
}

// disp.rgb(r, g, b) — create RGB565 color
static int l_disp_rgb(lua_State* L) {
    int r = luaL_checkinteger(L, 1);
    int g = luaL_checkinteger(L, 2);
    int b = luaL_checkinteger(L, 3);
    uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    lua_pushinteger(L, color);
    return 1;
}

// disp.get_fb() — return pointer to framebuffer for UI task
uint16_t* lua_disp_get_fb(void) { return s_lua_fb; }
int lua_disp_get_fb_w(void) { return s_lua_fb_w; }
int lua_disp_get_fb_h(void) { return s_lua_fb_h; }
void lua_disp_free_fb(void) {
    if (s_lua_fb) { heap_caps_free(s_lua_fb); s_lua_fb = nullptr; }
}

void LuaEngine::_bind_disp(lua_State* L) {
    static const luaL_Reg m[] = {
        {"init",    l_disp_init},
        {"clear",   l_disp_clear},
        {"pixel",   l_disp_pixel},
        {"rect",    l_disp_rect},
        {"fill",    l_disp_fill},
        {"text",    l_disp_text},
        {"line",    l_disp_line},
        {"circle",  l_disp_circle},
        {"width",   l_disp_width},
        {"height",  l_disp_height},
        {"rgb",     l_disp_rgb},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "disp");
}

// ─── wifi.* ────────────────────────────────────────────────────────────────
static int l_wifi_get_ip(lua_State* L) {
    lua_pushstring(L, wifi_mgr_get_ip());
    return 1;
}

static int l_wifi_get_rssi(lua_State* L) {
    lua_pushinteger(L, wifi_mgr_get_rssi());
    return 1;
}

// wifi.scan() — blocking WiFi scan, returns array of {ssid, rssi, channel, auth}
static int l_wifi_scan(lua_State* L) {
    // Start a blocking scan (max ~5s)
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        lua_newtable(L);
        return 1;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > MAX_WIFI_NETWORKS) count = MAX_WIFI_NETWORKS;
    if (count == 0) {
        lua_newtable(L);
        return 1;
    }

    wifi_ap_record_t* records = (wifi_ap_record_t*)heap_caps_malloc(
        count * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!records) {
        lua_newtable(L);
        return 1;
    }

    esp_wifi_scan_get_ap_records(&count, records);

    lua_newtable(L);
    for (uint16_t i = 0; i < count; i++) {
        lua_newtable(L);

        lua_pushstring(L, (const char*)records[i].ssid);
        lua_setfield(L, -2, "ssid");

        lua_pushinteger(L, records[i].rssi);
        lua_setfield(L, -2, "rssi");

        lua_pushinteger(L, records[i].primary);
        lua_setfield(L, -2, "channel");

        lua_pushinteger(L, (int)records[i].authmode);
        lua_setfield(L, -2, "auth");

        lua_rawseti(L, -2, i + 1);
    }

    heap_caps_free(records);
    return 1;
}

void LuaEngine::_bind_wifi(lua_State* L) {
    static const luaL_Reg m[] = {
        {"get_ip",   l_wifi_get_ip},
        {"get_rssi", l_wifi_get_rssi},
        {"scan",     l_wifi_scan},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "wifi");
}

// ─── led.* ──────────────────────────────────────────────────────────────────
static int l_led_set_rgb(lua_State* L) {
    int r = luaL_checkinteger(L, 1);
    int g = luaL_checkinteger(L, 2);
    int b = luaL_checkinteger(L, 3);
    led_driver_set_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    return 0;
}

static int l_led_off(lua_State* L) {
    led_driver_off();
    return 0;
}

void LuaEngine::_bind_led(lua_State* L) {
    static const luaL_Reg m[] = {
        {"set_rgb", l_led_set_rgb},
        {"off",     l_led_off},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "led");
}

// ─── Master binding registration ─────────────────────────────────────────────
void LuaEngine::_register_bindings() {
    _bind_sys(m_L);
    _bind_disp(m_L);
    _bind_fs(m_L);
    _bind_net(m_L);
    _bind_gpio(m_L);
    _bind_i2c(m_L);
    _bind_uart(m_L);
    _bind_adc(m_L);
    _bind_pwm(m_L);
    _bind_spi(m_L);
    _bind_wifi(m_L);
    _bind_led(m_L);
    ESP_LOGI(TAG, "All hardware bindings registered");
}
