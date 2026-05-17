#!/bin/bash
# ============================================================
# flash_all.sh — Flash BOTH firmware AND LittleFS in one shot
# Run this from the project root with your device connected
# ============================================================

PORT=${1:-/dev/ttyUSB0}   # Override: ./flash_all.sh /dev/ttyACM0

set -e

echo ""
echo "=== ESP32-S3 Custom Launcher — Full Flash ==="
echo "Port: $PORT"
echo ""

# 1. Build firmware
echo "[1/4] Building firmware..."
pio run

# 2. Build LittleFS image (ROMs + catalog)
echo "[2/4] Building LittleFS image..."
pio run -t buildfs

# 3. Flash firmware
echo "[3/4] Flashing firmware..."
pio run -t upload --upload-port "$PORT"

# 4. Flash LittleFS (games partition)
echo "[4/4] Flashing LittleFS (games partition)..."
~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 \
    --port "$PORT" \
    --baud 921600 \
    write_flash \
    0x610000 \
    .pio/build/esp32-s3-devkitc-1/littlefs.bin

echo ""
echo "=== Done! Device is flashing and will reboot. ==="
echo "    Open serial monitor: pio device monitor --port $PORT"
