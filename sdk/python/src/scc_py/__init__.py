"""Python binding for the Spatial Compression Codec (SCC).

The package re-exports :class:`Encoder` and :class:`Decoder` from the
native pybind11 extension. The accompanying ``_core.pyi`` describes the
public type surface for ``mypy --strict``.

[REQ-028, ADR-008]
"""

from __future__ import annotations

from ._core import Decoder, Encoder

__all__ = ["Encoder", "Decoder"]
__version__ = "1.0.0"
