// tests/AnalyticsPanel.test.tsx

import { describe, it, expect } from 'vitest';
import React from 'react';
import { render, screen } from '@testing-library/react';

import AnalyticsPanel from '../src/components/AnalyticsPanel';
import { useStore } from '../src/store';

describe('AnalyticsPanel', () => {
    it('renders placeholder dashes when no samples', () => {
        render(<AnalyticsPanel />);
        // FPS / Mb/s / Encode ms each render '–' before any data.
        expect(screen.getAllByText('–').length).toBe(3);
    });

    it('reflects the latest sample after a push', () => {
        useStore.getState().pushMetricSample({
            timestamp: 1,
            fps: 29.5,
            bitrateMbps: 4.2,
            encodeMs: 0.91,
            decodeMs: 0.6,
            payloadBytes: 1234,
            profile: 'lossy_high',
        });
        render(<AnalyticsPanel />);
        expect(screen.getByText('29.5')).toBeInTheDocument();
        expect(screen.getByText('4.20')).toBeInTheDocument();
        expect(screen.getByText('0.91')).toBeInTheDocument();
    });

    it('toggles streaming badge based on wsConnected', () => {
        useStore.getState().setWsConnected(true);
        render(<AnalyticsPanel />);
        expect(screen.getByText('streaming')).toBeInTheDocument();
    });
});
