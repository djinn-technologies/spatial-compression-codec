// tests/scc.node.test.ts
//
// Vitest -- Node 20+ ESM round-trip + parameter API.
//
// Mapped to AI prompt #7 test list:
//   prompt #1 -> roundtrip (W1)
//   prompt #3 -> heap stability (W2 in scc.memory.test.ts)

import { describe, it, expect, afterEach } from 'vitest';
import { SCCEncoder, SCCDecoder } from '../dist/index.js';
// ^ tests run AFTER `npm run build`. The TS source compiles to ./dist/.

function makeFrame(W: number, H: number, seed: number): Uint16Array {
    const out = new Uint16Array(W * H);
    let s = seed >>> 0;
    for (let i = 0; i < out.length; ++i) {
        s = (s * 1664525 + 1013904223) >>> 0;
        out[i] = s & 0x3FF;                 // 10-bit-ish depth values
    }
    return out;
}

describe('SCC WASM binding (Node)', () => {
    let openInstances: { close(): void }[] = [];
    afterEach(() => {
        for (const inst of openInstances) {
            try { inst.close(); } catch { /* ignore */ }
        }
        openInstances = [];
    });

    it('round-trips a 16x16 synthetic frame', async () => {
        const W = 16, H = 16;
        const enc = await SCCEncoder.create({ width: W, height: H, bitDepth: 12 });
        openInstances.push(enc);
        const dec = await SCCDecoder.create();
        openInstances.push(dec);

        const depth = makeFrame(W, H, 0xC0FFEE);
        const sei = await enc.encode(depth);
        expect(sei).toBeInstanceOf(Uint8Array);
        expect(sei.byteLength).toBeGreaterThan(0);

        const out = await dec.decode(sei);
        expect(out.width).toBe(W);
        expect(out.height).toBe(H);
        expect(out.bitDepth).toBe(12);
        expect(out.depth).toEqual(depth);
    });

    it('round-trips a non-trivial 64x48 frame', async () => {
        const W = 64, H = 48;
        const enc = await SCCEncoder.create({
            width: W,
            height: H,
            bitDepth: 16,
            profile: 'lossless',
        });
        openInstances.push(enc);
        const dec = await SCCDecoder.create();
        openInstances.push(dec);

        const depth = makeFrame(W, H, 0xDEADBEEF);
        const sei = await enc.encode(depth);
        const out = await dec.decode(sei);
        expect(out.width).toBe(W);
        expect(out.height).toBe(H);
        expect(out.bitDepth).toBe(16);
        expect(out.depth).toEqual(depth);
    });

    it('rejects mis-sized depth arrays', async () => {
        const enc = await SCCEncoder.create({ width: 16, height: 16 });
        openInstances.push(enc);
        const wrong = new Uint16Array(15 * 16);
        await expect(enc.encode(wrong)).rejects.toThrow(/length/);
    });

    it('rejects malformed SEI bytes', async () => {
        const dec = await SCCDecoder.create();
        openInstances.push(dec);
        const garbage = new Uint8Array(64);
        for (let i = 0; i < garbage.length; ++i) garbage[i] = (i * 31) & 0xFF;
        await expect(dec.decode(garbage)).rejects.toThrow();
    });

    it('close() is idempotent and prevents further calls', async () => {
        const enc = await SCCEncoder.create({ width: 8, height: 8 });
        enc.close();
        enc.close();                                       // no-op
        await expect(enc.encode(new Uint16Array(64)))
            .rejects.toThrow(/closed/);
    });

    it('supports the three named profiles', async () => {
        for (const profile of ['lossless', 'lossy:high', 'lossy:streaming'] as const) {
            const enc = await SCCEncoder.create({
                width: 16, height: 16, profile,
            });
            openInstances.push(enc);
            const depth = makeFrame(16, 16, 0xA5A5A5);
            const sei = await enc.encode(depth);
            expect(sei.byteLength).toBeGreaterThan(0);
        }
    });
});
