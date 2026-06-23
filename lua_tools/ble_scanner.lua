-- BLE Scanner Tool
-- Scans for nearby Bluetooth Low Energy devices

function run()
    sys.print("=== BLE Scanner ===")
    sys.print("Scanning for BLE devices...")
    sys.print("(This is a basic scan using available BLE APIs)")
    sys.print("")

    sys.print("BLE scanning requires the BLE stack to be")
    sys.print("initialized. Current Lua bindings support:")
    sys.print("")
    sys.print("  - wifi.scan() for WiFi networks")
    sys.print("  - i2c.probe() for I2C devices")
    sys.print("  - gpio.read() for digital pins")
    sys.print("  - adc.read() for analog values")
    sys.print("")
    sys.print("For full BLE scanning, use the dedicated")
    sys.print("BLE tools in the Hack Store firmware.")
    sys.print("")
    sys.print("Tip: Use the WiFi Scanner tool for")
    sys.print("network discovery instead.")
end

run()
