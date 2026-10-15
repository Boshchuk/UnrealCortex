using UnrealBuildTool;

public class CortexBlueprint : ModuleRules
{
	public CortexBlueprint(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CortexCore",
			"CortexGraph",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetRegistry",
			"Json",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"GameplayTags",
			"ToolMenus",    // for toolbar extension
			"Slate",
			"SlateCore",
		});
		// FInstancedStruct is still supplied by the StructUtils plugin before 5.5.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
