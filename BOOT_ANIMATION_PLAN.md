# Boot Animation Plan — ESP_Launcher

## Current State

The current boot splash (`boot_ctrl.cpp:252-321`) shows for 4 seconds:
- Static dark blue background with dot grid
- "Press SELECT to stay in Launcher" text at top
- 64×64 boot logo rotated 90° CW, scaled 1.25×, centered at y=82
- Progress bar at bottom (cyan → orange → red as time passes)
- Countdown timer (e.g., "3.2s")

It's functional but not visually impressive. This is the first thing users see.

## Goal

Replace the static splash with a polished, multi-phase animation that:
- Plays for the full 4-second window
- Looks professional and memorable
- Works before `theme_mgr` is initialized (hardcoded colors are acceptable here per AGENTS.md)
- Allows SELECT press at any time to skip
- Stays within the 25ms frame budget (40fps target)
- Uses canvas-relative positions for resolution independence

## Display Context

- Panel: ST7735S, 128×160, rotated to landscape → effective 160×128
- Canvas: 160×132 (with +4 offset for ST7735S alignment)
- Boot logo: 64×64 RGB565, flash-resident (`icon_boot_logo`)
- No PSRAM needed for canvas (160×132×2 = 42KB, fits in internal SRAM)
- LovyanGFX available for drawing primitives, text, sprites

## Animation Design

### Phase 1: Logo Reveal (0.0s → 1.2s)

**What happens:**
- Background starts solid black
- Logo sprite (64×64) fades in from 0% to 100% opacity
- Logo starts slightly smaller (scale 0.8) and grows to 1.0
- Subtle glow ring expands outward from logo center

**Implementation:**
```cpp
float t = elapsed_s / 1.2f;  // 0.0 → 1.0 over 1.2s
float alpha = fminf(t * 2.0f, 1.0f);  // 0→1 over first 0.6s, then hold
float scale = 0.8f + 0.2f * t;  // 0.8 → 1.0

// Draw logo with alpha blend onto background
// Use pushRotateZoom for scale, manual alpha blend for transparency
```

**Colors:**
- Background: `color565(3, 4, 12)` (current dark blue — acceptable, runs before theme_mgr)
- Logo: original `icon_boot_logo` data
- Glow ring: `color565(0, 200, 255)` (cyan) at low alpha

### Phase 2: Text Reveal (1.0s → 2.2s)

**What happens:**
- Logo stays centered and pulses gently (scale 1.0 → 1.03 → 1.0)
- "LAUNCHER" text types in below logo, one character at a time
- Each character fades in as it appears
- Version text "v1.0" fades in below

**Implementation:**
```cpp
// Logo pulse (overlaps with phase 1 end)
float pulse = sinf(elapsed_s * 4.0f) * 0.03f;  // ±3% scale
float logo_scale = 1.0f + pulse;

// Text typewriter
const char* title = "LAUNCHER";
int chars_shown = (int)((elapsed_s - 1.0f) * 8.0f);  // 8 chars/sec
if (chars_shown > (int)strlen(title)) chars_shown = strlen(title);
// Draw each character with individual alpha fade
```

**Colors:**
- Title text: `color565(0, 255, 255)` (cyan)
- Version text: `color565(100, 160, 200)` (muted blue)

### Phase 3: Particle Ring (1.5s → 3.5s)

**What happens:**
- 12 small dots orbit around the logo in a circle
- Dots have varying speeds and radii
- Creates a "tech" feel — like electrons orbiting a nucleus
- Particles fade out as countdown begins

**Implementation:**
```cpp
#define NUM_PARTICLES 12
static const float particle_speed[NUM_PARTICLES] = { /* pre-computed speeds */ };
static const float particle_radius[NUM_PARTICLES] = { /* pre-computed radii */ };

for (int i = 0; i < NUM_PARTICLES; i++) {
    float angle = elapsed_s * particle_speed[i] + i * (2.0f * M_PI / NUM_PARTICLES);
    float r = particle_radius[i];
    int px = cx + (int)(cosf(angle) * r);
    int py = logo_cy + (int)(sinf(angle) * r);
    float alpha = /* fade in at 1.5s, fade out at 3.0s */;
    canvas->fillCircle(px, py, 1, alpha_blend(color565(0, 200, 255), alpha));
}
```

**Colors:**
- Particles: `color565(0, 200, 255)` (cyan) with alpha fade

### Phase 4: Countdown (2.5s → 4.0s)

**What happens:**
- Particles fade out
- Progress bar appears at bottom (same as current, but smoother)
- Countdown text shows remaining time
- "Press SELECT to stay in Launcher" text fades in
- If SELECT pressed at any point, animation stops immediately

**Implementation:**
- Reuse current progress bar logic (already works well)
- Add fade-in for the hint text
- Make progress bar color transition smoother (lerp between colors)

**Colors:**
- Progress bar: `color565(0, 200, 255)` → `color565(255, 180, 0)` → `color565(255, 50, 40)`
- Countdown text: `color565(255, 255, 255)` (white)
- Hint text: `color565(100, 160, 200)` (muted blue)

## Layout

```
┌──────────────────────────────┐ y=0
│                              │
│   "Press SELECT"             │ y=6 (Phase 4 fade-in)
│   "to stay in Launcher"      │ y=16
│                              │
│         ┌──────┐             │
│         │ LOGO │             │ y=32-96 (64×64 centered)
│         │ 64×64│             │
│         └──────┘             │
│    • • • (particles) • • •  │ y=64 (orbit around logo)
│                              │
│      L A U N C H E R        │ y=100 (Phase 2 typewriter)
│         v1.0                 │ y=112
│                              │
│   ╔════════════════════╗     │ y=122 (Phase 4)
│   ║ ████████████░░░░░░ ║     │ Progress bar
│   ╚════════════════════╝     │ y=127
│                      3.2s    │ y=118 (countdown)
└──────────────────────────────┘ y=132
```

## File Changes

| File | Change |
|------|--------|
| `src/system/boot_ctrl.cpp` | Rewrite `draw_splash()` function (~150 lines). Add particle state, phase logic, text typewriter. Keep all other functions unchanged. |

No new files needed. All animation logic lives in the existing `draw_splash()` function.

## Constants (add to boot_ctrl.cpp)

```cpp
#define BC_ANIM_LOGO_REVEAL_S   1.2f    // Phase 1 duration
#define BC_ANIM_TEXT_REVEAL_S   1.2f    // Phase 2 duration (starts at 1.0s)
#define BC_ANIM_PARTICLES_S     2.0f    // Phase 3 duration (starts at 1.5s)
#define BC_ANIM_COUNTDOWN_S     1.5f    // Phase 4 duration (starts at 2.5s)
#define BC_ANIM_NUM_PARTICLES   12      // Orbiting dots
```

## Performance Budget

| Metric | Target | Notes |
|--------|--------|-------|
| Frame time | ≤25ms | 40fps target |
| Draw calls | ≤50 per frame | Logo push + primitives + text |
| Sprite operations | ≤3 | Logo sprite, canvas push |
| Math operations | ≤20 sin/cos | Particle positions |
| Flash reads | 1 (logo data) | 64×64×2 = 8KB, cached |

The animation uses only:
- `fillSprite()` — 1 call
- `pushRotateZoom()` — 1 call (logo)
- `fillCircle()` — 12 calls (particles)
- `drawString()` — 3 calls (title, version, hint)
- `fillRect()` — 1 call (progress bar)
- `drawRect()` — 2 calls (progress bar border)
- `pushSprite()` — 1 call (final push to TFT)

Total: ~21 draw operations per frame. Well within budget.

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Frame drops during logo pushRotateZoom | Logo is 64×64, small enough. Pre-render at init if needed. |
| Stack overflow from particle math | All math is local floats, ~64 bytes total. Well under 512B limit. |
| PSRAM pressure | Canvas is 42KB (internal SRAM). Logo is flash-resident. No PSRAM needed. |
| SELECT not detected during animation | Same polling logic as current code, 10ms interval. Already works. |
| Display init failure | Existing fallback: "running blind countdown" — preserved. |

## Testing Checklist

- [ ] Animation plays for full 4 seconds without SELECT
- [ ] SELECT press at 0.5s immediately exits animation
- [ ] SELECT press at 3.5s immediately exits animation
- [ ] Logo appears centered and scaled correctly
- [ ] Text typewriter effect is smooth
- [ ] Particles orbit without jitter
- [ ] Progress bar fills smoothly
- [ ] Countdown shows correct remaining time
- [ ] No frame drops (verify with logic analyzer or visual inspection)
- [ ] Works on ST7735S (160×128 landscape)
- [ ] Display init failure still runs blind countdown
