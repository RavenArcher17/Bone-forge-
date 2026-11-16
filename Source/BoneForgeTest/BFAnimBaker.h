// Bakes the Bone Forge procedural pose (FBFPoser) into real animation assets next to the Skeletal Mesh:
// in-place loops idle, walk and run (the rig's fast gait), one-shot bite and roar, and for species that fly
// glide and flap; plus a speed blend space (idle / walk / run) and, for flyers, a glide-to-flap blend space.
// The editor script bone_forge_anims.py calls it and saves the assets. Editor only.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BFAnimBaker.generated.h"

UCLASS()
class UBFAnimBaker : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds (or rebuilds) the clips in /Game/BoneForge/<Species>/<key>_skeletal/Anims and leaves them unsaved.
	 * Returns one report line per asset (length, frames, joints past limits_deg, loop seam), "ERROR: ..." on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bone Forge")
	static FString BakeClips(const FString& RigFolder, const FString& Key);
};
