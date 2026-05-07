// sdk/unreal/SCC/Source/SCC/Private/SCC.cpp
//
// Module entry point.

#include "SCC.h"

#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogSCC);

#define LOCTEXT_NAMESPACE "FSCCModule"

void FSCCModule::StartupModule()
{
    UE_LOG(LogSCC, Log, TEXT("SCC plugin starting up; SDK version %s"),
        GetSDKVersion());

#if PLATFORM_WINDOWS
    // [Ultrathink #1] -- explicit DLL load so the search path is the
    // plugin's installed Source/ThirdParty/SCC/Win64/ directory, not
    // the engine's default lookup chain.
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SCC"));
    if (Plugin.IsValid())
    {
        FString DllPath = FPaths::Combine(
            Plugin->GetBaseDir(),
            TEXT("Source"), TEXT("ThirdParty"), TEXT("SCC"),
            TEXT("Win64"), TEXT("libscc.dll"));
        if (FPaths::FileExists(DllPath))
        {
            LibSCCHandle = FPlatformProcess::GetDllHandle(*DllPath);
            if (LibSCCHandle == nullptr)
            {
                UE_LOG(LogSCC, Error, TEXT("Failed to load libscc.dll from %s"),
                    *DllPath);
            }
        }
        else
        {
            UE_LOG(LogSCC, Warning,
                TEXT("libscc.dll not found at %s -- run prepare-distribution.sh "
                     "or build-plugins.sh first."), *DllPath);
        }
    }
#endif
}

void FSCCModule::ShutdownModule()
{
#if PLATFORM_WINDOWS
    if (LibSCCHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(LibSCCHandle);
        LibSCCHandle = nullptr;
    }
#endif
    UE_LOG(LogSCC, Log, TEXT("SCC plugin shutting down."));
}

const TCHAR* FSCCModule::GetSDKVersion()
{
#ifdef SCC_SDK_VERSION
    return TEXT(SCC_SDK_VERSION);
#else
    return TEXT("unknown");
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSCCModule, SCC)
