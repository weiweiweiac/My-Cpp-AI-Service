# Vector RAG Design

## 目标

本轮将原有 Hash Embedding + JSON VectorStore 的轻量 RAG 能力扩展为可配置真实 Embedding 的 vector RAG 基础版。旧 `/rag/index`、`/rag/search`、`/chat/rag-send`、`/chat/rag-send-stream` 保留，新增 `/rag/vector/index`、`/rag/vector/search`、`/chat/vector-rag-send`、`/chat/vector-rag-send-stream`，降低对现有登录、额度、SSE 和业务接口的影响。

## 为什么升级

Hash Embedding 适合本地演示和无依赖测试，但它主要靠字符/token 哈希碰撞表达相似度，语义表达能力有限。真实 Embedding 可以把 query 和 chunk 映射到同一语义向量空间，更适合 AI 应用开发、Agent 后端和简历项目展示。当前实现保留 mock fallback，保证没有 API Key 的环境也能验证索引、持久化和 TopK 检索链路。

## 核心组件

### EmbeddingClient

- 从环境变量读取 `EMBEDDING_API_KEY`、`EMBEDDING_BASE_URL`、`EMBEDDING_MODEL`。
- 配置完整时按 OpenAI-compatible embeddings 请求格式调用远程 API。
- 配置缺失或远程调用失败时，记录错误并回退到 deterministic mock embedding。
- mock embedding 固定 256 维，基于稳定 hash 将 UTF-8 token 映射到向量空间，并做 L2 normalize。

### JsonVectorStore

- 仍实现 `VectorStore` 接口，但新增 `addChunk` 和 `searchByEmbedding`。
- 内存中保存 `DocumentChunk` 列表。
- JSON 持久化默认写入 `data/rag/vector_store.json`。
- 每条 chunk 包含 `chunkId`、`docId`、`title`、`source`、`content`、`embedding`、`createdAt`。
- 检索时对 query embedding 和 chunk embedding 计算 cosine similarity，按分数降序返回 TopK。
- 对空向量、维度不一致、空文本、坏 JSON chunk 做跳过处理，避免坏数据导致进程崩溃。

### VectorRagService

- 负责 vector RAG 的业务编排。
- `indexText(source, text)`：校验 source/text，使用固定长度 chunk + overlap 切分，调用 EmbeddingClient 生成向量，写入 JsonVectorStore 并保存。
- `search(query, topK)`：生成 query embedding，调用 `searchByEmbedding`，TopK 默认 3、最大 10。
- `buildRagPrompt(question, contexts)`：拼接用户问题、source、score 和上下文，要求模型优先依据知识库回答，资料不足时明确说明“当前知识库资料不足”。

## 数据流程

```text
POST /rag/vector/index
  -> parse source/text
  -> TextSplitter split(text, 500, 80)
  -> EmbeddingClient embedBatch(chunks)
  -> JsonVectorStore addChunks + save

GET /rag/vector/search
  -> parse query/topK
  -> EmbeddingClient embedText(query)
  -> JsonVectorStore cosine TopK
  -> return source/text/score

POST /chat/vector-rag-send-stream
  -> require login
  -> vector search
  -> AIQuotaService checkBeforeAI
  -> build RAG prompt
  -> AIHelper requestStream
  -> SSE retrieved/message/done
  -> consume quota on success
```

## 当前边界

- 当前 VectorStore 是 C++ 轻量自研版本，不是 Milvus、FAISS、Chroma。
- 当前 mock embedding 只用于无 API Key 环境测试，真实语义效果需要配置 embedding API。
- 当前 chunk 策略是固定长度 + overlap，后续可扩展 Markdown 结构切分、段落切分或语义切分。
- 当前适合小规模知识库验证，大规模知识库建议迁移到 FAISS、Milvus、Chroma 或 pgvector。
