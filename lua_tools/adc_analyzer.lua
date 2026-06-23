-- ADC Signal Analyzer
-- Samples ADC and displays waveform on screen
-- Uses: adc.*, disp.*, sys.*

function run()
    sys.print("=== ADC Signal Analyzer ===")

    disp.init()
    local W = disp.width()
    local H = disp.height()

    local BG     = disp.rgb(0, 0, 0)
    local GRID   = disp.rgb(20, 30, 20)
    local SIGNAL = disp.rgb(0, 255, 100)
    local TEXT   = disp.rgb(200, 220, 200)
    local ACCENT = disp.rgb(0, 200, 255)

    -- Header
    disp.fill(0, 0, W, 14, disp.rgb(15, 20, 15))
    disp.text(4, 3, "ADC ANALYZER", ACCENT)

    -- Grid area: y=16 to y=H-16
    local grid_top = 16
    local grid_bot = H - 16
    local grid_h = grid_bot - grid_top
    local grid_mid = grid_top + grid_h / 2

    -- Collect samples
    local samples = {}
    local num_samples = W - 8  -- leave margin
    for i = 1, num_samples do
        local val = adc.read(0)
        samples[i] = val
        sys.delay(2)
    end

    -- Draw grid
    disp.fill(0, grid_top, W, grid_h, BG)
    -- Horizontal lines
    for i = 0, 4 do
        local gy = grid_top + (grid_h * i) / 4
        disp.line(0, gy, W-1, gy, GRID)
    end
    -- Center line (0V reference)
    disp.line(0, grid_mid, W-1, grid_mid, disp.rgb(40, 60, 40))

    -- Draw signal waveform
    local prev_x = 4
    local prev_y = grid_top + grid_h - math.floor((samples[1] / 4095) * grid_h)
    for i = 2, num_samples do
        local x = 4 + i
        local y = grid_top + grid_h - math.floor((samples[i] / 4095) * grid_h)
        disp.line(prev_x, prev_y, x, y, SIGNAL)
        prev_x = x
        prev_y = y
    end

    -- Stats footer
    disp.fill(0, H-14, W, 14, disp.rgb(15, 20, 15))
    local min_val = 4095
    local max_val = 0
    local sum = 0
    for _, v in ipairs(samples) do
        if v < min_val then min_val = v end
        if v > max_val then max_val = v end
        sum = sum + v
    end
    local avg = math.floor(sum / num_samples)
    local range = max_val - min_val

    local stats = string.format("MIN:%d MAX:%d AVG:%d RNG:%d", min_val, max_val, avg, range)
    disp.text(2, H-11, stats, TEXT)

    sys.print(string.format("ADC Analysis: Min=%d Max=%d Avg=%d Range=%d", min_val, max_val, avg, range))
    sys.print("Display shows waveform. Press key to exit.")
    sys.delay(10000)
end

run()
