# @djinn/scc-wasm

WebAssembly binding for the **Spatial Compression Codec** — encode and
decode H.264-compatible depth-frame SEI payloads in Node 20+ and
Chromium 113+ (or any browser with WASM SIMD + `TransformStream`).

> Zero npm runtime dependencies. The WASM binary is shipped inside the
> package; no network fetch is needed beyond the package install.

## Install

```bash
npm install @djinn/scc-wasm
```

## Quick start (Node 20+ ESM)

```ts
import { SCCEncoder, SCCDecoder } from '@djinn/scc-wasm';

const W = 1280, H = 720;
const enc = await SCCEncoder.create({ width: W, height: H, bitDepth: 12 });
const dec = await SCCDecoder.create();

// `depth` is a row-major Uint16Array (W*H samples).
const sei = await enc.encode(depth);

// Round-trip:
const decoded = await dec.decode(sei);
console.log(decoded.width, decoded.height);   // 1280 720
console.log(decoded.depth instanceof Uint16Array);   // true

enc.close();
dec.close();
```

## Quick start (Browser)

```html
<script type="module">
    import {
        SCCEncoder, SCCDecoder
    } from 'https://cdn.example.com/@djinn/scc-wasm/dist/index.js';

    const W = 1280, H = 720;
    const enc = await SCCEncoder.create({ width: W, height: H });
    const dec = await SCCDecoder.create();

    const depth = new Uint16Array(W * H);
    // ... fill depth ...

    const sei = await enc.encode(depth);
    const decoded = await dec.decode(sei);

    enc.close();
    dec.close();
</script>
```

The package exports a single ESM bundle. Most build tools resolve the
companion `scc.wasm` automatically via `import.meta.url`. If you serve the
package yourself, ship `dist/scc.wasm` next to `dist/scc.js`.

## WebCodecs streaming

For browser pipelines that already produce `VideoFrame`s alongside depth,
use the TransformStream adapters:

```ts
import {
    encoderTransformStream,
    decoderTransformStream,
} from '@djinn/scc-wasm';

const sender = source                                       // ReadableStream<{rgb: VideoFrame, depth: Uint16Array}>
    .pipeThrough(encoderTransformStream({ width: 1280, height: 720 }));

for await (const sei of sender) {
    socket.send(sei);                                       // ArrayBuffer-friendly
}
```

The encoder transform takes ownership of each `VideoFrame`'s GPU
resources: it calls `videoFrame.close()` after consuming the frame, even
on the error path.

Back-pressure is honored automatically: every `transform()` callback
awaits the encode/decode promise before `controller.enqueue(...)`, so a
full readable queue throttles upstream pulls. [Ultrathink #2]

## Profiles

| Profile             | Use case                                                  |
|---------------------|-----------------------------------------------------------|
| `lossless`          | Capture / archival. Bit-exact round-trip.                |
| `lossy:high`        | Default. Visual fidelity prioritised over bandwidth.      |
| `lossy:streaming`   | Real-time streaming. Lower bitrate, motion-aware tuning.  |

Each profile maps to concrete codec parameters under the hood
(see `src/scc.ts: kProfileParams`).

## API

See generated TypeScript declarations in `dist/index.d.ts`. The full
public surface:

- `class SCCEncoder` — `static create()`, `encode()`, `close()`.
- `class SCCDecoder` — `static create()`, `decode()`, `close()`.
- `function encoderTransformStream(opts)` — returns `TransformStream`.
- `function decoderTransformStream()` — returns `TransformStream`.
- `interface SCCEncoderOptions` — `{ profile?, bitDepth?, width, height }`.
- `interface DecodedDepthFrame` — `{ depth, width, height, bitDepth }`.

VS Code IntelliSense works out of the box once `@djinn/scc-wasm` is in
your `node_modules` (the package ships `.d.ts` + source maps).
[Ultrathink #4]

## Building from source

Requires Emscripten (tested with 3.1.50+) and Node 20+.

```bash
cd sdk/wasm
bash emscripten/build.sh                    # produces dist/scc.js + dist/scc.wasm
npm run build:ts                            # produces dist/*.js + dist/*.d.ts
npm test                                    # vitest, Node round-trip + heap stability
npm run test:browser                        # playwright, Chromium real-browser
npm run size                                # check gzip(scc.js + scc.wasm) <= 800 KiB
```

## Bundle size

The `npm run size` script is the CI gate (Ultrathink #3). Default budget:
**800 KiB gzip** for `scc.js + scc.wasm` combined. Override via
`SCC_WASM_BUDGET_KIB=NNN`.

## Memory ownership

Every encode/decode call follows the pattern:

1. Allocate scratch in the WASM heap (`_malloc`).
2. Copy JS data IN.
3. Call C ABI.
4. Copy WASM data OUT to a fresh JS-owned buffer.
5. Free every scratch pointer (`_free`), even on error.

No pointer ever crosses the JS/WASM boundary alive past one call.
[Ultrathink #1]

## Browser support

- Chromium 113+ (WASM SIMD on by default; WebCodecs `VideoFrame` GA).
- Firefox 130+ (`VideoFrame` behind a flag in older versions).
- Safari 17+.
- Node 20+ (the streaming adapters work without `VideoFrame`; the field
  may be undefined in non-browser contexts).

## License

Apache-2.0.
