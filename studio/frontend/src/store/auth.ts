// src/store/auth.ts
//
// Auth slice. Tracks the current API token + the JWT principal claims.
// The HTTP client reads `useStore.getState().auth.token` per request
// and adds the Authorization header.

import type { StateCreator } from 'zustand';

export interface AuthSlice {
    auth: {
        token: string | null;
        principal: {
            kind: 'apiKey' | 'user';
            tenantId: string;
            id: string;
        } | null;
    };
    setToken: (token: string | null, principal: AuthSlice['auth']['principal']) => void;
    clearAuth: () => void;
}

export const createAuthSlice: StateCreator<AuthSlice, [], [], AuthSlice> = (set) => ({
    auth: { token: null, principal: null },
    setToken: (token, principal) => set({ auth: { token, principal } }),
    clearAuth: () => set({ auth: { token: null, principal: null } }),
});
