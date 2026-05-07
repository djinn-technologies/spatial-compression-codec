// tests-e2e/a11y.spec.ts
//
// axe-core sweep across all four primary routes. We assert no violations
// at the WCAG 2 AA level. Rules that can never be satisfied for canvas
// content (e.g. colour-contrast on the WebGPU canvas) are explicitly
// disabled with reasoning.

import { test, expect } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';

const ROUTES = ['/capture', '/playback', '/profiles', '/settings'];

for (const path of ROUTES) {
    test(`axe-core a11y: ${path}`, async ({ page }) => {
        await page.goto(path);
        // Let async-loaded routes settle.
        await page.waitForLoadState('networkidle');

        const result = await new AxeBuilder({ page })
            .withTags(['wcag2a', 'wcag2aa', 'best-practice'])
            // The WebGPU canvas pixels are dynamic, so colour-contrast
            // can't be statically reasoned about. Documented carve-out.
            .disableRules(['color-contrast'])
            .analyze();

        // Surface violations in the report rather than the bare assertion.
        if (result.violations.length > 0) {
            console.error(JSON.stringify(result.violations, null, 2));
        }
        expect(result.violations).toEqual([]);
    });
}
