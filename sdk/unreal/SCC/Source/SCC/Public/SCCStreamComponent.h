// sdk/unreal/SCC/Source/SCC/Public/SCCStreamComponent.h
//
// Blueprint-callable UActorComponent wrapping the SCC encoder/decoder.
//
// Evidence: [REQ-029, ADR-006, ADR-010].

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SCCStreamComponent.generated.h"

/**
 * Streaming component that encodes a depth frame to an SEI byte payload
 * and (separately) decodes an SEI payload back to a depth frame.
 *
 * The component owns up to two `scc_ctx*` handles:
 *   - one for encoding (created by BeginEncode, destroyed by EndEncode);
 *   - one for decoding (created lazily by DecodeSEI, destroyed by
 *     BeginDestroy).
 *
 * It is NOT thread-safe; one component per worker if parallelism is
 * required.
 */
UCLASS(ClassGroup=(SCC), meta=(BlueprintSpawnableComponent),
       Category="SCC")
class SCC_API USCCStreamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USCCStreamComponent();

    /**
     * Initialise an encoder for a fixed (Width, Height, BitDepth) stream.
     *
     * @param Width    Frame width in pixels (must be > 0 and <= 65535).
     * @param Height   Frame height in pixels.
     * @param BitDepth 8, 12, or 16.
     * @param Profile  "Lossless", "LossyHigh", or "LossyStreaming".
     * @return         true on success; false logs a LogSCC error.
     */
    UFUNCTION(BlueprintCallable, Category="SCC|Encode")
    bool BeginEncode(int32 Width, int32 Height, int32 BitDepth, FString Profile);

    /**
     * Encode one depth frame. Must be called between BeginEncode and
     * EndEncode. The Depth array is read directly via GetData() — no
     * per-element copy. [Ultrathink #3]
     *
     * @return SEI payload bytes (empty on failure).
     */
    UFUNCTION(BlueprintCallable, Category="SCC|Encode")
    TArray<uint8> EncodeFrame(const TArray<uint16>& Depth);

    /** Tear down the encoder. Idempotent. */
    UFUNCTION(BlueprintCallable, Category="SCC|Encode")
    void EndEncode();

    /**
     * Decode an SEI payload into a depth frame. Lazily creates a
     * decoder context on first call; reuses it for subsequent calls.
     *
     * @return true on success.
     */
    UFUNCTION(BlueprintCallable, Category="SCC|Decode")
    bool DecodeSEI(const TArray<uint8>& SEI, TArray<uint16>& OutDepth,
                   int32& OutWidth, int32& OutHeight, int32& OutBitDepth);

    /**
     * SDK version string, baked at build time from
     * SCC.Build.cs::SCCSDKVersion. [Ultrathink #5]
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="SCC")
    FString GetSCCSDKVersion() const;

    virtual void BeginDestroy() override;

private:
    /** Encoder libscc context. Stored as void* so libscc.h does not
     *  leak into this public header surface. */
    void* EncoderContext = nullptr;
    /** Decoder libscc context (lazily created). */
    void* DecoderContext = nullptr;

    UPROPERTY()
    int32 EncodeWidth = 0;
    UPROPERTY()
    int32 EncodeHeight = 0;
    UPROPERTY()
    int32 EncodeBitDepth = 12;
};
