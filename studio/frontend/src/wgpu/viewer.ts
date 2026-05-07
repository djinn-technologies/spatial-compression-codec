// src/wgpu/viewer.ts
//
// Minimal WebGPU point-cloud viewer used by VolumetricViewer.tsx.
//
// Lifecycle:
//   const v = await Viewer.create(canvas);   // throws if WebGPU absent
//   v.uploadFrame(depthFrame);                // each new frame
//   v.setCamera(viewProj);                    // when controls move
//   v.render();
//   v.dispose();
//
// We separate `create()` (async, may throw) from the constructor so the
// React component can render a fallback while WebGPU initialises.
//
// Defensive behaviour required by the prompt:
//   - check `'gpu' in navigator` before requestAdapter
//   - subscribe to device.lost and surface to the caller
//   - wrap each frame in pushErrorScope('validation') / popErrorScope
//   - never throw from render(); errors go through onError

import { mat4 } from 'gl-matrix';
import type { DepthFrame } from '../store/metrics';

import vertexShaderSource from './shaders/vertex.wgsl?raw';
import fragmentShaderSource from './shaders/fragment.wgsl?raw';

export interface ViewerOptions {
    pointSize?: number;        // default 2 px
    near?: number;             // default 0.2 m
    far?: number;              // default 6.0 m
    depthScale?: number;       // u16 -> metres; default 1/1000 (RealSense default)
    onError?: (err: Error) => void;
    onDeviceLost?: (info: GPUDeviceLostInfo) => void;
}

// mat4 (64) + 11 scalars (44) = 108 bytes; round up to a 16-byte
// boundary because WGSL requires `uniform` struct stride to be a
// multiple of the struct's alignment (16, set by the mat4x4<f32>).
const UNIFORM_SIZE_BYTES = 112;

export class Viewer {
    private constructor(
        private readonly canvas: HTMLCanvasElement,
        private readonly device: GPUDevice,
        private readonly context: GPUCanvasContext,
        private readonly format: GPUTextureFormat,
        private readonly pipeline: GPURenderPipeline,
        private readonly uniformBuffer: GPUBuffer,
        private readonly opts: Required<Omit<ViewerOptions, 'onError' | 'onDeviceLost'>>,
        private readonly userOnError: (err: Error) => void,
    ) {}

    static async create(canvas: HTMLCanvasElement, options: ViewerOptions = {}): Promise<Viewer> {
        if (typeof navigator === 'undefined' || !('gpu' in navigator) || !navigator.gpu) {
            throw new Error('WebGPU is not available in this browser');
        }

        const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
        if (!adapter) {
            throw new Error('No WebGPU adapter available');
        }

        const device = await adapter.requestDevice();
        device.lost.then((info) => {
            const handler = options.onDeviceLost;
            if (handler) handler(info);
        }).catch(() => { /* terminal; nothing else to do */ });

        const context = canvas.getContext('webgpu') as GPUCanvasContext | null;
        if (!context) {
            throw new Error('canvas.getContext("webgpu") returned null');
        }
        const format = navigator.gpu.getPreferredCanvasFormat();
        context.configure({ device, format, alphaMode: 'premultiplied' });

        const vsModule = device.createShaderModule({
            label: 'scc-viewer-vs',
            code: vertexShaderSource,
        });
        const fsModule = device.createShaderModule({
            label: 'scc-viewer-fs',
            code: fragmentShaderSource,
        });

        const pipeline = device.createRenderPipeline({
            label: 'scc-viewer-pipeline',
            layout: 'auto',
            vertex: { module: vsModule, entryPoint: 'vs_main' },
            fragment: {
                module: fsModule,
                entryPoint: 'fs_main',
                targets: [{ format }],
            },
            primitive: { topology: 'point-list' },
        });

        const uniformBuffer = device.createBuffer({
            label: 'scc-viewer-uniforms',
            size: UNIFORM_SIZE_BYTES,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });

        const filled: Required<Omit<ViewerOptions, 'onError' | 'onDeviceLost'>> = {
            pointSize: options.pointSize ?? 2,
            near: options.near ?? 0.2,
            far: options.far ?? 6.0,
            depthScale: options.depthScale ?? 1 / 1000,
        };
        const onError = options.onError ?? ((e) => console.error('[scc viewer]', e));

        return new Viewer(canvas, device, context, format, pipeline, uniformBuffer, filled, onError);
    }

    private depthBuffer: GPUBuffer | null = null;
    private bindGroup: GPUBindGroup | null = null;
    private currentFrame: DepthFrame | null = null;

    // CPU-side camera matrix; reused across renders.
    private viewProj: Float32Array = (() => {
        const m = mat4.create();
        mat4.perspective(m, (60 * Math.PI) / 180, 16 / 9, 0.05, 50);
        const v = mat4.create();
        mat4.lookAt(v, [0, 0, -1.5], [0, 0, 1.5], [0, -1, 0]);
        const out = mat4.create();
        mat4.multiply(out, m, v);
        return new Float32Array(out);
    })();

    setCamera(viewProj: Float32Array | mat4): void {
        this.viewProj = viewProj instanceof Float32Array
            ? new Float32Array(viewProj)
            : new Float32Array(viewProj);
    }

    /**
     * Upload a new depth frame. Re-allocates the storage buffer iff the
     * frame dimensions changed; the common case (steady-state stream)
     * is a single writeBuffer call.
     */
    uploadFrame(frame: DepthFrame): void {
        try {
            this.device.pushErrorScope('validation');
            const total = frame.width * frame.height;
            const packedU32 = (total + 1) >>> 1;
            const requiredBytes = packedU32 * 4;

            if (!this.depthBuffer || this.depthBuffer.size !== requiredBytes) {
                this.depthBuffer?.destroy();
                this.depthBuffer = this.device.createBuffer({
                    label: 'scc-viewer-depth',
                    size: requiredBytes,
                    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
                });
                this.bindGroup = this.device.createBindGroup({
                    label: 'scc-viewer-bind',
                    layout: this.pipeline.getBindGroupLayout(0),
                    entries: [
                        { binding: 0, resource: { buffer: this.uniformBuffer } },
                        { binding: 1, resource: { buffer: this.depthBuffer } },
                    ],
                });
            }

            // Pad odd-pixel-count frames to a u32 boundary.
            let payload: ArrayBuffer;
            if (frame.data.byteLength === requiredBytes) {
                payload = frame.data.buffer.slice(
                    frame.data.byteOffset,
                    frame.data.byteOffset + frame.data.byteLength,
                );
            } else {
                const padded = new Uint8Array(requiredBytes);
                padded.set(new Uint8Array(
                    frame.data.buffer,
                    frame.data.byteOffset,
                    frame.data.byteLength,
                ));
                payload = padded.buffer;
            }
            this.device.queue.writeBuffer(this.depthBuffer, 0, payload);
            this.currentFrame = frame;

            this.device.popErrorScope().then((err) => {
                if (err) this.userOnError(new Error(`uploadFrame: ${err.message}`));
            });
        } catch (e) {
            this.userOnError(e instanceof Error ? e : new Error(String(e)));
        }
    }

    render(): void {
        if (!this.currentFrame || !this.depthBuffer || !this.bindGroup) {
            // Clear-only frame (no data yet).
            this.clear();
            return;
        }
        try {
            this.device.pushErrorScope('validation');
            this.writeUniforms(this.currentFrame);

            const encoder = this.device.createCommandEncoder({ label: 'scc-viewer-encoder' });
            const view = this.context.getCurrentTexture().createView();
            const pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view,
                    clearValue: { r: 0.06, g: 0.09, b: 0.16, a: 1 },     // ink-DEFAULT
                    loadOp: 'clear',
                    storeOp: 'store',
                }],
            });
            pass.setPipeline(this.pipeline);
            pass.setBindGroup(0, this.bindGroup);
            pass.draw(this.currentFrame.width * this.currentFrame.height, 1, 0, 0);
            pass.end();
            this.device.queue.submit([encoder.finish()]);

            this.device.popErrorScope().then((err) => {
                if (err) this.userOnError(new Error(`render: ${err.message}`));
            });
        } catch (e) {
            this.userOnError(e instanceof Error ? e : new Error(String(e)));
        }
    }

    private clear(): void {
        const encoder = this.device.createCommandEncoder({ label: 'scc-viewer-clear' });
        const view = this.context.getCurrentTexture().createView();
        const pass = encoder.beginRenderPass({
            colorAttachments: [{
                view,
                clearValue: { r: 0.06, g: 0.09, b: 0.16, a: 1 },
                loadOp: 'clear',
                storeOp: 'store',
            }],
        });
        pass.end();
        this.device.queue.submit([encoder.finish()]);
    }

    private writeUniforms(frame: DepthFrame): void {
        // Per the WGSL Uniforms struct layout (see vertex.wgsl).
        const buf = new ArrayBuffer(UNIFORM_SIZE_BYTES);
        const f32 = new Float32Array(buf);
        const u32 = new Uint32Array(buf);

        f32.set(this.viewProj, 0);                      // mat4: bytes 0..63
        u32[16] = frame.width;                          // bytes 64..67
        u32[17] = frame.height;                         // bytes 68..71
        u32[18] = frame.bitDepth;                       // bytes 72..75
        f32[19] = this.opts.pointSize;                  // bytes 76..79
        // Default RealSense-class intrinsics; sufficient for v1 since
        // the agent does not yet stream per-frame intrinsics. Future
        // work: thread intrinsics through the WS frame header.
        const fx = 0.5 * frame.width;
        const fy = 0.5 * frame.height;
        // Layout (per the WGSL struct in shaders/vertex.wgsl):
        //   indices  0..15 -> viewProj (mat4)
        //   index    16    -> width        (u32)
        //   index    17    -> height       (u32)
        //   index    18    -> bitDepth     (u32)
        //   index    19    -> pointSize    (f32)
        //   index    20    -> fxRcp        (f32)
        //   index    21    -> fyRcp        (f32)
        //   index    22    -> cx           (f32)
        //   index    23    -> cy           (f32)
        //   index    24    -> depthScale   (f32)
        //   index    25    -> near         (f32)
        //   index    26    -> far          (f32)
        f32[20] = 1 / fx;
        f32[21] = 1 / fy;
        f32[22] = frame.width * 0.5;
        f32[23] = frame.height * 0.5;
        f32[24] = this.opts.depthScale;
        f32[25] = this.opts.near;
        f32[26] = this.opts.far;

        this.device.queue.writeBuffer(this.uniformBuffer, 0, buf);
    }

    dispose(): void {
        try {
            this.depthBuffer?.destroy();
            this.uniformBuffer.destroy();
            this.device.destroy();
        } catch {
            // device may already be lost; nothing actionable.
        }
    }
}
