-- System Info Tool
-- Displays comprehensive system information

function run()
    sys.print("=== System Info ===")
    sys.print("")

    -- Free heap
    local heap = sys.free_heap()
    sys.print(string.format("Free Heap:    %d KB", heap / 1024))

    -- Uptime
    local ms = sys.millis()
    local sec = ms / 1000
    local min = sec / 60
    local hrs = min / 60
    sys.print(string.format("Uptime:       %dh %dm %ds", hrs, min % 60, sec % 60))

    -- WiFi status
    local ip = wifi.get_ip()
    if ip and ip ~= "0.0.0.0" then
        sys.print(string.format("WiFi IP:      %s", ip))
        local rssi = wifi.get_rssi()
        sys.print(string.format("WiFi Signal:  %d dBm", rssi or 0))
    else
        sys.print("WiFi:         Disconnected")
    end

    sys.print("")
    sys.print(string.format("Free PSRAM:   %d KB", heap / 1024))
    sys.print("Lua Engine:   Active")
    sys.print("Platform:     ESP32-S3")
    sys.print("")
    sys.print("System info complete.")
end

run()
