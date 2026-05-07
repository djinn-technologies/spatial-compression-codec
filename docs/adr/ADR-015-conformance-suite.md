# ADR-015 — Executable conformance suite

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-031]`, `[ADR-001..014]`                                      |

## Context

`docs/Acceptance_Criteria.md` lists 57 numbered REQs that define
"done" for SCC. As written, it is a *document* — humans read it and
nod. We need it to be *executable*: every REQ becomes a CI assertion,
and every PR proves on the runner that the assertion still holds.

The constraints (from AI Build Prompt #15) are sharp:

- Pure Python 3.11+ stdlib. No PyYAML, no pytest, no jinja2.
- Total wallclock ≤ 20 minutes on a GitHub-hosted runner.
- Per-case timeouts must be `TIMEOUT` (not `FAIL`) — different signal.
- Bisect mode keyed on git-diff, so PR runs can iterate fast.
- HTML grouped by SAD section; one-screen failure diagnostic; JSON
  stable for downstream dashboards.

Decisions to lock:

1. **YAML or alternative?** The prompt says YAML; we honor it.
2. **Coverage policy** — strict, warn, or skip?
3. **Status taxonomy** — what's a TIMEOUT vs a FAIL?
4. **Diagnostic budget** — how much log content per failure?
5. **Bisect resolver** — explicit `evidence_paths` vs inferred deps?
6. **JSON schema lifecycle** — versioning + backward compat.
7. **CI gate semantics** — what exits non-zero?

## Decision

### 1. Constrained YAML, hand-rolled parser.

We honor the prompt's `REQ-NNN.yaml` requirement but parse a *strict
subset* (`ci/conformance/lib/yaml_subset.py`): top-level mapping, two-
space indent, sequences only via the `- item` form, no anchors / flow
style / multi-doc / block scalars. The parser is ~150 lines, tested
in `tests/test_runner.py`, and rejects anything outside the subset
with a line number — case authors fix the YAML rather than discover
silent parses.

The alternative (TOML via stdlib `tomllib`) would have been simpler
but required violating the prompt's explicit YAML extension. PyYAML
would have violated the "no external CI dependencies" constraint.

### 2. Coverage policy is `--coverage-warn` by default; `strict` is the goal.

The runner discovers every `REQ-NNN` token in
`docs/Acceptance_Criteria.md` and verifies a YAML exists for each.

- **`--coverage-strict`** — exits 2 on any missing or orphaned case.
- **`--coverage-warn`** *(CI default today)* — prints the missing
  list and continues.
- **`--coverage-skip`** — pure execution; no coverage check.

The current PR ships 10 sample cases out of 57 declared REQs; the
remaining 47 are honest gaps, not stubs that fake coverage. Stubs
would be worse: they claim discipline the suite doesn't have, and
they hide regression by always passing. ADR-015's migration plan:

| Phase           | Cases | Mode                       |
| --------------- | ----- | -------------------------- |
| 1 (this PR)     | 10    | `--coverage-warn`          |
| 2 (next ~3 PRs) | 30+   | `--coverage-warn`          |
| 3               | all   | `--coverage-strict` in CI  |

### 3. Five-state status taxonomy: PASS, FAIL, TIMEOUT, ERROR, SKIP.

`TIMEOUT` is *not* `FAIL`. A flake on a loaded runner is not the
same signal as a behavioural regression. The gate counts blocking
FAIL + ERROR; TIMEOUTs surface in the report for human review.

`ERROR` is for runner-side faults (couldn't spawn the subprocess,
case YAML invalid at runtime). `SKIP` is for cases that explicitly
declared `skip_reason` (e.g. "requires Postgres testcontainer; gated
to runners with Docker"). Skipped cases never count against the gate.

### 4. One-screen diagnostic, full log to artefact.

Each failure produces a focused (~40-line / ≤ 3 KiB) diagnostic that
fits on screen — what the runner *expected*, what it actually saw,
plus the most marker-rich slice of the failing log
(`FAIL`, `error:`, `assertion`, `panic`, `Expected`).

The full stdout / stderr (last 4 KiB each) lives inside the JSON;
the complete logs are uploaded as a workflow artefact for deeper
debugging. The HTML report's `<details>` block exposes the 4 KiB
tails inline so most failures don't require opening the artefact.

### 5. Explicit `evidence_paths` for `--bisect`.

`evidence_paths` is an explicit list per case — file globs (fnmatch)
or directory prefixes. The runner does *not* try to infer
dependencies from `test_command` (string parsing of shell is
fragile) or from build-system outputs (CMake's `compile_commands`
would couple the suite to one toolchain).

`--bisect` resolves `git diff --name-only $(git merge-base
origin/main HEAD)..HEAD` and keeps cases whose `evidence_paths`
overlaps. Falls back to "all cases" when not in a git checkout (e.g.
release tarball).

The trade-off: case authors must keep `evidence_paths` honest. To
keep them honest, the suite *also* runs in full on every push to
main (no bisect), so a stale `evidence_paths` shows up as a real
regression that the bisect filter missed.

### 6. JSON schema is versioned; missing fields are `null`.

`schema_version: "1.0.0"`. Every key in every row is always present;
missing data is `null`, never absent. Floats are rounded to 3
decimals. Rows are sorted by numeric REQ id. Timestamps are ISO-8601
UTC.

A breaking schema change bumps the major version; downstream
dashboards pin on the major. Additive (non-breaking) changes bump
the minor — adding a new field is fine, removing or retyping a
field is not.

### 7. The gate fails on any blocking FAIL or ERROR.

Workflow exits non-zero iff
`tally["blocking_fail"] + tally["blocking_error"] > 0`. Non-blocking
failures, TIMEOUTs, and SKIPs surface in the report and as
`::warning` annotations but do not block merge.

The gate also runs the suite's *self-tests*
(`tests/test_runner.py`) before any conformance case: if the runner
is broken, we want to learn that immediately rather than from
spurious passes.

## Linking from SAD §14 (Ultrathink #4)

The HTML report is uploaded as a workflow artefact; the URL is
emitted into `$GITHUB_STEP_SUMMARY` and appears in the PR check.
SAD §14 carries a permanent link to
`ci/conformance/reports/latest/conformance.html` (relative path,
resolves inside the workflow run artefact). README and ADR-015 also
link there.

## Risks

1. **Shell quoting variance across runners.** `cmd.exe` (Windows)
   treats single-quoted strings as literal characters; `/bin/sh`
   does not. The case template documents *use double-quoted
   strings*; the self-test exercises this on the host platform.

2. **Subprocess shell injection.** `test_command` runs under
   `shell=True` — by design. Cases are reviewed in PRs; the runner
   does not accept user-supplied commands.

3. **Decoder drift on non-UTF8 bytes.** The runner decodes with
   `errors='replace'` so binary noise does not crash the report.

4. **Coverage backfill stalling.** Phase 3 (`--coverage-strict` in
   CI) is gated on the case YAML count reaching declared REQ count.
   If contributors don't backfill, we publish discipline we don't
   have. Mitigation: every new REQ added to
   `Acceptance_Criteria.md` requires a same-PR YAML case (a check
   the runner can enforce in a separate `acceptance_lint` job).

5. **Bisect false-negatives.** A case with stale `evidence_paths`
   gets skipped by `--bisect` even when it should run. Mitigated by
   running the full suite (no bisect) on every push to main —
   regressions that bisect missed surface within one merge cycle.

6. **20-minute global cap on slow runners.** A self-hosted runner
   with weak hardware may exceed the budget. Mitigation: the runner
   marks remaining cases as TIMEOUT (not FAIL); CI flags the run
   for review rather than shipping a fake pass.

## Verification

```bash
# 1. Self-tests
python -m unittest ci.conformance.tests.test_runner -v

# 2. Lint cases without running them
python ci/conformance/run.py --no-execute

# 3. Run a single case
python ci/conformance/run.py --filter REQ-009

# 4. Full local run
python ci/conformance/run.py --out reports/local
xdg-open reports/local/conformance.html
```

Acceptance (mapped from prompt):

- [x] `conformance.html` groups REQs by SAD section, links source YAML.
- [x] One-screen diagnostic per failure (≤ 40 lines, ≤ 3 KiB).
- [x] Status string emitted: e.g. *"SCC conformance: 78 / 80 REQs PASS, 2 TIMEOUT"*.
- [x] Blocking vs non-blocking visually distinct in HTML (lock icon /
      red border vs warning icon / amber dashed border) — Ultrathink #1.
- [x] JSON schema versioned + stable (Ultrathink #2).
- [x] `--bisect` runs only cases whose evidence changed (Ultrathink #3).
- [x] SAD §14 + README link to the HTML report (Ultrathink #4).
- [x] Coverage check enforces REQ → YAML mapping (Ultrathink #5);
      currently `--coverage-warn`, migrating to `--coverage-strict`.
