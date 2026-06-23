-- I2C Device Scanner with Display
-- Scans I2C bus and shows results on screen
-- Uses: i2c.*, disp.*, sys.*

function run()
    sys.print("=== I2C Device Scanner ===")

    disp.init()
    local W = disp.width()
    local H = disp.height()

    local BG     = disp.rgb(8, 10, 20)
    local TEXT   = disp.rgb(220, 230, 240)
    local DIM    = disp.rgb(80, 100, 130)
    local GREEN  = disp.rgb(0, 220, 100)
    local ACCENT = disp.rgb(0, 180, 255)
    local YELLOW = disp.rgb(255, 200, 0)

    -- Header
    disp.fill(0, 0, W, 16, disp.rgb(12, 16, 30))
    disp.text(4, 4, "I2C BUS SCANNER", ACCENT)

    -- Setup I2C on bus 0 (common pins: SDA=21, SCL=22)
    disp.fill(0, 18, W, H-18, BG)
    disp.text(4, 22, "Initializing I2C...", DIM)

    local ok = i2c.setup(0, 21, 22, 100000)
    if not ok then
        disp.text(4, 36, "I2C init failed!", disp.rgb(255, 80, 80))
        disp.text(4, 50, "Check SDA/SCL wiring", DIM)
        sys.print("I2C setup failed")
        return
    end

    -- Scan
    disp.fill(0, 18, W, H-18, BG)
    disp.text(4, 22, "Scanning...", TEXT)

    local devices = {}
    local y = 36

    for addr = 0x03, 0x77 do
        local found = i2c.probe(0, addr)
        if found then
            local name = "Unknown"
            if addr == 0x27 or addr == 0x3F then name = "LCD 16x2"
            elseif addr == 0x48 or addr == 0x49 then name = "ADS1115/Temp"
            elseif addr == 0x50 then name = "EEPROM"
            elseif addr == 0x57 then name = "AT24C32"
            elseif addr == 0x68 then name = "DS3231/MPU6050"
            elseif addr == 0x76 or addr == 0x77 then name = "BME280/BMP280"
            elseif addr == 0x3C or addr == 0x3D then name = "SSD1306 OLED"
            end

            table.insert(devices, {addr=addr, name=name})

            if y + 12 < H - 16 then
                local addr_str = string.format("0x%02X", addr)
                disp.text(4, y, addr_str, GREEN)
                disp.text(40, y, name, TEXT)
                y = y + 12
            end

            sys.print(string.format("Found: 0x%02X (%d) - %s", addr, addr, name))
        end
        sys.delay(5)
    end

    -- Footer
    disp.fill(0, H-14, W, 14, disp.rgb(12, 16, 30))
    local count_str = string.format("Found %d device(s)", #devices)
    disp.text(4, H-11, count_str, #devices > 0 and GREEN or YELLOW)

    sys.print(count_str)
    sys.print("I2C scan complete.")
    sys.delay(5000)
end

run()
