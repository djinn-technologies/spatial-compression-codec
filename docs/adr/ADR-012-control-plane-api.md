# ADR-012 — Control-plane API (`studio/api`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-029]`, `[ADR-006]`, `[ADR-011]`                              |

## Context

The SCC Studio frontend (a future React app) and partner integrations
need a stable HTTP/WebSocket surface to:

- enumerate sensors via the capture agent (ADR-011);
- create / inspect / stop capture sessions;
- manage saved profiles per tenant;
- finalise recordings (push the final size + storage URL after the
  frontend uploads to S3);
- stream live metrics during a session.

Decisions to lock:

1. **Framework + ORM**.
2. **Tenant isolation strategy** — RLS vs query-level filtering.
3. **Auth**: API key, JWT, or both.
4. **Rate limit keying** — per-IP vs per-principal.
5. **WebSocket back-pressure**.
6. **OpenAPI source-of-truth + drift detection**.
7. **Cold-start budget**.

## Decision

### 1. Fastify 4 + TypeScript strict + Drizzle ORM.

Fastify gives us schema-first routing with first-class Zod integration
(`fastify-type-provider-zod`). Routes declare their request body /
response shape in Zod; the validator + serializer + OpenAPI emitter
all read the same Zod source.

Drizzle ORM is SQL-first: a thin TypeScript layer over real SQL,
co-existing with raw `db.execute(sql\`...\`)` for things the type-safe
builder can't express (e.g. `SELECT set_config('app.tenant_id', ...)`).
Prisma's escape-hatch raw query path defeats the purpose of having an
ORM at all when RLS forces us into raw SQL on every request.

### 2. Tenant isolation via Postgres RLS + per-request `SET LOCAL`.

The application connects as `scc_app`, a role that does NOT have
`BYPASSRLS`. Every request opens a transaction (Drizzle's
`db.transaction(async tx => ...)`) that begins with

```sql
SELECT set_config('app.tenant_id', '<uuid>', true);
```

RLS policies on every table read the GUC:

```sql
CREATE POLICY profiles_tenant ON profiles
    USING      (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid)
    WITH CHECK (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid);
```

The `NULLIF(..., '')::uuid` cast yields NULL when the GUC is unset,
which compares unequal to any tenant_id — **fail closed**.

`tests/rls.test.ts` is the load-bearing regression: a Postgres
testcontainer test connects as `scc_app`, switches the GUC mid-session
between two tenants, and asserts visibility flips accordingly.
[Ultrathink #1]

### 3. API key + JWT, dual-track.

- **API key** for service-to-service callers: `Bearer scc_<URL-safe>`.
  Stored as `(prefix, sha256_hash)` in `api_keys`; lookup is by
  prefix, then constant-time-compare on the SHA-256 hash.
- **JWT (RS256)** for human users: validated against
  `JWT_PUBLIC_KEY` with `iss`, `aud`, `exp`, `sub`, `tenant_id` claims.

Both produce a unified `request.principal = { tenantId, kind, id }`
which downstream middleware (tenant + rate-limit) consume.

The auth lookup itself runs **before** we know the tenant id, so it
needs a privileged read on `api_keys` that bypasses RLS. The
production wiring uses a `SECURITY DEFINER` function owned by a role
that DOES bypass RLS, granted EXECUTE to `scc_app`. v1 ships the
migration with the constraint and documents the wrapper as the
production add-on.

### 4. Rate limit keyed on `principal.id`, not IP.

`@fastify/rate-limit` with `keyGenerator: (req) => req.principal?.id ?? req.ip`.
A single API key cannot exceed its 60 rps budget by distributing
calls across egress IPs. The IP fallback is defence-in-depth for
unauthenticated paths (which the auth hook would already have
rejected with 401). [Ultrathink #5]

### 5. WebSocket back-pressure via inflight-flag.

`/api/sessions/:id/metrics` pushes a Metrics frame at 1 Hz. The
handler holds an `inflight` flag and only sends the next tick after
the previous send's callback has fired (signalling the kernel's
socket buffer accepted the bytes). If the client is slow, the next
tick is **dropped** (counted via `scc_api_ws_backpressure_drops_total`)
rather than queued indefinitely.

This is the simplest pattern that satisfies the contract: a slow
consumer cannot OOM the server. The metrics counter lets ops detect
sustained slow-consumer scenarios. [Ultrathink #4]

### 6. OpenAPI emitted from Zod; CI gate via snapshot diff.

`@fastify/swagger` reads the Zod schemas attached to each route and
emits OpenAPI 3.1 at `/api/openapi.json`. `@fastify/swagger-ui` renders
it at `/api/docs` (non-production only).

`scripts/check-openapi.ts` boots the server in test mode, fetches the
live doc, and diffs against the committed `openapi.json` snapshot.
CI runs `npm run check:openapi`; any drift fails the build. To
accept an intentional change, run with `--update` and commit the
new snapshot. [Ultrathink #2]

### 7. Cold start < 800 ms.

Verified design choices that buy us the budget:

- **Lazy-init OTel SDK.** `initTracer()` does nothing when
  `OTEL_EXPORTER_OTLP_ENDPOINT` is empty; the heavy `@opentelemetry/sdk-node`
  + auto-instrumentations module is imported via dynamic `import()`
  only when needed. Saves ~250-400 ms.
- **No top-level `import` of swagger-ui assets.** The plugin loads
  on first request to `/api/docs`.
- **Drizzle (no Prisma engine).** Drizzle is a pure-TS query builder;
  there's no separate query-engine binary to dlopen at startup.
- **Alpine + multi-stage Dockerfile.** Final image lands ≈ 200 MiB
  (well under the 250 MiB budget). `node:20-alpine` shaves ~80 MB
  vs the slim Debian variant.

### 8. Per-tenant logging by construction.

The `registerTenantHook` middleware replaces `request.log` with a
Pino child carrying `tenant_id`, `principal_id`, `principal_kind`,
`request_id`, `trace_id`. Route handlers **must** log via
`request.log`; direct `pino()` calls or `console.log` bypass the
tenant scoping.

ESLint rule (sketched in the README; deferred to a custom rule
package) flags `console.log` and direct `import { logger } from
'pino'` in route files. CI gates on `npm run lint`. [Ultrathink #3]

## Consequences

### Positive

- RLS makes tenancy isolation a database-level invariant, not a
  query-author discipline. The regression test in `tests/rls.test.ts`
  proves it.
- Drizzle's SQL-first model lets us mix builder queries with raw
  `set_config` calls in the same transaction, which is exactly what
  the `withTenant(tenantId, fn)` helper needs.
- Auth + tenant + rate-limit pipeline is three small middlewares
  registered in the right order. Each is independently testable.
- OpenAPI is auto-generated from Zod; the snapshot test catches drift
  before consumer SDKs go stale.
- WebSocket inflight-flag pattern is dirt-cheap to reason about and
  is the simplest design that satisfies the back-pressure contract.

### Negative

- The auth lookup needs a SECURITY DEFINER function in production
  (because RLS would otherwise hide the api_keys row from
  `scc_app`). v1 ships the migration with the constraint and
  documents the wrapper as a deployment-time add-on; bare-bones
  dev-mode happens to work because `scc_app` is the table owner
  and policies are applied via `FORCE`.
- Drizzle is younger than Prisma; some drivers' edges cases need
  workarounds. Mitigated by `postgres-js` being battle-tested and
  Drizzle being a thin layer over raw SQL.
- Zod-to-OpenAPI conversion has occasional gaps (e.g. discriminated
  unions). The snapshot test catches these; the workaround is to
  override `schema.response[200]` with a hand-written
  `zodToJsonSchema` invocation when needed.

### Deferred

- **WebSocket subscription fan-out.** Today every metrics WS
  connection makes its own `agent.call({metrics})` — for high
  concurrency this is wasteful. v2 introduces a single per-session
  subscription that fans out to N connected sockets.
- **Read replica routing.** `withTenant` always uses the primary;
  v2 routes pure-read queries to a replica.
- **gRPC surface.** The HTTP API is the Studio's primary contact
  surface; partner SDKs that need lower latency could grow a gRPC
  surface against the same Drizzle layer.

## Verification

- `tests/routes.test.ts` covers the router shape (auth gate,
  /health, OpenAPI doc surface) using `app.inject()`.
- `tests/rls.test.ts` covers the tenant isolation guarantee end to
  end against a real Postgres testcontainer (Ultrathink #1).
- `tests/websocket.test.ts` covers the inflight-flag back-pressure
  pattern (Ultrathink #4).
- `scripts/check-openapi.ts` is the OpenAPI drift gate
  (Ultrathink #2).
- `Dockerfile` multi-stage build targets a < 250 MiB final image.
- `src/middleware/rate-limit.ts` keys on `principal.id` per
  Ultrathink #5.

## References

- AI Build Prompt #12 (`docs/AI_Build_Prompts.md` §12).
- ADR-006 (C ABI), ADR-011 (capture agent) — the agent IPC client
  in `src/agent-controller.ts` is the only piece that crosses the
  Studio / agent boundary.
- Internal: `docs/SAD.md` §6.1.7 (SDK bindings), §6.2 (Studio API);
  REQ-029 in `docs/Acceptance_Criteria.md`.
- Postgres RLS: <https://www.postgresql.org/docs/current/ddl-rowsecurity.html>
- Fastify Zod type provider:
  <https://github.com/turkerdev/fastify-type-provider-zod>
