// tests/VolumetricViewer.test.tsx
//
// jsdom does not implement WebGPU, so this test verifies the
// "WebGPU absent -> show fallback" branch end-to-end. The "happy path"
// is covered by the Playwright e2e suite where a real Chromium
// instance has the GPU available.

import { describe, it, expect } from 'vitest';
import React from 'react';
import { render, screen } from '@testing-library/react';

import VolumetricViewer from '../src/components/VolumetricViewer';

describe('VolumetricViewer (no WebGPU)', () => {
    it('renders the fallback when navigator.gpu is missing', () => {
        // jsdom's navigator has no `gpu` key by default.
        render(<VolumetricViewer />);
        expect(screen.getByText(/WebGPU not available/i)).toBeInTheDocument();
    });
});
