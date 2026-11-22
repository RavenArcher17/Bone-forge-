// The world (design doc, World and AI tab, step 1): an island about 4 km across, generated in the game from a seed.
// Noise lays out the land (a coast, plains, hills, a rocky spine), simulated rain erodes it (droplet erosion), water
// collects where it drains (the sea all round, lakes filled into hollows, rivers carved along the valleys), and biomes
// follow height, slope and wetness. The same seed always gives the same island. Built as walkable tiles (procedural
// meshes with collision), water surfaces the swim system knows (ABFGameMode::WaterAt asks here).
#pragma once

#include <atomic>
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BFPlants.h"
#include "BFWorld.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UHierarchicalInstancedStaticMeshComponent;

/** One plant on the island: where (from the grid's corner, cm), the ground's slope it leans with, its turn and size. */
struct FBFPlantSpot
{
	FVector3f Pos;
	FVector3f Normal;
	float Yaw;
	float Scale;
};

UENUM()
enum class EBFBiome : uint8
{
	Sea, Beach, Plains, Forest, Swamp, Rock, Lake, River
};

/** A rock shape (rock pieces step B, 2026-11-14): a rounded, lumpy block turned and sized, built in the game as a
 *  faceted mesh. Kind 0 a boulder, 1 a slab (its top flat at TopZ: a ledge to stand on), 2 a fin (a tall blade). */
struct FBFRock
{
	FVector At = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	FVector Half = FVector(100.f);
	uint8 Kind = 0;
	int32 Seed = 0;
	float TopZ = 0.f;
	FColor Tint = FColor(0, 0, 0, 0);          // (alpha 0: its own grey) set in a wall: the wall's own colour
	FVector WallN = FVector::ZeroVector;       // set in a wall: its facets shaded part way to the wall's facing, so it reads as the wall's
	bool bBed = false;                          // a bed of a gorge wall (DressRavines): local +Y out of the wall, shaped as rock beds
};

UCLASS()
class ABFWorld : public AActor
{
	GENERATED_BODY()

public:
	ABFWorld();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Starts making the island for Seed: the land and water on a worker thread, then its tiles a few a frame. */
	void Generate(int32 InSeed);
	bool IsReady() const { return bReady; }
	/** 0..1 while it is being made, and what it is doing. */
	float GetProgress() const { return Progress.load(); }
	FString GetStage() const;
	int32 GetSeed() const { return Seed; }
	FString GetSummary() const { return Summary; }

	/** The ground's height (world Z, cm) under a point, and whether that point is over the island at all. */
	bool GroundAt(const FVector2D& XY, float& Z) const;
	/** Places to look at the ground from (-bfterrain): two of each kind of land, the peak, a sea cliff, an islet. */
	void TourPoints(TArray<FVector>& At, TArray<FString>& Names) const;
	/** Logs the ground round a point (-bflandform): kind, ground and water height of each grid point, 9 x 9. */
	void LogAround(const FVector& P, int32 R = 4) const;

	/** Rock pieces (caves, 2026-11-14): a cave carved into the hill along Path (world points, its floor flat under each
	 *  stretch), Radii one a point. The island's own ground stays everywhere else; over the piece's footprint its tiles
	 *  get a hole and the piece's own mesh (the hill's surface there, the cave's mouth and its inside, rough rock) takes
	 *  their place. Built at once. Its index, or INDEX_NONE (off the island). */
	/** Kind: 0 a den, 1 a cliff alcove, 2 a deep cave (a winding passage, chambers), 3 a cavern (a mountain's hall),
	 *  4 the hollow behind a waterfall (its face rough).
	 *  Squash (per point, 1 = round): a chamber flatter than it is wide. Shafts: holes up through the roof (A to B,
	 *  radius), a skylight. Name: what the O panel calls it. */
	int32 AddCave(const TArray<FVector>& Path, const TArray<float>& Radii, uint8 Kind = 0, const TArray<float>* Squash = nullptr,
		const TArray<FVector4>* Shafts = nullptr, const FString& Name = FString());
	/** The island's named places (caves, falls, ravines): a name and where to stand to see it. */
	struct FBFPlace { FString Name; FVector At; uint8 Kind; };
	TArray<FBFPlace> Places;
	FString CaveSummary;
	int32 NumCaves() const { return Caves.Num(); }
	/** A cave's kind (0 a den at a cliff's foot, walked into; 1 an alcove high in a cliff, flown into), the ground just
	 *  outside its mouth, which way is out, and points on its floor from the mouth in to its back. */
	uint8 CaveKind(int32 I) const { return Caves.IsValidIndex(I) ? Caves[I].Kind : 0; }
	FVector CaveMouth(int32 I) const { return Caves.IsValidIndex(I) ? Caves[I].MouthOut : FVector::ZeroVector; }
	FVector CaveOut(int32 I) const { return Caves.IsValidIndex(I) ? Caves[I].Out : FVector::ZeroVector; }
	const TArray<FVector>& CaveFloor(int32 I) const { return Caves[I].Floor; }
	/** The cave a point is inside (in its air, under the hill), or INDEX_NONE. */
	int32 CaveAt(const FVector& P) const;
	/** Under a point inside a cave: its floor's height there. */
	bool CaveFloorAt(const FVector& P, float& Z) const;
	static float CaveFloorK(float Squash);
	/** River water standing up out of its surface: how many of its points are over 30 cm higher than the mean of the wet
	 *  points round them (a dome), the worst (cm); and the steepest stretch of river (where its surface drops most in
	 *  12 m) nearest Near, for pictures. */
	int32 RiverBumps(float& WorstCm) const;
	/** Water standing up out of its surroundings: river points over 1.5 m deep whose surface is over 1 m above a point next to them
	 *  (the water there or, dry, the ground). How many water points the spilling lowered when the island was made. */
	int32 PerchedWater(float& WorstCm, TArray<FVector>* Worst = nullptr) const;
	int32 SpillLowered = 0;
	float FoamClock = 0.f;
	void TickFoam(float DeltaSeconds);
	int32 RunsLowered = 0;
	/** Ravines (gorges cut down a slope: walk in at the low end, a head wall at the top) and waterfalls (a river
	 *  dropping 3 m or more at once). */
	struct FBFRavine { TArray<FVector> Path; TArray<float> HalfCm; float DeepM = 0.f; };
	struct FBFFall { FVector Top; FVector Bottom; FVector Dir; float WidthCm = 400.f; float DropM = 0.f; float PoolCm = 0.f; FVector HollowAt = FVector::ZeroVector; float HollowCm = 0.f; };          // (the hollow behind it: its middle, radius)
	TArray<FBFRavine> Ravines;
	TArray<FBFFall> Falls;
	TArray<TArray<int32>> FallRuns;     // each fall's points, its lip first (MakeLand)
	TArray<int32> FallReach;
	FString LandformText;          // river points whose level the runs lowered (a step above them)
	bool SteepRiver(const FVector& Near, float RadiusCm, FVector& At, FVector& Downhill) const;
	/** Up to Max places where a river runs down a slope (mid-channel: water across it, water up- and downstream of it),
	 *  the steepest first, 100 m apart: where, which way it flows, how far its surface drops in 12 m (cm). */
	int32 RiverSlopes(int32 Max, TArray<FVector>& At, TArray<FVector>& Downhill, TArray<float>& DropCm) const;
	/** What makes the water at a point (for the checks): the ground, the water level, and the same for the points round it. */
	FString WaterText(const FVector& At) const;
	/** The island's own caves (dens at the feet of cliffs), made with it. */
	int32 IslandDens = 0;
	int32 CavesRefused = 0;          // caves not made: the hill not over the tunnel (AddCave)
	FString LakeText;
	bool NearFallFace(const FVector& P) const;          // where a fall's hollow will be cut (no rocks set there)
	TArray<uint8> Gorge;          // ground points a ravine cut            // what kinds of lake the island has (MakeLand)
	int32 BanksLowered = 0;      // river-bank points laid back (MakeLand)
	int32 SpiresCut = 0;         // blades and spires by gorges and falls brought down (MakeLand, last)
	/** A cliff near Near (within MaxCm, no lower than MinZ): a point at its top edge and the way down it (the ground at
	 *  least 9 m lower 10 m out). For the ledges and the caves. */
	bool FindCliff(const FVector& Near, float MaxCm, float MinZ, FRandomStream& R, FVector& Top, FVector& Down) const;
	/** For the checks: a piece's mesh size (vertices, triangles), the ground cells cut away under it, the seconds it took. */
	FString CaveText(int32 I) const;
	/** Rocks: built together as one mesh (with collision), the cells they stand on marked for the animals' paths. A
	 *  group number (to take them away again), or INDEX_NONE. */
	int32 AddRocks(const TArray<FBFRock>& Rocks);
	void RemoveRocks(int32 Group);
	/** The island's own rocks (placed when it's made): boulders fallen at the feet of its cliffs, fins on its steep high
	 *  ground. How many of each, the triangles, the seconds. */
	FString RockText() const { return RockSummary; }
	int32 NumRocks(uint8 Kind) const;
	/** The island's rock of a kind nearest Near (for the checks). */
	bool NearestRock(const FVector& Near, uint8 Kind, FBFRock& Out) const;
	/** Whether a rock stands on the ground at a point (the animals walk round). */
	bool RockAt(const FVector& At) const;
	/** Whether At is in water (under its surface, SurfaceZ) on the island. The sea's surface rides its waves. */
	bool WaterAt(const FVector& At, float& SurfaceZ) const;
	/** Fresh water's current at a point (cm/s): rivers run downhill along their channels, lakes drift to their outlets. */
	FVector CurrentAt(const FVector& At) const;
	/** The sea's surface (m above sea level) at a point at game time T: the waves the water material draws. */
	float SeaWaveM(const FVector2D& XY, float T) const;
	/** The wind blows toward this heading (degrees from +X): the sea's waves run with it and the plants lean with it. */
	static constexpr float WindDeg = 35.f;
	/** The island's clock (step 5): a day of 24 hours in DaySeconds of game time (24 minutes), from 8 in the morning. */
	float GetHours() const { return Hours; }
	int32 GetDay() const { return Day; }
	/** 1 in full day .. 0 at night (the sun's height). */
	float Daylight() const;
	FString ClockText() const;
	float DaySeconds = 1440.f;
	void SetHours(float NewHours) { Hours = FMath::Fmod(NewHours, 24.f); }
	/** A plant eater's mouthful from the nearest plant with leaves left that it can reach: its snout at Snout, reaching
	 *  ReachCm round it and up to TopZ (world Z). The kg it takes (0: nothing in reach); what it was and what's left on it. */
	float EatPlants(const FVector& Snout, float ReachCm, float TopZ, float WantKg, FString& What, float& LeftKg);
	/** How many plants there are, and of what (for the log). */
	int32 NumPlants() const;
	FString PlantSummary() const;
	/** For the animals (World and AI tab, step 3). The plant with leaves left nearest Near within RadiusCm whose leaves
	 *  start no higher than TopZ: its middle (world), how far out its leaves reach, and the kg left on it. */
	bool FindPlant(const FVector& Near, float RadiusCm, float TopZ, FVector& At, float& LeafReachCm, float& FoodKg, float MinZ = -1e9f, float* OutLowZ = nullptr, float* OutHighZ = nullptr) const;
	/** The nearest dry, gentle ground at fresh water's edge within RadiusCm of Near: where to stand and the water (world). */
	bool NearestShore(const FVector& Near, float RadiusCm, FVector& Stand, FVector& Water) const;
	/** A way over the island for this body (A* on 8 m steps): its pace on each slope (none steeper than it climbs), water
	 *  it can wade (WadeCm deep) or swim, forest the thicker the wider it is. Out: world points from From to To. */
	bool FindPath(const FVector& From, const FVector& To, const struct FBFSpecies& Body, float WadeCm, TArray<FVector>& Out, int32 MaxNodes = 60000) const;
	/** How much trees' crowns cover a point (0 open .. 1 and more, deep forest): cover for a hunter. */
	float CoverAt(const FVector2D& XY) const;
	/** Whether there's fresh water (a lake or a river, not the sea) within RadiusCm of a point, and where. */
	bool FreshWaterNear(const FVector& At, float RadiusCm, FVector& Water) const;
	/** Whether a body could stand at a point: on the island, dry (or shallow enough), not too steep. */
	bool Walkable(const FVector& At, const struct FBFSpecies& Body, float WadeCm) const;
	/** The river mouth nearest Near: where a river meets the sea, and which way the sea is (for the checks). */
	bool RiverMouth(const FVector2D& Near, FVector& At, FVector& Seaward) const;
	/** The nearest plant of a kind (from Near) and its place (for the checks). */
	bool NearestPlant(const FVector2D& Near, EBFPlant Kind, FVector& At) const;
	/** Takes away the island's trees (and tree ferns, palms) within RadiusCm of At: room for a giant. How many. */
	int32 ClearTreesNear(const FVector2D& At, float RadiusCm, bool bSmallToo = false);
	EBFBiome BiomeAt(const FVector2D& XY) const;
	/** Saving (life cycle step 3): the clock and every plant that's been eaten (its kg left), into and out of a JSON object. */
	void WriteState(TSharedRef<class FJsonObject> Out) const;
	void ReadState(const TSharedPtr<class FJsonObject>& In);
	/** Food (the population check): kg of leaves on the island whole, kg left now, and the most that can grow back a
	 *  second (every plant eaten down and regrowing) of the plants whose leaves start no higher than MaxLowCm up. */
	float FoodWholeKg() const;
	float FoodLeftKg() const;
	float RegrowKgPerS(float MaxLowCm) const;
	int32 NumRegrowing() const { return Regrowing.Num(); }
	/** How fast eaten plants grow back, against each kind's own rate (FBFPlantKind::RegrowPerMin): a fortieth, so the
	 *  island's leaves feed about as many plant eaters as the game can run (the population check, 2026-11-13). */
	float RegrowRate = 0.025f;
	/** Where to put a creature that has just arrived: a flat, dry spot on open ground near the middle (feet height). */
	FVector StartPoint() const;
	/** A point at a river, a lake shore or the sea shore nearest Near (for the checks). */
	bool NearestWater(const FVector2D& Near, EBFBiome Kind, FVector& At) const;
	/** The island's middle and its size (cm). */
	FVector GetCentre() const { return Origin + FVector(0.5 * SizeCm(), 0.5 * SizeCm(), 0.0); }
	double SizeCm() const { return double(N - 1) * CellCm; }

	/** The island being played on (the lab's ground has none), for the static water and ground questions. */
	static ABFWorld* Active;

	static constexpr int32 N = 1025;           // grid points a side
	static constexpr float CellCm = 400.f;     // 4 m between them: 4,096 m across
	FVector GridOrigin() const { return Origin; }          // the grid's (0, 0) corner, world cm (checks)
	static constexpr int32 ChunkCells = 64;    // a tile: 64 x 64 cells, 256 m (16 x 16 tiles)

private:
	/** The land and water, on a worker thread (no engine objects touched). */
	void MakeLand();
	void BuildChunk(int32 CX, int32 CY);
	/** The open sea's surface: a grid fine near its middle and coarser outward, kept under the camera. */
	void BuildSea();
	/** Where every plant goes (on the worker thread, with the land): by biome, slope, height and nearness to fresh water. */
	void PlacePlants(const TArray<int32>& ToWater, const TArray<uint8>& Kind);
	/** One kind and shape of plant put in the world (its model, then all of them as instances). */
	void GrowPlants(int32 KV);
	void TickPlants(float DeltaSeconds);
	FORCEINLINE int32 Idx(int32 X, int32 Y) const { return Y * N + X; }
	float HeightM(float GX, float GY) const;   // bilinear, grid units
	float Bilinear(const TArray<float>& A, float GX, float GY) const;

	int32 Seed = 0;
	FVector Origin = FVector::ZeroVector;      // the grid's (0, 0) corner, world cm; Z = sea level
	TArray<float> H;                           // ground height, metres above the sea
	TArray<float> W;                           // water surface, metres (below the ground: none)
	TArray<uint8> Biome;
	TArray<FColor> Colour;                     // the ground's colour at each point (by biome, blended)
	TArray<FVector2f> Flow;                    // fresh water's current, m/s
	TArray<float> ShoreM;                      // a point of sea: how far it is from the land, m (the surf runs in there)
	TArray<uint8> SeaSteps;                    // a point of land or fresh water: how many points from the sea (to 255)
	TArray<float> Crown;                       // how much of each point trees' crowns cover (0..1+; paths through forest)
	TArray<FColor> InfoPixels;                 // the water's map for its material (made with the land, uploaded once)
	static constexpr int32 NumKV = BFPlants::Kinds * BFPlants::Variants;
	TArray<FBFPlantSpot> Spots[NumKV];         // by kind x 3 + shape
	TArray<float> Food[NumKV];                 // kg of leaves left on each
	TArray<TArray<FIntPoint>> PlantGrid;       // 16 m squares: which plants (kind-shape, index) stand in each
	int32 GridN = 0;
	TArray<FIntPoint> Regrowing;               // plants that have been eaten and are growing back
	float RegrowClock = 0.f;
	int32 NextPlants = 0;                      // kinds-and-shapes put in the world so far
	bool bSeaBuilt = false;
	float Hours = 8.f;
	int32 Day = 1;
	TFuture<void> Work;
	FThreadSafeBool bLandDone = false;
	bool bReady = false;
	int32 NextChunk = 0;
	// Written by the worker thread while the menu reads them: atomics, the stage an index into fixed words.
	std::atomic<float> Progress { 0.f };
	std::atomic<int32> StageNo { 0 };
	FString Summary;                           // written before bLandDone, read after
	double StartedAt = 0.0;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> Tiles;
	// The rock pieces and the ground cells cut away under them; which of Tiles is each tile's ground (by tile number).
	struct FBFCave
	{
		TArray<FVector> Path;
		TArray<float> Radii;
		FBox Box;
		FIntRect Hole;              // ground cells (min inclusive, max exclusive) left out of the tiles
		uint8 Kind = 0;
		FVector MouthOut = FVector::ZeroVector, Out = FVector::ZeroVector;
		TArray<FVector> Floor;
		TArray<float> Squash;       // per path point (1 round; 2 a chamber half as tall as wide)
		TArray<FVector4> Shafts;    // A (xyz) up to B (xy + w radius; B.z the ground over it + 4 m): skylights
		TArray<FVector> ShaftTop;
		float Res = 50.f;           // its mesh's grid (cm): coarser for the big ones
		FString Name;
		int32 Verts = 0, Tris = 0;
		double Seconds = 0.0;
	};
	TArray<FBFCave> Caves;
	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> RockMeshes;     // by group
	TArray<TArray<FBFRock>> RockSets;
	TArray<uint8> RockCells;                                     // how many rocks stand on each ground point
	int32 IslandRockGroups = 0;
	FString RockSummary;
	float RockDensity(const FBFRock& R, const FVector& P) const;
	void MarkRock(const FBFRock& R, int32 Add);
	void PlaceRocks();
	void PlaceCaves();
	void DressRavines();
	void DressRivers();
	void BuildFalls();
	void PlaceBigCaves(int32 WantDeep, bool bWantCavern, FRandomStream& R);
	TArray<int32> GroundTile;
	TArray<int32> WaterTileOf;          // each chunk's fresh-water and white-water components (Tiles index pairs), to cut cave holes out of
	TArray<int32> RapidsTileOf;
	bool CellHoled(int32 GX, int32 GY) const;
	void GroundSection(int32 CX, int32 CY, TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FColor>& Colours) const;
	float CaveDensity(const FBFCave& C, const FVector& P) const;
	void BuildCaveMesh(FBFCave& C);
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GroundMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterMat;   // the sea (with waves)
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FreshMat;   // lakes and rivers
	UPROPERTY()
	TObjectPtr<class UTexture2D> Info;
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> SeaSurface;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlantMat;
	UPROPERTY()
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> PlantComps;   // by kind x 3 + shape
};
