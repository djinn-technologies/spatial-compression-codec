//! UVC (USB Video Class) depth-camera backend (gated by `feature = "uvc"`).
//!
//! v1 stub. The real impl wires libuvc / nokhwa to enumerate UVC devices
//! that expose a depth stream (e.g. Femto Mega, Orbbec Astra). Returns
//! Unavailable so the agent compiles without a UVC backend installed.

use super::{Frame, Result, Sensor, SensorCapabilities, SensorConfig, SensorError};

#[derive(Debug)]
pub struct UvcSensor {
    _placeholder: (),
}

impl Sensor for UvcSensor {
    fn id(&self) -> &str {
        "uvc:unimplemented"
    }
    fn capabilities(&self) -> &SensorCapabilities {
        unimplemented!("uvc backend stub -- integrate libuvc / nokhwa")
    }
    fn start(&mut self, _config: &SensorConfig) -> Result<()> {
        Err(SensorError::Unavailable("UVC backend is a v1 stub"))
    }
    fn stop(&mut self) -> Result<()> {
        Ok(())
    }
    fn next_frame(&mut self) -> Result<Frame> {
        Err(SensorError::Unavailable("UVC backend is a v1 stub"))
    }
}

pub fn enumerate() -> Vec<Box<dyn Sensor>> {
    Vec::new()
}
