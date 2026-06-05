# Security And Redis Phase 1

This phase focuses only on backend security hardening and Redis integration. Agent Tool Calling, RAG, Function Calling, MCP, vector store replacement, and RPC decomposition remain out of scope for this phase.

## Completed Scope

- Registration now uses prepared SQL statements and stores `password_hash` plus `password_salt`.
- Login now loads `password_hash` and `password_salt` by username and verifies the submitted password with PBKDF2-HMAC-SHA256.
- The legacy `users.password` column is kept only for compatibility and is written as an empty string for new registrations.
- `AIHelper::executeCurl` no longer prints API keys.
- Chat message persistence now publishes structured JSON to RabbitMQ; the consumer writes chat rows with parameter binding.
- Redis client support was added with `GET`, `SET`, `SETEX`, `DEL`, `INCR`, and `EXPIRE`.
- Redis-backed session storage can be enabled with `SESSION_STORAGE=redis`.
- Basic fixed-window rate limiting was added for `/login`, `/register`, and selected `/chat/*` AI endpoints.

## Database Migration

Run:

```sql
ALTER TABLE users
    ADD COLUMN password_hash VARCHAR(255) DEFAULT '',
    ADD COLUMN password_salt VARCHAR(255) DEFAULT '';
```

The migration is stored at `docs/sql/security_redis_phase1.sql`.

## Redis Usage

Environment variables:

```env
REDIS_HOST=127.0.0.1
REDIS_PORT=6379
REDIS_PASSWORD=
SESSION_STORAGE=redis
```

Session keys use:

```text
session:{sessionId}
```

The default Redis session TTL is 7 days.

Rate limit keys use:

```text
ratelimit:{path}:{identity}
```

Current default policies:

- `/login`: 10 requests per IP per minute.
- `/register`: 5 requests per IP per minute.
- `/chat/send`, `/chat/send-stream`, `/chat/vector-rag-send`, `/chat/vector-rag-send-stream`, `/chat/fitness-tool-send`, `/chat/fitness-tool-send-stream`: 20 requests per session per minute.
- `/ping` is not rate limited.

For chat endpoints, the current middleware uses `sessionId` from the cookie and falls back to IP headers. A future phase can bind rate limiting directly to authenticated `userId` after middleware has access to loaded session data without creating a new response cookie.

## Middleware Containers

`docker-compose.yml` provides MySQL, Redis, and RabbitMQ. It does not containerize the C++ service yet.

```bash
docker compose --env-file .env.example up -d mysql redis rabbitmq
```

## Next Phase Reserved Work

- Upgrade rule-based ToolRouter toward LLM Function Calling output parsing.
- Keep Validator, Executor, and Trace boundaries.
- Persist Agent trace.
- Add tool timeout control.
- Standardize tool error codes.
- Emit richer Agent step events in SSE.
- Optionally map MCP schema.
