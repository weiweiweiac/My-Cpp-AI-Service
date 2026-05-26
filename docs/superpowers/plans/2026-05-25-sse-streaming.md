# SSE Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add SSE streaming support without breaking existing synchronous HTTP handlers.

**Architecture:** Add a small `HttpStreamWriter` around `TcpConnectionPtr`, add exact-match stream routes to `Router`, and branch in `HttpServer::onRequest` before the normal one-shot `HttpResponse` path. Chat streaming uses the same session and `AIHelper` state as `/chat/send`, with synchronous endpoints kept as fallback.

**Tech Stack:** C++17, Muduo TCP connections, libcurl, nlohmann/json, browser `fetch` + `ReadableStream`.

---

### Task 1: SSE Writer And Route Plumbing

**Files:**
- Create: `HttpServer/include/http/HttpStreamWriter.h`
- Create: `HttpServer/src/http/HttpStreamWriter.cpp`
- Modify: `HttpServer/include/router/Router.h`
- Modify: `HttpServer/src/router/Router.cpp`
- Modify: `HttpServer/include/http/HttpServer.h`
- Modify: `HttpServer/src/http/HttpServer.cpp`
- Test: `tests/http_stream_writer_test.cpp`

- [ ] Write a failing test for SSE event formatting and JSON data escaping.
- [ ] Implement `HttpStreamWriter` static formatting helpers and connection send methods.
- [ ] Add exact stream callbacks to `Router`.
- [ ] Add `GetStream` and `PostStream` to `HttpServer`.
- [ ] Branch stream routes in `HttpServer::onRequest` while preserving normal route behavior.

### Task 2: Mock And Chat Stream Endpoints

**Files:**
- Create: `AIApps/ChatServer/include/handlers/ChatStreamHandler.h`
- Create: `AIApps/ChatServer/src/handlers/ChatStreamHandler.cpp`
- Modify: `AIApps/ChatServer/include/ChatServer.h`
- Modify: `AIApps/ChatServer/src/ChatServer.cpp`
- Modify: `AIApps/ChatServer/include/AIUtil/AIHelper.h`
- Modify: `AIApps/ChatServer/src/AIUtil/AIHelper.cpp`
- Modify: `AIApps/ChatServer/include/AIUtil/AIStrategy.h`
- Modify: `AIApps/ChatServer/src/AIUtil/AIStrategy.cpp`

- [ ] Add `GET /stream/mock` with delayed `message`, `status`, and `done` events.
- [ ] Add `POST /chat/send-stream` with login check, request parsing, SSE errors, and done event.
- [ ] Add `AIHelper::chatStream` and libcurl streaming callback for OpenAI-compatible providers.
- [ ] Fall back to one-chunk synchronous output for unsupported model shapes.

### Task 3: Frontend Stream Consumption

**Files:**
- Modify: `AIApps/ChatServer/resource/AI.html`

- [ ] Add SSE parser that buffers `ReadableStream` chunks by blank-line event boundary.
- [ ] Make existing send flow prefer `/chat/send-stream` for existing sessions.
- [ ] Keep `/chat/send` and `/chat/send-new-session` as fallbacks.

### Task 4: Verification

**Commands:**
- `cmake --build build --config Debug --target http_stream_writer_test`
- `ctest --test-dir build -C Debug --output-on-failure`
- `cmake --build build --config Debug --target http_server`
- `curl --http1.1 -N http://127.0.0.1:8080/stream/mock`

- [ ] Run available build/test commands and record blockers if local dependencies are missing.
