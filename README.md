# C++ 高并发 AI Agent 应用服务平台

基于 C++17、Muduo、MySQL、RabbitMQ、OpenSSL 构建的 AI Agent 后端服务，面向健身场景实现多模型对话、SSE 流式响应、Vector RAG 知识库问答、Agent Tool Calling、用户体系与 AI 调用额度控制。

这个仓库用于展示 C++ 后端工程能力和大模型应用落地能力。项目不是简单把大模型 API 包一层接口，而是把 AI 对话、RAG、工具调用、会话登录、额度控制、日志记录、数据库持久化和流式输出放进同一个可编译、可验证的 C++ 后端服务中。

## 核心亮点

- C++ 自研 HTTP 服务框架：封装请求解析、动态路由、Handler、Session、Middleware、CORS 和响应打包。
- Muduo Reactor 高并发网络模型：基于 Muduo `TcpServer` 承载 HTTP 请求处理和 SSE 长连接输出。
- SSE 流式响应：支持普通聊天、RAG 问答、训练计划生成和工具调用总结的实时输出。
- 多模型接入：通过策略模式和工厂模式封装豆包、阿里百炼等模型调用链路，并保留本地 RAG 问答链路。
- Vector RAG：实现文档切分、EmbeddingClient、轻量自研 VectorStore、TopK cosine 检索和 RAG Prompt 构造。
- Agent Tool Calling：提供 Tool schema、ToolRouter、Validator、Executor、工具结果和二次 LLM 总结链路。
- Agent trace：记录工具路由、参数抽取、校验、执行、二次总结等关键阶段，便于排查 Agent 行为。
- AI 额度控制：支持 user/admin 角色、普通用户免费额度、成功后扣减、失败不扣减和 AI 使用日志。
- MySQL 持久化：用户、会话、聊天记录、健身档案、训练计划、训练记录、AI 使用记录等数据落库。
- Redis Session / 限流：支持 Redis 存储登录 Session，并对登录、注册和关键 AI 接口做基础固定窗口限流。
- RabbitMQ 异步解耦：将聊天主链路与部分数据库写入、日志记录等后台操作解耦。
- ONNXRuntime / OpenCV 本地模型集成：保留本地图像模型推理和 OpenCV 处理接口。
- ASR/TTS API 集成：支持语音识别、语音合成相关调用封装。

## 技术栈

- 语言与构建：C++17、CMake
- 网络与协议：Muduo、HTTP、Server-Sent Events、OpenSSL
- 数据与中间件：MySQL、Redis、RabbitMQ、JSON / nlohmann-json
- AI 调用：HTTP / CURL、多模型策略封装、Embedding API
- 本地模型：ONNX Runtime、OpenCV
- 测试：CTest、轻量 C++ 单元测试

## 系统架构

```text
Client
  -> HttpServer
  -> ChatServer Router
  -> Auth / Session
  -> LLM Client
  -> Vector RAG
  -> Agent Tool Calling
  -> MySQL / RabbitMQ / Local Model
```

核心分层：

- `HttpServer/`：自研 HTTP Server 框架，负责网络连接、请求解析、路由、Session、Middleware 和 SSE Writer。
- `AIApps/ChatServer/`：业务服务层，负责用户体系、AI 对话、RAG、健身业务、工具调用和额度控制。
- `docs/sql/`：数据库表结构和迁移 SQL。
- `tests/`：RAG、SSE Writer、登录策略、工具调用、额度策略等单元测试。

## Vector RAG 流程

```text
文档文本
  -> Chunk 切分
  -> EmbeddingClient
  -> JsonVectorStore
  -> Query Embedding
  -> TopK Cosine Search
  -> RAG Prompt
  -> LLM
  -> SSE / JSON Response
```

当前 Vector RAG 新增接口：

- `POST /rag/vector/index`：导入文本并写入向量知识库。
- `GET /rag/vector/search`：按 query 和 topK 检索相关 chunk。
- `POST /chat/vector-rag-send`：检索后调用模型生成非流式回答。
- `POST /chat/vector-rag-send-stream`：检索后通过 SSE 输出回答。

实现边界：

- `EmbeddingClient` 支持远程 embedding API，未配置 API Key 时会回退到 deterministic mock embedding，便于本地验证链路。
- `JsonVectorStore` 使用内存向量列表加 JSON 持久化，默认路径为 `data/rag/vector_store.json`。
- chunk 默认使用固定长度和 overlap，后续可扩展 Markdown 结构切分、段落切分或语义切分。

## Agent Tool Calling 流程

```text
用户问题
  -> ToolRouter
  -> 参数抽取
  -> Validator
  -> Executor
  -> Tool Result
  -> Trace
  -> LLM 二次总结
  -> Response / SSE
```

当前标准化工具：

- `bmi_calculator`：BMI 计算，兼容旧工具 `calculate_bmi`
- `bmr_calculator`：BMR 计算，兼容旧工具 `calculate_bmr`
- `tdee_calculator`：TDEE 计算，兼容旧工具 `calculate_tdee`
- `training_volume_calculator`：训练容量计算，兼容旧工具 `calculate_training_volume`
- `training_record_query`：训练记录查询，兼容旧工具 `get_recent_training_records`

工具调用相关接口：

- `GET /fitness/tools/list`：查看可用工具。
- `POST /fitness/tool/call`：本地工具调用，只执行工具，不做二次 LLM 总结。
- `POST /chat/fitness-tool-send`：工具路由、工具执行、AI 二次总结。
- `POST /chat/fitness-tool-send-stream`：工具调用链路的 SSE 版本，会发送 `trace`、`tool_selected`、`tool_result`、`message`、`done` 等事件。

## 用户体系与 AI 额度

- 用户注册、登录、登出和 Session 登录态管理。
- `/user/me` 查询当前用户、角色和 AI 免费额度。
- `/user/ai-usage` 查询最近 AI 调用记录。
- 普通用户默认有免费 AI 调用额度，admin 不受额度限制。
- 高成本 AI 接口调用前检查额度，模型调用成功后扣减，失败不扣减。
- AI 使用日志记录 endpoint、modelType、是否扣额度、是否成功、错误信息和创建时间。

会消耗 AI 额度的典型接口：

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

不会消耗 AI 额度的典型接口：

- `GET /ping`
- `POST /echo`
- `POST /login`
- `POST /register`
- `POST /user/logout`
- `GET /user/me`
- `GET /user/ai-usage`
- `POST /rag/vector/index`
- `GET /rag/vector/search`
- `GET /fitness/tools/list`
- `POST /fitness/tool/call`

## 快速启动

以下命令面向 Linux / WSL / 服务器环境，依赖路径以实际机器为准。

### 1. 依赖安装

需要准备：

- C++17 编译器和 CMake
- Muduo
- MySQL Server、mysqlclient、MySQL Connector/C++
- Redis、hiredis
- OpenSSL
- CURL
- RabbitMQ、rabbitmq-c、SimpleAmqpClient
- OpenCV
- ONNX Runtime

如果依赖安装路径不同，请同步调整 `CMakeLists.txt` 中的 include 和 library 路径。

### 2. CMake 编译

```bash
cmake -S . -B build
cmake --build build -j1
```

### 3. 中间件启动

可以先复制并调整 `.env.example`，再启动 MySQL、Redis、RabbitMQ：

```bash
docker compose --env-file .env.example up -d mysql redis rabbitmq
```

`docker-compose.yml` 当前只提供中间件容器，C++ 服务仍按本机依赖编译运行。

### 4. 服务启动

```bash
./build/http_server
```

默认服务地址以 `AIApps/ChatServer/src/main.cpp` 中配置为准，当前 curl 示例使用 `http://127.0.0.1:8080`。

### 5. 健康检查

```bash
curl --http1.1 -i http://127.0.0.1:8080/ping
```

预期返回 `HTTP/1.1 200 OK`，body 为 `pong`。

## Demo 验证

核心验证接口：

- `GET /ping`
- `POST /register`
- `POST /login`
- `GET /user/me`
- `POST /rag/vector/index`
- `GET /rag/vector/search`
- `POST /chat/vector-rag-send`
- `POST /chat/vector-rag-send-stream`
- `POST /fitness/tool/call`
- `POST /chat/fitness-tool-send`
- `POST /chat/fitness-tool-send-stream`

完整可复制 curl 命令见 [docs/demo_curl.md](docs/demo_curl.md)。

## 项目结构

```text
.
├── HttpServer/                  # 自研 HTTP Server 框架
│   ├── include/http             # Request / Response / Server / StreamWriter
│   ├── include/router           # 路由系统
│   ├── include/session          # Session 管理
│   ├── include/middleware       # 中间件
│   └── src
├── AIApps/ChatServer
│   ├── include/handlers         # 业务 Handler
│   ├── include/AIUtil           # AI 调用、策略、配置、语音处理
│   ├── include/rag              # Hash RAG / Vector RAG
│   ├── include/tools            # Fitness Tool / Agent Tool Calling
│   ├── include/auth             # 登录策略、AI 额度服务
│   ├── resource                 # 前端页面
│   └── src
├── docs
│   ├── sql                      # 数据库迁移 SQL
│   ├── rag_vector_design.md
│   ├── agent_tool_calling_design.md
│   ├── interview_guide.md
│   ├── resume_project.md
│   ├── project_highlights.md
│   └── demo_curl.md
└── tests                        # 轻量单元测试
```

## 项目边界

为了让项目表述真实可信，当前边界明确如下：

- 当前 VectorStore 是轻量自研版本，不是 Milvus / FAISS / Chroma / pgvector 这类专用检索组件。
- 当前 Vector RAG 是基础版，主要验证索引、检索、Prompt 构造、JSON / SSE 回答链路。
- 当前 ToolRouter 是规则基础版，还没有接入模型原生 Function Calling。
- 当前 Agent trace 会随响应返回或通过 SSE 发送，暂不持久化。
- 当前不是 MCP Server，但模块边界为后续映射 MCP tool schema 预留了空间。
- 当前不是多智能体系统，重点是单服务内的工具路由、校验、执行和可观测链路。
- 当前 Redis 只用于 Session 和基础限流，不做复杂缓存，不缓存 AI 额度状态。
- 后续可扩展 FAISS / Milvus / Chroma / pgvector、Rerank、RAG 评测、LLM Function Calling、MCP、trace 持久化等能力。

## 安全与 Redis 第一阶段

本阶段只完成基础安全修复和 Redis 接入，详细说明见 [docs/security_redis_phase1.md](docs/security_redis_phase1.md)。

- 注册 SQL 已改为参数绑定，不再拼接用户名或密码。
- 注册时写入 `password_hash` 与 `password_salt`，不再写入明文密码。
- 登录按用户名读取 hash/salt 后校验密码，不再查询 `users.password`。
- 聊天消息异步入库改为 RabbitMQ JSON 消息，消费者使用参数绑定写 MySQL。
- `AIHelper::executeCurl` 不再打印 API Key。
- Session 可通过 `SESSION_STORAGE=redis` 切换为 Redis 存储。
- `/login`、`/register` 和关键 `/chat/*` AI 接口具备基础限流，超限返回 HTTP 429。

## 面试讲解点

面试官可能追问的 10 个问题：

1. 这个项目和普通调用大模型 API 的区别是什么？
2. 为什么用 C++ 做 AI 应用后端，而不是直接用 Python / FastAPI？
3. 自研 HTTP Server 做了哪些能力？和成熟 Web 框架相比边界在哪里？
4. SSE 流式响应在项目里如何实现？连接关闭和错误事件怎么处理？
5. Vector RAG 的索引、检索和 Prompt 构造流程是什么？
6. 当前轻量 VectorStore 和 FAISS / Milvus / Chroma 有什么差异？
7. ToolRouter、Validator、Executor 分别解决什么问题？
8. 为什么 ToolRouter 先使用规则和 regex，而不是一开始就依赖模型输出？
9. AI 额度控制怎么保证“成功后扣减、失败不扣减”？
10. RabbitMQ 在项目里解决了什么问题？哪些链路仍然是同步的？

更多回答口径见 [docs/interview_guide.md](docs/interview_guide.md)。

## 简历材料

- 简历项目描述：[docs/resume_project.md](docs/resume_project.md)
- 项目亮点整理：[docs/project_highlights.md](docs/project_highlights.md)
- Demo curl 验证：[docs/demo_curl.md](docs/demo_curl.md)
- Vector RAG 设计：[docs/rag_vector_design.md](docs/rag_vector_design.md)
- Agent Tool Calling 设计：[docs/agent_tool_calling_design.md](docs/agent_tool_calling_design.md)
