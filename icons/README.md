# icons/

Place your icon PNG files in this directory. They will be automatically
converted to RGB565 C arrays before each PlatformIO build.

## How It Works

1. PlatformIO invokes `convert_icons.py` as a pre-build script.
2. Each `<name>.png` is resized to 64×64 pixels (bicubic) and composited
   onto a solid background for transparency handling.
3. Pixels are converted to RGB565 (5-6-5, no byte swap, ESP32 native format).
4. A header is written to `src/assets/icons/<name>.h` containing:

```c
static const uint16_t icon_<name>[4096] __attribute__((aligned(4))) = { ... };
```

5. Files are only regenerated when the source PNG changes (SHA256 hash cache).

## Usage in Firmware

```cpp
#include "assets/icons/ota.h"

// Render at full 64×64 resolution
canvas->pushImage(x, y, 64, 64, icon_ota);

// Or render downscaled to 32×32 via App_Grid's built-in downsample helper
```

## Adding New Icons

1. Drop `myapp.png` into this folder.
2. Run `pio run` (or `python3 convert_icons.py` standalone).
3. Include the generated `src/assets/icons/myapp.h` and register the app:

```cpp
#include "assets/icons/myapp.h"
app_grid.registerApp("My App", icon_myapp, &my_app_instance);
```

## Naming Rules

- Filenames are sanitised to valid C identifiers (non-alphanumeric → `_`).
- Example: `my-app icon.png` → `icon_my_app_icon[4096]`.
- Prefer lowercase filenames with underscores for clarity.

## Current Icons

| File | Array | Description |
|---|---|---|
| `ota.png` | `icon_ota` | OTA firmware update app |
| `settings.png` | `icon_settings` | Device settings app |
