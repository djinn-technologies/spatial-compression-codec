// src/components/PlaybackPanel.tsx
//
// Lists past sessions and lets the user replay one through the
// volumetric viewer. Replay is implemented as a "fetch session frames"
// endpoint that the agent serves; for v1 we surface the controls but
// document the network behaviour as the only source of truth.

import React, { useEffect, useState } from 'react';

import { api, ApiError } from '../api/client';
import type { SessionRecord } from '../store/session';

interface PlaybackPanelProps {
    onSelect?: (id: string) => void;
}

export default function PlaybackPanel({ onSelect }: PlaybackPanelProps): JSX.Element {
    const [sessions, setSessions] = useState<SessionRecord[]>([]);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState<string | null>(null);
    const [selectedId, setSelectedId] = useState<string | null>(null);

    useEffect(() => {
        const ac = new AbortController();
        setLoading(true);
        api.listSessions(ac.signal)
            .then((r) => setSessions(r.sessions))
            .catch((e) => {
                if (ac.signal.aborted) return;
                setError(e instanceof ApiError ? `${e.code}: ${e.message}` : String(e));
            })
            .finally(() => {
                if (!ac.signal.aborted) setLoading(false);
            });
        return () => ac.abort();
    }, []);

    function pick(id: string): void {
        setSelectedId(id);
        onSelect?.(id);
    }

    return (
        <section aria-labelledby="playback-heading" className="panel space-y-3">
            <h2 id="playback-heading" className="text-lg font-semibold">Recorded sessions</h2>

            {loading && (
                <p role="status" className="text-sm text-ink-muted">Loading sessions&hellip;</p>
            )}
            {error && (
                <p role="alert" className="text-sm text-red-600">{error}</p>
            )}

            {!loading && sessions.length === 0 && !error && (
                <p className="text-sm text-ink-muted">
                    No recorded sessions yet. Capture one to enable playback.
                </p>
            )}

            <ul className="divide-y divide-ink/10 dark:divide-ink-inverse/10">
                {sessions.map((s) => (
                    <li key={s.id} className="py-2 flex items-center gap-3">
                        <button
                            type="button"
                            className={[
                                'flex-1 text-left rounded px-2 py-1 hover:bg-surface-light dark:hover:bg-surface-dark/40',
                                selectedId === s.id ? 'ring-2 ring-brand-accent' : '',
                            ].join(' ')}
                            aria-pressed={selectedId === s.id}
                            onClick={() => pick(s.id)}
                        >
                            <div className="font-mono text-sm">{s.id}</div>
                            <div className="text-xs text-ink-muted">
                                Sensor {s.sensorId} &middot; profile {s.profileId} &middot; {s.state}
                            </div>
                        </button>
                    </li>
                ))}
            </ul>
        </section>
    );
}
