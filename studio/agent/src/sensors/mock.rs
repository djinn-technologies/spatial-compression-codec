//! Mock sensor backend. Always compiled. Used by the test suite and
//! by CI runners that have no real depth hardware. Generates a
//! deterministic depth pattern so round-trip tests can byte-equality
//! check the result.

use std::time::SystemTime;

use super::{
    Frame, FrameMode, Result, Sensor, SensorCapabilities, SensorConfig, SensorError,
};

#[derive(Debug)]
pub struct MockSensor {
    id: String,
    capabilities: SensorCapabilities,
    started_mode: Option<FrameMode>,
    frame_index: u64,
}

impl MockSensor {
    pub fn new(id: impl Into<String>) -> Self {
        let modes = vec![
            FrameMode { width: 16,   height: 16,  fps: 30, bit_depth: 12 },
            FrameMode { width: 64,   height: 48,  fps: 30, bit_depth: 12 },
            FrameMode { width: 1280, height: 720, fps: 30, bit_depth: 12 },
        ];
        let id = id.into();
        Self {
            capabilities: SensorCapabilities {
                vendor: "Djinn".into(),
                model: format!("MockSensor-{id}"),
                backend: "mock",
                modes,
            },
            id,
            started_mode: None,
            frame_index: 0,
        }
    }
}

impl Sensor for MockSensor {
    fn id(&self) -> &str {
        &self.id
    }

    fn capabilities(&self) -> &SensorCapabilities {
        &self.capabilities
    }

    fn start(&mut self, config: &SensorConfig) -> Result<()> {
        if !self.capabilities.modes.contains(&config.mode) {
            return Err(SensorError::Config(format!(
                "mode {:?} not supported by mock sensor",
                config.mode
            )));
        }
        self.started_mode = Some(config.mode);
        self.frame_index = 0;
        Ok(())
    }

    fn stop(&mut self) -> Result<()> {
        self.started_mode = None;
        Ok(())
    }

    fn next_frame(&mut self) -> Result<Frame> {
        let mode = self
            .started_mode
            .ok_or(SensorError::Hardware("mock sensor not started".into()))?;
        let n = (mode.width * mode.height) as usize;
        let mut depth = vec![0u16; n];
        // Deterministic LCG so test consumers can byte-equality compare.
        let mut s: u32 = (self.frame_index as u32).wrapping_mul(0x9E37_79B9);
        for v in &mut depth {
            s = s.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            *v = (s & 0x3FF) as u16;
        }
        self.frame_index += 1;
        Ok(Frame {
            width: mode.width,
            height: mode.height,
            bit_depth: mode.bit_depth,
            timestamp: SystemTime::now(),
            depth,
        })
    }
}

pub fn enumerate() -> Vec<Box<dyn Sensor>> {
    vec![Box::new(MockSensor::new("mock-0"))]
}
