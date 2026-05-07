// src/routes/Profiles.tsx
//
// Read-only directory of available encoder profiles, with an inline
// description for each. Editing profiles is a server-side concern and
// is intentionally out of scope for the frontend's first release.

import React, { useEffect, useState } from 'react';

import { api, ApiError } from '../api/client';

interface ProfileRow {
    id: string;
    name: string;
    description: string;
}

export default function Profiles(): JSX.Element {
    const [profiles, setProfiles] = useState<ProfileRow[]>([]);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState<string | null>(null);

    useEffect(() => {
        const ac = new AbortController();
        setLoading(true);
        api.listProfiles(ac.signal)
            .then((r) => setProfiles(r.profiles))
            .catch((e) => {
                if (ac.signal.aborted) return;
                setError(e instanceof ApiError ? `${e.code}: ${e.message}` : String(e));
            })
            .finally(() => {
                if (!ac.signal.aborted) setLoading(false);
            });
        return () => ac.abort();
    }, []);

    return (
        <section aria-labelledby="profiles-heading" className="space-y-4">
            <h1 id="profiles-heading" className="text-2xl font-semibold">Profiles</h1>

            {loading && <p role="status" className="text-ink-muted">Loading&hellip;</p>}
            {error && <p role="alert" className="text-red-600">{error}</p>}

            <ul className="grid gap-3 md:grid-cols-2">
                {profiles.map((p) => (
                    <li key={p.id} className="panel">
                        <h2 className="font-semibold">{p.name}</h2>
                        <p className="text-xs font-mono text-ink-muted">{p.id}</p>
                        <p className="mt-2 text-sm">{p.description}</p>
                    </li>
                ))}
            </ul>
        </section>
    );
}
