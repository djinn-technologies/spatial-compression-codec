// tests-browser/scc.browser.test.ts
//
// Real-Chromium round-trip [Ultrathink #5]. The fixture page.html
// imports ../dist/index.js (which itself dynamic-imports ../dist/scc.js
// + the WASM binary) and exposes a global __sccTest entry point.
//
// We serve the fixture via a tiny in-process HTTP server so file://
// CORS doesn't bite us, then drive Playwright's `page.evaluate` to
// invoke the global.

import { test, expect } from '@playwright/test';
import { createServer, type Server } from 'node:http';
import { readFile } from 'node:fs/promises';
import { join, resolve as pathResolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const PKG_DIR = pathResolve(__dirname, '..');

let server: Server;
let baseURL: string;

const MIME: Record<string, string> = {
    '.html': 'text/html; charset=utf-8',
    '.js':   'application/javascript; charset=utf-8',
    '.mjs':  'application/javascript; charset=utf-8',
    '.wasm': 'application/wasm',
    '.map':  'application/json',
};

test.beforeAll(async () => {
    // Serve the package directory so /tests-browser/page.html and
    // /dist/scc.js / scc.wasm resolve correctly via relative imports.
    server = createServer(async (req, res) => {
        try {
            const url = new URL(req.url ?? '/', 'http://localhost');
            const safe = url.pathname.replace(/\.\.+/g, '');
            const filePath = join(PKG_DIR, safe);
            const buf = await readFile(filePath);
            const ext = '.' + (filePath.split('.').pop() ?? '');
            res.writeHead(200, {
                'Content-Type': MIME[ext] ?? 'application/octet-stream',
                'Cross-Origin-Embedder-Policy': 'require-corp',
                'Cross-Origin-Opener-Policy': 'same-origin',
            });
            res.end(buf);
        } catch {
            res.writeHead(404);
            res.end('not found');
        }
    });
    await new Promise<void>((r) => server.listen(0, '127.0.0.1', r));
    const addr = server.address();
    if (typeof addr !== 'object' || !addr) throw new Error('no address');
    baseURL = `http://127.0.0.1:${addr.port}`;
});

test.afterAll(async () => {
    await new Promise<void>((r, rj) =>
        server.close((err) => (err ? rj(err) : r()))
    );
});

test('SCC round-trip in real Chromium (Ultrathink #5)', async ({ page }) => {
    page.on('console', (msg) => console.log('[browser]', msg.text()));
    page.on('pageerror', (err) => console.error('[pageerror]', err));
    await page.goto(`${baseURL}/tests-browser/page.html`);
    await expect(page.locator('#status')).toHaveText(/ready/, { timeout: 30_000 });
    const result = await page.evaluate(
        async (args) => (globalThis as any).__sccTest(args),
        { width: 16, height: 16, seed: 0xC0FFEE }
    );
    expect(result.ok).toBe(true);
    expect(result.width).toBe(16);
    expect(result.height).toBe(16);
    expect(result.seiBytes).toBeGreaterThan(0);
});
