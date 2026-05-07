// sdk/wasm/src/scc.ts
//
// Public TypeScript facade for the Spatial Compression Codec.
//
// REQ-027 — This module is the only surface end-users (Node 20+, Chromium
// 113+) interact with. The underlying C ABI is reached only through the
// `raw.ts` shim. Memory ownership stays inside this module: every pointer
// allocated for a single encode/decode call is freed before the Promise
// resolves, on every code path including throws. [Ultrathink #1]

import {
    WASMScope,
    getModule,
    readLastError,
    type SCCModule,
} from './raw.js';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

export type SCCProfile = 'lossless' | 'lossy:high' | 'lossy:streaming';

export interface SCCEncoderOptions {
    /** Compression profile (see README §"Profiles"). Default: `'lossy:high'`. */
    profile?: SCCProfile;
    /** Bits per depth sample on the wire. Must match the input Uint16Array's
     *  depth-bits — the values fit in uint16 either way, but the SEI header
     *  records this so downstream tooling knows the dynamic range. */
    bitDepth?: 8 | 12 | 16;
    width: number;
    height: number;
}

export interface DecodedDepthFrame {
    depth: Uint16Array;
    width: number;
    height: number;
    bitDepth: number;
}

// ---------------------------------------------------------------------------
// Profile -> parameter mapping.
// ---------------------------------------------------------------------------

interface ParamSet {
    mode_flags: string;
    top_count: string;
    tau_static: string;
    tau_low: string;
}

const kProfileParams: Record<SCCProfile, ParamSet> = {
    lossless:           { mode_flags: '1', top_count: '4', tau_static: '1',  tau_low: '5'  },
    'lossy:high':       { mode_flags: '0', top_count: '4', tau_static: '5',  tau_low: '30' },
    'lossy:streaming':  { mode_flags: '2', top_count: '4', tau_static: '10', tau_low: '50' },
};

function applyProfile(mod: SCCModule, ctx: number, opts: SCCEncoderOptions): void {
    const profile = opts.profile ?? 'lossy:high';
    const params = kProfileParams[profile];
    const enc = new TextEncoder();
    const setParam = (key: string, value: string): void => {
        const k = enc.encode(key + '\0');
        const v = enc.encode(value + '\0');
        const kp = mod._malloc(k.byteLength);
        const vp = mod._malloc(v.byteLength);
        if (kp === 0 || vp === 0) {
            if (kp) mod._free(kp);
            if (vp) mod._free(vp);
            throw new Error('WASM _malloc failed in applyProfile');
        }
        try {
            mod.HEAPU8.set(k, kp);
            mod.HEAPU8.set(v, vp);
            const rc = mod._scc_set_param(ctx, kp, vp);
            if (rc !== 0) {
                throw new Error(
                    `scc_set_param ${key}=${value}: ` + readLastError(mod, ctx)
                );
            }
        } finally {
            mod._free(kp);
            mod._free(vp);
        }
    };
    setParam('mode_flags', params.mode_flags);
    setParam('top_count',  params.top_count);
    setParam('tau_static', params.tau_static);
    setParam('tau_low',    params.tau_low);
}

// ---------------------------------------------------------------------------
// SCCEncoder
// ---------------------------------------------------------------------------

/**
 * Streaming SCC encoder.
 *
 * Lifecycle:
 * ```ts
 * const enc = await SCCEncoder.create({ width: 1280, height: 720 });
 * const sei = await enc.encode(depth);  // Uint16Array (1280*720 elements)
 * enc.close();
 * ```
 *
 * @see REQ-027 — async-Promise public surface.
 */
export class SCCEncoder {
    private mod: SCCModule;
    private ctx: number;
    private opts: SCCEncoderOptions;
    private bitDepth: number;
    private closed = false;

    private constructor(
        mod: SCCModule,
        ctx: number,
        opts: SCCEncoderOptions,
        bitDepth: number
    ) {
        this.mod = mod;
        this.ctx = ctx;
        this.opts = opts;
        this.bitDepth = bitDepth;
    }

    /**
     * Create a fresh encoder context. The WASM module is loaded lazily on
     * the first call across all encoder/decoder instances. [REQ-027]
     */
    static async create(opts: SCCEncoderOptions): Promise<SCCEncoder> {
        if (!Number.isInteger(opts.width)  || opts.width  <= 0) {
            throw new Error(`SCCEncoder.create: invalid width=${opts.width}`);
        }
        if (!Number.isInteger(opts.height) || opts.height <= 0) {
            throw new Error(`SCCEncoder.create: invalid height=${opts.height}`);
        }
        const bitDepth = opts.bitDepth ?? 12;
        if (bitDepth !== 8 && bitDepth !== 12 && bitDepth !== 16) {
            throw new Error(`SCCEncoder.create: invalid bitDepth=${bitDepth}`);
        }
        const mod = await getModule();
        const ctx = mod._scc_init();
        if (ctx === 0) {
            throw new Error('scc_init failed (WASM out of memory?)');
        }
        try {
            applyProfile(mod, ctx, opts);
        } catch (err) {
            mod._scc_destroy(ctx);
            throw err;
        }
        return new SCCEncoder(mod, ctx, opts, bitDepth);
    }

    /**
     * Encode one depth frame to an SEI byte payload. The output is the
     * EBSP (emulation-prevention-escaped) body of an H.264
     * `user_data_unregistered` SEI message; the caller wraps it in a NAL
     * unit (e.g. via libavcodec). [REQ-013, REQ-027]
     *
     * @param depth - Width*Height uint16 depth samples, row-major.
     * @returns Uint8Array of SEI bytes.
     */
    async encode(depth: Uint16Array): Promise<Uint8Array> {
        if (this.closed) throw new Error('SCCEncoder is closed');
        const expected = this.opts.width * this.opts.height;
        if (depth.length !== expected) {
            throw new Error(
                `SCCEncoder.encode: depth length ${depth.length} ` +
                `does not match width*height ${expected}`
            );
        }

        const mod = this.mod;
        const scope = new WASMScope(mod);
        try {
            const stride = this.opts.width * 2;

            // Copy depth into the WASM heap.
            const depthBytes = new Uint8Array(
                depth.buffer, depth.byteOffset, depth.byteLength
            );
            const depthPtr = scope.mallocCopy(depthBytes);
            const lenPtr   = scope.mallocInt32();

            // Probe-then-call. First call with out_sei = 0 -> SCC_INVALID_ARG
            // is expected; the C ABI sets *out_len to required size.
            const probeRc = mod._scc_encode_frame(
                this.ctx,
                depthPtr,
                stride,
                this.opts.width,
                this.opts.height,
                this.bitDepth,
                0,           // out_sei = nullptr -> probe mode
                0,           // out_cap = 0
                lenPtr
            );
            // probeRc === 1 (SCC_INVALID_ARG) is expected here when probing.
            // The real failure modes set rc != 1 OR leave required at 0.
            const required = mod.HEAP32[lenPtr >> 2] ?? 0;
            if (required <= 0) {
                throw new Error(
                    'scc_encode_frame probe failed: ' +
                    readLastError(mod, this.ctx) +
                    ` (rc=${probeRc})`
                );
            }

            // Actual call.
            const outPtr = scope.malloc(required);
            const rc = mod._scc_encode_frame(
                this.ctx,
                depthPtr,
                stride,
                this.opts.width,
                this.opts.height,
                this.bitDepth,
                outPtr,
                required,
                lenPtr
            );
            if (rc !== 0) {
                throw new Error(
                    'scc_encode_frame failed: ' +
                    readLastError(mod, this.ctx) +
                    ` (rc=${rc})`
                );
            }
            const actual = mod.HEAP32[lenPtr >> 2] ?? 0;
            if (actual <= 0 || actual > required) {
                throw new Error(
                    `scc_encode_frame returned implausible out_len=${actual}`
                );
            }

            // Copy result out of the WASM heap. Re-fetch HEAPU8 in case any
            // allocation grew the heap.
            const result = new Uint8Array(actual);
            result.set(mod.HEAPU8.subarray(outPtr, outPtr + actual));
            return result;
        } finally {
            scope.release();
        }
    }

    /**
     * Release the underlying SCC context. Subsequent calls to `encode`
     * throw. Idempotent: calling `close()` twice is a no-op.
     */
    close(): void {
        if (this.closed) return;
        this.closed = true;
        if (this.ctx !== 0) {
            this.mod._scc_destroy(this.ctx);
            this.ctx = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// SCCDecoder
// ---------------------------------------------------------------------------

/**
 * SCC decoder. Symmetrical to {@link SCCEncoder}; takes the SEI bytes and
 * recovers the depth frame.
 *
 * @see REQ-014 — round-trippable demux.
 */
export class SCCDecoder {
    private mod: SCCModule;
    private ctx: number;
    private closed = false;

    private constructor(mod: SCCModule, ctx: number) {
        this.mod = mod;
        this.ctx = ctx;
    }

    static async create(): Promise<SCCDecoder> {
        const mod = await getModule();
        const ctx = mod._scc_init();
        if (ctx === 0) throw new Error('scc_init failed');
        return new SCCDecoder(mod, ctx);
    }

    /**
     * Decode SEI bytes back to depth samples. [REQ-014, REQ-027]
     */
    async decode(sei: Uint8Array): Promise<DecodedDepthFrame> {
        if (this.closed) throw new Error('SCCDecoder is closed');
        if (!(sei instanceof Uint8Array)) {
            throw new Error('SCCDecoder.decode: sei must be a Uint8Array');
        }

        const mod = this.mod;
        const scope = new WASMScope(mod);
        try {
            const seiPtr = scope.mallocCopy(sei);
            const wPtr  = scope.mallocInt32();
            const hPtr  = scope.mallocInt32();
            const bdPtr = scope.mallocInt32();

            // Probe: out_depth = 0 -> SCC_INVALID_ARG, but the dimensions
            // are populated.
            const probeRc = mod._scc_decode_sei(
                this.ctx,
                seiPtr,
                sei.byteLength,
                0,            // out_depth = nullptr -> probe mode
                0,            // out_stride = 0
                wPtr, hPtr, bdPtr
            );
            const W  = mod.HEAP32[wPtr  >> 2] ?? 0;
            const H  = mod.HEAP32[hPtr  >> 2] ?? 0;
            const BD = mod.HEAP32[bdPtr >> 2] ?? 0;
            if (W <= 0 || H <= 0) {
                throw new Error(
                    'scc_decode_sei probe failed: ' +
                    readLastError(mod, this.ctx) +
                    ` (rc=${probeRc})`
                );
            }

            // Allocate output buffer in WASM heap.
            const stride = W * 2;
            const depthPtr = scope.malloc(stride * H);
            const rc = mod._scc_decode_sei(
                this.ctx,
                seiPtr,
                sei.byteLength,
                depthPtr,
                stride,
                wPtr, hPtr, bdPtr
            );
            if (rc !== 0) {
                throw new Error(
                    'scc_decode_sei failed: ' +
                    readLastError(mod, this.ctx) +
                    ` (rc=${rc})`
                );
            }

            // Copy out as Uint16Array. Re-fetch HEAPU8 buffer reference.
            const depth = new Uint16Array(W * H);
            depth.set(
                new Uint16Array(mod.HEAPU8.buffer, depthPtr, W * H)
            );
            return { depth, width: W, height: H, bitDepth: BD };
        } finally {
            scope.release();
        }
    }

    close(): void {
        if (this.closed) return;
        this.closed = true;
        if (this.ctx !== 0) {
            this.mod._scc_destroy(this.ctx);
            this.ctx = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// WebCodecs TransformStream adapters
//
// Back-pressure (Ultrathink #2): TransformStream's writable side back-
// pressures automatically when the readable queue is full. The async
// `transform` callback awaits the encode/decode promise before
// `controller.enqueue(...)`; the await is the back-pressure path.
// Upstream producers see slowed pulls without any manual rate-limiting.
//
// VideoFrame ownership: the encoder transform receives `{rgb, depth}`
// where `rgb` is a browser VideoFrame holding a GPU resource. We MUST
// call `rgb.close()` after consuming it; otherwise the GPU surface
// leaks. The close runs in `finally` so it fires even on encoder errors.
// ---------------------------------------------------------------------------

export interface RGBDepthChunk {
    rgb: VideoFrame;
    depth: Uint16Array;
}

/**
 * Browser-side TransformStream for an encoder pipeline.
 *
 * @example
 * ```ts
 * const stream = sourceFrames                           // ReadableStream<{rgb, depth}>
 *     .pipeThrough(encoderTransformStream({width, height}));
 * for await (const sei of stream) socket.send(sei);
 * ```
 *
 * @see REQ-027 — async public surface.
 */
export function encoderTransformStream(
    opts: SCCEncoderOptions
): TransformStream<RGBDepthChunk, Uint8Array> {
    let encoder: SCCEncoder | null = null;
    return new TransformStream<RGBDepthChunk, Uint8Array>({
        async start(): Promise<void> {
            encoder = await SCCEncoder.create(opts);
        },
        async transform(chunk, controller): Promise<void> {
            if (!encoder) throw new Error('encoder not initialised');
            try {
                const sei = await encoder.encode(chunk.depth);
                controller.enqueue(sei);
            } finally {
                // Release the GPU-backed VideoFrame even on error. v1 does
                // not consume `rgb` further; it is reserved for future
                // RGB+depth fusion paths.
                if (chunk.rgb && typeof chunk.rgb.close === 'function') {
                    chunk.rgb.close();
                }
            }
        },
        flush(): void {
            encoder?.close();
            encoder = null;
        },
    });
}

/**
 * Browser-side TransformStream for a decoder pipeline.
 *
 * @example
 * ```ts
 * const frames = networkBytes
 *     .pipeThrough(decoderTransformStream())
 *     .pipeThrough(/* paint to canvas *\/);
 * ```
 *
 * @see REQ-014, REQ-027.
 */
export function decoderTransformStream(): TransformStream<Uint8Array, DecodedDepthFrame> {
    let decoder: SCCDecoder | null = null;
    return new TransformStream<Uint8Array, DecodedDepthFrame>({
        async start(): Promise<void> {
            decoder = await SCCDecoder.create();
        },
        async transform(sei, controller): Promise<void> {
            if (!decoder) throw new Error('decoder not initialised');
            const out = await decoder.decode(sei);
            controller.enqueue(out);
        },
        flush(): void {
            decoder?.close();
            decoder = null;
        },
    });
}
