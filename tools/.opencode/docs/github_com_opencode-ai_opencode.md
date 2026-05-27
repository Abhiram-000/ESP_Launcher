# GitHub - opencode-ai/opencode: A powerful AI coding agent. Built for the terminal. · GitHub

> Source: https://github.com/opencode-ai/opencode
> Cached: 2026-05-27T06:07:10.424Z

---

# Archived: Project has Moved

[](#archived-project-has-moved)
This repository is no longer maintained and has been archived for provenance.

The project has continued under the name [Crush](https://github.com/charmbracelet/crush), developed by the original author and the Charm team.

Please follow [Crush](https://github.com/charmbracelet/crush) for ongoing development.

# ⌬ OpenCode

[](#-opencode)
[](https://private-user-images.githubusercontent.com/25087/444283386-9ae61ef6-70e5-4876-bc45-5bcb4e52c714.gif?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3Nzk4NjIzMjksIm5iZiI6MTc3OTg2MjAyOSwicGF0aCI6Ii8yNTA4Ny80NDQyODMzODYtOWFlNjFlZjYtNzBlNS00ODc2LWJjNDUtNWJjYjRlNTJjNzE0LmdpZj9YLUFtei1BbGdvcml0aG09QVdTNC1ITUFDLVNIQTI1NiZYLUFtei1DcmVkZW50aWFsPUFLSUFWQ09EWUxTQTUzUFFLNFpBJTJGMjAyNjA1MjclMkZ1cy1lYXN0LTElMkZzMyUyRmF3czRfcmVxdWVzdCZYLUFtei1EYXRlPTIwMjYwNTI3VDA2MDcwOVomWC1BbXotRXhwaXJlcz0zMDAmWC1BbXotU2lnbmF0dXJlPTU1ZDliYTY5YmI3OWRjM2Y0NzI1NzgwYjJiMjhmNDc4MTg1NTZjYzA2ZWUzYzRiNzU1YWZhNjU4Mzc0M2ZiNWImWC1BbXotU2lnbmVkSGVhZGVycz1ob3N0JnJlc3BvbnNlLWNvbnRlbnQtdHlwZT1pbWFnZSUyRmdpZiJ9.tSIW1mD6UNKYPWhlrjOkMj-7bZk7Nfov8lzIiV8bKAw)

> 
**⚠️ Early Development Notice:** This project is in early development and is not yet ready for production use. Features may change, break, or be incomplete. Use at your own risk.

A powerful terminal-based AI assistant for developers, providing intelligent coding assistance directly in your terminal.

## Overview

[](#overview)
OpenCode is a Go-based CLI application that brings AI assistance to your terminal. It provides a TUI (Terminal User Interface) for interacting with various AI models to help with coding tasks, debugging, and more.

For a quick video overview, check out
[ OpenCode + Gemini 2.5 Pro: BYE Claude Code! I'm SWITCHING To the FASTEST AI Coder!](https://www.youtube.com/watch?v=P8luPmEa1QI)
[](https://www.youtube.com/watch?v=P8luPmEa1QI)

## Features

[](#features)

- **Interactive TUI**: Built with [Bubble Tea](https://github.com/charmbracelet/bubbletea) for a smooth terminal experience

- **Multiple AI Providers**: Support for OpenAI, Anthropic Claude, Google Gemini, AWS Bedrock, Groq, Azure OpenAI, and OpenRouter

- **Session Management**: Save and manage multiple conversation sessions

- **Tool Integration**: AI can execute commands, search files, and modify code

- **Vim-like Editor**: Integrated editor with text input capabilities

- **Persistent Storage**: SQLite database for storing conversations and sessions

- **LSP Integration**: Language Server Protocol support for code intelligence

- **File Change Tracking**: Track and visualize file changes during sessions

- **External Editor Support**: Open your preferred editor for composing messages

- **Named Arguments for Custom Commands**: Create powerful custom commands with multiple named placeholders

## Installation

[](#installation)
### Using the Install Script

[](#using-the-install-script)
# Install the latest version
curl -fsSL https://raw.githubusercontent.com/opencode-ai/opencode/refs/heads/main/install | bash

# Install a specific version
curl -fsSL https://raw.githubusercontent.com/opencode-ai/opencode/refs/heads/main/install | VERSION=0.1.0 bash
### Using Homebrew (macOS and Linux)

[](#using-homebrew-macos-and-linux)
brew install opencode-ai/tap/opencode
### Using AUR (Arch Linux)

[](#using-aur-arch-linux)
# Using yay
yay -S opencode-ai-bin

# Using paru
paru -S opencode-ai-bin
### Using Go

[](#using-go)
go install github.com/opencode-ai/opencode@latest
## Configuration

[](#configuration)
OpenCode looks for configuration in the following locations:

- `$HOME/.opencode.json`

- `$XDG_CONFIG_HOME/opencode/.opencode.json`

- `./.opencode.json` (local directory)

### Auto Compact Feature

[](#auto-compact-feature)
OpenCode includes an auto compact feature that automatically summarizes your conversation when it approaches the model's context window limit. When enabled (default setting), this feature:

- Monitors token usage during your conversation

- Automatically triggers summarization when usage reaches 95% of the model's context window

- Creates a new session with the summary, allowing you to continue your work without losing context

- Helps prevent "out of context" errors that can occur with long conversations

You can enable or disable this feature in your configuration file:

{
  "autoCompact": true // default is true
}
### Environment Variables

[](#environment-variables)
You can configure OpenCode using environment variables:

Environment Variable
Purpose

`ANTHROPIC_API_KEY`
For Claude models

`OPENAI_API_KEY`
For OpenAI models

`GEMINI_API_KEY`
For Google Gemini models

`GITHUB_TOKEN`
For Github Copilot models (see [Using Github Copilot](#using-github-copilot))

`VERTEXAI_PROJECT`
For Google Cloud VertexAI (Gemini)

`VERTEXAI_LOCATION`
For Google Cloud VertexAI (Gemini)

`GROQ_API_KEY`
For Groq models

`AWS_ACCESS_KEY_ID`
For AWS Bedrock (Claude)

`AWS_SECRET_ACCESS_KEY`
For AWS Bedrock (Claude)

`AWS_REGION`
For AWS Bedrock (Claude)

`AZURE_OPENAI_ENDPOINT`
For Azure OpenAI models

`AZURE_OPENAI_API_KEY`
For Azure OpenAI models (optional when using Entra ID)

`AZURE_OPENAI_API_VERSION`
For Azure OpenAI models

`LOCAL_ENDPOINT`
For self-hosted models

`SHELL`
Default shell to use (if not specified in config)

### Shell Configuration

[](#shell-configuration)
OpenCode allows you to configure the shell used by the bash tool. By default, it uses the shell specified in the `SHELL` environment variable, or falls back to `/bin/bash` if not set.

You can override this in your configuration file:

{
  "shell": {
    "path": "/bin/zsh",
    "args": ["-l"]
  }
}
This is useful if you want to use a different shell than your default system shell, or if you need to pass specific arguments to the shell.

### Configuration File Structure

[](#configuration-file-structure)
{
  "data": {
    "directory": ".opencode"
  },
  "providers": {
    "openai": {
      "apiKey": "your-api-key",
      "disabled": false
    },
    "anthropic": {
      "apiKey": "your-api-key",
      "disabled": false
    },
    "copilot": {
      "disabled": false
    },
    "groq": {
      "apiKey": "your-api-key",
      "disabled": false
    },
    "openrouter": {
      "apiKey": "your-api-key",
      "disabled": false
    }
  },
  "agents": {
    "coder": {
      "model": "claude-3.7-sonnet",
      "maxTokens": 5000
    },
    "task": {
      "model": "claude-3.7-sonnet",
      "maxTokens": 5000
    },
    "title": {
      "model": "claude-3.7-sonnet",
      "maxTokens": 80
    }
  },
  "shell": {
    "path": "/bin/bash",
    "args": ["-l"]
  },
  "mcpServers": {
    "example": {
      "type": "stdio",
      "command": "path/to/mcp-server",
      "env": [],
      "args": []
    }
  },
  "lsp": {
    "go": {
      "disabled": false,
      "command": "gopls"
    }
  },
  "debug": false,
  "debugLSP": false,
  "autoCompact": true
}
## Supported AI Models

[](#supported-ai-models)
OpenCode supports a variety of AI models from different providers:

### OpenAI

[](#openai)

- GPT-4.1 family (gpt-4.1, gpt-4.1-mini, gpt-4.1-nano)

- GPT-4.5 Preview

- GPT-4o family (gpt-4o, gpt-4o-mini)

- O1 family (o1, o1-pro, o1-mini)

- O3 family (o3, o3-mini)

- O4 Mini

### Anthropic

[](#anthropic)

- Claude 4 Sonnet

- Claude 4 Opus

- Claude 3.5 Sonnet

- Claude 3.5 Haiku

- Claude 3.7 Sonnet

- Claude 3 Haiku

- Claude 3 Opus

### GitHub Copilot

[](#github-copilot)

- GPT-3.5 Turbo

- GPT-4

- GPT-4o

- GPT-4o Mini

- GPT-4.1

- Claude 3.5 Sonnet

- Claude 3.7 Sonnet

- Claude 3.7 Sonnet Thinking

- Claude Sonnet 4

- O1

- O3 Mini

- O4 Mini

- Gemini 2.0 Flash

- Gemini 2.5 Pro

### Google

[](#google)

- Gemini 2.5

- Gemini 2.5 Flash

- Gemini 2.0 Flash

- Gemini 2.0 Flash Lite

### AWS Bedrock

[](#aws-bedrock)

- Claude 3.7 Sonnet

### Groq

[](#groq)

- Llama 4 Maverick (17b-128e-instruct)

- Llama 4 Scout (17b-16e-instruct)

- QWEN QWQ-32b

- Deepseek R1 distill Llama 70b

- Llama 3.3 70b Versatile

### Azure OpenAI

[](#azure-openai)

- GPT-4.1 family (gpt-4.1, gpt-4.1-mini, gpt-4.1-nano)

- GPT-4.5 Preview

- GPT-4o family (gpt-4o, gpt-4o-mini)

- O1 family (o1, o1-mini)

- O3 family (o3, o3-mini)

- O4 Mini

### Google Cloud VertexAI

[](#google-cloud-vertexai)

- Gemini 2.5

- Gemini 2.5 Flash

## Usage

[](#usage)
# Start OpenCode
opencode

# Start with debug logging
opencode -d

# Start with a specific working directory
opencode -c /path/to/project
## Non-interactive Prompt Mode

[](#non-interactive-prompt-mode)
You can run OpenCode in non-interactive mode by passing a prompt directly as a command-line argument. This is useful for scripting, automation, or when you want a quick answer without launching the full TUI.

# Run a single prompt and print the AI's response to the terminal
opencode -p "Explain the use of context in Go"

# Get response in JSON format
opencode -p "Explain the use of context in Go" -f json

# Run without showing the spinner (useful for scripts)
opencode -p "Explain the use of context in Go" -q
In this mode, OpenCode will process your prompt, print the result to standard output, and then exit. All permissions are auto-approved for the session.

By default, a spinner animation is displayed while the model is processing your query. You can disable this spinner with the `-q` or `--quiet` flag, which is particularly useful when running OpenCode from scripts or automated workflows.

### Output Formats

[](#output-formats)
OpenCode supports the following output formats in non-interactive mode:

Format
Description

`text`
Plain text output (default)

`json`
Output wrapped in a JSON object

The output format is implemented as a strongly-typed `OutputFormat` in the codebase, ensuring type safety and validation when processing outputs.

## Command-line Flags

[](#command-line-flags)

Flag
Short
Description

`--help`
`-h`
Display help information

`--debug`
`-d`
Enable debug mode

`--cwd`
`-c`
Set current working directory

`--prompt`
`-p`
Run a single prompt in non-interactive mode

`--output-format`
`-f`
Output format for non-interactive mode (text, json)

`--quiet`
`-q`
Hide spinner in non-interactive mode

## Keyboard Shortcuts

[](#keyboard-shortcuts)
### Global Shortcuts

[](#global-shortcuts)

Shortcut
Action

`Ctrl+C`
Quit application

`Ctrl+?`
Toggle help dialog

`?`
Toggle help dialog (when not in editing mode)

`Ctrl+L`
View logs

`Ctrl+A`
Switch session

`Ctrl+K`
Command dialog

`Ctrl+O`
Toggle model selection dialog

`Esc`
Close current overlay/dialog or return to previous mode

### Chat Page Shortcuts

[](#chat-page-shortcuts)

Shortcut
Action

`Ctrl+N`
Create new session

`Ctrl+X`
Cancel current operation/generation

`i`
Focus editor (when not in writing mode)

`Esc`
Exit writing mode and focus messages

### Editor Shortcuts

[](#editor-shortcuts)

Shortcut
Action

`Ctrl+S`
Send message (when editor is focused)

`Enter` or `Ctrl+S`
Send message (when editor is not focused)

`Ctrl+E`
Open external editor

`Esc`
Blur editor and focus messages

### Session Dialog Shortcuts

[](#session-dialog-shortcuts)

Shortcut
Action

`↑` or `k`
Previous session

`↓` or `j`
Next session

`Enter`
Select session

`Esc`
Close dialog

### Model Dialog Shortcuts

[](#model-dialog-shortcuts)

Shortcut
Action

`↑` or `k`
Move up

`↓` or `j`
Move down

`←` or `h`
Previous provider

`→` or `l`
Next provider

`Esc`
Close dialog

### Permission Dialog Shortcuts

[](#permission-dialog-shortcuts)

Shortcut
Action

`←` or `left`
Switch options left

`→` or `right` or `tab`
Switch options right

`Enter` or `space`
Confirm selection

`a`
Allow permission

`A`
Allow permission for session

`d`
Deny permission

### Logs Page Shortcuts

[](#logs-page-shortcuts)

Shortcut
Action

`Backspace` or `q`
Return to chat page

## AI Assistant Tools

[](#ai-assistant-tools)
OpenCode's AI assistant has access to various tools to help with coding tasks:

### File and Code Tools

[](#file-and-code-tools)

Tool
Description
Parameters

`glob`
Find files by pattern
`pattern` (required), `path` (optional)

`grep`
Search file contents
`pattern` (required), `path` (optional), `include` (optional), `literal_text` (optional)

`ls`
List directory contents
`path` (optional), `ignore` (optional array of patterns)

`view`
View file contents
`file_path` (required), `offset` (optional), `limit` (optional)

`write`
Write to files
`file_path` (required), `content` (required)

`edit`
Edit files
Various parameters for file editing

`patch`
Apply patches to files
`file_path` (required), `diff` (required)

`diagnostics`
Get diagnostics information
`file_path` (optional)

### Other Tools

[](#other-tools)

Tool
Description
Parameters

`bash`
Execute shell commands
`command` (required), `timeout` (optional)

`fetch`
Fetch data from URLs
`url` (required), `format` (required), `timeout` (optional)

`sourcegraph`
Search code across public repositories
`query` (required), `count` (optional), `context_window` (optional), `timeout` (optional)

`agent`
Run sub-tasks with the AI agent
`prompt` (required)

## Architecture

[](#architecture)
OpenCode is built with a modular architecture:

- **cmd**: Command-line interface using Cobra

- **internal/app**: Core application services

- **internal/config**: Configuration management

- **internal/db**: Database operations and migrations

- **internal/llm**: LLM providers and tools integration

- **internal/tui**: Terminal UI components and layouts

- **internal/logging**: Logging infrastructure

- **internal/message**: Message handling

- **internal/session**: Session management

- **internal/lsp**: Language Server Protocol integration

## Custom Commands

[](#custom-commands)
OpenCode supports custom commands that can be created by users to quickly send predefined prompts to the AI assistant.

### Creating Custom Commands

[](#creating-custom-commands)
Custom commands are predefined prompts stored as Markdown files in one of three locations:

**User Commands** (prefixed with `user:`):

```
$XDG_CONFIG_HOME/opencode/commands/

```

(typically `~/.config/opencode/commands/` on Linux/macOS)

or

```
$HOME/.opencode/commands/

```

**Project Commands** (prefixed with `project:`):

```
<PROJECT DIR>/.opencode/commands/

```

Each `.md` file in these directories becomes a custom command. The file name (without extension) becomes the command ID.

For example, creating a file at `~/.config/opencode/commands/prime-context.md` with content:

RUN git ls-files
READ README.md
This creates a command called `user:prime-context`.

### Command Arguments

[](#command-arguments)
OpenCode supports named arguments in custom commands using placeholders in the format `$NAME` (where NAME consists of uppercase letters, numbers, and underscores, and must start with a letter).

For example:

# Fetch Context for Issue $ISSUE_NUMBER

RUN gh issue view $ISSUE_NUMBER --json title,body,comments

... [Content truncated]