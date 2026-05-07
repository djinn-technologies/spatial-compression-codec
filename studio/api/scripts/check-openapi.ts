// scripts/check-openapi.ts
//
// CI gate: ensures the committed openapi.json snapshot matches what
// the live Fastify route registry emits. Run via:
//
//     npm run check:openapi
//
// Fails (non-zero exit) on any drift. To accept a deliberate change,
// run with --update to overwrite the snapshot.  [Ultrathink #2]

import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { buildApp } from '../src/index.js';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const SNAPSHOT_PATH = resolve(__dirname, '..', 'openapi.json');
const UPDATE = process.argv.includes('--update');

async function main(): Promise<void> {
    const app = await buildApp();
    await app.ready();

    const r = await app.inject({ method: 'GET', url: '/api/openapi.json' });
    if (r.statusCode !== 200) {
        console.error('failed to fetch /api/openapi.json:', r.statusCode);
        process.exit(2);
    }
    const live = JSON.stringify(r.json(), null, 2) + '\n';

    await app.close();

    if (UPDATE) {
        writeFileSync(SNAPSHOT_PATH, live);
        console.log('openapi.json updated.');
        return;
    }

    let snapshot: string;
    try {
        snapshot = readFileSync(SNAPSHOT_PATH, 'utf8');
    } catch {
        console.error(
            'openapi.json missing -- run `npm run check:openapi -- --update` ' +
                'to create it.',
        );
        process.exit(2);
    }

    if (live !== snapshot) {
        console.error(
            'OpenAPI drift detected. Diff:\n' +
                'expected (committed) vs got (live)\n' +
                '----- COMMITTED -----\n' +
                snapshot.slice(0, 800) +
                '\n----- LIVE -----\n' +
                live.slice(0, 800) +
                '\n\nIf the change is intentional, run:\n' +
                '    npm run check:openapi -- --update\n' +
                'and commit the updated openapi.json.',
        );
        process.exit(1);
    }
    console.log('openapi.json: OK');
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
