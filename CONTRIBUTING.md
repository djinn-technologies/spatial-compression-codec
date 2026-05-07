# Contributing to the Spatial Compression Codec
Thanks for your interest in contributing to **SCC**. This codec isthe basis of a commercial product, but the development process isopen: we accept patches from anyone, document our architecturaldecisions in public ADRs, and treat the conformance suite as thecontract between this codebase and its users.
This document describes how to participate in that process. It isthe practical companion to [`LICENSE.md`](LICENSE.md) (which is the legal contract) and [`docs/SAD.md`](docs/SAD.md) (which is thearchitectural contract).
---

## Before you start

### Code of Conduct
We follow the [Contributor Covenant v2.1](https://www.contributor-covenant.org/version/2/1/code_of_conduct/).Be kind, be specific, assume good intent. Report Code-of-Conductconcerns privately to **conduct@djinn.cloud** --- they go to anamed maintainer, not a public list.

### The contributor licence
By submitting a Contribution (a pull request, issue patch, emaildiff, or any other suggested change), you agree to the terms of[`LICENSE.md` §4 --- Contributions](LICENSE.md#4-contributions). Inplain English:
1. **Copyright** in your contribution is assigned to Djinn   Technologies Ltd. (or, where assignment is not legally available,   licensed to Djinn on equivalent terms).2. You agree **not to assert any patent** you own or control against   Djinn or Djinn's licensees of SCC, to the extent the assertion   would be based on the inclusion of your contribution.3. You **represent** that the contribution is your original work or   that you have the rights to submit it under these terms.
For substantive contributions we may ask you to sign a separateContributor Licence Agreement (CLA). For one-line typo fixes wewon't. If you can't agree to those terms --- for example because youremployer doesn't allow it --- please tell us before opening a PR; wecan usually find a path forward, and we'd rather know early thanhave to reject a finished change.

### Where to ask questions
| Channel                                | Use for                                       |
| -------------------------------------- | --------------------------------------------- |
| GitHub Issues                          | Reproducible bugs, concrete feature requests. |
| GitHub Discussions                     | Design questions, "is this in scope?", help.  |
| `security@djinn.cloud`                 | **Vulnerabilities only** --- see [`SECURITY.md`](SECURITY.md). |
| `licensing@djinn.cloud`                | Patent / commercial-licensing enquiries.      |
---

## Setting up your environment
The runbook is in the [main README](README.md#building-from-source).The short version:
bashcmake -S . -B build -DSCC_ENABLE_TESTS=ONcmake --build build -jctest --test-dir build --output-on-failure

For polyglot work (SDKs, Studio, conformance tooling) the\
per-component README has the relevant toolchain. The dependency\
matrix in the main README tells you what's needed where.

* * * * *

Development workflow
--------------------

### Branches

-   `main` is always green and is the only branch we cut releases\
    from.
-   `dev` is the integration branch for the current cycle.
-   Feature work happens on a topic branch off `dev`, named\
    `<initials>/<short-slug>` (e.g. `jbb/rans-simd`,\
    `ada/api-rate-limit-tests`).
-   Long-running spikes that you don't intend to merge live under\
    `spike/<slug>` and are deleted without ceremony.

### Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/).\
The commit log is read by humans and by tooling (changelog\
generation, CI scope detection), so the format matters.

```
<type>(<scope>): <imperative subject ≤ 70 chars><body --- what changed and *why*; wrap at 72 cols><trailer block --- Refs, Closes, Co-Authored-By>
```

| Type | Use for |
| --- | --- |
| `feat` | New user-visible capability. |
| `fix` | Bug fix. |
| `perf` | Throughput / memory / size improvement. |
| `refactor` | No behavioural change. |
| `docs` | README / ADR / SAD / inline-comment changes. |
| `test` | Tests only. |
| `build` | Build system, dependencies, packaging. |
| `ci` | CI configuration only. |
| `chore` | Anything else that doesn't ship to users. |

Common scopes: `codec`, `cabi`, `bench`, `agent`, `api`, `frontend`,\
`wasm`, `python`, `unity`, `unreal`, `conformance`, `docs`, `license`.

Subjects are imperative ("add", "fix", "remove" --- not "added",\
"fixes", "removed"). The body explains *why* the change was needed\
and what the alternatives were; `git log` is part of the\
documentation.

If a commit fixes a numbered requirement or implements a patent\
claim, cite it in the body:

```
Refs: REQ-009, [US10827161B2 col. 7-8]
```

### Pull requests

Open PRs against `dev` (or against `main` for a\
post-merge hotfix only). PR descriptions should explain:

1.  What changed and why. Restate the problem in your own words.
2.  What the alternatives were and why this is the right one.
3.  Test plan. Which suites you ran, what regressions you ruled\
    out, and which manual steps the reviewer should retrace.
4.  Reviewer notes. Anything non-obvious about the diff ---\
    intentional asymmetries, deferred work, follow-up issues.

Keep PRs small and self-contained. A 200-line PR that does one\
thing reviews in fifteen minutes; a 2000-line PR that does five\
things reviews in three days and lands with a stale `main`.

### Pre-submit checklist

Before you click "Ready for review", run through this list. Most of\
it is automated by CI, but it's faster to catch things locally.

-   not doneFormat. `clang-format` (C++), `rustfmt` (Rust),\
    `eslint --fix` + `prettier` (TS), `ruff format` (Python).
-   not doneBuild. `cmake --build build -j` is clean (no warnings).
-   not doneSanitisers. `cmake --preset asan && ctest --preset asan`\
    passes for any change touching `codec/`, `cabi/`, or `bench/`.\
    `msvc-asan` on Windows.
-   not doneUnit tests. `ctest`, `npm test`, `pytest`, `cargo test`\
    --- whichever applies to the directories you touched.
-   not doneConformance. If you changed code traced to a `REQ-NNN`,\
    run `python ci/conformance/run.py --filter REQ-NNN` and\
    attach the output to the PR. If you added a new requirement\
    to `docs/Acceptance_Criteria.md`, ship a YAML case under\
    `ci/conformance/cases/REQ-NNN.yaml` in the same PR.
-   not doneBench. If you changed code under `codec/src/` or\
    `cabi/src/`, run `bench/run-quick.sh` and confirm no\
    regression vs `bench/reports/baseline.json`. Updating the\
    baseline is a separate, explicit step (see "Performance"\
    below).
-   not doneDocs. Public-facing changes update the per-component\
    README. Architectural decisions add an ADR.
-   not doneLicense + Security. No new third-party dependency\
    without checking its licence is compatible. No new\
    attack-surface without a one-line note in the PR.

CI runs all of the above on push; if everything is green locally,\
CI will be green too.

* * * * *

Coding standards
----------------

The repo's `.clang-format`, `.editorconfig`, `eslint.config.js`,\
`rustfmt.toml`, and `pyproject.toml` are the source of truth. The\
notes below are *intent*, not redundant restatement.

### General principles (all languages)

-   Clarity over cleverness. A reader at 2 a.m. should be able to\
    understand the function on first read.
-   Don't add abstractions you don't need. Three similar lines\
    beat a premature template / trait / generic.
-   Don't catch what you can't handle. Let the boundary deal with\
    it. The C ABI is the only place exceptions get translated to\
    error codes --- not anywhere else.
-   No comments that restate the code. A comment should answer\
    *why*, not *what*. Hidden constraints, surprising invariants,\
    and pointers to the spec section that justifies the code are\
    worth a comment. `// increment counter` is not.
-   Cite your sources. If a line implements a patent claim, a\
    standard, or a paper, the inline citation goes immediately above\
    it (see "Citation conventions" below).

### C++ (`codec/`, `cabi/`, `bench/`)

-   C++17, `EXTENSIONS OFF`. We do not rely on C++20 anywhere yet.
-   Header layout: public headers under `codec/include/scc/`,\
    internal-detail headers under `codec/include/scc/detail/`.
-   The C ABI (`cabi/include/libscc.h`) is C99-pure. No `bool`,\
    no `inline`, no `_Generic`, no compound literals --- see the\
    comment at the top of that header for the full list.
-   Exceptions are forbidden across the C ABI boundary. Any C++\
    exception that could escape `extern "C"` is a bug.
-   Use `static_assert` to enforce wire-format invariants and bit-\
    width assumptions. The renormalisation arithmetic in the rANS\
    coder is the canonical example.
-   Format: `clang-format` with the repo's `.clang-format` (LLVM\
    base, 120 cols, 4-space indent, `PointerAlignment: Left`).

### Rust (`studio/agent/`)

-   Rust 1.75+ stable. Nightly features are not used.
-   Every `unsafe` block has a comment immediately above it\
    describing the invariant the caller relies on. CI will eventually\
    enforce this with a clippy lint; today it's a review obligation.
-   Run `cargo clippy --all-targets -- -D warnings` and `cargo fmt --check` locally.
-   Don't catch panics across FFI; the C ABI is exception-safe and\
    panics are not. `std::panic::catch_unwind` is for the very thin\
    shim layer only.

### TypeScript (`sdk/wasm/`, `studio/api/`, `studio/frontend/`)

-   TypeScript 5.5+, `strict: true`, `noUncheckedIndexedAccess: true`.
-   ESLint + Prettier. The frontend additionally enforces\
    `eslint-plugin-jsx-a11y`.
-   React: hooks only, no class components. Use the project's\
    Zustand sliced-store pattern (see ADR-013); don't introduce a\
    second state library.
-   API: every Fastify route declares its request and response shapes\
    in Zod. The same Zod source feeds the validator, the serialiser,\
    and the OpenAPI emitter.

### Python (`sdk/python/`, `ci/conformance/`)

-   Python 3.10+ for `sdk/python/`, 3.11+ for `ci/conformance/`.
-   `ci/conformance/` is stdlib-only by design (see ADR-015) ---\
    do not introduce dependencies, including for tests.
-   Type hints on every public function. `ruff` for linting,\
    `ruff format` for formatting.

### Unity / Unreal

-   Unity targets 2022.3 LTS; Unreal targets 5.3+. Older versions are\
    out of scope.
-   Don't break IL2CPP (Unity) or shipping builds (Unreal). Both\
    exclude `Mono.dll`-style runtime tricks.
-   Editor-only code lives under the Editor assembly / `Editor` source\
    group; runtime code does not depend on Editor APIs.

* * * * *

Citation conventions
--------------------

Three citation forms appear inline in the code. They exist so a\
reviewer can validate any line of code against an external authority.

| Form | Means |
| --- | --- |
| `[REQ-009]` | Numbered requirement in `docs/Acceptance_Criteria.md`. |
| `[US10827161B2 col. 7-8]` | Patent claim text, identified by column range. |
| `[H.264 §D.1.6]` | Industry-standard clause (ISO/IEC 14496-10 in this case). |
| `[Duda 2014]` | Academic paper --- short citation; the full reference is in the relevant ADR. |
| `[ADR-001]` | Cross-reference to an architecture decision record. |

Every code site that implements a patent claim has the patent\
citation. Do not remove or alter these citations --- they are part\
of the legal posture documented in `LICENSE.md` §5.2.

* * * * *

Testing
-------

| Path | Framework | Command |
| --- | --- | --- |
| `codec/` | Catch2 v3 + rapidcheck | `ctest --test-dir build` |
| `cabi/` | Catch2 v3 | `ctest --test-dir build` |
| `bench/` | ctest entries (`-L bench`, `-L perf`) | `ctest --test-dir build -L bench` |
| `sdk/wasm/` | Vitest + Playwright | `npm test` / `npm run test:browser` |
| `sdk/python/` | pytest | `pytest` |
| `sdk/unity/` | Unity Test Framework | EditMode / PlayMode runner in the Editor |
| `sdk/unreal/` | Unreal Automation + FunctionalTest | Session Frontend → Automation |
| `studio/agent/` | `cargo test` + `criterion` benches | `cargo test` |
| `studio/api/` | Vitest + Postgres testcontainer | `npm test` |
| `studio/frontend/` | Vitest + @testing-library + Playwright | `npm test` / `npm run test:e2e` |
| `ci/conformance/` | `unittest` (stdlib only) | `python -m unittest ci.conformance.tests.test_runner` |

Test-first is the norm, especially for the codec. A new feature\
that reaches `main` without a test is treated as a regression\
waiting to happen.

Property tests are encouraged for codec code (rapidcheck for C++,\
Vitest's `fast-check` integration for TS). Round-trip properties\
("for any frame `f`, `decode(encode(f)) == f` in lossless mode")\
are the load-bearing guarantee, and they are exactly what a property\
test expresses.

### Synthetic vs real test data

-   Tests should use synthetic, deterministic input wherever\
    possible. The `bench/` corpus generator is the model --- every\
    fixture is a function of `--seed` and reproducible bit-for-bit.
-   Real-world depth recordings are not committed. They are\
    declared in `bench/fixtures/manifest.json` with a download URL\
    and SHA-256 and downloaded on first use.

* * * * *

Performance and conformance gates
---------------------------------

Two automated gates protect `main`:

### Performance --- `bench/`

`bench/run-quick.sh` produces an HTML report and exits non-zero on\
regression vs `bench/reports/baseline.json`. The thresholds are:

-   > 5 % single-thread throughput regression → fail.

-   > 2 percentage-points compression-ratio regression → fail.

-   > 1 dB PSNR regression on the lossy profile → fail.

-   > 0.01 SSIM regression → fail.

If your change makes SCC genuinely faster or more compact, update\
the baseline in the same PR. Reviewers can sanity-check the diff\
and the baseline together. Don't sneak a baseline update in\
silently --- that is how regressions accumulate one half-percent at a\
time.

### Conformance --- `ci/conformance/`

Every blocking REQ in `docs/Acceptance_Criteria.md` has (or soon\
will have) a YAML case under `ci/conformance/cases/`. The runner\
exits non-zero if any blocking case fails.

If you add a new requirement to the acceptance doc, ship a matching\
YAML case in the same PR. The template at\
`ci/conformance/cases/_template.yaml` is the starting point.

* * * * *

Documentation expectations
--------------------------

| Change | Document where |
| --- | --- |
| New public API or component | Per-component `README.md` + `docs/SAD.md` if architecturally significant. |
| New numbered requirement | `docs/Acceptance_Criteria.md` + matching YAML case. |
| Architectural decision (any "we chose X over Y" with consequences) | New ADR under `docs/adr/`, numbered next in sequence. |
| New build flag, env var, CLI option | The relevant README's "options" section. |
| New error code or result enum value | Inline doc comment + the relevant ADR (if it widens the ABI surface). |

ADRs follow the established template: a YAML metadata table at the\
top, then Context, Decision, Consequences,\
Verification. Use ADR-014 or ADR-015 as a model --- they are\
short, complete, and reviewable.

* * * * *

Review process
--------------

-   Two-reviewer minimum on changes that touch `codec/`, `cabi/`, or\
    the SAD. One reviewer is enough elsewhere.
-   Reviewers will look for: correctness, test coverage, performance\
    impact, ABI / API stability, and whether the documentation moved\
    with the code.
-   "Approved with comments" means *land it after addressing the\
    comments* --- you don't need a second review pass for clarifying\
    edits.
-   "Request changes" means a reviewer needs to look again before the\
    PR lands.
-   The author merges (after green CI + approval). Maintainers may\
    merge on the author's behalf if the author is unreachable for\
    more than a working week.
-   Squash-merge is the default; merge-commits are reserved for\
    multi-commit PRs whose individual commits are independently\
    meaningful (rare).

* * * * *

What we do *not* accept
-----------------------

-   Patent grants disguised as code comments. Adding "this is\
    freely licensed" or similar notices is not a contribution we will\
    merge --- the licence is `LICENSE.md`, full stop.
-   Removed citations. PRs that delete `[REQ-NNN]`, `[Patent col. N]`,\
    or `[ADR-...]` references without explanation will be sent back.
-   Code without tests for behavioural changes.
-   Third-party dependencies with incompatible licences (GPL, AGPL,\
    CC-BY-NC, anything restricting commercial use). Apache 2.0, MIT,\
    BSD, ISC, Zlib, public-domain --- fine.
-   Generated content presented as your own work. AI-assisted\
    contributions are welcome (this codebase has plenty of them) but\
    must be reviewed and understood by the human submitter, and the\
    contributor terms in `LICENSE.md` §4 still apply.

* * * * *

Acknowledgements
----------------

Contributors are credited:

-   In commit metadata (`Co-Authored-By:` trailers).
-   In ADRs for substantial architectural input.
-   In `SECURITY.md`'s acknowledgements section for security work.
-   In release notes for material features and fixes.

Thanks for helping make SCC better.
