// src/routes/profiles.ts

import { and, eq } from 'drizzle-orm';
import type { FastifyInstance } from 'fastify';
import type { ZodTypeProvider } from 'fastify-type-provider-zod';

import { withTenant } from '../db/client.js';
import { profiles } from '../db/schema.js';
import {
    CreateProfileBody,
    PatchProfileBody,
    ProfileParams,
    ProfileResource,
} from '../schemas/profiles.js';
import { ErrorSchema } from '../schemas/common.js';

export default async function profileRoutes(app: FastifyInstance): Promise<void> {
    const f = app.withTypeProvider<ZodTypeProvider>();

    f.get('/api/profiles', {
        schema: {
            tags: ['profiles'],
            response: { 200: ProfileResource.array() },
        },
        handler: async (req) => {
            return await withTenant(req.tenantId!, async (tx) => {
                return await tx.select().from(profiles);
            });
        },
    });

    f.post('/api/profiles', {
        schema: {
            tags: ['profiles'],
            body: CreateProfileBody,
            response: { 201: ProfileResource, 400: ErrorSchema },
        },
        handler: async (req, reply) => {
            const body = req.body;
            const [row] = await withTenant(req.tenantId!, async (tx) => {
                return await tx
                    .insert(profiles)
                    .values({
                        tenantId: req.tenantId!,
                        name: body.name,
                        profile: body.profile,
                        bitDepth: body.bitDepth,
                        width: body.width,
                        height: body.height,
                        metadata: body.metadata,
                    })
                    .returning();
            });
            reply.code(201);
            return row;
        },
    });

    f.get('/api/profiles/:id', {
        schema: {
            tags: ['profiles'],
            params: ProfileParams,
            response: { 200: ProfileResource, 404: ErrorSchema },
        },
        handler: async (req, reply) => {
            const { id } = req.params;
            const [row] = await withTenant(req.tenantId!, async (tx) => {
                return await tx
                    .select()
                    .from(profiles)
                    .where(eq(profiles.id, id))
                    .limit(1);
            });
            if (!row) {
                reply.code(404);
                return { error: 'not_found', message: 'profile' };
            }
            return row;
        },
    });

    f.patch('/api/profiles/:id', {
        schema: {
            tags: ['profiles'],
            params: ProfileParams,
            body: PatchProfileBody,
            response: { 200: ProfileResource, 404: ErrorSchema },
        },
        handler: async (req, reply) => {
            const { id } = req.params;
            const body = req.body;
            const [row] = await withTenant(req.tenantId!, async (tx) => {
                return await tx
                    .update(profiles)
                    .set({ ...body, updatedAt: new Date() })
                    .where(and(eq(profiles.id, id)))
                    .returning();
            });
            if (!row) {
                reply.code(404);
                return { error: 'not_found', message: 'profile' };
            }
            return row;
        },
    });

    f.delete('/api/profiles/:id', {
        schema: {
            tags: ['profiles'],
            params: ProfileParams,
            response: { 204: ErrorSchema.partial(), 404: ErrorSchema },
        },
        handler: async (req, reply) => {
            const { id } = req.params;
            const deleted = await withTenant(req.tenantId!, async (tx) => {
                return await tx
                    .delete(profiles)
                    .where(eq(profiles.id, id))
                    .returning();
            });
            if (deleted.length === 0) {
                reply.code(404);
                return { error: 'not_found', message: 'profile' };
            }
            reply.code(204);
            return {};
        },
    });
}
