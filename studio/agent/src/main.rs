//! `scc-studio-agent` binary entrypoint.
//!
//! Wires the Tokio runtime, tracing/OTLP, and the IPC server. The
//! Studio frontend connects to the configured socket / pipe and
//! drives sessions via the protobuf RPC defined in
//! `src/proto/control.proto`.
//!
//! [REQ-029, ADR-011]

use std::sync::Arc;

use anyhow::Result;
use clap::Parser;
use tracing_subscriber::{prelude::*, EnvFilter};

use scc_studio_agent::config::Cli;
use scc_studio_agent::ipc;
use scc_studio_agent::session::{AgentHandler, AgentState};

#[tokio::main(flavor = "multi_thread")]
async fn main() -> Result<()> {
    let cli = Cli::parse();
    init_tracing(&cli)?;

    tracing::info!(
        version = env!("CARGO_PKG_VERSION"),
        socket  = ?cli.socket,
        "scc-studio-agent starting"
    );

    let state = Arc::new(AgentState::new());
    let handler = Arc::new(AgentHandler::new(Arc::clone(&state)));

    let (shutdown_tx, shutdown_rx) = tokio::sync::watch::channel(false);

    let signal_task = tokio::spawn(async move {
        wait_for_shutdown_signal().await;
        let _ = shutdown_tx.send(true);
    });

    let serve_result = ipc::serve(&cli.socket, handler, shutdown_rx).await;

    let _ = signal_task.await;

    if let Err(e) = serve_result {
        tracing::error!(error = %e, "ipc server exited with error");
        std::process::exit(1);
    }

    tracing::info!("scc-studio-agent stopped");
    Ok(())
}

// ---------------------------------------------------------------------------
// Tracing / OTLP setup
// ---------------------------------------------------------------------------

fn init_tracing(cli: &Cli) -> Result<()> {
    let env_filter = EnvFilter::try_new(&cli.log_level)
        .unwrap_or_else(|_| EnvFilter::new("info"));

    let fmt_layer = tracing_subscriber::fmt::layer()
        .with_target(true)
        .with_thread_ids(false);

    let registry = tracing_subscriber::registry().with(env_filter).with(fmt_layer);

    if let Some(endpoint) = cli.otlp_endpoint.as_deref().filter(|s| !s.is_empty()) {
        // OTLP exporter via tonic. Errors here are non-fatal: log and
        // continue without OTLP rather than refuse to start.
        match build_otlp_layer(endpoint) {
            Ok(otel_layer) => {
                registry.with(otel_layer).try_init().ok();
                return Ok(());
            }
            Err(e) => {
                eprintln!("warn: OTLP exporter init failed ({e}); continuing without remote tracing");
            }
        }
    }

    registry.try_init().ok();
    Ok(())
}

fn build_otlp_layer(
    endpoint: &str,
) -> Result<impl tracing_subscriber::Layer<tracing_subscriber::Registry> + Send + Sync> {
    use opentelemetry::trace::TracerProvider as _;
    use opentelemetry_otlp::WithExportConfig;
    use opentelemetry_sdk::Resource;

    let exporter = opentelemetry_otlp::SpanExporter::builder()
        .with_tonic()
        .with_endpoint(endpoint.to_string())
        .build()?;

    let provider = opentelemetry_sdk::trace::TracerProvider::builder()
        .with_batch_exporter(exporter, opentelemetry_sdk::runtime::Tokio)
        .with_resource(Resource::new(vec![opentelemetry::KeyValue::new(
            "service.name",
            "scc-studio-agent",
        )]))
        .build();

    let tracer = provider.tracer("scc-studio-agent");
    Ok(tracing_opentelemetry::layer().with_tracer(tracer))
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

#[cfg(unix)]
async fn wait_for_shutdown_signal() {
    use tokio::signal::unix::{signal, SignalKind};
    let mut sigint = signal(SignalKind::interrupt()).expect("install SIGINT handler");
    let mut sigterm = signal(SignalKind::terminate()).expect("install SIGTERM handler");
    tokio::select! {
        _ = sigint.recv()  => tracing::info!("SIGINT received"),
        _ = sigterm.recv() => tracing::info!("SIGTERM received"),
    }
}

#[cfg(windows)]
async fn wait_for_shutdown_signal() {
    let _ = tokio::signal::ctrl_c().await;
    tracing::info!("Ctrl-C received");
}
