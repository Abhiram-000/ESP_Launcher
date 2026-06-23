export const meta = {
  name: "debug-crash",
  description: "Autonomous crash investigation: web research → evidence collection → hypothesis → inspect → fix → validate"
};

export default async function(args, { agent, parallel }) {
  const { crash_log, symptom, error_message } = args;

  const input = crash_log || symptom || error_message || "Unknown crash";

  const analysis = await agent({
    subagent_type: "general",
    description: "Investigate crash with web research",
    prompt: `You are an ESP32-S3 crash investigation specialist. You research before you diagnose. You verify before you fix. You never guess.

CRASH EVIDENCE:
${input}

## Investigation Protocol

### Step 1 — Collect Evidence
From the crash log, extract:
- Exception type (LoadProhibited, StoreProhibited, IllegalInstruction, etc.)
- Backtrace address
- Task name that crashed
- Register values (if available)
- Free heap and PSRAM at time of crash

### Step 2 — Web Research (MANDATORY)
Before hypothesizing, search the web for similar crashes:

1. Search: "ESP32-S3 [exception_type] [error_message]"
2. Search: "ESP-IDF [component] [error] crash debug"
3. Search: "ESP32 [exception] common cause solution"
4. Search: "FreeRTOS [error] ESP32 debugging"
5. Search: "ESP32-S3 [backtrace_pattern] crash analysis"

For each search, record:
- Query used
- URL of source
- Key finding
- How it relates to this crash

### Step 3 — Read Actual Code
Read the source files mentioned in the backtrace. For each file:
- Verify function signatures against headers
- Check buffer sizes and bounds
- Check mutex state transitions
- Check ISR handler correctness
- Check task stack sizes

### Step 4 — Hypothesize
Generate ≥3 ranked hypotheses. For each:
- Hypothesis statement
- Supporting evidence (from logs AND web research)
- Contradicting evidence
- How to confirm or rule it out
- Confidence level (HIGH/MEDIUM/LOW)

### Step 5 — Root Cause
Select the best hypothesis with evidence from:
- Crash log analysis
- Source code inspection
- Web research findings

### Step 6 — Fix
Propose the smallest correct fix:
- Exact file and line number
- What to change
- Why this fix addresses the root cause
- Whether it touches any Critical Invariants

### Step 7 — Validate
List what must be tested on hardware.

## Output Format

## Crash Analysis

### Evidence Extracted
| Field | Value |
|-------|-------|
| Exception | [type] |
| Backtrace | [address] |
| Task | [name] |
| Free Heap | [KB] |
| Free PSRAM | [KB] |

### Web Research
| # | Query | Source | Finding |
|---|-------|--------|---------|
| 1 | [query] | [URL] | [finding] |

### Hypotheses (Ranked)
1. [Hypothesis] — Confidence: [HIGH/MEDIUM/LOW]
   Evidence for: ...
   Evidence against: ...
   How to confirm: ...
   Web support: [URL]

### Root Cause
[Best hypothesis with code evidence: file:line]

### Fix
[Exact change needed — file:line — what to change]

### Critical Invariant Impact
- [ ] Does this fix touch any Critical Invariants? [YES/NO]
- [ ] If YES, explain why it's safe

### Hardware Verification
- [ ] [What to test on device]

### Prevention
[What coding practice would prevent this class of bug in the future]`
  });

  return { analysis };
}
