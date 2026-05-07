// src/schemas/sessions.ts

import { z } from 'zod';

import {
    BitDepthSchema,
    FrameDimsSchema,
    ProfileNameSchema,
    UuidSchema,
} from './common.js';

export const CreateSessionBody = z
    .object({
        sensorId: z.string().min(1),
        profileId: UuidSchema.optional(),
        profile: ProfileNameSchema.optional(),
        bitDepth: BitDepthSchema.default(12),
        fps: z.number().int().min(1).max(120).default(30),
    })
    .merge(FrameDimsSchema)
    .refine(
        (v) => Boolean(v.profile ?? v.profileId),
        { message: 'either profile or profileId is required' },
    );

export const SessionResource = z.object({
    id: UuidSchema,
    tenantId: UuidSchema,
    sensorId: z.string(),
    state: z.enum(['pending', 'active', 'completed', 'failed']),
    width: z.number().int(),
    height: z.number().int(),
    bitDepth: z.number().int(),
    agentSessionId: z.string().nullable(),
    startedAt: z.string().datetime().nullable(),
    endedAt: z.string().datetime().nullable(),
});

export const SessionParams = z.object({
    id: UuidSchema,
});
