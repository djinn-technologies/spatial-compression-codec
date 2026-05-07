// src/routes/sensors.ts
//
// GET /api/sensors -- delegates to the agent's ListSensors RPC.

import type { FastifyInstance } from 'fastify';
import type { ZodTypeProvider } from 'fastify-type-provider-zod';
import { z } from 'zod';

import { ErrorSchema } from '../schemas/common.js';

const SensorMode = z.object({
    width: z.number().int(),
    height: z.number().int(),
    fps: z.number().int(),
    bitDepth: z.number().int(),
});

const SensorDescriptor = z.object({
    id: z.string(),
    vendor: z.string(),
    model: z.string(),
    backend: z.string(),
    modes: SensorMode.array(),
});

const SensorListResponse = z.object({
    sensors: SensorDescriptor.array(),
});

interface AgentDescriptor {
    id: string;
    vendor: string;
    model: string;
    backend: string;
    modes: { width: number; height: number; fps: number; bitDepth: number }[];
}

export default async function sensorRoutes(app: FastifyInstance): Promise<void> {
    const f = app.withTypeProvider<ZodTypeProvider>();

    f.get('/api/sensors', {
        schema: {
            tags: ['sensors'],
            response: { 200: SensorListResponse, 502: ErrorSchema },
        },
        handler: async (req, reply) => {
            try {
                const resp = await app.agent.call({ list: {} });
                if (resp.err) {
                    req.log.warn({ err: resp.err }, 'agent ListSensors error');
                    reply.code(502);
                    return { error: 'agent_error' };
                }
                const list = (resp.sensors as { sensors?: AgentDescriptor[] }) ?? {};
                return { sensors: list.sensors ?? [] };
            } catch (err) {
                req.log.error({ err }, 'agent ListSensors call failed');
                reply.code(502);
                return { error: 'agent_unreachable' };
            }
        },
    });
}
