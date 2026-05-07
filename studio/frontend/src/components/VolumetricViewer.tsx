// src/components/VolumetricViewer.tsx
//
// React wrapper around the WebGPU Viewer in src/wgpu/viewer.ts.
//
// Responsibilities:
//   - Detect WebGPU and render <FallbackMessage /> when absent
//   - Mount the canvas on first render, dispose on unmount
//   - Subscribe to `metrics.latestFrame` and call viewer.uploadFrame
//   - Drive a requestAnimationFrame render loop
//   - Surface initialisation errors via accessible status text
//
// Design choice (Ultrathink #2): WebGPU absence is a normal user state,
// not an exception. The fallback is a card explaining what's needed,
// not a stack trace.

import React, { useEffect, useRef, useState } from 'react';

import overlay from '../styles/canvas-overlay.module.css';
import { useStore, selectLatestFrame } from '../store';
import { Viewer } from '../wgpu/viewer';

type Status =
    | { kind: 'init' }
    | { kind: 'ready' }
    | { kind: 'unsupported' }
    | { kind: 'error'; message: string };

function FallbackMessage({ heading, body }: { heading: string; body: string }): JSX.Element {
    return (
        <div className={overlay.fallback} role="status" aria-live="polite">
            <h3 className="font-semibold text-ink dark:text-ink-inverse mb-1">{heading}</h3>
            <p className="text-sm text-ink-muted max-w-sm">{body}</p>
        </div>
    );
}

export default function VolumetricViewer(): JSX.Element {
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const viewerRef = useRef<Viewer | null>(null);
    const rafRef = useRef<number | null>(null);
    const [status, setStatus] = useState<Status>({ kind: 'init' });
    const latestFrame = useStore(selectLatestFrame);

    // Mount: create the viewer once.
    useEffect(() => {
        let cancelled = false;
        const canvas = canvasRef.current;
        if (!canvas) return;

        if (typeof navigator === 'undefined' || !('gpu' in navigator) || !navigator.gpu) {
            setStatus({ kind: 'unsupported' });
            return;
        }

        // Resize-aware backing-store sizing for HiDPI.
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        const rect = canvas.getBoundingClientRect();
        canvas.width = Math.max(1, Math.floor(rect.width * dpr));
        canvas.height = Math.max(1, Math.floor(rect.height * dpr));

        Viewer.create(canvas, {
            onError: (e) => setStatus({ kind: 'error', message: e.message }),
            onDeviceLost: (info) =>
                setStatus({ kind: 'error', message: `GPU device lost: ${info.message ?? '(no detail)'}` }),
        })
            .then((v) => {
                if (cancelled) {
                    v.dispose();
                    return;
                }
                viewerRef.current = v;
                setStatus({ kind: 'ready' });
                const tick = () => {
                    viewerRef.current?.render();
                    rafRef.current = requestAnimationFrame(tick);
                };
                rafRef.current = requestAnimationFrame(tick);
            })
            .catch((e: unknown) => {
                if (cancelled) return;
                const msg = e instanceof Error ? e.message : String(e);
                setStatus({ kind: 'error', message: msg });
            });

        return () => {
            cancelled = true;
            if (rafRef.current !== null) {
                cancelAnimationFrame(rafRef.current);
                rafRef.current = null;
            }
            viewerRef.current?.dispose();
            viewerRef.current = null;
        };
    }, []);

    // Push every new depth frame into the viewer.
    useEffect(() => {
        if (!latestFrame || !viewerRef.current) return;
        viewerRef.current.uploadFrame(latestFrame);
    }, [latestFrame]);

    return (
        <div className={overlay.viewport} aria-label="Volumetric depth viewer">
            <canvas ref={canvasRef} className={overlay.canvas} />
            {status.kind === 'unsupported' && (
                <FallbackMessage
                    heading="WebGPU not available"
                    body="This browser does not expose a WebGPU adapter. Use Chrome 113+, Edge 113+, or enable the WebGPU flag in Firefox Nightly to view live depth frames."
                />
            )}
            {status.kind === 'error' && (
                <FallbackMessage
                    heading="Viewer error"
                    body={status.message}
                />
            )}
            {status.kind === 'init' && (
                <FallbackMessage
                    heading="Initialising&hellip;"
                    body="Requesting a GPU adapter."
                />
            )}
        </div>
    );
}
