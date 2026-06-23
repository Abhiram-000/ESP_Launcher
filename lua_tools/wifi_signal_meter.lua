-- WiFi Signal Strength Meter
-- Visual signal bars with real-time updates
-- Uses: wifi.*, disp.*, sys.*

function run()
    sys.print("=== WiFi Signal Meter ===")

    disp.init()
    local W = disp.width()
    local H = disp.height()

    local BG     = disp.rgb(8, 10, 20)
    local TEXT   = disp.rgb(220, 230, 240)
    local DIM    = disp.rgb(80, 100, 130)
    local GREEN  = disp.rgb(0, 220, 100)
    local YELLOW = disp.rgb(255, 200, 0)
    local RED    = disp.rgb(255, 80, 80)
    local ACCENT = disp.rgb(0, 180, 255)

    -- Header
    disp.fill(0, 0, W, 16, disp.rgb(12, 16, 30))
    disp.text(4, 4, "WIFI SIGNAL METER", ACCENT)

    for frame = 1, 40 do
        disp.fill(0, 17, W, H-17, BG)

        local ip = wifi.get_ip()
        local rssi = wifi.get_rssi() or -100

        if not ip or ip == "0.0.0.0" then
            disp.text(10, 40, "Not Connected", RED)
            disp.text(10, 55, "Connect to WiFi first", DIM)
        else
            -- Signal strength classification
            local strength = "Excellent"
            local sig_color = GREEN
            if rssi < -80 then strength = "Weak"; sig_color = RED
            elseif rssi < -60 then strength = "Fair"; sig_color = YELLOW
            elseif rssi < -40 then strength = "Good"; sig_color = GREEN
            end

            -- Draw signal bars (5 bars, increasing height)
            local bar_w = 12
            local bar_gap = 4
            local bars_start_x = (W - (5 * bar_w + 4 * bar_gap)) / 2
            local max_bar_h = H - 60
            local bar_base_y = H - 20

            for i = 1, 5 do
                local bar_h = math.floor(max_bar_h * i / 5)
                local bar_x = bars_start_x + (i-1) * (bar_w + bar_gap)
                local bar_y = bar_base_y - bar_h

                -- Determine if this bar should be lit
                local threshold = -30 - (5-i) * 15  -- -30, -45, -60, -75, -90
                local lit = rssi > threshold

                if lit then
                    disp.fill(bar_x, bar_y, bar_w, bar_h, sig_color)
                else
                    disp.fill(bar_x, bar_y, bar_w, bar_h, disp.rgb(25, 30, 40))
                end
                disp.rect(bar_x, bar_y, bar_w, bar_h, DIM)
            end

            -- Signal info text
            disp.text(4, 20, "Signal: " .. strength, sig_color)
            disp.text(4, 34, "RSSI: " .. rssi .. " dBm", TEXT)
            disp.text(4, 48, "IP: " .. ip, DIM)

            -- dBm scale on right
            disp.text(W-30, 20, "-30", DIM)
            disp.text(W-30, 34, "-60", DIM)
            disp.text(W-30, 48, "-90", DIM)
        end

        -- Frame info
        disp.fill(0, H-12, W, 12, disp.rgb(12, 16, 30))
        disp.text(4, H-10, string.format("Sample: %d/40", frame), DIM)

        sys.delay(750)
    end

    sys.print("Signal meter complete.")
end

run()
