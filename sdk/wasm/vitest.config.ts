// vitest.config.ts
import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        // The Emscripten-generated scc.js does dynamic-import-of-WASM at
        // load time; jsdom is not exercised here so we use Node.
        environment: 'node',
        include: ['tests/**/*.test.ts'],
        // Allow longer timeouts for the 1000-cycle memory test.
        testTimeout: 60_000,
        hookTimeout: 30_000,
    },
});
