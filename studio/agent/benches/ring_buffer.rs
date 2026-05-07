//! Bench the SPSC ring used between the sensor thread and the
//! encoder thread. [Ultrathink #3]
//!
//! Run via:
//!
//!     cargo bench --bench ring_buffer
//!
//! The bench measures push+pop throughput on a `crossbeam::queue::ArrayQueue`
//! sized for the agent's default capacity (8 slots).  Numbers are
//! reported on whichever host runs the bench and pinned in the project
//! perf log when CI runs it.

use std::sync::Arc;
use std::thread;

use criterion::{black_box, criterion_group, criterion_main, Criterion};
use crossbeam::queue::ArrayQueue;

fn ring_throughput(c: &mut Criterion) {
    let mut group = c.benchmark_group("ring_buffer");
    for &cap in &[8usize, 32, 128] {
        group.bench_function(format!("spsc_cap_{cap}"), |b| {
            b.iter(|| {
                let q: Arc<ArrayQueue<u64>> = Arc::new(ArrayQueue::new(cap));
                let producer = {
                    let q = Arc::clone(&q);
                    thread::spawn(move || {
                        for i in 0u64..1_000 {
                            while q.push(i).is_err() {
                                std::hint::spin_loop();
                            }
                        }
                    })
                };
                let consumer = {
                    let q = Arc::clone(&q);
                    thread::spawn(move || {
                        let mut count = 0u64;
                        while count < 1_000 {
                            if let Some(v) = q.pop() {
                                black_box(v);
                                count += 1;
                            } else {
                                std::hint::spin_loop();
                            }
                        }
                    })
                };
                producer.join().unwrap();
                consumer.join().unwrap();
            });
        });
    }
    group.finish();
}

criterion_group!(benches, ring_throughput);
criterion_main!(benches);
