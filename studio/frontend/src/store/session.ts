// src/store/session.ts
//
// Capture-session slice. Mirrors the API's /v1/sessions resource:
// the user picks a sensor + profile, POSTs to /v1/sessions, and we
// track the returned id + state machine here. The agent-side session
// id is held alongside so the live-metrics WS knows what to subscribe
// to.

import type { StateCreator } from 'zustand';

export type SessionState =
    | 'idle'
    | 'creating'
    | 'capturing'
    | 'stopping'
    | 'completed'
    | 'failed';

export interface SessionRecord {
    id: string;
    sensorId: string;
    profileId: string;
    agentSessionId: string | null;
    state: SessionState;
    startedAt: string | null;
    endedAt: string | null;
    error: string | null;
}

export interface SessionSlice {
    session: {
        active: SessionRecord | null;
        history: SessionRecord[];
    };
    setActiveSession: (record: SessionRecord | null) => void;
    setSessionState: (state: SessionState, error?: string | null) => void;
    pushSessionHistory: (record: SessionRecord) => void;
    clearSession: () => void;
}

export const createSessionSlice: StateCreator<SessionSlice, [], [], SessionSlice> = (set) => ({
    session: { active: null, history: [] },
    setActiveSession: (record) =>
        set((s) => ({ session: { ...s.session, active: record } })),
    setSessionState: (state, error = null) =>
        set((s) => ({
            session: {
                ...s.session,
                active: s.session.active
                    ? { ...s.session.active, state, error }
                    : null,
            },
        })),
    pushSessionHistory: (record) =>
        set((s) => ({
            session: {
                ...s.session,
                // Cap history at 50 to avoid unbounded growth.
                history: [record, ...s.session.history].slice(0, 50),
            },
        })),
    clearSession: () =>
        set((s) => ({ session: { ...s.session, active: null } })),
});
