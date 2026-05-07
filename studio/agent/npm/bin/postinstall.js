#!/usr/bin/env node
// npm/bin/postinstall.js
//
// Best-effort: chmod +x on the platform-matching binary so users can
// run it directly. On Windows this is a no-op.

import { chmodSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, '..');

const map = {
    'linux:x64':  ['linux-x64',  'scc-studio-agent'],
    'linux:arm64':['linux-arm64','scc-studio-agent'],
    'darwin:x64': ['darwin-x64', 'scc-studio-agent'],
    'darwin:arm64':['darwin-arm64','scc-studio-agent'],
    'win32:x64':  ['win32-x64',  'scc-studio-agent.exe'],
};

const entry = map[`${process.platform}:${process.arch}`];
if (!entry) process.exit(0);

const [dir, exe] = entry;
const path = join(root, 'binaries', dir, exe);
if (!existsSync(path)) process.exit(0);

if (process.platform !== 'win32') {
    try {
        chmodSync(path, 0o755);
    } catch {
        /* ignore */
    }
}
