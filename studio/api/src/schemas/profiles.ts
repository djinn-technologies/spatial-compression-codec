// src/schemas/profiles.ts

import { z } from 'zod';

import {
    BitDepthSchema,
    FrameDimsSchema,
    ProfileNameSchema,
    UuidSchema,
} from './common.js';

export const CreateProfileBody = z
    .object({
        name: z.string().min(1).max(120),
        profile: ProfileNameSchema,
        bitDepth: BitDepthSchema,
        metadata: z.record(z.unknown()).default({}),
    })
    .merge(FrameDimsSchema);

export const PatchProfileBody = z
    .object({
        name: z.string().min(1).max(120),
        profile: ProfileNameSchema,
        bitDepth: BitDepthSchema,
        width: z.number().int().positive().max(65535),
        height: z.number().int().positive().max(65535),
        metadata: z.record(z.unknown()),
    })
    .partial();

export const ProfileResource = z.object({
    id: UuidSchema,
    tenantId: UuidSchema,
    name: z.string(),
    profile: ProfileNameSchema,
    bitDepth: z.number().int(),
    width: z.number().int(),
    height: z.number().int(),
    metadata: z.record(z.unknown()),
    createdAt: z.string().datetime(),
    updatedAt: z.string().datetime(),
});

export const ProfileParams = z.object({
    id: UuidSchema,
});
