// sdk/unreal/SCC/Source/SCC/Private/SCCAutomationTests.cpp
//
// Headless automation tests. Run via:
//
//   UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests SCC."
//                    -unattended -nopause -nullrhi -log
//
// These do NOT require a map -- they construct a USCCStreamComponent in
// memory and exercise the public surface. CI runs them on every commit.
// [Ultrathink #2]

#if WITH_AUTOMATION_TESTS

#include "SCCStreamComponent.h"
#include "SCC.h"

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
    constexpr int32 kAutomationFlags =
        EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSCCRoundTripAutomationTest,
    "SCC.RoundTrip.SyntheticFrame",
    kAutomationFlags)

bool FSCCRoundTripAutomationTest::RunTest(const FString& Parameters)
{
    USCCStreamComponent* Comp = NewObject<USCCStreamComponent>();
    if (!Comp)
    {
        AddError(TEXT("NewObject<USCCStreamComponent> returned null"));
        return false;
    }

    constexpr int32 W = 16, H = 16;
    if (!TestTrue(TEXT("BeginEncode"),
                  Comp->BeginEncode(W, H, 12, TEXT("Lossless"))))
        return false;

    TArray<uint16> Depth;
    Depth.SetNumUninitialized(W * H);
    uint32 RngState = 0xC0FFEEu;
    for (int32 i = 0; i < Depth.Num(); ++i)
    {
        RngState = RngState * 1664525u + 1013904223u;
        Depth[i] = static_cast<uint16>(RngState & 0x3FF);
    }

    const TArray<uint8> SEI = Comp->EncodeFrame(Depth);
    if (!TestTrue(TEXT("EncodeFrame produced bytes"), SEI.Num() > 0)) return false;

    TArray<uint16> Decoded;
    int32 OutW = 0, OutH = 0, OutBD = 0;
    if (!TestTrue(TEXT("DecodeSEI"),
                  Comp->DecodeSEI(SEI, Decoded, OutW, OutH, OutBD))) return false;
    if (!TestEqual(TEXT("Width"),  OutW,  W))  return false;
    if (!TestEqual(TEXT("Height"), OutH,  H))  return false;
    if (!TestEqual(TEXT("BitDepth"), OutBD, 12)) return false;
    if (!TestEqual(TEXT("Decoded.Num"), Decoded.Num(), Depth.Num())) return false;

    for (int32 i = 0; i < Depth.Num(); ++i)
    {
        if (Decoded[i] != Depth[i])
        {
            AddError(FString::Printf(
                TEXT("Mismatch at sample %d: %u != %u"),
                i, Decoded[i], Depth[i]));
            return false;
        }
    }

    Comp->EndEncode();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSCCEncodeBeforeBeginRejected,
    "SCC.RoundTrip.EncodeWithoutBegin",
    kAutomationFlags)

bool FSCCEncodeBeforeBeginRejected::RunTest(const FString& Parameters)
{
    USCCStreamComponent* Comp = NewObject<USCCStreamComponent>();
    TArray<uint16> Depth; Depth.SetNumUninitialized(16);
    const TArray<uint8> SEI = Comp->EncodeFrame(Depth);
    return TestEqual(TEXT("Empty bytes returned"), SEI.Num(), 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSCCDecodeOnGarbageRejected,
    "SCC.RoundTrip.DecodeOnGarbage",
    kAutomationFlags)

bool FSCCDecodeOnGarbageRejected::RunTest(const FString& Parameters)
{
    USCCStreamComponent* Comp = NewObject<USCCStreamComponent>();
    TArray<uint8> Junk;
    Junk.SetNumUninitialized(64);
    for (int32 i = 0; i < Junk.Num(); ++i) Junk[i] = static_cast<uint8>(i * 31);
    TArray<uint16> Out; int32 W = 0, H = 0, BD = 0;
    return TestFalse(TEXT("DecodeSEI on garbage returns false"),
                     Comp->DecodeSEI(Junk, Out, W, H, BD));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSCCEachProfileEncodesOK,
    "SCC.RoundTrip.AllProfiles",
    kAutomationFlags)

bool FSCCEachProfileEncodesOK::RunTest(const FString& Parameters)
{
    constexpr int32 W = 8, H = 8;
    TArray<uint16> Depth;
    Depth.SetNumUninitialized(W * H);
    for (int32 i = 0; i < Depth.Num(); ++i) Depth[i] = static_cast<uint16>(i * 17);

    for (const TCHAR* Profile : {TEXT("Lossless"), TEXT("LossyHigh"), TEXT("LossyStreaming")})
    {
        USCCStreamComponent* Comp = NewObject<USCCStreamComponent>();
        if (!TestTrue(FString::Printf(TEXT("BeginEncode(%s)"), Profile),
                      Comp->BeginEncode(W, H, 12, FString(Profile)))) return false;
        const TArray<uint8> SEI = Comp->EncodeFrame(Depth);
        if (!TestTrue(FString::Printf(TEXT("EncodeFrame(%s)"), Profile),
                      SEI.Num() > 0)) return false;
        Comp->EndEncode();
    }
    return true;
}

#endif // WITH_AUTOMATION_TESTS
