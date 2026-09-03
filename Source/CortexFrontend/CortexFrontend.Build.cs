using UnrealBuildTool;

public class CortexFrontend : ModuleRules
{
    public CortexFrontend(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "Engine",
            "DeveloperSettings",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "UnrealEd",
            "WorkspaceMenuStructure",
            "ApplicationCore",
            "Slate",
            "SlateCore",
            "InputCore",
            "Json",
            "JsonUtilities",
            "ToolMenus",
            "Settings",
            "Projects",
            "CortexCore",
            "CortexGen",
            "ImageWrapper",
            "EditorScriptingUtilities",
            "DesktopPlatform",
            "GraphEditor",
            "BlueprintGraph",
            "AssetRegistry",
            "MessageLog",
            "Kismet",
            "ContentBrowser",
        });

        // LiveCoding lives in Developer/Windows and only exists where Live Coding is supported.
        // Depending on it unconditionally pulled a Windows-only module into Linux/Mac editor
        // builds, which fail on an undefined _WIN32 and a missing Microsoft/PreWindowsApiPrivate.h.
        // The call sites are already #if WITH_LIVE_CODING guarded.
        if (Target.bWithLiveCoding)
        {
            PrivateDependencyModuleNames.Add("LiveCoding");
        }
    }
}
