import { tool } from "@opencode-ai/plugin"

export default tool({
  description: "Check ESP32-S3 code for AGENTS.md violations (PSRAM, ISR, naming, architecture)",
  args: {
    file: tool.schema.string().optional().describe("Specific file to check (relative to project root)"),
    category: tool.schema.string().optional().describe("Check category: memory, isr, naming, architecture, all"),
  },
  async execute(args, context) {
    const { $, worktree } = context
    const file = args.file || "src/"
    const category = args.category || "all"
    
    const issues: string[] = []
    
    // Memory violations
    if (category === "all" || category === "memory") {
      const mallocCheck = await $`cd ${worktree} && rg "malloc\\(" ${file} --include="*.c" --include="*.cpp" --include="*.h" -n`.text()
      if (mallocCheck.trim()) {
        issues.push("⚠️  MALLOC USAGE DETECTED — must use heap_caps_malloc with PSRAM flags for >1KB allocations:\n" + mallocCheck)
      }
      
      const allocaCheck = await $`cd ${worktree} && rg "alloca\\(" ${file} --include="*.c" --include="*.cpp" --include="*.h" -n`.text()
      if (allocaCheck.trim()) {
        issues.push("❌ ALLOCA USAGE — forbidden in variable-input paths:\n" + allocaCheck)
      }
    }
    
    // ISR violations
    if (category === "all" || category === "isr") {
      const isrCheck = await $`cd ${worktree} && rg "void.*_isr_handler|void.*_callback.*\\(void" ${file} --include="*.c" --include="*.cpp" -n`.text()
      if (isrCheck.trim()) {
        // Check if they have IRAM_ATTR
        const lines = isrCheck.split("\n").filter(l => l.trim())
        for (const line of lines) {
          if (!line.includes("IRAM_ATTR")) {
            issues.push("❌ ISR missing IRAM_ATTR: " + line)
          }
        }
      }
    }
    
    // Naming violations
    if (category === "all" || category === "naming") {
      const todoCheck = await $`cd ${worktree} && rg "// TODO" ${file} --include="*.c" --include="*.cpp" --include="*.h" -n`.text()
      if (todoCheck.trim()) {
        issues.push("❌ TODO COMMENTS — forbidden in committed code:\n" + todoCheck)
      }
    }
    
    // Architecture violations
    if (category === "all" || category === "architecture") {
      const arduinoCheck = await $`cd ${worktree} && rg "digitalWrite|analogRead|Serial\\.print|arduino\\.h" ${file} --include="*.c" --include="*.cpp" --include="*.h" -n`.text()
      if (arduinoCheck.trim()) {
        issues.push("❌ ARDUINO APIs — this is ESP-IDF, not Arduino:\n" + arduinoCheck)
      }
      
      const delayCheck = await $`cd ${worktree} && rg "vTaskDelay\\(" ${file} --include="*.c" --include="*.cpp" -n`.text()
      if (delayCheck.trim()) {
        issues.push("⚠️  vTaskDelay usage — forbidden in render/driver paths:\n" + delayCheck)
      }
      
      const portMaxCheck = await $`cd ${worktree} && rg "portMAX_DELAY" ${file} --include="*.c" --include="*.cpp" -n`.text()
      if (portMaxCheck.trim()) {
        issues.push("⚠️  portMAX_DELAY — rejected on application mutexes:\n" + portMaxCheck)
      }
    }
    
    if (issues.length === 0) {
      return `✅ No ${category} violations found in ${file}`
    }
    
    return `Found ${issues.length} issue(s) in ${file}:\n\n${issues.join("\n\n")}`
  },
})
