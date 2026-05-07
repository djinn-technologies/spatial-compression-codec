// bench/bench_rans.cpp
//
// Minimal <chrono>-based throughput harness for the SCC rANS coder.
// Acceptance gate from AI prompt #1:
//   "Benchmarked encode > 200 MB/s, decode > 250 MB/s on a Ryzen-class CPU."
//
// Reports MB/s per variant. No external deps. A real Google Benchmark
// integration lands with AI prompt #13.

#include "scc/rans.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace {

constexpr std::size_t kPayloadBytes  = 8u << 20;   // 8 MiB per variant
constexpr int         kRepeats       = 4;

template <class Fn>
double median_seconds(int n_repeats, Fn&& fn) {
    std::vector<double> samples;
    samples.reserve(n_repeats);
    for (int i = 0; i < n_repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void bench_rans8() {
    // Mildly skewed alphabet (256 symbols, geometric-ish counts).
    std::vector<uint32_t> counts(256);
    for (std::size_t i = 0; i < counts.size(); ++i) {
        counts[i] = static_cast<uint32_t>(1 + (1000u >> (i / 16)));
    }
    const auto tab = scc::rans::ProbTable8::from_counts(counts.data(), counts.size());

    std::vector<uint8_t> data(kPayloadBytes);
    std::mt19937_64 rng(0xB10C5EEDULL);
    std::uniform_int_distribution<uint32_t> dist(0, scc::rans::kProbScale - 1);
    for (auto& d : data) d = tab.slot_to_symbol[dist(rng)];

    std::vector<uint8_t> bytes;
    bytes.reserve(data.size() + 64);

    const double encode_s = median_seconds(kRepeats, [&] {
        bytes.clear();
        scc::rans::Encoder8 enc(tab);
        enc.reserve(data.size() + 16);
        for (auto it = data.rbegin(); it != data.rend(); ++it) enc.put(*it);
        enc.finish(std::back_inserter(bytes));
    });

    std::vector<uint8_t> decoded(data.size());
    const double decode_s = median_seconds(kRepeats, [&] {
        scc::rans::Decoder8 dec(bytes.data(), bytes.size(), tab);
        for (std::size_t i = 0; i < data.size(); ++i) decoded[i] = dec.get();
    });

    if (decoded != data) {
        std::cerr << "rans8 bench round-trip MISMATCH\n";
        std::exit(1);
    }

    const double mb        = static_cast<double>(data.size()) / (1024.0 * 1024.0);
    const double encode_ms = encode_s * 1000.0;
    const double decode_ms = decode_s * 1000.0;
    std::printf("rANS-8   payload %6.2f MB | encode %7.2f ms (%7.1f MB/s) | decode %7.2f ms (%7.1f MB/s)\n",
                mb, encode_ms, mb / encode_s, decode_ms, mb / decode_s);
}

void bench_rans16() {
    // 1024-symbol alphabet, distribution that uses most of the renorm budget.
    constexpr std::size_t kAlphabet = 1024;
    std::vector<uint32_t> counts(kAlphabet);
    std::mt19937_64 rng_seed(0xB10C5EEDULL ^ 0xA5A5);
    std::uniform_int_distribution<uint32_t> seed_dist(1, 200);
    for (auto& c : counts) c = seed_dist(rng_seed);
    const auto tab = scc::rans::ProbTable16::from_counts(counts.data(), counts.size());

    const std::size_t n_symbols = kPayloadBytes / 2;
    std::vector<uint16_t> data(n_symbols);
    std::mt19937_64 rng(0xB10C5EEDULL ^ 0x99);
    std::uniform_int_distribution<uint32_t> dist(0, scc::rans::kProbScale - 1);
    for (auto& d : data) d = tab.slot_to_symbol[dist(rng)];

    std::vector<uint8_t> bytes;
    bytes.reserve(2 * data.size() + 64);

    const double encode_s = median_seconds(kRepeats, [&] {
        bytes.clear();
        scc::rans::Encoder16 enc(tab);
        enc.reserve(2 * data.size() + 16);
        for (auto it = data.rbegin(); it != data.rend(); ++it) enc.put(*it);
        enc.finish(std::back_inserter(bytes));
    });

    std::vector<uint16_t> decoded(data.size());
    const double decode_s = median_seconds(kRepeats, [&] {
        scc::rans::Decoder16 dec(bytes.data(), bytes.size(), tab);
        for (std::size_t i = 0; i < data.size(); ++i) decoded[i] = dec.get();
    });

    if (decoded != data) {
        std::cerr << "rans16 bench round-trip MISMATCH\n";
        std::exit(1);
    }

    // Throughput is reported on the symbol-byte payload (2 bytes per uint16_t),
    // matching how the SCC frame format costs an R-list payload.
    const double mb        = static_cast<double>(data.size() * sizeof(uint16_t)) / (1024.0 * 1024.0);
    const double encode_ms = encode_s * 1000.0;
    const double decode_ms = decode_s * 1000.0;
    std::printf("rANS-16  payload %6.2f MB | encode %7.2f ms (%7.1f MB/s) | decode %7.2f ms (%7.1f MB/s)\n",
                mb, encode_ms, mb / encode_s, decode_ms, mb / decode_s);
}

} // namespace

int main() {
    std::printf("scc::rans bench (median-of-%d, payload %.0f MiB)\n",
                kRepeats, double(kPayloadBytes) / (1024.0 * 1024.0));
    std::printf("Acceptance gate: encode > 200 MB/s, decode > 250 MB/s.\n\n");
    bench_rans8();
    bench_rans16();
    return 0;
}
