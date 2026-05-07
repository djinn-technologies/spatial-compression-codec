//! Output mux for SEI bytes.
//!
//! v1: raw passthrough — every encoded SEI is appended length-prefixed
//! to the configured output (file or socket). The libavformat-backed
//! H.264 NAL wrap is a v2 follow-up; for now we ship the encoder's
//! output verbatim so downstream tools can wrap it themselves.
//! [REQ-029]

use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;

use thiserror::Error;

#[derive(Debug, Error)]
pub enum MuxerError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
}

#[derive(Debug)]
pub enum Muxer {
    /// Discard the bytes (in-memory benchmarking).
    Null,
    /// Append each frame as `[u32 BE length][bytes]` to a file.
    LengthPrefixedFile(BufWriter<File>),
}

impl Muxer {
    pub fn null() -> Self {
        Muxer::Null
    }

    pub fn open_file(path: &Path) -> Result<Self, MuxerError> {
        let file = File::create(path)?;
        Ok(Muxer::LengthPrefixedFile(BufWriter::new(file)))
    }

    pub fn write_frame(&mut self, sei: &[u8]) -> Result<(), MuxerError> {
        match self {
            Muxer::Null => Ok(()),
            Muxer::LengthPrefixedFile(w) => {
                let len = u32::try_from(sei.len()).map_err(|_| {
                    MuxerError::Io(std::io::Error::new(
                        std::io::ErrorKind::InvalidData,
                        "frame > 4 GiB",
                    ))
                })?;
                w.write_all(&len.to_be_bytes())?;
                w.write_all(sei)?;
                Ok(())
            }
        }
    }

    pub fn flush(&mut self) -> Result<(), MuxerError> {
        match self {
            Muxer::Null => Ok(()),
            Muxer::LengthPrefixedFile(w) => Ok(w.flush()?),
        }
    }
}
