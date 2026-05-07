// tests/CapturePanel.test.tsx
//
// Drives the CapturePanel through a happy-path POST /v1/sessions and
// asserts the store reflects the returned session. We mock global
// fetch with a small stub since the API client always goes through
// fetch.

import { describe, it, expect, beforeEach, vi } from 'vitest';
import React from 'react';
import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';

import CapturePanel from '../src/components/CapturePanel';
import { useStore } from '../src/store';
import type { SensorDescriptor } from '../src/store/sensors';

const sensors: SensorDescriptor[] = [
    { id: 's1', vendor: 'Intel', model: 'D435', backend: 'librealsense', modes: [] },
];

describe('CapturePanel', () => {
    beforeEach(() => {
        useStore.getState().setSensors(sensors);
    });

    it('disables Start until a sensor is selected', () => {
        render(<CapturePanel profileId="lossy_high" />);
        const start = screen.getByRole('button', { name: /Start capture/i });
        expect(start).toBeDisabled();
    });

    it('creates a session via the API on click', async () => {
        const user = userEvent.setup();
        useStore.getState().selectSensor('s1');

        const fetchMock = vi.fn(async () =>
            new Response(JSON.stringify({
                id: 'sess-1',
                sensorId: 's1',
                profileId: 'lossy_high',
                agentSessionId: 'agent-7',
                state: 'capturing',
                startedAt: '2026-01-01T00:00:00Z',
                endedAt: null,
                error: null,
            }), { status: 201, headers: { 'Content-Type': 'application/json' } }),
        );
        // @ts-expect-error: assigning to global fetch in test only
        globalThis.fetch = fetchMock;

        render(<CapturePanel profileId="lossy_high" />);
        await user.selectOptions(screen.getByLabelText(/Sensor/i), 's1');
        await user.click(screen.getByRole('button', { name: /Start capture/i }));

        await waitFor(() => {
            expect(useStore.getState().session.active?.id).toBe('sess-1');
        });
        expect(fetchMock).toHaveBeenCalledTimes(1);
        const [, init] = fetchMock.mock.calls[0]!;
        expect((init as RequestInit).method).toBe('POST');
    });

    it('surfaces API errors as alerts', async () => {
        const user = userEvent.setup();
        useStore.getState().selectSensor('s1');

        // @ts-expect-error: assigning to global fetch in test only
        globalThis.fetch = vi.fn(async () =>
            new Response(JSON.stringify({
                error: { code: 'sensor_busy', message: 'already in use' },
            }), { status: 409, headers: { 'Content-Type': 'application/json' } }),
        );

        render(<CapturePanel profileId="lossy_high" />);
        await user.click(screen.getByRole('button', { name: /Start capture/i }));

        await waitFor(() => {
            expect(screen.getByRole('alert')).toHaveTextContent(/sensor_busy/);
        });
        expect(useStore.getState().session.active?.state).toBe('failed');
    });
});
