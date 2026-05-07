# ADR-013 — React frontend (`studio/frontend`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-030]`, `[ADR-011]`, `[ADR-012]`                              |

## Context

The Studio control-plane API (ADR-012) and capture agent (ADR-011) need
a first-party UI: sensor selection, live encoder telemetry, recorded-
session playback, and an editable profile / encoder-settings surface.
The same UI is the on-ramp for a future tenant-admin console, so the
choices here outlive the v1 scope.

Decisions to lock:

1. **Framework, router, state**.
2. **Styling system** and brand-token definition.
3. **3D rendering API** for live volumetric data + fallback policy.
4. **WebSocket adapter** lifecycle and back-pressure.
5. **Bundle-size budget** + how it's enforced.
6. **Accessibility** baseline.
7. **Test boundary** — what runs in jsdom vs a real browser.

## Decision

### 1. React 18 + react-router 6 (lazy routes) + Zustand 4 (sliced).

React-with-hooks is the lowest-risk choice for a small team; it has
the deepest pool of contributors and the most maturity around testing.
react-router 6 with `lazy()` gives us per-route code-splitting at the
declaration site, which slots cleanly into the bundle-size gate
(decision 5).

State lives in Zustand with the **sliced-store pattern**: each domain
(`auth`, `sensors`, `session`, `metrics`, `settings`) declares a
`StateCreator<Slice>` and the root composes them by spread. This gives
us:

- Independent unit tests per slice (just call the action against a
  fresh store).
- No provider tree — `useStore(selector)` is enough.
- Clear write paths: every action returns the partial new state, so
  effects are explicit.

Redux Toolkit was rejected because the boilerplate cost is not worth
it at this scope, and Jotai was rejected because the metrics ring
buffer benefits from a single subscription rather than per-atom
subscriptions.

### 2. Tailwind 3.4 + brand tokens in `theme.extend.colors.brand.*`.

Tailwind keeps the styling colocated with markup (no orphaned CSS to
maintain) and the `theme.extend` API is the right place to land brand
tokens. `tailwind.config.js` defines:

- `brand.primary` (`#0B4884`) and `brand.accent` (`#1168BD`) plus
  hover / active variants;
- semantic surfaces (`surface.light` / `surface` / `surface.dark`) and
  ink colours (`ink` / `ink.muted` / `ink.inverse`);
- `font-family: Inter, Calibri, system-ui, …` (web-safe fallback chain).

Dark mode is `darkMode: 'class'`. The class is applied **before first
paint** by a small inline script in `main.tsx` so users never see a
flash of light theme.

### 3. WebGPU only in v1; explicit fallback card when absent.

The viewer is a vertex-shader pinhole reprojection: each `vertex_index`
is a `(u, v)` raster cell; the depth value is read from a packed-u16
storage buffer; the back-projection (`x = (u-cx)*z/fx`) happens in
WGSL; topology is `point-list`. No compute pass, no per-vertex CPU
upload of position data — the only per-frame upload is the depth
buffer itself.

Defensive boilerplate (mandatory):

- `'gpu' in navigator` check before `requestAdapter`;
- `device.lost` promise wired to a state setter;
- every `uploadFrame` / `render` is bracketed by
  `pushErrorScope('validation')` / `popErrorScope`, with errors
  surfaced through an `onError` callback (never thrown out of an RAF).

When WebGPU is unavailable (Firefox stable today, Safari pre-17.4,
older Linux Mesa with no Vulkan), the route renders a `<FallbackMessage />`
card explaining the requirement. **A WebGL2 fallback is intentionally
deferred to v1.1** — duplicating the renderer would more than double
the viewer's surface area, and the user population that can run the
agent (which needs a depth-camera SDK) is highly correlated with
WebGPU-capable hardware.

### 4. WebSocket adapter with exponential-backoff reconnect.

`MetricsSocket` (in `src/api/ws.ts`) handles two interleaved message
kinds on one socket:

- text envelopes (`{kind: "metric", …}`) parsed as `MetricSample`;
- binary frames (preceded by a text `{kind: "frame", w, h, bd, ts}`
  header) decoded as `Uint16Array` and pushed into `metrics.latestFrame`.

Auth rides on the WebSocket subprotocol (`['scc.v1', 'bearer.<token>']`)
because browsers cannot set `Authorization` on the upgrade. The server
reads `protocol[1]` and rejects with close-code 4401 on failure;
**4401 is not retried** since it is a credential issue, not a transient
fault. All other close codes trigger reconnect with backoff
`1s → 2s → 4s → … → 30s max`.

Back-pressure: the API's `MAX_LIVE_QUEUE` (16, per ADR-012) bounds
server-side buffering. The client never queues frames — it copies the
incoming `ArrayBuffer` into the latest-frame slot, replacing whatever
was there. The viewer reads from the slot at its own RAF rate, so
slower-than-stream rendering is non-fatal.

### 5. size-limit gate at 350 KB initial / 80 KB per route.

`size-limit` runs as a CI step (`npm run size`). The vite config's
`chunkFileNames` names route chunks `route-Capture-[hash].js` etc. so
the size-limit glob `dist/assets/route-*.js` matches deterministically.
Dependency choices (Zustand instead of Redux, `gl-matrix` instead of
three.js, hand-rolled SVG sparkline instead of recharts) are all
sized against this budget.

### 6. Accessibility: jsx-a11y + axe-core in the e2e suite.

ESLint's `jsx-a11y` plugin runs in `npm run lint`. Playwright e2e
tests sweep all four routes through `@axe-core/playwright` and fail
on any WCAG 2 AA violation. Carve-outs (documented in
`tests-e2e/a11y.spec.ts`):

- `color-contrast` is disabled because the canvas pixels are dynamic
  and cannot be statically reasoned about.

Manual practices baked into the markup: a skip-link as the first
focusable; visible-focus styling via `:focus-visible` ring; semantic
landmarks (`header` / `nav[aria-label="Primary"]` / `main` / `footer`);
status / alert live regions for async loading and error paths.

### 7. Two test boundaries: Vitest (jsdom) + Playwright (Chromium).

| Concern                              | Where             |
| ------------------------------------ | ----------------- |
| Store algebra (slices, selectors)    | Vitest            |
| Component rendering / events         | Vitest + RTL      |
| API client error surfacing           | Vitest (mocked fetch) |
| WebGPU absence → fallback            | Vitest            |
| WebGPU happy path                    | Playwright        |
| Route navigation + skip-link         | Playwright        |
| axe-core sweep                       | Playwright        |

jsdom does not implement WebGPU (and probably never will), so the
"WebGPU works" assertion can only meaningfully run in a real Chromium
launched with `--enable-unsafe-webgpu`.

## Consequences

**Positive**

- The whole frontend is one Vite project with one config; new
  contributors can boot in minutes.
- Per-slice testing makes state regressions easy to bisect.
- WebGPU-first-with-fallback keeps v1 small while leaving a clear
  upgrade path to a WebGL2 renderer in v1.1.
- size-limit and axe-core in CI give us a published bundle ceiling and
  a published a11y floor that the next prompt cannot quietly erode.

**Negative**

- The viewer must keep its dependencies at exactly `gl-matrix` for
  matrix math; pulling in three.js or babylon would break the bundle
  budget.
- Browsers without WebGPU (Firefox stable, older Safari) see a static
  card, not a degraded but functional viewer. v1.1 should add WebGL2.
- Subprotocol-based WS auth is non-obvious. Documented in
  `src/api/ws.ts` and in this ADR.

## Verification

The frontend's verification matrix (per the prompt's acceptance block,
all deferred to CI since this shell has no Node 20):

- [ ] `npm run typecheck` passes under `--strict`
- [ ] `npm run lint` passes (jsx-a11y + react-hooks)
- [ ] `npm run test` — Vitest: store + component coverage
- [ ] `npm run build` produces `dist/`
- [ ] `npm run size` — initial < 350 KB gzip; routes < 80 KB gzip
- [ ] `npm run test:e2e` — Playwright smoke + a11y on Chromium
      (with `--enable-unsafe-webgpu`)
- [ ] Manual review: dark/light theme, keyboard navigation, screen
      reader landmarks (NVDA / VoiceOver)
