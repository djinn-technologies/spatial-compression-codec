# scc-py

Python binding for the **Spatial Compression Codec** — encode and decode
H.264-compatible depth-frame SEI payloads with NumPy zero-copy interop.

## Install

```bash
pip install scc-py
```

Wheels are published for Python 3.10–3.13 on Linux x86_64 / aarch64,
macOS universal2, and Windows AMD64. The pure-source `sdist` builds from
source on any platform with a C++17 compiler + CMake ≥ 3.27.

## Five-line example

```python
import numpy as np
from scc_py import Encoder, Decoder

depth = np.random.randint(0, 1000, size=(720, 1280), dtype=np.uint16)
with Encoder(profile="lossless", bit_depth=12) as enc, Decoder() as dec:
    sei = enc.encode(depth)
    decoded = dec.decode(sei)
assert np.array_equal(depth, decoded)
```

## API

```python
class Encoder:
    def __init__(
        self,
        profile: Literal["lossless", "lossy:high", "lossy:streaming"] = "lossless",
        bit_depth: Literal[8, 12, 16] = 12,
    ) -> None: ...
    def encode(self, depth: np.ndarray) -> bytes: ...
    def close(self) -> None: ...
    def __enter__(self) -> Encoder: ...
    def __exit__(self, *a) -> None: ...

class Decoder:
    def __init__(self) -> None: ...
    def decode(self, sei: bytes) -> np.ndarray: ...
    def close(self) -> None: ...
    def __enter__(self) -> Decoder: ...
    def __exit__(self, *a) -> None: ...
```

`Encoder.encode` requires a 2-D C-contiguous `uint16` NumPy array. The
buffer is passed straight to the codec — **no Python-side copy** of the
input. Wrong dtype or stride raises `TypeError`.

`Decoder.decode` returns a freshly-allocated `(H, W) uint16` NumPy array
populated directly by the codec — one allocation, no intermediate copy.

## Profiles

| Profile             | Use case                                          |
|---------------------|---------------------------------------------------|
| `lossless`          | Capture / archival. Bit-exact round-trip.        |
| `lossy:high`        | Visual fidelity prioritised over bandwidth.       |
| `lossy:streaming`   | Real-time streaming. Lower bitrate, motion-aware. |

## Type checking

`scc-py` ships PEP-561 type info (`py.typed` + `_core.pyi`). `mypy --strict`
sees fully-typed `Encoder` / `Decoder` with `Literal` profile values, no
`Any` leaks.

```bash
mypy --strict your_code.py
```

## Building from source

Requires Python ≥ 3.10, a C++17 compiler, and CMake ≥ 3.27. Inside the
repo:

```bash
cd sdk/python
pip install --editable ".[dev]"          # editable install with dev deps
pytest tests/                             # run the round-trip tests
mypy --strict src/scc_py                  # check the stubs
```

For a release build of the wheel:

```bash
pip install build
python -m build --wheel
```

## License

Apache-2.0.
