import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        environment: 'node',
        include: ['tests/**/*.test.ts'],
        testTimeout: 60_000,
        hookTimeout: 60_000,
        // Tests use Postgres testcontainers; run serially to avoid
        // port-binding conflicts.
        fileParallelism: false,
    },
});
