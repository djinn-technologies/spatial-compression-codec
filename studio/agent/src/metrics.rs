//! Per-session metrics: throughput, drop counters, encode-latency
//! percentiles. Read by `GetMetrics` IPC requests; exported via OTLP
//! when the OTLP endpoint is configured.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use hdrhistogram::Histogram;

#[derive(Debug)]
pub struct SessionMetrics {
    started: Instant,
    frames_total: AtomicU64,
    bytes_total: AtomicU64,
    dropped_frames: AtomicU64,
    ring_buffer_fill_high: AtomicU64,
    /// Encode latency histogram (microseconds; values up to 60 s).
    encode_latency_us: Mutex<Histogram<u64>>,
}

impl Default for SessionMetrics {
    fn default() -> Self {
        Self {
            started: Instant::now(),
            frames_total: AtomicU64::new(0),
            bytes_total: AtomicU64::new(0),
            dropped_frames: AtomicU64::new(0),
            ring_buffer_fill_high: AtomicU64::new(0),
            encode_latency_us: Mutex::new(
                Histogram::<u64>::new_with_bounds(1, 60_000_000, 3)
                    .expect("histogram construction"),
            ),
        }
    }
}

impl SessionMetrics {
    pub fn record_frame(&self, encoded_bytes: u64, encode_time: Duration) {
        self.frames_total.fetch_add(1, Ordering::Relaxed);
        self.bytes_total.fetch_add(encoded_bytes, Ordering::Relaxed);
        if let Ok(mut h) = self.encode_latency_us.lock() {
            let us: u64 = encode_time.as_micros().min(u64::MAX as u128) as u64;
            // record_correct distributes the value across the histogram
            // even for spikes that exceed the resolution.
            let _ = h.record(us.max(1));
        }
    }

    pub fn record_drop(&self) {
        self.dropped_frames.fetch_add(1, Ordering::Relaxed);
    }

    pub fn observe_ring_fill(&self, fill: u64) {
        self.ring_buffer_fill_high
            .fetch_max(fill, Ordering::Relaxed);
    }

    pub fn snapshot(&self) -> MetricsSnapshot {
        let frames = self.frames_total.load(Ordering::Relaxed);
        let bytes = self.bytes_total.load(Ordering::Relaxed);
        let dropped = self.dropped_frames.load(Ordering::Relaxed);
        let fill = self.ring_buffer_fill_high.load(Ordering::Relaxed);
        let elapsed = self.started.elapsed().as_secs_f64().max(1e-9);

        let (p50_ms, p99_ms) = match self.encode_latency_us.lock() {
            Ok(h) => (
                h.value_at_quantile(0.50) as f64 / 1000.0,
                h.value_at_quantile(0.99) as f64 / 1000.0,
            ),
            Err(_) => (0.0, 0.0),
        };

        MetricsSnapshot {
            frames_per_second: frames as f64 / elapsed,
            dropped_frames: dropped,
            encode_p50_ms: p50_ms,
            encode_p99_ms: p99_ms,
            ring_buffer_fill_high: fill,
            frames_total: frames,
            bytes_total: bytes,
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct MetricsSnapshot {
    pub frames_per_second: f64,
    pub dropped_frames: u64,
    pub encode_p50_ms: f64,
    pub encode_p99_ms: f64,
    pub ring_buffer_fill_high: u64,
    pub frames_total: u64,
    pub bytes_total: u64,
}
