using UnrealBuildTool;

public class BoneForgeTestTarget : TargetRules
{
	public BoneForgeTestTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("BoneForgeTest");
	}
}