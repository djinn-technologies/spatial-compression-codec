// src/observability/logger.ts
//
// Per-request Pino child logger. Every log line gets `tenant_id`,
// `request_id`, `trace_id`, and (when authenticated) `principal_id`
// stamped on it. Route handlers MUST log via `request.log` so the
// tenant scoping is automatic. Direct `pino()` calls in route code
// are flagged by ESLint. [Ultrathink #3]

import pino, { type Logger } from 'pino';

import type { Config } from '../config.js';

export function createRootLogger(cfg: Config): Logger {
    return pino({
        level: cfg.logLevel,
        base: { service: cfg.otelServiceName },
        timestamp: pino.stdTimeFunctions.isoTime,
        formatters: {
            level: (label) => ({ level: label }),
        },
        // Production: JSON. Dev: pretty.
        ...(cfg.nodeEnv === 'development'
            ? {
                  transport: {
                      target: 'pino-pretty',
                      options: { colorize: true },
                  },
              }
            : {}),
    });
}
