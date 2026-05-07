// tests/store.test.ts
//
// Unit tests for every Zustand slice. These cover the algebra of the
// store -- ring-buffer cap on metrics, history truncation on session,
// settings deep-merge -- so the components only have to assert on
// rendering.

import { describe, it, expect } from 'vitest';

import { useStore } from '../src/store';
import type { MetricSample } from '../src/store/metrics';
import type { SensorDescriptor } from '../src/store/sensors';
import type { SessionRecord } from '../src/store/session';

describe('auth slice', () => {
    it('sets and clears the token', () => {
        useStore.getState().setToken('abc', { kind: 'apiKey', tenantId: 't1', id: 'k1' });
        expect(useStore.getState().auth.token).toBe('abc');
        expect(useStore.getState().auth.principal?.tenantId).toBe('t1');

        useStore.getState().clearAuth();
        expect(useStore.getState().auth.token).toBeNull();
        expect(useStore.getState().auth.principal).toBeNull();
    });
});

describe('sensors slice', () => {
    const fixture: SensorDescriptor[] = [
        { id: 's1', vendor: 'Intel', model: 'D435', backend: 'librealsense', modes: [] },
        { id: 's2', vendor: 'Microsoft', model: 'Azure Kinect', backend: 'k4a', modes: [] },
    ];

    it('clears loading and error when setSensors is called', () => {
        const s = useStore.getState();
        s.setSensorsLoading(true);
        s.setSensorsError('boom');
        s.setSensors(fixture);
        const after = useStore.getState().sensors;
        expect(after.list).toHaveLength(2);
        expect(after.loading).toBe(false);
        expect(after.error).toBeNull();
    });

    it('selects and deselects', () => {
        useStore.getState().setSensors(fixture);
        useStore.getState().selectSensor('s2');
        expect(useStore.getState().sensors.selectedId).toBe('s2');
        useStore.getState().selectSensor(null);
        expect(useStore.getState().sensors.selectedId).toBeNull();
    });
});

describe('session slice', () => {
    const rec = (id: string): SessionRecord => ({
        id,
        sensorId: 's1',
        profileId: 'lossy_high',
        agentSessionId: null,
        state: 'capturing',
        startedAt: null,
        endedAt: null,
        error: null,
    });

    it('updates the active state in place', () => {
        useStore.getState().setActiveSession(rec('a'));
        useStore.getState().setSessionState('failed', 'oops');
        const got = useStore.getState().session.active!;
        expect(got.state).toBe('failed');
        expect(got.error).toBe('oops');
    });

    it('caps history at 50 entries', () => {
        for (let i = 0; i < 60; ++i) useStore.getState().pushSessionHistory(rec(String(i)));
        expect(useStore.getState().session.history).toHaveLength(50);
        // Newest first.
        expect(useStore.getState().session.history[0]!.id).toBe('59');
    });
});

describe('metrics slice', () => {
    const sample = (t: number): MetricSample => ({
        timestamp: t,
        fps: 30,
        bitrateMbps: 5,
        encodeMs: 1.2,
        decodeMs: 0.8,
        payloadBytes: 1024,
        profile: 'lossy_high',
    });

    it('rings the sample buffer at 600', () => {
        for (let i = 0; i < 1000; ++i) useStore.getState().pushMetricSample(sample(i));
        const samples = useStore.getState().metrics.samples;
        expect(samples).toHaveLength(600);
        expect(samples[0]!.timestamp).toBe(400);
        expect(samples[samples.length - 1]!.timestamp).toBe(999);
    });

    it('clearMetrics drops samples and frame', () => {
        useStore.getState().pushMetricSample(sample(1));
        useStore.getState().setLatestFrame({
            width: 4, height: 4, bitDepth: 12, data: new Uint16Array(16), timestamp: 1,
        });
        useStore.getState().clearMetrics();
        expect(useStore.getState().metrics.samples).toHaveLength(0);
        expect(useStore.getState().metrics.latestFrame).toBeNull();
    });
});

describe('settings slice', () => {
    it('merges patches into encoder and ui', () => {
        useStore.getState().setEncoderSettings({ profile: 'lossless' });
        useStore.getState().setEncoderSettings({ targetFps: 60 });
        const enc = useStore.getState().settings.encoder;
        expect(enc.profile).toBe('lossless');
        expect(enc.targetFps).toBe(60);
        // bitDepth was not touched; default preserved.
        expect(enc.bitDepth).toBe(12);

        useStore.getState().setUiPreferences({ theme: 'dark' });
        expect(useStore.getState().settings.ui.theme).toBe('dark');
    });
});
