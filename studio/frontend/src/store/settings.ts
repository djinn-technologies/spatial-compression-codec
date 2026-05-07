// src/store/settings.ts
//
// Encoder-settings + UI-preferences slice. Encoder settings (profile,
// quality, hw flags) are persisted server-side -- they ride on the
// session POST. UI preferences (theme, viewer point-size, chart units)
// live in localStorage via the persist middleware applied at compose
// time in src/store/index.ts.

import type { StateCreator } from 'zustand';

export type EncoderProfile = 'lossless' | 'lossy_high' | 'lossy_low';

export interface EncoderSettings {
    profile: EncoderProfile;
    bitDepth: 8 | 10 | 12;
    targetFps: number;
    enableSimd: boolean;
}

export interface UiPreferences {
    theme: 'light' | 'dark' | 'system';
    viewerPointSize: number;       // pixels
    chartUnits: 'mbps' | 'kbps';
    reducedMotion: boolean;
}

export interface SettingsSlice {
    settings: {
        encoder: EncoderSettings;
        ui: UiPreferences;
    };
    setEncoderSettings: (patch: Partial<EncoderSettings>) => void;
    setUiPreferences: (patch: Partial<UiPreferences>) => void;
}

const DEFAULT_ENCODER: EncoderSettings = {
    profile: 'lossy_high',
    bitDepth: 12,
    targetFps: 30,
    enableSimd: true,
};

const DEFAULT_UI: UiPreferences = {
    theme: 'system',
    viewerPointSize: 2,
    chartUnits: 'mbps',
    reducedMotion: false,
};

export const createSettingsSlice: StateCreator<SettingsSlice, [], [], SettingsSlice> = (set) => ({
    settings: { encoder: DEFAULT_ENCODER, ui: DEFAULT_UI },
    setEncoderSettings: (patch) =>
        set((s) => ({
            settings: {
                ...s.settings,
                encoder: { ...s.settings.encoder, ...patch },
            },
        })),
    setUiPreferences: (patch) =>
        set((s) => ({
            settings: {
                ...s.settings,
                ui: { ...s.settings.ui, ...patch },
            },
        })),
});
