// src/routes/Settings.tsx
//
// User-facing settings: encoder knobs and UI preferences.

import React from 'react';

import EncoderSettings from '../components/EncoderSettings';
import { useStore } from '../store';

function applyTheme(next: 'light' | 'dark' | 'system'): void {
    const root = document.documentElement;
    if (next === 'dark') {
        root.classList.add('dark');
    } else if (next === 'light') {
        root.classList.remove('dark');
    } else {
        const prefers = window.matchMedia('(prefers-color-scheme: dark)').matches;
        root.classList.toggle('dark', prefers);
    }
}

export default function Settings(): JSX.Element {
    const ui = useStore((s) => s.settings.ui);
    const setUi = useStore((s) => s.setUiPreferences);

    return (
        <div className="grid gap-6 lg:grid-cols-2">
            <EncoderSettings />

            <section aria-labelledby="ui-heading" className="panel space-y-4">
                <h2 id="ui-heading" className="text-lg font-semibold">Interface</h2>

                <fieldset>
                    <legend className="text-sm font-medium mb-2">Theme</legend>
                    <div role="radiogroup" className="flex gap-2">
                        {(['light', 'dark', 'system'] as const).map((t) => (
                            <label
                                key={t}
                                className={[
                                    'px-3 py-1.5 text-sm rounded-md cursor-pointer border',
                                    ui.theme === t
                                        ? 'border-brand-primary bg-brand-primary/10'
                                        : 'border-ink/10 dark:border-ink-inverse/10',
                                ].join(' ')}
                            >
                                <input
                                    type="radio"
                                    name="ui-theme"
                                    value={t}
                                    className="sr-only"
                                    checked={ui.theme === t}
                                    onChange={() => {
                                        setUi({ theme: t });
                                        applyTheme(t);
                                    }}
                                />
                                {t}
                            </label>
                        ))}
                    </div>
                </fieldset>

                <label className="block">
                    <span className="text-sm font-medium">Viewer point size (px)</span>
                    <input
                        type="number"
                        min={1}
                        max={8}
                        step={1}
                        className="input-field mt-1"
                        value={ui.viewerPointSize}
                        onChange={(e) => setUi({ viewerPointSize: Number(e.target.value) })}
                    />
                </label>

                <fieldset>
                    <legend className="text-sm font-medium mb-2">Chart units</legend>
                    <div role="radiogroup" className="flex gap-2">
                        {(['mbps', 'kbps'] as const).map((u) => (
                            <label
                                key={u}
                                className={[
                                    'px-3 py-1.5 text-sm rounded-md cursor-pointer border',
                                    ui.chartUnits === u
                                        ? 'border-brand-primary bg-brand-primary/10'
                                        : 'border-ink/10 dark:border-ink-inverse/10',
                                ].join(' ')}
                            >
                                <input
                                    type="radio"
                                    name="ui-units"
                                    value={u}
                                    className="sr-only"
                                    checked={ui.chartUnits === u}
                                    onChange={() => setUi({ chartUnits: u })}
                                />
                                {u === 'mbps' ? 'Mb/s' : 'kb/s'}
                            </label>
                        ))}
                    </div>
                </fieldset>

                <label className="flex items-center gap-2">
                    <input
                        type="checkbox"
                        checked={ui.reducedMotion}
                        onChange={(e) => setUi({ reducedMotion: e.target.checked })}
                    />
                    <span className="text-sm">Reduce motion (skip animated transitions)</span>
                </label>
            </section>
        </div>
    );
}
