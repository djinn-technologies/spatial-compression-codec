# SCC conformance suite

Executable acceptance gate for `docs/Acceptance_Criteria.md`. One YAML
file per REQ, run by a stdlib-only Python harness, reported as HTML +
JSON + GitHub annotations. Wired into CI by
`.github/workflows/conformance.yml`.

> **Why this exists.** A spec that nobody can mechanically check is a
> spec that drifts. Every REQ in the SAD's acceptance doc gets a YAML
> case here; CI fails the build if any blocking REQ fails. The result
> is a "reproduce in 20 minutes on a fresh runner" gate rather than a
> static document.

## Quick start

```bash
# Lint cases + check coverage; never spawn a subprocess.
python ci/conformance/run.py --no-execute

# Full local run with default 20-minute global wallclock.
python ci/conformance/run.py --out reports/

# Run only the REQs whose evidence files changed since merge-base.
python ci/conformance/run.py --bisect

# Run a single case by id.
python ci/conformance/run.py --filter REQ-009
```

After a successful run:

- `reports/<dir>/conformance.html` — human-readable report
- `reports/<dir>/conformance.json` — schema-versioned, dashboard-ready
- `reports/<dir>/summary.txt` — one-line CI step-summary

## Authoring a new case

Copy `cases/_template.yaml` to `cases/REQ-NNN.yaml` and fill in the
fields. The runner lints with `--no-execute`, so a malformed YAML
fails fast in CI.

| Field                       | Notes                                              |
| --------------------------- | -------------------------------------------------- |
| `id`                        | `REQ-NNN`, matching `Acceptance_Criteria.md`.      |
| `description`               | One line, ≤ 120 chars; mirrors the REQ text.       |
| `sad_section`               | The SAD heading the REQ lives under.               |
| `evidence`                  | Citation: patent col, standard clause, paper.      |
| `evidence_paths`            | List of file globs `--bisect` keys on.             |
| `test_command`              | Shell, run from the repo root. **Use double-quoted strings** so cmd.exe and `/bin/sh` both parse them. |
| `pass_criteria_type`        | `exit_code` / `regex` / `numeric`.                 |
| `pass_criteria_expected`    | (`exit_code`) integer.                             |
| `pass_criteria_pattern`     | (`regex`/`numeric`) Python `re` pattern.           |
| `pass_criteria_stream`      | `stdout` / `stderr` / `both` (default `both`).     |
| `pass_criteria_comparator`  | (`numeric`) `>`, `>=`, `<`, `<=`, `==`, `!=`.      |
| `pass_criteria_threshold`   | (`numeric`) reference value.                       |
| `timeout_seconds`           | Per-case wallclock; default 120.                   |
| `blocking`                  | `true` blocks merge on failure; `false` warns.     |
| `skip_reason`               | Optional. If set, runner emits SKIP.               |

## Status taxonomy

| Status   | Meaning                                                  | Affects gate? |
| -------- | -------------------------------------------------------- | ------------- |
| PASS     | `pass_criteria` evaluated to true                        | no            |
| FAIL     | `pass_criteria` evaluated to false                       | yes if blocking |
| TIMEOUT  | per-case or global wallclock expired                     | no (≠ FAIL)   |
| ERROR    | runner could not spawn / decode the test                 | yes if blocking |
| SKIP     | case carried `skip_reason`                               | no            |

`TIMEOUT` is intentionally not the same as `FAIL`: a flake on a loaded
runner is not the same signal as a behavioural regression. The gate
counts blocking FAIL/ERROR; TIMEOUTs are surfaced for human review.

## `--bisect` mode

Runs only cases whose declared `evidence_paths` overlap the set of
files changed since `git merge-base origin/main HEAD`. Use it locally
to keep iteration fast on a focused change. CI runs the full suite
(blocking) and a bisect pass (informational) side by side.

`evidence_paths` are globs (`fnmatch`) and also accept directory
prefixes (matching every file beneath). They are *explicit* — the
runner does not try to infer dependencies from the test command.

## JSON schema (stable)

```json
{
  "schema_version": "1.0.0",
  "suite": { "started_at": "...", "completed_at": "...", "totals": {...} },
  "rows": [
    {
      "id": "REQ-009",
      "description": "...",
      "sad_section": "§B. Entropy coder — rANS",
      "evidence": "[Patent col. 7-8]; [Duda 2014 §3.3]",
      "evidence_paths": ["codec/include/scc/rans.hpp", "..."],
      "blocking": true,
      "pass_criteria": { "type": "exit_code", "expected": 0,
                         "pattern": null, "stream": null,
                         "comparator": null, "threshold": null },
      "test_command": "ctest --test-dir build -R 'rans8_'",
      "timeout_seconds": 60,
      "status": "PASS",
      "started_at": "2026-05-07T12:34:56Z",
      "duration_s": 2.341,
      "exit_code": 0,
      "diagnostic": "exit_code=0, expected=0"
    }
  ]
}
```

Every key is always present — missing values are `null`, never
absent. Floats are rounded to 3 decimals. Rows are sorted by REQ
numeric id.

## Coverage policy

The runner cross-checks `cases/REQ-*.yaml` against every `REQ-NNN`
token in `docs/Acceptance_Criteria.md`:

- `--coverage-warn` *(default)* — print missing REQs, exit 0.
- `--coverage-strict` — print missing REQs, exit 2.
- `--coverage-skip` — skip the check.

`--coverage-strict` becomes the CI default once every REQ has a YAML
case (see ADR-015 for the migration plan).

## Self-tests

```bash
python -m unittest ci.conformance.tests.test_runner -v
```

These exercise the YAML parser, schema validation, runner timeouts,
and diagnostic generation. They have zero external dependencies and
run in ~10 seconds.

## See also

- [`docs/SAD.md` §14 — Conformance & Acceptance](../../docs/SAD.md#14-conformance)
- [`docs/Acceptance_Criteria.md`](../../docs/Acceptance_Criteria.md)
- [`docs/adr/ADR-015-conformance-suite.md`](../../docs/adr/ADR-015-conformance-suite.md)
