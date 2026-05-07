// src/api/client.ts
//
// Thin typed fetch wrapper for the Studio control-plane API. Adds the
// Authorization header from the auth slice on every request and
// surfaces structured error envelopes (the API returns {error: {code,
// message}} on non-2xx).
//
// We deliberately avoid axios / ky to keep the bundle small; the
// surface is small enough that hand-rolled fetch is clearer and
// easier to mock in Vitest.

import { useStore } from '../store';
import type { SensorDescriptor } from '../store/sensors';
import type { SessionRecord } from '../store/session';
import type { EncoderSettings } from '../store/settings';

export interface ApiErrorBody {
    code: string;
    message: string;
    details?: unknown;
}

export class ApiError extends Error {
    public readonly status: number;
    public readonly code: string;
    public readonly details: unknown;

    constructor(status: number, body: ApiErrorBody) {
        super(`${body.code}: ${body.message}`);
        this.name = 'ApiError';
        this.status = status;
        this.code = body.code;
        this.details = body.details;
    }
}

interface RequestOpts {
    method?: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';
    body?: unknown;
    signal?: AbortSignal;
}

async function request<T>(path: string, opts: RequestOpts = {}): Promise<T> {
    const token = useStore.getState().auth.token;
    const headers: Record<string, string> = {
        Accept: 'application/json',
    };
    if (token) headers.Authorization = `Bearer ${token}`;
    if (opts.body !== undefined) headers['Content-Type'] = 'application/json';

    const res = await fetch(`/api${path}`, {
        method: opts.method ?? 'GET',
        headers,
        body: opts.body !== undefined ? JSON.stringify(opts.body) : undefined,
        signal: opts.signal,
    });

    if (res.status === 204) {
        // No-content response (DELETE etc.). Cast through unknown to
        // satisfy callers that asked for T = void without forcing them
        // to special-case the path.
        return undefined as unknown as T;
    }

    const text = await res.text();
    const json: unknown = text ? JSON.parse(text) : null;

    if (!res.ok) {
        const body = (typeof json === 'object' && json !== null && 'error' in json
            ? (json as { error: ApiErrorBody }).error
            : { code: 'unknown', message: res.statusText }) satisfies ApiErrorBody;
        throw new ApiError(res.status, body);
    }
    return json as T;
}

// ---------------------------------------------------------------------
// Resource-specific helpers (typed surface).
// ---------------------------------------------------------------------

export interface CreateSessionInput {
    sensorId: string;
    profileId: string;
    encoder: EncoderSettings;
}

export const api = {
    // GET /v1/sensors -> { sensors: [...] }
    listSensors: (signal?: AbortSignal) =>
        request<{ sensors: SensorDescriptor[] }>('/v1/sensors', { signal }),

    // GET /v1/profiles -> { profiles: [{id, name, ...}] }
    listProfiles: (signal?: AbortSignal) =>
        request<{ profiles: Array<{ id: string; name: string; description: string }> }>(
            '/v1/profiles',
            { signal },
        ),

    // POST /v1/sessions
    createSession: (input: CreateSessionInput, signal?: AbortSignal) =>
        request<SessionRecord>('/v1/sessions', {
            method: 'POST',
            body: input,
            signal,
        }),

    // POST /v1/sessions/:id/stop
    stopSession: (id: string, signal?: AbortSignal) =>
        request<SessionRecord>(`/v1/sessions/${encodeURIComponent(id)}/stop`, {
            method: 'POST',
            signal,
        }),

    // GET /v1/sessions/:id
    getSession: (id: string, signal?: AbortSignal) =>
        request<SessionRecord>(`/v1/sessions/${encodeURIComponent(id)}`, { signal }),

    // GET /v1/sessions
    listSessions: (signal?: AbortSignal) =>
        request<{ sessions: SessionRecord[] }>('/v1/sessions', { signal }),

    // PATCH /v1/settings/encoder
    updateEncoderSettings: (patch: Partial<EncoderSettings>, signal?: AbortSignal) =>
        request<EncoderSettings>('/v1/settings/encoder', {
            method: 'PATCH',
            body: patch,
            signal,
        }),
};

// Re-exported so consumers can write `import { api, ApiError } from '@/api/client'`.
export type { SensorDescriptor, SessionRecord };
