// sdk/unreal/SCC/Source/SCC/Public/SCC.h
//
// Module class for the SCC plugin runtime.

#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSCC, Log, All);

class FSCCModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Returns the SDK version string baked at build time. */
    static const TCHAR* GetSDKVersion();

private:
    /**
     * On Windows we explicitly load libscc.dll via FPlatformProcess so
     * the search path is relative to the plugin's installed
     * directory, not LoadLibrary's default lookup. Other platforms use
     * the system loader's search rules.
     */
    void* LibSCCHandle = nullptr;
};
