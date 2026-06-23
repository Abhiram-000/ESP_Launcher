-- Network Weather Station
-- Fetches weather from Open-Meteo API and displays on screen
-- Uses: net.fetch(), disp.*, sys.*

function run()
    sys.print("=== Weather Station ===")

    -- Initialize display
    disp.init()
    local W = disp.width()
    local H = disp.height()

    -- Colors
    local BG     = disp.rgb(10, 12, 28)
    local TEXT   = disp.rgb(255, 255, 255)
    local DIM    = disp.rgb(100, 120, 160)
    local ACCENT = disp.rgb(0, 200, 255)
    local WARM   = disp.rgb(255, 100, 50)
    local COOL   = disp.rgb(50, 150, 255)

    -- Clear screen
    disp.fill(0, 0, W, H, BG)

    -- Header
    disp.fill(0, 0, W, 18, disp.rgb(20, 25, 50))
    disp.text(4, 4, "WEATHER STATION", ACCENT)

    -- Fetch weather data
    sys.print("Fetching weather...")
    disp.text(4, 24, "Fetching...", DIM)

    local url = "https://api.open-meteo.com/v1/forecast?latitude=16.5&longitude=78.5&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code&timezone=Asia/Kolkata"
    local resp = net.fetch(url)

    if not resp then
        disp.fill(0, 20, W, H-20, BG)
        disp.text(4, 30, "Network error!", disp.rgb(255, 80, 80))
        disp.text(4, 45, "Check WiFi connection", DIM)
        sys.print("Failed to fetch weather")
        return
    end

    sys.print("Weather data received")
    disp.fill(0, 20, W, H-20, BG)

    -- Parse JSON (simple extraction)
    local temp = "??"
    local humidity = "??"
    local wind = "??"
    local code = "??"

    local t = resp:find('"temperature_2m":')
    if t then
        local s = resp:sub(t+18)
        local e = s:find('[,%}]')
        if e then temp = s:sub(1, e-1) end
    end

    local h = resp:find('"relative_humidity_2m":')
    if h then
        local s = resp:sub(h+23)
        local e = s:find('[,%}]')
        if e then humidity = s:sub(1, e-1) end
    end

    local w = resp:find('"wind_speed_10m":')
    if w then
        local s = resp:sub(w+18)
        local e = s:find('[,%}]')
        if e then wind = s:sub(1, e-1) end
    end

    local c = resp:find('"weather_code":')
    if c then
        local s = resp:sub(c+15)
        local e = s:find('[,%}]')
        if e then code = s:sub(1, e-1) end
    end

    sys.print("Temp: " .. temp .. "C  Humidity: " .. humidity .. "%")

    -- Weather code to description
    local weather_desc = "Unknown"
    local code_num = tonumber(code) or 0
    if code_num == 0 then weather_desc = "Clear Sky"
    elseif code_num <= 3 then weather_desc = "Partly Cloudy"
    elseif code_num <= 48 then weather_desc = "Foggy"
    elseif code_num <= 57 then weather_desc = "Drizzle"
    elseif code_num <= 67 then weather_desc = "Rain"
    elseif code_num <= 77 then weather_desc = "Snow"
    elseif code_num <= 82 then weather_desc = "Showers"
    elseif code_num <= 86 then weather_desc = "Snow Showers"
    elseif code_num >= 95 then weather_desc = "Thunderstorm"
    end

    -- Draw temperature (large)
    disp.fill(4, 22, W-8, 40, BG)
    local temp_str = temp .. " C"
    disp.text(10, 25, temp_str, tonumber(temp) and tonumber(temp) > 25 and WARM or COOL)

    -- Draw weather description
    disp.text(4, 50, weather_desc, TEXT)

    -- Draw humidity
    disp.text(4, 65, "Humidity: " .. humidity .. "%", DIM)

    -- Draw wind speed
    disp.text(4, 78, "Wind: " .. wind .. " km/h", DIM)

    -- Draw location
    disp.fill(0, H-14, W, 14, disp.rgb(20, 25, 50))
    disp.text(4, H-12, "Hyderabad, India", DIM)

    -- Draw decorative line
    disp.line(0, 20, W, 20, ACCENT)

    sys.print("Display updated. Press any key to exit.")
    sys.delay(10000)
end

run()
