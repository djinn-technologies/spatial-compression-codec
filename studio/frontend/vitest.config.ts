/// <reference types="vitest" />
import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

export default defineConfig({
    plugins: [react()],
    test: {
        environment: 'jsdom',
        globals: false,
        include: ['tests/**/*.test.ts', 'tests/**/*.test.tsx'],
        setupFiles: ['./tests/setup.ts'],
        css: false,
        // The frontend tests are pure unit tests (store + components);
        // there are no shared resources to serialise on, so let Vitest
        // parallelise files freely.
    },
    resolve: {
        alias: {
            // Allow ?raw imports of .wgsl in tests by stubbing them.
            // The viewer module is imported from tests/wgpu only; the
            // store/component tests don't pull WGSL.
        },
    },
});
