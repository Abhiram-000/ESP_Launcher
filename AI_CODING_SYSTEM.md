# ESP32-S3 AI Coding System — Complete Reference

> Production-grade AI coding infrastructure with autonomous routing, web research, sub-agentic workflows, and zero hallucination.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     USER REQUEST                                │
│         "Add a Bluetooth settings screen"                       │
│         "My app crashes when I press SELECT"                    │
│         "Review my OTA service"                                 │
│         "How do I add a new service?"                           │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                  ROUTER AGENT (router.md)                        │
│         Classifies intent → Delegates to right agent            │
│                                                                  │
│  ┌─────────┬──────────┬──────────┬──────────┬──────────┐       │
│  │ Feature │  Debug   │ Review   │ Perf     │ Question │       │
│  │ Add     │ Crash    │ Audit    │ Optimize │ Research │       │
│  └────┬────┴────┬─────┴────┬─────┴────┬─────┴────┬─────┘       │
│       │         │          │          │          │               │
└───────┼─────────┼──────────┼──────────┼──────────┼──────────────┘
        │         │          │          │          │
        ▼         ▼          ▼          ▼          ▼
┌──────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
│implement-│ │ debug- │ │ code-  │ │ perf-  │ │implement│
│ feature  │ │ crash  │ │ review │ │analyzer│ │  er    │
│workflow  │ │workflow│ │workflow│ │ agent  │ │ agent  │
└──────────┘ └────────┘ └────────┘ └────────┘ └────────┘
    │            │           │          │          │
    │            │           │          │          │
    ▼            ▼           ▼          ▼          ▼
┌─────────────────────────────────────────────────────────────┐
│                    SUB-AGENTS                                │
│  explore × N  │  general × N  │  (parallel execution)       │
│  - Research   │  - Implement  │  - No conflicts             │
│  - Inspect    │  - Fix        │  - Independent context      │
│  - Verify     │  - Build      │  - Dedicated output         │
└─────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                  QUALITY GATES                               │
│  1. Web research (verify APIs, patterns, edge cases)        │
│  2. Self-review (memory, ISR, concurrency, display)         │
│  3. Independent review (parallel 4-agent review)            │
│  4. Build verification (pio run must pass)                  │
│  5. Human presentation (summary, sources, verification)     │
└─────────────────────────────────────────────────────────────┘
```

---

## Files Created/Modified

### Core Documents
| File | Lines | Purpose |
|------|-------|---------|
| `AGENTS.md` | 513 | Operating manual (existing, unchanged) |
| `esp32_system_prompt.md` | 346 | System prompt (existing, unchanged) |
| `ARCHITECTURE.md` | ~150 | Module map, dependencies, task architecture |
| `AI_CODING_SYSTEM.md` | This file | Complete system reference |

### Skills (9 total)
| Skill | Lines | Purpose |
|-------|-------|---------|
| `esp32-standards/SKILL.md` | ~80 | Naming, error handling, architecture |
| `freertos-patterns/SKILL.md` | ~100 | ISR, tasks, mutexes, event groups |
| `memory-safety/SKILL.md` | ~80 | PSRAM, stack budgets, allocation patterns |
| `display-rendering/SKILL.md` | ~100 | Theme colors, layout, animation, IApp |
| `build-flash/SKILL.md` | ~80 | PlatformIO build, flash, monitor |
| `service-pattern/SKILL.md` | ~150 | Seven invariants, worker template, error enums |
| `app-template/SKILL.md` | ~180 | Complete IApp template with all states |
| `codebase-architecture/SKILL.md` | ~80 | Module map summary, dependencies |
| `performance-profiling/SKILL.md` | ~80 | Memory snapshots, stack analysis, timing |

### Agents (10 total)
| Agent | Mode | Purpose |
|-------|------|---------|
| `router.md` | primary | Intent classification → agent delegation |
| `implementer.md` | primary | Autonomous coding with web research |
| `plan.md` | primary | Architecture planning with web research |
| `debug.md` | subagent | Crash triage with web research |
| `review.md` | subagent | Code review against AGENTS.md |
| `refactor.md` | subagent | Behavior-preserving restructuring |
| `docs.md` | subagent | Doxygen and architecture documentation |
| `perf-analyzer.md` | subagent | Memory, stack, CPU profiling |
| `arch-explorer.md` | subagent | Module relationship mapping |
| `ecet-research.md` | subagent | College research (personal) |

### Workflows (4 total)
| Workflow | Agents | Purpose |
|----------|--------|---------|
| `implement-feature.js` | 5 agents sequentially | Research → plan → implement → review → build |
| `debug-crash.js` | 1 agent | Web research → evidence → hypothesis → fix |
| `code-review.js` | 5 agents (4 parallel + 1 synthesize) | Multi-aspect review with synthesis |
| `add-new-app.js` | 4 agents sequentially | Study → research → create → register |

---

## Key Design Principles

### 1. Autonomous Routing
The router agent classifies user intent and automatically selects the right agent/workflow. Users don't need to know which agent to use.

### 2. Web Research First
Every agent does extensive web research before implementing. This prevents:
- Hallucinated APIs
- Outdated patterns
- Missing edge cases
- Wrong best practices

### 3. Sub-Agent Orchestration
Complex tasks are decomposed into sub-agents that run in parallel. Each sub-agent gets:
- Clear role definition
- Exact task description
- Input context
- Constraints
- Output format
- Quality bar

### 4. No Hallucination
Every agent follows anti-hallucination rules:
- Read actual files before referencing them
- Verify APIs via web search
- Cite sources (codebase file:line or web URL)
- Ask human when uncertain

### 5. Quality Gates
Every implementation goes through:
1. Web research (verify correctness)
2. Self-review (check standards)
3. Independent review (4 parallel agents)
4. Build verification (pio run)
5. Human presentation (summary with sources)

### 6. Progressive Disclosure
Skills load in 3 levels:
- Level 1: Metadata (~100 tokens, always loaded)
- Level 2: SKILL.md body (<5k tokens, loaded when triggered)
- Level 3: References (unlimited, loaded as needed)

This saves context window for actual coding.

---

## Usage Examples

### "Add a Bluetooth settings screen"
1. Router classifies: ADD FEATURE
2. Routes to: implement-feature workflow
3. Agent 1: Reads codebase, searches web for ESP32 Bluetooth patterns
4. Agent 2: Creates implementation plan with memory budget
5. Agent 3: Writes complete App_Bluetooth.h/.cpp
6. Agent 4: Reviews against AGENTS.md (4 parallel reviews)
7. Agent 5: Fixes issues, runs pio run
8. Result: Complete, compilable, reviewed app

### "My app crashes when SELECT is pressed"
1. Router classifies: DEBUG
2. Routes to: debug-crash workflow
3. Agent: Searches web for similar crashes, reads source code, hypothesizes 3 causes, proposes minimal fix
4. Result: Root cause identified, fix proposed, hardware verification listed

### "Is my OTA service correct?"
1. Router classifies: CODE REVIEW
2. Routes to: code-review workflow
3. 4 parallel agents review: memory, ISR, display, quality
4. Synthesis agent combines findings
5. Result: Complete review with severity ratings

### "How do I add a new service?"
1. Router classifies: QUESTION
2. Routes to: implementer agent
3. Agent: Searches web for ESP-IDF service patterns, reads existing services, provides answer with examples
4. Result: Detailed answer with code examples and web sources

---

## Anti-Hallucination Rules

1. **NEVER** write code for an API you haven't verified exists
2. **NEVER** assume a function signature — read the header or web docs
3. **NEVER** cite a source you haven't accessed and read
4. **ALWAYS** ask the human when uncertain
5. **ALWAYS** do web research before implementing unfamiliar code
6. **ALWAYS** verify against actual codebase before referencing

---

## Quick Reference

### Router Commands
```
"Add [feature]"        → implement-feature workflow
"Fix [bug]"            → debug-crash workflow
"Review [file]"        → code-review workflow
"Optimize [module]"    → perf-analyzer agent
"Explain [architecture]" → arch-explorer agent
"Build/Flash"          → direct bash commands
"How do I [question]"  → implementer agent (with web search)
```

### Agent Permissions
| Agent | Read | Edit | Bash | Web |
|-------|------|------|------|-----|
| router | ✓ | ✓ | ✓ | ✓ |
| implementer | ✓ | ✓ | ✓ | ✓ |
| plan | ✓ | ✗ | ✗ | ✓ |
| debug | ✓ | ✓ | ✓ | ✓ |
| review | ✓ | ✗ | ✗ | ✗ |
| refactor | ✓ | ✓ | ✗ | ✗ |
| perf-analyzer | ✓ | ✓ | ✓ | ✗ |
| arch-explorer | ✓ | ✗ | ✗ | ✗ |
