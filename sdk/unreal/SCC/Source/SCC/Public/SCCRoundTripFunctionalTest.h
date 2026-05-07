// sdk/unreal/SCC/Source/SCC/Public/SCCRoundTripFunctionalTest.h
//
// AFunctionalTest derivative for in-map round-trip verification.
//
// Usage: drop one of these actors into Content/SCC/Tests/SCCRoundTripMap.umap
// (created in-editor; see Content/SCC/Tests/README.md). The map runs in
// PIE; the FunctionalTest reports pass/fail via Session Frontend.
//
// For headless CI, prefer the IMPLEMENT_SIMPLE_AUTOMATION_TEST in
// SCCAutomationTests.cpp -- it does not require a map. [Ultrathink #2]

#pragma once

#if WITH_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "SCCRoundTripFunctionalTest.generated.h"

UCLASS(Blueprintable)
class SCC_API ASCCRoundTripFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()

public:
    ASCCRoundTripFunctionalTest();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SCC")
    int32 TestWidth = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SCC")
    int32 TestHeight = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SCC")
    FString TestProfile = TEXT("Lossless");

    virtual void StartTest() override;
};

#endif // WITH_AUTOMATION_TESTS
