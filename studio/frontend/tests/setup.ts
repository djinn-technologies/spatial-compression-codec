// tests/setup.ts
//
// Global Vitest setup. We extend `expect` with @testing-library/jest-dom
// matchers and reset the Zustand store between tests so per-file state
// never bleeds. WebGPU is not available under jsdom, so the
// VolumetricViewer test only exercises the fallback branch.

import '@testing-library/jest-dom/vitest';
import { afterEach, beforeEach, vi } from 'vitest';
import { cleanup } from '@testing-library/react';

import { useStore } from '../src/store';

// Snapshot the initial state once so we can restore it between tests.
const INITIAL_STATE = useStore.getState();

beforeEach(() => {
    // Reset to fully-fresh slice values without losing the action
    // bindings (those live on the same state object).
    useStore.setState(INITIAL_STATE, true);
    // jsdom doesn't ship matchMedia; many components defensively call it.
    if (!window.matchMedia) {
        window.matchMedia = vi.fn().mockImplementation((query: string) => ({
            matches: false,
            media: query,
            onchange: null,
            addListener: vi.fn(),
            removeListener: vi.fn(),
            addEventListener: vi.fn(),
            removeEventListener: vi.fn(),
            dispatchEvent: vi.fn(),
        }));
    }
});

afterEach(() => {
    cleanup();
});
