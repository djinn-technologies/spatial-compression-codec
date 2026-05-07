// src/db/schema.ts
//
// Drizzle ORM schema. The Postgres tables, indexes, and FK constraints
// live here; the Row-Level Security policies + the dedicated app role
// live in the migration SQL (drizzle-orm has no RLS DSL today). See
// migrations/0001_init.sql.

import { sql } from 'drizzle-orm';
import {
    boolean,
    integer,
    jsonb,
    pgTable,
    text,
    timestamp,
    uuid,
} from 'drizzle-orm/pg-core';

// ---------------------------------------------------------------------------
// Tenancy
// ---------------------------------------------------------------------------

export const tenants = pgTable('tenants', {
    id:        uuid('id').primaryKey().defaultRandom(),
    name:      text('name').notNull(),
    createdAt: timestamp('created_at', { withTimezone: true })
                  .default(sql`now()`)
                  .notNull(),
});

// API keys are hashed at rest; the prefix is the first 8 characters of
// the URL-safe random string for fast lookup.
export const apiKeys = pgTable('api_keys', {
    id:        uuid('id').primaryKey().defaultRandom(),
    tenantId:  uuid('tenant_id')
                  .notNull()
                  .references(() => tenants.id, { onDelete: 'cascade' }),
    prefix:    text('prefix').notNull().unique(),
    hash:      text('hash').notNull(),       // base64(sha256(secret))
    label:     text('label').notNull(),
    revoked:   boolean('revoked').notNull().default(false),
    createdAt: timestamp('created_at', { withTimezone: true })
                  .default(sql`now()`)
                  .notNull(),
    lastUsed:  timestamp('last_used', { withTimezone: true }),
});

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

export const profiles = pgTable('profiles', {
    id:        uuid('id').primaryKey().defaultRandom(),
    tenantId:  uuid('tenant_id')
                  .notNull()
                  .references(() => tenants.id, { onDelete: 'cascade' }),
    name:      text('name').notNull(),
    profile:   text('profile').notNull(),     // 'lossless' | 'lossy:high' | 'lossy:streaming'
    bitDepth:  integer('bit_depth').notNull(),
    width:     integer('width').notNull(),
    height:    integer('height').notNull(),
    metadata:  jsonb('metadata').notNull().default({}),
    createdAt: timestamp('created_at', { withTimezone: true })
                  .default(sql`now()`)
                  .notNull(),
    updatedAt: timestamp('updated_at', { withTimezone: true })
                  .default(sql`now()`)
                  .notNull(),
});

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

export const sessions = pgTable('sessions', {
    id:        uuid('id').primaryKey().defaultRandom(),
    tenantId:  uuid('tenant_id')
                  .notNull()
                  .references(() => tenants.id, { onDelete: 'cascade' }),
    profileId: uuid('profile_id').references(() => profiles.id, {
                  onDelete: 'set null',
              }),
    sensorId:  text('sensor_id').notNull(),
    state:     text('state').notNull(),       // 'pending' | 'active' | 'completed' | 'failed'
    agentSessionId: text('agent_session_id'), // returned by the agent
    width:     integer('width').notNull(),
    height:    integer('height').notNull(),
    bitDepth:  integer('bit_depth').notNull(),
    startedAt: timestamp('started_at', { withTimezone: true }),
    endedAt:   timestamp('ended_at',   { withTimezone: true }),
    metadata:  jsonb('metadata').notNull().default({}),
    createdAt: timestamp('created_at', { withTimezone: true })
                  .default(sql`now()`)
                  .notNull(),
});

// ---------------------------------------------------------------------------
// Recordings
// ---------------------------------------------------------------------------

export const recordings = pgTable('recordings', {
    id:        uuid('id').primaryKey().defaultRandom(),
    tenantId:  uuid('tenant_id')
                  .notNull()
                  .references(() => tenants.id, { onDelete: 'cascade' }),
    sessionId: uuid('session_id')
                  .notNull()
                  .references(() => sessions.id, { onDelete: 'cascade' }),
    storageUrl:    text('storage_url'),       // populated on finalize
    sizeBytes:     integer('size_bytes'),
    framesTotal:   integer('frames_total'),
    finalized:     boolean('finalized').notNull().default(false),
    createdAt:     timestamp('created_at', { withTimezone: true })
                      .default(sql`now()`)
                      .notNull(),
    finalizedAt:   timestamp('finalized_at', { withTimezone: true }),
});
