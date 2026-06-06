import { tool } from "@opencode-ai/plugin"

export default tool({
  description: "Flash ESP32-S3 firmware and monitor serial output with exception decoding",
  args: {
    monitor_only: tool.schema.boolean().optional().describe("Only monitor, don't flash first"),
    port: tool.schema.string().optional().describe("Serial port (e.g., /dev/ttyUSB0)"),
  },
  async execute(args, context) {
    const { $, worktree } = context
    
    if (!args.monitor_only) {
      // Flash first
      const portFlag = args.port ? `--upload-port ${args.port}` : ""
      const flashResult = await $`cd ${worktree} && pio run -t upload ${portFlag}`.text()
      
      if (flashResult.includes("error") || flashResult.includes("FAILED")) {
        return `Flash failed:\n${flashResult.slice(-20)}`
      }
    }
    
    return `Flash complete. To monitor serial output, run:

\`\`\`bash
pio device monitor --filter esp32_exception_decoder --filter colorize --filter time
\`\`\`

Or use the serial port directly:
\`\`\`bash
pio device monitor -b 115200 ${args.port ? `--port ${args.port}` : ""}
\`\`\`

Note: opencode cannot maintain a persistent serial monitor session. Please run the command above in a separate terminal.`
  },
})
