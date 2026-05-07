// bench/bench_quadtree.cpp
//
// Quadtree region-map throughput regression test.
//
// Acceptance gate from AI prompt #4:
//   "Bench: 1280x720 region map built in < 0.5 ms (single-thread, no SIMD
//    required for the histogram step)."
//
// Doubles as the perf-regression CTest entry. Median wall-time is
// budget-gated to budget * 1.10 in NDEBUG / non-sanitizer builds.
//
// Uses a *realistic* synthetic input: most depth values are tightly
// clustered (small inter-frame delta) with a 64x64 patch of significant
// motion. This matches typical depth-camera output. Random Delta with no
// spatial coherence is a pathological worst case and is not the target.

#include "scc/quadtree.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

constexpr int kWidth   = 1280;
constexpr int kHeight  = 720;
constexpr int kRepeats = 64;
constexpr int kWarmup  = 8;
constexpr double kDefaultBudgetMs = 0.5;
constexpr double kSlack           = 1.10;

double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double parse_budget_env() {
    const char* e = std::getenv("SCC_QUADTREE_BUDGET_MS");
    if (!e || !*e) return kDefaultBudgetMs;
    char* end = nullptr;
    const double v = std::strtod(e, &end);
    if (end == e || v <= 0.0) return kDefaultBudgetMs;
    return v;
}

bool budget_enforced() {
#ifdef NDEBUG
  #if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return false;
  #elif defined(__has_feature)
    #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) \
        || __has_feature(memory_sanitizer) || __has_feature(undefined_behavior_sanitizer)
      return false;
    #else
      return true;
    #endif
  #else
    return true;
  #endif
#else
    return false;
#endif
}

void make_realistic_pair(std::vector<uint16_t>& curr,
                         std::vector<uint16_t>& prev) {
    curr.assign(static_cast<size_t>(kWidth) * kHeight, 10000);
    prev.assign(static_cast<size_t>(kWidth) * kHeight, 10000);
    std::mt19937_64 rng(0x000A0BACDEull);

    // Global low-amplitude noise on both frames (simulates sensor noise +
    // sub-pixel registration jitter).
    std::uniform_int_distribution<int> noise(-3, 3);
    for (auto& v : curr) v = static_cast<uint16_t>(static_cast<int>(v) + noise(rng));
    for (auto& v : prev) v = static_cast<uint16_t>(static_cast<int>(v) + noise(rng));

    // Patch of high-amplitude motion in the middle of the frame.
    std::uniform_int_distribution<int> hi(50, 500);
    for (int j = 280; j < 440; ++j) {
        for (int i = 600; i < 760; ++i) {
            curr[static_cast<size_t>(j) * kWidth + i] =
                static_cast<uint16_t>(10000 + hi(rng));
        }
    }
}

} // namespace

int main() {
    std::vector<uint16_t> curr, prev;
    make_realistic_pair(curr, prev);

    auto build = [&] {
        return scc::build_region_map(curr.data(), prev.data(),
                                     kWidth, kHeight,
                                     /*tau_static=*/5,
                                     /*tau_low=*/30,
                                     /*tau_high=*/200,
                                     /*leaf_size_px=*/16);
    };

    for (int i = 0; i < kWarmup; ++i) (void)build();

    std::vector<double> samples;
    samples.reserve(kRepeats);
    std::size_t leaf_count_last = 0;
    for (int i = 0; i < kRepeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        auto rm = build();
        const auto t1 = std::chrono::steady_clock::now();
        leaf_count_last = rm.leaves.size();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    const double median_ms = median(samples);
    const double min_ms    = *std::min_element(samples.begin(), samples.end());
    const double budget    = parse_budget_env();
    const bool   enforce   = budget_enforced();

    std::printf("quadtree %dx%d  median %.3f ms  min %.3f ms  leaves %zu  budget %.2f ms (%senforced)\n",
                kWidth, kHeight,
                median_ms, min_ms, leaf_count_last, budget,
                enforce ? "" : "not ");

    if (enforce && median_ms > budget * kSlack) {
        std::fprintf(stderr,
                     "PERF REGRESSION: median %.3f ms exceeds budget*1.10 = %.3f ms\n",
                     median_ms, budget * kSlack);
        return 2;
    }
    return 0;
}
