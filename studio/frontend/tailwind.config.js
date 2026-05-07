/** @type {import('tailwindcss').Config} */
export default {
    content: ['./index.html', './src/**/*.{ts,tsx}'],
    darkMode: 'class',
    theme: {
        extend: {
            colors: {
                // Akuma Engineering Ltd. brand tokens. Components reference
                // these via `bg-brand-primary`, never the hex literal.
                // [Ultrathink #5]
                brand: {
                    primary: '#0B4884',
                    'primary-dark': '#093C70',
                    'primary-light': '#1462AE',
                    accent: '#1168BD',
                    'accent-dark': '#0E5AA3',
                    'accent-light': '#2A7DCE',
                },
                surface: {
                    light: '#FFFFFF',
                    DEFAULT: '#F5F7FA',
                    dark: '#0F172A',
                },
                ink: {
                    DEFAULT: '#0F172A',
                    muted: '#475569',
                    inverse: '#F1F5F9',
                },
            },
            fontFamily: {
                // Inter preferred. Fallback to Calibri (per brand guide) and
                // then system stack so headless / CI environments don't
                // render fall-back-of-fall-back fonts at random.
                sans: [
                    'Inter',
                    'Calibri',
                    'system-ui',
                    '-apple-system',
                    'Segoe UI',
                    'Roboto',
                    'sans-serif',
                ],
                mono: ['JetBrains Mono', 'Menlo', 'Consolas', 'monospace'],
            },
            screens: {
                xs: '420px',
            },
        },
    },
    plugins: [],
};
