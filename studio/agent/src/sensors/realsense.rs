//! Intel RealSense backend (gated by `feature = "realsense"`).
//!
//! v1 stub. Real implementation wires the `librealsense2-sys` crate and
//! drives the rs2 pipeline. The stub returns Unavailable so the agent
//! still compiles and runs without the SDK installed.

use super::{Frame, Result, Sensor, SensorCapabilities, SensorConfig, SensorError};

#[derive(Debug)]
pub struct RealSenseSensor {
    /// Field reserved for the rs2 pipeline handle when the real impl lands.
    _placeholder: (),
}

impl Sensor for RealSenseSensor {
    fn id(&self) -> &str {
        "realsense:unimplemented"
    }
    fn capabilities(&self) -> &SensorCapabilities {
        unimplemented!("realsense backend stub -- integrate librealsense2-sys")
    }
    fn start(&mut self, _config: &SensorConfig) -> Result<()> {
        Err(SensorError::Unavailable(
            "RealSense backend is a v1 stub; install librealsense2 and \
             swap in the real implementation",
        ))
    }
    fn stop(&mut self) -> Result<()> {
        Ok(())
    }
    fn next_frame(&mut self) -> Result<Frame> {
        Err(SensorError::Unavailable("RealSense backend is a v1 stub"))
    }
}

pub fn enumerate() -> Vec<Box<dyn Sensor>> {
    // Real impl: rs2::Context::new().query_devices() -> per-device sensor.
    // Stub: zero sensors (so the SensorList rpc just lists nothing for this
    // backend, no spurious "broken sensor" entries).
    Vec::new()
}
