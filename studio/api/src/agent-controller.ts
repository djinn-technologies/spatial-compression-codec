// src/agent-controller.ts
//
// IPC client for scc-studio-agent. Connects to the agent's Unix
// socket and exchanges length-prefixed protobuf messages defined in
// studio/agent/src/proto/control.proto.
//
// One persistent connection per API process; reconnects on disconnect
// with exponential backoff. Outbound RPCs are queued; only one in
// flight at a time (the agent's IPC contract is request/response).

import { EventEmitter } from 'node:events';
import * as net from 'node:net';
import { setTimeout as sleep } from 'node:timers/promises';

import type { Logger } from 'pino';
import * as protobuf from 'protobufjs';

import { resolve as resolvePath } from 'node:path';
import { fileURLToPath } from 'node:url';

const PROTO_PATH = resolvePath(
    fileURLToPath(import.meta.url),
    '..', '..', '..', 'agent', 'src', 'proto', 'control.proto',
);

interface AgentRequestPayload {
    list?: Record<string, never>;
    start?: {
        sensorId: string;
        width: number;
        height: number;
        bitDepth: number;
        profile: string;
        outputPath?: string;
        fps: number;
    };
    stop?: { sessionId: string };
    metrics?: { sessionId: string };
}

export class AgentClient extends EventEmitter {
    private root: protobuf.Root | null = null;
    private requestT: protobuf.Type | null = null;
    private responseT: protobuf.Type | null = null;
    private socket: net.Socket | null = null;
    private connecting = false;
    private buffer = Buffer.alloc(0);
    private pending: ((bytes: Buffer) => void)[] = [];
    private writing = Promise.resolve();

    constructor(
        private socketPath: string,
        private log: Logger,
    ) {
        super();
    }

    async load(): Promise<void> {
        this.root = await protobuf.load(PROTO_PATH);
        this.requestT  = this.root.lookupType('scc.studio.agent.v1.AgentRequest');
        this.responseT = this.root.lookupType('scc.studio.agent.v1.AgentResponse');
    }

    async connect(): Promise<void> {
        if (this.socket && !this.socket.destroyed) return;
        if (this.connecting) {
            await new Promise<void>((r) => this.once('connected', r));
            return;
        }
        this.connecting = true;
        try {
            for (let attempt = 0; ; attempt++) {
                try {
                    await this.openOnce();
                    this.emit('connected');
                    return;
                } catch (err) {
                    const backoff = Math.min(1000 * 2 ** attempt, 10_000);
                    this.log.warn(
                        { err, attempt, backoff },
                        'agent ipc connect failed, retrying',
                    );
                    await sleep(backoff);
                }
            }
        } finally {
            this.connecting = false;
        }
    }

    private async openOnce(): Promise<void> {
        await new Promise<void>((resolve, reject) => {
            const s = net.createConnection(this.socketPath);
            s.once('connect', () => {
                this.socket = s;
                s.on('data', (chunk) => this.onData(chunk));
                s.once('close', () => {
                    this.log.warn('agent ipc disconnected');
                    this.socket = null;
                    // Reject any in-flight pending callbacks.
                    while (this.pending.length > 0) {
                        const cb = this.pending.shift();
                        cb?.(Buffer.alloc(0));
                    }
                    this.emit('disconnected');
                });
                resolve();
            });
            s.once('error', (err) => {
                s.destroy();
                reject(err);
            });
        });
    }

    private onData(chunk: Buffer): void {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (this.buffer.length >= 4) {
            const len = this.buffer.readUInt32BE(0);
            if (this.buffer.length < 4 + len) break;
            const frame = this.buffer.subarray(4, 4 + len);
            this.buffer = this.buffer.subarray(4 + len);
            const cb = this.pending.shift();
            cb?.(frame);
        }
    }

    async call(payload: AgentRequestPayload): Promise<Record<string, unknown>> {
        if (!this.requestT || !this.responseT) {
            throw new Error('AgentClient.load() not called');
        }
        await this.connect();
        const sock = this.socket;
        if (!sock) throw new Error('agent ipc not connected');

        const req = this.requestT.create(payload);
        const buf = Buffer.from(this.requestT.encode(req).finish());
        const len = Buffer.alloc(4);
        len.writeUInt32BE(buf.length, 0);

        const responseFuture = new Promise<Buffer>((resolve) => {
            this.pending.push(resolve);
        });
        // Serialize writes so we never interleave halves of two frames.
        this.writing = this.writing.then(
            () =>
                new Promise<void>((resolve, reject) => {
                    sock.write(Buffer.concat([len, buf]), (err) =>
                        err ? reject(err) : resolve(),
                    );
                }),
        );
        await this.writing;

        const respBytes = await responseFuture;
        if (respBytes.length === 0) {
            throw new Error('agent disconnected before responding');
        }
        const resp = this.responseT.decode(respBytes);
        return this.responseT.toObject(resp, {
            longs: Number,
            enums: String,
            defaults: true,
            arrays: true,
            objects: true,
        }) as Record<string, unknown>;
    }

    async close(): Promise<void> {
        this.socket?.destroy();
        this.socket = null;
    }
}
