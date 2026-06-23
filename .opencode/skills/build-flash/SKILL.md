---
name: build-flash
description: PlatformIO build, flash, and monitor commands for ESP32-S3 — exception decoding, partition management, icon pipeline. Load when building, flashing, monitoring, or managing partitions.
---

# Build & Flash

## Build

```bash
pio run                      # Full build
pio run -v                   # Verbose
pio run -t clean             # Clean build
pio run -e esp32-s3-devkitc-1  # Specific environment
```

## Flash

```bash
pio run -t upload            # Flash
pio run -t upload && pio device monitor  # Flash + monitor
pio run -t upload --upload-port /dev/ttyUSB0  # Specific port
```

## Monitor — Always Use Exception Decoder

```bash
pio device monitor --filter esp32_exception_decoder --filter colorize --filter time
```

## Partition Table

Current in `partitions.csv`:
- `ota_0` — Launcher (always boots first)
- `ota_1` — New firmware (rollback target)
- `nvs` — Non-volatile storage
- `littlefs` — Games partition (4MB at 0x610000)

**Critical:** Partition table changes require full flash erase + re-flash. Partial re-flash causes silent corruption.

## Icon Pipeline

```bash
python3 convert_icons.py icons/ src/assets/icons/
```

Requires: `pip install Pillow`

## Pre-Commit

```bash
pio run                      # Must pass
pio check                    # Static analysis
clang-format --dry-run src/**/*.c src/**/*.cpp  # Format check
```

## Troubleshooting

- **Build fails "partition table mismatch":** `pio run -t clean && pio run`
- **Flash fails:** Hold BOOT button, try `esptool.py --port /dev/ttyUSB0 erase_flash`
- **Monitor garbage:** Check baud rate (115200), correct port, reset device
