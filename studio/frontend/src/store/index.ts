// src/store/index.ts
//
// Composes the five sliced stores into one Zustand root. The persist
// middleware whitelists only `settings.ui` -- encoder settings and
// session/metrics are server-driven, not local-first.

import { create } from 'zustand';
import { persist, createJSONStorage } from 'zustand/middleware';

import type { AuthSlice } from './auth';
import { createAuthSlice } from './auth';
import type { SensorsSlice } from './sensors';
import { createSensorsSlice } from './sensors';
import type { SessionSlice } from './session';
import { createSessionSlice } from './session';
import type { MetricsSlice } from './metrics';
import { createMetricsSlice } from './metrics';
import type { SettingsSlice } from './settings';
import { createSettingsSlice } from './settings';

export type RootState = AuthSlice & SensorsSlice & SessionSlice & MetricsSlice & SettingsSlice;

export const useStore = create<RootState>()(
    persist(
        (...a) => ({
            ...createAuthSlice(...a),
            ...createSensorsSlice(...a),
            ...createSessionSlice(...a),
            ...createMetricsSlice(...a),
            ...createSettingsSlice(...a),
        }),
        {
            name: 'scc-studio-prefs/v1',
            storage: createJSONStorage(() => localStorage),
            // Persist only UI preferences -- never tokens, never server state.
            partialize: (state) => ({
                settings: { ui: state.settings.ui, encoder: state.settings.encoder },
            }),
            // Selective rehydration: deep-merge so newly-added defaults survive
            // a partial localStorage payload from an earlier release.
            merge: (persisted, current) => {
                const p = (persisted ?? {}) as Partial<RootState>;
                return {
                    ...current,
                    settings: {
                        encoder: { ...current.settings.encoder, ...(p.settings?.encoder ?? {}) },
                        ui: { ...current.settings.ui, ...(p.settings?.ui ?? {}) },
                    },
                };
            },
        },
    ),
);

// Stable selector helpers -- consumers import these to avoid re-renders.
export const selectAuthToken = (s: RootState) => s.auth.token;
export const selectActiveSession = (s: RootState) => s.session.active;
export const selectLatestFrame = (s: RootState) => s.metrics.latestFrame;
export const selectWsConnected = (s: RootState) => s.metrics.wsConnected;
