// bench/src/report.cpp

#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace scc::bench {

namespace {

// Tiny, dependency-free JSON utilities. The bench needs to read a
// baseline JSON file produced by an earlier invocation of itself --
// the format is stable and constrained, so a hand-rolled scanner is
// simpler than pulling in a JSON library.

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// JSON has no Infinity / NaN literals. We emit `null` for both so the
// output stays parseable by every standard JSON reader. Quality
// thresholds in the gate guard with `> 0` checks, so the round-trip
// "Infinity -> null -> 0.0" still yields a correct gate decision (the
// check is simply skipped, which is what we want for "no signal").
std::string fmt_d(double v) {
    if (std::isinf(v) || std::isnan(v)) return "null";
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(6) << v;
    return os.str();
}

// HTML helpers
std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

} // namespace

void write_csv(std::ostream& os, const std::vector<Row>& rows) {
    os << "codec,profile,width,height,bit_depth,sequence,frame_count,"
          "encode_fps_st,decode_fps_st,encode_fps_mt,decode_fps_mt,"
          "compression_ratio,psnr_db_mean,ssim_mean,point_cloud_diff_pct,error\n";
    for (const auto& r : rows) {
        os << r.codec << ',' << r.profile << ','
           << r.width << ',' << r.height << ',' << static_cast<int>(r.bit_depth) << ','
           << r.sequence << ',' << r.frame_count << ','
           << fmt_d(r.encode_fps_st) << ',' << fmt_d(r.decode_fps_st) << ','
           << fmt_d(r.encode_fps_mt) << ',' << fmt_d(r.decode_fps_mt) << ','
           << fmt_d(r.compression_ratio) << ','
           << fmt_d(r.psnr_db_mean) << ',' << fmt_d(r.ssim_mean) << ','
           << fmt_d(r.point_cloud_diff_pct) << ','
           << '"' << json_escape(r.error) << '"' << '\n';
    }
}

void write_json(std::ostream& os, const ReportMeta& meta, const std::vector<Row>& rows) {
    os << "{\n";
    os << "  \"meta\": {\n";
    os << "    \"scc_version\": \"" << json_escape(meta.scc_version) << "\",\n";
    os << "    \"host\": \""        << json_escape(meta.host)        << "\",\n";
    os << "    \"compiler\": \""    << json_escape(meta.compiler)    << "\",\n";
    os << "    \"build_type\": \""  << json_escape(meta.build_type)  << "\",\n";
    os << "    \"seed\": \""        << json_escape(meta.seed)        << "\",\n";
    os << "    \"runs\": "          << meta.runs                     << ",\n";
    os << "    \"pinned\": "        << (meta.pinned ? "true" : "false") << "\n";
    os << "  },\n";
    os << "  \"rows\": [\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        os << "    {"
           << "\"codec\":\""       << json_escape(r.codec)    << "\","
           << "\"profile\":\""     << json_escape(r.profile)  << "\","
           << "\"width\":"         << r.width                 << ","
           << "\"height\":"        << r.height                << ","
           << "\"bit_depth\":"     << static_cast<int>(r.bit_depth) << ","
           << "\"sequence\":\""    << json_escape(r.sequence) << "\","
           << "\"frame_count\":"   << r.frame_count           << ","
           << "\"encode_fps_st\":" << fmt_d(r.encode_fps_st)  << ","
           << "\"decode_fps_st\":" << fmt_d(r.decode_fps_st)  << ","
           << "\"encode_fps_mt\":" << fmt_d(r.encode_fps_mt)  << ","
           << "\"decode_fps_mt\":" << fmt_d(r.decode_fps_mt)  << ","
           << "\"compression_ratio\":"     << fmt_d(r.compression_ratio) << ","
           << "\"psnr_db_mean\":"          << fmt_d(r.psnr_db_mean)      << ","
           << "\"ssim_mean\":"             << fmt_d(r.ssim_mean)         << ","
           << "\"point_cloud_diff_pct\":"  << fmt_d(r.point_cloud_diff_pct) << ","
           << "\"error\":\""               << json_escape(r.error) << "\""
           << "}";
        if (i + 1 < rows.size()) os << ',';
        os << '\n';
    }
    os << "  ]\n}\n";
}

void write_html(std::ostream& os, const ReportMeta& meta, const std::vector<Row>& rows) {
    os <<
R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>scc-bench report</title>
<style>
  :root { --brand:#0B4884; --accent:#1168BD; --muted:#475569; --bg:#F5F7FA; --ink:#0F172A; }
  body { font: 14px/1.4 -apple-system, "Segoe UI", Roboto, sans-serif; color:var(--ink); background:var(--bg); margin:24px; }
  h1 { color: var(--brand); margin-top: 0; }
  table { border-collapse: collapse; width:100%; background:#fff; border-radius:6px; overflow:hidden; box-shadow: 0 1px 2px rgba(0,0,0,.05); }
  th, td { padding: 8px 10px; text-align: left; border-bottom: 1px solid #e2e8f0; }
  th { background: var(--brand); color: #fff; position: sticky; top: 0; }
  tbody tr:hover { background: #eff6ff; }
  td.num { text-align: right; font-variant-numeric: tabular-nums; }
  .ok { color: #047857; }
  .err { color: #b91c1c; }
  .meta { color: var(--muted); margin-bottom: 16px; }
  .meta dl { display:grid; grid-template-columns: max-content 1fr; gap: 4px 16px; max-width: 600px; }
  .meta dt { font-weight: 600; }
</style>
</head>
<body>
<h1>scc-bench report</h1>
<section class="meta"><dl>
)";
    auto emit_meta = [&](const char* k, const std::string& v) {
        os << "<dt>" << html_escape(k) << "</dt><dd>" << html_escape(v) << "</dd>";
    };
    emit_meta("SCC version", meta.scc_version);
    emit_meta("Host",         meta.host);
    emit_meta("Compiler",     meta.compiler);
    emit_meta("Build type",   meta.build_type);
    emit_meta("Seed",         meta.seed);
    emit_meta("Runs",         std::to_string(meta.runs));
    emit_meta("Pinned",       meta.pinned ? "yes" : "no");
    os << "</dl></section>\n";

    os << "<table><thead><tr>"
          "<th>codec</th><th>profile</th><th>res</th><th>bd</th><th>sequence</th><th>frames</th>"
          "<th>enc fps (st)</th><th>dec fps (st)</th><th>enc fps (mt)</th><th>dec fps (mt)</th>"
          "<th>ratio</th><th>PSNR dB</th><th>SSIM</th><th>PC&nbsp;diff %</th><th>error</th>"
          "</tr></thead><tbody>\n";

    for (const auto& r : rows) {
        os << "<tr>"
           << "<td>" << html_escape(r.codec)    << "</td>"
           << "<td>" << html_escape(r.profile)  << "</td>"
           << "<td>" << r.width << '\xC3' << '\x97' << r.height << "</td>"
           << "<td class=\"num\">" << static_cast<int>(r.bit_depth) << "</td>"
           << "<td>" << html_escape(r.sequence) << "</td>"
           << "<td class=\"num\">" << r.frame_count << "</td>"
           << "<td class=\"num\">" << fmt_d(r.encode_fps_st) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.decode_fps_st) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.encode_fps_mt) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.decode_fps_mt) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.compression_ratio) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.psnr_db_mean) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.ssim_mean) << "</td>"
           << "<td class=\"num\">" << fmt_d(r.point_cloud_diff_pct) << "</td>"
           << "<td class=\"" << (r.error.empty() ? "ok" : "err") << "\">"
           << html_escape(r.error) << "</td>"
           << "</tr>\n";
    }
    os << "</tbody></table>\n</body></html>\n";
}

namespace {

// Tiny scanner-as-needed JSON reader for the baseline file. The
// baseline is produced by write_json above, so the format is constrained
// to: { "meta": {...}, "rows": [ {flat keys}, ... ] }. Robustness
// requirements are minimal -- we don't need to parse arbitrary JSON.

struct BaselineRow {
    std::string codec, profile, sequence;
    int width = 0, height = 0, bit_depth = 0;
    double encode_fps_st = 0, decode_fps_st = 0;
    double encode_fps_mt = 0, decode_fps_mt = 0;
    double compression_ratio = 0;
    double psnr_db_mean = 0, ssim_mean = 0;
};

// Extract the value following "key": from a single object literal. The
// brittle simplification: keys are unique within a row, values are
// either string-quoted or numeric (no nested objects / arrays). Returns
// empty optional on miss.
std::string extract_string(const std::string& obj, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto p = obj.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
    if (p >= obj.size() || obj[p] != '"') return {};
    auto end = obj.find('"', p + 1);
    if (end == std::string::npos) return {};
    return obj.substr(p + 1, end - p - 1);
}
double extract_number(const std::string& obj, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto p = obj.find(needle);
    if (p == std::string::npos) return 0.0;
    p += needle.size();
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
    auto end = p;
    while (end < obj.size() && obj[end] != ',' && obj[end] != '}') ++end;
    try { return std::stod(obj.substr(p, end - p)); }
    catch (...) { return 0.0; }
}

std::vector<BaselineRow> load_baseline(const std::string& path) {
    std::vector<BaselineRow> out;
    std::ifstream in(path);
    if (!in) return out;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string s = buf.str();

    auto rows_pos = s.find("\"rows\"");
    if (rows_pos == std::string::npos) return out;
    // Walk balanced braces row by row.
    auto array_start = s.find('[', rows_pos);
    if (array_start == std::string::npos) return out;
    std::size_t i = array_start + 1;
    while (i < s.size()) {
        // Skip whitespace and commas.
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == ',')) ++i;
        if (i >= s.size() || s[i] == ']') break;
        if (s[i] != '{') { ++i; continue; }
        const std::size_t obj_start = i;
        int depth = 0;
        for (; i < s.size(); ++i) {
            if (s[i] == '{') ++depth;
            else if (s[i] == '}') { --depth; if (depth == 0) { ++i; break; } }
        }
        const std::string obj = s.substr(obj_start, i - obj_start);
        BaselineRow b;
        b.codec    = extract_string(obj, "codec");
        b.profile  = extract_string(obj, "profile");
        b.sequence = extract_string(obj, "sequence");
        b.width      = static_cast<int>(extract_number(obj, "width"));
        b.height     = static_cast<int>(extract_number(obj, "height"));
        b.bit_depth  = static_cast<int>(extract_number(obj, "bit_depth"));
        b.encode_fps_st     = extract_number(obj, "encode_fps_st");
        b.decode_fps_st     = extract_number(obj, "decode_fps_st");
        b.encode_fps_mt     = extract_number(obj, "encode_fps_mt");
        b.decode_fps_mt     = extract_number(obj, "decode_fps_mt");
        b.compression_ratio = extract_number(obj, "compression_ratio");
        b.psnr_db_mean      = extract_number(obj, "psnr_db_mean");
        b.ssim_mean         = extract_number(obj, "ssim_mean");
        out.push_back(std::move(b));
    }
    return out;
}

std::string row_key(const std::string& codec, const std::string& profile,
                    int w, int h, int bd, const std::string& seq) {
    return codec + "|" + profile + "|" + std::to_string(w) + "x" + std::to_string(h)
         + "|" + std::to_string(bd) + "|" + seq;
}

} // namespace

std::string evaluate_gate(const std::string& baseline_json_path,
                          const std::vector<Row>& current) {
    const auto base = load_baseline(baseline_json_path);
    if (base.empty()) {
        return "gate: baseline file empty or unreadable: " + baseline_json_path;
    }
    std::unordered_map<std::string, BaselineRow> idx;
    for (const auto& b : base) {
        idx.emplace(row_key(b.codec, b.profile, b.width, b.height, b.bit_depth, b.sequence), b);
    }
    std::ostringstream report;
    int regressions = 0;

    auto throughput_regression = [&](double cur, double base_v, double tol_frac) {
        if (base_v <= 0) return false;
        return cur < base_v * (1.0 - tol_frac);
    };

    for (const auto& r : current) {
        if (r.codec != "scc") continue;     // we only gate SCC, not the references
        const auto k = row_key(r.codec, r.profile, r.width, r.height, r.bit_depth, r.sequence);
        auto it = idx.find(k);
        if (it == idx.end()) {
            report << "gate: no baseline entry for " << k << "\n";
            ++regressions;
            continue;
        }
        const auto& b = it->second;
        // Throughput: > 5% slower than baseline.
        if (throughput_regression(r.encode_fps_st, b.encode_fps_st, 0.05)) {
            report << "gate: encode_fps_st regression on " << k
                   << " (cur " << r.encode_fps_st << " < base " << b.encode_fps_st << ")\n";
            ++regressions;
        }
        if (throughput_regression(r.decode_fps_st, b.decode_fps_st, 0.05)) {
            report << "gate: decode_fps_st regression on " << k
                   << " (cur " << r.decode_fps_st << " < base " << b.decode_fps_st << ")\n";
            ++regressions;
        }
        // Compression ratio: > 2 percentage points worse (larger ratio == worse).
        if (b.compression_ratio > 0 && r.compression_ratio > b.compression_ratio + 0.02) {
            report << "gate: compression_ratio regression on " << k
                   << " (cur " << r.compression_ratio << " > base " << b.compression_ratio << ")\n";
            ++regressions;
        }
        // PSNR: > 1 dB worse.
        if (b.psnr_db_mean > 0 && r.psnr_db_mean < b.psnr_db_mean - 1.0) {
            report << "gate: psnr_db_mean regression on " << k
                   << " (cur " << r.psnr_db_mean << " < base " << b.psnr_db_mean << ")\n";
            ++regressions;
        }
        // SSIM: > 0.01 worse.
        if (b.ssim_mean > 0 && r.ssim_mean < b.ssim_mean - 0.01) {
            report << "gate: ssim_mean regression on " << k
                   << " (cur " << r.ssim_mean << " < base " << b.ssim_mean << ")\n";
            ++regressions;
        }
    }
    if (regressions == 0) return {};
    return report.str();
}

} // namespace scc::bench
