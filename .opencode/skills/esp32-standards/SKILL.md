---
name: esp32-standards
description: ESP32-S3 coding standards from AGENTS.md — naming, error handling, architecture, code organization. Load when writing, reviewing, or refactoring C/C++ code.
---

# ESP32-S3 Coding Standards

Enforce AGENTS.md Part II and Part V standards. The codebase is ground truth — verify before acting.

## Naming Conventions

| Context | Convention | Example |
|---------|-----------|---------|
| C source files | `snake_case.c/.h` | `wifi_mgr.c` |
| C++ classes | `PascalCase` | `App_Grid`, `UI_Keyboard` |
| Member variables | `_underscore_prefix` | `_rom_path`, `_anim_time` |
| File-scope statics | `s_` prefix | `s_input_queue`, `s_mutex` |
| `#define` constants | `ALL_CAPS` | `MAX_ROM_SIZE` |
| Enum constants | `ALL_CAPS` | `BTN_SELECT`, `OTA_ERR_MAGIC` |
| Boolean flags | `is_`, `has_`, `can_` prefix | `is_busy`, `has_valid_rom` |

## Error Handling — Every ESP-IDF Return Checked

```c
static const char* TAG = "MODULE_NAME";

esp_err_t err = nvs_open("ns", NVS_READWRITE, &handle);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    return MY_ERR_STORAGE;
}
```

- `ESP_LOGE` → unrecoverable. `ESP_LOGW` → degraded. `ESP_LOGI` → state transitions.
- Error enums: `MODULE_ERR_OK = 0`, then specific codes. Zero always success.
- `abort()` forbidden except init where recovery impossible.

## Architecture — Layer Model (Strict)

```
HAL → Services → System → UI
```

- HAL: thin ESP-IDF wrappers only. No business logic.
- Services: business logic, network, storage. Never call UI.
- System: orchestration, event routing.
- UI: rendering, input, polling services. Never call HAL directly.

## Code Organization

- Max **1000 lines per file**. Split approaching this limit.
- All constants in `sys_config.h`. No `#define` in implementation files.
- No `// TODO:` in committed code. Implement fully or remove.
- Comments explain **WHY**, not WHAT.
- `static` on all file-scope symbols not exported via header.
- Fixed-width types in HAL: `uint8_t`, `int32_t`, not bare `int`.
- Group functions: init → public API → private helpers → ISR/callbacks.

## Pre-Commit Checklist

```
□ pio run — zero errors, zero new warnings
□ No hardcoded pin numbers, pixel positions, or color values
□ No // TODO comments
□ All new constants in sys_config.h
□ No file >1000 lines
```
