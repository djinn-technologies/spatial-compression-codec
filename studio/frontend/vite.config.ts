import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

export default defineConfig({
    plugins: [react()],
    server: {
        port: 5173,
        proxy: {
            // Forward API + WS to the control plane during dev.
            '/api': {
                target: 'http://localhost:8080',
                ws: true,
                changeOrigin: true,
            },
        },
    },
    build: {
        target: 'es2022',
        sourcemap: true,
        rollupOptions: {
            output: {
                // Stable entry name so size-limit's pattern keeps matching.
                entryFileNames: 'assets/index-[hash].js',
                chunkFileNames: ({ name }) => {
                    if (name.startsWith('Capture') || name.startsWith('Playback') ||
                        name.startsWith('Profiles') || name.startsWith('Settings')) {
                        return `assets/route-${name}-[hash].js`;
                    }
                    return 'assets/chunk-[name]-[hash].js';
                },
                assetFileNames: 'assets/[name]-[hash][extname]',
            },
        },
    },
    test: {
        environment: 'jsdom',
        setupFiles: ['./tests/setup.ts'],
        include: ['tests/**/*.test.{ts,tsx}'],
    },
    assetsInclude: ['**/*.wgsl'],
});
