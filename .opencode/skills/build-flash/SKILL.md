---
name: build-flash
description: PlatformIO build, flash, and monitor commands for ESP32-S3 — includes exception decoding and partition management
---

## What I do

I provide the build, flash, and monitor workflow for this ESP32-S3 project. Load me when building, flashing, monitoring, or managing partitions.

## When to use me

- Building the firmware
- Flashing to device
- Monitoring serial output
- Managing partitions
- Running pre-commit checks

## Build Commands

```bash
# Full build
pio run

# Build with verbose output
pio run -v

# Clean build
pio run -t clean

# Build specific environment
pio run -e esp32-s3-devkitc-1
```

## Flash Commands

```bash
# Flash firmware
pio run -t upload

# Flash with monitor
pio run -t upload && pio device monitor

# Flash specific port
pio run -t upload --upload-port /dev/ttyUSB0
```

## Monitor Commands

```bash
# Standard monitor
pio device monitor

# With exception decoder (REQUIRED for debugging)
pio device monitor --filter esp32_exception_decoder --filter colorize --filter time

# Specific baud rate
pio device monitor -b 115200
```

## Partition Management

Current partition table in `partitions.csv`:
- `ota_0` — Launcher (always boots first)
- `ota_1` — New firmware (rollback target)
- `nvs` — Non-volatile storage
- `littlefs` — Games partition (4MB at 0x610000)

### Critical: Partition Table Changes
Any change to partition layout requires:
1. Full flash erase: `esptool.py erase_flash`
2. Complete re-flash
3. Partial re-flash after partition change causes silent corruption

## Icon Pipeline

```bash
# Convert icons (runs automatically before build)
python3 convert_icons.py

# Manual conversion
python3 convert_icons.py icons/ src/assets/icons/
```

Requires: `pip install Pillow`

## Pre-Commit Checks

```bash
# Build must pass
pio run

# Static analysis
pio check

# Format check
clang-format --dry-run src/**/*.c src/**/*.cpp
```

## Build Configuration

Key settings in `platformio.ini`:
- Board: ESP32-S3-N16R8 (16MB flash, 8MB PSRAM)
- Framework: ESP-IDF
- Flash mode: QIO
- Partition table: custom (`partitions.csv`)
- Build type: debug
- Monitor filters: esp32_exception_decoder, colorize, time

## Troubleshooting

### Build fails with "partition table mismatch"
Run: `pio run -t clean && pio run`

### Flash fails
1. Check USB connection
2. Hold BOOT button during flash
3. Try: `esptool.py --port /dev/ttyUSB0 erase_flash`

### Monitor shows garbage
1. Check baud rate (115200)
2. Check correct port selected
3. Reset device
