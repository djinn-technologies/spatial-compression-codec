// src/schemas/common.ts
//
// Reusable Zod fragments. Every body / response schema in routes/*.ts
// composes these so OpenAPI emits consistent type names.

import { z } from 'zod';

export const UuidSchema = z.string().uuid();

export const ProfileNameSchema = z.enum([
    'lossless',
    'lossy:high',
    'lossy:streaming',
]);

export const BitDepthSchema = z.union([
    z.literal(8),
    z.literal(12),
    z.literal(16),
]);

export const FrameDimsSchema = z.object({
    width: z.number().int().positive().max(65535),
    height: z.number().int().positive().max(65535),
});

export const ErrorSchema = z.object({
    error: z.string(),
    message: z.string().optional(),
});

export const TimestampSchema = z.string().datetime();
