import { tool } from "@opencode-ai/plugin"

export default tool({
  description: "Validate ESP32-S3 partition table and check for AGENTS.md compliance",
  args: {
    check_ota: tool.schema.boolean().optional().describe("Verify OTA rollback configuration"),
  },
  async execute(args, context) {
    const { $, worktree } = context
    
    const issues: string[] = []
    const warnings: string[] = []
    
    // Read partition table
    const partitionContent = await $`cat ${worktree}/partitions.csv`.text()
    const lines = partitionContent.split("\n").filter(l => l.trim() && !l.startsWith("#"))
    
    // Check for required partitions
    const hasOta0 = lines.some(l => l.includes("ota_0"))
    const hasOta1 = lines.some(l => l.includes("ota_1"))
    const hasNvs = lines.some(l => l.includes("nvs"))
    const hasLittlefs = lines.some(l => l.includes("littlefs"))
    
    if (!hasOta0) issues.push("❌ Missing ota_0 partition — Launcher must always boot first")
    if (!hasOta1) issues.push("❌ Missing ota_1 partition — OTA rollback requires both slots")
    if (!hasNvs) issues.push("❌ Missing nvs partition — required for non-volatile storage")
    if (!hasLittlefs) warnings.push("⚠️  No littlefs partition — games partition not configured")
    
    // Check OTA safety
    if (args.check_ota) {
      const sdkconfigContent = await $`cat ${worktree}/sdkconfig.defaults`.text()
      
      if (!sdkconfigContent.includes("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y")) {
        issues.push("❌ OTA rollback not enabled in sdkconfig.defaults")
      }
      
      if (!sdkconfigContent.includes("CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC=y")) {
        warnings.push("⚠️  Custom RTC reserve not enabled — boot GPIO hook may not work")
      }
    }
    
    // Check flash size
    const platformioContent = await $`cat ${worktree}/platformio.ini`.text()
    if (!platformioContent.includes("flash_size = 16MB")) {
      warnings.push("⚠️  Flash size not set to 16MB in platformio.ini")
    }
    
    // Summary
    if (issues.length === 0 && warnings.length === 0) {
      return `✅ Partition table is valid and AGENTS.md compliant.

Current partitions:
${lines.map(l => "  " + l.split(",").slice(0, 2).join(" — ")).join("\n")}`
    }
    
    let result = ""
    if (issues.length > 0) {
      result += `ISSUES (${issues.length}):\n${issues.join("\n")}\n\n`
    }
    if (warnings.length > 0) {
      result += `WARNINGS (${warnings.length}):\n${warnings.join("\n")}\n\n`
    }
    
    result += `Partitions found:\n${lines.map(l => "  " + l.split(",").slice(0, 2).join(" — ")).join("\n")}`
    
    return result
  },
})
