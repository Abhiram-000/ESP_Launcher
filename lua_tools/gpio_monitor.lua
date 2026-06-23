-- GPIO Monitor Tool
-- Monitors GPIO pin states in real-time

function run()
    sys.print("=== GPIO Monitor ===")
    sys.print("Monitoring pins for 10 seconds...")
    sys.print("")

    -- Monitor common GPIO pins
    local pins = {3, 4, 5, 15, 16, 17, 18}
    local pin_names = {
        [3] = "UP", [4] = "BL", [5] = "GPIO5",
        [15] = "RIGHT", [16] = "SELECT", [17] = "DOWN", [18] = "LEFT"
    }

    for iteration = 1, 20 do
        local line = ""
        for _, pin in ipairs(pins) do
            local val = gpio.read(pin)
            local label = pin_names[pin] or string.format("P%d", pin)
            local state = val == 1 and "H" or "L"
            line = line .. string.format("%s:%s ", label, state)
        end
        sys.print(line)
        sys.delay(500)
    end

    sys.print("")
    sys.print("Monitor complete.")
end

run()
