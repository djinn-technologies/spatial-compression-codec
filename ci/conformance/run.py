#!/usr/bin/env python3
"""SCC conformance suite runner.

This is the executable layer over `acceptance/Acceptance_Criteria.md`
(the SAD's source of truth for "done"). It:

  1. Discovers every REQ-NNN.yaml under ci/conformance/cases/.
  2. Optionally filters by `--bisect` (run only cases whose evidence
     paths changed since `merge-base origin/main HEAD`).
  3. Verifies coverage: every REQ id mentioned in the acceptance doc
     has a YAML case (Ultrathink #5).
  4. Executes cases in parallel, under a global wallclock budget.
  5. Writes HTML, JSON, and (if enabled) GitHub workflow annotations.
  6. Exits non-zero iff any *blocking* case failed.

Pure stdlib. Python 3.11+.

Usage examples:
    python ci/conformance/run.py
    python ci/conformance/run.py --out reports/ --gh-annotations
    python ci/conformance/run.py --bisect --since origin/main
    python ci/conformance/run.py --coverage-strict --no-execute
"""

from __future__ import annotations

import argparse
import sys
from datetime import datetime, timezone
from pathlib import Path

# Make `lib` importable when invoked as `python ci/conformance/run.py`.
_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))

from conformance.lib import bisect as _bisect
from conformance.lib import coverage as _coverage
from conformance.lib import diagnose as _diagnose
from conformance.lib import report as _report
from conformance.lib import runner as _runner
from conformance.lib import schema as _schema
from conformance.lib.runner import PASS, FAIL, TIMEOUT, ERROR, SKIP


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(prog="scc-conformance",
                                description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2],
                   help="repository root (default: detected from script path)")
    p.add_argument("--cases-dir", type=Path,
                   default=None,
                   help="dir of REQ-*.yaml (default: ci/conformance/cases)")
    p.add_argument("--acceptance-md", type=Path,
                   default=None,
                   help="path to Acceptance_Criteria.md (default: docs/Acceptance_Criteria.md)")
    p.add_argument("--out", type=Path, default=None,
                   help="output dir for reports (default: ci/conformance/reports/latest)")
    p.add_argument("--bisect", action="store_true",
                   help="run only cases whose evidence_paths have changed")
    p.add_argument("--since", type=str, default=None,
                   help="git ref for --bisect (default: merge-base with origin/main)")
    p.add_argument("--global-budget-seconds", type=int, default=20 * 60,
                   help="hard wallclock cap for the whole suite (default: 1200s)")
    p.add_argument("--max-workers", type=int, default=4,
                   help="parallel cases (default: 4)")
    p.add_argument("--coverage-strict", action="store_true",
                   help="fail if any REQ in Acceptance_Criteria.md has no YAML")
    p.add_argument("--coverage-warn", action="store_true",
                   help="warn but don't fail on missing YAML (default)")
    p.add_argument("--coverage-skip", action="store_true",
                   help="skip the coverage check entirely")
    p.add_argument("--no-execute", action="store_true",
                   help="lint cases + check coverage only, do not run any test")
    p.add_argument("--gh-annotations", action="store_true",
                   help="emit ::error/::warning workflow commands on stdout")
    p.add_argument("--filter", type=str, default=None,
                   help="comma-separated REQ ids to run, all others SKIP")
    return p.parse_args()


def _resolve_paths(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    repo = args.repo_root.resolve()
    cases_dir = (args.cases_dir or repo / "ci" / "conformance" / "cases").resolve()
    accept_md = (args.acceptance_md or repo / "docs" / "Acceptance_Criteria.md").resolve()
    out_dir   = (args.out or repo / "ci" / "conformance" / "reports" / "latest").resolve()
    return cases_dir, accept_md, out_dir


def _print_coverage(cov: _coverage.Coverage, *, strict: bool) -> int:
    if not cov.missing and not cov.orphaned:
        print(f"coverage: OK ({len(cov.defined)}/{len(cov.declared)} declared REQs covered)")
        return 0
    if cov.missing:
        print(f"coverage: {len(cov.missing)} REQs declared in Acceptance_Criteria.md "
              f"have NO YAML:")
        for r in cov.missing:
            print(f"  - {r}")
    if cov.orphaned:
        print(f"coverage: {len(cov.orphaned)} YAML cases have no matching REQ in the doc:")
        for r in cov.orphaned:
            print(f"  - {r}")
    return 2 if strict and (cov.missing or cov.orphaned) else 0


def main() -> int:
    args = _parse_args()
    cases_dir, accept_md, out_dir = _resolve_paths(args)

    # 1. Discover + lint every YAML.
    try:
        cases = _schema.discover_cases(cases_dir)
    except _schema.SchemaError as e:
        print(f"conformance: schema error: {e}", file=sys.stderr)
        return 2

    # 2. Coverage check.
    if not args.coverage_skip and accept_md.exists():
        cov = _coverage.compute(accept_md, cases)
        rc = _print_coverage(cov, strict=args.coverage_strict)
        if rc != 0 and args.coverage_strict:
            return rc

    # 3. --bisect filter.
    if args.bisect:
        changed = _bisect.changed_files(args.repo_root.resolve(), args.since)
        cases = _bisect.filter_cases(cases, changed)
        print(f"bisect: {len(cases)} cases match {len(changed)} changed files")
        if not cases:
            # Nothing to do is a *pass*, not a failure.
            print("bisect: no relevant cases — exiting 0")
            return 0

    # 4. --filter shortlist.
    if args.filter:
        keep = {s.strip() for s in args.filter.split(",") if s.strip()}
        cases = [c for c in cases if c.id in keep]
        if not cases:
            print(f"filter: no cases matched {sorted(keep)}", file=sys.stderr)
            return 2

    if args.no_execute:
        print(f"lint OK: {len(cases)} cases would run")
        return 0

    # 5. Execute.
    suite_started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    results = _runner.run_all(
        cases,
        repo_root=args.repo_root.resolve(),
        global_budget_seconds=args.global_budget_seconds,
        max_workers=args.max_workers,
    )

    # 6. Diagnose failures (one-screen).
    for r in results:
        if r.status in (FAIL, TIMEOUT, ERROR):
            r.diagnostic = _diagnose.diagnose(r)

    # 7. Reports.
    out_dir.mkdir(parents=True, exist_ok=True)
    _report.write_json(results, out_dir / "conformance.json", suite_started=suite_started)
    _report.write_html(results, out_dir / "conformance.html", suite_started=suite_started)
    _report.write_summary_txt(results, out_dir / "summary.txt")
    if args.gh_annotations:
        _report.emit_github_annotations(results)

    t = _report.tally(results)
    print(f"SCC conformance: {t[PASS]} / {t['total']} REQs PASS, "
          f"{t[FAIL]} FAIL, {t[TIMEOUT]} TIMEOUT, {t[ERROR]} ERROR, {t[SKIP]} SKIP")
    print(f"reports: {out_dir}")

    return 0 if t["blocking_fail"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
