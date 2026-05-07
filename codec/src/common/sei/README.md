# SCC SEI payload — wire format

This directory ships the SCC SEI muxer / demuxer
(`scc::sei::mux` / `scc::sei::demux`). The compressed depth payload is
packaged as the body of an H.264 `user_data_unregistered` SEI message
(ISO/IEC 14496-10 §D.1.6) and is emulation-prevention-escaped per §7.4.1.1.

## Wire format (RBSP, before NAL escape)

All multi-byte integer fields are big-endian. Signed values use two's-
complement; the `int16` top values are sign-extended on read.

```
Offset (RBSP)   Bytes                                            Field
-------------   ----------------------------------------------   ----------------
+0 .. +15       16 bytes: SCC UUID (5cc1d4e7-...-167a45)         UUID         [REQ-013, ADR-005]
+16             u8                                                version_major (= 1)
+17             u8                                                version_minor (= 0)
+18             u16 BE                                            width
+20             u16 BE                                            height
+22             u8                                                bit_depth   {8, 12, 16}
+23             u8                                                mode_flags  bit0 = lossless ...
+24             u8                                                top_count   [2, 16]
+25             int16 BE × top_count                              top[]
+25 + 2*N       u32 BE                                            rans_top_len
+...            rans_top_len bytes                                rans_top
+...            u32 BE                                            rans_b_len
+...            rans_b_len bytes                                  rans_b
+...            u32 BE                                            rans_r_len
+...            rans_r_len bytes                                  rans_r
+...            (rest of the SEI payload)                         quadtree_descriptor
```

`quadtree_descriptor` has **no length prefix**: it consumes whatever bytes
remain until the SEI message envelope (the H.264 outer framing controls
the total length, so consumers always know the boundary).

After RBSP construction the muxer applies the §7.4.1.1
emulation-prevention transformation:

> For any 4 consecutive RBSP bytes `B0 B1 B2 B3` where `B0 == 0x00`,
> `B1 == 0x00`, and `B2 ∈ {0x00, 0x01, 0x02, 0x03}`, insert `0x03`
> between `B1` and `B2`.

The output bytes are the EBSP that the caller (libavcodec or equivalent)
wraps in a NAL unit. The demuxer reverses this transformation before
parsing.

## Annotated example

Inputs:

```
hdr           = { width = 1, height = 1, bit_depth = 8, mode_flags = 0x00 }
top           = [ 0x0001, 0x0002 ]                  (top_count = 2)
rans_top      = [ 0x42, 0x43 ]
rans_b        = [ 0x44 ]
rans_r        = [ ]
quadtree_desc = [ 0x55 ]
```

### RBSP layout (45 bytes)

```
Offset   Bytes                                              Description
------   ------------------------------------------------   --------------------------
0x00     5c c1 d4 e7 7e 93 4b 51 9d 3c fa 20 b8 16 7a 45    SCC UUID
0x10     01                                                  version_major
0x11     00                                                  version_minor
0x12     00 01                                               width  = 1   (BE)
0x14     00 01                                               height = 1   (BE)
0x16     08                                                  bit_depth = 8
0x17     00                                                  mode_flags = 0x00
0x18     02                                                  top_count = 2
0x19     00 01 00 02                                         top[] = [1, 2]      (int16 BE × 2)
0x1d     00 00 00 02                                         rans_top_len = 2    (u32 BE)
0x21     42 43                                               rans_top
0x23     00 00 00 01                                         rans_b_len = 1
0x27     44                                                  rans_b
0x28     00 00 00 00                                         rans_r_len = 0
0x2c     55                                                  quadtree_desc
```

### EBSP layout (after NAL escape)

The `00 00 00 02` (rans_top_len), `00 00 00 01` (rans_b_len) and
`00 00 00 00` (rans_r_len) fields each contain a `0x00 0x00 0x00`
sub-sequence and trigger the emulation-prevention escape:

```
Offset   Bytes                                              Notes
------   ------------------------------------------------   --------------------------
0x00     5c c1 d4 e7 7e 93 4b 51 9d 3c fa 20 b8 16 7a 45    UUID            (no escape)
0x10     01 00                                               version
0x12     00 01 00 01                                         width / height (no escape: 00 01 stops the run)
0x16     08 00 02                                             bit_depth, mode_flags, top_count
0x19     00 01 00 02                                         top values
0x1d     00 00 03 00 02                                       rans_top_len with inserted 0x03
0x22     42 43                                                rans_top bytes
0x24     00 00 03 00 01                                       rans_b_len  with inserted 0x03
0x29     44                                                   rans_b
0x2a     00 00 03 00 00                                       rans_r_len  with inserted 0x03
0x2f     55                                                   quadtree_desc
```

EBSP total length = 48 bytes (RBSP 45 + 3 inserted escape bytes).

`scc::sei::mux` returns the EBSP. `scc::sei::demux` accepts the EBSP,
de-escapes (recovers the RBSP), and parses the fields above.

## Validation order on demux

The demuxer validates aggressively before allocating any output buffer:

1. **Input cap.** If `sei_payload.size() > max_payload_bytes` the demuxer
   returns `{ok = false}` immediately.
2. **De-escape.** Output of `nal_unescape` is at most `input.size()` so
   the temporary RBSP buffer is bounded.
3. **Per-field bounds.** Every `u32` length is compared against (a) the
   cap and (b) the bytes remaining in the RBSP buffer *before* the
   `vector::assign(p, p + len)` allocation runs. An attacker cannot
   trigger a multi-gigabyte allocation from a tiny payload.

A round-trip property test (`sei_prop_random_bytes_no_crash`,
[test_sei.cpp](../../tests/test_sei.cpp)) feeds rapidcheck-generated
arbitrary input through `demux` and asserts no crash on any byte
sequence. A libFuzzer harness (`codec/fuzz/fuzz_sei.cpp`, gated by
`SCC_ENABLE_LIBFUZZER`) extends this to coverage-guided fuzzing under
Clang.

## References

- ISO/IEC 14496-10 (H.264) §D.1.6 — `user_data_unregistered` SEI message
  syntax: <https://www.itu.int/rec/T-REC-H.264>.
- ISO/IEC 14496-10 §7.4.1.1 — `emulation_prevention_three_byte`.
- ADR-005 — SCC SEI UUID registration and validation order.
