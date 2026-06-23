---
name: display-rendering
description: LovyanGFX display rendering — theme colors, resolution-independent layout, animation timing, IApp render contract. Load when writing render(), loop(), or handleInput() for apps.
---

# Display Rendering

Enforce AGENTS.md Part IV. Every render() starts with fillSprite.

## Render Contract

```cpp
void App_Example::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());  // ALWAYS first

    const int32_t W  = canvas->width();
    const int32_t H  = canvas->height();
    const int32_t CX = W / 2;
    // ... layout from canvas dimensions
}
```

**Rules:**
1. `fillSprite(theme_get_bg())` — FIRST line, no exceptions
2. All colors via `theme_get_*()` — hardcoded RGB565 → REJECTED
3. All positions from `canvas->width()` / `canvas->height()` — hardcoded pixels → REJECTED
4. Apps NEVER call `pushSprite()` — UI task owns the push
5. 40fps target: loop() + render() + DMA push = 25ms total

## Theme Colors

```
theme_get_bg()        → fill background (first call)
theme_get_panel()     → headers, cards
theme_get_text()      → primary text
theme_get_text_dim()  → hints, labels
theme_get_accent()    → selection highlight, progress
```

## Animation Timing — Always _anim_time

```cpp
_anim_time += 0.025f;  // 1.0 = 1 second at 40fps
float pulse = (sinf(_anim_time * 3.0f) + 1.0f) * 0.5f;
float spin  = fmodf(_anim_time * 2.0f, 6.28318f);
float ramp  = fminf(_anim_time / 0.5f, 1.0f);
```

Frame-count (`_frame++`) → REJECTED.

## Resolution-Independent Layout

```cpp
const int32_t MARGIN   = W / 20;      // 5%
const int32_t HEADER_H = H / 9;       // ~11%
const int32_t FOOTER_Y = H - 3;
const int32_t ITEM_H   = 12;
canvas->setFont(H >= 240 ? &fonts::Font2 : &fonts::Font0);
```

## UI State Coverage — All Required

Every app with async data MUST handle:
1. **Loading** — spinner + status
2. **Error** — error display + retry
3. **Empty** — "Nothing here yet"
4. **Normal** — full content

## Input — BTN_LEFT Always Back

```cpp
case BTN_LEFT: switch_to_grid(); return;
```

Immediate visual feedback:
```cpp
_flash_frames = 3;
// render: if (_flash_frames > 0) { _flash_frames--; canvas->fillSprite(theme_get_accent()); return; }
```
