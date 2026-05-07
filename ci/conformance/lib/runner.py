"""Subprocess execution with per-case + global wallclock budgets.

Cases are run in a ThreadPoolExecutor. We stay with threads (not
processes) because each case is itself a subprocess; the threading
layer just shepherds the timeouts. ``max_workers`` defaults to 4 to
match GitHub-hosted runner cores.

Status taxonomy:
    PASS    — pass_criteria evaluated to True
    FAIL    — pass_criteria evaluated to False (this includes "regex
              didn't match", "exit code != expected", "numeric value
              didn't satisfy comparator")
    TIMEOUT — per-case `timeout_seconds` elapsed, or the global budget
              expired before this case started running
    ERROR   — runner-side failure (couldn't spawn, decoder bug, etc.) —
              never user-visible if everything's healthy
    SKIP    — the case YAML carries `skip_reason` (e.g. "platform=mac")

TIMEOUT is intentionally distinct from FAIL because a timing flake on a
loaded runner is not the same signal as a behavioural regression. The
HTML report shows them in different colours; the gate counts them
separately.
"""

from __future__ import annotations

import os
import re
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, Future
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

from .schema import Case, PassCriteria


# Status values are short uppercase strings; downstream code (HTML, JSON,
# CI annotations) uses these as keys, so they're API-stable.
PASS, FAIL, TIMEOUT, ERROR, SKIP = "PASS", "FAIL", "TIMEOUT", "ERROR", "SKIP"


@dataclass
class Result:
    case: Case
    status: str
    started_at: str            # ISO-8601 UTC; "" if not started
    duration_s: float
    exit_code: int | None
    stdout_tail: str           # last ~4 KiB
    stderr_tail: str
    diagnostic: str            # filled by diagnose.py for failures
    extra: dict = field(default_factory=dict)


def _now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _tail(s: str, max_bytes: int = 4096) -> str:
    b = s.encode("utf-8", errors="replace")
    if len(b) <= max_bytes:
        return s
    return "[…truncated…]\n" + b[-max_bytes:].decode("utf-8", errors="replace")


def _evaluate(pc: PassCriteria,
              exit_code: int,
              stdout: str,
              stderr: str) -> tuple[bool, str]:
    """Return (passed, evaluator_note). The note is a short, human-readable
    sentence describing the verdict, used by the diagnoser."""
    haystack = {
        "stdout": stdout,
        "stderr": stderr,
        "both":   stdout + "\n" + stderr,
    }[pc.stream]

    if pc.type == "exit_code":
        ok = exit_code == pc.expected
        return ok, f"exit_code={exit_code}, expected={pc.expected}"

    if pc.type == "regex":
        m = re.search(pc.pattern, haystack, flags=re.MULTILINE)
        if m:
            return True, f"regex matched at offset {m.start()}"
        return False, f"regex {pc.pattern!r} did not match"

    if pc.type == "numeric":
        m = re.search(pc.pattern, haystack, flags=re.MULTILINE)
        if not m or not m.groups():
            return False, f"numeric extractor {pc.pattern!r} produced no capture group"
        try:
            value = float(m.group(1))
        except ValueError:
            return False, f"numeric extractor captured {m.group(1)!r}, not a number"
        op = pc.comparator
        thr = pc.threshold
        cmp = {
            ">":  value >  thr,
            ">=": value >= thr,
            "<":  value <  thr,
            "<=": value <= thr,
            "==": value == thr,
            "!=": value != thr,
        }[op]
        return cmp, f"value={value} {op} threshold={thr}"

    return False, f"unknown pass_criteria.type {pc.type!r}"


def _run_one(case: Case, repo_root: Path, deadline: float) -> Result:
    if case.skip_reason:
        return Result(case=case, status=SKIP, started_at=_now_iso(),
                      duration_s=0.0, exit_code=None,
                      stdout_tail="", stderr_tail="",
                      diagnostic=f"skipped: {case.skip_reason}")

    # Global-budget guard: if we've already passed `deadline`, mark
    # this case TIMEOUT without spawning anything.
    now = time.monotonic()
    remaining_global = deadline - now
    if remaining_global <= 0:
        return Result(case=case, status=TIMEOUT, started_at=_now_iso(),
                      duration_s=0.0, exit_code=None,
                      stdout_tail="", stderr_tail="",
                      diagnostic="global budget expired before case started")

    per_case = min(case.timeout_seconds, max(1, int(remaining_global)))
    started = _now_iso()
    t0 = time.monotonic()

    try:
        proc = subprocess.run(
            case.test_command,
            shell=True,
            cwd=str(repo_root),
            capture_output=True,
            timeout=per_case,
            check=False,
            text=False,    # decode manually with errors='replace'
            env={**os.environ, "SCC_CONFORMANCE": "1"},
        )
    except subprocess.TimeoutExpired as e:
        dt = time.monotonic() - t0
        out = (e.stdout or b"").decode("utf-8", errors="replace")
        err = (e.stderr or b"").decode("utf-8", errors="replace")
        return Result(case=case, status=TIMEOUT, started_at=started,
                      duration_s=dt, exit_code=None,
                      stdout_tail=_tail(out), stderr_tail=_tail(err),
                      diagnostic=f"timed out after {per_case}s (per-case budget)")
    except FileNotFoundError as e:
        dt = time.monotonic() - t0
        return Result(case=case, status=ERROR, started_at=started,
                      duration_s=dt, exit_code=None,
                      stdout_tail="", stderr_tail=str(e),
                      diagnostic=f"failed to spawn: {e}")

    dt = time.monotonic() - t0
    stdout = proc.stdout.decode("utf-8", errors="replace") if proc.stdout else ""
    stderr = proc.stderr.decode("utf-8", errors="replace") if proc.stderr else ""

    assert case.pass_criteria is not None
    ok, note = _evaluate(case.pass_criteria, proc.returncode, stdout, stderr)
    status = PASS if ok else FAIL

    return Result(
        case=case,
        status=status,
        started_at=started,
        duration_s=dt,
        exit_code=proc.returncode,
        stdout_tail=_tail(stdout),
        stderr_tail=_tail(stderr),
        diagnostic=note,
    )


def run_all(cases: list[Case],
            *,
            repo_root: Path,
            global_budget_seconds: float,
            max_workers: int = 4) -> list[Result]:
    """Execute all cases in parallel under one global wallclock."""
    deadline = time.monotonic() + global_budget_seconds
    results: list[Result] = []
    with ThreadPoolExecutor(max_workers=max_workers) as ex:
        futures: dict[Future, Case] = {
            ex.submit(_run_one, c, repo_root, deadline): c for c in cases
        }
        for fut in futures:
            try:
                results.append(fut.result())
            except Exception as e:  # noqa: BLE001 - last-resort runner safety net
                c = futures[fut]
                results.append(Result(case=c, status=ERROR, started_at=_now_iso(),
                                      duration_s=0.0, exit_code=None,
                                      stdout_tail="", stderr_tail=str(e),
                                      diagnostic=f"runner error: {e!r}"))
    results.sort(key=lambda r: r.case.numeric_id)
    return results
