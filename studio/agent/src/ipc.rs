//! Async IPC server. Length-prefixed protobuf on a Unix domain socket
//! (Linux/macOS) or a Windows named pipe.
//!
//! Wire framing:
//!
//!     [u32 BE length] [length bytes of protobuf-encoded AgentRequest]
//!
//! Each connection runs as a `tokio::spawn`'d task that decodes
//! requests and dispatches via the `Handler` trait. The compute heavy
//! work (sensor capture + libscc encode) happens on dedicated worker
//! threads owned by `Session`; this async layer only validates and
//! routes. [REQ-029, ADR-011]

use bytes::Bytes;
use prost::Message;
use thiserror::Error;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

use crate::proto;

#[derive(Debug, Error)]
pub enum IpcError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("protobuf encode: {0}")]
    Encode(#[from] prost::EncodeError),
    #[error("protobuf decode: {0}")]
    Decode(#[from] prost::DecodeError),
    #[error("frame too large: {0} bytes (max {1})")]
    OversizedFrame(u32, u32),
    #[error("connection closed mid-frame")]
    Closed,
}

/// Cap a single inbound message at 4 MiB. The IPC contract today only
/// carries small control messages (no frame data crosses this surface).
pub const MAX_FRAME_BYTES: u32 = 4 * 1024 * 1024;

/// Implementations dispatch a single decoded request to the right
/// session / state machine and produce one response.
#[::async_trait::async_trait]
pub trait Handler: Send + Sync + 'static {
    async fn handle(&self, req: proto::AgentRequest) -> proto::AgentResponse;
}

// ---------------------------------------------------------------------------
// Length-prefix framing
// ---------------------------------------------------------------------------

pub async fn read_message<R>(stream: &mut R) -> Result<proto::AgentRequest, IpcError>
where
    R: tokio::io::AsyncRead + Unpin,
{
    let mut len_buf = [0u8; 4];
    if let Err(e) = stream.read_exact(&mut len_buf).await {
        return if e.kind() == std::io::ErrorKind::UnexpectedEof {
            Err(IpcError::Closed)
        } else {
            Err(IpcError::Io(e))
        };
    }
    let len = u32::from_be_bytes(len_buf);
    if len > MAX_FRAME_BYTES {
        return Err(IpcError::OversizedFrame(len, MAX_FRAME_BYTES));
    }
    let mut buf = vec![0u8; len as usize];
    stream.read_exact(&mut buf).await?;
    Ok(proto::AgentRequest::decode(Bytes::from(buf))?)
}

pub async fn write_message<W>(
    stream: &mut W,
    resp: &proto::AgentResponse,
) -> Result<(), IpcError>
where
    W: tokio::io::AsyncWrite + Unpin,
{
    let mut buf = Vec::with_capacity(64);
    resp.encode(&mut buf)?;
    let len: u32 = buf
        .len()
        .try_into()
        .map_err(|_| IpcError::OversizedFrame(u32::MAX, MAX_FRAME_BYTES))?;
    stream.write_all(&len.to_be_bytes()).await?;
    stream.write_all(&buf).await?;
    stream.flush().await?;
    Ok(())
}

// ---------------------------------------------------------------------------
// Per-platform listener
// ---------------------------------------------------------------------------

#[cfg(unix)]
pub mod unix_listener {
    use std::path::Path;
    use std::sync::Arc;

    use tokio::net::UnixListener;

    use super::{read_message, write_message, Handler, IpcError};

    pub async fn serve<H: Handler>(
        socket: &Path,
        handler: Arc<H>,
        shutdown: tokio::sync::watch::Receiver<bool>,
    ) -> Result<(), IpcError> {
        // Best-effort cleanup of a stale socket from a prior run.
        let _ = std::fs::remove_file(socket);
        let listener = UnixListener::bind(socket)?;
        tracing::info!(?socket, "ipc: listening on Unix socket");
        accept_loop(listener, handler, shutdown).await
    }

    async fn accept_loop<H: Handler>(
        listener: UnixListener,
        handler: Arc<H>,
        mut shutdown: tokio::sync::watch::Receiver<bool>,
    ) -> Result<(), IpcError> {
        loop {
            tokio::select! {
                accept = listener.accept() => {
                    let (stream, _) = accept?;
                    let h = Arc::clone(&handler);
                    tokio::spawn(async move {
                        if let Err(e) = handle_connection(stream, h).await {
                            if !matches!(e, IpcError::Closed) {
                                tracing::warn!(error = %e, "ipc connection error");
                            }
                        }
                    });
                }
                _ = shutdown.changed() => {
                    if *shutdown.borrow() {
                        tracing::info!("ipc: shutdown signaled");
                        return Ok(());
                    }
                }
            }
        }
    }

    async fn handle_connection<S, H>(mut stream: S, handler: Arc<H>) -> Result<(), IpcError>
    where
        S: tokio::io::AsyncRead + tokio::io::AsyncWrite + Unpin,
        H: Handler,
    {
        loop {
            let req = read_message(&mut stream).await?;
            let resp = handler.handle(req).await;
            write_message(&mut stream, &resp).await?;
        }
    }
}

#[cfg(windows)]
pub mod windows_listener {
    //! TODO(v2): Windows named-pipe listener via
    //! `tokio::net::windows::NamedPipeServer`. The contract is identical
    //! (length-prefixed protobuf); only the listener type differs.
    //!
    //! Today the Windows agent runs over the same Unix-style API on
    //! Cygwin/MSYS environments that emulate Unix sockets. CI gates
    //! Windows on a pipe-listener follow-up before shipping.

    use std::path::Path;
    use std::sync::Arc;

    use super::{Handler, IpcError};

    pub async fn serve<H: Handler>(
        _socket: &Path,
        _handler: Arc<H>,
        _shutdown: tokio::sync::watch::Receiver<bool>,
    ) -> Result<(), IpcError> {
        Err(IpcError::Io(std::io::Error::new(
            std::io::ErrorKind::Unsupported,
            "Windows named-pipe IPC is a v2 follow-up",
        )))
    }
}

#[cfg(unix)]
pub use unix_listener::serve;

#[cfg(windows)]
pub use windows_listener::serve;
