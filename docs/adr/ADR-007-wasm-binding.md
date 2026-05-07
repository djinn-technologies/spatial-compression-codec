# ADR-007 — WASM / TypeScript binding (`@djinn/scc-wasm`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-027]`, `[ADR-006]`                                           |

## Context

The codec needs to run in browser and Node environments to support the
SDK / Studio web tooling and any browser-based viewers shipping the SCC
format. A WebAssembly binding compiled from the C ABI gives us:

- **One codebase**, two languages: the C++ codec is the source of truth;
  the TS wrapper is thin glue.
- **Zero runtime dependencies** for the npm package — the WASM blob is
  shipped inside, no need for end users to set up a separate native
  build.
- **WebCodecs interop**: the browser's `VideoFrame` + `TransformStream`
  primitives plug naturally into a depth-encoding pipeline.

Decisions to lock:

1. **Binding surface**. What does the TS API look like, and how does
   it relate to the C ABI?
2. **Memory model**. Pointers don't survive across the JS/WASM
   boundary cleanly; how do we marshal Uint8/Uint16 buffers without
   leaking? [Ultrathink #1]
3. **Async semantics**. The C ABI is synchronous; the JS world expects
   Promise-based APIs.
4. **TransformStream back-pressure**. Streaming integrations need to
   honour producer/consumer flow control. [Ultrathink #2]
5. **Bundle-size budget**. The package needs to load fast enough for
   browser use. [Ultrathink #3]
6. **Test infrastructure**. Real Chromium, not jsdom. [Ultrathink #5]

## Decision

### 1. Two-class facade (`SCCEncoder`, `SCCDecoder`) over the C ABI.

The C ABI exposes one opaque context type usable for both encode and
decode (`scc_ctx*`). The TS facade splits this into two classes for
clearer programmer ergonomics — encoders never call `scc_decode_sei`
and vice versa. Both classes hold one `scc_ctx` underneath.

The TS API matches the prompt verbatim:

```ts
class SCCEncoder {
    static async create(opts: SCCEncoderOptions): Promise<SCCEncoder>;
    encode(depth: Uint16Array): Promise<Uint8Array>;
    close(): void;
}
class SCCDecoder {
    static async create(): Promise<SCCDecoder>;
    decode(sei: Uint8Array): Promise<DecodedDepthFrame>;
    close(): void;
}
```

`create()` is async because the WASM module loads lazily on the first
call across the whole process — subsequent encoder/decoder instances
share the same WASM instance, keeping the codec's tables and the
JIT'd code paths hot.

`SCCEncoderOptions.profile` is the only knob most callers need. Three
profiles map to concrete `(mode_flags, top_count, tau_static, tau_low)`
tuples passed through the C ABI's `scc_set_param` (see
`kProfileParams` in `src/scc.ts`). Adding a new profile is a one-row
table edit; the public surface stays stable.

### 2. Scoped allocator + try/finally for memory ownership.

Every encode/decode follows the pattern:

```ts
const scope = new WASMScope(mod);
try {
    const a = scope.malloc(...);   // queued for free
    const b = scope.mallocCopy(jsBuf);
    // ... call C ABI ...
} finally {
    scope.release();               // frees a, b unconditionally
}
```

`WASMScope` collects every `_malloc`'d pointer into a list and frees
the entire list on `release()`. Every public method puts the body in a
`try { ... } finally { scope.release(); }` block, so even an early
throw or a non-zero C ABI return code triggers the free path.
[Ultrathink #1]

A subtle but important detail: `ALLOW_MEMORY_GROWTH=1` may replace the
underlying `ArrayBuffer` of the heap on a `_malloc` that grows the
heap, invalidating any cached `HEAPU8` reference. The TS code reads
`mod.HEAPU8` / `mod.HEAP32` *each time* through the Module's getter,
never caches a reference past a `_malloc` call.

### 3. Promise wrappers; synchronous under the hood.

The C ABI is synchronous (one CPU thread does the whole encode). The
TS wrapper returns `Promise<...>` to match the API contract but the
work happens before the Promise resolves on the same microtask. This
matches the usual JS convention "I/O-shaped APIs return Promises even
if the work is fast" without paying for a Worker round-trip.

True off-thread async (Worker-hosted WASM with postMessage marshalling)
is a future option. Document that v1 is synchronous-under-Promise.
[REQ-027]

### 4. Async `transform()` with controlled `enqueue()` for back-pressure.

`encoderTransformStream` / `decoderTransformStream` are TransformStream
factories. The transform callback is `async`:

```ts
async transform(chunk, controller) {
    const out = await encoder.encode(chunk.depth);
    controller.enqueue(out);
}
```

This pattern is back-pressure-correct: the writable side of a
TransformStream waits for the previous transform's Promise to resolve
before pulling the next chunk. The readable side's queue strategy
(default `highWaterMark: 1`) plus the awaited transform Promise gives
end-to-end back-pressure without any manual rate-limiting.
[Ultrathink #2]

The encoder transform also takes ownership of the `chunk.rgb`
`VideoFrame` and calls `chunk.rgb.close()` in a `finally` block so the
GPU resource is released even on an encoder error. (The `rgb` slot is
reserved for future RGB+depth fusion; v1 does not consume the colour
data.)

### 5. `-O3 -flto -msimd128`, separate `.wasm`, gzip-budget gate.

Compile flags chosen for size + speed:

- `-O3 -flto` — aggressive optimisation + link-time inlining.
- `-msimd128` — WebAssembly SIMD, enabling auto-vectorisation of
  disparity / quadtree / frequency loops.
- `-fexceptions` — the codec uses `std::invalid_argument` throws.
  Emscripten's exception lowering adds some bytes but is needed for
  correctness.
- `-sFILESYSTEM=0 -sASSERTIONS=0 -sNO_EXIT_RUNTIME=1 -sSTRICT=1` —
  drop debug machinery and unused runtime bits.
- `-sMODULARIZE=1 -sEXPORT_NAME=createSCC -sEXPORT_ES6=1` — emit a
  modern factory that returns `Promise<Module>`.
- `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB` — start with enough
  heap for typical workloads (1280×720 frame ≈ 1.84 MiB; scratch a
  few times that).

The `.wasm` is shipped as a separate file (NOT inlined as base64).
This keeps `scc.js` small and lets the browser cache the binary
across page loads; the tradeoff is one extra `fetch()` for the WASM.

`emscripten/check-size.sh` enforces ≤ 800 KiB gzip for
`scc.js + scc.wasm` combined. Wired as an npm script (`npm run size`)
and intended as a CI gate. [Ultrathink #3]

### 6. Vitest (Node) + Playwright (real Chromium).

Two test surfaces:

- **Vitest** runs in Node 20+ and exercises the round-trip
  (`tests/scc.node.test.ts`), profiles, error paths, and the
  1000-cycle heap-stability test (`tests/scc.memory.test.ts`).
- **Playwright** runs the same round-trip in **real Chromium**
  (`tests-browser/scc.browser.test.ts`). The fixture `page.html` is
  served from a tiny in-process HTTP server (so SharedArrayBuffer's
  COOP/COEP headers can be set if we ever need threads). The test
  drives `page.evaluate` to invoke a global hook on the fixture.

`Ultrathink #5` ("not jsdom") is satisfied: Playwright launches a real
Chromium binary; jsdom is never in the path.

## Consequences

### Positive

- One codebase covers Node / browser / future Worker contexts.
- The C++ codec internals can evolve freely behind the WASM frontier.
  TS bindings only need to change when the C ABI changes (which is
  itself gated by ADR-006's symbol-baseline diff).
- Zero npm runtime deps means consumers don't drag in transitive
  problems; package install is fast.
- TransformStream adapters slot cleanly into modern WebCodecs
  pipelines.

### Negative

- No off-thread async in v1. A future Worker-hosted variant is
  possible but adds a postMessage marshalling layer.
- Bundle size depends on emcc's exception-lowering implementation;
  swapping to `-fwasm-exceptions` (when stable across browsers) could
  shrink it by ~30–80 KiB.

### Deferred

- **`-pthread` + `SharedArrayBuffer`** for parallel encode. Requires
  COOP/COEP headers on every consuming page; not friendly for
  CDN-distributed bundles.
- **Streaming MP4/H.264 SEI wrap.** v1 produces the SEI body; wrapping
  in a NAL is the muxer prompt's job.
- **Auto-published `.d.ts` for the `scc.js` Emscripten output.** Today
  we declare a minimal `SCCModule` interface in `raw.ts`. A future
  enhancement would parse emcc's `.symbols` output to generate the
  full type from the C ABI.

## Verification

- `tests/scc.node.test.ts` — round-trip + profile + close-idempotent +
  malformed-SEI rejection (Vitest, Node 20+).
- `tests/scc.memory.test.ts` — 1000-cycle heap stability, drift bounded
  to ±2 MiB.
- `tests-browser/scc.browser.test.ts` — Playwright real-Chromium round
  trip. [Ultrathink #5]
- `emscripten/check-size.sh` — bundle gzip ≤ 800 KiB. [Ultrathink #3]
- `tsconfig.json` `strict: true` + `declaration: true` ensures complete
  `.d.ts` emission for IntelliSense. [Ultrathink #4]

The full build + test verification requires Emscripten + Node 20+ +
Chromium on the host; the on-disk source is committed and ready to be
exercised by a CI workflow that has those tools.

## References

- AI Build Prompt #7 (`docs/AI_Build_Prompts.md` §7).
- ADR-006 (C ABI) — the surface this binding wraps.
- Internal: `docs/SAD.md` §6.1.7 (SDK bindings); REQ-027 in
  `docs/Acceptance_Criteria.md`.
