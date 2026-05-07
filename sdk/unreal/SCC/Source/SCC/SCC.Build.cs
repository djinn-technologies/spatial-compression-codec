// sdk/unreal/SCC/Source/SCC/SCC.Build.cs
//
// Build rules for the SCC runtime module. Wraps libscc per-platform.
//
// NOTE: This file lives at the canonical UE location
// (Source/<Module>/<Module>.Build.cs), not under Private/. UBT searches
// the entire module directory and accepts either, but Epic Fab
// submission expects the canonical location.
//
// Evidence: [REQ-029, ADR-006, ADR-010].

using UnrealBuildTool;
using System.IO;

public class SCC : ModuleRules
{
    // [Ultrathink #5] -- pinned SCC SDK version. Bumped in lockstep with
    // the parent repo's libscc release.
    public const string SCCSDKVersion = "1.0.0";

    public SCC(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUsePrecompiled = false;

        PublicDependencyModuleNames.AddRange(new[] {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new[] {
            "Projects",            // for IPluginManager (DLL path resolution)
        });

        // Functional / automation tests live in this module, gated by
        // WITH_AUTOMATION_TESTS so they cook out of shipping builds.
        if (Target.bBuildDeveloperTools || Target.Configuration != UnrealTargetConfiguration.Shipping)
        {
            PrivateDependencyModuleNames.Add("FunctionalTesting");
        }

        // Pin the SDK version into a C macro so the runtime can log /
        // assert against it.  [Ultrathink #5]
        PublicDefinitions.Add("SCC_SDK_VERSION=\"" + SCCSDKVersion + "\"");

        // ---------------------------------------------------------------
        // libscc include path. Two scenarios:
        //   - In-tree development (this plugin lives inside the
        //     spatial-compression-codec repo): include
        //     <repo>/cabi/include directly so libscc.h has a single
        //     source of truth.
        //   - Standalone marketplace distribution: prepare-distribution.sh
        //     copies cabi/include/libscc.h into
        //     Source/ThirdParty/SCC/Include/ before zipping; the local
        //     path resolves first.
        // ---------------------------------------------------------------
        string ThirdPartyDir = Path.Combine(ModuleDirectory, "..", "ThirdParty", "SCC");
        string LocalIncludeDir = Path.Combine(ThirdPartyDir, "Include");
        string RepoIncludeDir = Path.Combine(ModuleDirectory, "..", "..", "..", "..", "..", "cabi", "include");

        if (File.Exists(Path.Combine(LocalIncludeDir, "libscc.h")))
        {
            PublicIncludePaths.Add(LocalIncludeDir);
        }
        else
        {
            PublicIncludePaths.Add(RepoIncludeDir);
        }

        // ---------------------------------------------------------------
        // Per-platform libscc linkage. [Ultrathink #1]
        // ---------------------------------------------------------------
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string Win64Dir = Path.Combine(ThirdPartyDir, "Win64");
            // Import library + delay-load DLL. The DLL is staged into
            // the cooked Plugins/SCC/Source/ThirdParty/SCC/Win64/ via
            // RuntimeDependencies.
            PublicAdditionalLibraries.Add(Path.Combine(Win64Dir, "libscc.lib"));
            PublicDelayLoadDLLs.Add("libscc.dll");
            RuntimeDependencies.Add(Path.Combine(Win64Dir, "libscc.dll"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            string LinuxDir = Path.Combine(ThirdPartyDir, "Linux");
            PublicAdditionalLibraries.Add(Path.Combine(LinuxDir, "libscc.so"));
            RuntimeDependencies.Add(Path.Combine(LinuxDir, "libscc.so"));
            // ld.so will pick up the .so via RPATH = $ORIGIN.
            PublicRuntimeLibraryPaths.Add(LinuxDir);
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string MacDir = Path.Combine(ThirdPartyDir, "Mac");
            PublicAdditionalLibraries.Add(Path.Combine(MacDir, "libscc.dylib"));
            RuntimeDependencies.Add(Path.Combine(MacDir, "libscc.dylib"));
            PublicRuntimeLibraryPaths.Add(MacDir);
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // Android: the .so for arm64-v8a is the only architecture we
            // ship (Quest 2/3, Pico 4/5).
            string AndroidArm64Dir = Path.Combine(ThirdPartyDir, "Android", "arm64-v8a");
            PublicAdditionalLibraries.Add(Path.Combine(AndroidArm64Dir, "libscc.so"));
            RuntimeDependencies.Add(Path.Combine(AndroidArm64Dir, "libscc.so"));

            string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory,
                Target.RelativeEnginePath);
            AdditionalPropertiesForReceipt.Add("AndroidPlugin",
                Path.Combine(PluginPath, "..", "..", "SCC_APL.xml"));
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            // iOS forbids dynamic loading; libscc.a is statically linked.
            string IOSDir = Path.Combine(ThirdPartyDir, "IOS");
            PublicAdditionalLibraries.Add(Path.Combine(IOSDir, "libscc.a"));
        }
    }
}
