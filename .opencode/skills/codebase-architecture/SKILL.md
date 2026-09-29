---
name: codebase-architecture
description: ESP32-S3 Launcher module map, dependency graph, task architecture, NVS schema, LittleFS layout. Load when understanding module relationships or planning integration changes.
---

# Codebase Architecture

Read `ARCHITECTURE.md` in the project root for the full module map. This skill provides the essential summary.

## Layer Model (Strict)

```
HAL → Services → System → UI
```

Upper depends on lower. Lower NEVER calls upper. Exception: sys_mgr → ui_task message queue.

## Module → File Mapping

| Layer | Modules | Files |
|-------|---------|-------|
| HAL | GPIO, WiFi frame-bypass | `hal/hal_gpio.c`, `hal/wsl_bypass.c` |
| Drivers | LED | `drivers/led_driver.c` |
| Services | WiFi, OTA, Web, NTP, Weather, Games, HackStore, Lua, LLM, LED, Pomodoro | `services/*.c/cpp` |
| System | Boot, SysMgr | `system/boot_ctrl.cpp`, `system/sys_mgr.c` |
| UI | Task, Theme, Apps, Components | `ui/ui_task.cpp`, `ui/theme_mgr.cpp`, `ui/apps/*`, `ui/components/*` |

## App → Service Dependencies

| App | Services Used |
|-----|--------------|
| App_Grid | (none) |
| App_OTA | ota_service, wifi_mgr |
| App_NES | game_store |
| App_GameStore | game_store |
| App_HackStore | hack_store, ota_service, boot_ctrl |
| App_ClockWeather | weather_service, ntp_service |
| App_Pomodoro | pomodoro_service |
| App_Torch | led_driver |
| App_Settings | wifi_mgr, led_service, idle_mgr |

## Task Pinning

| Task | Core | Stack |
|------|------|-------|
| UI | 1 | 8KB |
| Services (all) | 0 | 4-48KB |
| NES emulator | 0 | 48KB |

## Key Config Files

- `src/config/sys_config.h` — ALL compile-time constants
- `src/platform/board.h` — GPIO pin assignments
- `partitions.csv` — Flash layout
- `platformio.ini` — Build configuration
