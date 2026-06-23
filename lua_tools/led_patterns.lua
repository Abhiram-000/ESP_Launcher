-- LED Patterns Tool
-- Runs various LED animation patterns on the NeoPixel

function run()
    sys.print("=== LED Patterns ===")
    sys.print("Running patterns for 10 seconds...")
    sys.print("")

    -- Rainbow cycle
    sys.print("Pattern 1: Rainbow")
    for cycle = 1, 3 do
        for hue = 0, 255, 5 do
            local r, g, b = hsv_to_rgb(hue, 255, 128)
            led.set_rgb(r, g, b)
            sys.delay(20)
        end
    end

    -- Breathing
    sys.print("Pattern 2: Breathing")
    for i = 1, 40 do
        local brightness = math.floor(128 + 127 * math.sin(i * 0.15))
        led.set_rgb(brightness, 0, brightness / 2)
        sys.delay(50)
    end

    -- Strobe
    sys.print("Pattern 3: Strobe")
    for i = 1, 10 do
        led.set_rgb(255, 255, 255)
        sys.delay(50)
        led.set_rgb(0, 0, 0)
        sys.delay(50)
    end

    -- Off
    led.set_rgb(0, 0, 0)
    sys.print("")
    sys.print("Patterns complete.")
end

-- Helper: HSV to RGB conversion
function hsv_to_rgb(h, s, v)
    h = h / 255.0 * 6.0
    local i = math.floor(h)
    local f = h - i
    local p = v * (1 - s / 255.0)
    local q = v * (1 - s / 255.0 * f)
    local t = v * (1 - s / 255.0 * (1 - f))
    v = v
    if i == 0 then return v, t, p
    elseif i == 1 then return q, v, p
    elseif i == 2 then return p, v, t
    elseif i == 3 then return p, q, v
    elseif i == 4 then return t, p, v
    else return v, p, q end
end

run()
