using UnrealBuildTool;

public class BoneForgeTestEditorTarget : TargetRules
{
	public BoneForgeTestEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("BoneForgeTest");
	}
}