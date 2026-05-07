// src/observability/metrics.ts
//
// Aggregated counters / histograms exposed via OTel meter (when the
// OTLP endpoint is configured) and via a /api/internal/metrics HTTP
// endpoint for Prometheus scraping (when SCRAPE_ENABLED=true).
//
// Production deployments choose ONE of OTel-push or Prom-scrape; the
// counters here are written via OTel API calls regardless and the SDK
// pipeline routes them appropriately.

import { metrics } from '@opentelemetry/api';

const meter = metrics.getMeter('scc-studio-api');

export const requestCounter = meter.createCounter('scc_api_requests_total', {
    description: 'Number of requests served, labelled by route + method + status.',
});

export const requestDurationMs = meter.createHistogram('scc_api_request_duration_ms', {
    description: 'Per-request handler duration in milliseconds.',
    unit: 'ms',
});

export const wsActiveConnections = meter.createUpDownCounter(
    'scc_api_ws_active_connections',
    {
        description: 'Currently-open metric-stream WebSocket connections.',
    },
);

export const wsBackpressureDrops = meter.createCounter(
    'scc_api_ws_backpressure_drops_total',
    {
        description:
            'Number of WS messages dropped because the client could not keep up. ' +
            'Indicates a slow consumer; does NOT cause server OOM.',
    },
);
