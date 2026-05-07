// src/routes/recordings.ts
//
// POST /api/recordings/:id/finalize  -- closes the recording, pushes
// the final size + storage URL into the row, marks finalized = true.
// The actual file move / S3 upload happens out of band; this route
// just records the result.

import { eq } from 'drizzle-orm';
import type { FastifyInstance } from 'fastify';
import type { ZodTypeProvider } from 'fastify-type-provider-zod';
import { z } from 'zod';

import { withTenant } from '../db/client.js';
import { recordings } from '../db/schema.js';
import { ErrorSchema, UuidSchema } from '../schemas/common.js';

const FinalizeBody = z.object({
    storageUrl: z.string().url(),
    sizeBytes: z.number().int().nonnegative(),
    framesTotal: z.number().int().nonnegative(),
});

const RecordingResource = z.object({
    id: UuidSchema,
    tenantId: UuidSchema,
    sessionId: UuidSchema,
    storageUrl: z.string().nullable(),
    sizeBytes: z.number().int().nullable(),
    framesTotal: z.number().int().nullable(),
    finalized: z.boolean(),
    createdAt: z.string().datetime(),
    finalizedAt: z.string().datetime().nullable(),
});

const Params = z.object({ id: UuidSchema });

export default async function recordingRoutes(app: FastifyInstance): Promise<void> {
    const f = app.withTypeProvider<ZodTypeProvider>();

    f.post('/api/recordings/:id/finalize', {
        schema: {
            tags: ['recordings'],
            params: Params,
            body: FinalizeBody,
            response: { 200: RecordingResource, 404: ErrorSchema, 409: ErrorSchema },
        },
        handler: async (req, reply) => {
            const { id } = req.params;
            const body = req.body;
            const tenantId = req.tenantId!;

            const [existing] = await withTenant(tenantId, async (tx) => {
                return await tx
                    .select()
                    .from(recordings)
                    .where(eq(recordings.id, id))
                    .limit(1);
            });
            if (!existing) {
                reply.code(404);
                return { error: 'not_found' };
            }
            if (existing.finalized) {
                reply.code(409);
                return { error: 'already_finalized' };
            }
            const [row] = await withTenant(tenantId, async (tx) => {
                return await tx
                    .update(recordings)
                    .set({
                        storageUrl: body.storageUrl,
                        sizeBytes: body.sizeBytes,
                        framesTotal: body.framesTotal,
                        finalized: true,
                        finalizedAt: new Date(),
                    })
                    .where(eq(recordings.id, id))
                    .returning();
            });
            return row!;
        },
    });
}
