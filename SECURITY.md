# Security Policy

Thank you for taking the time to report a security issue in the
**Spatial Compression Codec (SCC)**. This document explains how to
contact us, what we consider in scope, and what response you can
expect.

---

## Reporting a vulnerability

**Please do not open a public GitHub issue for security bugs.**

We strongly prefer reports through one of the two private channels
below; both are monitored by the same team.

### Preferred — GitHub Security Advisories

Open a private advisory at:
**<https://github.com/djinn-technologies/spatial-compression-codec/security/advisories/new>**

This is the fastest path. The advisory is private to you and our
maintainers until we publish it; it lets us collaborate on patches
and CVE assignment in the same place.

### Alternative — encrypted email

Email **`security@djinn.cloud`** with a clear subject line
(e.g. *"Security report: SCC SEI parser overflow"*).

If you would like to encrypt the message, request our PGP key in a
short plaintext email first and we will reply with the current key
and fingerprint within one business day. We rotate keys annually.

### What to include

The more of the following you can provide, the faster we can act:

1. **Affected component(s)** — e.g. `codec/`, `cabi/libscc`,
   `sdk/wasm/`, `studio/api/`, `studio/agent/`, `studio/frontend/`.
2. **Affected version(s)** — git SHA, tag, or release version.
3. **Description** of the vulnerability and the impact you've
   observed or believe to be possible.
4. **Steps to reproduce** — a minimal, self-contained reproducer is
   extremely helpful (a sample malformed SEI payload, a curl
   sequence, a small program linking against `libscc`, etc.).
5. **Proof-of-concept** code or test vectors, if available.
6. **Environment** — OS, compiler version, build flags, and which
   sensor / browser / runtime applies.
7. **Your disclosure preferences** — whether you want public credit
   in the advisory, and any embargo timing constraints.

If a reproducer touches third-party data (e.g. a depth recording
captured from a real sensor), please redact or synthesise it before
sending so no personal data ends up in the advisory.

---

## What we'll do

| Stage                          | Target SLA                         |
| ------------------------------ | ---------------------------------- |
| Initial acknowledgement        | within **3 business days**         |
| Triage decision (in scope?)    | within **14 calendar days**        |
| Fix + coordinated disclosure   | within **90 calendar days** of triage, in line with industry-standard responsible disclosure |

We will keep you updated at each stage, request a CVE through GitHub
when one is warranted, credit you in the advisory unless you ask us
not to, and coordinate a public-disclosure date with you.

If we cannot meet a milestone, we will tell you why before the
deadline lapses.

---

## Scope

The following components, distributed from this repository, are in
scope for security reporting:

| Path                | Component                                      |
| ------------------- | ---------------------------------------------- |
| `codec/`            | C++ reference codec (rANS, disparity, frequency, quad-tree, SEI muxer / demuxer) |
| `cabi/`             | `libscc` — the public C ABI                    |
| `sdk/wasm/`         | `@djinn/scc-wasm` (Emscripten + TS adapter)    |
| `sdk/python/`       | `scc-py` (pybind11)                            |
| `sdk/unity/`        | `com.djinn.scc` Unity Package                  |
| `sdk/unreal/`       | `SCC` Unreal plugin                            |
| `studio/agent/`     | Rust capture agent (incl. IPC, sensor backends) |
| `studio/api/`       | Studio control-plane API (Fastify + Postgres) |
| `studio/frontend/`  | Studio UI (React + WebGPU)                     |
| `bench/`            | `scc-bench` and its codec wrappers             |

Vulnerability classes we are particularly interested in:

- **Codec parsing**: any input that causes `libscc` to crash, hang,
  read or write out-of-bounds, leak heap memory, or run for unbounded
  time on a bounded input. SCC is designed to safely reject malformed
  SEI payloads delivered over an untrusted network — bugs that break
  that property are high-priority.
- **Memory safety in the C ABI**: use-after-free, double-free,
  integer overflow / sign confusion in `libscc.h` arguments,
  unsoundness in the thread-local error mechanism.
- **WASM boundary**: anything that lets WebAssembly code escape the
  documented memory or arena bounds, plus issues in the TS adapter
  layer (`scc.ts`, `raw.ts`).
- **Studio API**: authentication / authorization bypass (API key,
  RS256 JWT), Postgres RLS policies that don't enforce
  `app.tenant_id`, SSRF, CSRF, server-side template / log injection,
  rate-limit / DoS issues that affect *other* tenants.
- **Studio agent**: IPC unsoundness, FFI lifetime bugs (the agent
  links libscc dynamically), unsafe Rust that violates a documented
  invariant (the codebase aims to keep all `unsafe` blocks
  reviewer-tractable).
- **Studio frontend**: XSS, CSRF on state-changing API calls,
  WebSocket subprotocol-token leakage, dependency-chain compromises.
- **Build / supply-chain**: malicious behaviour introduced via
  FetchContent dependencies, `npm ci` lockfile drift, or compromised
  release artefacts.

If you are unsure whether something falls in scope, please report it
anyway and we will tell you.

### Cryptographic note

SCC does not currently encrypt the bitstream; the codec produces
plaintext payloads which an outer transport (TLS, SRTP, etc.) is
expected to protect. **Reports of "the bitstream is not encrypted at
rest" are not vulnerabilities** — that is the documented design
(see `docs/SAD.md` §2.3 "Out of scope"). If, however, an outer-layer
encryption key, JWT secret, or DB credential leaks through SCC code,
that is a vulnerability and we want to know.

---

## Out of scope

To set expectations, the following are **not** considered
vulnerabilities under this policy and will be triaged accordingly:

1. Reports generated solely by automated scanners with no proof of
   exploitability against a built artefact.
2. Volumetric / brute-force denial of service (sending more requests
   than the system can handle, exhausting bandwidth, etc.).
3. Missing security headers on a developer-only or local-bind
   service (`vite dev`, `npm run dev`, agent's debug HTTP port).
4. Lack of rate limiting on endpoints documented as un-rate-limited
   (currently: none — but the policy may evolve).
5. Self-XSS that requires the victim to paste attacker-controlled
   content into their own browser console.
6. Issues affecting only end-of-life or unsupported versions (see
   "Supported versions" below).
7. Patent-licensing complaints. Use the
   [`licensing@djinn.cloud`](mailto:licensing@djinn.cloud) channel
   instead — that is not a security-team concern.
8. Social engineering of Djinn Technologies staff or contractors.
9. Physical attacks on sensor hardware, supply-chain compromises of
   third-party sensor SDKs (those are reported to the SDK vendor).
10. Theoretical vulnerabilities without a demonstrated exploit
    against current code.

---

## Coordinated disclosure

We follow industry-standard **coordinated disclosure**:

- Default embargo: **90 calendar days** from triage, or until a fix
  ships and downstream users have had a reasonable window to update
  — whichever is longer.
- We will publish a GitHub Security Advisory (and request a CVE
  through GitHub) on the disclosure date.
- We may extend the embargo by mutual agreement if the fix is
  unusually complex or affects shipped binaries that take time to
  rebuild and distribute.
- We may shorten the embargo if active exploitation is observed in
  the wild.
- Reporters who prefer a different timeline are welcome to discuss
  it in the initial report — we will work with you in good faith.

Please do **not** publish details, demonstrations, or proof-of-
concept code before the agreed disclosure date.

---

## Supported versions

| Version line                | Status                                 |
| --------------------------- | -------------------------------------- |
| `1.x` (current)             | ✅ Supported. Receives security fixes. |
| Pre-`1.0` development tags  | ❌ Not supported.                      |
| Experimental branches       | ❌ Not supported. Report against `main` instead. |

When `2.x` ships, `1.x` will continue to receive security fixes for
**12 months** from the date of the `2.0` release.

If you find an issue against an old commit on `main`, please retest
against the current `main` before reporting — the bug may already
have been fixed.

---

## Severity classification

We use **CVSS v3.1** to score reports, plus an internal tier
mapping that drives our SLA:

| Tier         | CVSS         | Examples                                                          |
| ------------ | ------------ | ----------------------------------------------------------------- |
| **Critical** | 9.0 – 10.0   | Remote code execution from a malformed SEI payload; auth bypass on the Studio API affecting all tenants. |
| **High**     | 7.0 – 8.9    | Out-of-bounds read leaking inter-tenant data; RLS policy that fails for a specific query shape. |
| **Medium**   | 4.0 – 6.9    | DoS via crafted input that crashes a single tenant's agent; XSS confined to a single tenant's UI. |
| **Low**      | 0.1 – 3.9    | Information disclosure with no privilege boundary crossed; defence-in-depth weaknesses. |

Tier determines the fix SLA:

| Tier      | Patch target            | Public advisory target |
| --------- | ----------------------- | ---------------------- |
| Critical  | within 7 calendar days  | within 14 days         |
| High      | within 30 calendar days | within 60 days         |
| Medium    | within 60 calendar days | within 90 days         |
| Low       | next minor release      | bundled with release notes |

---

## Acknowledgements

We do not currently operate a paid bug-bounty programme, but we
maintain a **public hall of fame** of security researchers who have
helped harden SCC. Unless you ask us not to, your name (or handle)
will be credited in the GitHub Security Advisory and listed in this
file when the advisory becomes public.

If you would like to contribute to a future bounty programme, let us
know in your initial report.

---

## Hardening and threat model

The project's high-level threat model is documented in
[`docs/15_threat_model.png`](docs/15_threat_model.png) and discussed
in `docs/SAD.md` §11. Architectural decisions affecting the security
posture are recorded in the relevant ADRs:

- [ADR-006 — C ABI](docs/adr/ADR-006-c-abi.md) — exception-safety boundary, thread-local errors.
- [ADR-007 — WASM binding](docs/adr/ADR-007-wasm-binding.md) — sandbox boundary.
- [ADR-011 — Capture agent](docs/adr/ADR-011-capture-agent.md) — IPC and unsafe-Rust policy.
- [ADR-012 — Control-plane API](docs/adr/ADR-012-control-plane-api.md) — RLS, dual-track auth, rate limiting.
- [ADR-013 — React frontend](docs/adr/ADR-013-react-frontend.md) — CSRF, WebSocket subprotocol auth.

Build- and supply-chain hardening:

- The C codec builds clean under **AddressSanitizer + UndefinedBehaviorSanitizer** on Linux/macOS (`asan` preset) and ASan on MSVC (`msvc-asan` preset). Reports of UB / OOB issues are taken seriously.
- A continuous fuzzing target lives in [`codec/fuzz/fuzz_sei.cpp`](codec/fuzz/fuzz_sei.cpp); coverage gaps you find are welcome reports.
- The C ABI exports a *baseline-checked* symbol set; CI fails on unexpected exports (see [`cabi/abi-baseline.txt`](cabi/abi-baseline.txt)).
- Conformance tests for every numbered REQ in `docs/Acceptance_Criteria.md` run in CI; see [`ci/conformance/`](ci/conformance/).

---

## Contact summary

| Channel                               | Use for                                      |
| ------------------------------------- | -------------------------------------------- |
| GitHub Security Advisories *(preferred)* | All security reports.                     |
| `security@djinn.cloud`                | Reports preferring email; PGP available on request. |
| `licensing@djinn.cloud`               | Patent / commercial-licensing enquiries (**not** security). |
| Public GitHub issues                  | Functional bugs and feature requests **only** — never security. |

---

*Maintained by Djinn Technologies Ltd., 111 New Union Street,
Coventry CV1 2NT, England (Company No. 13918535, registered in
England and Wales). This policy is reviewed annually; last revised
2026-05-07.*
