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

#include <stdio.h>
#include <string.h>
#include <dirent.h>

static const char* TAG = "LuaBind";

// Blacklisted GPIO pins — the Lua sandbox cannot touch these
static const int BLOCKED_PINS[] = { 6, 7, 8, 9, 10, 11, 12, 13 }; // QSPI flash
static bool _pin_allowed(int pin) {
    for (size_t i = 0; i < sizeof(BLOCKED_PINS)/sizeof(BLOCKED_PINS[0]); i++)
        if (BLOCKED_PINS[i] == pin) return false;
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

// ─── i2c.* ───────────────────────────────────────────────────────────────────
static int l_i2c_setup(lua_State* L) {
    int bus  = luaL_checkinteger(L, 1);
    if (bus < 0 || bus > 1) { lua_pushboolean(L, 0); return 1; }
    int sda  = luaL_checkinteger(L, 2);
    int scl  = luaL_checkinteger(L, 3);
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
    
    esp_err_t e = i2c_master_transmit(dev_handle, (const uint8_t*)data, len, -1);
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
    
    uint8_t* buf = (uint8_t*)alloca(len);
    esp_err_t e = i2c_master_receive(dev_handle, buf, len, -1);
    i2c_master_bus_rm_device(dev_handle);
    
    if (e != ESP_OK) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)buf, len); return 1;
}

void LuaEngine::_bind_i2c(lua_State* L) {
    static const luaL_Reg m[] = {
        {"setup", l_i2c_setup},
        {"write", l_i2c_write},
        {"read",  l_i2c_read},
        {nullptr, nullptr}
    };
    luaL_newlib(L, m); lua_setglobal(L, "i2c");
}

// ─── uart.* ──────────────────────────────────────────────────────────────────
static int l_uart_setup(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
    int tx   = luaL_checkinteger(L, 2);
    int rx   = luaL_checkinteger(L, 3);
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
    uint8_t* buf = (uint8_t*)alloca(max_len);
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
static int l_adc_read(lua_State* L) {
    int ch = luaL_checkinteger(L, 1);
    adc_oneshot_unit_handle_t hdl;
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    adc_oneshot_new_unit(&unit_cfg, &hdl);
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten   = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    adc_oneshot_config_channel(hdl, (adc_channel_t)ch, &ch_cfg);
    int raw = 0;
    adc_oneshot_read(hdl, (adc_channel_t)ch, &raw);
    adc_oneshot_del_unit(hdl);
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
    // Scripts must stay within /games/ai/
    return strncmp(path, AI_BASE_DIR, strlen(AI_BASE_DIR)) == 0;
}
static int l_fs_read(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!_fs_path_allowed(path)) { lua_pushnil(L); lua_pushstring(L, "access denied"); return 2; }
    FILE* f = fopen(path, "rb");
    if (!f) { lua_pushnil(L); lua_pushstring(L, "file not found"); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(f); lua_pushnil(L); return 1; }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
    lua_pushlstring(L, buf, sz); heap_caps_free(buf); return 1;
}
static int l_fs_write(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    size_t len; const char* data = luaL_checklstring(L, 2, &len);
    if (!_fs_path_allowed(path)) { lua_pushboolean(L, 0); return 1; }
    FILE* f = fopen(path, "wb");
    if (!f) { lua_pushboolean(L, 0); return 1; }
    fwrite(data, 1, len, f); fclose(f);
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
void LuaEngine::_bind_spi(lua_State* /*L*/) { /* TODO: implement in next iteration */ }

// ─── net.* ───────────────────────────────────────────────────────────────────
static int l_net_fetch(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 10000;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    char* out = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
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

// ─── disp.* (minimal display bindings for Lua scripts) ──────────────────────
void LuaEngine::_bind_disp(lua_State* /*L*/) { /* TODO: route through ui_task queue */ }

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
    ESP_LOGI(TAG, "All hardware bindings registered");
}
