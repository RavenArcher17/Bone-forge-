// A physics asset built from a Bone Forge rig: one body per bone in the rig JSON's collision_capsules
// (capsules, with the JSON's masses) and one constraint per body, limited to the joint's limits_deg.
// The editor script bone_forge_physics.py saves it next to the Skeletal Mesh; the testing ground uses
// it for the ragdoll (K), building it on the fly if it was never saved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BFPhysics.generated.h"

struct FBFSpecies;
class UPhysicsAsset;

UCLASS()
class UBFPhysics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds (or rebuilds) /Game/BoneForge/<Species>/<key>_skeletal/<key>_physics for the imported mesh and
	 * sets it as the mesh's physics asset. Both are left unsaved; returns a one-line report ("ERROR: ..." on failure).
	 */
	UFUNCTION(BlueprintCallable, Category = "Bone Forge")
	static FString CreatePhysicsAsset(const FString& RigFolder, const FString& Key);

	/** Fills Target (or a new transient asset when null) from the species' capsules and limits. */
	static UPhysicsAsset* Build(const FBFSpecies& Species, UPhysicsAsset* Target, FString& Report);
};
