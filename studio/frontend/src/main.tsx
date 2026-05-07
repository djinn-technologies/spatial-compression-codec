// src/main.tsx
//
// React entry point. Wires the store, the router, and the global
// stylesheet. We deliberately do NOT use StrictMode in development
// because the WebGPU viewer's effect-mount-unmount-mount cycle is
// expensive enough to confuse first-frame timings; the e2e test grids
// catch regressions instead.

import React from 'react';
import { createRoot } from 'react-dom/client';

import './styles/globals.css';
import App from './App';

const rootEl = document.getElementById('root');
if (!rootEl) {
    throw new Error('#root element missing from index.html');
}

// Bootstrap the theme class before first paint to avoid a flash of
// the wrong theme when the user has chosen `dark` explicitly.
try {
    const persisted = localStorage.getItem('scc-studio-prefs/v1');
    if (persisted) {
        const parsed = JSON.parse(persisted) as { state?: { settings?: { ui?: { theme?: string } } } };
        const theme = parsed.state?.settings?.ui?.theme;
        if (theme === 'dark') document.documentElement.classList.add('dark');
        if (theme === 'system') {
            const prefers = window.matchMedia('(prefers-color-scheme: dark)').matches;
            if (prefers) document.documentElement.classList.add('dark');
        }
    } else if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
        document.documentElement.classList.add('dark');
    }
} catch {
    // Storage unavailable; ignore.
}

createRoot(rootEl).render(<App />);
