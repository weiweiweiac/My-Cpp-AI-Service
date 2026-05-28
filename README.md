# My Cpp AI Service

一个基于 C++17 自研 HTTP 服务框架实现的 AI 健身教练后端项目。项目围绕“用户登录、健身档案、训练计划、训练记录、AI 对话、RAG 知识库、工具调用、SSE 流式响应、用户权限与免费额度”构建完整业务闭环，重点展示 C++ 后端工程能力与 AI 应用落地能力。

## 项目亮点

- 自研 C++ HTTP Server：封装路由、Handler、Session、Middleware、SSE 流式响应等基础能力
- C++17 后端业务服务：不依赖 Spring / FastAPI 等成熟 Web 框架，完整实现请求分发和业务处理
- AI 健身教练业务闭环：用户档案、训练日历、训练记录、AI 计划生成、知识库问答、工具调用
- SSE 流式输出：支持普通聊天、RAG 问答、训练计划生成、工具调用总结的流式响应
- 本地轻量 RAG：文本切分、Hash Embedding、JSON VectorStore、TopK 检索、Prompt 增强回答
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

### 5. 本地轻量 RAG

- 支持导入健身知识文本
- 文本切分并保存到本地 JSON VectorStore
- 使用轻量 Hash Embedding 做本地检索
- `/rag/search` 只做本地检索，不消耗 AI 额度
- `/chat/rag-send` 和 `/chat/rag-send-stream` 会基于检索结果调用 AI 生成回答

### 6. 健身工具调用

- 工具列表查询：`GET /fitness/tools/list`
- 本地工具直接调用：`POST /fitness/tool/call`
- 支持 BMI、BMR、TDEE、热量缺口、训练容量、训练计划查询、训练记录总结等
- `/chat/fitness-tool-send` 支持“问题 -> 工具匹配 -> 工具执行 -> AI 总结”
- 支持工具调用流式输出

### 7. 用户权限与 AI 免费额度

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
