#!/usr/bin/env node
// npm/bin/scc-studio-agent.js
//
// Cross-platform launcher for the scc-studio-agent native binary. The
// npm package ships per-platform binaries under binaries/<os>-<arch>/;
// this shim picks the right one for the host and execs it, forwarding
// argv and the entire env.
//
// Platforms covered:
//   binaries/linux-x64/scc-studio-agent       (musl static)
//   binaries/linux-arm64/scc-studio-agent     (musl static)
//   binaries/darwin-x64/scc-studio-agent       (signed + notarised)
//   binaries/darwin-arm64/scc-studio-agent     (signed + notarised)
//   binaries/win32-x64/scc-studio-agent.exe    (signed Authenticode)
//
// [REQ-029, ADR-011]

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, '..');

function platformDir() {
    const map = {
        'linux:x64': 'linux-x64',
        'linux:arm64': 'linux-arm64',
        'darwin:x64': 'darwin-x64',
        'darwin:arm64': 'darwin-arm64',
        'win32:x64': 'win32-x64',
    };
    const key = `${process.platform}:${process.arch}`;
    const dir = map[key];
    if (!dir) {
        console.error(
            `scc-studio-agent: unsupported platform ${key}; ` +
                `supported: ${Object.keys(map).join(', ')}`
        );
        process.exit(2);
    }
    return dir;
}

function binaryPath() {
    const exe = process.platform === 'win32'
        ? 'scc-studio-agent.exe'
        : 'scc-studio-agent';
    const path = join(root, 'binaries', platformDir(), exe);
    if (!existsSync(path)) {
        console.error(
            `scc-studio-agent: binary missing at ${path}\n` +
                `Either the npm package is corrupt or the postinstall ` +
                `step did not run. Try \`npm install --force\` or check ` +
                `the package's @djinn/scc-studio-agent issue tracker.`
        );
        process.exit(3);
    }
    return path;
}

const child = spawn(binaryPath(), process.argv.slice(2), {
    stdio: 'inherit',
    windowsHide: false,
});

child.on('exit', (code, signal) => {
    if (signal) {
        process.kill(process.pid, signal);
    } else {
        process.exit(code ?? 0);
    }
});

child.on('error', (err) => {
    console.error('scc-studio-agent: failed to spawn:', err);
    process.exit(127);
});
