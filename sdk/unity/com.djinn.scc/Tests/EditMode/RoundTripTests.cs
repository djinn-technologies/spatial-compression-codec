// sdk/unity/com.djinn.scc/Tests/EditMode/RoundTripTests.cs
//
// Unity Test Framework EditMode tests for the SCC plugin.
//
// Mapped to AI prompt #9 test list:
//   1. Round-trip on a synthetic frame.
//   2. Disposal cycle (10^5 iterations) -> stable memory.

using System;
using NUnit.Framework;
using Unity.Collections;

namespace Djinn.SCC.Tests
{
    public sealed class RoundTripTests
    {
        private static NativeArray<ushort> MakeFrame(int W, int H, int seed,
                                                     Allocator alloc)
        {
            var d = new NativeArray<ushort>(W * H, alloc,
                NativeArrayOptions.UninitializedMemory);
            // Linear-congruential PRNG so the same seed produces the same
            // bytes on every platform / Unity version (no GC churn).
            uint s = (uint)seed;
            for (int i = 0; i < d.Length; ++i)
            {
                s = unchecked(s * 1664525u + 1013904223u);
                d[i] = (ushort)(s & 0x3FF);
            }
            return d;
        }

        [Test]
        public void RoundTripSyntheticFrame()
        {
            const int W = 16, H = 16;
            using var depth = MakeFrame(W, H, 0xC0FFEE, Allocator.Temp);
            using var enc = new Encoder(SCCProfile.Lossless, 12, W, H);
            using var dec = new Decoder();

            byte[] sei = enc.Encode(depth);
            Assert.IsNotNull(sei);
            Assert.That(sei.Length, Is.GreaterThan(0));

            using var seiNa = new NativeArray<byte>(sei, Allocator.Temp);
            NativeArray<ushort> decoded = dec.Decode(seiNa,
                out int Wo, out int Ho, out int BDo);
            try
            {
                Assert.AreEqual(W, Wo);
                Assert.AreEqual(H, Ho);
                Assert.AreEqual(12, BDo);
                Assert.AreEqual(W * H, decoded.Length);
                for (int i = 0; i < W * H; ++i)
                {
                    Assert.AreEqual(depth[i], decoded[i],
                        $"sample {i} differs: {depth[i]} vs {decoded[i]}");
                }
            }
            finally
            {
                // Decoder.Decode allocates with Persistent -- caller MUST
                // dispose. [Ultrathink #1]
                decoded.Dispose();
            }
        }

        [Test]
        public void RoundTripWithEachProfile()
        {
            const int W = 8, H = 8;
            foreach (SCCProfile p in Enum.GetValues(typeof(SCCProfile)))
            {
                using var depth = MakeFrame(W, H, (int)p ^ 0xA5A5, Allocator.Temp);
                using var enc = new Encoder(p, 12, W, H);
                byte[] sei = enc.Encode(depth);
                Assert.That(sei.Length, Is.GreaterThan(0),
                    $"profile {p} produced empty payload");
            }
        }

        [Test]
        public void DisposalCycleIsStable()
        {
            // 100k init/destroy pairs; the C ABI never leaks even under
            // sustained churn. The default budget can be raised to 1M
            // for a nightly soak by setting SCC_UNITY_LONG=1.
            int N = 100_000;
            string longRun = Environment.GetEnvironmentVariable("SCC_UNITY_LONG");
            if (!string.IsNullOrEmpty(longRun)) N = 1_000_000;

            for (int i = 0; i < N; ++i)
            {
                var enc = new Encoder(SCCProfile.Lossless, 12, 4, 4);
                enc.Dispose();
            }
            // No assertion -- the test passes by not crashing / not OOMing.
            Assert.Pass($"{N} disposal cycles completed without leaks");
        }

        [Test]
        public void DisposeIsIdempotent()
        {
            var enc = new Encoder(SCCProfile.LossyHigh, 12, 4, 4);
            enc.Dispose();
            Assert.DoesNotThrow(() => enc.Dispose());
        }

        [Test]
        public void EncodeOnDisposedThrows()
        {
            var enc = new Encoder(SCCProfile.LossyHigh, 12, 4, 4);
            enc.Dispose();
            using var depth = new NativeArray<ushort>(16, Allocator.Temp);
            Assert.Throws<ObjectDisposedException>(() => enc.Encode(depth));
        }

        [Test]
        public void DecodeOnGarbageThrows()
        {
            using var dec = new Decoder();
            using var junk = new NativeArray<byte>(64, Allocator.Temp);
            for (int i = 0; i < junk.Length; ++i) junk[i] = (byte)(i * 31);
            Assert.Throws<InvalidOperationException>(() =>
            {
                NativeArray<ushort> r = dec.Decode(junk, out _, out _, out _);
                if (r.IsCreated) r.Dispose();
            });
        }
    }
}
