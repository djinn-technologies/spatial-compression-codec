//! End-to-end IPC integration test.
//!
//! Spawns the agent's IPC handler in-process on a tempfile-backed Unix
//! socket and drives it through the protobuf RPC. Exercises:
//!
//!   - ListSensors round-trip (mock backend present).
//!   - StartSession creates a session id; subsequent metrics query
//!     resolves to that id.
//!   - StopSession removes it; further metrics on that id error out.
//!   - GarbageRequest (no `kind`) returns Error{code=1}.
//!
//! Runs on Unix targets only; Windows named-pipe integration is a v2
//! follow-up (the same tests exercise the same handler when that lands).

#![cfg(unix)]

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use prost::Message;
use scc_studio_agent::ipc;
use scc_studio_agent::proto;
use scc_studio_agent::session::{AgentHandler, AgentState};
use tempfile::tempdir;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::UnixStream;

async fn write_msg(stream: &mut UnixStream, req: &proto::AgentRequest) {
    let mut buf = Vec::new();
    req.encode(&mut buf).unwrap();
    let len: u32 = buf.len().try_into().unwrap();
    stream.write_all(&len.to_be_bytes()).await.unwrap();
    stream.write_all(&buf).await.unwrap();
    stream.flush().await.unwrap();
}

async fn read_msg(stream: &mut UnixStream) -> proto::AgentResponse {
    let mut len_buf = [0u8; 4];
    stream.read_exact(&mut len_buf).await.unwrap();
    let len = u32::from_be_bytes(len_buf) as usize;
    let mut buf = vec![0u8; len];
    stream.read_exact(&mut buf).await.unwrap();
    proto::AgentResponse::decode(&*buf).unwrap()
}

fn list_sensors_req() -> proto::AgentRequest {
    proto::AgentRequest {
        kind: Some(proto::agent_request::Kind::List(proto::ListSensors {})),
    }
}

fn start_session_req(sensor_id: &str) -> proto::AgentRequest {
    proto::AgentRequest {
        kind: Some(proto::agent_request::Kind::Start(proto::StartSession {
            sensor_id: sensor_id.into(),
            width: 16,
            height: 16,
            bit_depth: 12,
            profile: "lossless".into(),
            output_path: String::new(),
            fps: 30,
        })),
    }
}

fn metrics_req(session_id: &str) -> proto::AgentRequest {
    proto::AgentRequest {
        kind: Some(proto::agent_request::Kind::Metrics(proto::GetMetrics {
            session_id: session_id.into(),
        })),
    }
}

fn stop_session_req(session_id: &str) -> proto::AgentRequest {
    proto::AgentRequest {
        kind: Some(proto::agent_request::Kind::Stop(proto::StopSession {
            session_id: session_id.into(),
        })),
    }
}

#[tokio::test]
async fn full_session_round_trip() {
    let dir = tempdir().unwrap();
    let socket_path: PathBuf = dir.path().join("agent.sock");
    let state = Arc::new(AgentState::new());
    let handler = Arc::new(AgentHandler::new(Arc::clone(&state)));
    let (_tx, rx) = tokio::sync::watch::channel(false);

    let socket_for_serve = socket_path.clone();
    let serve_handle = tokio::spawn(async move {
        ipc::serve(&socket_for_serve, handler, rx).await
    });
    // Give the listener a moment to bind.
    tokio::time::sleep(Duration::from_millis(50)).await;

    let mut s = UnixStream::connect(&socket_path).await.unwrap();

    // 1. ListSensors -- mock backend should appear.
    write_msg(&mut s, &list_sensors_req()).await;
    let resp = read_msg(&mut s).await;
    let sensors = match resp.kind {
        Some(proto::agent_response::Kind::Sensors(l)) => l.sensors,
        other => panic!("unexpected response: {other:?}"),
    };
    assert!(
        sensors.iter().any(|d| d.backend == "mock"),
        "mock backend missing from {sensors:?}"
    );
    let sensor_id = sensors[0].id.clone();

    // 2. StartSession.
    write_msg(&mut s, &start_session_req(&sensor_id)).await;
    let session_id = match read_msg(&mut s).await.kind {
        Some(proto::agent_response::Kind::Session(info)) => {
            assert!(info.active);
            info.session_id
        }
        other => panic!("unexpected: {other:?}"),
    };
    assert!(!session_id.is_empty());

    // 3. GetMetrics for that session id resolves.
    write_msg(&mut s, &metrics_req(&session_id)).await;
    match read_msg(&mut s).await.kind {
        Some(proto::agent_response::Kind::Metrics(_)) => {}
        other => panic!("expected Metrics, got {other:?}"),
    }

    // 4. StopSession.
    write_msg(&mut s, &stop_session_req(&session_id)).await;
    match read_msg(&mut s).await.kind {
        Some(proto::agent_response::Kind::Ack(_)) => {}
        other => panic!("expected Ack, got {other:?}"),
    }

    // 5. Stop again -> Error.
    write_msg(&mut s, &stop_session_req(&session_id)).await;
    match read_msg(&mut s).await.kind {
        Some(proto::agent_response::Kind::Err(e)) => assert!(e.code != 0),
        other => panic!("expected Err, got {other:?}"),
    }

    // Tear down.
    drop(s);
    serve_handle.abort();
}

#[tokio::test]
async fn empty_request_kind_returns_error() {
    let dir = tempdir().unwrap();
    let socket_path = dir.path().join("agent.sock");
    let state = Arc::new(AgentState::new());
    let handler = Arc::new(AgentHandler::new(state));
    let (_tx, rx) = tokio::sync::watch::channel(false);

    let socket_for_serve = socket_path.clone();
    let serve_handle = tokio::spawn(async move {
        ipc::serve(&socket_for_serve, handler, rx).await
    });
    tokio::time::sleep(Duration::from_millis(50)).await;

    let mut s = UnixStream::connect(&socket_path).await.unwrap();
    write_msg(&mut s, &proto::AgentRequest { kind: None }).await;
    match read_msg(&mut s).await.kind {
        Some(proto::agent_response::Kind::Err(e)) => assert_eq!(e.code, 1),
        other => panic!("expected Err{{code=1}}, got {other:?}"),
    }
    drop(s);
    serve_handle.abort();
}
