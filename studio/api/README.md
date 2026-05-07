# SCC Studio control-plane API

Fastify 4 + TypeScript strict + Drizzle ORM + Postgres RLS.

## Quick start

```bash
cp .env.example .env                # then edit DATABASE_URL, JWT_PUBLIC_KEY, etc.
npm install
npm run db:migrate                  # apply 0001_init.sql + RLS policies
npm run dev                         # tsx watch on src/index.ts
# -> Fastify listening at http://0.0.0.0:8080
# -> Swagger UI at http://localhost:8080/api/docs (non-prod only)
```

## Routes

| Method | Path                                    | Notes                                           |
|--------|-----------------------------------------|-------------------------------------------------|
| GET    | `/api/sensors`                          | Lists sensors via the agent IPC.                |
| POST   | `/api/sessions`                         | Creates a capture session; talks to the agent.  |
| GET    | `/api/sessions/:id`                     | Reads one session (RLS-filtered).               |
| DELETE | `/api/sessions/:id`                     | Stops + finalises.                              |
| WS     | `/api/sessions/:id/metrics`             | Live metrics stream (1 Hz).                     |
| GET    | `/api/profiles`                         |                                                 |
| POST   | `/api/profiles`                         |                                                 |
| GET    | `/api/profiles/:id`                     |                                                 |
| PATCH  | `/api/profiles/:id`                     |                                                 |
| DELETE | `/api/profiles/:id`                     |                                                 |
| POST   | `/api/recordings/:id/finalize`          |                                                 |
| GET    | `/health`                               | No-auth liveness probe.                         |
| GET    | `/api/openapi.json`                     | OpenAPI 3.1 doc.                                |
| GET    | `/api/docs`                             | Swagger UI (non-prod only).                     |

## Auth

Every API route requires `Authorization: Bearer <token>`:

- **API key**: `Bearer scc_<URL-safe-32-byte>`. Looked up by 12-char prefix
  + constant-time hash compare.
- **JWT** (RS256): `Bearer <jwt>`. Verified against `JWT_PUBLIC_KEY` with
  required `iss`, `aud`, `exp`, `sub`, and `tenant_id` claims.

Both yield `request.principal = { tenantId, kind, id }`. Routes consume
only `request.tenantId` plus `request.log` (which is a Pino child
logger pre-stamped with `tenant_id`, `principal_id`, `request_id`,
`trace_id`). [Ultrathink #3]

## Tenant isolation

Postgres RLS. The app role (`scc_app`) does NOT have `BYPASSRLS`. Every
request opens a transaction that begins with
`SET LOCAL app.tenant_id = '<uuid>'`; RLS policies on every table read
that GUC and filter rows. A request that forgets to set the GUC sees
zero rows -- fail closed. [Ultrathink #1]

The `tests/rls.test.ts` Postgres-testcontainer regression test proves
this end-to-end: the test connects as `scc_app`, switches the GUC
mid-session between two tenants, and asserts visibility flips
accordingly.

## Rate limit

`@fastify/rate-limit` keyed on `request.principal.id`, NOT on IP. A
single API key cannot exceed its budget by distributing calls across
egress IPs. [Ultrathink #5]

Defaults: 60 rps per principal; configurable via `RATE_LIMIT_MAX` and
`RATE_LIMIT_WINDOW`.

## WebSocket back-pressure

`/api/sessions/:id/metrics` uses an inflight-flag pattern: only push
the next 1 Hz tick after the previous `socket.send` callback has
fired. If the client is slow, ticks are dropped rather than queued
indefinitely -- a slow consumer cannot OOM the server.
[Ultrathink #4]

## OpenAPI sync gate

`scripts/check-openapi.ts` boots the server in test mode, fetches
`/api/openapi.json`, and diffs against the committed `openapi.json`
snapshot. CI fails on drift. To accept an intentional change, run
`npm run check:openapi -- --update` and commit. [Ultrathink #2]

## Observability

- **Logs:** Pino. Production: NDJSON. Dev: pretty.
- **Traces:** OpenTelemetry SDK with OTLP gRPC exporter (lazy-init,
  active only when `OTEL_EXPORTER_OTLP_ENDPOINT` is set).
- **Metrics:** OTel meter exposes counters / histograms via the same
  OTLP pipeline. `scc_api_ws_backpressure_drops_total` is the
  load-bearing slow-consumer signal.

## 12-factor

All config lives in `process.env`. `loadConfig()` validates via Zod at
startup; missing or invalid values throw synchronously so the
container fails fast (matches Kubernetes CrashLoopBackoff).
`.env.example` is shipped; `.env` is git-ignored.

## Docker

```bash
docker build -t scc-studio-api .
# Image size budget: < 250 MiB. Multi-stage; node:20-alpine; prod-only deps.

docker run --rm -p 8080:8080 --env-file .env scc-studio-api
```

## Tests

```bash
npm test                # vitest -- routes + websocket + RLS testcontainer
npm run lint            # eslint
npm run typecheck       # tsc --noEmit
npm run check:openapi   # OpenAPI snapshot diff
```

## License

Apache-2.0.
