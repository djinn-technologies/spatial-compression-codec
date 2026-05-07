// src/routes/sessions.ts

import { eq } from 'drizzle-orm';
import type { FastifyInstance } from 'fastify';
import type { ZodTypeProvider } from 'fastify-type-provider-zod';
import type { WebSocket } from 'ws';

import { withTenant } from '../db/client.js';
import { sessions, profiles } from '../db/schema.js';
import {
    CreateSessionBody,
    SessionParams,
    SessionResource,
} from '../schemas/sessions.js';
import { ErrorSchema } from '../schemas/common.js';
import {
    wsActiveConnections,
    wsBackpressureDrops,
} from '../observability/metrics.js';

export default async function sessionRoutes(app: FastifyInstance): Promise<void> {
    const f = app.withTypeProvider<ZodTypeProvider>();

    // -------------------------------------------------------------- POST
    f.post('/api/sessions', {
        schema: {
            tags: ['sessions'],
            body: CreateSessionBody,
            response: { 201: SessionResource, 400: ErrorSchema },
        },
        handler: async (req, reply) => {
            const body = req.body;
            const tenantId = req.tenantId!;

            const profileName = body.profile ?? (await resolveProfileName(tenantId, body.profileId!));

            const [row] = await withTenant(tenantId, async (tx) => {
                return await tx
                    .insert(sessions)
                    .values({
                        tenantId,
                        profileId: body.profileId,
                        sensorId: body.sensorId,
                        state: 'pending',
                        width: body.width,
                        height: body.height,
                        bitDepth: body.bitDepth,
                    })
                    .returning();
            });

            // Tell the agent to start. On failure we mark the session
            // failed but still return 201 with state=failed -- the
            // client can poll / DELETE.
            try {
                const resp = await app.agent.call({
                    start: {
                        sensorId: body.sensorId,
                        width: body.width,
                        height: body.height,
                        bitDepth: body.bitDepth,
                        profile: profileName,
                        fps: body.fps,
                    },
                });
                if (resp.session) {
                    const sess = resp.session as { sessionId: string };
                    await withTenant(tenantId, async (tx) => {
                        await tx
                            .update(sessions)
                            .set({
                                agentSessionId: sess.sessionId,
                                state: 'active',
                                startedAt: new Date(),
                            })
                            .where(eq(sessions.id, row!.id));
                    });
                    row!.state = 'active';
                    row!.agentSessionId = sess.sessionId;
                    row!.startedAt = new Date();
                } else if (resp.err) {
                    req.log.warn({ err: resp.err }, 'agent StartSession error');
                    await withTenant(tenantId, async (tx) => {
                        await tx
                            .update(sessions)
                            .set({ state: 'failed' })
                            .where(eq(sessions.id, row!.id));
                    });
                    row!.state = 'failed';
                }
            } catch (err) {
                req.log.error({ err }, 'agent StartSession call failed');
                await withTenant(tenantId, async (tx) => {
                    await tx
                        .update(sessions)
                        .set({ state: 'failed' })
                        .where(eq(sessions.id, row!.id));
                });
                row!.state = 'failed';
            }

            reply.code(201);
            return row;
        },
    });

    // -------------------------------------------------------------- GET
    f.get('/api/sessions/:id', {
        schema: {
            tags: ['sessions'],
            params: SessionParams,
            response: { 200: SessionResource, 404: ErrorSchema },
        },
        handler: async (req, reply) => {
            const { id } = req.params;
            const [row] = await withTenant(req.tenantId!, async (tx) => {
                return await tx.select().from(sessions).where(eq(sessions.id, id)).limit(1);
            });
            if (!row) {
                reply.code(404);
                return { error: 'not_found' };
            }
            return row;
        },
    });

    // -------------------------------------------------------------- DELETE
    f.delete('/api/sessions/:id', {
        schema: {
            tags: ['sessions'],
            params: SessionParams,
            response: { 200: SessionResource, 404: ErrorSchema },
        },
        handler: async (req, reply) => {
            const { id } = req.params;
            const tenantId = req.tenantId!;
            const [row] = await withTenant(tenantId, async (tx) => {
                return await tx.select().from(sessions).where(eq(sessions.id, id)).limit(1);
            });
            if (!row) {
                reply.code(404);
                return { error: 'not_found' };
            }
            if (row.agentSessionId) {
                try {
                    await app.agent.call({ stop: { sessionId: row.agentSessionId } });
                } catch (err) {
                    req.log.warn({ err }, 'agent StopSession failed; finalising regardless');
                }
            }
            const [updated] = await withTenant(tenantId, async (tx) => {
                return await tx
                    .update(sessions)
                    .set({ state: 'completed', endedAt: new Date() })
                    .where(eq(sessions.id, id))
                    .returning();
            });
            return updated!;
        },
    });

    // -------------------------------------------------------------- WS
    //
    // /api/sessions/:id/metrics -- pushes Metrics frames at 1 Hz.
    // Back-pressure: only push the next frame after the previous send's
    // callback resolves; if the client is slow we drop frames rather
    // than buffer indefinitely. [Ultrathink #4]
    f.get<{ Params: { id: string } }>(
        '/api/sessions/:id/metrics',
        { websocket: true },
        async (connection, request) => {
            const sessionId = request.params.id;
            const tenantId = request.tenantId!;
            const sock = connection.socket;

            // Verify the session belongs to the caller's tenant before
            // streaming anything.
            const [row] = await withTenant(tenantId, async (tx) => {
                return await tx
                    .select()
                    .from(sessions)
                    .where(eq(sessions.id, sessionId))
                    .limit(1);
            });
            if (!row || !row.agentSessionId) {
                sock.send(JSON.stringify({ error: 'no_active_session' }));
                sock.close();
                return;
            }

            wsActiveConnections.add(1);
            request.log.info({ sessionId }, 'metrics ws opened');

            let closed = false;
            let inflight = false;
            const interval = setInterval(async () => {
                if (closed) return;
                if (inflight) {
                    // Slow client. Drop this tick.
                    wsBackpressureDrops.add(1);
                    return;
                }
                inflight = true;
                try {
                    const resp = await app.agent.call({
                        metrics: { sessionId: row.agentSessionId! },
                    });
                    const m = resp.metrics ?? {};
                    sock.send(JSON.stringify({ ts: Date.now(), metrics: m }), () => {
                        inflight = false;
                    });
                } catch (err) {
                    inflight = false;
                    request.log.warn({ err }, 'metrics ws agent call failed');
                }
            }, 1_000);

            const cleanup = (): void => {
                if (closed) return;
                closed = true;
                clearInterval(interval);
                wsActiveConnections.add(-1);
                request.log.info({ sessionId }, 'metrics ws closed');
            };
            (sock as WebSocket).on('close', cleanup);
            (sock as WebSocket).on('error', cleanup);
        },
    );
}

async function resolveProfileName(
    tenantId: string,
    profileId: string,
): Promise<string> {
    const [row] = await withTenant(tenantId, async (tx) => {
        return await tx
            .select({ profile: profiles.profile })
            .from(profiles)
            .where(eq(profiles.id, profileId))
            .limit(1);
    });
    if (!row) {
        throw new Error(`profile ${profileId} not found`);
    }
    return row.profile;
}
