import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
    testDir: './tests-e2e',
    timeout: 60_000,
    expect: { timeout: 10_000 },
    fullyParallel: true,
    forbidOnly: !!process.env.CI,
    retries: process.env.CI ? 2 : 0,
    workers: process.env.CI ? 1 : undefined,
    reporter: process.env.CI ? [['github'], ['list']] : 'list',
    use: {
        baseURL: 'http://localhost:4173',
        trace: 'retain-on-failure',
        screenshot: 'only-on-failure',
        video: 'retain-on-failure',
    },
    projects: [
        // Chromium with WebGPU enabled (channel: chromium ships with the
        // adapter compiled in; --enable-unsafe-webgpu lights it up under
        // headless).
        {
            name: 'chromium-webgpu',
            use: {
                ...devices['Desktop Chrome'],
                launchOptions: {
                    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan'],
                },
            },
        },
    ],
    webServer: {
        command: 'npm run build && npm run preview -- --port 4173 --strictPort',
        url: 'http://localhost:4173',
        reuseExistingServer: !process.env.CI,
        timeout: 120_000,
    },
});
