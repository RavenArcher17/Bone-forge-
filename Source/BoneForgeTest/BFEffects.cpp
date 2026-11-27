#include "BFEffects.h"

#include "BFWorld.h"
#include "BFDino.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// The ground's colours (as BFWorld paints them: sRGB bytes), by biome.
	const FColor GroundCol[] =
	{
		FColor(52, 64, 66), FColor(204, 189, 143), FColor(72, 98, 44), FColor(52, 64, 36),
		FColor(64, 74, 40), FColor(124, 118, 108), FColor(84, 74, 54), FColor(84, 74, 54),
	};
	constexpr float PrintLife = 300.f;          // seconds a print lasts (fading into the ground the last half)
	constexpr int32 MaxPrints = 2000;           // (each drawn as 5 pieces)
	constexpr int32 MaxPuffs = 500;
	constexpr float NearCm = 15000.f;           // only near the camera (nobody sees a print 150 m off)
}

ABFEffects::ABFEffects()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	auto Make = [&](const TCHAR* Name, const TCHAR* Mesh, const TCHAR* Mat)
	{
		UInstancedStaticMeshComponent* I = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		I->SetupAttachment(GetRootComponent());
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetCanEverAffectNavigation(false);
		I->SetCastShadow(false);
		I->NumCustomDataFloats = 5;
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Mesh))
		{
			I->SetStaticMesh(M);
		}
		// (2026-11-25: the material is set in BeginPlay, not here: held by the class default, a material can't be remade in
		// the editor (the rooted-object assertion))
		return I;
	};
	PrintISM = Make(TEXT("Prints"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("/Game/BoneForge/Materials/M_BoneForgeMark.M_BoneForgeMark"));
	PuffISM = Make(TEXT("Puffs"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Game/BoneForge/Materials/M_BoneForgeSpray.M_BoneForgeSpray"));
	RingISM = Make(TEXT("Rings"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Game/BoneForge/Materials/M_BoneForgeSpray.M_BoneForgeSpray"));
}

void ABFEffects::BeginPlay()
{
	Super::BeginPlay();
	const TPair<UInstancedStaticMeshComponent*, const TCHAR*> Mats[] = {
		{ PrintISM, TEXT("/Game/BoneForge/Materials/M_BoneForgeMark.M_BoneForgeMark") },
		{ PuffISM, TEXT("/Game/BoneForge/Materials/M_BoneForgeSpray.M_BoneForgeSpray") },
		{ RingISM, TEXT("/Game/BoneForge/Materials/M_BoneForgeSpray.M_BoneForgeSpray") },
	};
	for (const auto& Pm : Mats)
	{
		if (Pm.Key)
		{
			if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Pm.Value, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Pm.Key->SetMaterial(0, M);
			}
		}
	}
}

ABFEffects* ABFEffects::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	static TWeakObjectPtr<ABFEffects> Cached;
	if (Cached.IsValid() && Cached->GetWorld() == World)
	{
		return Cached.Get();
	}
	for (TActorIterator<ABFEffects> It(World); It; ++It)
	{
		Cached = *It;
		return *It;
	}
	Cached = World->SpawnActor<ABFEffects>();
	return Cached.Get();
}

ABFWorld* ABFEffects::FindIsland()
{
	if (!Island.IsValid())
	{
		for (TActorIterator<ABFWorld> It(GetWorld()); It; ++It)
		{
			Island = *It;
			break;
		}
	}
	return Island.Get();
}

void ABFEffects::Footfall(const FVector& FootAt, float Yaw, float FootCm, float SpeedCm, float MassKg, int32 Toes)
{
	UWorld* W = GetWorld();
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(W, 0);
	if (!Cam || FVector::Dist(Cam->GetCameraLocation(), FootAt) > NearCm)
	{
		return;
	}
	const float Now = W->GetTimeSeconds();
	ABFWorld* I = FindIsland();
	const EBFBiome Biome = I ? I->BiomeAt(FVector2D(FootAt)) : EBFBiome::Plains;
	const FLinearColor Ground = FLinearColor(GroundCol[FMath::Clamp(int32(Biome), 0, 7)]);
	float Surface = 0.f;
	const bool bWet = I && I->WaterAt(FootAt + FVector(0.f, 0.f, 5.f), Surface) && Surface > FootAt.Z + 3.f;
	if (bWet)
	{
		// A splash: spray thrown up (more for a heavy foot, a fast one), and a ripple spreading on the surface.
		// (Raven's list, 31: they were too small and faint to see; now a sheet of drops, bigger the faster, a white
		// crown of foam where the foot went in, and a brighter ring.)
		const float Fast = FMath::Clamp(SpeedCm / 600.f, 0.f, 1.f);
		const int32 N = FMath::Clamp(int32(4 + MassKg / 1500.f + SpeedCm / 150.f), 4, 14);
		for (int32 k = 0; k < N && Puffs.Num() < MaxPuffs; ++k)
		{
			const FVector Out = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector();
			Puffs.Add({ FVector(FootAt.X, FootAt.Y, Surface) + Out * 0.35f * FootCm, Out * FMath::FRandRange(80.f, 200.f) * (0.7f + Fast) + FVector(0.f, 0.f, FMath::FRandRange(180.f, 360.f) + 0.35f * SpeedCm),
				FMath::FRandRange(0.22f, 0.4f) * FootCm, 1.3f, Now, FMath::FRandRange(0.5f, 0.85f), FLinearColor(0.85f, 0.9f, 0.92f), 0.8f, false });
		}
		for (int32 k = 0; k < 3 && Puffs.Num() < MaxPuffs; ++k)
		{
			const FVector Out = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector();
			Puffs.Add({ FVector(FootAt.X, FootAt.Y, Surface + 0.1f * FootCm) + Out * 0.25f * FootCm, Out * 30.f + FVector(0.f, 0.f, 25.f), 0.5f * FootCm * (1.f + Fast), 2.f, Now, FMath::FRandRange(0.9f, 1.4f),
				FLinearColor(0.9f, 0.93f, 0.95f), 0.5f, false });
		}
		Puffs.Add({ FVector(FootAt.X, FootAt.Y, Surface + 1.f), FVector::ZeroVector, 0.9f * FootCm, 5.f, Now, 1.8f, FLinearColor(0.85f, 0.9f, 0.92f), 0.5f, true });
		++SplashesMade;
		return;
	}
	// On the ground under the foot (Raven, 2026-11-15: prints at two heights, a Pteranodon's folded hands leaving them
	// in the air): a trace down to the island's ground or rock; nothing if the foot is over 15 cm above it.
	FHitResult Hit;
	FCollisionObjectQueryParams Statics(ECC_WorldStatic);
	// (The foot's place is its ankle, a little over the ground, more for a big foot: allowed 15 cm and 60% of its size.)
	if (!W->LineTraceSingleByObjectType(Hit, FootAt + FVector(0.f, 0.f, 40.f), FootAt - FVector(0.f, 0.f, 150.f), Statics))
	{
		++PrintsNoGround;
		return;
	}
	if (Hit.ImpactPoint.Z < FootAt.Z - 15.f - 0.6f * FootCm)
	{
		++PrintsInAir;
		return;
	}
	const FVector At = Hit.ImpactPoint;
	// A print: soft ground only (no rock), deeper and darker the heavier the animal; the beach's sand shows them most.
	if (Biome != EBFBiome::Rock && Biome != EBFBiome::Sea && !(I && I->RockAt(At)))
	{
		// (Raven's list, 30: faint on grass. Where it grows, a foot crushes it into the mud under it: a brown print, not
		// a darker green one; on sand a dark hollow.)
		const bool bGrass = Biome == EBFBiome::Plains || Biome == EBFBiome::Forest || Biome == EBFBiome::Swamp;
		const float Press = FMath::Clamp(0.6f + 0.1f * FMath::Loge(FMath::Max(MassKg, 1.f) / 50.f), 0.55f, 0.9f) * (Biome == EBFBiome::Beach ? 1.1f : 1.f);
		const FLinearColor Mud = bGrass ? FLinearColor(0.035f, 0.022f, 0.012f) : FLinearColor(0.012f, 0.009f, 0.006f);
		const FLinearColor Col = Ground * (1.f - Press) + Mud * Press;
		const FVector Up = FVector(Hit.ImpactNormal).GetSafeNormal();          // (lying on the slope, not level through it)
		Prints.Add({ At, Yaw, FootCm, Now, Col, Up, Toes });
		if (Prints.Num() > MaxPrints)
		{
			Prints.RemoveAt(0, Prints.Num() - MaxPrints);
		}
		bPrintsDirty = true;
		++PrintsMade;
	}
	// Dust: running on dry open ground (the beach, the plains, rock), a puff from each footfall.
	if ((SpeedCm > 300.f || (MassKg > 3000.f && SpeedCm > 120.f)) && (Biome == EBFBiome::Beach || Biome == EBFBiome::Plains || Biome == EBFBiome::Rock))
	{
		// (Raven's list, 31: too faint, and on the plains green. Dust is dry earth whatever grows on it: tan, bigger,
		// thicker, hanging longer; thrown back from the foot.)
		const FLinearColor Dust = FMath::Lerp(Ground, FLinearColor(0.36f, 0.29f, 0.2f), Biome == EBFBiome::Beach ? 0.3f : 0.75f);
		const FVector Back = -FRotator(0.f, Yaw, 0.f).Vector();
		const int32 N = FMath::Clamp(int32(2 + SpeedCm / 400.f + MassKg / 3000.f), 2, 7);
		for (int32 k = 0; k < N && Puffs.Num() < MaxPuffs; ++k)
		{
			const FVector Out = (FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector() + Back * 0.8f).GetSafeNormal();
			Puffs.Add({ At + Out * 0.4f * FootCm + FVector(0.f, 0.f, 0.3f * FootCm), Out * FMath::FRandRange(50.f, 140.f) + FVector(0.f, 0.f, FMath::FRandRange(30.f, 90.f)),
				FMath::FRandRange(0.6f, 0.9f) * FootCm, 4.f, Now, FMath::FRandRange(2.f, 3.5f), Dust, 0.42f, false });
		}
		++DustMade;
	}
}

void ABFEffects::Foam(const FVector& At, float SizeCm)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Puffs.Num() < MaxPuffs)
	{
		Puffs.Add({ At + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * SizeCm, FVector(0.f, 0.f, FMath::FRandRange(10.f, 40.f)),
			0.4f * SizeCm, 1.6f, Now, FMath::FRandRange(1.2f, 2.2f), FLinearColor(0.85f, 0.9f, 0.9f), 0.45f, false });
	}
}

void ABFEffects::DrawPrints(float Now)
{
	// Fading: the second half of its life it blends back into the ground's colour (an opaque mark can't go see-through).
	ABFWorld* I = FindIsland();
	PrintISM->ClearInstances();
	int32 Old = 0;
	TArray<float> Data;
	Data.SetNum(4);
	for (const FPrint& P : Prints)
	{
		const float Age = Now - P.Born;
		if (Age > PrintLife)
		{
			++Old;
			continue;
		}
		const EBFBiome Biome = I ? I->BiomeAt(FVector2D(P.At)) : EBFBiome::Plains;
		const FLinearColor Ground = FLinearColor(GroundCol[FMath::Clamp(int32(Biome), 0, 7)]);
		// (Fading to the ground's colour showed as pale discs, the ground drawn otherwise than its colour says: now it
		// fills in, shrinking away the second half of its life.)
		const FLinearColor C = P.Col;
		const float Fill = 1.f - FMath::SmoothStep(0.5f * PrintLife, PrintLife, Age);
		// Its shape: a biped's three toes splayed forward from a heel pad (a theropod's track); a four-footed animal's a
		// broad round pad with blunt toe marks round its front. Each piece a flat disc on the ground's slope.
		const FVector Fw0 = FRotator(0.f, P.Yaw, 0.f).Vector();
		const FVector Fw = (Fw0 - P.Up * FVector::DotProduct(Fw0, P.Up)).GetSafeNormal();
		const FVector Rt = FVector::CrossProduct(P.Up, Fw);
		auto Piece = [&](const FVector& Off, float AngDeg, float Len, float Wide, float Dark)
		{
			const FVector Dir = Fw.RotateAngleAxis(AngDeg, P.Up);
			const FQuat Q = FRotationMatrix::MakeFromZX(P.Up, Dir).ToQuat();
			const FVector At = P.At + Fw * Off.X + Rt * Off.Y + P.Up * 0.9f;
			const int32 Idx = PrintISM->AddInstance(FTransform(Q, At, FVector(Len / 100.f, Wide / 100.f, 0.004f)), true);
			Data[0] = C.R * Dark;
			Data[1] = C.G * Dark;
			Data[2] = C.B * Dark;
			Data[3] = 0.55f;
			PrintISM->SetCustomData(Idx, Data, false);
		};
		const float Cm = P.Cm * FMath::Max(Fill, 0.05f);
		// (2026-11-15, first look: a big heel and stubby toes read as a stick figure. A theropod's track is mostly toes:
		// long, splayed, the middle one longest, a small heel pad where they meet. A raptor's has two toes, side by side.)
		if (P.Toes == 3)
		{
			// (heavy toes, broad and blunt, a broad heel: a big theropod's, not a bird's scratch)
			Piece(FVector(-0.08f * Cm, 0.f, 0.f), 0.f, 0.55f * Cm, 0.55f * Cm, 1.f);
			for (const float A : { -27.f, 0.f, 27.f })
			{
				const float L = A == 0.f ? 1.f : 0.8f;
				const FVector D = FVector(1.f, 0.f, 0.f).RotateAngleAxis(A, FVector::UpVector);
				Piece(D * (0.12f + 0.45f * L) * Cm, A, L * Cm, 0.32f * Cm, 0.9f);
			}
		}
		else if (P.Toes == 2)
		{
			Piece(FVector(-0.05f * Cm, 0.f, 0.f), 0.f, 0.36f * Cm, 0.32f * Cm, 1.f);
			for (const float A : { -9.f, 9.f })
			{
				const FVector D = FVector(1.f, 0.f, 0.f).RotateAngleAxis(A, FVector::UpVector);
				Piece(D * 0.5f * Cm, A, 0.95f * Cm, 0.22f * Cm, 0.9f);
			}
		}
		else
		{
			Piece(FVector::ZeroVector, 0.f, 1.15f * Cm, 1.05f * Cm, 1.f);
			for (const float A : { -50.f, -17.f, 17.f, 50.f })
			{
				const FVector D = FVector(1.f, 0.f, 0.f).RotateAngleAxis(A, FVector::UpVector);
				Piece(D * 0.52f * Cm, A, 0.28f * Cm, 0.22f * Cm, 0.7f);
			}
		}
	}
	PrintISM->MarkRenderStateDirty();
	if (Old > 0)
	{
		Prints.RemoveAll([&](const FPrint& P) { return Now - P.Born > PrintLife; });
	}
}

void ABFEffects::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	PrintRedraw -= DeltaSeconds;
	if (PrintRedraw <= 0.f || (bPrintsDirty && PrintRedraw < 1.5f))
	{
		PrintRedraw = 2.f;
		bPrintsDirty = false;
		DrawPrints(Now);
	}
	// Plants pushed aside: the twelve animals nearest the camera (within 50 m) into MPC_BoneForgePush, where their feet
	// are and how far round them they push (their body's breadth and a bit; a long one twice, its hips and shoulders).
	if (!PushMPC)
	{
		PushMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/BoneForge/Materials/MPC_BoneForgePush.MPC_BoneForgePush"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (PushMPC && Cam)
	{
		const FVector C = Cam->GetCameraLocation();
		TArray<TPair<float, FLinearColor>> Near;
		for (TActorIterator<ABFDino> It(GetWorld()); It; ++It)
		{
			const ABFDino* D = *It;
			const float Dist = float(FVector::Dist(D->GetActorLocation(), C));
			if (Dist > 5000.f || D->IsFlying() || D->IsRagdoll())
			{
				continue;
			}
			const float R = D->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const FVector Feet = D->GetActorLocation() - FVector(0.f, 0.f, D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			Near.Add({ Dist, FLinearColor(Feet.X, Feet.Y, Feet.Z, 1.6f * R + 40.f) });
			if (D->BodyLengthCm() > 4.f * R)
			{
				const FVector Back = Feet - D->GetActorForwardVector() * 0.3f * D->BodyLengthCm();
				Near.Add({ Dist + 1.f, FLinearColor(Back.X, Back.Y, Back.Z, 1.4f * R + 40.f) });
			}
		}
		Near.Sort([](const TPair<float, FLinearColor>& A, const TPair<float, FLinearColor>& B) { return A.Key < B.Key; });
		for (int32 k = 0; k < 12; ++k)
		{
			UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), PushMPC, *FString::Printf(TEXT("Push%d"), k), k < Near.Num() ? Near[k].Value : FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
	}
	// Puffs: dust drifts and slows, spray falls back, ripples spread; all fade.
	PuffISM->ClearInstances();
	RingISM->ClearInstances();
	TArray<float> Data;
	Data.SetNum(5);
	ABFWorld* Isl = FindIsland();
	for (int32 k = Puffs.Num() - 1; k >= 0; --k)
	{
		FPuff& P = Puffs[k];
		const float U = (Now - P.Born) / P.Life;
		if (U >= 1.f)
		{
			Puffs.RemoveAtSwap(k);
			continue;
		}
		P.At += P.Vel * DeltaSeconds;
		if (P.Alpha > 0.5f && !P.bRing)
		{
			P.Vel.Z -= 980.f * DeltaSeconds;          // spray falls
		}
		else
		{
			P.Vel *= FMath::Max(1.f - 1.5f * DeltaSeconds, 0.f);          // dust hangs
		}
		const float Size = P.Cm * (1.f + (P.Grow - 1.f) * FMath::Sqrt(U));
		const float A = P.Alpha * (1.f - U) * FMath::SmoothStep(0.f, 0.08f, U);
		UInstancedStaticMeshComponent* To = P.bRing ? RingISM : PuffISM;
		const FVector S = P.bRing ? FVector(Size / 100.f, Size / 100.f, 0.01f) : FVector(Size / 100.f);
		if (!P.bWaterKnown)
		{
			P.bWaterKnown = true;
			float Wz = 0.f;
			P.WaterZ = Isl && Isl->WaterAt(P.At + FVector(0.f, 0.f, 50.f), Wz) ? Wz : -1e7f;
		}
		const int32 Idx = To->AddInstance(FTransform(FRotator::ZeroRotator, P.At, S), true);
		Data[0] = P.Col.R;
		Data[1] = P.Col.G;
		Data[2] = P.Col.B;
		Data[3] = P.bRing ? A + 10.f : A;          // (2026-11-25: M_BoneForgeSpray draws a ring as it was, unfaded, by this)
		Data[4] = P.WaterZ;
		To->SetCustomData(Idx, Data, false);
	}
	PuffISM->MarkRenderStateDirty();
	RingISM->MarkRenderStateDirty();
}
