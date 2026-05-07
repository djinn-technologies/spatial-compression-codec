// src/store/metrics.ts
//
// Live-metrics slice. The Studio API streams encoder telemetry over a
// WebSocket (/v1/sessions/:id/stream). Each message is one MetricSample;
// we keep a bounded ring buffer for the analytics chart and the most
// recent decoded depth frame for the WebGPU viewer. The ring is sized
// at 600 -- 60 fps * 10 s -- which is enough for the rolling-window
// chart in AnalyticsPanel without growing unbounded.

import type { StateCreator } from 'zustand';

export interface MetricSample {
    timestamp: number;        // unix-ms, agent-side clock
    fps: number;
    bitrateMbps: number;
    encodeMs: number;
    decodeMs: number;
    payloadBytes: number;
    profile: string;
}

export interface DepthFrame {
    width: number;
    height: number;
    bitDepth: number;
    // The 16-bit depth buffer, ready to upload as a GPUBuffer.
    // The WS client allocates a fresh ArrayBuffer per frame; the viewer
    // copies into a persistent GPU buffer and discards.
    data: Uint16Array;
    timestamp: number;
}

const RING_CAPACITY = 600;

export interface MetricsSlice {
    metrics: {
        samples: MetricSample[];     // newest at the end
        latestFrame: DepthFrame | null;
        wsConnected: boolean;
        wsError: string | null;
    };
    pushMetricSample: (sample: MetricSample) => void;
    setLatestFrame: (frame: DepthFrame | null) => void;
    setWsConnected: (connected: boolean) => void;
    setWsError: (error: string | null) => void;
    clearMetrics: () => void;
}

export const createMetricsSlice: StateCreator<MetricsSlice, [], [], MetricsSlice> = (set) => ({
    metrics: {
        samples: [],
        latestFrame: null,
        wsConnected: false,
        wsError: null,
    },
    pushMetricSample: (sample) =>
        set((s) => {
            const next = s.metrics.samples.length >= RING_CAPACITY
                ? [...s.metrics.samples.slice(s.metrics.samples.length - RING_CAPACITY + 1), sample]
                : [...s.metrics.samples, sample];
            return { metrics: { ...s.metrics, samples: next } };
        }),
    setLatestFrame: (frame) =>
        set((s) => ({ metrics: { ...s.metrics, latestFrame: frame } })),
    setWsConnected: (connected) =>
        set((s) => ({ metrics: { ...s.metrics, wsConnected: connected } })),
    setWsError: (error) =>
        set((s) => ({ metrics: { ...s.metrics, wsError: error } })),
    clearMetrics: () =>
        set((s) => ({
            metrics: {
                ...s.metrics,
                samples: [],
                latestFrame: null,
            },
        })),
});
