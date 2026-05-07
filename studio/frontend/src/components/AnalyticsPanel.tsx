// src/components/AnalyticsPanel.tsx
//
// Lightweight rolling-window line chart for live encoder telemetry.
// Hand-rolled inline SVG instead of a charting library: ~1 KB gzipped
// stays well under the per-route bundle budget.
//
// Displays:
//   - latest fps / bitrate / encode-ms (numeric)
//   - 60-second sparkline of bitrate
//
// All visuals come from CSS variables so the chart respects the brand
// palette and dark mode without recompilation.

import React from 'react';

import { useStore } from '../store';

const W = 360;
const H = 80;
const PAD = 4;

function buildPath(samples: number[]): string {
    if (samples.length === 0) return '';
    const max = Math.max(1, ...samples);
    const stepX = (W - PAD * 2) / Math.max(1, samples.length - 1);
    return samples
        .map((v, i) => {
            const x = PAD + i * stepX;
            const y = H - PAD - (v / max) * (H - PAD * 2);
            return `${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`;
        })
        .join(' ');
}

export default function AnalyticsPanel(): JSX.Element {
    const samples = useStore((s) => s.metrics.samples);
    const wsConnected = useStore((s) => s.metrics.wsConnected);
    const wsError = useStore((s) => s.metrics.wsError);
    const units = useStore((s) => s.settings.ui.chartUnits);

    const last = samples[samples.length - 1];
    const window = samples.slice(-Math.min(samples.length, 60));
    const series = window.map((s) =>
        units === 'mbps' ? s.bitrateMbps : s.bitrateMbps * 1000,
    );
    const path = buildPath(series);

    return (
        <section aria-labelledby="analytics-heading" className="panel space-y-3">
            <header className="flex items-center justify-between">
                <h2 id="analytics-heading" className="text-lg font-semibold">Live metrics</h2>
                <span
                    className={[
                        'text-xs font-medium px-2 py-0.5 rounded',
                        wsConnected
                            ? 'bg-brand-accent/20 text-brand-accent-dark'
                            : 'bg-ink/10 text-ink-muted',
                    ].join(' ')}
                    aria-live="polite"
                >
                    {wsConnected ? 'streaming' : 'idle'}
                </span>
            </header>

            <dl className="grid grid-cols-3 gap-3 text-sm">
                <div>
                    <dt className="text-ink-muted">FPS</dt>
                    <dd className="text-2xl font-semibold tabular-nums">
                        {last ? last.fps.toFixed(1) : '–'}
                    </dd>
                </div>
                <div>
                    <dt className="text-ink-muted">{units === 'mbps' ? 'Mb/s' : 'kb/s'}</dt>
                    <dd className="text-2xl font-semibold tabular-nums">
                        {last
                            ? (units === 'mbps'
                                ? last.bitrateMbps.toFixed(2)
                                : (last.bitrateMbps * 1000).toFixed(0))
                            : '–'}
                    </dd>
                </div>
                <div>
                    <dt className="text-ink-muted">Encode ms</dt>
                    <dd className="text-2xl font-semibold tabular-nums">
                        {last ? last.encodeMs.toFixed(2) : '–'}
                    </dd>
                </div>
            </dl>

            <svg
                role="img"
                aria-label={`Bitrate sparkline, ${series.length} samples`}
                viewBox={`0 0 ${W} ${H}`}
                className="w-full h-20"
            >
                <path
                    d={path}
                    fill="none"
                    stroke="currentColor"
                    strokeWidth={1.5}
                    className="text-brand-accent"
                />
            </svg>

            {wsError && (
                <p role="alert" className="text-xs text-red-600">{wsError}</p>
            )}
        </section>
    );
}
