// Builds a creature's Skeletal Mesh inside the running game: the splicing editor's new creatures are playable at once,
// with nothing imported in the editor and no restart. Unreal's own glTF import (Interchange) turns the skinned .glb
// that bone_forge/unreal/build_glb.py writes from the rig and the grown body into a Skeletal Mesh and its Skeleton,
// kept in memory under the same /Game path the editor's import would have saved them to, so FBFSpecies finds them.
#pragma once

#include "CoreMinimal.h"
#include "InterchangeManager.h"

class USkeletalMesh;

namespace BFRuntimeImport
{
	using FImportHandle = TSharedPtr<UE::Interchange::FImportResult, ESPMode::ThreadSafe>;

	/** Imports GlbPath for Key and waits for it. Returns the Skeletal Mesh (in memory, not saved) or null with Error.
	 *  Not from inside an actor's tick: the wait runs the game thread's tasks, which a tick is one of (it asserted). */
	USkeletalMesh* ImportGlb(const FString& Key, const FString& GlbPath, FString& Error);

	/** The same, started and left to run: poll it with Finished from a tick (Walk it). Invalid with Error on failure. */
	FImportHandle StartImport(const FString& Key, const FString& GlbPath, FString& Error);

	/** Once the import is done: true, with its Skeletal Mesh in Mesh (null and Error set if it made none). */
	bool Finished(const FImportHandle& Handle, USkeletalMesh*& Mesh, FString& Error);

	/** Writes <bone_forge>/unreal/out/<Key>_skeletal.glb with python (build_glb.py); false with Error on failure. */
	bool BuildGlb(const FString& RigFolder, const FString& Key, FString& GlbPath, FString& Error);

	/** Each material slot of a mesh built here takes what Scheme's own mesh (a species, or a splice's base) wears on
	 *  that part, as the editor's bone_forge_materials.py puts them: skin, eyes, toes, mouth, teeth, horn. */
	void ApplySkin(USkeletalMesh* Mesh, const FString& Scheme);
}
