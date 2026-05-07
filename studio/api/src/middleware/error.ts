// src/middleware/error.ts
//
// Centralised error formatter. Zod validation errors get 400 with
// per-field details; everything else gets a redacted 500 with a
// trace_id the client can quote in support requests.

import type { FastifyInstance } from 'fastify';
import { ZodError } from 'zod';

export function registerErrorHandler(app: FastifyInstance): void {
    app.setErrorHandler((err, req, reply) => {
        if (err instanceof ZodError) {
            req.log.warn({ issues: err.issues }, 'request validation failed');
            return reply.code(400).send({
                error: 'validation_error',
                issues: err.issues.map((i) => ({
                    path: i.path.join('.'),
                    code: i.code,
                    message: i.message,
                })),
            });
        }

        // Fastify's @fastify/sensible attaches statusCode to its HTTP
        // errors (notFound, unauthorized, ...). Honor them.
        const status = (err as { statusCode?: number }).statusCode ?? 500;
        if (status < 500) {
            req.log.warn({ err }, 'client error');
            return reply.code(status).send({ error: err.name, message: err.message });
        }

        req.log.error({ err }, 'unhandled error');
        return reply.code(500).send({
            error: 'internal_error',
            message: 'unexpected server error -- include the trace_id when reporting',
            request_id: req.id,
        });
    });
}
