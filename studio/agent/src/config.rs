//! Runtime configuration parsed from the CLI / env.

use std::path::PathBuf;

use clap::Parser;

#[derive(Debug, Clone, Parser)]
#[command(name = "scc-studio-agent", version, about)]
pub struct Cli {
    /// Path to the IPC socket / named pipe.
    #[cfg(unix)]
    #[arg(
        long,
        env = "SCC_AGENT_SOCKET",
        default_value = "/tmp/scc-agent.sock"
    )]
    pub socket: PathBuf,

    /// Path to the IPC named-pipe stem on Windows (the OS prepends \\.\pipe\).
    #[cfg(windows)]
    #[arg(
        long,
        env = "SCC_AGENT_SOCKET",
        default_value = r"\\.\pipe\scc-agent"
    )]
    pub socket: PathBuf,

    /// Log level filter (env_logger / tracing-subscriber syntax).
    #[arg(long, env = "SCC_AGENT_LOG", default_value = "info")]
    pub log_level: String,

    /// OTLP gRPC endpoint for trace export. Empty = disabled.
    #[arg(long, env = "SCC_AGENT_OTLP_ENDPOINT")]
    pub otlp_endpoint: Option<String>,

    /// Capacity (in frames) of the SPSC ring between sensor and encoder.
    #[arg(long, env = "SCC_AGENT_RING_CAPACITY", default_value_t = 8)]
    pub ring_capacity: usize,
}
