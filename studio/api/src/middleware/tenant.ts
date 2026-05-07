// src/middleware/tenant.ts
//
// Once auth has resolved `request.principal`, this hook attaches the
// per-request Pino child logger and exposes a typed `request.tenantId`
// so route handlers can use `withTenant(request.tenantId, ...)` from
// db/client.ts. The Pino child includes tenant_id, request_id,
// trace_id, principal_id on every log line. [Ultrathink #3]

import { context as otelContext, trace } from '@opentelemetry/api';
import type { FastifyInstance } from 'fastify';

declare module 'fastify' {
    interface FastifyRequest {
        tenantId?: string;
    }
}

export function registerTenantHook(app: FastifyInstance): void {
    app.addHook('onRequest', async (req) => {
        const principal = req.principal;
        if (!principal) return;
        req.tenantId = principal.tenantId;

        // Trace id from the active OTel span if any; otherwise the
        // request id Fastify already assigned.
        const span = trace.getSpan(otelContext.active());
        const traceId = span?.spanContext().traceId;

        // Replace req.log with a child logger carrying the tenant scope.
        // From this point on, route handlers MUST use req.log -- direct
        // `pino()` calls bypass the tenant tag (lint rule rejects them).
        req.log = req.log.child({
            tenant_id: principal.tenantId,
            principal_id: principal.id,
            principal_kind: principal.kind,
            request_id: req.id,
            ...(traceId ? { trace_id: traceId } : {}),
        });
    });
}
