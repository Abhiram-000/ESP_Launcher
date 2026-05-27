# 在 Roo Code 中使用 Agent Router | AgentRouter | Developers

> Source: https://docs.agentrouter.org/roocode.html
> Cached: 2026-05-27T06:08:35.279Z

---

# 在 Roo Code 中使用 Agent Router [​](#在-roo-code-中使用-agent-router)

## 在 VSCode 中安装 RooCode [​](#在-vscode-中安装-roocode)

在 VScode 中搜索 [RooCode](https://marketplace.visualstudio.com/items?itemName=RooVeterinaryInc.roo-cline) 并安装。

访问 [https://agentrouter.org/console/token](https://agentrouter.org/console/token) ，获取你的 API Key

## 新增提供商 [​](#新增提供商)

完成 Roo Code 安装后，可以打开 Roo Code，选择配置提供商，新增一个 OpenAI Compatible 的供应商，具体配置项可参考：

- OpenAI 基础 URL： `https://agentrouter.org/v1`
- API 密钥：你在 [https://agentrouter.org/console/token](https://agentrouter.org/console/token) 创建的 Key
- 模型： `gpt-5` 也可以将其修改为 `glm-4.5`,`glm-4.6`,or `deepseek-v3.1` 等模型。

配置完成后，保存即可使用。