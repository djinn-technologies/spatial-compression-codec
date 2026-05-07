// src/App.tsx
//
// Top-level router and shell layout.
//
// All four routes are lazy-loaded so each becomes its own Vite chunk;
// the size-limit config in package.json gates each route at 80 KB
// gzip. The shell itself (header, nav, footer) is tiny and ships in
// the initial bundle.

import React, { Suspense, lazy } from 'react';
import { BrowserRouter, NavLink, Navigate, Route, Routes } from 'react-router-dom';

const Capture  = lazy(() => import('./routes/Capture'));
const Playback = lazy(() => import('./routes/Playback'));
const Profiles = lazy(() => import('./routes/Profiles'));
const Settings = lazy(() => import('./routes/Settings'));

const NAV: Array<{ path: string; label: string }> = [
    { path: '/capture',  label: 'Capture'  },
    { path: '/playback', label: 'Playback' },
    { path: '/profiles', label: 'Profiles' },
    { path: '/settings', label: 'Settings' },
];

function navClass({ isActive }: { isActive: boolean }): string {
    return [
        'px-4 py-2 rounded-md text-sm font-medium transition-colors',
        isActive
            ? 'bg-brand-primary text-ink-inverse'
            : 'text-ink-muted hover:text-ink hover:bg-surface',
    ].join(' ');
}

function Shell({ children }: { children: React.ReactNode }): JSX.Element {
    return (
        <div className="min-h-screen flex flex-col bg-surface text-ink dark:bg-surface-dark dark:text-ink-inverse">
            <a href="#main-content" className="skip-link">Skip to main content</a>
            <header className="border-b border-ink/10 dark:border-ink-inverse/10 bg-surface-light dark:bg-surface-dark">
                <div className="max-w-7xl mx-auto px-6 py-3 flex items-center gap-6">
                    <div className="flex items-center gap-2">
                        <span
                            aria-hidden="true"
                            className="block w-7 h-7 rounded bg-brand-primary"
                        />
                        <span className="font-semibold tracking-tight">SCC Studio</span>
                    </div>
                    <nav aria-label="Primary" className="flex gap-1">
                        {NAV.map((n) => (
                            <NavLink key={n.path} to={n.path} className={navClass}>
                                {n.label}
                            </NavLink>
                        ))}
                    </nav>
                </div>
            </header>
            <main id="main-content" className="flex-1 max-w-7xl w-full mx-auto px-6 py-6">
                {children}
            </main>
            <footer className="text-xs text-ink-muted px-6 py-3 border-t border-ink/10 dark:border-ink-inverse/10">
                Spatial Compression Codec &middot; Studio Frontend
            </footer>
        </div>
    );
}

function RouteFallback(): JSX.Element {
    return (
        <div role="status" aria-live="polite" className="p-8 text-ink-muted">
            Loading view&hellip;
        </div>
    );
}

export default function App(): JSX.Element {
    return (
        <BrowserRouter>
            <Shell>
                <Suspense fallback={<RouteFallback />}>
                    <Routes>
                        <Route path="/" element={<Navigate to="/capture" replace />} />
                        <Route path="/capture"  element={<Capture  />} />
                        <Route path="/playback" element={<Playback />} />
                        <Route path="/profiles" element={<Profiles />} />
                        <Route path="/settings" element={<Settings />} />
                        <Route path="*" element={<Navigate to="/capture" replace />} />
                    </Routes>
                </Suspense>
            </Shell>
        </BrowserRouter>
    );
}
