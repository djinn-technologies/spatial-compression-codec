// tests/routes.test.ts
//
// Vitest unit tests against an in-process Fastify server using
// `app.inject()`. A fake AgentClient stands in for the real IPC
// connection so these tests run without a live agent.

import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';

// We mock loadConfig so the tests never reach for a real Postgres.
vi.mock('../src/config.js', async (importOriginal) => {
    const actual = await importOriginal<typeof import('../src/config.js')>();
    return {
        ...actual,
        loadConfig: () => ({
            host: '127.0.0.1',
            port: 0,
            nodeEnv: 'test' as const,
            logLevel: 'silent' as const,
            databaseUrl: 'postgres://test/test',
            databaseMaxPoolSize: 1,
            jwtPublicKey: 'unused',
            jwtIssuer: 'iss',
            jwtAudience: 'aud',
            sccAgentSocket: '/tmp/agent.sock',
            rateLimitMax: 10_000,
            rateLimitWindow: '1 minute',
            otelExporterOtlpEndpoint: undefined,
            otelServiceName: 'scc-studio-api-test',
            swaggerUiEnabled: 'false' as const,
        }),
    };
});

// Mock the DB so no real Postgres is needed for non-RLS tests.
vi.mock('../src/db/client.js', () => ({
    initDb: vi.fn(),
    closeDb: vi.fn(),
    getDb: vi.fn(),
    withTenant: async <T>(_id: string, fn: (tx: unknown) => Promise<T>) =>
        fn({ select: () => ({ from: () => ({ where: () => ({ limit: async () => [] }) }) }) }),
}));

// Mock the agent client so .call() returns canned responses.
vi.mock('../src/agent-controller.js', () => {
    return {
        AgentClient: class FakeAgent {
            constructor(_p: string, _l: unknown) {}
            async load(): Promise<void> {}
            async call(req: Record<string, unknown>): Promise<Record<string, unknown>> {
                if (req.list) {
                    return {
                        sensors: { sensors: [{ id: 'mock-0', vendor: 'Djinn', model: 'm', backend: 'mock', modes: [] }] },
                    };
                }
                return { err: { code: 99, message: 'unmocked' } };
            }
            async close(): Promise<void> {}
        },
    };
});

import { buildApp } from '../src/index.js';
import type { FastifyInstance } from 'fastify';

let app: FastifyInstance;

beforeAll(async () => {
    app = await buildApp();
    await app.ready();
});

afterAll(async () => {
    await app.close();
});

describe('GET /health', () => {
    it('returns ok without auth', async () => {
        const r = await app.inject({ method: 'GET', url: '/health' });
        expect(r.statusCode).toBe(200);
        expect(r.json()).toEqual({ ok: true });
    });
});

describe('Auth', () => {
    it('rejects requests without Authorization header', async () => {
        const r = await app.inject({ method: 'GET', url: '/api/sensors' });
        expect(r.statusCode).toBe(401);
    });

    it('rejects malformed Authorization header', async () => {
        const r = await app.inject({
            method: 'GET',
            url: '/api/sensors',
            headers: { authorization: 'Token foo' },
        });
        expect(r.statusCode).toBe(401);
    });
});

describe('OpenAPI', () => {
    it('exposes /api/openapi.json with all routes', async () => {
        const r = await app.inject({ method: 'GET', url: '/api/openapi.json' });
        expect(r.statusCode).toBe(200);
        const doc = r.json() as { paths: Record<string, unknown> };
        expect(doc.paths['/api/sensors']).toBeDefined();
        expect(doc.paths['/api/sessions']).toBeDefined();
        expect(doc.paths['/api/profiles']).toBeDefined();
        expect(doc.paths['/api/recordings/{id}/finalize']).toBeDefined();
    });
});
