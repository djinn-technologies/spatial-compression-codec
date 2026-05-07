// bench/bench_disparity.cpp
//
// Disparity transform throughput regression test.
//
// Acceptance gate from AI prompt #2:
//   "forward + inverse on 1280x720 16bpp completes in < 1.5 ms on a
//    Ryzen 5600X (single-threaded, AVX2 path)."
//
// Doubles as the perf-regression CTest entry (Ultrathink #5). Exits non-zero
// if median wall-time exceeds budget * 1.10. The 10% slack absorbs ordinary
// scheduler / boost-clock jitter across CI runs without masking real
// regressions.
//
// In Debug or sanitizer builds the timing is meaningless, so the assertion
// is suppressed (we still run the round-trip to catch correctness bugs).

#include "scc/disparity.hpp"
#include "scc/detail/disparity_impls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr int kWidth   = 1280;
constexpr int kHeight  = 720;
constexpr int kRepeats = 64;
constexpr int kWarmup  = 8;
constexpr double kDefaultBudgetMs = 1.5;
constexpr double kSlack           = 1.10;

const char* dispatch_name(scc::detail::DispatchKind k) {
    switch (k) {
    case scc::detail::DispatchKind::Avx2:   return "AVX2";
    case scc::detail::DispatchKind::Neon:   return "NEON";
    case scc::detail::DispatchKind::Scalar: return "scalar";
    }
    return "?";
}

double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double parse_budget_env() {
    const char* e = std::getenv("SCC_DISPARITY_BUDGET_MS");
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

} // namespace

int main() {
    std::vector<uint16_t> d(static_cast<std::size_t>(kWidth) * kHeight);
    std::mt19937_64 rng(0xD15A12U);
    std::uniform_int_distribution<uint32_t> dist(0, 65535);
    for (auto& v : d) v = static_cast<uint16_t>(dist(rng));

    std::vector<int16_t>  s(d.size());
    std::vector<uint16_t> d2(d.size());

    auto roundtrip = [&] {
        scc::disparity_forward(d.data(), kWidth, kHeight, s.data());
        scc::disparity_inverse(s.data(), kWidth, kHeight, d2.data());
    };

    for (int i = 0; i < kWarmup; ++i) roundtrip();

    std::vector<double> samples;
    samples.reserve(kRepeats);
    for (int i = 0; i < kRepeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        roundtrip();
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    if (d2 != d) {
        std::fprintf(stderr, "disparity bench: ROUND-TRIP FAIL\n");
        return 1;
    }

    const double median_ms = median(samples);
    const double min_ms    = *std::min_element(samples.begin(), samples.end());
    const double budget    = parse_budget_env();
    const bool   enforce   = budget_enforced();

    std::printf("disparity %dx%d (%s)  median %.3f ms  min %.3f ms  budget %.2f ms (%senforced)\n",
                kWidth, kHeight,
                dispatch_name(scc::detail::active_dispatch_kind()),
                median_ms, min_ms, budget,
                enforce ? "" : "not ");

    if (enforce && median_ms > budget * kSlack) {
        std::fprintf(stderr,
                     "PERF REGRESSION: median %.3f ms exceeds budget*1.10 = %.3f ms\n",
                     median_ms, budget * kSlack);
        return 2;
    }
    return 0;
}
