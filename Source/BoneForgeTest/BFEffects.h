// Bone Forge testing ground: the marks animals leave and what they kick up (Raven's realism list, 15, 30, 31, 35):
// footprints pressed into soft ground (sand, mud, a river's bank, soft earth; none on rock), fading over minutes;
// puffs of dust from running feet on dry ground; splashes and rings where a foot goes into water. Drawn as instanced
// engine shapes with M_BoneForgeMark (opaque, colour per instance) and M_BoneForgePuff (soft, see-through).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BFEffects.generated.h"

class UInstancedStaticMeshComponent;
class ABFWorld;

UCLASS()
class BONEFORGETEST_API ABFEffects : public AActor
{
	GENERATED_BODY()

public:
	ABFEffects();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** The world's (spawned the first time it's asked for). */
	static ABFEffects* Get(UWorld* World);

	/** A foot put down: where (on the ground), the way it points, its size (cm across), how fast the animal is going
	 *  (cm/s), its weight (kg), its toes (0 a round four-footed print; 2 a raptor's, the sickle claw held up; 3 a
	 *  theropod's). Leaves a print, kicks up dust or splashes as the ground there takes it. */
	void Footfall(const FVector& At, float Yaw, float FootCm, float SpeedCm, float MassKg, int32 Toes = 0);

	/** Foam where water tumbles (a river's steps): a few puffs that drift down and fade, at At. */
	void Foam(const FVector& At, float SizeCm);

	int32 NumPrints() const { return Prints.Num(); }
	int32 NumPuffs() const { return Puffs.Num(); }
	int32 PrintsMade = 0, DustMade = 0, SplashesMade = 0, PrintsInAir = 0, PrintsNoGround = 0;

private:
	struct FPrint { FVector At; float Yaw; float Cm; float Born; FLinearColor Col; FVector Up; int32 Toes; };
	// (WaterZ: the surface of the water it's over, found on its first tick; the material hides what's under it)
	struct FPuff { FVector At; FVector Vel; float Cm; float Grow; float Born; float Life; FLinearColor Col; float Alpha; bool bRing; float WaterZ = 0.f; bool bWaterKnown = false; };
	TArray<FPrint> Prints;
	TArray<FPuff> Puffs;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PrintISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PuffISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RingISM;
	TWeakObjectPtr<ABFWorld> Island;
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> PushMPC;
	float PrintRedraw = 0.f;
	bool bPrintsDirty = false;
	ABFWorld* FindIsland();
	void DrawPrints(float Now);
};
