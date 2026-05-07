// src/routes/Playback.tsx

import React, { useEffect, useState } from 'react';

import PlaybackPanel from '../components/PlaybackPanel';
import VolumetricViewer from '../components/VolumetricViewer';
import { MetricsSocket } from '../api/ws';

export default function Playback(): JSX.Element {
    const [selectedId, setSelectedId] = useState<string | null>(null);

    useEffect(() => {
        if (!selectedId) return;
        // Replay over the same WS endpoint -- the agent treats a closed
        // session as a "replay from disk" stream, in chronological order.
        const sock = new MetricsSocket(selectedId);
        sock.connect();
        return () => sock.close();
    }, [selectedId]);

    return (
        <div className="grid gap-6 lg:grid-cols-[minmax(0,420px)_1fr]">
            <PlaybackPanel onSelect={setSelectedId} />
            <div className="min-h-[480px]">
                <VolumetricViewer />
            </div>
        </div>
    );
}
