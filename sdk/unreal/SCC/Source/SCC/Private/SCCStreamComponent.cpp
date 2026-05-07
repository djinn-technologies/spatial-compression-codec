// sdk/unreal/SCC/Source/SCC/Private/SCCStreamComponent.cpp

#include "SCCStreamComponent.h"
#include "SCC.h"

extern "C" {
#include "libscc.h"
}

namespace
{
    bool ApplyProfileParams(scc_ctx* Ctx, const FString& Profile)
    {
        struct FParamSet { const char* mode; const char* top; const char* tau_s; const char* tau_l; };
        FParamSet P{};
        if (Profile.Equals(TEXT("Lossless"),       ESearchCase::IgnoreCase))
            P = { "1", "4", "1",  "5"  };
        else if (Profile.Equals(TEXT("LossyHigh"),  ESearchCase::IgnoreCase))
            P = { "0", "4", "5",  "30" };
        else if (Profile.Equals(TEXT("LossyStreaming"), ESearchCase::IgnoreCase))
            P = { "2", "4", "10", "50" };
        else
        {
            UE_LOG(LogSCC, Error,
                TEXT("Unknown profile '%s'; expected Lossless / LossyHigh / LossyStreaming"),
                *Profile);
            return false;
        }
        bool bOk = true;
        bOk &= scc_set_param(Ctx, "mode_flags", P.mode)  == SCC_OK;
        bOk &= scc_set_param(Ctx, "top_count",  P.top)   == SCC_OK;
        bOk &= scc_set_param(Ctx, "tau_static", P.tau_s) == SCC_OK;
        bOk &= scc_set_param(Ctx, "tau_low",    P.tau_l) == SCC_OK;
        return bOk;
    }

    FString GetLastErrorString(scc_ctx* Ctx)
    {
        if (!Ctx) return FString();
        const char* Err = scc_get_last_error(Ctx);
        return Err ? UTF8_TO_TCHAR(Err) : FString();
    }
}

USCCStreamComponent::USCCStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USCCStreamComponent::BeginDestroy()
{
    EndEncode();
    if (DecoderContext)
    {
        scc_destroy(static_cast<scc_ctx*>(DecoderContext));
        DecoderContext = nullptr;
    }
    Super::BeginDestroy();
}

bool USCCStreamComponent::BeginEncode(int32 Width, int32 Height, int32 BitDepth,
                                      FString Profile)
{
    EndEncode();   // Idempotent reset.

    if (Width  <= 0 || Width  > 65535 ||
        Height <= 0 || Height > 65535)
    {
        UE_LOG(LogSCC, Error, TEXT("BeginEncode: invalid dimensions %dx%d"),
            Width, Height);
        return false;
    }
    if (BitDepth != 8 && BitDepth != 12 && BitDepth != 16)
    {
        UE_LOG(LogSCC, Error, TEXT("BeginEncode: BitDepth must be 8/12/16; got %d"),
            BitDepth);
        return false;
    }

    scc_ctx* Ctx = scc_init();
    if (!Ctx)
    {
        UE_LOG(LogSCC, Error, TEXT("BeginEncode: scc_init failed"));
        return false;
    }
    if (!ApplyProfileParams(Ctx, Profile))
    {
        UE_LOG(LogSCC, Error, TEXT("BeginEncode: profile setup failed: %s"),
            *GetLastErrorString(Ctx));
        scc_destroy(Ctx);
        return false;
    }

    EncoderContext  = Ctx;
    EncodeWidth     = Width;
    EncodeHeight    = Height;
    EncodeBitDepth  = BitDepth;
    return true;
}

TArray<uint8> USCCStreamComponent::EncodeFrame(const TArray<uint16>& Depth)
{
    TArray<uint8> Result;
    if (!EncoderContext)
    {
        UE_LOG(LogSCC, Error, TEXT("EncodeFrame called before BeginEncode"));
        return Result;
    }
    const int32 Expected = EncodeWidth * EncodeHeight;
    if (Depth.Num() != Expected)
    {
        UE_LOG(LogSCC, Error,
            TEXT("EncodeFrame: Depth.Num()=%d != Width*Height=%d"),
            Depth.Num(), Expected);
        return Result;
    }

    scc_ctx* Ctx = static_cast<scc_ctx*>(EncoderContext);

    // [Ultrathink #3] -- TArray<uint16>::GetData() is a contiguous
    // pointer; we reinterpret_cast to const uint8* and pass directly
    // to the C ABI. No per-element copy.
    const uint8_t* DepthData = reinterpret_cast<const uint8_t*>(Depth.GetData());
    const size_t   Stride    = static_cast<size_t>(EncodeWidth) * sizeof(uint16);

    // Probe required size.
    size_t Required = 0;
    scc_encode_frame(Ctx, DepthData, Stride,
                     EncodeWidth, EncodeHeight, EncodeBitDepth,
                     /*out_sei=*/nullptr, /*out_cap=*/0, &Required);
    if (Required == 0)
    {
        UE_LOG(LogSCC, Error, TEXT("EncodeFrame probe failed: %s"),
            *GetLastErrorString(Ctx));
        return Result;
    }
    if (Required > static_cast<size_t>(MAX_int32))
    {
        UE_LOG(LogSCC, Error, TEXT("EncodeFrame: required %llu exceeds int32"),
            static_cast<uint64>(Required));
        return Result;
    }

    Result.SetNumUninitialized(static_cast<int32>(Required));
    size_t Written = 0;
    const scc_result Rc = scc_encode_frame(Ctx, DepthData, Stride,
                                            EncodeWidth, EncodeHeight, EncodeBitDepth,
                                            Result.GetData(), Required, &Written);
    if (Rc != SCC_OK)
    {
        UE_LOG(LogSCC, Error, TEXT("EncodeFrame failed (rc=%d): %s"),
            static_cast<int32>(Rc), *GetLastErrorString(Ctx));
        return TArray<uint8>();
    }
    if (static_cast<int32>(Written) < Result.Num())
    {
        Result.SetNum(static_cast<int32>(Written), EAllowShrinking::No);
    }
    return Result;
}

bool USCCStreamComponent::DecodeSEI(const TArray<uint8>& SEI, TArray<uint16>& OutDepth,
                                    int32& OutWidth, int32& OutHeight, int32& OutBitDepth)
{
    OutDepth.Reset();
    OutWidth = OutHeight = OutBitDepth = 0;

    if (SEI.Num() <= 0)
    {
        UE_LOG(LogSCC, Error, TEXT("DecodeSEI: empty input"));
        return false;
    }

    if (!DecoderContext)
    {
        DecoderContext = scc_init();
        if (!DecoderContext)
        {
            UE_LOG(LogSCC, Error, TEXT("DecodeSEI: scc_init failed"));
            return false;
        }
    }
    scc_ctx* Ctx = static_cast<scc_ctx*>(DecoderContext);

    int W = 0, H = 0, BD = 0;
    scc_decode_sei(Ctx, SEI.GetData(), static_cast<size_t>(SEI.Num()),
                   /*out_depth=*/nullptr, /*out_stride=*/0, &W, &H, &BD);
    if (W <= 0 || H <= 0)
    {
        UE_LOG(LogSCC, Error, TEXT("DecodeSEI probe failed: %s"),
            *GetLastErrorString(Ctx));
        return false;
    }

    // [Ultrathink #3] -- single SetNumUninitialized; the decoder writes
    // directly into the TArray's contiguous backing store via
    // reinterpret_cast<uint8*>(GetData()).
    OutDepth.SetNumUninitialized(W * H);
    const size_t Stride = static_cast<size_t>(W) * sizeof(uint16);
    const scc_result Rc = scc_decode_sei(Ctx, SEI.GetData(),
                                          static_cast<size_t>(SEI.Num()),
                                          reinterpret_cast<uint8_t*>(OutDepth.GetData()),
                                          Stride, &W, &H, &BD);
    if (Rc != SCC_OK)
    {
        UE_LOG(LogSCC, Error, TEXT("DecodeSEI failed (rc=%d): %s"),
            static_cast<int32>(Rc), *GetLastErrorString(Ctx));
        OutDepth.Reset();
        return false;
    }
    OutWidth     = W;
    OutHeight    = H;
    OutBitDepth  = BD;
    return true;
}

void USCCStreamComponent::EndEncode()
{
    if (EncoderContext)
    {
        scc_destroy(static_cast<scc_ctx*>(EncoderContext));
        EncoderContext = nullptr;
    }
    EncodeWidth = EncodeHeight = 0;
}

FString USCCStreamComponent::GetSCCSDKVersion() const
{
    return FString(FSCCModule::GetSDKVersion());
}
