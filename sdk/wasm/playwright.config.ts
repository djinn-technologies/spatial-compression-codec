// playwright.config.ts
//
// Real-Chromium browser test harness. [Ultrathink #5 -- not jsdom.]
//
// `npm run test:browser` builds the WASM bundle, serves it from
// tests-browser/page.html, and runs the browser-side spec under
// Chromium 113+. Headless by default; pass `--headed` for debugging.

import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
    testDir: './tests-browser',
    timeout: 60_000,
    fullyParallel: false,                 // serialise so the WASM bundle is hot
    reporter: [['list']],
    use: {
        // Tests serve their own files via Playwright's `page.goto('file://...')`
        // or via a tiny http server set up in beforeAll. No external server.
        ignoreHTTPSErrors: true,
        screenshot: 'only-on-failure',
    },
    projects: [
        {
            name: 'chromium-113-plus',
            use: { ...devices['Desktop Chrome'] },
        },
    ],
});
