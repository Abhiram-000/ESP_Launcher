export const meta = {
  name: "code-review",
  description: "Parallel multi-aspect code review: 4 independent agents check memory, ISR, display, quality simultaneously"
};

export default async function(args, { agent, parallel }) {
  const { files, scope, file_paths } = args;

  const target = file_paths || files || scope || "recent changes";

  const reviews = await parallel([
    agent({
      subagent_type: "explore",
      description: "Review memory safety",
      prompt: `You are an ESP32-S3 memory safety expert. You ONLY review memory-related issues. You are extremely thorough.

Review these files for MEMORY SAFETY issues:

${target}

## What to Check

For EVERY file, verify:

1. **PSRAM Allocation:**
   - All buffers >1KB use heap_caps_malloc with MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
   - Every allocation return value is checked for NULL
   - No malloc() without PSRAM flags for large buffers

2. **ISR Memory:**
   - No allocation in ISR context (check all IRAM_ATTR functions)
   - No stack allocations >512 bytes in any function
   - No alloca() usage

3. **Dynamic Allocation:**
   - No dynamic allocation in steady-state (loop/render paths)
   - Pre-allocate at init, free at deinit pattern followed
   - LovyanGFX sprites use setPsram(true) before createSprite()

4. **Buffer Safety:**
   - All string operations bounded (strlcpy, snprintf with sizeof)
   - No buffer overflows possible
   - Array bounds checked

## Output Format

For EACH issue found:
[SEVERITY] file:line — Memory Safety
  Description: [what's wrong]
  Risk: [what could happen]
  Fix: [exact change needed]

Severity levels:
- BLOCKER: Will crash or corrupt memory
- MAJOR: Likely to cause issues under load
- MINOR: Theoretical risk, unlikely in practice
- SUGGESTION: Best practice improvement

Summary:
- BLOCKER: [count]
- MAJOR: [count]
- MINOR: [count]
- SUGGESTION: [count]
- Overall: PASS / FAIL`
    }),
    agent({
      subagent_type: "explore",
      description: "Review ISR and concurrency",
      prompt: `You are an ESP32-S3 ISR and concurrency expert. You ONLY review threading and interrupt issues. You are extremely thorough.

Review these files for ISR and CONCURRENCY issues:

${target}

## What to Check

For EVERY file, verify:

1. **ISR Discipline:**
   - All ISR functions have IRAM_ATTR
   - All data accessed from ISRs has DRAM_ATTR
   - All flags shared between tasks and ISRs have volatile
   - ISRs only use ISR-safe primitives (xQueueSendFromISR, xTaskNotifyFromISR, xSemaphoreGiveFromISR)
   - ISRs call portYIELD_FROM_ISR if a higher-priority task was woken

2. **Mutex Usage:**
   - Every mutex has a timeout (never portMAX_DELAY)
   - Every xSemaphoreTake is paired with xSemaphoreGive
   - Mutex protects the correct shared state
   - No mutex held while calling blocking functions

3. **Task Architecture:**
   - No vTaskDelay in render or driver paths
   - No vTaskDelete(external_handle) — only vTaskDelete(NULL)
   - Core affinity explicitly set for all tasks
   - Stack sizes adequate (check uxTaskGetStackHighWaterMark if available)

4. **Race Conditions:**
   - Shared state between tasks is protected
   - ISR-to-task communication uses proper primitives
   - No TOCTOU (time-of-check-time-of-use) issues

## Output Format

For EACH issue found:
[SEVERITY] file:line — ISR/Concurrency
  Description: [what's wrong]
  Risk: [what could happen — crash, data corruption, deadlock]
  Fix: [exact change needed]

Summary:
- BLOCKER: [count]
- MAJOR: [count]
- MINOR: [count]
- SUGGESTION: [count]
- Overall: PASS / FAIL`
    }),
    agent({
      subagent_type: "explore",
      description: "Review display and architecture",
      prompt: `You are an ESP32-S3 display rendering and architecture expert. You ONLY review display patterns and layer model compliance. You are extremely thorough.

Review these files for DISPLAY and ARCHITECTURE issues:

${target}

## Display Checks

For EVERY render() method:
1. Is canvas->fillSprite(theme_get_bg()) the FIRST line?
2. Are ALL colors from theme_get_*() functions?
3. Are ALL positions derived from canvas->width()/height()?
4. Is pushSprite() called anywhere in app code? (must NOT be)
5. Are all four UI states handled: loading, error, empty, normal?
6. Do animations use _anim_time (not frame counters)?
7. Does handleInput() have BTN_LEFT → switch_to_grid()?
8. Is there immediate visual feedback on input (_flash_frames)?

## Architecture Checks

1. Layer model: HAL → Services → System → UI (no reverse calls)
2. Services follow the 7-invariant pattern
3. Apps implement the full IApp interface
4. LittleFS writes use atomic .tmp → rename()
5. All ESP-IDF return values are checked
6. No circular dependencies between modules

## Output Format

For EACH issue found:
[SEVERITY] file:line — Display/Architecture
  Description: [what's wrong]
  Rule: [which AGENTS.md rule violated]
  Fix: [exact change needed]

Summary:
- BLOCKER: [count]
- MAJOR: [count]
- MINOR: [count]
- SUGGESTION: [count]
- Overall: PASS / FAIL`
    }),
    agent({
      subagent_type: "explore",
      description: "Review code quality",
      prompt: `You are an ESP32-S3 code quality expert. You ONLY review naming conventions, code organization, and style. You are extremely thorough.

Review these files for CODE QUALITY issues:

${target}

## What to Check

1. **Naming Conventions:**
   - C files: snake_case.c/.h
   - C++ classes: PascalCase
   - Member variables: _underscore_prefix
   - File-scope statics: s_ prefix
   - #define constants: ALL_CAPS
   - Enum constants: ALL_CAPS
   - Boolean flags: is_, has_, can_ prefix

2. **Code Organization:**
   - No files >1000 lines
   - Constants in sys_config.h (not scattered in implementation)
   - No // TODO comments
   - static on all file-scope symbols not in public header
   - Fixed-width types in HAL code (uint8_t, int32_t, not bare int)
   - Functions grouped: init → public API → private helpers → ISR

3. **String Safety:**
   - strlcpy instead of strcpy
   - snprintf instead of sprintf
   - sizeof used in all bounded operations

4. **Documentation:**
   - static const char* TAG in every .c/.cpp file
   - Doxygen on public functions (if docs agent was involved)
   - Comments explain WHY, not WHAT

## Output Format

For EACH issue found:
[SEVERITY] file:line — Code Quality
  Description: [what's wrong]
  Convention: [which rule violated]
  Fix: [exact change needed]

Summary:
- BLOCKER: [count]
- MAJOR: [count]
- MINOR: [count]
- SUGGESTION: [count]
- Overall: PASS / FAIL`
    })
  ]);

  // Synthesize results
  const synthesis = await agent({
    subagent_type: "general",
    description: "Synthesize review results",
    prompt: `You are a code review synthesizer. Combine these 4 parallel review results into a single, actionable report.

MEMORY SAFETY REVIEW:
${reviews[0]}

ISR/CONCURRENCY REVIEW:
${reviews[1]}

DISPLAY/ARCHITECTURE REVIEW:
${reviews[2]}

CODE QUALITY REVIEW:
${reviews[3]}

## Synthesis Rules

1. Deduplicate findings (same issue reported by multiple reviewers)
2. Keep the HIGHEST severity for duplicated issues
3. Sort by severity: BLOCKER → MAJOR → MINOR → SUGGESTION
4. Provide a single, clear verdict

## Output Format

# Code Review Report

## Summary
| Severity | Count |
|----------|-------|
| BLOCKER | [N] |
| MAJOR | [N] |
| MINOR | [N] |
| SUGGESTION | [N] |

## Verdict: PASS / FAIL

## BLOCKERs (Must fix before merge)
1. [file:line] — [description] — [fix]

## MAJORs (Should fix)
1. [file:line] — [description] — [fix]

## MINORs (Consider fixing)
1. [file:line] — [description] — [fix]

## SUGGESTIONS (Optional improvements)
1. [file:line] — [description] — [fix]

## Hardware Verification Needed
- [ ] [What to test on device]`
  });

  return { reviews, synthesis };
}
