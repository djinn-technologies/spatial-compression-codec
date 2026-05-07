// src/middleware/rate-limit.ts
//
// `@fastify/rate-limit` keyed on the authenticated principal id, NOT
// on the IP. This means a single API key cannot exceed its budget by
// distributing calls across IPs (think: API consumer running behind a
// fleet of egress NAT'd workers).  [Ultrathink #5]
//
// Falls back to IP for unauthenticated paths (which the auth hook
// would already have rejected with 401, so this is defence-in-depth).

import rateLimit from '@fastify/rate-limit';
import type { FastifyInstance } from 'fastify';

import type { Config } from '../config.js';

export async function registerRateLimit(
    app: FastifyInstance,
    cfg: Config,
): Promise<void> {
    await app.register(rateLimit, {
        max: cfg.rateLimitMax,
        timeWindow: cfg.rateLimitWindow,
        keyGenerator: (req) => req.principal?.id ?? req.ip,
        // Allow health + docs to bypass the limiter.
        skipOnError: false,
        allowList: (req) => {
            const path = req.routerPath ?? req.url;
            return path === '/health' || path.startsWith('/api/docs');
        },
        addHeadersOnExceeding: {
            'x-ratelimit-limit': true,
            'x-ratelimit-remaining': true,
            'x-ratelimit-reset': true,
        },
    });
}
