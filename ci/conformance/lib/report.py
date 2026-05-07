"""Reports: HTML + JSON + GitHub annotations.

The JSON schema is API-stable (per Ultrathink #2) — every field is
always present, missing data is `null`, floats are rounded to 3
decimals, timestamps are ISO-8601 UTC. A schema_version field at the
top tracks breaking changes; downstream dashboards pin on it.

The HTML groups cases by SAD section, links each row back to its
source YAML, and visually distinguishes the four states (Ultrathink #1).
"""

from __future__ import annotations

import html
import json
import os
import sys
from collections import defaultdict
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path

from .runner import Result, PASS, FAIL, TIMEOUT, ERROR, SKIP


JSON_SCHEMA_VERSION = "1.0.0"


# --- JSON ---------------------------------------------------------------

def _round(v):
    if isinstance(v, float):
        return round(v, 3)
    return v


def _row_json(r: Result) -> dict:
    pc = r.case.pass_criteria
    return {
        "id":            r.case.id,
        "description":   r.case.description,
        "sad_section":   r.case.sad_section,
        "evidence":      r.case.evidence,
        "evidence_paths": list(r.case.evidence_paths),
        "blocking":      bool(r.case.blocking),
        "pass_criteria": {
            "type":       pc.type if pc else None,
            "expected":   pc.expected if pc else None,
            "pattern":    pc.pattern if pc else None,
            "stream":     pc.stream if pc else None,
            "comparator": pc.comparator if pc else None,
            "threshold":  _round(pc.threshold) if pc else None,
        },
        "test_command":  r.case.test_command,
        "timeout_seconds": int(r.case.timeout_seconds),
        "status":        r.status,
        "started_at":    r.started_at or None,
        "duration_s":    _round(r.duration_s),
        "exit_code":     r.exit_code,
        "diagnostic":    r.diagnostic,
    }


def write_json(results: list[Result], out_path: Path, *, suite_started: str) -> None:
    payload = {
        "schema_version": JSON_SCHEMA_VERSION,
        "suite": {
            "started_at": suite_started,
            "completed_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "totals": tally(results),
        },
        "rows": [_row_json(r) for r in sorted(results, key=lambda r: r.case.numeric_id)],
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=False), encoding="utf-8")


def tally(results: list[Result]) -> dict:
    out = {PASS: 0, FAIL: 0, TIMEOUT: 0, ERROR: 0, SKIP: 0,
           "blocking_fail": 0, "total": len(results)}
    for r in results:
        out[r.status] = out.get(r.status, 0) + 1
        if r.case.blocking and r.status in (FAIL, ERROR):
            out["blocking_fail"] += 1
    return out


# --- HTML ---------------------------------------------------------------

_HTML_HEAD = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>SCC conformance report</title>
<style>
  :root {
    --brand:#0B4884; --accent:#1168BD; --muted:#475569;
    --bg:#F5F7FA; --ink:#0F172A;
    --pass:#047857; --fail:#b91c1c; --warn:#b45309; --to:#6d28d9;
  }
  body { font: 14px/1.45 -apple-system, "Segoe UI", Roboto, sans-serif;
         color:var(--ink); background:var(--bg); margin:24px; }
  h1 { color:var(--brand); margin:0 0 8px 0; }
  h2 { color:var(--brand); margin-top:32px; padding-bottom:4px;
       border-bottom:1px solid #cbd5e1; }
  .summary { display:grid; grid-template-columns:repeat(6,minmax(0,1fr));
             gap:8px; margin:16px 0 24px; }
  .summary div { background:#fff; border-radius:6px; padding:10px 12px;
                 box-shadow:0 1px 2px rgba(0,0,0,.05); }
  .summary strong { display:block; font-size:22px; line-height:1; }
  .summary span { color:var(--muted); font-size:12px; }
  .filter { margin: 8px 0 16px; }
  .filter label { background:#fff; padding:4px 10px; border-radius:6px;
                  border:1px solid #cbd5e1; cursor:pointer; }
  table { border-collapse:collapse; width:100%; background:#fff;
          border-radius:6px; overflow:hidden;
          box-shadow:0 1px 2px rgba(0,0,0,.05); }
  th, td { padding:8px 10px; border-bottom:1px solid #e2e8f0; vertical-align:top; }
  th { background:var(--brand); color:#fff; text-align:left; }
  td.id { font-family: ui-monospace, Consolas, monospace; white-space:nowrap; }
  td.dur { font-variant-numeric: tabular-nums; text-align:right; }
  .status { display:inline-block; padding:2px 8px; border-radius:4px;
            font-weight:600; font-size:12px; }
  .s-PASS    { color:#fff; background:var(--pass); }
  .s-FAIL    { color:#fff; background:var(--fail); }
  .s-TIMEOUT { color:#fff; background:var(--to); }
  .s-ERROR   { color:#fff; background:var(--fail); }
  .s-SKIP    { color:#fff; background:var(--muted); }
  tr.block-fail td { box-shadow: inset 4px 0 0 0 var(--fail); }
  tr.nonblock-fail td { box-shadow: inset 4px 0 0 0 var(--warn);
                       border-left-style: dashed; }
  tr.timeout td  { box-shadow: inset 4px 0 0 0 var(--to); }
  details summary { cursor:pointer; color:var(--accent); }
  details pre { background:#0f172a; color:#e2e8f0; padding:12px;
                border-radius:6px; overflow:auto; max-height:480px;
                font-size:12px; line-height:1.4; }
  .lock::before { content:"\\1F512"; margin-right:4px; }
  .nb::before { content:"\\26A0"; margin-right:4px; opacity:.7; }
  .hide-non-blocking tr.nonblock-pass,
  .hide-non-blocking tr.nonblock-fail { display:none; }
</style>
<script>
  function toggleNonBlocking(){
    document.body.classList.toggle('hide-non-blocking');
  }
</script>
</head><body>
"""

_HTML_FOOT = """
<p style="margin-top:32px; color:var(--muted); font-size:12px;">
Generated by <code>ci/conformance/run.py</code>. JSON schema:
%(schema)s. See <a href="../../docs/SAD.md#14-conformance">SAD §14</a>
and <a href="../../docs/adr/ADR-015-conformance-suite.md">ADR-015</a>.
</p>
</body></html>
"""


def _row_class(r: Result) -> str:
    if r.status == PASS:
        return "block-pass" if r.case.blocking else "nonblock-pass"
    if r.status == FAIL or r.status == ERROR:
        return "block-fail" if r.case.blocking else "nonblock-fail"
    if r.status == TIMEOUT:
        return "timeout"
    return ""


def _row_html(r: Result, yaml_href: str) -> str:
    badge = f'<span class="status s-{r.status}">{r.status}</span>'
    blocked = ('<span class="lock" title="blocking"></span>' if r.case.blocking
               else '<span class="nb" title="non-blocking"></span>')
    diag = html.escape(r.diagnostic or "")
    stdout = html.escape(r.stdout_tail or "")
    stderr = html.escape(r.stderr_tail or "")
    cmd = html.escape(r.case.test_command)
    descr = html.escape(r.case.description)
    yaml_link = f'<a href="{html.escape(yaml_href)}">{html.escape(r.case.id)}</a>'
    return (
        f'<tr class="{_row_class(r)}">'
        f'<td class="id">{blocked}{yaml_link}</td>'
        f'<td>{badge}</td>'
        f'<td>{descr}</td>'
        f'<td class="dur">{r.duration_s:.2f}s</td>'
        f'<td><details><summary>details</summary>'
        f'<p><strong>command:</strong> <code>{cmd}</code></p>'
        f'<p><strong>diagnostic:</strong></p><pre>{diag}</pre>'
        f'<p><strong>stdout (tail):</strong></p><pre>{stdout}</pre>'
        f'<p><strong>stderr (tail):</strong></p><pre>{stderr}</pre>'
        f'</details></td>'
        f'</tr>'
    )


def write_html(results: list[Result], out_path: Path, *,
               suite_started: str,
               cases_dir_rel: str = "../cases") -> None:
    totals = tally(results)
    by_section: dict[str, list[Result]] = defaultdict(list)
    for r in results:
        by_section[r.case.sad_section].append(r)

    parts: list[str] = []
    parts.append(_HTML_HEAD)
    parts.append("<h1>SCC conformance report</h1>")
    parts.append(f'<p style="color:var(--muted);">started {html.escape(suite_started)} · '
                 f'{totals["total"]} cases</p>')
    parts.append('<div class="summary">'
                 f'<div><strong>{totals[PASS]}</strong><span>PASS</span></div>'
                 f'<div><strong>{totals[FAIL]}</strong><span>FAIL</span></div>'
                 f'<div><strong>{totals[TIMEOUT]}</strong><span>TIMEOUT</span></div>'
                 f'<div><strong>{totals[ERROR]}</strong><span>ERROR</span></div>'
                 f'<div><strong>{totals[SKIP]}</strong><span>SKIP</span></div>'
                 f'<div><strong>{totals["blocking_fail"]}</strong><span>blocking failures</span></div>'
                 '</div>')
    parts.append('<div class="filter"><label><input type="checkbox" '
                 'onclick="toggleNonBlocking()"> Hide non-blocking '
                 'rows</label></div>')

    for section in sorted(by_section.keys()):
        rows = sorted(by_section[section], key=lambda r: r.case.numeric_id)
        parts.append(f'<h2>{html.escape(section)}</h2>')
        parts.append('<table><thead><tr>'
                     '<th>REQ</th><th>status</th><th>description</th>'
                     '<th>dur</th><th>diag</th></tr></thead><tbody>')
        for r in rows:
            href = f"{cases_dir_rel}/{r.case.path.name}"
            parts.append(_row_html(r, href))
        parts.append('</tbody></table>')

    parts.append(_HTML_FOOT % {"schema": JSON_SCHEMA_VERSION})
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("".join(parts), encoding="utf-8")


# --- GitHub annotations -------------------------------------------------

def emit_github_annotations(results: list[Result], stream=sys.stdout) -> None:
    """Emit `::error|warning file=...,title=...::message` workflow commands.

    Blocking FAIL/ERROR -> ::error.
    Non-blocking FAIL/ERROR or TIMEOUT -> ::warning.
    Successful runs emit nothing (otherwise the PR review pane drowns).
    """
    for r in results:
        if r.status in (PASS, SKIP):
            continue
        level = "error" if (r.case.blocking and r.status in (FAIL, ERROR)) else "warning"
        title = f"{r.case.id} {r.status}: {r.case.description[:80]}"
        # The first line of the diagnostic, escaped per the spec.
        msg_first = (r.diagnostic or "").splitlines()[:1]
        message = msg_first[0] if msg_first else r.status
        # Escape per https://github.com/actions/toolkit
        for ch in ("%", "\r", "\n"):
            message = message.replace(ch, "%25" if ch == "%" else ("%0D" if ch == "\r" else "%0A"))
            title = title.replace(ch, "%25" if ch == "%" else ("%0D" if ch == "\r" else "%0A"))
        file_arg = ""
        if r.case.evidence_paths:
            # Annotate the first evidence path so the PR diff highlights it.
            file_arg = f"file={r.case.evidence_paths[0]},"
        print(f"::{level} {file_arg}title={title}::{message}", file=stream)


def write_summary_txt(results: list[Result], out_path: Path) -> None:
    """One-liner that the GH workflow appends to $GITHUB_STEP_SUMMARY."""
    t = tally(results)
    lines = [f"# SCC conformance: {t[PASS]} / {t['total']} REQs PASS, "
             f"{t[FAIL]} FAIL, {t[TIMEOUT]} TIMEOUT, {t[ERROR]} ERROR, {t[SKIP]} SKIP"]
    if t["blocking_fail"]:
        lines.append(f"\n**{t['blocking_fail']} blocking failure(s).**")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
