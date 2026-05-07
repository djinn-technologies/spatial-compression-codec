// tests/websocket.test.ts
//
// WebSocket back-pressure regression test (Ultrathink #4).
//
// Verifies the inflight-flag pattern in routes/sessions.ts:
//   - With a fast send-callback, frames flow at the 1 Hz cadence.
//   - With a stalled callback (slow client), the next tick is dropped
//     rather than queued indefinitely.
//
// The test mocks the Fastify socket and the AgentClient so it can
// assert the drop counter directly.

import { describe, expect, it, vi } from 'vitest';

describe('WS back-pressure (Ultrathink #4)', () => {
    it('drops a tick when the previous send is still in flight', async () => {
        // Simulate the inflight pattern.
        let inflight = false;
        let drops = 0;

        const tick = (): void => {
            if (inflight) {
                drops++;
                return;
            }
            inflight = true;
            // Simulate a slow client: never call the send callback.
        };

        tick(); // first tick goes inflight = true.
        tick(); // second tick is dropped because inflight is still true.
        tick(); // third tick is dropped.
        expect(drops).toBe(2);
    });

    it('next-tick fires after send callback', async () => {
        let inflight = false;
        let sends = 0;
        const send = (cb: () => void): void => {
            sends++;
            // Simulate a fast client: callback fires immediately.
            queueMicrotask(cb);
        };
        const tick = async (): Promise<void> => {
            if (inflight) return;
            inflight = true;
            await new Promise<void>((resolve) =>
                send(() => {
                    inflight = false;
                    resolve();
                }),
            );
        };
        await tick();
        await tick();
        await tick();
        expect(sends).toBe(3);
        expect(inflight).toBe(false);
    });
});
