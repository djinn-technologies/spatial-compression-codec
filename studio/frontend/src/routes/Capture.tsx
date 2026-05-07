// src/routes/Capture.tsx
//
// The capture route is the working surface: sensor + profile picker on
// the left, live volumetric viewer on the right, analytics underneath.
// The MetricsSocket lifecycle is owned here so it gets torn down when
// the user navigates away.

import React, { useEffect, useMemo, useState } from 'react';

import AnalyticsPanel from '../components/AnalyticsPanel';
import CapturePanel from '../components/CapturePanel';
import VolumetricViewer from '../components/VolumetricViewer';
import { api, ApiError } from '../api/client';
import { MetricsSocket } from '../api/ws';
import { useStore } from '../store';

export default function Capture(): JSX.Element {
    const setSensors = useStore((s) => s.setSensors);
    const setSensorsLoading = useStore((s) => s.setSensorsLoading);
    const setSensorsError = useStore((s) => s.setSensorsError);
    const session = useStore((s) => s.session.active);

    const [profiles, setProfiles] = useState<Array<{ id: string; name: string; description: string }>>([]);
    const [profileId, setProfileId] = useState<string>('');
    const [profilesError, setProfilesError] = useState<string | null>(null);

    // Initial fetch: sensors + profiles in parallel.
    useEffect(() => {
        const ac = new AbortController();
        setSensorsLoading(true);
        Promise.all([
            api.listSensors(ac.signal),
            api.listProfiles(ac.signal),
        ])
            .then(([sensorsR, profilesR]) => {
                setSensors(sensorsR.sensors);
                setProfiles(profilesR.profiles);
                if (profilesR.profiles.length > 0) {
                    setProfileId((cur) => cur || profilesR.profiles[0]!.id);
                }
            })
            .catch((e) => {
                if (ac.signal.aborted) return;
                const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
                setSensorsError(msg);
                setProfilesError(msg);
            });
        return () => ac.abort();
    }, [setSensors, setSensorsLoading, setSensorsError]);

    // Drive the live-metrics WS off the active session id.
    const sessionId = session?.id && session.id !== 'pending' ? session.id : null;
    const socket = useMemo(
        () => (sessionId ? new MetricsSocket(sessionId) : null),
        [sessionId],
    );
    useEffect(() => {
        if (!socket) return;
        socket.connect();
        return () => socket.close();
    }, [socket]);

    return (
        <div className="grid gap-6 lg:grid-cols-[minmax(0,360px)_1fr]">
            <div className="space-y-6">
                <section aria-labelledby="profile-heading" className="panel">
                    <h2 id="profile-heading" className="text-lg font-semibold mb-2">Profile</h2>
                    {profilesError ? (
                        <p role="alert" className="text-sm text-red-600">{profilesError}</p>
                    ) : (
                        <select
                            className="input-field"
                            value={profileId}
                            onChange={(e) => setProfileId(e.target.value)}
                            aria-label="Encoder profile"
                        >
                            {profiles.map((p) => (
                                <option key={p.id} value={p.id}>{p.name}</option>
                            ))}
                        </select>
                    )}
                </section>

                <CapturePanel profileId={profileId} />
                <AnalyticsPanel />
            </div>

            <div className="min-h-[480px]">
                <VolumetricViewer />
            </div>
        </div>
    );
}
