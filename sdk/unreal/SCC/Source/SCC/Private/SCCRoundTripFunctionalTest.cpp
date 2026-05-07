// sdk/unreal/SCC/Source/SCC/Private/SCCRoundTripFunctionalTest.cpp

#if WITH_AUTOMATION_TESTS

#include "SCCRoundTripFunctionalTest.h"
#include "SCC.h"
#include "SCCStreamComponent.h"

ASCCRoundTripFunctionalTest::ASCCRoundTripFunctionalTest()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ASCCRoundTripFunctionalTest::StartTest()
{
    Super::StartTest();

    if (TestWidth <= 0 || TestHeight <= 0)
    {
        FinishTest(EFunctionalTestResult::Invalid,
            FString::Printf(TEXT("Invalid dimensions %dx%d"),
                TestWidth, TestHeight));
        return;
    }

    USCCStreamComponent* Comp = NewObject<USCCStreamComponent>(this);
    if (!Comp->BeginEncode(TestWidth, TestHeight, /*BitDepth=*/12, TestProfile))
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("BeginEncode returned false"));
        return;
    }

    TArray<uint16> Depth;
    Depth.SetNumUninitialized(TestWidth * TestHeight);
    uint32 RngState = 0xC0FFEEu;
    for (int32 i = 0; i < Depth.Num(); ++i)
    {
        RngState = RngState * 1664525u + 1013904223u;
        Depth[i] = static_cast<uint16>(RngState & 0x3FF);
    }

    const TArray<uint8> SEI = Comp->EncodeFrame(Depth);
    if (SEI.Num() == 0)
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("EncodeFrame produced empty bytes"));
        return;
    }

    TArray<uint16> Decoded;
    int32 W = 0, H = 0, BD = 0;
    if (!Comp->DecodeSEI(SEI, Decoded, W, H, BD))
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("DecodeSEI returned false"));
        return;
    }
    if (W != TestWidth || H != TestHeight)
    {
        FinishTest(EFunctionalTestResult::Failed,
            FString::Printf(TEXT("Decoded dims %dx%d != input %dx%d"),
                W, H, TestWidth, TestHeight));
        return;
    }
    if (Decoded.Num() != Depth.Num())
    {
        FinishTest(EFunctionalTestResult::Failed,
            FString::Printf(TEXT("Decoded.Num()=%d != Depth.Num()=%d"),
                Decoded.Num(), Depth.Num()));
        return;
    }
    for (int32 i = 0; i < Depth.Num(); ++i)
    {
        if (Decoded[i] != Depth[i])
        {
            FinishTest(EFunctionalTestResult::Failed,
                FString::Printf(TEXT("Mismatch at sample %d: got %u expected %u"),
                    i, Decoded[i], Depth[i]));
            return;
        }
    }

    Comp->EndEncode();
    FinishTest(EFunctionalTestResult::Succeeded, TEXT(""));
}

#endif // WITH_AUTOMATION_TESTS
