import { tool } from "@opencode-ai/plugin"

export default tool({
  description: "Build the ESP32-S3 firmware using PlatformIO",
  args: {
    clean: tool.schema.boolean().optional().describe("Run clean build (pio run -t clean first)"),
    verbose: tool.schema.boolean().optional().describe("Enable verbose build output"),
  },
  async execute(args, context) {
    const { $, worktree } = context
    
    if (args.clean) {
      await $`cd ${worktree} && pio run -t clean`
    }
    
    const verboseFlag = args.verbose ? "-v" : ""
    const result = await $`cd ${worktree} && pio run ${verboseFlag}`.text()
    
    // Check for errors in output
    const lines = result.split("\n")
    const errorLines = lines.filter(l => l.includes("error:") || l.includes("FAILED"))
    
    if (errorLines.length > 0) {
      return `BUILD FAILED\n\nErrors:\n${errorLines.join("\n")}\n\nFull output available in build log.`
    }
    
    const successLine = lines.find(l => l.includes("SUCCESS"))
    return successLine || `Build completed.\n${lines.slice(-10).join("\n")}`
  },
})
