"""sdk/python/tests/test_roundtrip.py

pytest tests for scc-py.

Mapped to AI prompt #8 test list:

    1. Round-trip on a synthetic frame.
    2. Wrong dtype (e.g. float32) -> TypeError.
    3. Non-contiguous input -> TypeError.
    4. Context manager closes the underlying C ABI handle on exception
       (Ultrathink #3).

Plus profile coverage and idempotent close.
"""

from __future__ import annotations

import numpy as np
import pytest

from scc_py import Decoder, Encoder


def make_frame(H: int, W: int, seed: int = 0xC0FFEE) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return rng.integers(0, 1000, size=(H, W), dtype=np.uint16)


# ---------------------------------------------------------------------------
# Round-trip
# ---------------------------------------------------------------------------


def test_roundtrip_small_frame() -> None:
    depth = make_frame(16, 16)
    with Encoder(profile="lossless", bit_depth=12) as enc, Decoder() as dec:
        sei = enc.encode(depth)
        out = dec.decode(sei)
    assert isinstance(sei, bytes)
    assert len(sei) > 0
    assert out.dtype == np.uint16
    assert out.shape == (16, 16)
    assert np.array_equal(out, depth)


def test_roundtrip_realistic_size() -> None:
    depth = make_frame(64, 96, seed=0xDEADBEEF)
    with Encoder() as enc, Decoder() as dec:
        sei = enc.encode(depth)
        out = dec.decode(sei)
    assert np.array_equal(out, depth)


@pytest.mark.parametrize(
    "profile",
    ["lossless", "lossy:high", "lossy:streaming"],
)
def test_each_profile(profile: str) -> None:
    depth = make_frame(8, 8, seed=hash(profile) & 0xFFFF_FFFF)
    with Encoder(profile=profile) as enc:  # type: ignore[arg-type]
        sei = enc.encode(depth)
    assert len(sei) > 0


# ---------------------------------------------------------------------------
# dtype / contiguity validation
# ---------------------------------------------------------------------------


def test_wrong_dtype_raises_typeerror() -> None:
    enc = Encoder()
    try:
        with pytest.raises(TypeError):
            enc.encode(np.zeros((4, 4), dtype=np.float32))  # type: ignore[arg-type]
        with pytest.raises(TypeError):
            enc.encode(np.zeros((4, 4), dtype=np.int16))    # type: ignore[arg-type]
        with pytest.raises(TypeError):
            enc.encode(np.zeros((4, 4), dtype=np.uint8))    # type: ignore[arg-type]
    finally:
        enc.close()


def test_non_contiguous_input_raises_typeerror() -> None:
    # A strided view: take every other column.
    base = np.zeros((8, 16), dtype=np.uint16)
    view = base[:, ::2]
    assert not view.flags["C_CONTIGUOUS"]
    with Encoder() as enc, pytest.raises(TypeError, match="C-contiguous"):
        enc.encode(view)


def test_wrong_ndim_raises_typeerror() -> None:
    with Encoder() as enc, pytest.raises(TypeError, match="2-D"):
        enc.encode(np.zeros((4, 4, 1), dtype=np.uint16))
    with Encoder() as enc, pytest.raises(TypeError, match="2-D"):
        enc.encode(np.zeros((16,), dtype=np.uint16))


# ---------------------------------------------------------------------------
# Decoder validation
# ---------------------------------------------------------------------------


def test_malformed_sei_raises_runtimeerror() -> None:
    with Decoder() as dec:
        with pytest.raises(RuntimeError):
            dec.decode(b"\x00" * 32)
        with pytest.raises(RuntimeError):
            dec.decode(b"not an SCC SEI payload at all" + b"\xff" * 64)


def test_empty_sei_raises_valueerror() -> None:
    with Decoder() as dec:
        with pytest.raises((ValueError, RuntimeError)):
            dec.decode(b"")


# ---------------------------------------------------------------------------
# Context manager + close semantics  (Ultrathink #3)
# ---------------------------------------------------------------------------


def test_context_manager_closes_on_exception() -> None:
    """The context manager must release the C ABI handle even when the
    body raises; subsequent encode() calls should fail cleanly rather
    than crash the interpreter."""
    enc = Encoder()
    raised = False
    try:
        with enc:
            depth = make_frame(8, 8)
            assert enc.encode(depth)
            raise RuntimeError("deliberate")
    except RuntimeError as e:
        raised = (str(e) == "deliberate")
    assert raised, "the deliberate exception was swallowed"
    assert enc.closed
    # And calls after close raise a clean RuntimeError (not segfault).
    with pytest.raises(RuntimeError):
        enc.encode(make_frame(8, 8))


def test_double_close_is_idempotent() -> None:
    enc = Encoder()
    enc.close()
    enc.close()
    assert enc.closed


def test_decoder_context_manager() -> None:
    dec = Decoder()
    with dec as d:
        assert d is dec
    assert dec.closed
    with pytest.raises(RuntimeError):
        dec.decode(b"\x00" * 32)


def test_invalid_profile_raises() -> None:
    with pytest.raises(ValueError, match="unknown profile"):
        Encoder(profile="not-a-profile")  # type: ignore[arg-type]


def test_invalid_bit_depth_raises() -> None:
    with pytest.raises(ValueError, match="bit_depth"):
        Encoder(bit_depth=10)  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# Zero-copy property check  (Ultrathink #1)
# ---------------------------------------------------------------------------


def test_encode_does_not_mutate_input() -> None:
    """If the binding accidentally wrote to the input via a non-const
    buffer, this would catch it. Equivalent to a 'no aliasing' check."""
    depth = make_frame(32, 32, seed=0xAA)
    snapshot = depth.copy()
    with Encoder() as enc:
        enc.encode(depth)
    assert np.array_equal(depth, snapshot)
