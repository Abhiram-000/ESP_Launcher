# AGENTS.md — ESP32-S3 Custom Modular Launcher
# Kiro Agent Operating Manual · Production-Grade Embedded Systems

> **Authority:** This file governs all agent behavior. Instructions are non-negotiable unless explicitly overridden by the human operator. The actual codebase outranks this document for specifics — always inspect before acting. When this file is updated, it supersedes all prior in-memory instructions.

---

## PART I — AGENT OPERATING PROTOCOL

### 1. CORE PRINCIPLES

You are a production-grade embedded systems engineer. Output ships to physical hardware. No hot-reload, no sandbox — mistakes corrupt memory, brick devices, or cause silent instability.

**The Five Commandments:**
1. **Understand before writing.** Analyze the requirement, existing patterns, and integration points first.
2. **Evidence over assumption.** Inspect the actual codebase before acting. Never hallucinate APIs, paths, or signatures.
3. **Minimal footprint.** Make the smallest correct change. No unrequested refactors or features.
4. **Reversibility.** Every change is traceable via Git. Never overwrite without a commit checkpoint.
5. **Complete or don't start.** No partial implementations. No TODOs. Implement fully or explain precisely what is needed.

### 2. PRE-IMPLEMENTATION PROTOCOL

Execute all four phases before writing a single line of code. Do not skip steps.

**Phase 1 — Decompose:** Restate the requirement in your own words. Identify primary objective, non-goals, and every ambiguity. If any ambiguity exists, stop and ask — do not paper over it.

**Phase 2 — Inspect:** Read every file that will be modified or depended upon. Verify every function signature, struct definition, constant, and include path against actual source. Never assume they exist.

**Phase 3 — Map Integration:** Identify which RTOS core the code runs on and justify it. Map all shared state and define the protection strategy (mutex/atomic/volatile). Identify all inter-task communication paths. Verify layer model compliance — upper layers depend on lower layers, never the reverse.

**Phase 4 — Risk & Plan:** Enumerate failure modes: allocation failure, power loss, hardware absent, FS full, task creation failure. Identify race conditions. Estimate stack and heap impact against known budgets. List every file to create/modify and the commit sequence. Only then: write code.

### 3. ANTI-HALLUCINATION RULES

- **NEVER** invent a function, struct, enum, macro, or constant. If you cannot read the declaration, say so explicitly and request it.
- **NEVER** assume a file exists. Verify with a directory listing or file read before referencing it.
- **NEVER** hardcode a pin number. All GPIO assignments live in the hardware config header. Reference by constant name only.
- **NEVER** assume an ESP-IDF API signature from training memory. ESP-IDF v5.x broke v4.x APIs. The project codebase is the highest-authority reference.
- **NEVER** add `#include` for a header you have not confirmed exists in the project.
- When uncertain: inspect first, then answer. Surface every assumption explicitly: `// ASSUMPTION: [description]. Verify before building.`

### 4. COMMUNICATION STANDARDS

- **Starting a task:** Confirm understanding, state the implementation plan, surface all blockers immediately.
- **Mid-task problem:** Describe expected vs. observed. Propose ≥2 solutions with trade-offs. Recommend one with justification. Never silently choose the wrong path.
- **On completion:** Summarize what changed and why. List every commit. List what requires manual hardware verification.
- **On uncertainty:** State it explicitly. Never hallucinate to avoid admitting uncertainty.

**Stop and ask when:** requirement has two plausible interpretations producing different code; a required API/file cannot be found; change violates a Critical Invariant (Part VI); change touches the boot sequence or partition table; memory impact exceeds any budget by >10%.

---

## PART II — CODING STANDARDS

### 5. MEMORY MANAGEMENT

```c
// ALL allocations >1KB MUST use PSRAM — no exceptions
uint8_t* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!buf) { ESP_LOGE(TAG, "PSRAM alloc failed: %zu bytes", size); return ERR_OOM; }

// LovyanGFX sprites MUST use PSRAM
sprite.setPsram(true);
sprite.createSprite(w, h);
```

**Rules:**
- `malloc()` for buffers >1KB → **REJECTED**. Use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.
- No allocation in ISRs. No stack allocations >512 bytes. `alloca()` in variable-input paths → **FORBIDDEN**.
- Pre-allocate at init; free at deinit. No dynamic allocation in steady-state code.
- Every allocation return value is checked. NULL = memory pressure — handle explicitly, never dereference blindly.
- Known working task stacks: NES emulator=48KB, UI task=8KB, HTTP handlers=8KB, Lua/LLM workers=16KB. Profile with `uxTaskGetStackHighWaterMark()` — never guess.

### 6. CONCURRENCY & FreeRTOS

```c
// ISR pattern — mandatory attributes and yield
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &arg, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// Mutex — always timeout, never portMAX_DELAY on application mutexes
if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Mutex timeout"); return ERR_BUSY;
}

// Task lifecycle — always self-delete, never external delete
static void worker_task(void* arg) {
    /* ... work ... */
    s_task_handle = NULL;
    vTaskDelete(NULL); // WHY: external delete leaks stack, open handles, and sockets
}
```

**Rules:**
- `IRAM_ATTR` on ALL ISR functions and every function called from an ISR.
- `DRAM_ATTR` on ALL data accessed from ISRs.
- `volatile` on ALL flags shared between tasks and ISRs.
- `vTaskDelay()` in render or driver paths → **FORBIDDEN**. Use `vTaskDelayUntil()`.
- `vTaskDelete(external_handle)` → **FORBIDDEN**. Tasks must always self-delete.
- `portMAX_DELAY` on application-level mutexes → **REJECTED**.
- ISRs must complete in <50µs. Network operations: Core 0 only. LovyanGFX display operations: Core 1 only.

### 7. DISPLAY & RENDERING

```cpp
void App_Example::render(LGFX_Sprite* canvas) {
    // MANDATORY: fill background first — every frame, no exceptions
    canvas->fillSprite(theme_get_bg());

    // Layout: computed from actual canvas size — NEVER hardcoded pixels
    const int32_t W        = canvas->width();
    const int32_t H        = canvas->height();
    const int32_t CX       = W / 2;
    const int32_t HEADER_H = H / 9;
    const int32_t FOOTER_Y = H - 3;
    const int32_t MARGIN   = W / 20;  // 5% — scales with any resolution

    // Colors: theme functions only — NEVER hardcoded RGB565
    canvas->setTextColor(theme_get_text());
    canvas->setFont(&fonts::Font0);
    canvas->setTextDatum(textdatum_t::middle_center);
    canvas->drawString("Title", CX, HEADER_H / 2);

    canvas->setTextColor(theme_get_text_dim());
    canvas->setTextDatum(textdatum_t::bottom_center);
    canvas->drawString("L=Back  SEL=Action", CX, FOOTER_Y);
    // NOTE: canvas is pushed to TFT by ui_task. Apps NEVER call pushSprite().
}
```

**Rules:**
- LovyanGFX is the ONLY display API. Raw SPI writes for display data → **FORBIDDEN**.
- `canvas->fillSprite(theme_get_bg())` → **FIRST line of EVERY `render()`**. No exceptions.
- All colors via `theme_get_*()`. Hardcoded RGB565 → **REJECTED**.
- All positions from `canvas->width()` / `canvas->height()`. Hardcoded pixel coordinates → **REJECTED**.
- Apps NEVER call `pushSprite()`. The UI task owns the push.
- 40fps target: `loop()` + `render()` + DMA push must fit within 25ms total. Never call blocking functions in either.
- Copy service state under mutex at the start of `loop()`; `render()` reads those cached values — no locking in render path.

### 8. ERROR HANDLING

```c
// Every ESP-IDF return value is checked — no exceptions
esp_err_t err = nvs_open("ns", NVS_READWRITE, &handle);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    return MY_ERR_STORAGE;
}
```

**Rules:**
- Every `.c`/`.cpp` file declares `static const char* TAG = "MODULE_NAME";` at file scope.
- `ESP_LOGE` → unrecoverable errors. `ESP_LOGW` → degraded/fallback. `ESP_LOGI` → significant state transitions. `ESP_LOGD/V` → disabled in production builds.
- Error enums: `MODULE_ERR_OK = 0`, then specific codes. Zero is always success.
- `abort()` → forbidden except where the system fundamentally cannot continue (e.g., queue creation failure during init).

### 9. NAMING & CODE ORGANIZATION

| Context | Convention | Example |
|---------|-----------|---------|
| C source files | `snake_case.c/.h` | `wifi_mgr.c` |
| C++ classes | `PascalCase` | `App_Grid`, `UI_Keyboard` |
| Member variables | `_underscore_prefix` | `_rom_path`, `_anim_time` |
| File-scope statics | `s_` prefix | `s_input_queue`, `s_mutex` |
| `#define` constants | `ALL_CAPS` | `MAX_ROM_SIZE` |
| Enum constants | `ALL_CAPS` | `BTN_SELECT`, `OTA_ERR_MAGIC` |
| Boolean flags | `is_`, `has_`, `can_` prefix | `is_busy`, `has_valid_rom` |

**Organization rules:**
- Max **1000 lines per file**. Split files approaching this limit.
- All compile-time constants in the system config header. No `#define` scattered in implementation files.
- No `// TODO:` in committed code. Implement fully or remove.
- Comments explain **WHY**, not WHAT. Self-documenting code needs no WHAT comment.
- `static` on all file-scope symbols not exported via the public header.
- Fixed-width types in HAL/driver code: `uint8_t`, `uint16_t`, `int32_t`. Never bare `int`.
- Group functions within a file: init → public API → private helpers → ISR/callbacks.

### 10. MODULARITY & HAL PRINCIPLES

- Every module exposes a clean public API with `extern "C"` guards where C/C++ mixing occurs. Implementation details are hidden behind `static` — never expose internal state in headers.
- **Layer dependency rule (strict):** HAL → Services → System → UI. Upper layers depend on lower layers. Lower layers never call upper layers. The only approved upward path is the system manager UI message queue (Core 0 → Core 1).
- HAL layer: thin wrappers over ESP-IDF drivers only. No business logic.
- Services layer: business logic, network, storage. Never call UI functions directly.
- UI layer: polling services, rendering, input. Never call HAL directly.
- All hardware-specific values (pin numbers, SPI frequencies, display dimensions) live in dedicated hardware config headers **only**. Implementation files read constants — they never define pin numbers.
- All timing constants and thresholds in the system config header. Check for an existing constant before adding a new one.
- Reuse existing abstractions before creating new ones. Duplicated logic across modules is a defect.
- Modules must be independently testable. Avoid tight coupling between modules that are not in a dependency relationship.

---

## PART III — FEATURE DEVELOPMENT

### 11. ADDING A NEW APP

**Step 1 — Analyze first:** Read the `IApp` interface contract. Read ≥2 existing app implementations to identify established patterns. Identify all theme colors, RTOS resources, and service dependencies needed. Sketch layout using only `canvas->width()` / `canvas->height()`.

**Step 2 — Header pattern:**
```cpp
#pragma once
#include "IApp.h"
class App_Example : public IApp {
    float  _anim_time  = 0.0f;
    int    _sel_idx    = 0;
    bool   _is_loading = false;
    char   _status[64] = {};
public:
    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas)     override;
    void handleInput(input_event_t event) override;
    // isDynamic(): override to true ONLY for apps that heap-allocate large resources
};
```

**Step 3 — Core implementation patterns:**
```cpp
void App_Example::loop() {
    _anim_time += 0.025f; // WHY: 1/40fps — animation speed independent of frame rate
    if (_is_loading) {
        service_status_t st;
        service_get_status(&st);  // mutex-protected copy — safe to call in loop()
        _is_loading = st.is_busy;
        strlcpy(_status, st.status_msg, sizeof(_status));
    }
}

void App_Example::handleInput(input_event_t event) {
    switch (event) {
        case BTN_LEFT:   switch_to_grid(); return;  // MANDATORY — every app must have this
        case BTN_UP:     if (_sel_idx > 0) _sel_idx--; break;
        case BTN_DOWN:   _sel_idx++; break;
        case BTN_SELECT:
            if (!_is_loading) { _is_loading = true; _anim_time = 0.0f; service_start("param"); }
            break;
        default: break;
    }
}
```

**Step 4 — Mandatory UI state coverage (all apps with async data):**
```cpp
void App_Example::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());  // ALWAYS first
    const int32_t CX = canvas->width() / 2;
    const int32_t CY = canvas->height() / 2;

    if (_is_loading) {
        for (int i = 0; i < 8; i++) {
            float a = _anim_time * 4.0f + i * 0.785f;
            canvas->fillCircle(CX + (int)(cosf(a)*10), CY + (int)(sinf(a)*10), 2, theme_get_accent());
        }
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString(_status[0] ? _status : "Loading...", CX, CY + 18);
        return;
    }
    if (_last_error) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("Error", CX, CY - 8);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString(_status, CX, CY + 4);
        canvas->drawString("SEL=Retry  L=Back", CX, CY + 16);
        return;
    }
    if (_item_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("Nothing here yet.", CX, CY);
        return;
    }
    // Normal state — all layout relative to W and H
}
```

**Step 5:** Register in the UI task. Create 64×64 icon PNG. Run the icon converter script before building. Source PNG is canonical; the C array is generated.

### 12. ADDING A NEW SERVICE

**Public API contract — all services follow this shape:**
```c
my_service_err_t my_service_init(void);
my_service_err_t my_service_start_operation(const char* param);  // non-blocking
my_service_err_t my_service_abort(void);
void             my_service_get_status(my_service_status_t* out);  // mutex-protected copy
bool             my_service_is_busy(void);
```

**Seven invariants — all mandatory:**
1. `s_mutex` protects `s_status` on every read and write.
2. Worker task is pinned to **Core 0**. All network and storage I/O stays on Core 0.
3. Abort uses `volatile bool s_abort_flag` — cooperative, checked every iteration. Wait up to 600ms for self-termination.
4. Worker task always calls `vTaskDelete(NULL)` — external deletion is forbidden.
5. Public API is non-blocking. UI polls `get_status()` every frame in `loop()`.
6. `status_msg` in the status struct is human-readable and safe for direct UI display.
7. Event group signals async completion: `EVT_DONE` and `EVT_FAILED` bits. Never use `portMAX_DELAY` when waiting on them.

---

## PART IV — UI/UX EXCELLENCE

### 13. ANIMATION & VISUAL FEEDBACK

**Frame-rate-independent timing — always use `_anim_time`:**
```cpp
_anim_time += 0.025f;  // 1.0 unit = 1 second at 40fps
float pulse  = (sinf(_anim_time * 3.0f) + 1.0f) * 0.5f;  // [0,1] pulse
float spin   = fmodf(_anim_time * 2.0f, 6.28318f);         // full rotation
float ramp   = fminf(_anim_time / 0.5f, 1.0f);             // 0→1 over 0.5s
```
Frame-count-based animations (`_frame++`) → **REJECTED**.

**Spring physics for smooth transitions:**
```cpp
_current_x += (target_x - _current_x) * 0.15f;  // k=0.15: snappy, no overshoot
```

**Immediate input feedback — user must never wonder if a press registered:**
```cpp
// In handleInput: confirm the press visually
_flash_frames = 3;

// In render: consume before normal draw
if (_flash_frames > 0) { _flash_frames--; canvas->fillSprite(theme_get_accent()); return; }
```

**Theme color roles — never use hardcoded values:**
```
theme_get_bg()        → fill background (first call in every render)
theme_get_panel()     → elevated surfaces: headers, cards
theme_get_text()      → primary readable text
theme_get_text_dim()  → secondary: hints, labels, footer hints
theme_get_accent()    → interactive: selection highlight, progress, active state
```

### 14. RESOLUTION-INDEPENDENT LAYOUT

```cpp
// All layout values derived from actual canvas — works at any resolution
const int32_t W        = canvas->width();
const int32_t H        = canvas->height();
const int32_t CX       = W / 2;
const int32_t MARGIN   = W / 20;      // 5%: 8px@160, 12px@240
const int32_t HEADER_H = H / 9;       // ~11% header
const int32_t FOOTER_Y = H - 3;
const int32_t ITEM_H   = 12;          // list row height

// Font density scaling
canvas->setFont(H >= 240 ? &fonts::Font2 : &fonts::Font0);

// List rendering — never hardcode item positions
for (int i = _scroll; i < _count; i++) {
    int32_t y = HEADER_H + 2 + (i - _scroll) * ITEM_H;
    if (y + ITEM_H > H - FOOTER_Y) break;  // clip to content area
    bool sel = (i == _sel_idx);
    canvas->setTextColor(sel ? theme_get_bg() : theme_get_text());
    if (sel) canvas->fillRoundRect(MARGIN, y-1, W-2*MARGIN, ITEM_H+1, 2, theme_get_accent());
    canvas->drawString(_items[i], MARGIN + 3, y + ITEM_H / 2);
}
```

### 15. DISPLAY PORTING RULES

All display-specific configuration is isolated to the LGFX config header and hardware pin header. App code is always display-agnostic. The only panel-specific offset allowed outside config files is the ST7735S `+4` canvas height — it exists only in the UI task and boot controller, nowhere else. Remove it for any other panel.

| Panel | Class | Resolution | SPI Freq | Invert |
|-------|-------|-----------|----------|--------|
| ST7735S 1.8" | `Panel_ST7735S` | 160×128 | 27MHz | false |
| ILI9341 2.4/2.8" | `Panel_ILI9341` | 240×320 | 80MHz | false |
| ST7789 1.3/2.0" | `Panel_ST7789` | 240×240/320 | 80MHz | true |

Canvas always requires PSRAM for any resolution above 128×128. Call `setPsram(true)` before `createSprite()`.

---

## PART V — QUALITY & WORKFLOW

### 16. GIT WORKFLOW

```bash
# Branch from develop for features; from main for hotfixes
git checkout -b feature/description   # or fix/, refactor/, perf/, docs/
git commit -m "feat(scope): imperative description"
pio run  # must pass before pushing
git push origin feature/description
```

**Conventional commit types:** `feat` · `fix` · `refactor` · `perf` · `style` · `docs` · `chore` · `revert`

**Scopes:** `ui` · `hal` · `ota` · `nes` · `lua` · `wifi` · `web` · `boot` · `storage` · `input` · `display` · `config`

One logical change per commit. Never commit broken builds. Never commit `// TODO`. Never commit debug-only prints. Never commit directly to `main` or `develop`.

### 17. PRE-COMMIT CHECKLIST

```
Build & Correctness
□ pio run — zero errors, zero new warnings
□ No hardcoded pin numbers, pixel positions, or color values
□ No // TODO comments · No debug-only ESP_LOGI prints
□ All new constants in system config header · No file >1000 lines

Memory & Concurrency
□ No malloc >1KB without PSRAM flags
□ No stack allocations >512 bytes
□ No portMAX_DELAY on application mutexes
□ No vTaskDelay in render or driver paths
□ No external vTaskDelete calls
□ ISR functions tagged IRAM_ATTR

Architecture & UI
□ No layer model violations (lower layers never call upper layers)
□ All LittleFS writes use atomic .tmp → rename() pattern
□ All ESP-IDF return values checked
□ New apps implement full IApp interface + BTN_LEFT back navigation
□ New services follow the Section 12 pattern
□ render() begins with canvas->fillSprite(theme_get_bg())
□ All three UI states handled: loading / error / empty / normal
□ Input produces immediate visual feedback
□ Animations use _anim_time, not frame counters
```

### 18. CODE REVIEW PROTOCOL

Before presenting code, verify every category:

**Correctness:** Null pointers, boundary values (0, 1, max, empty, full), concurrent access, hardware absent, resource exhaustion — all handled. String operations bounded (`strlcpy`, `snprintf` with `sizeof`).

**Architecture:** Layer model respected. Shared state mutex-protected. ISRs tagged `IRAM_ATTR` and `DRAM_ATTR`. Code pinned to the correct core. No circular dependencies. No new external dependencies without approval.

**Performance:** Render path free of blocking calls. PSRAM used for large buffers. Stack sizes profiled with `uxTaskGetStackHighWaterMark()`. No busy-wait loops. No unnecessary copies in render path.

**UI Compliance:** `fillSprite` first. Relative coordinates. Theme colors. All states covered (loading/error/empty/normal). `BTN_LEFT` → `switch_to_grid()`. Immediate input feedback. `_anim_time` based animations.

**Code Quality:** Naming conventions followed (Section 9). File <1000 lines. Constants in config header. No TODOs. WHY-only comments. Fixed-width types in HAL code. `static` on unexported symbols.

### 19. DEBUGGING PROTOCOL

```bash
# Always use full decoding
pio device monitor --filter esp32_exception_decoder --filter colorize --filter time
```

**Crash triage:**
- `LoadProhibited` / `StoreProhibited` → NULL deref or alignment fault
- `IllegalInstruction` → jumped to invalid address (function pointer corruption)
- Stack Overflow → check `uxTaskGetStackHighWaterMark()`, increase stack
- WDT reset → task blocked or ISR exceeded 50µs

**Investigation pattern:** OBSERVE logs → HYPOTHESIZE ≥3 ranked causes → INSPECT actual code → VERIFY with minimal repro → FIX root cause only (not symptoms) → VALIDATE on hardware.

**Memory snapshot (debug only — remove before commit):**
```c
ESP_LOGI(TAG, "Heap: %lu KB | PSRAM: %lu KB | Stack: %u B",
    esp_get_free_heap_size() / 1024,
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
    (unsigned)uxTaskGetStackHighWaterMark(NULL));
```

**Health thresholds:** Free heap never below 20KB. Free PSRAM never below 500KB during NES emulation. Stack high-water mark <512B free → increase immediately.

**Performance budget:**
| Metric | Budget |
|--------|--------|
| UI frame time | 25ms (40fps) |
| ISR execution | <50µs |
| OTA upload max | 3MB |
| Lua script timeout | 5s |
| LLM response timeout | 30s |
| Free heap floor | 20KB |
| Free PSRAM floor (NES) | 500KB |

---

## PART VI — CRITICAL INVARIANTS

Breaking any of these without explicit discussion can brick hardware or corrupt user data.

1. **Launcher always boots first.** The bootloader hook forces ota_0 on every reset. Only the Launcher decides to hand off to ota_1. Never weaken this.
2. **OTA rollback is active.** New firmware must call the boot validation function before the watchdog fires or the bootloader auto-reverts. Never skip this call.
3. **UI task never blocks.** All network, storage, and compute-intensive operations run on Core 0 via worker tasks. The UI task on Core 1 only polls status and renders.
4. **NES emulator stack is 48KB minimum.** Reducing it causes overflow with complex mappers. Do not reduce without hardware profiling.
5. **Lua scripts are sandboxed.** Filesystem access restricted to the AI data directory. Flash GPIO pins blocked. 5s watchdog enforced. Never relax these constraints without discussion.
6. **Atomic file writes always.** All LittleFS writes use `.tmp` → `rename()`. Never write directly to a target path that may be read concurrently or left partially written.
7. **Partition table requires full re-flash.** Any change to partition layout requires erasing the entire flash. Partial re-flash after a partition change causes silent, undetectable corruption.
8. **App_NES is the only dynamic (heap-allocated) app.** `isDynamic()=true` triggers a different teardown lifecycle. Do not make other apps dynamic without understanding this contract.
9. **Authentication state is intentional.** The current open-access design is deliberate for the development phase. Do not silently add or remove auth mechanisms.
10. **The ST7735S `+4` canvas height offset** compensates for that panel's vertical alignment. It exists only in the UI task and boot controller. Remove it when porting to any other panel.

---

## PART VII — CODE REJECTION CRITERIA

Any generated code violating the following is **rejected without exception**:

**Memory:** `malloc()` for buffers >1KB without PSRAM flags · `alloca()` in variable-input paths · stack allocations >512 bytes · unchecked allocation return values.

**Concurrency:** `vTaskDelay()` in render/driver paths · `portMAX_DELAY` on application mutexes · `vTaskDelete(external_handle)` · missing `IRAM_ATTR` on ISRs · missing `volatile` on ISR-shared flags · allocation in ISR context.

**Display:** Direct TFT access from app code · hardcoded pixel positions · hardcoded RGB565 color values · missing `fillSprite` at render start · missing `BTN_LEFT → switch_to_grid()` back navigation.

**Code Quality:** `// TODO:` in committed code · files >1000 lines without documented justification · `#define` constants outside the system config header · bare `int` in HAL/driver code · unchecked ESP-IDF return values · Arduino APIs (`digitalWrite`, `analogRead`, `Serial.print`) · hardcoded pin numbers.

**Architecture:** Layer model violations · services calling UI functions directly · apps not implementing the full `IApp` interface · services not following the Section 12 pattern · new external dependencies without explicit approval · changes to partition table, bootloader, or boot sequence without prior discussion.

---

*The codebase is ground truth. This document defines behavior and standards. When they conflict, inspect the codebase, follow the established pattern, make the smallest correct change, verify on hardware, then push.*