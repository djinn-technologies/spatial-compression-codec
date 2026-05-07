// src/middleware/auth.ts
//
// Two auth tracks:
//
//   1. API key  -- Header `Authorization: Bearer scc_<base64url>`.
//                  32 random bytes URL-safe encoded; we look up by
//                  prefix (first 8 chars) and constant-time compare
//                  the SHA-256 hash.
//
//   2. JWT      -- Header `Authorization: Bearer <jwt>` (RS256).
//                  Validates iss/aud/exp; tenantId comes from a
//                  required custom claim.
//
// Both produce a unified `request.principal = {tenantId, kind, id}`.
// Routes downstream consume only `request.principal.tenantId` and the
// kind for audit metadata.

import { createHash, timingSafeEqual } from 'node:crypto';

import { eq } from 'drizzle-orm';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import jwt, { type VerifyOptions } from 'jsonwebtoken';

import type { Config } from '../config.js';
import { getDb } from '../db/client.js';
import { apiKeys } from '../db/schema.js';

export type Principal =
    | { kind: 'apiKey'; tenantId: string; id: string }
    | { kind: 'user';   tenantId: string; id: string };

declare module 'fastify' {
    interface FastifyRequest {
        principal?: Principal;
    }
}

export async function registerAuthHook(
    app: FastifyInstance,
    cfg: Config,
): Promise<void> {
    app.addHook('onRequest', async (req, reply) => {
        // Skip auth for health + docs.
        if (skipAuth(req.routerPath ?? req.url)) return;

        const header = req.headers.authorization;
        if (!header || !header.startsWith('Bearer ')) {
            return reply.code(401).send({
                error: 'unauthorized',
                message: 'missing Authorization: Bearer ... header',
            });
        }
        const token = header.slice('Bearer '.length).trim();

        if (token.startsWith('scc_')) {
            const principal = await verifyApiKey(token);
            if (!principal) {
                return reply.code(401).send({ error: 'unauthorized', message: 'bad api key' });
            }
            req.principal = principal;
            return;
        }

        const principal = await verifyJwt(token, cfg);
        if (!principal) {
            return reply.code(401).send({ error: 'unauthorized', message: 'bad jwt' });
        }
        req.principal = principal;
    });
}

function skipAuth(path: string): boolean {
    if (path === '/health' || path === '/api/openapi.json') return true;
    if (path.startsWith('/api/docs')) return true;
    return false;
}

// ---------------------------------------------------------------------------
// API key verification.
// ---------------------------------------------------------------------------

async function verifyApiKey(token: string): Promise<Principal | null> {
    if (token.length < 12) return null;
    const prefix = token.slice(0, 12); // "scc_" + 8 chars
    const hash = createHash('sha256').update(token).digest('base64url');

    const db = getDb();
    // The lookup must run with the privileged role / SECURITY DEFINER
    // function in production. For v1 the migration documents this; for
    // now this query runs with RLS off via a pre-arranged superuser
    // connection during boot (not implemented here -- documented in
    // ADR-012 §"Auth lookup path").
    const [row] = await db
        .select()
        .from(apiKeys)
        .where(eq(apiKeys.prefix, prefix))
        .limit(1);
    if (!row || row.revoked) return null;

    const stored = Buffer.from(row.hash);
    const candidate = Buffer.from(hash);
    if (stored.length !== candidate.length) return null;
    if (!timingSafeEqual(stored, candidate)) return null;

    return { kind: 'apiKey', tenantId: row.tenantId, id: row.id };
}

// ---------------------------------------------------------------------------
// JWT verification.
// ---------------------------------------------------------------------------

async function verifyJwt(token: string, cfg: Config): Promise<Principal | null> {
    const opts: VerifyOptions = {
        algorithms: ['RS256'],
        issuer: cfg.jwtIssuer,
        audience: cfg.jwtAudience,
    };
    return await new Promise<Principal | null>((resolve) => {
        jwt.verify(token, cfg.jwtPublicKey, opts, (err, decoded) => {
            if (err || !decoded || typeof decoded === 'string') {
                resolve(null);
                return;
            }
            const claims = decoded as { sub?: string; tenant_id?: string };
            if (!claims.sub || !claims.tenant_id) {
                resolve(null);
                return;
            }
            resolve({
                kind: 'user',
                tenantId: claims.tenant_id,
                id: claims.sub,
            });
        });
    });
}

