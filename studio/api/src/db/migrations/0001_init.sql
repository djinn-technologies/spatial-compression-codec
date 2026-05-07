-- src/db/migrations/0001_init.sql
--
-- Initial schema + Row-Level Security policies for the SCC Studio API.
--
-- Strategy: the application connects as a NON-superuser, NON-BYPASSRLS
-- role (`scc_app`). Every request opens a transaction that begins
-- with `SET LOCAL app.tenant_id = '<uuid>'`. RLS policies on every
-- table read that GUC and filter rows accordingly. A request that
-- forgets to set the GUC sees zero rows -- fail closed. [Ultrathink #1]

-- ---------------------------------------------------------------------------
-- Roles. Idempotent so re-running the migration doesn't blow up.
-- ---------------------------------------------------------------------------

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'scc_app') THEN
        CREATE ROLE scc_app WITH LOGIN NOSUPERUSER NOBYPASSRLS;
    END IF;
END
$$;

-- ---------------------------------------------------------------------------
-- Tables. (Mirrors src/db/schema.ts.)
-- ---------------------------------------------------------------------------

CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE IF NOT EXISTS tenants (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS api_keys (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id  UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    prefix     TEXT NOT NULL UNIQUE,
    hash       TEXT NOT NULL,
    label      TEXT NOT NULL,
    revoked    BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_used  TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS profiles (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id  UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    profile    TEXT NOT NULL,
    bit_depth  INTEGER NOT NULL,
    width      INTEGER NOT NULL,
    height     INTEGER NOT NULL,
    metadata   JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS sessions (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id         UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    profile_id        UUID REFERENCES profiles(id) ON DELETE SET NULL,
    sensor_id         TEXT NOT NULL,
    state             TEXT NOT NULL,
    agent_session_id  TEXT,
    width             INTEGER NOT NULL,
    height            INTEGER NOT NULL,
    bit_depth         INTEGER NOT NULL,
    started_at        TIMESTAMPTZ,
    ended_at          TIMESTAMPTZ,
    metadata          JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS recordings (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id    UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    session_id   UUID NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    storage_url  TEXT,
    size_bytes   INTEGER,
    frames_total INTEGER,
    finalized    BOOLEAN NOT NULL DEFAULT FALSE,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    finalized_at TIMESTAMPTZ
);

-- Indexes on tenant_id so RLS-filtered queries don't seq-scan.
CREATE INDEX IF NOT EXISTS api_keys_tenant_idx   ON api_keys   (tenant_id);
CREATE INDEX IF NOT EXISTS profiles_tenant_idx   ON profiles   (tenant_id);
CREATE INDEX IF NOT EXISTS sessions_tenant_idx   ON sessions   (tenant_id);
CREATE INDEX IF NOT EXISTS recordings_tenant_idx ON recordings (tenant_id);

-- ---------------------------------------------------------------------------
-- Row-Level Security.
--
-- The pattern: every table FORCEs RLS (ignored by superusers but
-- respected by the scc_app role). The policy reads
-- current_setting('app.tenant_id', true) and casts to UUID. The `true`
-- arg makes a missing setting return NULL rather than raising; the
-- cast then yields NULL, which compares unequal to any tenant_id ->
-- zero rows visible. Fail-closed.
-- ---------------------------------------------------------------------------

GRANT USAGE  ON SCHEMA public TO scc_app;
GRANT SELECT, INSERT, UPDATE, DELETE
    ON tenants, api_keys, profiles, sessions, recordings
    TO scc_app;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO scc_app;

ALTER TABLE tenants     ENABLE ROW LEVEL SECURITY;
ALTER TABLE tenants     FORCE  ROW LEVEL SECURITY;
ALTER TABLE api_keys    ENABLE ROW LEVEL SECURITY;
ALTER TABLE api_keys    FORCE  ROW LEVEL SECURITY;
ALTER TABLE profiles    ENABLE ROW LEVEL SECURITY;
ALTER TABLE profiles    FORCE  ROW LEVEL SECURITY;
ALTER TABLE sessions    ENABLE ROW LEVEL SECURITY;
ALTER TABLE sessions    FORCE  ROW LEVEL SECURITY;
ALTER TABLE recordings  ENABLE ROW LEVEL SECURITY;
ALTER TABLE recordings  FORCE  ROW LEVEL SECURITY;

-- tenants: a tenant can only see itself.
CREATE POLICY tenants_self ON tenants
    USING      (id = NULLIF(current_setting('app.tenant_id', true), '')::uuid)
    WITH CHECK (id = NULLIF(current_setting('app.tenant_id', true), '')::uuid);

-- api_keys / profiles / sessions / recordings: filter by tenant_id.
CREATE POLICY api_keys_tenant ON api_keys
    USING      (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid)
    WITH CHECK (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid);

CREATE POLICY profiles_tenant ON profiles
    USING      (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid)
    WITH CHECK (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid);

CREATE POLICY sessions_tenant ON sessions
    USING      (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid)
    WITH CHECK (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid);

CREATE POLICY recordings_tenant ON recordings
    USING      (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid)
    WITH CHECK (tenant_id = NULLIF(current_setting('app.tenant_id', true), '')::uuid);
