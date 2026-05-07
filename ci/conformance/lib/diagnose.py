"""One-screen failure summarisation.

Given a `Result` for a failed case, produce a focused diagnostic string
that fits on one screen (~40 lines, ~3 KiB) instead of the full log.
The full stdout/stderr remain in the JSON report and as CI artefacts;
this is the human-readable summary surfaced inline in the HTML report
and in GitHub annotations.

Strategy by pass_criteria type:

    exit_code  -> last 30 lines containing "FAIL", "error:", "assertion",
                  "Expected", "panic", or "undefined reference"; fall back
                  to the absolute last 30 lines if no marker matched.
    regex      -> show 5 lines of context where the pattern *should*
                  appear; if the haystack is short, show the whole thing.
    numeric    -> highlight the line(s) that contained the captured
                  number (or the absence thereof) and call out
                  expected/actual.
"""

from __future__ import annotations

import re

from .schema import PassCriteria
from .runner import Result, FAIL, TIMEOUT, ERROR


_FAIL_MARKERS = re.compile(
    r"(FAIL|FAILED|error:|assertion|Expected|panic(?:!|ked)|undefined reference|"
    r"Segmentation fault|terminate called|^\s*at\s+\S+:\d+|core dumped)",
    re.IGNORECASE,
)

_MAX_LINES = 30
_CONTEXT = 5


def _tail_lines(s: str, n: int) -> list[str]:
    return s.splitlines()[-n:] if s else []


def _select_marked(s: str, n: int) -> list[str]:
    lines = s.splitlines()
    marked = [i for i, ln in enumerate(lines) if _FAIL_MARKERS.search(ln)]
    if not marked:
        return lines[-n:]
    # Take the last `n` marked lines plus 1-line context above each.
    selected: set[int] = set()
    for i in marked[-n:]:
        selected.add(max(0, i - 1))
        selected.add(i)
    return [lines[i] for i in sorted(selected)]


def diagnose(result: Result) -> str:
    """Populate `result.diagnostic` with a focused explanation."""
    if result.status == TIMEOUT:
        return (f"TIMEOUT after {result.duration_s:.1f}s "
                f"(per-case limit {result.case.timeout_seconds}s).\n"
                f"Last stderr lines:\n"
                + "\n".join(_tail_lines(result.stderr_tail, 10)))

    if result.status == ERROR:
        return f"runner ERROR: {result.diagnostic}"

    if result.status == FAIL:
        pc = result.case.pass_criteria
        assert pc is not None
        if pc.type == "exit_code":
            body = "\n".join(_select_marked(result.stdout_tail + "\n" + result.stderr_tail, _MAX_LINES))
            return (f"FAIL: {result.diagnostic}\n"
                    f"--- last marked lines (stdout+stderr) ---\n{body}")
        if pc.type == "regex":
            haystack = _haystack_for(pc, result)
            short = "\n".join(_tail_lines(haystack, _MAX_LINES))
            return (f"FAIL: {result.diagnostic}\n"
                    f"--- expected regex {pc.pattern!r} on {pc.stream} ---\n"
                    f"--- last {_MAX_LINES} lines ---\n{short}")
        if pc.type == "numeric":
            haystack = _haystack_for(pc, result)
            m = re.search(pc.pattern, haystack, re.MULTILINE)
            if not m:
                tail = "\n".join(_tail_lines(haystack, _MAX_LINES))
                return (f"FAIL: extractor {pc.pattern!r} produced no value.\n"
                        f"--- last {_MAX_LINES} lines ---\n{tail}")
            ctx_text = _around(haystack, m.start(), _CONTEXT)
            return (f"FAIL: {result.diagnostic}\n"
                    f"  extracted: {m.group(1)} @ offset {m.start()}\n"
                    f"  expected:  value {pc.comparator} {pc.threshold}\n"
                    f"--- {_CONTEXT}-line context ---\n{ctx_text}")

    return result.diagnostic    # PASS / SKIP — leave evaluator note


def _haystack_for(pc: PassCriteria, r: Result) -> str:
    if pc.stream == "stdout": return r.stdout_tail
    if pc.stream == "stderr": return r.stderr_tail
    return r.stdout_tail + "\n" + r.stderr_tail


def _around(s: str, offset: int, n: int) -> str:
    lines = s.splitlines()
    # Translate byte offset -> line number.
    cur = 0
    line_no = 0
    for i, ln in enumerate(lines):
        if cur + len(ln) >= offset:
            line_no = i
            break
        cur += len(ln) + 1
    lo = max(0, line_no - n)
    hi = min(len(lines), line_no + n + 1)
    return "\n".join(lines[lo:hi])
