//! Stereolabs ZED backend (gated by `feature = "zed"`).
//!
//! v1 stub.

use super::{Frame, Result, Sensor, SensorCapabilities, SensorConfig, SensorError};

#[derive(Debug)]
pub struct ZedSensor {
    _placeholder: (),
}

impl Sensor for ZedSensor {
    fn id(&self) -> &str {
        "zed:unimplemented"
    }
    fn capabilities(&self) -> &SensorCapabilities {
        unimplemented!("zed backend stub -- integrate the ZED SDK Rust bindings")
    }
    fn start(&mut self, _config: &SensorConfig) -> Result<()> {
        Err(SensorError::Unavailable("ZED backend is a v1 stub"))
    }
    fn stop(&mut self) -> Result<()> {
        Ok(())
    }
    fn next_frame(&mut self) -> Result<Frame> {
        Err(SensorError::Unavailable("ZED backend is a v1 stub"))
    }
}

pub fn enumerate() -> Vec<Box<dyn Sensor>> {
    Vec::new()
}
