-- System Dashboard
-- Real-time system monitoring with live display updates
-- Uses: sys.*, wifi.*, disp.*, gpio.*

function run()
    sys.print("=== System Dashboard ===")

    disp.init()
    local W = disp.width()
    local H = disp.height()

    local BG      = disp.rgb(8, 10, 20)
    local PANEL   = disp.rgb(15, 20, 35)
    local TEXT    = disp.rgb(220, 230, 240)
    local DIM     = disp.rgb(80, 100, 130)
    local GREEN   = disp.rgb(0, 220, 100)
    local YELLOW  = disp.rgb(255, 200, 0)
    local RED     = disp.rgb(255, 80, 80)
    local ACCENT  = disp.rgb(0, 180, 255)

    -- Header
    disp.fill(0, 0, W, 16, disp.rgb(12, 16, 30))
    disp.text(4, 4, "SYSTEM DASHBOARD", ACCENT)

    -- Run for 30 seconds with live updates
    for frame = 1, 60 do
        -- Clear content area
        disp.fill(0, 17, W, H-17, BG)

        local y = 20

        -- Free Heap
        local heap = sys.free_heap()
        local heap_kb = math.floor(heap / 1024)
        disp.text(4, y, "Free Heap:", DIM)
        local heap_color = heap_kb > 200 and GREEN or (heap_kb > 100 and YELLOW or RED)
        disp.text(70, y, heap_kb .. " KB", heap_color)
        y = y + 12

        -- Heap bar
        local bar_w = W - 8
        local bar_h = 4
        local heap_pct = math.min(heap_kb / 800, 1.0)
        disp.fill(4, y, bar_w, bar_h, disp.rgb(30, 30, 40))
        disp.fill(4, y, math.floor(bar_w * heap_pct), bar_h, heap_color)
        y = y + 8

        -- WiFi Status
        local ip = wifi.get_ip()
        disp.text(4, y, "WiFi:", DIM)
        if ip and ip ~= "0.0.0.0" then
            disp.text(40, y, "Connected", GREEN)
            y = y + 12
            disp.text(4, y, "IP: " .. ip, TEXT)
            local rssi = wifi.get_rssi() or 0
            y = y + 12
            disp.text(4, y, "Signal: " .. rssi .. " dBm", rssi > -60 and GREEN or (rssi > -80 and YELLOW or RED))
        else
            disp.text(40, y, "Disconnected", RED)
        end
        y = y + 16

        -- Uptime
        local ms = sys.millis()
        local sec = math.floor(ms / 1000)
        local min = math.floor(sec / 60)
        local hrs = math.floor(min / 60)
        disp.text(4, y, "Uptime:", DIM)
        disp.text(55, y, string.format("%dh %dm %ds", hrs, min % 60, sec % 60), TEXT)
        y = y + 16

        -- LED status indicator
        local led_state = frame % 2 == 0
        gpio.mode(48, 1)
        gpio.write(48, led_state and 1 or 0)
        disp.text(4, y, "LED:", DIM)
        disp.text(35, y, led_state and "ON" or "OFF", led_state and GREEN or DIM)

        -- Frame counter
        disp.fill(0, H-12, W, 12, disp.rgb(12, 16, 30))
        disp.text(4, H-10, string.format("Frame: %d/60", frame), DIM)

        sys.delay(500)
    end

    -- Cleanup
    gpio.write(48, 0)
    sys.print("Dashboard complete.")
end

run()
