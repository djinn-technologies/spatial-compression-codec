# sdk/python/src/scc_py/_core.pyi
#
# Type stubs for the pybind11-built `_core` module.
#
# Designed to pass `mypy --strict` -- every method is fully annotated;
# no `Any` leaks. [Ultrathink #4]
#
# Note: forward references to `'Encoder'` / `'Decoder'` are used in
# `__enter__` instead of `typing.Self` so the stubs work on Python 3.10
# (Self was added in 3.11).

from __future__ import annotations

from types import TracebackType
from typing import Literal

import numpy as np
import numpy.typing as npt

SCCProfile = Literal["lossless", "lossy:high", "lossy:streaming"]
DepthArray = npt.NDArray[np.uint16]

class Encoder:
    """SCC encoder backed by the C ABI. [REQ-028]"""

    def __init__(
        self,
        profile: SCCProfile = "lossless",
        bit_depth: Literal[8, 12, 16] = 12,
    ) -> None: ...
    def encode(self, depth: DepthArray) -> bytes:
        """Encode a (H, W) uint16 array; return SEI payload bytes.

        Raises:
            TypeError: ``depth`` is not a 2-D C-contiguous uint16 ndarray.
            ValueError: ``H`` or ``W`` is out of (0, 65535].
            RuntimeError: the encoder is closed or the codec rejected the input.
        """
        ...
    def close(self) -> None: ...
    @property
    def closed(self) -> bool: ...
    def __enter__(self) -> "Encoder": ...
    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None: ...

class Decoder:
    """SCC decoder backed by the C ABI. [REQ-028]"""

    def __init__(self) -> None: ...
    def decode(self, sei: bytes) -> DepthArray:
        """Decode SEI bytes; return a (H, W) uint16 ndarray.

        Raises:
            ValueError: ``sei`` is empty.
            RuntimeError: the decoder is closed or the bytestream is malformed.
        """
        ...
    def close(self) -> None: ...
    @property
    def closed(self) -> bool: ...
    def __enter__(self) -> "Decoder": ...
    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None: ...
