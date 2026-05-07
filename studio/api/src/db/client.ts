// src/db/client.ts
//
// Drizzle + postgres-js client. Exposes a `withTenant(tenantId, fn)`
// helper that runs `fn` inside a transaction with `SET LOCAL app.tenant_id`
// so RLS policies see the right tenant.

import { drizzle } from 'drizzle-orm/postgres-js';
import { sql } from 'drizzle-orm';
import postgres from 'postgres';

import * as schema from './schema.js';

export type Db = ReturnType<typeof drizzle<typeof schema>>;

let _client: ReturnType<typeof postgres> | null = null;
let _db: Db | null = null;

export function initDb(databaseUrl: string, maxPoolSize: number): Db {
    if (_db) return _db;
    _client = postgres(databaseUrl, {
        max: maxPoolSize,
        idle_timeout: 30,
        max_lifetime: 60 * 30,
        // The role must NOT have BYPASSRLS; the app explicitly sets
        // app.tenant_id per request.
        prepare: false,
    });
    _db = drizzle(_client, { schema });
    return _db;
}

export function getDb(): Db {
    if (!_db) {
        throw new Error('initDb() not called -- check src/index.ts startup order');
    }
    return _db;
}

export async function closeDb(): Promise<void> {
    if (_client) {
        await _client.end({ timeout: 5 });
        _client = null;
        _db = null;
    }
}

/**
 * Run `fn` inside a Postgres transaction with `SET LOCAL app.tenant_id`
 * already applied. RLS policies on every table read this GUC and
 * filter rows accordingly. [Ultrathink #1]
 *
 * The GUC is reset at transaction commit/rollback automatically (the
 * `LOCAL` keyword scopes it to the transaction).
 */
export async function withTenant<T>(
    tenantId: string,
    fn: (tx: Db) => Promise<T>,
): Promise<T> {
    const db = getDb();
    return await db.transaction(async (tx) => {
        await tx.execute(sql`SELECT set_config('app.tenant_id', ${tenantId}, true)`);
        return await fn(tx as unknown as Db);
    });
}

/**
 * Privileged helper for the auth path -- the API-key lookup must run
 * BEFORE we know the tenant, so we use the privileged role's
 * BYPASSRLS-via-superuser-or-direct-connection. In production this
 * reads from a dedicated role with SELECT on `api_keys` only, no RLS
 * applied. For v1 we do the lookup with `app.tenant_id` unset, which
 * means the policy returns zero rows -- so the auth path must EXPLICITLY
 * disable RLS for this query via a SECURITY DEFINER function. See
 * scripts/migrate.ts and ADR-012 for the production wiring.
 *
 * For v1 we expose the bare query and document that real production
 * deploys add a SECURITY DEFINER `lookup_api_key(prefix text)` function
 * owned by a role that DOES bypass RLS, granted EXECUTE to scc_app.
 */
export async function rawQuery<T = unknown>(
    raw: ReturnType<typeof sql>,
): Promise<T[]> {
    const db = getDb();
    const result = await db.execute(raw);
    return result as unknown as T[];
}
