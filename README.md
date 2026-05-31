# My Cpp AI Service

一个基于 C++17 自研 HTTP 服务框架实现的 **C++ 高并发 AI Agent 应用服务平台**。项目以健身场景为业务载体，围绕用户体系、AI 对话、SSE 流式响应、RAG 知识库、真实 Embedding 向量检索、Agent 工具调用、AI 额度控制和可部署验证构建后端闭环，重点展示 C++ 后端工程能力与大模型应用落地能力。

## 项目亮点

- 自研 C++ HTTP Server：封装路由、Handler、Session、Middleware、SSE 流式响应等基础能力
- C++17 后端业务服务：不依赖 Spring / FastAPI 等成熟 Web 框架，完整实现请求分发和业务处理
- 面向健身场景的 AI Agent 后端：用户档案、训练日历、训练记录、AI 计划生成、知识库问答、工具调用
- SSE 流式输出：支持普通聊天、RAG 问答、训练计划生成、工具调用总结的流式响应
- Vector RAG 检索模块：EmbeddingClient、固定长度 chunk + overlap、C++ 内存向量索引、JSON 持久化、TopK cosine 检索、Prompt 增强回答
- 真实 Embedding 配置：支持 `EMBEDDING_API_KEY`、`EMBEDDING_BASE_URL`、`EMBEDDING_MODEL`，无 Key 环境自动回退 deterministic mock embedding
- 健身工具调用：BMI、BMR、TDEE、训练容量、训练记录查询等工具，支持 AI 二次总结
- 用户权限与 AI 免费额度：支持 user/admin 角色、普通用户免费次数、AI 使用日志
- MySQL 持久化：用户、聊天记录、健身档案、训练计划、训练记录、AI 使用记录落库
- 可部署验证：支持 Linux 服务器 CMake 构建，提供 SQL 迁移和 curl 验证方式

## 技术栈

- 语言：C++17
- 网络库：muduo
- 数据库：MySQL
- 构建：CMake
- AI 调用：HTTP / CURL
- 数据格式：JSON / nlohmann-json
- 流式协议：Server-Sent Events
- 其他：OpenSSL、RabbitMQ、OpenCV、ONNX Runtime

## 核心功能

### 1. 用户与会话

- 用户注册、登录、登出
- Session 登录态管理
- 关闭网页后可重新登录
- 新登录会覆盖旧登录，避免旧 Session 继续占用在线状态
- `/user/me` 查询当前用户信息、角色和 AI 免费额度
- `/user/ai-usage` 查询最近 AI 使用记录

### 2. AI 教练对话

- 普通 AI 聊天：`POST /chat/send`
- SSE 流式聊天：`POST /chat/send-stream`
- 多会话上下文管理
- 聊天记录持久化
- 支持不同模型类型切换

### 3. 健身档案

- 维护用户性别、年龄、身高、体重、训练目标、训练水平、每周训练天数、器械条件、伤病限制
- AI 训练计划生成会读取用户档案作为上下文
- 支持档案保存和查询

### 4. 训练日历与训练记录

- AI 根据用户档案生成未来训练计划
- 训练计划写入日历
- 支持按日期查看训练安排
- 支持保存训练动作、重量、次数、组数、RPE、RIR、休息时间、完成状态和训练感受
- 支持训练状态更新和最近训练记录查询

### 5. RAG 知识库与向量检索

- 支持导入健身知识文本
- 旧版 `/rag/index`、`/rag/search` 保留 Hash Embedding + JSON VectorStore，保证兼容
- 新版 `/rag/vector/index`、`/rag/vector/search` 使用 EmbeddingClient 生成 float embedding
- 文档按固定长度 chunk + overlap 切分，每个 chunk 保留 source、text、embedding、createdAt
- 当前 VectorStore 使用 C++ 内存向量索引 + JSON 持久化，默认路径为 `data/rag/vector_store.json`
- 检索使用 cosine similarity，按 TopK 返回 source、text、score
- `/rag/search` 只做本地检索，不消耗 AI 额度
- `/chat/rag-send` 和 `/chat/rag-send-stream` 会基于检索结果调用 AI 生成回答
- `/chat/vector-rag-send` 和 `/chat/vector-rag-send-stream` 会基于 vector RAG 检索结果调用 AI 生成回答

### 6. 健身工具调用

- 工具列表查询：`GET /fitness/tools/list`
- 本地工具直接调用：`POST /fitness/tool/call`
- 支持 BMI、BMR、TDEE、热量缺口、训练容量、训练计划查询、训练记录总结等
- `/chat/fitness-tool-send` 支持“问题 -> 工具匹配 -> 工具执行 -> AI 总结”
- 支持工具调用流式输出

### 7. Agent Tool Calling 标准化链路

本轮将原有健身工具调用升级为 Agent Tool Calling 基础版：

```text
用户问题
  -> AgentToolRouter 意图识别与工具路由
  -> AgentToolValidator 参数类型、必填项和范围校验
  -> AgentToolExecutor 复用 FitnessToolService 执行本地工具
  -> Tool Result
  -> LLM 二次总结
  -> Response / SSE
```

当前标准化 toolName：

- `bmi_calculator`：BMI 计算，兼容旧工具 `calculate_bmi`
- `bmr_calculator`：BMR 计算，兼容旧工具 `calculate_bmr`
- `tdee_calculator`：TDEE 计算，兼容旧工具 `calculate_tdee`
- `training_volume_calculator`：训练容量计算，兼容旧工具 `calculate_training_volume`
- `training_record_query`：训练记录查询，兼容旧工具 `get_recent_training_records`

当前参数抽取是 regex/规则基础版，支持从中文自然语言中解析 `cm`、`厘米`、`kg`、`公斤`、`岁`、`次`、`组` 等常见表达。例如：

- `我身高175cm，体重70kg，BMI是多少？` -> `heightCm=175, weightKg=70`
- `男，22岁，175cm，70kg，每周训练4次，帮我算TDEE` -> `gender=male, age=22, heightCm=175, weightKg=70, activityLevel=moderate`
- `卧推80kg做8次4组，训练容量是多少？` -> `weight=80, reps=8, sets=4`

`/fitness/tool/call`、`/chat/fitness-tool-send` 和 `/chat/fitness-tool-send-stream` 会返回或发送 `trace`，用于定位工具误选、参数缺失、校验失败、工具执行失败和二次总结失败。trace 当前只返回给前端，不持久化，不记录 API Key、密码等敏感信息，`userMessage` 会截断到 500 字节。

trace 示例：

```json
{
  "traceId": "agent-20260531-153012-1234",
  "type": "agent_tool_call",
  "userMessage": "我身高175cm，体重70kg，BMI是多少？",
  "intent": "calculate_bmi",
  "selectedTool": "bmi_calculator",
  "arguments": {
    "heightCm": 175,
    "weightKg": 70
  },
  "validationStatus": "success",
  "validationError": "",
  "toolStatus": "success",
  "toolResultSummary": "BMI=22.86",
  "needSecondLLMCall": true,
  "finalAnswerStatus": "success",
  "errorMessage": "",
  "createdAt": "2026-05-31 15:30:12"
}
```

Agent Tool Calling curl 示例：

```bash
curl --http1.1 -i -X POST http://127.0.0.1:8080/fitness/tool/call \
  -H "Content-Type: application/json" \
  -H "Cookie: sessionId=xxx" \
  -d '{"message":"我身高175cm，体重70kg，BMI是多少？"}'
```

```bash
curl --http1.1 -i -X POST http://127.0.0.1:8080/chat/fitness-tool-send \
  -H "Content-Type: application/json" \
  -H "Cookie: sessionId=xxx" \
  -d '{"message":"男，22岁，175cm，70kg，每周训练4次，帮我算TDEE"}'
```

```bash
curl --http1.1 -i -N -X POST http://127.0.0.1:8080/chat/fitness-tool-send-stream \
  -H "Content-Type: application/json" \
  -H "Cookie: sessionId=xxx" \
  -d '{"message":"卧推80kg做8次4组，训练容量是多少？"}'
```

面试讲解重点：

- ToolRouter 先用规则而不是完全依赖 LLM，是为了保证简历项目可本地验证、结果稳定、成本可控，后续可替换为 Function Calling。
- Validator 独立于 Router 和 Executor，避免错误参数进入业务工具，也便于返回清晰的缺参和范围错误。
- 工具调用失败时仍返回 trace，能定位失败发生在路由、校验、执行还是二次总结阶段。
- trace 让 Agent 调试从“只看最终答案”变成“看完整执行轨迹”。
- 当前不是完整 MCP Server，也不是多智能体系统；后续可扩展到 MCP Tool Protocol、trace 持久化、工具调用评测和多轮工具调用。

### 8. 用户权限与 AI 免费额度

- 用户角色：
  - `user`：普通用户
  - `admin`：超级用户
- 普通用户默认 5 次免费 AI 调用额度
- admin 不受额度限制
- AI 调用成功后才扣额度
- AI 调用失败不扣额度
- 记录 AI 使用日志，包括 endpoint、modelType、是否扣额度、是否成功、错误信息、创建时间

## AI 额度规则

会消耗额度的高成本接口：

- `POST /chat/send`
- `POST /chat/send-stream`
- `POST /fitness/calendar/generate-plan`
- `POST /fitness/calendar/generate-plan-stream`
- `POST /chat/rag-send`
- `POST /chat/rag-send-stream`
- `POST /chat/vector-rag-send`
- `POST /chat/vector-rag-send-stream`
- `POST /chat/fitness-tool-send`
- `POST /chat/fitness-tool-send-stream`

不会消耗额度的本地或基础接口：

- `GET /ping`
- `POST /echo`
- `/login`
- `/register`
- `/user/logout`
- `/fitness/profile`
- `/fitness/calendar/list`
- `/fitness/calendar/day`
- `/fitness/calendar/record/save`
- `/fitness/calendar/status/update`
- `/rag/index`
- `/rag/search`
- `/rag/vector/index`
- `/rag/vector/search`
- `/fitness/tools/list`
- `/fitness/tool/call`

## 项目结构

```text
.
├── HttpServer/                  # 自研 HTTP Server 框架
│   ├── include/http             # Request / Response / Server / StreamWriter
│   ├── include/router           # 路由系统
│   ├── include/session          # Session 管理
│   └── src
├── AIApps/ChatServer
│   ├── include/handlers         # 业务 Handler
│   ├── include/AIUtil           # AI 调用、策略、配置
│   ├── include/rag              # RAG 服务
│   ├── include/tools            # 健身工具服务
│   ├── include/auth             # 登录策略、AI 额度服务
│   ├── resource                 # 前端页面
│   └── src
├── docs/sql                     # 数据库迁移 SQL
└── tests                        # 轻量单元测试
```

## Vector RAG 架构

```text
用户问题
  -> EmbeddingClient 生成 query embedding
  -> JsonVectorStore / VectorStore 执行 TopK cosine 检索
  -> VectorRagService 拼接 RAG Prompt
  -> 复用现有 AIHelper / AIStrategy 调用大模型
  -> JSON 或 SSE 返回 answer + contexts
```

当前采用 C++ 内存向量索引 + JSON 持久化，适合简历项目和小规模知识库验证。后续可以替换为 FAISS、Milvus、Chroma、pgvector 等正式向量数据库。

### Embedding 配置

生产或投递展示建议配置真实 embedding API：

```bash
export EMBEDDING_API_KEY="your-api-key"
export EMBEDDING_BASE_URL="https://your-openai-compatible-host/v1/embeddings"
export EMBEDDING_MODEL="your-embedding-model"
```

如果未配置上述变量，`EmbeddingClient` 会使用 256 维 deterministic mock embedding，便于本地编译和无 Key 环境测试。mock embedding 不是生产效果，只用于验证索引、持久化、TopK 检索和接口链路。

### curl 示例

索引文档：

```bash
curl --http1.1 -i -X POST http://127.0.0.1:8080/rag/vector/index \
  -H "Content-Type: application/json" \
  -d '{"source":"bench_press.md","text":"杠铃卧推是一种经典胸部训练动作，主要刺激胸大肌，同时需要肩胛稳定，动作过程中应保持肩胛后缩下沉，避免肩关节过度前移。"}'
```

检索文档：

```bash
curl --http1.1 -i "http://127.0.0.1:8080/rag/vector/search?query=卧推怎么保护肩膀&topK=3"
```

Vector RAG 非流式问答：

```bash
curl --http1.1 -i -X POST http://127.0.0.1:8080/chat/vector-rag-send \
  -H "Content-Type: application/json" \
  -H "Cookie: sessionId=xxx" \
  -d '{"message":"卧推怎么保护肩膀？","topK":3}'
```

Vector RAG SSE 流式问答：

```bash
curl --http1.1 -i -X POST http://127.0.0.1:8080/chat/vector-rag-send-stream \
  -H "Content-Type: application/json" \
  -H "Cookie: sessionId=xxx" \
  -d '{"message":"卧推怎么保护肩膀？","topK":3}'
```

## 面试讲解点

- 为什么从 Hash Embedding 升级到真实 Embedding：Hash 适合本地演示，真实 embedding 更能表达语义相似度，便于扩展到通用知识库。
- chunk_size 和 overlap 怎么选：当前默认 500/80，控制上下文完整性和召回冗余，后续可按 Markdown 标题、段落或语义切分。
- cosine similarity 怎么计算：对 query embedding 和 chunk embedding 计算点积，再除以两个向量范数。
- TopK 检索结果如何拼进 Prompt：每条上下文带 `source` 和 `score`，要求模型优先基于资料回答，资料不足时明确说明。
- 如何防止 RAG 幻觉：Prompt 中约束“资料不足就说明不足”，并将来源和分数显式暴露给模型和接口调用方。
- 当前轻量 VectorStore 和 FAISS/Milvus/Chroma 的区别：本项目是 C++ 自研轻量检索模块，适合小规模验证，不是生产级向量数据库。
- SSE 流式 RAG 怎么做：先完成检索并发送 retrieved 事件，再构造 Prompt，最后复用现有流式模型调用逐块输出。

## 后续 Roadmap

- 将轻量 JSON VectorStore 替换为 FAISS、Milvus、Chroma 或 pgvector。
- 增加 RAG 评测与可观测性，包括检索耗时、TopK 命中 source、AI 调用耗时。
- 将健身工具调用继续升级为更标准的 Agent Tool Calling 链路。
