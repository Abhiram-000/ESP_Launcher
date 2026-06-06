---
name: esp32-standards
description: ESP32-S3 coding standards from AGENTS.md — naming, error handling, architecture, code organization
---

## What I do

I enforce the ESP32-S3 coding standards defined in AGENTS.md Part II and Part V. Load me when writing, reviewing, or refactoring code to ensure compliance.

## When to use me

- Writing new C/C++ code for this project
- Reviewing existing code for convention compliance
- Renaming files, functions, or variables
- Adding new modules or services

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

## Error Handling

Every ESP-IDF return value is checked. No exceptions.

```c
static const char* TAG = "MODULE_NAME";

esp_err_t err = nvs_open("ns", NVS_READWRITE, &handle);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    return MY_ERR_STORAGE;
}
```

- `ESP_LOGE` → unrecoverable errors
- `ESP_LOGW` → degraded/fallback
- `ESP_LOGI` → significant state transitions
- Error enums: `MODULE_ERR_OK = 0`, then specific codes
- `abort()` forbidden except in init where recovery is impossible

## Architecture

Layer model (strict):
```
HAL → Services → System → UI
```

- HAL: thin wrappers over ESP-IDF drivers only
- Services: business logic, network, storage
- System: orchestration, event routing
- UI: rendering, input, polling services
- Upper layers depend on lower layers. Lower layers never call upper layers.

## Code Organization

- Max 1000 lines per file
- All compile-time constants in system config header
- No `// TODO:` in committed code
- Comments explain WHY, not WHAT
- `static` on all file-scope symbols not exported via public header
- Fixed-width types in HAL code: `uint8_t`, `uint16_t`, `int32_t`
- Group functions: init → public API → private helpers → ISR/callbacks

## Pre-Commit Checklist

```
□ pio run — zero errors, zero new warnings
□ No hardcoded pin numbers, pixel positions, or color values
□ No // TODO comments
□ All new constants in system config header
□ No file >1000 lines
```
