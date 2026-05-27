# 在 Qwen Code 中使用 Agent Router | AgentRouter | Developers

> Source: https://docs.agentrouter.org/qwencode.html
> Cached: 2026-05-27T06:08:35.278Z

---

# 在 Qwen Code 中使用 Agent Router [​](#在-qwen-code-中使用-agent-router)

## 安装 Qwen Code [​](#安装-qwen-code)

pnpmnpmyarnbash```
pnpm install -g @qwen-code/qwen-code
```

bash```
npm install -g @qwen-code/qwen-code
```

bash```
yarn global add @qwen-code/qwen-code
```

## 配置环境变量 [​](#配置环境变量)

在终端中输入如下命令，设置环境变量（[环境变量设置教程](https://www.java.com/zh-CN/download/help/path.html)）来使用 AgentRouter 的 OpenAI API :

Linux/macOSWindows PowerShellWindows CMDbash```
# 设置您的 AgentRouter API 密钥, 可以从 https://agentrouter.org/console/token 获取
export OPENAI_API_KEY="sk-xxx"
# 设置 AgentRouter 的 API 基础 URL
export OPENAI_BASE_URL="https://agentrouter.org/v1"  # 可选
export OPENAI_MODEL="gpt-5" # 可选，模型名 ，可以修改为 `deepseek-v3.1` , `deepseek-v3.2`
```

powershell```
# 设置您的 AgentRouter API 密钥, 可以从 https://agentrouter.org/console/token 获取
$env:OPENAI_API_KEY="sk-xxx"
# 设置 AgentRouter 的 API 基础 URL
$env:OPENAI_BASE_URL="https://agentrouter.org/v1"  # 可选
$env:OPENAI_MODEL="gpt-5" # 可选，模型名 ，可以修改为 `deepseek-v3.1` , `deepseek-v3.2`
```

bash```
# 设置您的 AgentRouter API 密钥, 可以从 https://agentrouter.org/console/token 获取
set OPENAI_API_KEY="sk-xxx"
# 设置 AgentRouter 的 API 基础 URL
set OPENAI_BASE_URL="https://agentrouter.org/v1"  # 可选
set OPENAI_MODEL="gpt-5" # 可选，模型名
```

IMPORTANT

请确保将 sk-xxx 替换为 AgentRouter 提供的 Token 。您可以在 [AgentRouter 控制台](https://agentrouter.org/console/token) 中获取 API Key。

## 直接启动使用 [​](#直接启动使用)

配置好环境变量后，使用终端打开你要操作的项目目录，然后运行如下命令启动 Qwen Code

bash```
# 进入项目目录
cd my-project

# 直接启动 Qwen Code
qwen
```

TIP

您可以将环境变量配置添加到您的 shell 配置文件中，避免每次手动设置：

Linux/macOSWindows PowerShellWindows CMDbash```
# 添加到 ~/.bashrc 或 ~/.zshrc 文件中
# 设置您的 AgentRouter API 密钥, 可以从 https://agentrouter.org/console/token 获取
export OPENAI_API_KEY="sk-xxx"
# 设置 AgentRouter 的 API 基础 URL
export OPENAI_BASE_URL="https://agentrouter.org/v1"  # 可选
export OPENAI_MODEL="gpt-5" # 可选，模型名 ，可以修改为 `deepseek-v3.1` , `deepseek-v3.2`
```

powershell```
# 添加到 PowerShell 配置文件 ($PROFILE)
# 设置您的 AgentRouter API 密钥, 可以从 https://agentrouter.org/console/token 获取
$env:OPENAI_API_KEY="sk-xxx"
# 设置 AgentRouter 的 API 基础 URL
$env:OPENAI_BASE_URL="https://agentrouter.org/v1"  # 可选
$env:OPENAI_MODEL="gpt-5" # 可选，模型名 ，可以修改为 `deepseek-v3.1` , `deepseek-v3.2`
```

bash```
# 设置您的 AgentRouter API 密钥, 可以从 https://agentrouter.org/console/token 获取
set OPENAI_API_KEY="sk-xxx"
# 设置 AgentRouter 的 API 基础 URL
set OPENAI_BASE_URL="https://agentrouter.org/v1"  # 可选
set OPENAI_MODEL="gpt-5" # 可选，模型名
```

:::