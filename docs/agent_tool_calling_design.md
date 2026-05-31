# Agent Tool Calling Design

## 设计目标

本轮目标是把原有“健身工具调用”从普通接口升级为可讲清楚、可调试、可扩展的 Agent Tool Calling 基础链路。用户输入自然语言后，服务端按“意图识别 -> 工具路由 -> 参数抽取 -> 参数校验 -> 工具执行 -> trace -> 二次 LLM 总结”的顺序处理，并在成功和失败场景都返回执行轨迹。

## 和普通工具接口的区别

普通工具接口通常要求调用方直接传 `toolName` 和结构化参数，服务端只负责执行。Agent Tool Calling 额外增加了工具 schema、自然语言路由、参数抽取、参数校验、统一 trace 和二次总结，让接口更接近真实 Agent 工程链路，也更适合面试讲解。

## 核心模块

- `AgentToolSchema`：描述标准工具名、旧工具名、意图、参数 schema、必填字段和示例。
- `AgentToolCall`：表示一次工具调用请求，包括标准 toolName、legacy toolName、原始用户问题、参数、置信度和是否需要二次 LLM。
- `AgentToolResult`：封装工具执行结果，包括成功状态、标准工具名、原始结果 JSON 和错误信息。
- `AgentTrace`：记录一次调用的 traceId、用户问题、意图、选中工具、参数、校验状态、工具状态、结果摘要、最终回答状态和创建时间。
- `AgentToolRouter`：基于规则和 schema 选择工具，并从中文自然语言中抽取基础参数。
- `AgentToolValidator`：校验必填字段、数值类型、范围、枚举值、日期格式和工具是否存在。
- `AgentToolExecutor`：把标准 toolName 适配到现有 `FitnessToolService`，复用原有计算和查询逻辑。

## 调用流程

```text
用户问题
  -> AgentToolRouter
  -> AgentToolValidator
  -> AgentToolExecutor
  -> FitnessToolService
  -> Tool Result
  -> LLM 二次总结
  -> Response / SSE trace event
```

`POST /fitness/tool/call` 只执行本地工具，不需要二次 LLM。`POST /chat/fitness-tool-send` 和 `POST /chat/fitness-tool-send-stream` 会在工具成功后继续调用 AI 总结，且保留原 AIQuotaService 扣减逻辑。

## 当前支持工具

| 标准 toolName | 兼容旧工具名 | 参数 |
| --- | --- | --- |
| `bmi_calculator` | `calculate_bmi` | `heightCm`, `weightKg` |
| `bmr_calculator` | `calculate_bmr` | `gender`, `age`, `heightCm`, `weightKg` |
| `tdee_calculator` | `calculate_tdee` | `gender`, `age`, `heightCm`, `weightKg`, `activityLevel` |
| `training_volume_calculator` | `calculate_training_volume` | `weight`, `reps`, `sets` |
| `training_record_query` | `get_recent_training_records` | `startDate`, `endDate`, `exerciseName` 可选 |

## 参数抽取策略

当前采用 regex/规则基础版，不引入复杂 NLP 依赖：

- `身高175cm`、`175厘米` -> `heightCm`
- `体重70kg`、`70公斤` -> `weightKg`
- `男` / `女` -> `gender`
- `22岁` -> `age`
- `每周训练4次` -> `activityLevel=moderate`
- `80kg做8次4组` -> `weight=80, reps=8, sets=4`
- 训练记录查询未提供日期时，默认查询最近 30 天

## trace 字段

trace 包含：

- `traceId`
- `type`
- `userMessage`
- `intent`
- `selectedTool`
- `arguments`
- `validationStatus`
- `validationError`
- `toolStatus`
- `toolResultSummary`
- `needSecondLLMCall`
- `finalAnswerStatus`
- `errorMessage`
- `createdAt`

trace 不记录 API Key、密码等敏感信息，`userMessage` 最多保留 500 字节。当前 trace 只返回给前端或通过 SSE `event: trace` 发送，暂不持久化。

## 错误处理策略

- 工具无法匹配：返回明确提示，chat 流式接口可 fallback 到普通 AI 教练回答。
- 参数缺失：返回 `缺少参数: heightCm, weightKg` 这类明确错误。
- 参数范围错误：返回具体字段范围，例如 `heightCm 范围应为 80-250`。
- 工具执行失败：保留工具结果并在 trace 中标记 `toolStatus=failed`。
- 二次 LLM 失败：返回工具结果和 AI 总结失败信息，不吞掉本地工具结果。

## 和 LLM Function Calling 的关系

当前 Router 是规则 + schema 的基础版。它的边界和 Function Calling 一致：先得到工具名和参数，再校验，再执行。后续可以把 `AgentToolRouter` 替换为 OpenAI Function Calling 或兼容协议输出，而 `AgentToolValidator`、`AgentToolExecutor` 和 `AgentTrace` 可以继续复用。

## MCP 扩展预留

当前不是 MCP Server。后续如果接入 MCP，可以把 `AgentToolSchema` 映射为 MCP tool schema，把 `AgentToolExecutor` 改造成 MCP tool dispatcher，并把 trace 持久化为调试日志。

## 当前限制

- ToolRouter 是规则基础版，不是完整 LLM Function Calling。
- 参数抽取是 regex/规则基础版，不是复杂 NLP。
- trace 暂不持久化。
- 当前不是完整 MCP Server。
- 当前不是多智能体系统。
