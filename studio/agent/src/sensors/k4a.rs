//! Azure Kinect (K4A) backend (gated by `feature = "k4a"`).
//!
//! v1 stub. Real implementation wires `k4a-sys` and drives the
//! k4a_device API. The stub returns Unavailable so the agent compiles
//! without the K4A SDK installed -- the gate semantics that
//! Ultrathink #2 asks about.

use super::{Frame, Result, Sensor, SensorCapabilities, SensorConfig, SensorError};

#[derive(Debug)]
pub struct K4ASensor {
    _placeholder: (),
}

impl Sensor for K4ASensor {
    fn id(&self) -> &str {
        "k4a:unimplemented"
    }
    fn capabilities(&self) -> &SensorCapabilities {
        unimplemented!("k4a backend stub -- integrate k4a-sys")
    }
    fn start(&mut self, _config: &SensorConfig) -> Result<()> {
        Err(SensorError::Unavailable("K4A backend is a v1 stub"))
    }
    fn stop(&mut self) -> Result<()> {
        Ok(())
    }
    fn next_frame(&mut self) -> Result<Frame> {
        Err(SensorError::Unavailable("K4A backend is a v1 stub"))
    }
}

pub fn enumerate() -> Vec<Box<dyn Sensor>> {
    Vec::new()
}
