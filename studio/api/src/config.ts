// src/config.ts
//
// 12-factor configuration. Every knob is read from process.env once at
// startup and validated via Zod. Throws synchronously on missing /
// invalid values so the container fails fast (matches Kubernetes
// CrashLoopBackoff semantics).

import { z } from 'zod';

const ConfigSchema = z.object({
    host: z.string().default('0.0.0.0'),
    port: z.coerce.number().int().positive().default(8080),
    nodeEnv: z.enum(['development', 'test', 'production']).default('production'),
    logLevel: z.enum(['fatal', 'error', 'warn', 'info', 'debug', 'trace'])
        .default('info'),

    databaseUrl: z.string().min(1),
    databaseMaxPoolSize: z.coerce.number().int().positive().default(20),

    jwtPublicKey: z.string().min(1),
    jwtIssuer: z.string().min(1),
    jwtAudience: z.string().min(1),

    sccAgentSocket: z.string().min(1),

    rateLimitMax: z.coerce.number().int().positive().default(60),
    rateLimitWindow: z.string().default('1 minute'),

    otelExporterOtlpEndpoint: z.string().optional(),
    otelServiceName: z.string().default('scc-studio-api'),

    swaggerUiEnabled: z.enum(['auto', 'true', 'false']).default('auto'),
});

export type Config = z.infer<typeof ConfigSchema>;

export function loadConfig(): Config {
    const raw = {
        host: process.env.HOST,
        port: process.env.PORT,
        nodeEnv: process.env.NODE_ENV,
        logLevel: process.env.LOG_LEVEL,

        databaseUrl: process.env.DATABASE_URL,
        databaseMaxPoolSize: process.env.DATABASE_MAX_POOL_SIZE,

        jwtPublicKey: process.env.JWT_PUBLIC_KEY,
        jwtIssuer: process.env.JWT_ISSUER,
        jwtAudience: process.env.JWT_AUDIENCE,

        sccAgentSocket: process.env.SCC_AGENT_SOCKET,

        rateLimitMax: process.env.RATE_LIMIT_MAX,
        rateLimitWindow: process.env.RATE_LIMIT_WINDOW,

        otelExporterOtlpEndpoint: process.env.OTEL_EXPORTER_OTLP_ENDPOINT,
        otelServiceName: process.env.OTEL_SERVICE_NAME,

        swaggerUiEnabled: process.env.SWAGGER_UI_ENABLED,
    };
    return ConfigSchema.parse(raw);
}

export function shouldEnableSwaggerUi(cfg: Config): boolean {
    if (cfg.swaggerUiEnabled === 'true') return true;
    if (cfg.swaggerUiEnabled === 'false') return false;
    return cfg.nodeEnv !== 'production';
}
