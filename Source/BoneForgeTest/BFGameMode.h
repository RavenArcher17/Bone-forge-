// The testing ground: builds the arena at start (a flat field and four ramps at 10, 20, 30 and 40 degrees),
// loads the six species, and spawns the chosen one for the player.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BFLook.h"
#include "BFSpecies.h"
#include "BFGameMode.generated.h"

class ABFDino;
class USkeletalMesh;
class UExponentialHeightFogComponent;

UENUM()
enum class EBFSpawnArea : uint8
{
	Flat,
	Slopes,
	Cliff,      // the top of the 20 m cliff, facing its edge (glide tests)
	Pool,       // the shore of the pool, facing its entry slope (swim tests)
	World       // the generated island (ABFWorld), on open ground near its middle
};

UCLASS(Config = Game)
class ABFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABFGameMode();

	virtual void StartPlay() override;

	/** Spawns species Index at the given area for Controller, replacing its current dinosaur. */
	ABFDino* SpawnDino(APlayerController* Controller, int32 Index, EBFSpawnArea Area, int32 Ramp = 0);
	/** A second animal nobody controls (lab plan item 6): species Index standing at At (its feet on the ground there),
	 *  facing Yaw. Its physics run without a controller; the player's controller steers it when it follows or copies. */
	ABFDino* SpawnCompanion(int32 Index, const FVector& At, float Yaw);

	const TArray<FBFSpecies>& GetSpecies() const { return Species; }
	const TArray<FString>& GetLoadErrors() const { return LoadErrors; }
	/** Loads (or loads again) a creature while the game runs around Mesh, just built in the game (Walk it from the
	 *  splicing editor). Returns its index in the species, or INDEX_NONE with Error. */
	int32 AddSpecies(const FString& Key, USkeletalMesh* Mesh, FString& Error);

	static constexpr int32 NumRamps = 4;
	static float RampAngle(int32 Ramp) { return 10.f * (Ramp + 1); }
	/** X (cm) halfway up a ramp's slope. */
	static float RampMiddleX(int32 Ramp);
	/** X (cm) where every ramp leaves the flat. */
	static float RampFootX();
	/** The cliff: its top (Z) and front edge (X), facing +X over the flat field. */
	static constexpr float CliffTopZ = 2000.f;
	static constexpr float CliffEdgeX = -10000.f;
	/** The pool (swim tests): 12 m deep, its footprint on the flat field south of the spawn, a slope in at its west end. */
	static constexpr float PoolMinX = -9000.f, PoolMaxX = 3000.f, PoolMinY = -17000.f, PoolMaxY = -13000.f;
	static constexpr float PoolDepth = 1200.f, PoolEntryRun = 4500.f, WaterZ = -30.f;
	/** Whether At is over the pool and below its surface (SurfaceZ is the surface). */
	static bool WaterAt(const FVector& At, float& SurfaceZ);
	/** The water's current at a point (cm/s): the island's rivers and lakes; still anywhere else. */
	static FVector CurrentAt(const FVector& At);
	/** The view from under the pool's surface: the fog turns thick and blue-green (the water's murk), and back. */
	void SetUnderwaterView(bool bUnder);
	/** How many of the species are the six starter ones (they load first; the splices follow). */
	int32 NumBaseSpecies() const;

	/** Your creatures: the ones made in the splicing editor (keys ed_*). */
	static bool IsYours(const FString& Key) { return Key.StartsWith(TEXT("ed_")); }
	/** The bone_forge folder (the rig folder's parent). */
	FString BoneForgeDir() const;
	/** The name you gave a creature, or empty for the one it was built with. Names live in bone_forge/my_creatures.json, so
	 *  growing a creature again can't lose them. */
	FString GivenNameOf(const FString& Key) const;
	/** Names a creature (empty: back to the name it was built with). Another creature already called that gets " 2", " 3"... */
	FString RenameCreature(const FString& Key, const FString& Name);
	/** Moves a creature's files into bone_forge/removed/<key>/ (nothing is deleted) and drops it from the species. */
	bool RemoveCreature(const FString& Key, FString& Error);
	/** Moves a removed creature's files back; it is then built into the game like Walk it (it isn't a species until then). */
	bool RestoreFiles(const FString& Key, FString& Error);
	struct FRemoved { FString Key, Name; };
	TArray<FRemoved> GetRemoved() const;
	/** The world (World and AI tab): makes the island for Seed (replacing any other), made over a few seconds. */
	class ABFWorld* MakeIsland(int32 Seed);
	class ABFWorld* GetIsland() const { return Island; }
	/** Islands kept by their seeds (bone_forge/my_worlds.json): to come back to. */
	TArray<int32> GetKeptIslands() const;
	void KeepIsland(int32 Seed);
	/** A creature's look (colours, covering), any creature's, the six's too; kept beside the names. Empty: its own. */
	FBFLook LookOf(const FString& Key) const;
	void SetLook(const FString& Key, const FBFLook& Look);

	/** Folder holding the *_rig.json files. */
	UPROPERTY(Config)
	FString RigFolder;

private:
	void BuildArena();
	AActor* SpawnBox(const FVector& Center, const FVector& SizeCm, const FRotator& Rotation, const FLinearColor& Tint);
	void SpawnLabel(const FVector& At, const FString& Text, float Size);
	static UMaterialInterface* TintedMaterial(const FLinearColor& Tint, UObject* Outer);

	TArray<FBFSpecies> Species;
	TArray<FString> LoadErrors;
	TMap<FString, FString> GivenNames;   // my_creatures.json
	TMap<FString, FBFLook> Looks;        // my_creatures.json too
	UPROPERTY()
	TObjectPtr<class ABFWorld> Island;
	TMap<FString, FString> BuiltNames;   // each creature's name as built (its rig's "creature")
	void LoadNames();
	void SaveNames() const;
public:
	/** The game pace and the rest of the settings kept in my_creatures.json. */
	void SaveSettings() const { SaveNames(); }
private:
	/** The name a creature is shown by: its given name, else the one it was built with. */
	void ApplyName(FBFSpecies& S);

	UPROPERTY()
	TArray<TObjectPtr<UObject>> KeepAssets;   // meshes and baked clips

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY()
	TObjectPtr<class ADirectionalLight> SunLight;
	UPROPERTY()
	TObjectPtr<class ADirectionalLight> MoonLight;
	UPROPERTY()
	TObjectPtr<class APostProcessVolume> NightPost;
public:
	/** The sky at an hour of the day (World and AI tab, step 5): the sun across the sky from east to west, below the
	 *  horizon at night, and a dim blue moonlight then. */
	void SetSkyAt(float Hours);
	float GetMist() const { return MistNow; }
	float MistNow = 0.f;          // 0 clear .. 1 thick morning mist
private:
	bool bUnderwaterView = false;
	float AirFogDensity = 0.f, AirFogFalloff = 0.f;
	FLinearColor AirFogColour, AirSunFogColour, AirSkyFogScale;
};
