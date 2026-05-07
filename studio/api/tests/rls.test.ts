// tests/rls.test.ts
//
// Postgres RLS regression test (Ultrathink #1).
//
// Spins up a Postgres testcontainer, runs the 0001_init.sql migration,
// then connects as the (non-bypass) `scc_app` role and proves that:
//
//   1. With NO `app.tenant_id` set: zero rows visible.
//   2. With `app.tenant_id = A`: only A's rows visible.
//   3. Switching to `app.tenant_id = B` mid-test: only B's rows visible.
//
// This is the load-bearing test for the multi-tenant story.

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import postgres from 'postgres';
import { PostgreSqlContainer, type StartedPostgreSqlContainer } from 'testcontainers';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const MIGRATION_SQL = readFileSync(
    resolve(__dirname, '..', 'src', 'db', 'migrations', '0001_init.sql'),
    'utf8',
);

describe('Postgres RLS', () => {
    let container: StartedPostgreSqlContainer;
    let admin: ReturnType<typeof postgres>;
    let app: ReturnType<typeof postgres>;
    let tenantA: string;
    let tenantB: string;

    beforeAll(async () => {
        container = await new PostgreSqlContainer('postgres:16-alpine')
            .withDatabase('scc_studio')
            .withUsername('postgres')
            .withPassword('postgres')
            .start();

        admin = postgres(container.getConnectionUri());
        await admin.unsafe(MIGRATION_SQL);
        // Set a password for scc_app so the per-test connection can log in.
        await admin.unsafe(`ALTER ROLE scc_app WITH PASSWORD 'apppass'`);

        const appUri = new URL(container.getConnectionUri());
        appUri.username = 'scc_app';
        appUri.password = 'apppass';
        app = postgres(appUri.toString(), { prepare: false });

        // Seed two tenants (admin connection bypasses RLS).
        const [a] = await admin<{ id: string }[]>`
            INSERT INTO tenants (name) VALUES ('A') RETURNING id`;
        const [b] = await admin<{ id: string }[]>`
            INSERT INTO tenants (name) VALUES ('B') RETURNING id`;
        tenantA = a!.id;
        tenantB = b!.id;
        await admin`INSERT INTO profiles (tenant_id, name, profile, bit_depth, width, height)
                    VALUES (${tenantA}, 'A1', 'lossless', 12, 16, 16)`;
        await admin`INSERT INTO profiles (tenant_id, name, profile, bit_depth, width, height)
                    VALUES (${tenantB}, 'B1', 'lossless', 12, 16, 16)`;
    }, 120_000);

    afterAll(async () => {
        await app?.end({ timeout: 5 });
        await admin?.end({ timeout: 5 });
        await container?.stop();
    }, 60_000);

    it('forbids reading rows when app.tenant_id is unset (fail closed)', async () => {
        await app.begin(async (sql) => {
            const rows = await sql`SELECT id FROM profiles`;
            expect(rows).toHaveLength(0);
        });
    });

    it('shows only tenant A rows when app.tenant_id = A', async () => {
        await app.begin(async (sql) => {
            await sql`SELECT set_config('app.tenant_id', ${tenantA}, true)`;
            const rows = await sql<{ name: string }[]>`SELECT name FROM profiles`;
            expect(rows.map((r) => r.name)).toEqual(['A1']);
        });
    });

    it('switching to tenant B mid-session changes visibility', async () => {
        await app.begin(async (sql) => {
            await sql`SELECT set_config('app.tenant_id', ${tenantA}, true)`;
            const a = await sql<{ name: string }[]>`SELECT name FROM profiles`;
            expect(a.map((r) => r.name)).toEqual(['A1']);
        });
        await app.begin(async (sql) => {
            await sql`SELECT set_config('app.tenant_id', ${tenantB}, true)`;
            const b = await sql<{ name: string }[]>`SELECT name FROM profiles`;
            expect(b.map((r) => r.name)).toEqual(['B1']);
        });
    });

    it('refuses to insert into a tenant that is not the active one', async () => {
        // Insert with tenant_id = A while the GUC says B -> WITH CHECK fails.
        await expect(
            app.begin(async (sql) => {
                await sql`SELECT set_config('app.tenant_id', ${tenantB}, true)`;
                await sql`INSERT INTO profiles (tenant_id, name, profile, bit_depth, width, height)
                          VALUES (${tenantA}, 'cross', 'lossless', 12, 16, 16)`;
            }),
        ).rejects.toThrow(/row-level security|policy/i);
    });
});
