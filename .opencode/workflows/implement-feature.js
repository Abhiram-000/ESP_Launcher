export const meta = {
  name: "implement-feature",
  description: "Full autonomous feature pipeline: research → plan → implement → review → build. Agent decides all steps."
};

export default async function(args, { agent, parallel, phase }) {
  const { feature, user_context } = args;

  phase("1. Research & Understand");

  const research = await agent({
    subagent_type: "general",
    description: "Research the feature",
    prompt: `You are an ESP32-S3 embedded systems researcher. Your task is to thoroughly research how to implement the following feature for the ESP32-S3 Launcher project:

FEATURE REQUEST: ${feature}
USER CONTEXT: ${user_context || "None provided"}

## Your Research Process

### Step 1 — Understand the Codebase
Read these files first:
1. ARCHITECTURE.md — module map and dependencies
2. AGENTS.md — coding standards and patterns
3. src/ui/apps/IApp.h — app interface contract
4. src/config/sys_config.h — all constants
5. src/platform/board.h — GPIO pin assignments

Then find and read 2-3 existing apps similar to what's needed:
- If it's a new app → read App_Torch.cpp (simple) and App_ClockWeather.cpp (async data)
- If it's a new service → read wifi_mgr.c or weather_service.c
- If it's a display feature → read theme_mgr.cpp

### Step 2 — Web Research (MANDATORY)
Before planning, do EXTENSIVE web research:

1. Search: "ESP-IDF v5 [relevant_component] [function_name]"
2. Search: "ESP32-S3 [feature_type] example tutorial"
3. Search: "FreeRTOS [pattern] ESP32 production code"
4. Search: "ESP32-S3 [feature] best practices 2024 2025"
5. Search: "ESP32-S3 [feature] common bugs pitfalls"

For each search, cite the URL and what you learned.

### Step 3 — Identify Integration Points
- Which existing services does this depend on?
- Which existing apps is this similar to?
- What constants need to go in sys_config.h?
- What GPIO pins are available (check board.h)?
- What RTOS resources are needed (tasks, queues, mutexes)?

## Output Format
Return your complete research as:

### Feature Understanding
[Your understanding of what the user wants]

### Web Research
| Query | Source (URL) | Finding |
|-------|-------------|---------|

### Existing Patterns Found
[Files read, patterns identified]

### Integration Points
[Dependencies, constants, resources needed]

### Ambiguities
[Things you're unsure about — ask user]

### Recommended Approach
[Your recommended implementation strategy with justification]

### Edge Cases Identified
[What could go wrong, failure modes]
`
  });

  phase("2. Create Implementation Plan");

  const plan = await agent({
    subagent_type: "general",
    description: "Create detailed implementation plan",
    prompt: `You are an ESP32-S3 architecture planner. Based on this research, create a detailed implementation plan.

RESEARCH:
${research}

FEATURE: ${feature}

## Plan Requirements

Create a plan that includes:

### 1. Files to Create (with exact paths)
For each file:
- Full path
- Purpose
- Key functions/structs
- Dependencies

### 2. Files to Modify (with exact changes)
For each file:
- What lines change
- What the change is
- Why it's needed

### 3. Service API Design (if new service)
```c
// Complete API with all functions
typedef enum { ... } my_service_err_t;
typedef struct { ... } my_service_status_t;
esp_err_t my_service_init(void);
// ... etc
```

### 4. App Class Structure (if new app)
```cpp
class App_Example : public IApp {
    // All member variables
    // All virtual methods
};
```

### 5. Memory Budget
| Resource | Current | After | Delta |
|----------|---------|-------|-------|

### 6. Commit Sequence
1. feat(scope): [description]
2. feat(scope): [description]

### 7. Edge Cases to Handle
1. [edge case] → [how to handle]
2. [edge case] → [how to handle]

### 8. Hardware Verification Needed
- [ ] [What to test on device]

### 9. Critical Invariant Check
- [ ] All invariants preserved

Return the COMPLETE plan. No placeholders. No TODOs.`
  });

  phase("3. Implement Code");

  const implementation = await agent({
    subagent_type: "general",
    description: "Implement all code",
    prompt: `You are an elite ESP32-S3 embedded systems engineer. Implement the following plan with PRODUCTION-GRADE code.

PLAN:
${plan}

FEATURE: ${feature}

## Implementation Rules

### Before Writing ANY Code
1. Read every file that will be modified — verify function signatures
2. Check sys_config.h for existing constants before defining new ones
3. Verify ESP-IDF APIs via web search if you're unsure about ANY signature

### Code Standards (NON-NEGOTIABLE)
1. Every ESP-IDF return value checked
2. PSRAM for allocations >1KB: heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
3. Check every allocation for NULL
4. IRAM_ATTR on all ISR functions
5. DRAM_ATTR on all ISR data
6. volatile on all ISR-shared flags
7. fillSprite(theme_get_bg()) FIRST in every render()
8. All colors via theme_get_*() — no hardcoded RGB565
9. All layout from canvas->width()/height() — no hardcoded pixels
10. All UI states: loading/error/empty/normal
11. BTN_LEFT → switch_to_grid() in every app
12. _anim_time += 0.025f in loop() — frame-rate independent
13. Error enums: MODULE_ERR_OK = 0, then specific codes
14. static const char* TAG = "MODULE_NAME" in every file
15. strlcpy/snprintf with sizeof — never strcpy/sprintf
16. Worker tasks pinned to Core 0
17. UI task on Core 1 — never blocks

### Service Pattern (if creating a service)
```c
// Seven invariants — ALL mandatory
1. s_mutex protects s_status on every read/write
2. Worker pinned to Core 0
3. Abort via volatile bool s_abort_flag — checked every iteration
4. Worker calls vTaskDelete(NULL) — self-deletes
5. Public API non-blocking
6. status_msg human-readable for UI display
7. Event group: EVT_DONE / EVT_FAILED bits
```

### Forbidden
- TODO comments
- Hardcoded pins, pixels, colors
- Arduino APIs
- Files >1000 lines
- malloc >1KB without PSRAM
- vTaskDelay in render/driver paths
- portMAX_DELAY on application mutexes
- vTaskDelete(external_handle)

### Quality Check Before Finishing
Before presenting code, verify:
- [ ] Every function signature matches the actual header
- [ ] Every ESP-IDF return value is checked
- [ ] Every allocation checks for NULL
- [ ] No hardcoded values
- [ ] All UI states handled
- [ ] Naming conventions followed
- [ ] No TODOs

Write COMPLETE, COMPILABLE code for EVERY file. No omissions. No placeholders.`
  });

  phase("4. Independent Review");

  const review = await agent({
    subagent_type: "general",
    description: "Independent code review",
    prompt: `You are a production-grade ESP32-S3 code reviewer. You do NOT write code — you ONLY analyze it.

Review the following implementation against AGENTS.md standards. Report EVERY issue you find.

IMPLEMENTATION:
${implementation}

## Review Checklist — Check EVERY Item

### Memory Safety
- [ ] No malloc >1KB without PSRAM flags
- [ ] No alloca in variable-input paths
- [ ] No stack allocations >512 bytes
- [ ] Every allocation return checked for NULL
- [ ] LovyanGFX sprites use setPsram(true)
- [ ] No allocation in ISR context
- [ ] Pre-allocate at init, free at deinit

### ISR Discipline
- [ ] All ISR functions tagged IRAM_ATTR
- [ ] All ISR data tagged DRAM_ATTR
- [ ] All ISR-shared flags marked volatile
- [ ] Only ISR-safe primitives used
- [ ] portYIELD_FROM_ISR at end if woken

### Concurrency
- [ ] No vTaskDelay in render/driver paths
- [ ] No portMAX_DELAY on application mutexes
- [ ] No vTaskDelete(external_handle)
- [ ] Shared state protected by mutex with timeout
- [ ] Task core affinity explicit

### Display / Rendering
- [ ] fillSprite(theme_get_bg()) is FIRST line of every render()
- [ ] No hardcoded RGB565
- [ ] No hardcoded pixel positions
- [ ] No pushSprite in app code
- [ ] All UI states handled
- [ ] Animations use _anim_time

### Architecture
- [ ] Layer model respected
- [ ] Services follow 7-invariant pattern
- [ ] Apps implement full IApp interface
- [ ] LittleFS writes use atomic .tmp→rename()
- [ ] All ESP-IDF return values checked

### Code Quality
- [ ] Naming conventions followed
- [ ] No // TODO comments
- [ ] No hardcoded pin numbers
- [ ] Fixed-width types in HAL code
- [ ] Constants in sys_config.h
- [ ] Files <1000 lines

### Critical Invariants (ANY = REJECT)
- [ ] Launcher boots first (ota_0)
- [ ] OTA rollback active
- [ ] UI task never blocks
- [ ] NES stack >=48KB (if touched)
- [ ] Atomic file writes

## Output Format

For EACH issue found:
[SEVERITY] file:line — Category
  Description of the violation
  Rule: AGENTS.md §X.Y
  Suggested fix

Severity: BLOCKER | MAJOR | MINOR | SUGGESTION

Summary:
- BLOCKER: [count]
- MAJOR: [count]
- MINOR: [count]
- SUGGESTION: [count]
- Verdict: PASS / FAIL
- Hardware verification needed: [list]`
  });

  phase("5. Fix & Build");

  const build = await agent({
    subagent_type: "general",
    description: "Fix issues and build",
    prompt: `Based on this code review, fix all BLOCKERs and MAJORs, then build the project.

REVIEW:
${review}

IMPLEMENTATION:
${implementation}

## Instructions

1. Fix all BLOCKER issues first
2. Fix all MAJOR issues
3. Leave MINOR and SUGGESTION for the user to decide
4. Run: pio run
5. If build fails, fix errors
6. Report: build status, warnings, what was fixed

## Output Format

### Fixes Applied
1. [BLOCKER/MAJOR] file:line — what was changed

### Build Result
- Status: PASS / FAIL
- Warnings: [list]
- Errors: [list, if any]

### Remaining Issues (for user)
- MINOR: [list]
- SUGGESTION: [list]

### Hardware Verification Needed
- [ ] [What to test on device]

### Files Created/Modified
- [list all files]`
  });

  return { research, plan, implementation, review, build };
}
