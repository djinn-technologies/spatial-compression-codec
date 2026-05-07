//! `scc-studio-agent` library entry.
//!
//! The agent ships as a single binary (`src/main.rs`) but the modules
//! are exposed here as a library so integration tests under `tests/`
//! can spawn the IPC server in-process and drive it without execing a
//! subprocess.
//!
//! [REQ-029]

#![forbid(unsafe_op_in_unsafe_fn)]
#![deny(missing_debug_implementations)]
#![allow(clippy::result_large_err)]

pub mod config;
pub mod ipc;
pub mod metrics;
pub mod muxer;
pub mod scc_bridge;
pub mod sensors;
pub mod session;

/// Generated protobuf types (`prost-build` writes into `OUT_DIR/scc.studio.agent.v1.rs`).
pub mod proto {
    include!(concat!(env!("OUT_DIR"), "/scc.studio.agent.v1.rs"));
}

/// libscc FFI declarations (`bindgen` writes into `OUT_DIR/scc_sys.rs`).
pub mod scc_sys {
    #![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code)]
    include!(concat!(env!("OUT_DIR"), "/scc_sys.rs"));
}
