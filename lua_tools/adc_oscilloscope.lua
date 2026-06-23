-- ADC Oscilloscope Tool
-- Samples ADC pin and displays waveform data

function run()
    sys.print("=== ADC Oscilloscope ===")
    sys.print("Sampling ADC pin 1 for 5 seconds...")
    sys.print("")

    local samples = {}
    local num_samples = 100
    local sample_delay = 50  -- ms between samples

    -- Collect samples
    for i = 1, num_samples do
        local val = adc.read(1)
        samples[i] = val
        sys.delay(sample_delay)
    end

    -- Display as ASCII waveform
    sys.print("Waveform (ADC values):")
    sys.print("----------------------")

    local min_val = 4095
    local max_val = 0
    local sum = 0

    for i, val in ipairs(samples) do
        if val < min_val then min_val = val end
        if val > max_val then max_val = val end
        sum = sum + val
    end

    local avg = sum / num_samples

    -- Show summary
    sys.print(string.format("Min:   %d", min_val))
    sys.print(string.format("Max:   %d", max_val))
    sys.print(string.format("Avg:   %d", avg))
    sys.print(string.format("Range: %d", max_val - min_val))
    sys.print("")

    -- Show mini histogram
    sys.print("Distribution:")
    local buckets = {0, 0, 0, 0, 0, 0, 0, 0}
    local bucket_size = 4096 / 8
    for _, val in ipairs(samples) do
        local b = math.floor(val / bucket_size) + 1
        if b > 8 then b = 8 end
        buckets[b] = buckets[b] + 1
    end

    for i = 1, 8 do
        local bar = string.rep("#", math.floor(buckets[i] / 2))
        local range_lo = (i - 1) * bucket_size
        sys.print(string.format("%4d-%4d: %s (%d)", range_lo, range_lo + bucket_size - 1, bar, buckets[i]))
    end

    sys.print("")
    sys.print("Sampling complete.")
end

run()
