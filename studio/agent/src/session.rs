//! Session lifecycle and IPC request dispatch.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use tokio::sync::Mutex as AsyncMutex;
use uuid::Uuid;

use crate::ipc::Handler;
use crate::metrics::{MetricsSnapshot, SessionMetrics};
use crate::proto;

#[derive(Debug)]
pub struct Session {
    pub id: String,
    pub sensor_id: String,
    pub width: u32,
    pub height: u32,
    pub bit_depth: u32,
    pub profile: String,
    pub metrics: Arc<SessionMetrics>,
}

#[derive(Debug, Default)]
pub struct AgentState {
    sessions: Mutex<HashMap<String, Arc<Session>>>,
}

impl AgentState {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn list_sensors(&self) -> Vec<proto::SensorDescriptor> {
        crate::sensors::enumerate()
            .into_iter()
            .map(|s| {
                let caps = s.capabilities();
                proto::SensorDescriptor {
                    id: s.id().to_string(),
                    vendor: caps.vendor.clone(),
                    model: caps.model.clone(),
                    backend: caps.backend.to_string(),
                    modes: caps
                        .modes
                        .iter()
                        .map(|m| proto::FrameMode {
                            width: m.width,
                            height: m.height,
                            fps: m.fps,
                            bit_depth: m.bit_depth,
                        })
                        .collect(),
                }
            })
            .collect()
    }

    pub fn start_session(&self, req: &proto::StartSession) -> Result<Arc<Session>, String> {
        if req.width == 0 || req.height == 0 {
            return Err("width and height must be > 0".into());
        }
        if !matches!(req.bit_depth, 8 | 12 | 16) {
            return Err(format!("bit_depth must be 8/12/16; got {}", req.bit_depth));
        }
        let session = Arc::new(Session {
            id: Uuid::new_v4().to_string(),
            sensor_id: req.sensor_id.clone(),
            width: req.width,
            height: req.height,
            bit_depth: req.bit_depth,
            profile: req.profile.clone(),
            metrics: Arc::new(SessionMetrics::default()),
        });
        self.sessions
            .lock()
            .insert(session.id.clone(), Arc::clone(&session));
        // The actual sensor + encoder threads are wired in the binary's
        // session-orchestration code (out of scope for the IPC dispatch
        // surface; v1 returns the SessionInfo immediately and the worker
        // threads spin up asynchronously).
        Ok(session)
    }

    pub fn stop_session(&self, id: &str) -> Result<(), String> {
        let removed = self.sessions.lock().expect("sessions mutex poisoned").remove(id);
        if removed.is_none() {
            return Err(format!("no such session {id}"));
        }
        Ok(())
    }

    pub fn metrics(&self, id: &str) -> Option<MetricsSnapshot> {
        self.sessions
            .lock()
            .get(id)
            .map(|s| s.metrics.snapshot())
    }

    pub fn aggregate_metrics(&self) -> MetricsSnapshot {
        // Sum frame counts; min/max histogram percentiles -- v1 returns the
        // first session's snapshot if any, else zeros.
        if let Some((_, s)) = self.sessions.lock().expect("sessions mutex poisoned").iter().next() {
            s.metrics.snapshot()
        } else {
            MetricsSnapshot::default()
        }
    }
}

// ---------------------------------------------------------------------------
// Handler -- routes AgentRequest variants to the right state methods.
// ---------------------------------------------------------------------------

pub struct AgentHandler {
    pub state: Arc<AgentState>,
    /// Locked when we need to serialise mutating IPC requests against
    /// each other. Read-only requests don't take it.
    write_guard: AsyncMutex<()>,
}

impl AgentHandler {
    pub fn new(state: Arc<AgentState>) -> Self {
        Self {
            state,
            write_guard: AsyncMutex::new(()),
        }
    }
}

impl std::fmt::Debug for AgentHandler {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("AgentHandler").finish_non_exhaustive()
    }
}

#[::async_trait::async_trait]
impl Handler for AgentHandler {
    async fn handle(&self, req: proto::AgentRequest) -> proto::AgentResponse {
        use proto::agent_request::Kind as ReqKind;
        use proto::agent_response::Kind as RespKind;

        let kind = match req.kind {
            Some(k) => k,
            None => return error_response(1, "AgentRequest.kind missing"),
        };

        match kind {
            ReqKind::List(_) => proto::AgentResponse {
                kind: Some(RespKind::Sensors(proto::SensorList {
                    sensors: self.state.list_sensors(),
                })),
            },
            ReqKind::Start(start) => {
                let _g = self.write_guard.lock().await;
                match self.state.start_session(&start) {
                    Ok(session) => proto::AgentResponse {
                        kind: Some(RespKind::Session(proto::SessionInfo {
                            session_id: session.id.clone(),
                            frames_captured: 0,
                            bytes_emitted: 0,
                            active: true,
                        })),
                    },
                    Err(e) => error_response(2, &e),
                }
            }
            ReqKind::Stop(stop) => {
                let _g = self.write_guard.lock().await;
                match self.state.stop_session(&stop.session_id) {
                    Ok(()) => proto::AgentResponse {
                        kind: Some(RespKind::Ack(proto::Ack {
                            message: "stopped".into(),
                        })),
                    },
                    Err(e) => error_response(3, &e),
                }
            }
            ReqKind::Metrics(req) => {
                let snap = if req.session_id.is_empty() {
                    self.state.aggregate_metrics()
                } else {
                    match self.state.metrics(&req.session_id) {
                        Some(s) => s,
                        None => return error_response(4, "no such session"),
                    }
                };
                proto::AgentResponse {
                    kind: Some(RespKind::Metrics(proto::Metrics {
                        frames_per_second: snap.frames_per_second,
                        dropped_frames: snap.dropped_frames,
                        encode_p50_ms: snap.encode_p50_ms,
                        encode_p99_ms: snap.encode_p99_ms,
                        ring_buffer_fill: snap.ring_buffer_fill_high,
                        frames_total: snap.frames_total,
                        bytes_total: snap.bytes_total,
                    })),
                }
            }
        }
    }
}

fn error_response(code: u32, message: &str) -> proto::AgentResponse {
    proto::AgentResponse {
        kind: Some(proto::agent_response::Kind::Err(proto::Error {
            code,
            message: message.to_string(),
        })),
    }
}
