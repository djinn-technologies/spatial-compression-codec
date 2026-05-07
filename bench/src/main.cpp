// bench/src/main.cpp
//
// scc-bench: comparator harness CLI.
//
// Argument parsing is hand-rolled (no boost / cxxopts dependency) --
// the surface is small enough that a single switch statement is
// easier to audit than a third-party parser. Errors short-circuit
// with a usage message printed to stderr.
//
// Per Ultrathink #1 the inner loop is identical for every codec;
// the only variation is the codec instance retrieved by name. Per
// Ultrathink #2 the bench runs each (codec, profile, res, bd, seq)
// in two phases:
//   - Phase ST: pinned to one core (Linux) for noise-resistant numbers
//   - Phase MT: thread-parallel across hardware_concurrency() workers
// Both throughput numbers are reported in the same row.
//
// Per Ultrathink #5 the gate compares medians, not means, against a
// committed baseline JSON. CI runs the gate on the tiny synthetic
// corpus (320x240, 30 frames) at every PR.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(SCC_BENCH_HAS_AFFINITY)
    #include <pthread.h>
    #include <sched.h>
#endif

#include "codecs/codec.hpp"
#include "corpus.hpp"
#include "frame.hpp"
#include "metrics.hpp"
#include "report.hpp"

namespace fs = std::filesystem;
using namespace scc::bench;

namespace {

struct Args {
    std::string corpus_dir;
    std::vector<std::string> codecs    { "scc", "zlib" };
    std::vector<std::string> profiles  { "lossless" };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> resolutions
        { {320, 240} };
    std::vector<std::uint8_t> bit_depths { 12 };
    std::string out_dir = "bench-out";
    std::vector<std::string> formats { "csv", "json", "html" };
    int          runs   = 5;
    std::uint64_t seed  = 0xC0FFEEull;
    bool         no_pin = false;
    std::string  gate_baseline;
};

[[noreturn]] void usage_and_exit(const char* msg) {
    if (msg) std::fprintf(stderr, "scc-bench: %s\n", msg);
    std::fprintf(stderr,
        "Usage:\n"
        "  scc-bench --corpus DIR \\\n"
        "            --codecs scc,jpeg2000,png,tiff,zlib \\\n"
        "            --profiles lossless,lossy:high,lossy:streaming \\\n"
        "            --resolutions 640x480,1280x720 \\\n"
        "            --bit-depths 8,12,16 \\\n"
        "            --runs N \\\n"
        "            --seed 0xCAFEBABE \\\n"
        "            --out DIR \\\n"
        "            --format csv,json,html \\\n"
        "            [--no-pin] \\\n"
        "            [--gate baseline.json]\n");
    std::exit(2);
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::pair<std::uint32_t, std::uint32_t> parse_res(const std::string& s) {
    auto x = s.find('x');
    if (x == std::string::npos) usage_and_exit(("bad resolution: " + s).c_str());
    return { static_cast<std::uint32_t>(std::stoul(s.substr(0, x))),
             static_cast<std::uint32_t>(std::stoul(s.substr(x + 1))) };
}

std::uint64_t parse_seed(const std::string& s) {
    if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
        return std::stoull(s.substr(2), nullptr, 16);
    }
    return std::stoull(s, nullptr, 10);
}

Args parse_args(int argc, char** argv) {
    Args a;
    auto need = [&](int& i) {
        if (i + 1 >= argc) usage_and_exit("missing argument");
        return std::string{argv[++i]};
    };
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if      (k == "--corpus")      a.corpus_dir = need(i);
        else if (k == "--codecs")      a.codecs     = split_csv(need(i));
        else if (k == "--profiles")    a.profiles   = split_csv(need(i));
        else if (k == "--resolutions") {
            a.resolutions.clear();
            for (auto& r : split_csv(need(i))) a.resolutions.push_back(parse_res(r));
        }
        else if (k == "--bit-depths")  {
            a.bit_depths.clear();
            for (auto& b : split_csv(need(i))) a.bit_depths.push_back(static_cast<std::uint8_t>(std::stoi(b)));
        }
        else if (k == "--out")         a.out_dir = need(i);
        else if (k == "--format")      a.formats = split_csv(need(i));
        else if (k == "--runs")        a.runs    = std::max(1, std::stoi(need(i)));
        else if (k == "--seed")        a.seed    = parse_seed(need(i));
        else if (k == "--no-pin")      a.no_pin  = true;
        else if (k == "--gate")        a.gate_baseline = need(i);
        else if (k == "--help" || k == "-h") usage_and_exit(nullptr);
        else usage_and_exit(("unknown flag: " + k).c_str());
    }
    return a;
}

// Pin the calling thread to CPU 0. Returns true on success.
bool pin_to_cpu0() {
#if defined(SCC_BENCH_HAS_AFFINITY)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    return false;
#endif
}

double seconds_since(std::chrono::steady_clock::time_point t0) {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now() - t0).count();
}

double median(std::vector<double> xs) {
    if (xs.empty()) return 0.0;
    std::sort(xs.begin(), xs.end());
    const auto n = xs.size();
    return (n & 1u) ? xs[n / 2] : 0.5 * (xs[n / 2 - 1] + xs[n / 2]);
}

struct Times {
    double encode_seconds = 0.0;
    double decode_seconds = 0.0;
    std::size_t total_bytes = 0;
    std::vector<std::vector<std::uint8_t>> encoded;   // per frame
    std::string error;
};

// Single-thread phase: same instance encodes/decodes all frames in
// order. Returns once on success; populates Times.
Times run_st(ICodec& codec, const Sequence& seq) {
    Times t;
    t.encoded.reserve(seq.frames.size());

    // Warmup: one encode/decode round to amortise first-call dispatch.
    if (!seq.frames.empty()) {
        auto enc = codec.encode(seq.frames.front());
        if (!enc.error.empty()) { t.error = enc.error; return t; }
        auto dec = codec.decode(enc.bytes.data(), enc.bytes.size(),
                                seq.frames.front().width, seq.frames.front().height,
                                seq.frames.front().bit_depth);
        if (!dec.error.empty()) { t.error = dec.error; return t; }
    }
    codec.reset();

    // Encode timing.
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& f : seq.frames) {
        auto enc = codec.encode(f);
        if (!enc.error.empty()) { t.error = enc.error; return t; }
        t.total_bytes += enc.bytes.size();
        t.encoded.push_back(std::move(enc.bytes));
    }
    t.encode_seconds = seconds_since(t0);

    // Decode timing.
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < seq.frames.size(); ++i) {
        auto dec = codec.decode(t.encoded[i].data(), t.encoded[i].size(),
                                seq.frames[i].width, seq.frames[i].height,
                                seq.frames[i].bit_depth);
        if (!dec.error.empty()) { t.error = dec.error; return t; }
        // Discard decoded frame for ST timing -- quality metrics are
        // computed in a separate pass on the encoded bytes so the
        // timing loop stays cache-friendly.
    }
    t.decode_seconds = seconds_since(t0);
    return t;
}

// Multi-thread phase: split the sequence into roughly equal chunks
// across `worker_count` threads, each with its own codec instance.
// Wall-clock = max worker time; we report frames / wall-clock.
struct MtTimes {
    double encode_wall = 0.0;
    double decode_wall = 0.0;
    std::string error;
};

MtTimes run_mt(const std::string& codec_name,
               const std::string& profile,
               const Sequence& seq,
               unsigned worker_count) {
    MtTimes mt;
    if (worker_count < 2 || seq.frames.size() < 2) {
        // Trivial single-worker path -- caller will fall back to ST data.
        return mt;
    }
    // Per-worker pre-allocated codec instances.
    std::vector<std::unique_ptr<ICodec>> codecs;
    for (unsigned w = 0; w < worker_count; ++w) {
        auto c = make_codec_by_name(codec_name);
        if (!c) { mt.error = "make_codec_by_name returned null"; return mt; }
        const auto& f0 = seq.frames.front();
        std::string e = c->configure(profile, f0.width, f0.height, f0.bit_depth);
        if (!e.empty()) { mt.error = e; return mt; }
        codecs.push_back(std::move(c));
    }

    // Round-robin frame assignment: avoids pathological hot-spots when
    // codec cost is content-dependent (e.g. quad-tree depth varies).
    std::vector<std::vector<std::vector<std::uint8_t>>> encoded_per_worker(worker_count);
    std::atomic<std::string*> err_signal { nullptr };
    std::vector<std::string> errors(worker_count);

    auto encode_worker = [&](unsigned w) {
        auto& my_enc = encoded_per_worker[w];
        for (std::size_t i = w; i < seq.frames.size(); i += worker_count) {
            auto er = codecs[w]->encode(seq.frames[i]);
            if (!er.error.empty()) {
                errors[w] = er.error;
                err_signal.store(&errors[w]);
                return;
            }
            my_enc.push_back(std::move(er.bytes));
        }
    };
    auto decode_worker = [&](unsigned w) {
        auto& my_enc = encoded_per_worker[w];
        std::size_t k = 0;
        for (std::size_t i = w; i < seq.frames.size(); i += worker_count, ++k) {
            const auto& f = seq.frames[i];
            auto dr = codecs[w]->decode(my_enc[k].data(), my_enc[k].size(),
                                        f.width, f.height, f.bit_depth);
            if (!dr.error.empty()) {
                errors[w] = dr.error;
                err_signal.store(&errors[w]);
                return;
            }
        }
    };

    // Encode wall-clock.
    auto t0 = std::chrono::steady_clock::now();
    {
        std::vector<std::thread> ts;
        for (unsigned w = 0; w < worker_count; ++w) ts.emplace_back(encode_worker, w);
        for (auto& t : ts) t.join();
    }
    mt.encode_wall = seconds_since(t0);
    if (auto* p = err_signal.load(); p && !p->empty()) { mt.error = *p; return mt; }

    // Decode wall-clock.
    t0 = std::chrono::steady_clock::now();
    {
        std::vector<std::thread> ts;
        for (unsigned w = 0; w < worker_count; ++w) ts.emplace_back(decode_worker, w);
        for (auto& t : ts) t.join();
    }
    mt.decode_wall = seconds_since(t0);
    if (auto* p = err_signal.load(); p && !p->empty()) { mt.error = *p; return mt; }
    return mt;
}

// Compute quality metrics by re-encoding once with a fresh codec and
// decoding into materialised frames. Kept out of the timing loops on
// purpose -- quality is invariant of timing, and including the metric
// pass in the timed loop would skew throughput.
struct QualityResult {
    double psnr_mean = 0.0;
    double ssim_mean = 0.0;
    double pcd_pct   = 0.0;     // [0, 100]
    double comp_ratio = 0.0;
    std::string error;
};

QualityResult measure_quality(ICodec& codec,
                              const Sequence& seq,
                              const Intrinsics& intr) {
    QualityResult q;
    if (seq.frames.empty()) return q;
    long double psnr_sum = 0.0L, ssim_sum = 0.0L, pcd_sum = 0.0L;
    std::size_t total_payload = 0;
    std::size_t total_raw     = 0;

    codec.reset();
    for (const auto& f : seq.frames) {
        auto enc = codec.encode(f);
        if (!enc.error.empty()) { q.error = enc.error; return q; }
        total_payload += enc.bytes.size();
        total_raw     += f.raw_size_bytes();

        auto dec = codec.decode(enc.bytes.data(), enc.bytes.size(),
                                f.width, f.height, f.bit_depth);
        if (!dec.error.empty()) { q.error = dec.error; return q; }
        if (dec.frame.width != f.width || dec.frame.height != f.height) {
            q.error = "decoded dims differ from source";
            return q;
        }
        psnr_sum += metrics::psnr(f, dec.frame);
        ssim_sum += metrics::ssim(f, dec.frame);
        pcd_sum  += metrics::point_cloud_diff(f, dec.frame, intr, 0.005);     // 5 mm tolerance
    }
    const double n = static_cast<double>(seq.frames.size());
    q.psnr_mean   = static_cast<double>(psnr_sum / n);
    q.ssim_mean   = static_cast<double>(ssim_sum / n);
    q.pcd_pct     = 100.0 * static_cast<double>(pcd_sum / n);
    q.comp_ratio  = total_raw ? static_cast<double>(total_payload) / static_cast<double>(total_raw) : 0.0;
    return q;
}

std::string detect_compiler() {
#if defined(__clang__)
    return "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

std::string detect_build_type() {
#if defined(NDEBUG)
    return "Release-like (NDEBUG defined)";
#else
    return "Debug-like (NDEBUG NOT defined)";
#endif
}

} // namespace

int main(int argc, char** argv) try {
    Args args = parse_args(argc, argv);

    // Build the synthetic corpus deterministically from --seed. The
    // committed gate (CI) runs against this; the path to --corpus is
    // currently informational for the report metadata. Future work:
    // load real-world manifests from corpus_dir/manifest.json.
    std::vector<Sequence> all_sequences;
    for (auto [w, h] : args.resolutions) {
        for (auto bd : args.bit_depths) {
            CorpusSpec spec;
            spec.width = w; spec.height = h; spec.bit_depth = bd;
            spec.frames = 30; spec.seed = args.seed;
            for (auto& s : synthesize_corpus(spec)) all_sequences.push_back(std::move(s));
        }
    }

    const bool pinned = !args.no_pin && pin_to_cpu0();

    ReportMeta meta;
    meta.scc_version = "scc-bench/1.0";
    meta.host        = "scc-bench";
    meta.compiler    = detect_compiler();
    meta.build_type  = detect_build_type();
    {
        std::ostringstream os; os << "0x" << std::hex << args.seed;
        meta.seed = os.str();
    }
    meta.runs   = args.runs;
    meta.pinned = pinned;

    std::vector<Row> rows;

    const unsigned hw_threads = std::max(1u, std::thread::hardware_concurrency());
    const unsigned mt_workers = std::min(8u, hw_threads);

    for (const auto& codec_name : args.codecs) {
        auto probe = make_codec_by_name(codec_name);
        if (!probe) {
            // Fail loud per the prompt's requirement.
            std::fprintf(stderr,
                "scc-bench: unknown / unlinked codec: %s\n", codec_name.c_str());
            return 3;
        }
        for (const auto& profile : args.profiles) {
            for (auto [w, h] : args.resolutions) {
                for (auto bd : args.bit_depths) {
                    // Find the matching synthesized sequences.
                    for (const auto& seq : all_sequences) {
                        if (seq.frames.empty()) continue;
                        if (seq.frames.front().width != w
                            || seq.frames.front().height != h
                            || seq.frames.front().bit_depth != bd) continue;

                        auto codec = make_codec_by_name(codec_name);
                        std::string cfg_err = codec->configure(profile, w, h, bd);

                        Row row;
                        row.codec = codec_name;
                        row.profile = profile;
                        row.width = w; row.height = h; row.bit_depth = bd;
                        row.sequence = seq.name;
                        row.frame_count = static_cast<std::uint32_t>(seq.frames.size());

                        if (!cfg_err.empty()) {
                            row.error = cfg_err;
                            rows.push_back(std::move(row));
                            continue;
                        }

                        // Phase ST: median across `runs` repetitions.
                        std::vector<double> enc_st, dec_st;
                        std::string st_err;
                        for (int r = 0; r < args.runs; ++r) {
                            auto t = run_st(*codec, seq);
                            if (!t.error.empty()) { st_err = t.error; break; }
                            enc_st.push_back(static_cast<double>(seq.frames.size()) / std::max(1e-9, t.encode_seconds));
                            dec_st.push_back(static_cast<double>(seq.frames.size()) / std::max(1e-9, t.decode_seconds));
                        }
                        if (!st_err.empty()) {
                            row.error = st_err;
                            rows.push_back(std::move(row));
                            continue;
                        }
                        row.encode_fps_st = median(enc_st);
                        row.decode_fps_st = median(dec_st);

                        // Phase MT: one repetition (multi-thread variance is
                        // already amortised over workers; medianing wastes time).
                        auto mt = run_mt(codec_name, profile, seq, mt_workers);
                        if (!mt.error.empty()) {
                            row.error = mt.error;
                            rows.push_back(std::move(row));
                            continue;
                        }
                        if (mt.encode_wall > 0.0) {
                            row.encode_fps_mt = static_cast<double>(seq.frames.size()) / mt.encode_wall;
                            row.decode_fps_mt = static_cast<double>(seq.frames.size()) / mt.decode_wall;
                        } else {
                            row.encode_fps_mt = row.encode_fps_st;
                            row.decode_fps_mt = row.decode_fps_st;
                        }

                        // Quality + compression ratio.
                        auto intr = default_intrinsics(w, h);
                        auto q = measure_quality(*codec, seq, intr);
                        if (!q.error.empty()) {
                            row.error = q.error;
                        } else {
                            row.psnr_db_mean         = q.psnr_mean;
                            row.ssim_mean            = q.ssim_mean;
                            row.point_cloud_diff_pct = q.pcd_pct;
                            row.compression_ratio    = q.comp_ratio;
                        }
                        rows.push_back(std::move(row));
                    }
                }
            }
        }
    }

    // Emit reports.
    fs::create_directories(args.out_dir);
    bool wrote_csv = false, wrote_json = false, wrote_html = false;
    for (const auto& f : args.formats) {
        if (f == "csv") {
            std::ofstream o(fs::path(args.out_dir) / "report.csv");
            write_csv(o, rows); wrote_csv = true;
        } else if (f == "json") {
            std::ofstream o(fs::path(args.out_dir) / "report.json");
            write_json(o, meta, rows); wrote_json = true;
        } else if (f == "html") {
            std::ofstream o(fs::path(args.out_dir) / "report.html");
            write_html(o, meta, rows); wrote_html = true;
        }
    }
    if (!(wrote_csv || wrote_json || wrote_html)) {
        std::fprintf(stderr, "scc-bench: no output formats selected\n");
        return 4;
    }

    // Print a one-line summary so stdout is informative when run from CI.
    std::printf("scc-bench: %zu rows, output in %s\n",
                rows.size(), fs::absolute(args.out_dir).string().c_str());

    if (!args.gate_baseline.empty()) {
        const std::string gate_msg = evaluate_gate(args.gate_baseline, rows);
        if (!gate_msg.empty()) {
            std::fprintf(stderr, "scc-bench: REGRESSION DETECTED\n%s\n", gate_msg.c_str());
            return 5;
        }
        std::printf("scc-bench: gate passed against %s\n", args.gate_baseline.c_str());
    }
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "scc-bench: unhandled exception: %s\n", e.what());
    return 1;
} catch (...) {
    std::fprintf(stderr, "scc-bench: unhandled non-exception failure\n");
    return 1;
}
