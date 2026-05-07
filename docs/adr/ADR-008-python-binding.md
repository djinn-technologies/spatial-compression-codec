# ADR-008 — Python binding (`scc-py`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-028]`, `[ADR-006]`, `[ADR-007]`                              |

## Context

The codec needs a first-class Python binding for analysis tools, ML
data pipelines, and teaching notebooks. The constraints are familiar:

1. **Surface stability.** Per ADR-006 the C ABI is the SOLE public
   surface for downstream language bindings. The Python binding must
   call through `libscc.h`, not include any `scc/` C++ headers.
2. **Zero-copy NumPy interop.** Depth frames are typically 1280×720
   uint16 ≈ 1.84 MiB. Copying the whole array into a pybind buffer on
   every encode would be visible in the perf budget.
3. **Type safety.** `mypy --strict` should pass on the stubs without
   any `Any` leaks (Ultrathink #4).
4. **Self-contained wheels.** `pip install scc-py` should work on a
   fresh venv with no system codec library installed; the wheel bundles
   the codec directly.
5. **Multi-platform / multi-Python wheels.** cp310/cp311/cp312/cp313 ×
   {linux x86_64, linux aarch64, macos universal2, windows amd64} =
   16 wheels, built via cibuildwheel in CI (Ultrathink #2).

## Decision

### 1. pybind11 ≥ 2.12 + scikit-build-core.

- **pybind11** is the de-facto C++/Python bridge. Native NumPy support
  via `py::array_t<T>`, ergonomic class bindings, automatic dispatch on
  argument types. CFFI/ctypes would force us to redeclare the C ABI in
  Python; Cython would split logic between two compilation toolchains.
- **scikit-build-core** is the modern PEP 517 build backend with first-
  class CMake integration. It hands the CMake build off to `pybind11`'s
  CMake helper (`pybind11_add_module`) and handles wheel packaging.

The pyproject.toml's `[build-system].requires` lists both packages so
`pip install` (or cibuildwheel) provisions them automatically into the
isolated build environment.

### 2. Static-link the codec into a single self-contained module.

`sdk/python/CMakeLists.txt` enumerates the codec sources directly:

```cmake
set(CODEC_SOURCES
    ${REPO_ROOT}/codec/src/common/rans.cpp
    ${REPO_ROOT}/codec/src/common/disparity.cpp
    ${REPO_ROOT}/codec/src/common/frequency.cpp
    ${REPO_ROOT}/codec/src/common/quadtree.cpp
    ${REPO_ROOT}/codec/src/common/sei/sei.cpp
    ${REPO_ROOT}/cabi/src/libscc.cpp
)
pybind11_add_module(_core MODULE src/scc_py/_core.cpp ${CODEC_SOURCES})
```

The result is a single `_core.so` / `_core.pyd` / `_core.dylib` shipped
inside the wheel. No `auditwheel`/`delocate` shim, no runtime hunt for
`libscc.so` — the wheel is fully relocatable. The cost is that scc-py
and the native `libscc.so` (if present) are independent codec instances;
that's acceptable because the C ABI is the only stable contract and
both go through it.

We do NOT `add_subdirectory()` the parent CMake project because that
would pull in the full test/bench tree (FetchContent of Catch2,
rapidcheck, etc.). Enumerating sources directly is a small price for
a clean, fast Python build.

### 3. Zero-copy NumPy interop on encode (Ultrathink #1).

The `Encoder.encode` signature is

```cpp
py::bytes encode(const py::array_t<std::uint16_t>& depth);
```

pybind11's dispatch on `py::array_t<uint16_t>` rejects wrong-dtype
arrays at the boundary (TypeError, no copy attempted). The bare type
without the `py::array::c_style` flag does NOT auto-convert non-
contiguous arrays; we explicitly check `flags() & py::array::c_style`
and raise TypeError on stride mismatch.

For the contiguous uint16 case, `depth.data()` returns a `const
uint16_t*` pointing directly into NumPy's buffer. We `reinterpret_cast`
to `const uint8_t*` and pass to `scc_encode_frame`. **No bytes copied
into pybind buffers** on the input side — verified by
`test_encode_does_not_mutate_input` in tests/test_roundtrip.py.

The output `py::bytes` does involve one copy from the encoder's
internal `std::string` into Python's `bytes` heap. This is unavoidable
because Python `bytes` own their memory.

### 4. Zero-copy on decode output.

`Decoder.decode` allocates the result NumPy array first, then has the
decoder write directly into its buffer:

```cpp
py::array_t<std::uint16_t> result({H, W});
auto buf = result.request(/*writable=*/true);
scc_decode_sei(ctx_, sei_data, sei_len,
               static_cast<uint8_t*>(buf.ptr), W * 2,
               &W, &H, &BD);
return result;
```

One allocation (the NumPy array), no intermediate `std::string` /
`std::vector` copy. The probe-then-allocate pattern is identical to
the WASM binding's (see ADR-007 §5).

For decode INPUT (the `bytes` parameter), we use `PyBytes_AsStringAndSize`
directly to get a `const char*` view without the `std::string` copy that
pybind11's automatic conversion would do.

### 5. GIL held throughout encode/decode.

Releasing the GIL would be a pure win in principle: the C ABI runs on
plain C arrays and never touches Python objects. In practice, releasing
the GIL while we hold a raw pointer into a NumPy array opens a window
for another Python thread to mutate or reallocate the array under us.

For v1: hold the GIL. The encode/decode work is bounded (a few ms per
frame at 1280×720) and applications doing heavy multi-threading
typically allocate one Encoder per worker thread anyway.

A future optimisation could:
- Snapshot the array dimensions into stack values BEFORE releasing.
- Hold a `py::buffer_info` reference (which keeps NumPy from realloc'ing).
- `py::gil_scoped_release` for the duration of the C call.

Documented as deferred.

### 6. Hand-written `.pyi` stubs; mypy --strict clean.

The `_core` extension's signature is auto-generated by pybind11 with
generic types (e.g., `numpy.ndarray` instead of `npt.NDArray[np.uint16]`).
For mypy --strict to be happy without `Any` leaks, we ship a hand-
written `_core.pyi` with:

- `Literal["lossless", "lossy:high", "lossy:streaming"]` for `profile`.
- `Literal[8, 12, 16]` for `bit_depth`.
- `npt.NDArray[np.uint16]` for the depth array type.
- Standard `__exit__` signature with `type[BaseException] | None` etc.

A `py.typed` marker (PEP 561) tells type checkers to use the stubs
that ship inside the package.

`__enter__` returns `'Encoder'` / `'Decoder'` as a forward-reference
string instead of `typing.Self` so the stubs work on Python 3.10
(Self landed in 3.11). [Ultrathink #4]

### 7. cibuildwheel matrix (Ultrathink #2).

`pyproject.toml [tool.cibuildwheel]` declares:

```
build = ["cp310-*", "cp311-*", "cp312-*", "cp313-*"]
skip  = ["*-musllinux_*", "pp*"]
```

with platform sub-tables for Linux (x86_64, aarch64), macOS
(universal2), and Windows (AMD64). 4 Pythons × 4 platforms = 16
wheels per release. The `test-command = "pytest"` clause runs the
round-trip suite inside each wheel's environment so a regression on
any platform fails the matrix build.

The CI workflow that drives cibuildwheel is one of the deferred items
in this milestone (no `.github/` is in the repo yet), but the
`pyproject.toml` settings are ready for any cibuildwheel runner to
pick up.

## Consequences

### Positive

- One Python build artefact per (cpython × OS) combination. No
  runtime DLL search, no LD_LIBRARY_PATH dance.
- pybind11's `py::array_t<uint16_t>` dispatch gives free dtype
  validation; no manual buffer protocol code.
- Zero-copy on the hot path: encode reads NumPy in place, decode
  writes NumPy in place.
- Type stubs are first-class (.pyi + py.typed); IDEs autocomplete the
  whole API including the profile literals.

### Negative

- The Python wheel and the native `libscc.so` are independent codec
  builds. Bug fixes need to land in two places (or, more accurately,
  one C++ source tree that both consume — which is what we already
  have).
- GIL held during encode/decode limits Python-level concurrency to
  one-encoder-per-thread. Documented; v2 candidate.

### Deferred

- **GIL release** during the C-side work, with explicit documentation
  that the input array must not be mutated concurrently.
- **Buffer protocol on output bytes.** The `bytes` return forces one
  copy into the Python heap. A future API could expose the result as
  a memoryview backed by a custom-allocated buffer + `__buffer__`
  protocol, avoiding the copy.
- **Direct C++ binding** that bypasses the C ABI for in-process Python
  use. The C ABI's exception-to-error-code translation has a small
  cost; calling pybind11 → C++ directly skips it. Today we go through
  the C ABI for ABI-stability symmetry with WASM/Rust/Swift bindings.

## Verification

- `tests/test_roundtrip.py` covers:
  - round-trip on a 16×16 random frame and on a 64×96 frame;
  - all three named profiles;
  - wrong dtype (float32, int16, uint8) raising TypeError;
  - non-contiguous strided view raising TypeError;
  - empty / malformed SEI raising the appropriate exception class;
  - context-manager-on-exception correctly closing the C ABI handle
    and rejecting subsequent calls (Ultrathink #3);
  - idempotent close;
  - `enc.encode` not mutating the input (zero-copy property check).
- `mypy --strict src/scc_py` passes with the hand-written `.pyi`
  (Ultrathink #4). Setting is in `[tool.mypy]`.
- `pyproject.toml [tool.cibuildwheel]` declares the 16-wheel matrix
  (Ultrathink #2). Actual matrix run is gated on a CI workflow.

The full build + test cycle requires Python 3.10+ + scikit-build-core
+ pybind11 + a C++17 compiler on the host; the on-disk source is
committed and ready for `pip install .` on any matrix entry.

## References

- AI Build Prompt #8 (`docs/AI_Build_Prompts.md` §8).
- ADR-006 (C ABI) — the surface this binding wraps.
- ADR-007 (WASM) — sibling binding with the same memory-ownership
  pattern.
- Internal: `docs/SAD.md` §6.1.7 (SDK bindings); REQ-028 in
  `docs/Acceptance_Criteria.md`.
