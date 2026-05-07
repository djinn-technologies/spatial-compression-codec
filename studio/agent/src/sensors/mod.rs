//! Sensor backend abstraction.
//!
//! Each backend is gated by a Cargo feature so a host without the
//! K4A SDK can still build the agent (just without that backend).
//! The `mock` backend is always available -- tests + CI use it
//! exclusively. [Ultrathink #2]

use std::time::SystemTime;

use thiserror::Error;

#[derive(Clone, Debug)]
pub struct SensorCapabilities {
    pub vendor: String,
    pub model: String,
    pub backend: &'static str,
    pub modes: Vec<FrameMode>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameMode {
    pub width: u32,
    pub height: u32,
    pub fps: u32,
    pub bit_depth: u32,
}

#[derive(Clone, Debug)]
pub struct SensorConfig {
    pub mode: FrameMode,
}

/// One depth frame produced by a sensor.
#[derive(Debug)]
pub struct Frame {
    pub width: u32,
    pub height: u32,
    pub bit_depth: u32,
    pub timestamp: SystemTime,
    pub depth: Vec<u16>,
}

#[derive(Debug, Error)]
pub enum SensorError {
    #[error("backend unavailable: {0}")]
    Unavailable(&'static str),
    #[error("hardware error: {0}")]
    Hardware(String),
    #[error("invalid configuration: {0}")]
    Config(String),
    #[error("timeout waiting for frame")]
    Timeout,
}

pub type Result<T> = std::result::Result<T, SensorError>;

/// Common sensor surface. Backends implement this; the encoder
/// thread holds a `Box<dyn Sensor>` and calls `next_frame` in a loop.
pub trait Sensor: Send {
    fn id(&self) -> &str;
    fn capabilities(&self) -> &SensorCapabilities;
    fn start(&mut self, config: &SensorConfig) -> Result<()>;
    fn stop(&mut self) -> Result<()>;
    fn next_frame(&mut self) -> Result<Frame>;
}

pub mod mock;

#[cfg(feature = "realsense")]
pub mod realsense;

#[cfg(feature = "k4a")]
pub mod k4a;

#[cfg(feature = "zed")]
pub mod zed;

#[cfg(feature = "uvc")]
pub mod uvc;

/// Enumerate every sensor reachable from this host. Each backend is
/// queried independently; if a backend is feature-disabled or its
/// SDK isn't installed, it simply contributes nothing.
pub fn enumerate() -> Vec<Box<dyn Sensor>> {
    let mut out: Vec<Box<dyn Sensor>> = Vec::new();
    out.extend(mock::enumerate());
    #[cfg(feature = "realsense")]
    out.extend(realsense::enumerate());
    #[cfg(feature = "k4a")]
    out.extend(k4a::enumerate());
    #[cfg(feature = "zed")]
    out.extend(zed::enumerate());
    #[cfg(feature = "uvc")]
    out.extend(uvc::enumerate());
    out
}
