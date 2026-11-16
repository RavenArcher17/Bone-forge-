using UnrealBuildTool;

public class BoneForgeTest : ModuleRules
{
	public BoneForgeTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Json", "PhysicsCore", "ProceduralMeshComponent" });  // the Pteranodon's live wing membrane
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });  // the dinosaur picker and the HUD text
		// Building a new creature's Skeletal Mesh while the game runs (BFRuntimeImport): Unreal's glTF import
		PrivateDependencyModuleNames.AddRange(new string[] { "InterchangeCore", "InterchangeEngine", "InterchangePipelines" });
		PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });  // frame timings (the population check)  // the island's plants, built in the game (BFPlants)
	}
}