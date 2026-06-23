-- WiFi Scanner Tool
-- Scans nearby WiFi networks and displays results
-- Uses the wifi binding for scanning

function run()
    sys.print("=== WiFi Scanner ===")
    sys.print("Scanning networks...")
    sys.delay(500)

    local nets = wifi.scan()
    if not nets or #nets == 0 then
        sys.print("No networks found")
        return
    end

    sys.print(string.format("Found %d networks:", #nets))
    sys.print("")

    for i, net in ipairs(nets) do
        local enc = "Open"
        if net.auth ~= 0 then enc = "Secured" end
        sys.print(string.format("%2d. %-32s %4ddBm  Ch%d  %s",
            i, net.ssid or "?", net.rssi or 0, net.channel or 0, enc))
    end

    sys.print("")
    sys.print("Scan complete.")
end

run()
