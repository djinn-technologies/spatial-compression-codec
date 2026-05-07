// tests/scc.memory.test.ts
//
// 1000-cycle heap-stability test. The WASM heap must not grow without
// bound across encode/decode iterations. We allow up to ±2 MiB of
// drift to absorb internal cache hot-loading.
//
// AI prompt #7 test 3:
//   "1000 encode/decode cycles → measured WASM heap stable
//    (within ±2 MiB)"

import { describe, it, expect } from 'vitest';
import { SCCEncoder, SCCDecoder } from '../dist/index.js';
import { getModule } from '../dist/raw.js';

describe('SCC WASM binding -- heap stability', () => {
    it('1000 encode/decode cycles do not grow the heap unboundedly', async () => {
        const W = 32, H = 32;
        const enc = await SCCEncoder.create({ width: W, height: H, bitDepth: 12 });
        const dec = await SCCDecoder.create();

        const depth = new Uint16Array(W * H);
        for (let i = 0; i < depth.length; ++i) depth[i] = i & 0x3FF;

        // Warm-up: prime any first-call allocations so we don't measure them.
        for (let i = 0; i < 16; ++i) {
            const sei = await enc.encode(depth);
            await dec.decode(sei);
        }

        const mod = await getModule();
        const startBytes = mod.HEAPU8.byteLength;

        const N = parseInt(process.env.SCC_WASM_CYCLES ?? '1000', 10);
        for (let i = 0; i < N; ++i) {
            const sei = await enc.encode(depth);
            const out = await dec.decode(sei);
            // Sanity once in a while -- otherwise the optimiser could in
            // principle elide the work.
            if ((i & 127) === 0) {
                expect(out.depth.length).toBe(depth.length);
            }
        }

        const endBytes = mod.HEAPU8.byteLength;
        const driftBytes = endBytes - startBytes;
        const driftMiB = driftBytes / (1024 * 1024);

        console.log(
            `[heap-stability] N=${N}  start=${(startBytes / 1024 / 1024).toFixed(2)} MiB  ` +
            `end=${(endBytes / 1024 / 1024).toFixed(2)} MiB  drift=${driftMiB.toFixed(2)} MiB`
        );
        // Allow up to +2 MiB. (Heap may grow once-and-stay for working-set
        // reasons; what we forbid is unbounded growth over time.)
        expect(driftBytes).toBeLessThanOrEqual(2 * 1024 * 1024);

        enc.close();
        dec.close();
    });
});
