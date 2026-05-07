# SCC Studio Frontend

React 18 + WebGPU + Zustand control plane for the Spatial Compression
Codec. Talks to the Studio API (`studio/api`) and the capture agent
(`studio/agent`) over HTTP and WebSocket.

## Stack

| Layer            | Choice                                                 |
| ---------------- | ------------------------------------------------------ |
| Build            | Vite 5 + TypeScript 5 strict                           |
| UI               | React 18 + react-router 6 (lazy routes)                |
| State            | Zustand 4 (sliced store + persist for UI prefs only)   |
| Styling          | Tailwind 3.4 (brand-token theme + dark mode via class) |
| 3D               | WebGPU via vertex-shader pinhole reprojection          |
| Tests (unit)     | Vitest + @testing-library/react (jsdom)                |
| Tests (e2e/a11y) | Playwright + @axe-core/playwright                      |
| Bundle gate      | size-limit (350 KB initial / 80 KB per route, gzip)    |

## Running locally

The frontend assumes the Studio API is reachable at `http://localhost:8080`
(the dev server proxies `/api` and `/api/*/stream` WebSockets).

```powershell
# From studio/frontend
npm install
npm run dev          # starts Vite dev server on :5173
```

## Scripts

| Script              | Purpose                                  |
| ------------------- | ---------------------------------------- |
| `npm run dev`       | Vite dev server with HMR                 |
| `npm run build`     | `tsc --noEmit` + production bundle       |
| `npm run preview`   | Serve the production build               |
| `npm run typecheck` | TypeScript strict-mode check only        |
| `npm run lint`      | ESLint with jsx-a11y + react-hooks rules |
| `npm run test`      | Vitest unit tests (jsdom)                |
| `npm run test:e2e`  | Playwright + axe-core sweep              |
| `npm run size`      | size-limit bundle gate                   |

## Layout

```
studio/frontend/
├── index.html
├── src/
│   ├── main.tsx              # entry: theme bootstrap + createRoot
│   ├── App.tsx               # router + shell (header / nav / footer)
│   ├── api/
│   │   ├── client.ts         # typed fetch wrapper + ApiError
│   │   └── ws.ts             # MetricsSocket (live encoder telemetry)
│   ├── store/
│   │   ├── auth.ts           # token + principal
│   │   ├── sensors.ts        # sensor list + selection
│   │   ├── session.ts        # active capture session state machine
│   │   ├── metrics.ts        # ring buffer + latest depth frame
│   │   ├── settings.ts       # encoder + UI prefs
│   │   └── index.ts          # composed root + persist middleware
│   ├── routes/               # lazy-loaded route bundles
│   │   ├── Capture.tsx
│   │   ├── Playback.tsx
│   │   ├── Profiles.tsx
│   │   └── Settings.tsx
│   ├── components/
│   │   ├── VolumetricViewer.tsx
│   │   ├── CapturePanel.tsx
│   │   ├── PlaybackPanel.tsx
│   │   ├── AnalyticsPanel.tsx
│   │   └── EncoderSettings.tsx
│   ├── wgpu/
│   │   ├── viewer.ts         # WebGPU lifecycle (create / upload / render / dispose)
│   │   └── shaders/{vertex,fragment}.wgsl
│   └── styles/
│       ├── globals.css       # Tailwind layers + brand component classes
│       └── canvas-overlay.module.css
├── tests/                    # Vitest unit + component tests
├── tests-e2e/                # Playwright + axe-core
└── playwright.config.ts
```

## WebGPU + fallback

WebGPU is detected at viewer-mount time. If `'gpu' in navigator` is
false (Firefox stable, Safari pre-17.4), or `requestAdapter` returns
null (older Linux Mesa), the route renders a `<FallbackMessage />`
card explaining the requirement instead of a broken canvas. A WebGL2
renderer is intentionally deferred to v1.1 so the v1 surface stays
small and reviewable.

## Bundle budgets

`npm run size` runs as the bundle gate in CI:

- **350 KB gzip** for the initial bundle (`dist/assets/index-*.js`)
- **80 KB gzip** per lazy route chunk (`dist/assets/route-*.js`)

The vite config names route chunks `route-Capture-…`, `route-Playback-…`
etc. so the size-limit glob matches predictably.

## Brand palette

Defined in `tailwind.config.js`:

- `brand.primary`   `#0B4884`
- `brand.accent`    `#1168BD`
- `surface` (light/default/dark) and `ink` (default/muted/inverse)

Components use the semantic tokens (`bg-surface`, `text-ink-muted`),
not raw hex literals. Dark mode is class-based; `main.tsx` applies the
class before first paint to avoid a flash of incorrect theme.

## Verification (deferred to CI)

This shell does not have Node 20 installed. Verification steps that
must pass on CI:

```bash
npm ci
npm run typecheck
npm run lint
npm run test
npm run build
npm run size
npx playwright install --with-deps chromium
npm run test:e2e
```

See `docs/adr/ADR-013-react-frontend.md` for the architectural
decisions behind the choices above.
