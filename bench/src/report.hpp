// bench/src/report.hpp
//
// Report row + writers for CSV / JSON / HTML.
//
// The CSV is the canonical machine-readable form (one row per
// measurement); JSON is the same data with a metadata header; HTML is
// a human-readable rendering. All three are written from the same
// Row[] vector to keep them in sync by construction.
//
// The regression-gate compares the JSON form of a fresh run against a
// committed baseline JSON; thresholds are tuned per Ultrathink #5.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace scc::bench {

struct Row {
    std::string  codec;
    std::string  profile;
    std::uint32_t width      = 0;
    std::uint32_t height     = 0;
    std::uint8_t  bit_depth  = 0;
    std::string  sequence;
    std::uint32_t frame_count = 0;

    // Throughput aggregates (median over `runs`).
    double encode_fps_st     = 0.0;
    double decode_fps_st     = 0.0;
    double encode_fps_mt     = 0.0;
    double decode_fps_mt     = 0.0;

    // Compression: total payload bytes / total raw bytes (smaller is
    // better). E.g. 0.20 = 5x ratio.
    double compression_ratio = 0.0;

    // Quality (per-frame mean over the sequence).
    double psnr_db_mean      = 0.0;
    double ssim_mean         = 0.0;
    double point_cloud_diff_pct = 0.0;     // [0, 100], moved-fraction * 100

    std::string error;       // first-encountered error string, if any
};

struct ReportMeta {
    std::string scc_version;
    std::string host;
    std::string compiler;
    std::string build_type;
    std::string seed;        // hex string ("0xC0FFEE")
    int         runs   = 0;
    bool        pinned = false;
};

void write_csv (std::ostream& os, const std::vector<Row>& rows);
void write_json(std::ostream& os, const ReportMeta& meta, const std::vector<Row>& rows);
void write_html(std::ostream& os, const ReportMeta& meta, const std::vector<Row>& rows);

// Regression gate: compare `current` against `baseline_json_path`.
// Returns "" on pass, otherwise a human-readable list of the rows that
// regressed. Thresholds:
//   - throughput regression > 5%   (median, both ST and MT)
//   - compression-ratio regression > 2 percentage points
//   - quality regression > 1 dB PSNR or > 0.01 SSIM
std::string evaluate_gate(const std::string& baseline_json_path,
                          const std::vector<Row>& current);

} // namespace scc::bench
