// src/observability/tracer.ts
//
// OpenTelemetry SDK wiring. Lazy-init: when the OTLP endpoint env var
// is missing or empty, we DO NOT spin up the SDK at all -- keeping
// cold start under the 800 ms acceptance budget on minimal deploys.

import type { Config } from '../config.js';

let started: { shutdown(): Promise<void> } | null = null;

export async function initTracer(cfg: Config): Promise<void> {
    if (!cfg.otelExporterOtlpEndpoint) return;
    if (started) return;

    // Dynamic-import the heavy SDK only when actually used. The cold-start
    // win is significant: ~250-400 ms saved.
    const { NodeSDK } = await import('@opentelemetry/sdk-node');
    const { OTLPTraceExporter } = await import(
        '@opentelemetry/exporter-trace-otlp-grpc'
    );
    const { getNodeAutoInstrumentations } = await import(
        '@opentelemetry/auto-instrumentations-node'
    );

    const sdk = new NodeSDK({
        serviceName: cfg.otelServiceName,
        traceExporter: new OTLPTraceExporter({ url: cfg.otelExporterOtlpEndpoint }),
        instrumentations: [
            getNodeAutoInstrumentations({
                // Pino auto-instrumentation injects trace_id into log records.
                '@opentelemetry/instrumentation-fs': { enabled: false },
            }),
        ],
    });
    sdk.start();
    started = sdk;
}

export async function shutdownTracer(): Promise<void> {
    if (started) {
        await started.shutdown();
        started = null;
    }
}
