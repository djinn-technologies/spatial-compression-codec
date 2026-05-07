// src/index.ts
//
// SCC Studio control-plane API server bootstrap.

import sensible from '@fastify/sensible';
import swagger from '@fastify/swagger';
import swaggerUi from '@fastify/swagger-ui';
import websocket from '@fastify/websocket';
import Fastify, { type FastifyInstance } from 'fastify';
import {
    serializerCompiler,
    validatorCompiler,
    type ZodTypeProvider,
} from 'fastify-type-provider-zod';

import { AgentClient } from './agent-controller.js';
import { loadConfig, shouldEnableSwaggerUi } from './config.js';
import { closeDb, initDb } from './db/client.js';
import { registerAuthHook } from './middleware/auth.js';
import { registerErrorHandler } from './middleware/error.js';
import { registerRateLimit } from './middleware/rate-limit.js';
import { registerTenantHook } from './middleware/tenant.js';
import { createRootLogger } from './observability/logger.js';
import { initTracer, shutdownTracer } from './observability/tracer.js';
import profileRoutes from './routes/profiles.js';
import recordingRoutes from './routes/recordings.js';
import sensorRoutes from './routes/sensors.js';
import sessionRoutes from './routes/sessions.js';

declare module 'fastify' {
    interface FastifyInstance {
        agent: AgentClient;
    }
}

export async function buildApp(): Promise<FastifyInstance> {
    const cfg = loadConfig();
    const logger = createRootLogger(cfg);

    await initTracer(cfg);
    initDb(cfg.databaseUrl, cfg.databaseMaxPoolSize);

    const agent = new AgentClient(cfg.sccAgentSocket, logger);
    await agent.load();

    const app = Fastify({
        logger,
        disableRequestLogging: false,
        trustProxy: true,
        bodyLimit: 1024 * 1024,            // 1 MiB body cap
        connectionTimeout: 30_000,
    }).withTypeProvider<ZodTypeProvider>();

    app.setValidatorCompiler(validatorCompiler);
    app.setSerializerCompiler(serializerCompiler);
    app.decorate('agent', agent);

    // Order matters: error handler last, but registered first so the
    // built-in default doesn't get latched in.
    registerErrorHandler(app);
    await app.register(sensible);

    // OpenAPI docs.
    await app.register(swagger, {
        openapi: {
            info: {
                title: 'SCC Studio API',
                version: '1.0.0',
                description: 'Control-plane API for the SCC Studio.',
            },
            servers: [{ url: '/' }],
            components: {
                securitySchemes: {
                    apiKey: { type: 'http', scheme: 'bearer', description: 'Bearer scc_<key>' },
                    bearerAuth: { type: 'http', scheme: 'bearer', bearerFormat: 'JWT' },
                },
            },
            security: [{ apiKey: [] }, { bearerAuth: [] }],
        },
    });
    if (shouldEnableSwaggerUi(cfg)) {
        await app.register(swaggerUi, { routePrefix: '/api/docs' });
    }

    // Auth + tenant + rate-limit pipeline.
    await registerAuthHook(app, cfg);
    registerTenantHook(app);
    await registerRateLimit(app, cfg);

    // WebSocket upgrade support (registered before the WS route).
    await app.register(websocket, {
        options: {
            maxPayload: 1 * 1024 * 1024,
            // Per-socket high-water mark; the WS adapter respects this.
            perMessageDeflate: false,
        },
    });

    // Health probe.
    app.get('/health', { logLevel: 'silent' }, async () => ({ ok: true }));

    // Routes.
    await app.register(sensorRoutes);
    await app.register(profileRoutes);
    await app.register(sessionRoutes);
    await app.register(recordingRoutes);

    app.addHook('onClose', async () => {
        await agent.close();
        await closeDb();
        await shutdownTracer();
    });

    return app;
}

async function main(): Promise<void> {
    const cfg = loadConfig();
    const app = await buildApp();
    try {
        await app.listen({ host: cfg.host, port: cfg.port });
    } catch (err) {
        app.log.error({ err }, 'listen failed');
        process.exit(1);
    }
}

const isEntry = (() => {
    try {
        return import.meta.url === `file://${process.argv[1]}`;
    } catch {
        return false;
    }
})();

if (isEntry) {
    void main();
}
