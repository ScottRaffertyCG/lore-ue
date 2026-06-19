using UnrealBuildTool;

public class LoreSourceControl : ModuleRules
{
	public LoreSourceControl(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"SourceControl"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
			"Projects",
			"UnrealEd"
		});

		// The Lore provider drives the public `lore` CLI as a child process,
		// mirroring Epic's bundled Subversion/Git providers. No native Lore
		// library is linked, so there is no ThirdParty dependency here.
	}
}
