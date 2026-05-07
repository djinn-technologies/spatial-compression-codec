// src/api/ws.ts
//
// Live-metrics WebSocket adapter. The Studio API streams two message
// kinds on /v1/sessions/:id/stream:
//   1. {kind: "metric",  ...MetricSample}      (JSON, ~10/s)
//   2. {kind: "frame",   width, height, ...}   (JSON header)
//      followed immediately by the binary depth frame as a blob.
//
// The wire protocol uses interleaved text+binary so we don't have to
// base64 the depth buffer. Reconnection is exponential-backoff
// (1s, 2s, 4s, max 30s) and is stopped on close-code 4401 (unauthorised)
// since that's a credential issue, not a transient network failure.

import { useStore } from '../store';
import type { MetricSample, DepthFrame } from '../store/metrics';

interface FrameHeader {
    kind: 'frame';
    width: number;
    height: number;
    bitDepth: number;
    timestamp: number;
}

interface MetricMessage extends MetricSample {
    kind: 'metric';
}

type Envelope = MetricMessage | FrameHeader;

const MAX_BACKOFF_MS = 30_000;
const INITIAL_BACKOFF_MS = 1_000;

export class MetricsSocket {
    private ws: WebSocket | null = null;
    private backoff = INITIAL_BACKOFF_MS;
    private retryTimer: ReturnType<typeof setTimeout> | null = null;
    private pendingFrameHeader: FrameHeader | null = null;
    private closed = false;

    constructor(private readonly sessionId: string) {}

    connect(): void {
        if (this.closed) return;

        const token = useStore.getState().auth.token;
        // The protocol arg is how browsers transport custom auth on a
        // WebSocket -- you can't set Authorization on the upgrade. The
        // server's ws upgrade handler reads `protocol[1]` as the token.
        const protocols = token ? ['scc.v1', `bearer.${token}`] : ['scc.v1'];

        const url = this.buildUrl();
        let ws: WebSocket;
        try {
            ws = new WebSocket(url, protocols);
        } catch (err) {
            useStore.getState().setWsError(
                err instanceof Error ? err.message : 'WebSocket construction failed',
            );
            this.scheduleReconnect();
            return;
        }
        ws.binaryType = 'arraybuffer';

        ws.addEventListener('open', () => {
            this.backoff = INITIAL_BACKOFF_MS;
            useStore.getState().setWsConnected(true);
            useStore.getState().setWsError(null);
        });

        ws.addEventListener('message', (ev) => this.onMessage(ev));

        ws.addEventListener('close', (ev) => {
            useStore.getState().setWsConnected(false);
            // 4401 == auth failure; do not retry, surface to the user.
            if (ev.code === 4401) {
                useStore.getState().setWsError('Unauthorised. Please sign in again.');
                this.closed = true;
                return;
            }
            if (!this.closed) this.scheduleReconnect();
        });

        ws.addEventListener('error', () => {
            useStore.getState().setWsError('WebSocket connection error');
        });

        this.ws = ws;
    }

    close(): void {
        this.closed = true;
        if (this.retryTimer !== null) {
            clearTimeout(this.retryTimer);
            this.retryTimer = null;
        }
        this.ws?.close();
        this.ws = null;
        useStore.getState().setWsConnected(false);
    }

    private buildUrl(): string {
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        return `${proto}//${location.host}/api/v1/sessions/${encodeURIComponent(this.sessionId)}/stream`;
    }

    private onMessage(ev: MessageEvent): void {
        // Binary frame following a text header.
        if (ev.data instanceof ArrayBuffer) {
            const header = this.pendingFrameHeader;
            this.pendingFrameHeader = null;
            if (!header) {
                // Out-of-order binary; drop. Logging here would be noisy
                // under reconnect, and the server should never send one
                // without a header preceding it.
                return;
            }
            // Defensive copy: the runtime is free to recycle ev.data.
            const u16 = new Uint16Array(ev.data.slice(0));
            const expected = header.width * header.height;
            if (u16.length !== expected) {
                useStore.getState().setWsError(
                    `Frame size mismatch: got ${u16.length} samples, expected ${expected}`,
                );
                return;
            }
            const frame: DepthFrame = {
                width: header.width,
                height: header.height,
                bitDepth: header.bitDepth,
                data: u16,
                timestamp: header.timestamp,
            };
            useStore.getState().setLatestFrame(frame);
            return;
        }

        // Text envelope.
        if (typeof ev.data !== 'string') return;
        let parsed: Envelope;
        try {
            parsed = JSON.parse(ev.data) as Envelope;
        } catch {
            useStore.getState().setWsError('Invalid JSON from agent stream');
            return;
        }
        if (parsed.kind === 'metric') {
            const { kind: _kind, ...sample } = parsed;
            useStore.getState().pushMetricSample(sample);
        } else if (parsed.kind === 'frame') {
            this.pendingFrameHeader = parsed;
        }
    }

    private scheduleReconnect(): void {
        if (this.closed) return;
        const delay = this.backoff;
        this.backoff = Math.min(this.backoff * 2, MAX_BACKOFF_MS);
        this.retryTimer = setTimeout(() => {
            this.retryTimer = null;
            this.connect();
        }, delay);
    }
}
