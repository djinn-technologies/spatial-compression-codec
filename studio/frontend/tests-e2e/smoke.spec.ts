// tests-e2e/smoke.spec.ts
//
// Boots the built frontend behind `vite preview`, navigates the four
// primary routes, and asserts:
//   1. The shell renders (header, primary nav, skip link)
//   2. Each route reaches its main heading
//   3. The Capture route either initialises WebGPU or shows the
//      well-formed fallback (jsdom can't tell us this; only a real
//      headed/headless Chromium can)

import { test, expect } from '@playwright/test';

test.describe('frontend smoke', () => {
    test('shell loads and skip-link is the first focusable', async ({ page }) => {
        await page.goto('/');
        await expect(page).toHaveURL(/\/capture$/);

        await expect(page.getByText('SCC Studio')).toBeVisible();
        await expect(page.getByRole('navigation', { name: 'Primary' })).toBeVisible();

        // Skip link is rendered first in tab order.
        await page.keyboard.press('Tab');
        const focusedText = await page.evaluate(() =>
            document.activeElement?.textContent?.trim() ?? '',
        );
        expect(focusedText).toBe('Skip to main content');
    });

    test('navigates between all four routes', async ({ page }) => {
        await page.goto('/');
        const routes: Array<[string, RegExp]> = [
            ['Playback', /Recorded sessions/],
            ['Profiles', /Profiles/],
            ['Settings', /Interface/],
            ['Capture',  /Capture/],
        ];
        for (const [label, heading] of routes) {
            await page.getByRole('link', { name: label }).click();
            await expect(page.getByRole('heading', { name: heading }).first()).toBeVisible();
        }
    });

    test('viewer initialises or falls back gracefully', async ({ page }) => {
        await page.goto('/capture');
        // Either the canvas has content (size > 0) or the fallback is visible.
        const region = page.getByLabel('Volumetric depth viewer');
        await expect(region).toBeVisible();
        // Wait a moment for adapter init.
        await page.waitForTimeout(750);
        const fallbackVisible = await page
            .getByRole('status')
            .filter({ hasText: /WebGPU not available|Initialising|Viewer error/i })
            .first()
            .isVisible()
            .catch(() => false);
        const canvasVisible = await page.locator('canvas').first().isVisible();
        // At least one branch must hold.
        expect(fallbackVisible || canvasVisible).toBe(true);
    });
});
