# Interview Guide

## Agent Tool Calling 一分钟解释

这个项目里的工具调用不是单纯把 BMI/TDEE 做成普通接口，而是按 Agent 工具调用链路做了一层工程化封装。用户输入自然语言后，`AgentToolRouter` 会根据工具 schema 和规则识别意图并选择工具，参数抽取模块会从中文文本中解析身高、体重、年龄、训练组数等参数，`AgentToolValidator` 做类型和范围校验，`AgentToolExecutor` 复用已有本地工具完成计算。对于 AI 对话接口，工具结果会再交给大模型做二次总结。整个过程会返回 trace，包括意图、选中的工具、参数、校验状态、工具执行状态和最终回答状态，方便调试工具误选、参数缺失和执行失败。

## 为什么不是简单 if-else 工具接口

简单 if-else 只能根据关键词直接调用某个函数，缺少 schema、参数标准化、校验、错误阶段定位和 trace。现在的实现把路由、校验、执行和观测分开，后续可以把规则路由替换为 LLM Function Calling 或 MCP 协议，而不用重写底层业务工具。

## Router、Validator、Executor 分别解决什么问题

- `AgentToolRouter`：解决“用户想调用哪个工具”和“自然语言里有哪些参数”的问题。
- `AgentToolValidator`：解决“参数是否完整、类型是否正确、范围是否合理”的问题，防止脏参数进入业务逻辑。
- `AgentToolExecutor`：解决“标准 toolName 如何复用现有本地工具”的问题，不重复实现 BMI、BMR、TDEE 或训练容量计算。

## trace 有什么用

trace 记录一次工具调用的完整轨迹，包括 traceId、用户问题、意图、选中工具、参数、校验状态、工具状态、结果摘要和最终回答状态。它能帮助排查三类问题：工具选错、参数抽取失败、工具或二次总结执行失败。对于 Agent 项目来说，trace 是从“只看最终答案”升级到“看执行过程”的关键可观测性设计。

## 和 OpenAI Function Calling / MCP 的关系

当前实现是规则基础版，不是完整 OpenAI Function Calling，也不是 MCP Server。但模块边界是按这些协议预留的：schema 描述工具，router 产生工具名和参数，validator 做安全校验，executor 执行工具。后续可以把 router 替换成 Function Calling 输出，或者把 schema/executor 映射到 MCP Tool Protocol。

## 当前不足和后续优化

当前 ToolRouter 依赖关键词规则，参数抽取依赖 regex，覆盖的是常见中文表达；trace 只返回给前端，暂不持久化；当前也不是多智能体系统。后续可以扩展 LLM Function Calling、MCP Tool Protocol、trace 持久化、工具调用评测、Redis 缓存和限流，以及多轮工具调用。
