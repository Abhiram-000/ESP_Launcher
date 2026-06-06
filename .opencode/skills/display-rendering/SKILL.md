---
name: display-rendering
description: LovyanGFX display rendering rules — theme colors, resolution-independent layout, animation timing, IApp render contract
---

## What I do

I enforce the display and rendering standards from AGENTS.md Part IV. Load me when writing `render()`, `loop()`, or `handleInput()` methods for apps.

## When to use me

- Implementing new apps (IApp interface)
- Writing render() methods
- Adding animations or visual feedback
- Debugging display issues
- Porting to new display panels

## Render Contract

Every `render()` method follows this pattern:

```cpp
void App_Example::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());  // ALWAYS first

    const int32_t W  = canvas->width();
    const int32_t H  = canvas->height();
    const int32_t CX = W / 2;
    // ... layout from canvas dimensions, never hardcoded
}
```

## Rules

1. `canvas->fillSprite(theme_get_bg())` — FIRST line, no exceptions
2. All colors via `theme_get_*()` — hardcoded RGB565 → REJECTED
3. All positions from `canvas->width()` / `canvas->height()` — hardcoded pixels → REJECTED
4. Apps NEVER call `pushSprite()` — UI task owns the push
5. 40fps target: `loop()` + `render()` + DMA push = 25ms total
6. No blocking functions in render path

## Theme Color Roles

```
theme_get_bg()        → fill background (first call)
theme_get_panel()     → elevated surfaces: headers, cards
theme_get_text()      → primary readable text
theme_get_text_dim()  → secondary: hints, labels, footer hints
theme_get_accent()    → interactive: selection highlight, progress
```

## Animation Timing

Always use `_anim_time` (frame-rate independent):

```cpp
_anim_time += 0.025f;  // 1.0 unit = 1 second at 40fps
float pulse = (sinf(_anim_time * 3.0f) + 1.0f) * 0.5f;
```

Frame-count animations (`_frame++`) → REJECTED.

## Resolution-Independent Layout

```cpp
const int32_t W        = canvas->width();
const int32_t H        = canvas->height();
const int32_t MARGIN   = W / 20;      // 5%
const int32_t HEADER_H = H / 9;       // ~11%
const int32_t FOOTER_Y = H - 3;
```

## UI State Coverage

Every app with async data MUST handle all three states:
1. **Loading** — spinner + status message
2. **Error** — error display + retry prompt
3. **Empty** — "Nothing here yet" message
4. **Normal** — full content display

## Input Handling

Every app MUST handle `BTN_LEFT`:
```cpp
case BTN_LEFT: switch_to_grid(); return;
```

Immediate visual feedback on input:
```cpp
_flash_frames = 3;
// In render: if (_flash_frames > 0) { _flash_frames--; canvas->fillSprite(theme_get_accent()); return; }
```

## IApp Interface

```cpp
class IApp {
    virtual void init() = 0;
    virtual void exit() = 0;
    virtual void loop() = 0;
    virtual void render(LGFX_Sprite* canvas) = 0;
    virtual void handleInput(input_event_t event) = 0;
    virtual bool isDynamic() { return false; }
};
```

## Display Panels

| Panel | Resolution | SPI Freq | Canvas Offset |
|-------|-----------|----------|---------------|
| ST7735S 1.8" | 160x128 | 27MHz | +4 height |
| ILI9341 2.4/2.8" | 240x320 | 80MHz | none |
| ST7789 1.3/2.0" | 240x240/320 | 80MHz | none |

Canvas PSRAM required for resolutions above 128x128.
