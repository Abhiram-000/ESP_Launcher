-- Network Speed Test
-- Downloads a test file and measures throughput
-- Uses: net.fetch(), disp.*, sys.*

function run()
    sys.print("=== Network Speed Test ===")

    disp.init()
    local W = disp.width()
    local H = disp.height()

    local BG     = disp.rgb(8, 10, 20)
    local TEXT   = disp.rgb(220, 230, 240)
    local DIM    = disp.rgb(80, 100, 130)
    local GREEN  = disp.rgb(0, 220, 100)
    local YELLOW = disp.rgb(255, 200, 0)
    local ACCENT = disp.rgb(0, 180, 255)

    -- Header
    disp.fill(0, 0, W, 16, disp.rgb(12, 16, 30))
    disp.text(4, 4, "SPEED TEST", ACCENT)

    -- Test URLs (small files for measurement)
    local test_urls = {
        {"Google", "https://www.google.com/generate_204"},
        {"GitHub", "https://raw.githubusercontent.com/Abhiram9641/ESP32_Store/main/firmware_catalog.json"},
    }

    local results = {}

    for i, test in ipairs(test_urls) do
        local name, url = test[1], test[2]

        disp.fill(0, 18, W, H-18, BG)
        disp.text(4, 22, "Testing: " .. name, TEXT)
        disp.text(4, 36, "Downloading...", DIM)

        local start_ms = sys.millis()
        local resp = net.fetch(url)
        local end_ms = sys.millis()

        if resp then
            local elapsed_s = (end_ms - start_ms) / 1000
            local size_bytes = #resp
            local speed_kbps = 0
            if elapsed_s > 0 then
                speed_kbps = (size_bytes / 1024) / elapsed_s
            end

            results[i] = {name=name, size=size_bytes, time=elapsed_s, speed=speed_kbps}

            sys.print(string.format("%s: %d bytes in %.2fs = %.1f KB/s", name, size_bytes, elapsed_s, speed_kbps))

            -- Draw result
            disp.fill(0, 50, W, 30, BG)
            disp.text(4, 52, string.format("Size: %d bytes", size_bytes), TEXT)
            disp.text(4, 64, string.format("Time: %.2f s", elapsed_s), TEXT)
            disp.text(4, 76, string.format("Speed: %.1f KB/s", speed_kbps), GREEN)
        else
            results[i] = {name=name, size=0, time=0, speed=0}
            disp.text(4, 52, "Failed!", disp.rgb(255, 80, 80))
        end

        sys.delay(1500)
    end

    -- Summary
    disp.fill(0, 18, W, H-18, BG)
    disp.text(4, 20, "RESULTS", ACCENT)
    disp.line(0, 32, W, 32, DIM)

    local y = 36
    local total_speed = 0
    for _, r in ipairs(results) do
        local speed_color = r.speed > 50 and GREEN or (r.speed > 10 and YELLOW or disp.rgb(255, 150, 50))
        disp.text(4, y, string.format("%s: %.1f KB/s", r.name, r.speed), speed_color)
        y = y + 14
        total_speed = total_speed + r.speed
    end

    local avg_speed = total_speed / #results
    disp.line(0, y, W, y, DIM)
    y = y + 4
    disp.text(4, y, string.format("Average: %.1f KB/s", avg_speed), ACCENT)

    sys.print(string.format("Average speed: %.1f KB/s", avg_speed))
    sys.print("Speed test complete.")
    sys.delay(5000)
end

run()
