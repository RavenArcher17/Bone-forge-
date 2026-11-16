// The island's plants (design doc, World and AI tab, step 2): Late Cretaceous flora, the world the six lived in. No grass
// (it was rare until long after); the open ground is fern prairie with cycads, bennettites and early flowering shrubs; the
// forests are conifers (monkey puzzles, redwoods) with ginkgos and tree ferns under them; swamps have bald cypress and
// horsetail beds; the beaches early fan palms. Each plant is built here in code from its shape (trunks as tubes, fronds
// as rachises with paired leaflets, conifer tiers as jagged skirts, palm fans as pleated ribs), three shapes of each
// and three levels of detail, once a game; ABFWorld scatters them by biome and keeps how much of each has been eaten.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

enum class EBFPlant : uint8
{
	Araucaria, Redwood, Cypress, Ginkgo, TreeFern, Cycad, Bennettite, Palm, Shrub, Fern, Horsetail, Count
};

struct FBFPlantKind
{
	const TCHAR* Name;           // what it is (the status line when it's eaten)
	float FoodKg;                // leaves a plant eater can crop from a whole one (full size)
	float FoodLowCm, FoodHighCm; // the height band its leaves are in (full size)
	float FoodRadiusCm;          // how far out from its middle they reach
	float RegrowPerMin;          // the share of a whole one that grows back a minute
	float CullCm;                // drawn out to this far
	float SinkCm;                // set this far into the ground (roots, uneven ground)
	bool bShadow;
};

namespace BFPlants
{
	constexpr int32 Variants = 3;
	constexpr int32 Kinds = int32(EBFPlant::Count);
	const FBFPlantKind& Kind(EBFPlant P);
	/** One plant's model (three levels of detail; Variant 0..2 differ in shape and size), built the first time it's
	 *  asked for and kept for the game. Trees, palms and cycads get a trunk that blocks (a capsule). */
	UStaticMesh* Mesh(EBFPlant P, int32 Variant, UMaterialInterface* Mat);
	/** How many to a hectare: by biome (EBFBiome), and how many more within a few metres of fresh water. */
	float Density(EBFPlant P, uint8 Biome);
	float NearFreshWater(EBFPlant P);
	/** The steepest ground it grows on (degrees) and the highest (m above the sea). */
	float MaxSlope(EBFPlant P);
	float MaxHeightM(EBFPlant P);
	/** Its triangles at its finest (for the log). */
	int32 Triangles(EBFPlant P, int32 Variant);
}
