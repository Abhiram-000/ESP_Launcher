# Lua Tool Marketplace — ESP32-S3 Launcher

Downloadable Lua scripts that run directly on your ESP32-S3 device. Access via **App_AICode → Marketplace**.

## Available Tools

| Tool | Description | Uses |
|------|-------------|------|
| **Weather Station** | Fetches live weather from Open-Meteo API, displays temperature, humidity, wind on screen | `net.fetch()`, `disp.*` |
| **ADC Signal Analyzer** | Samples ADC pin and displays real-time waveform on screen | `adc.read()`, `disp.*` |
| **System Dashboard** | Live CPU, heap, WiFi, uptime stats with animated LED indicator | `sys.*`, `wifi.*`, `gpio.*` |
| **WiFi Signal Meter** | Visual signal strength bars with dBm readings | `wifi.*`, `disp.*` |
| **RGB Color Mixer** | Interactive color picker using 3 ADC pots, displays color preview | `adc.read()`, `disp.*` |
| **Network Speed Test** | Downloads test files and measures throughput in KB/s | `net.fetch()`, `disp.*` |
| **I2C Device Scanner** | Scans I2C bus, identifies devices, shows results on screen | `i2c.*`, `disp.*` |

## Installation

1. Open **App_AICode** on your ESP32
2. Select **Marketplace** from the main menu
3. Browse tools — each shows name, version, and description
4. Select a tool → press **Install**
5. Go to **Scripts** → new tool appears in the list
6. Select and **Run** the tool

## Display API (disp.*)

All display tools use the `disp` binding to draw graphics on the TFT screen:

```lua
disp.init()                    -- Allocate framebuffer
disp.clear(color)              -- Fill screen with color
disp.pixel(x, y, color)       -- Draw single pixel
disp.line(x1, y1, x2, y2, c)  -- Draw line
disp.rect(x, y, w, h, color)  -- Draw rectangle outline
disp.fill(x, y, w, h, color)  -- Fill rectangle
disp.circle(cx, cy, r, color) -- Draw circle outline
disp.text(x, y, str, color)   -- Draw text (6x8 font)
disp.rgb(r, g, b)             -- Create RGB565 color (0-255 each)
disp.width()                   -- Get display width
disp.height()                  -- Get display height
```

### Color Examples
```lua
local WHITE  = disp.rgb(255, 255, 255)
local RED    = disp.rgb(255, 0, 0)
local GREEN  = disp.rgb(0, 255, 0)
local BLUE   = disp.rgb(0, 0, 255)
local CYAN   = disp.rgb(0, 200, 255)
local YELLOW = disp.rgb(255, 200, 0)
local BG     = disp.rgb(10, 12, 28)  -- Dark background
```

## Hardware APIs Available

| Module | Functions |
|--------|-----------|
| `sys` | `delay(ms)`, `millis()`, `free_heap()`, `print(msg)` |
| `gpio` | `mode(pin, mode)`, `write(pin, val)`, `read(pin)` |
| `i2c` | `setup(bus, sda, scl, freq)`, `write(bus, addr, data)`, `read(bus, addr, len)` |
| `adc` | `read(channel)` — returns 0-4095 (12-bit) |
| `pwm` | `setup(pin, freq)`, `set_duty(channel, duty)` |
| `uart` | `setup(port, tx, rx, baud)`, `write(port, data)`, `read(port)` |
| `wifi` | `scan()`, `get_ip()`, `get_rssi()` |
| `net` | `fetch(url)` — HTTP GET, returns response string |
| `fs` | `read(path)`, `write(path, data)`, `list(path)` — sandboxed to `/storage/ai/` |
| `led` | `set_rgb(r, g, b)` — NeoPixel LED |
| `disp` | `init()`, `clear()`, `pixel()`, `line()`, `rect()`, `fill()`, `circle()`, `text()`, `rgb()`, `width()`, `height()` |

## Creating Your Own Tools

### Template
```lua
-- My Tool
-- Description of what it does

function run()
    -- Initialize display (optional)
    disp.init()
    disp.clear(disp.rgb(10, 12, 28))
    disp.text(4, 4, "My Tool", disp.rgb(0, 200, 255))

    -- Your logic here
    local val = adc.read(0)
    disp.text(4, 20, "ADC: " .. val, disp.rgb(255, 255, 255))

    sys.print("Done!")
    sys.delay(5000)
end

run()
```

### Publishing to the Marketplace

1. Create your `.lua` file following the template above
2. Add an entry to `lua_tools_catalog.json`:
```json
{
    "id": "my_tool",
    "name": "My Tool",
    "version": "1.0.0",
    "description": "What my tool does",
    "author": "YourName",
    "url": "https://raw.githubusercontent.com/YOUR_USER/YOUR_REPO/main/lua_tools/my_tool.lua",
    "size_bytes": 1024,
    "tags": ["category"]
}
```
3. Push to GitHub
4. The ESP32 will automatically discover it in the Marketplace

## Troubleshooting

- **"No tools found"** — Check WiFi connection, press R to refresh
- **"Script too large"** — Max 16KB per script
- **Display not showing** — Some tools need `disp.init()` at the start
- **Network error** — Ensure WiFi is connected before running network tools

## License

MIT — Free to use, modify, and distribute.
