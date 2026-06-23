-- RGB Color Mixer
-- Interactive color picker using ADC for RGB adjustment
-- Uses: adc.*, disp.*, sys.*

function run()
    sys.print("=== RGB Color Mixer ===")

    disp.init()
    local W = disp.width()
    local H = disp.height()

    local BG   = disp.rgb(15, 15, 20)
    local TEXT = disp.rgb(200, 210, 220)
    local DIM  = disp.rgb(80, 90, 100)

    -- Start with white
    local r, g, b = 255, 255, 255

    for sample = 1, 100 do
        -- Read ADC channels for R, G, B
        local adc_r = adc.read(0)
        local adc_g = adc.read(3)
        local adc_b = adc.read(6)

        -- Map 0-4095 to 0-255
        r = math.floor(adc_r / 16)
        g = math.floor(adc_g / 16)
        b = math.floor(adc_b / 16)

        local color = disp.rgb(r, g, b)

        -- Clear screen
        disp.fill(0, 0, W, H, BG)

        -- Header
        disp.fill(0, 0, W, 14, disp.rgb(25, 25, 30))
        disp.text(4, 3, "RGB COLOR MIXER", disp.rgb(150, 160, 170))

        -- Large color preview box
        local box_size = math.min(W - 20, H - 70)
        local box_x = (W - box_size) / 2
        local box_y = 18
        disp.fill(box_x, box_y, box_size, box_size, color)
        disp.rect(box_x, box_y, box_size, box_size, DIM)

        -- Color values
        local info_y = box_y + box_size + 6
        disp.text(4, info_y, string.format("R: %3d (ADC:%d)", r, adc_r), disp.rgb(255, 80, 80))
        disp.text(4, info_y + 12, string.format("G: %3d (ADC:%d)", g, adc_g), disp.rgb(80, 255, 80))
        disp.text(4, info_y + 24, string.format("B: %3d (ADC:%d)", b, adc_b), disp.rgb(80, 80, 255))

        -- RGB565 value
        disp.text(4, info_y + 38, string.format("RGB565: 0x%04X", color), DIM)

        -- Instructions
        disp.fill(0, H-12, W, 12, disp.rgb(25, 25, 30))
        disp.text(4, H-10, "Adjust pots: R=CH0 G=CH3 B=CH6", DIM)

        sys.delay(200)
    end

    sys.print(string.format("Final color: R=%d G=%d B=%d (0x%04X)", r, g, b, disp.rgb(r, g, b)))
    sys.print("Color mixer complete.")
end

run()
