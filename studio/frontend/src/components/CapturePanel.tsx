// src/components/CapturePanel.tsx
//
// Sensor selection + start/stop controls. Reads sensors from the store
// (loaded by the Capture route), creates a session via the API, then
// the Capture route opens a MetricsSocket to that session's id.
//
// All buttons follow the brand button classes from globals.css; the
// disabled state mirrors the session state machine so the user can
// never double-start.

import React, { useState } from 'react';

import { api, ApiError } from '../api/client';
import { useStore } from '../store';

interface CapturePanelProps {
    profileId: string;
    onSessionCreated?: (sessionId: string) => void;
}

export default function CapturePanel({ profileId, onSessionCreated }: CapturePanelProps): JSX.Element {
    const sensors        = useStore((s) => s.sensors);
    const selectSensor   = useStore((s) => s.selectSensor);
    const session        = useStore((s) => s.session.active);
    const setActive      = useStore((s) => s.setActiveSession);
    const setSessionState = useStore((s) => s.setSessionState);
    const encoder        = useStore((s) => s.settings.encoder);

    const [actionError, setActionError] = useState<string | null>(null);

    const isCapturing = session?.state === 'capturing' || session?.state === 'creating';
    const canStart    = !isCapturing && sensors.selectedId !== null && !sensors.loading;

    async function start(): Promise<void> {
        if (!sensors.selectedId) return;
        setActionError(null);
        // Optimistic UI: flip state to creating so the button reflects work.
        setActive({
            id: 'pending',
            sensorId: sensors.selectedId,
            profileId,
            agentSessionId: null,
            state: 'creating',
            startedAt: null,
            endedAt: null,
            error: null,
        });
        try {
            const created = await api.createSession({
                sensorId: sensors.selectedId,
                profileId,
                encoder,
            });
            setActive(created);
            onSessionCreated?.(created.id);
        } catch (e) {
            const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
            setActionError(msg);
            setSessionState('failed', msg);
        }
    }

    async function stop(): Promise<void> {
        if (!session) return;
        setActionError(null);
        setSessionState('stopping');
        try {
            const updated = await api.stopSession(session.id);
            setActive(updated);
        } catch (e) {
            const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
            setActionError(msg);
            setSessionState('failed', msg);
        }
    }

    return (
        <section aria-labelledby="capture-panel-heading" className="panel space-y-4">
            <h2 id="capture-panel-heading" className="text-lg font-semibold">Capture</h2>

            <div>
                <label htmlFor="sensor-select" className="block text-sm font-medium mb-1">
                    Sensor
                </label>
                <select
                    id="sensor-select"
                    className="input-field"
                    disabled={isCapturing || sensors.loading}
                    value={sensors.selectedId ?? ''}
                    onChange={(e) => selectSensor(e.target.value || null)}
                >
                    <option value="">Select a sensor&hellip;</option>
                    {sensors.list.map((s) => (
                        <option key={s.id} value={s.id}>
                            {s.vendor} {s.model} ({s.backend})
                        </option>
                    ))}
                </select>
                {sensors.loading && (
                    <p role="status" className="mt-1 text-xs text-ink-muted">
                        Loading sensors&hellip;
                    </p>
                )}
                {sensors.error && (
                    <p role="alert" className="mt-1 text-xs text-red-600">
                        {sensors.error}
                    </p>
                )}
            </div>

            <div className="flex gap-2">
                <button
                    type="button"
                    className="btn-primary"
                    disabled={!canStart}
                    onClick={start}
                >
                    {session?.state === 'creating' ? 'Starting…' : 'Start capture'}
                </button>
                <button
                    type="button"
                    className="btn-secondary"
                    disabled={!isCapturing}
                    onClick={stop}
                >
                    {session?.state === 'stopping' ? 'Stopping…' : 'Stop'}
                </button>
            </div>

            {actionError && (
                <p role="alert" className="text-sm text-red-600">{actionError}</p>
            )}

            {session && (
                <dl className="grid grid-cols-2 gap-2 text-sm">
                    <dt className="text-ink-muted">Session</dt>
                    <dd className="font-mono">{session.id}</dd>
                    <dt className="text-ink-muted">State</dt>
                    <dd>{session.state}</dd>
                </dl>
            )}
        </section>
    );
}
