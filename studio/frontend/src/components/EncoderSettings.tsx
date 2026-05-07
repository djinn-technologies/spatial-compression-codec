// src/components/EncoderSettings.tsx
//
// Encoder-side knobs (profile, bit depth, target FPS, SIMD toggle).
// Local store updates are immediate; remote persistence happens on
// blur via api.updateEncoderSettings so we don't spam the API on every
// keystroke.

import React, { useState } from 'react';

import { api, ApiError } from '../api/client';
import type { EncoderProfile } from '../store/settings';
import { useStore } from '../store';

const PROFILES: Array<{ id: EncoderProfile; label: string; help: string }> = [
    { id: 'lossless',   label: 'Lossless',   help: 'Bit-exact reconstruction. Largest payloads.' },
    { id: 'lossy_high', label: 'Lossy high', help: 'Visually lossless. Recommended default.'    },
    { id: 'lossy_low',  label: 'Lossy low',  help: 'Smallest payloads. Acceptable for previews.' },
];

export default function EncoderSettings(): JSX.Element {
    const settings = useStore((s) => s.settings.encoder);
    const setEncoderSettings = useStore((s) => s.setEncoderSettings);
    const [pushError, setPushError] = useState<string | null>(null);
    const [pushing, setPushing] = useState(false);

    async function persist(patch: Parameters<typeof setEncoderSettings>[0]): Promise<void> {
        setEncoderSettings(patch);
        setPushError(null);
        setPushing(true);
        try {
            await api.updateEncoderSettings(patch);
        } catch (e) {
            setPushError(e instanceof ApiError ? `${e.code}: ${e.message}` : String(e));
        } finally {
            setPushing(false);
        }
    }

    return (
        <section aria-labelledby="encoder-heading" className="panel space-y-4">
            <h2 id="encoder-heading" className="text-lg font-semibold">Encoder</h2>

            <fieldset>
                <legend className="text-sm font-medium mb-2">Profile</legend>
                <div className="grid gap-2">
                    {PROFILES.map((p) => (
                        <label
                            key={p.id}
                            className={[
                                'flex items-start gap-2 rounded p-2 cursor-pointer border',
                                settings.profile === p.id
                                    ? 'border-brand-primary bg-brand-primary/5'
                                    : 'border-ink/10 dark:border-ink-inverse/10',
                            ].join(' ')}
                        >
                            <input
                                type="radio"
                                name="encoder-profile"
                                value={p.id}
                                checked={settings.profile === p.id}
                                onChange={() => persist({ profile: p.id })}
                                className="mt-1"
                            />
                            <span>
                                <span className="block font-medium">{p.label}</span>
                                <span className="block text-xs text-ink-muted">{p.help}</span>
                            </span>
                        </label>
                    ))}
                </div>
            </fieldset>

            <div className="grid grid-cols-2 gap-4">
                <label className="block">
                    <span className="text-sm font-medium">Bit depth</span>
                    <select
                        className="input-field mt-1"
                        value={settings.bitDepth}
                        onChange={(e) =>
                            persist({ bitDepth: Number(e.target.value) as 8 | 10 | 12 })
                        }
                    >
                        <option value={8}>8</option>
                        <option value={10}>10</option>
                        <option value={12}>12</option>
                    </select>
                </label>
                <label className="block">
                    <span className="text-sm font-medium">Target FPS</span>
                    <input
                        type="number"
                        min={1}
                        max={120}
                        className="input-field mt-1"
                        value={settings.targetFps}
                        onChange={(e) => setEncoderSettings({ targetFps: Number(e.target.value) })}
                        onBlur={() => persist({ targetFps: settings.targetFps })}
                    />
                </label>
            </div>

            <label className="flex items-center gap-2">
                <input
                    type="checkbox"
                    checked={settings.enableSimd}
                    onChange={(e) => persist({ enableSimd: e.target.checked })}
                />
                <span className="text-sm">Enable SIMD acceleration (AVX2 / NEON)</span>
            </label>

            {pushing && (
                <p role="status" className="text-xs text-ink-muted">Saving&hellip;</p>
            )}
            {pushError && (
                <p role="alert" className="text-xs text-red-600">{pushError}</p>
            )}
        </section>
    );
}
