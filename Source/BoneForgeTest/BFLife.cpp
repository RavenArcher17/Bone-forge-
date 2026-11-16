#include "BFLife.h"

#include "BFDino.h"
#include "BFGameMode.h"
#include "BFWorld.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "BFSpecies.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "GameFramework/PlayerController.h"

bool ABFLife::bShowThoughts = true;

namespace
{
	const FVector Wind(FMath::Cos(FMath::DegreesToRadians(ABFWorld::WindDeg)), FMath::Sin(FMath::DegreesToRadians(ABFWorld::WindDeg)), 0.f);

	FString ShortName(const FBFSpecies& S)
	{
		return S.Key == TEXT("trike") ? TEXT("Trike") : S.Key == TEXT("ankylo") ? TEXT("Ankylo") : S.Key == TEXT("brachio") ? TEXT("Brachio")
			: S.Key == TEXT("trex") ? TEXT("T. rex") : S.Key == TEXT("raptor") ? TEXT("Raptor") : S.Name;
	}

	FString Metres(float Cm)
	{
		return FString::Printf(TEXT("%.0f m"), Cm / 100.f);
	}
}

ABFLife::ABFLife()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ABFLife::EndPlay(const EEndPlayReason::Type Reason)
{
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life behaviour: %s"), *CallsText());
	Clear();
	Super::EndPlay(Reason);
}

void ABFLife::Clear()
{
	for (FBFAnimal& A : Animals)
	{
		if (A.Body.IsValid())
		{
			A.Body->Destroy();
		}
	}
	Animals.Reset();
	Herds.Reset();
	for (FBFCarcass& K : Carcasses)
	{
		for (UInstancedStaticMeshComponent* M : { K.Pools.Get(), K.Bones.Get(), K.Skull.Get() })
		{
			if (M)
			{
				M->DestroyComponent();
			}
		}
	}
	Carcasses.Reset();
	IndexCache.Reset();
	YoungWanted.Reset();
	for (FBFNest& N : Nests)
	{
		if (N.EggComp)
		{
			N.EggComp->DestroyComponent();
		}
		if (N.Scrape)
		{
			N.Scrape->DestroyComponent();
		}
	}
	Nests.Reset();
	KindInfo.Reset();
	ReadyFemales.Reset();          // (indices into the old animals: a load had one point past the new ones)
	Shoals.Reset();
	Colony = FVector::ZeroVector;
	Perches.Reset();
	NestTrees = 0;
	if (Island.IsValid() && LedgeRocks != INDEX_NONE)
	{
		Island->RemoveRocks(LedgeRocks);
	}
	LedgeRocks = INDEX_NONE;
	for (UInstancedStaticMeshComponent* C : { BarkComp.Get(), ShelfComp.Get(), LeafComp.Get(), StoneComp.Get(), SpireComp.Get() })
	{
		if (C)
		{
			C->ClearInstances();
		}
	}
	if (FishComp)
	{
		FishComp->ClearInstances();
	}
	if (FinComp)
	{
		FinComp->ClearInstances();
	}
	AliveClock = 0.f;
	PlayerI = INDEX_NONE;
	PlayerCourt = INDEX_NONE;
	PendingPlayerBody.Reset();
	OldPlayerBody.Reset();
	Newcomers = 0;
	Births = EggsLaid = EggsEaten = NumMatings = Contests = DiedYoung = Starved = OldAge = 0;
}

const TCHAR* ABFLife::ActName(EBFAct A)
{
	static const TCHAR* Names[] = { TEXT("idle"), TEXT("grazing"), TEXT("drinking"), TEXT("resting"), TEXT("fleeing"), TEXT("standing its ground"),
		TEXT("rejoining the herd"), TEXT("wandering"), TEXT("alert"), TEXT("hunting"), TEXT("feeding on a kill"), TEXT("backing off"), TEXT("courting"), TEXT("nesting") };
	return Names[FMath::Clamp(int32(A), 0, int32(EBFAct::Count) - 1)];
}

void ABFLife::Populate(ABFWorld* World, const FVector& Near)
{
	Clear();
	Island = World;
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	if (!GM || !World || !World->IsReady())
	{
		return;
	}
	// A herd of each plant eater among the six, round where the player is: Triceratops in fives, Ankylosaurus in threes,
	// Brachiosaurus in threes, each herd's place dry, gentle ground with plants.
	struct FPlan { const TCHAR* Key; int32 Count; float Dist; float Yaw; bool bHunter; };
	// And the hunters (step 4): a T. rex on its own, a pack of three raptors, further out.
	TArray<FPlan> Plans = { { TEXT("trike"), 5, 22000.f, 20.f, false }, { TEXT("ankylo"), 3, 28000.f, 140.f, false }, { TEXT("brachio"), 3, 36000.f, 260.f, false } };
	if (bHunters)
	{
		Plans.Add({ TEXT("trex"), 2, 45000.f, 80.f, true });          // a pair: she and a male
		Plans.Add({ TEXT("raptor"), 3, 42000.f, 200.f, true });
	}
	FRandomStream R(World->GetSeed() + 501);
	for (const FPlan& P : Plans)
	{
		AddHerd(P.Key, P.Count, Near, P.Dist, P.Yaw, P.bHunter, R);
	}
	if (bFliers)
	{
		PlaceColony(Near, R);
	}
}

int32 ABFLife::AddHerd(const FString& KindKey, int32 HowMany, const FVector& Near, float DistCm, float YawDeg, bool bHunter, FRandomStream& R, bool bLog)
{
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	ABFWorld* World = Island.Get();
	if (!GM || !World)
	{
		return 0;
	}
	const TArray<FBFSpecies>& Sp = GM->GetSpecies();
	struct { FString Key; int32 Count; float Dist; float Yaw; bool bHunter; } P = { KindKey, HowMany, DistCm, YawDeg, bHunter };
	{
		const int32 Index = Sp.IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == P.Key && !S.IsSplice(); });
		if (Index == INDEX_NONE)
		{
			return 0;
		}
		const FBFSpecies& S = Sp[Index];
		const float Wade = 0.45f * S.Bounds.GetSize().Z;
		FVector Centre = FVector::ZeroVector;
		bool bFound = false;
		for (int32 k = 0; k < 48 && !bFound; ++k)
		{
			const float Ang = FMath::DegreesToRadians(P.Yaw + (k % 2 == 0 ? 1.f : -1.f) * (k / 2) * 12.f);
			const float Dist = P.Dist * (1.f + 0.15f * (k / 12));
			const FVector At = Near + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * Dist;
			const EBFBiome B = World->BiomeAt(FVector2D(At));
			if ((B == EBFBiome::Plains || B == EBFBiome::Forest) && World->Walkable(At, S, 0.f))
			{
				Centre = At;
				bFound = true;
			}
		}
		if (!bFound)
		{
			return 0;
		}
		FBFHerd& Herd = Herds.AddDefaulted_GetRef();
		Herd.Kind = ShortName(S);
		Herd.Home = Centre;
		const float Body = S.Bounds.GetSize().X;
		for (int32 m = 0; m < P.Count; ++m)
		{
			FVector At = Centre;
			for (int32 Try = 0; Try < 12; ++Try)
			{
				const FVector Cand = Centre + FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), 0.f).GetSafeNormal() * (1.6f * Body * (0.5f + m));
				if (World->Walkable(Cand, S, 0.f))
				{
					At = Cand;
					break;
				}
			}
			float Ground = 0.f;
			World->GroundAt(FVector2D(At), Ground);
			AddAnimal(Index, Herds.Num() - 1, FVector(At.X, At.Y, Ground), R.FRandRange(0.f, 360.f), FString::Printf(TEXT("%s %d"), *Herd.Kind, m + 1), P.bHunter, R, bLog && m == 0);
		}
		Herd.Leader = Herd.Members.Num() > 0 ? Herd.Members[0] : 0;
		if (!P.bHunter && bYoung && Herd.Members.Num() > 0)
		{
			const FString YK = YoungKey(P.Key);
			TPair<FString, TArray<int32>>* Want = YoungWanted.FindByPredicate([&](const TPair<FString, TArray<int32>>& X) { return X.Key == YK; });
			if (!Want)
			{
				Want = &YoungWanted.Add_GetRef(TPair<FString, TArray<int32>>(YK, TArray<int32>()));
			}
			Want->Value.Add(Herds.Num() - 1);
		}
		if (bLog)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: a herd of %d %s at %s (%.0f m from you)"), Herd.Members.Num(), *Herd.Kind, *Centre.ToString(),
				FVector::Dist2D(Centre, Near) / 100.f);
		}
		return Herd.Members.Num();
	}
}

int32 ABFLife::AddAnimal(int32 Index, int32 HerdI, const FVector& At, float Yaw, const FString& Name, bool bHunter, FRandomStream& R, bool bLog)
{
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	const FBFSpecies& S = GM->GetSpecies()[Index];
	ABFDino* D = GM->SpawnCompanion(Index, At, Yaw);
	if (!D)
	{
		return INDEX_NONE;
	}
	if (const int32 Up = PerchNear(At, 450.f); Up != INDEX_NONE)
	{
		D->SetOnPerch(Perches[Up].Out);          // hatched up a tree: it can't walk off the shelf
	}
	const float Body = S.Bounds.GetSize().X;
	const FString Base = S.BaseKey.IsEmpty() ? S.Key : S.BaseKey;
	D->SetIdleBehaviours(true);
	FBFAnimal& A = Animals.AddDefaulted_GetRef();
	A.Body = D;
	A.Herd = HerdI;
	A.Name = Name;
	A.MassKg = S.MassKg;
	// Its senses from its body: eyes as high as its head stands see further; its nostrils' size, how far it smells.
	const float EyeM = 0.85f * S.Bounds.GetSize().Z / 100.f;
	A.SightCm = FMath::Clamp(9000.f + 4000.f * EyeM, 10000.f, 40000.f);
	A.SmellCm = 15000.f + 500000.f * S.NostrilShare;
	A.HearCm = 5000.f + 0.5f * Body;
	A.MouthfulKg = FMath::Max(0.2f, 0.0005f * S.MassKg);
	A.StomachMaxKg = FMath::Max(2.f, 0.004f * S.MassKg);
	A.DigestKgPerS = A.StomachMaxKg / 900.f;        // a full stomach lasts a quarter of an hour
	A.StomachKg = A.StomachMaxKg * R.FRandRange(0.2f, 0.7f);
	A.Thirst = R.FRandRange(0.1f, 0.5f);
	A.Tired = R.FRandRange(0.05f, 0.35f);
	A.ReachCm = 120.f + 0.5f * D->GetCapsuleComponent()->GetScaledCapsuleRadius();
	A.Spacing = 0.9f * Body;
	// Its weapons and armour, from its body: the Triceratops' horns, the Ankylosaurus' plates and tail club, the
	// sauropod's lashing tail and weight, the meat eaters' teeth and claws.
	const FString& Head = S.HeadKey.IsEmpty() ? S.Key : S.HeadKey;
	A.Weapon = Head == TEXT("trike") ? 1.6f : Base == TEXT("ankylo") ? 1.4f : Base == TEXT("brachio") ? 1.f : Head == TEXT("raptor") ? 0.9f : 1.f;
	A.Armour = Base == TEXT("ankylo") ? 0.55f : Base == TEXT("trike") ? 0.2f : 0.1f;
	A.FastCm = D->GetSpecies().Fast.SpeedCm;      // at the game pace
	A.bHunter = bHunter;
	if (A.bHunter)
	{
		// A meat eater eats big and seldom: a stomach of 4% of its weight, a meal lasting half an hour; it starts hungry.
		A.StomachMaxKg = FMath::Max(2.f, 0.04f * S.MassKg);
		A.DigestKgPerS = A.StomachMaxKg / 1800.f;
		A.StomachKg = A.StomachMaxKg * R.FRandRange(0.1f, 0.3f);
		A.Thirst = R.FRandRange(0.f, 0.15f);
	}
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s: %.2f t, walks %.1f m/s, runs %.1f m/s, sees %.0f m, smells %.0f m, weapon %.1f, armour %.2f"),
			*Name, S.MassKg / 1000.f, D->GetSpecies().Walk.SpeedCm / 100.f, D->GetSpecies().Fast.SpeedCm / 100.f, A.SightCm / 100.f, A.SmellCm / 100.f, A.Weapon, A.Armour);
	}
	A.WadeCm = 0.45f * S.Bounds.GetSize().Z;
	A.ThinkIn = R.FRandRange(0.f, 0.4f);
	A.LastPos = A.LastStep = D->GetActorLocation();
	A.SpeciesIndex = Index;
	A.Base = S.BaseKey.IsEmpty() ? S.Key : S.BaseKey;
	A.Stage = 4;
	A.bFemale = Herds[HerdI].Members.Num() % 2 == 0;
	A.Genes = FounderGenes(R);
	A.AgeS = R.FRandRange(32400.f, 0.6f * Lifespan);         // a grown animal, somewhere in its life (the young are set after)
	A.ClutchIn = R.FRandRange(0.f, 0.5f * BreedEvery);
	Herds[HerdI].Members.Add(Animals.Num() - 1);
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(D);
	T->SetupAttachment(D->GetRootComponent());
	T->SetRelativeLocation(FVector(0.f, 0.f, 0.5f * S.Bounds.GetSize().Z + 120.f));
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetWorldSize(FMath::Clamp(0.1f * Body, 50.f, 150.f));
	T->SetTextRenderColor(FColor(255, 250, 230));
	T->SetCastShadow(false);
	T->RegisterComponent();
	A.Label = T;
	ApplyGenes(A);
	ApplyBody(A);
	return Animals.Num() - 1;
}

// ---------------------------------------------------------------------------------------------- the young

void ABFLife::SpawnYoung(int32 HerdI, int32 SpeciesIndex, float Now)
{
	// Two young beside the herd's first adult (their mother), at 30% of its length, keeping to her.
	FBFHerd& Herd = Herds[HerdI];
	if (Herd.Members.Num() == 0 || !Animals[Herd.Members[0]].Body.IsValid())
	{
		return;
	}
	const int32 Mother = Herd.Members[0];
	const FVector M = Animals[Mother].Body->GetActorLocation();
	const float Len = Animals[Mother].Body->BodyLengthCm();
	FRandomStream R(HerdI * 31 + 7);
	for (int32 k = 0; k < 2; ++k)
	{
		const FVector At = M + Animals[Mother].Body->GetActorRightVector() * ((k == 0 ? 1.f : -1.f) * 0.9f * Len);
		float Ground = 0.f;
		Island->GroundAt(FVector2D(At), Ground);
		const int32 I = AddAnimal(SpeciesIndex, HerdI, FVector(At.X, At.Y, Ground), Animals[Mother].Body->GetActorRotation().Yaw,
			FString::Printf(TEXT("Young %s %d"), *Herd.Kind, k + 1), Animals[Mother].bHunter, R, k == 0);      // they take after her
		if (I != INDEX_NONE)
		{
			// Which stage this body is (its key), and its age at that stage's start.
			const FString Key = GetWorld()->GetAuthGameMode<ABFGameMode>()->GetSpecies()[SpeciesIndex].Key;
			int32 Stage = 2;
			for (int32 St = 1; St <= 3; ++St)
			{
				Stage = StageKey(Animals[Mother].Base, St) == Key ? St : Stage;
			}
			static const float Start[4] = { 0.f, 0.f, 5400.f, 14400.f };
			Animals[I].Stage = Stage;
			Animals[I].AgeS = Start[Stage];
			Animals[I].bYoung = Stage <= 2;
			++Herd.YoungBorn;
			Animals[I].Name = FString::Printf(TEXT("%s %s %d"), Stage == 1 ? TEXT("Hatchling") : Stage == 2 ? TEXT("Young") : TEXT("Subadult"), *Herd.Kind, NextNumber(Herd.Kind));       // numbered in its own herd
			Animals[I].Mother = Mother;
			Animals[I].Spacing = 0.6f * Len;          // close to her
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: two young with %s"), *Animals[Mother].Name);
}

bool ABFLife::StageBody(const FString& Base, int32 Stage, float& Size, int32& HeadPct)
{
	// The growth stages' bodies (1 hatchling, 2 juvenile, 3 subadult; 4 is the adult itself): a share of the adult's
	// length and a head that's bigger the younger. The raptors start so small their stages begin at 30% (a 15% raptor
	// would be a 30 cm body the walking can't keep on its feet).
	static const float Big[3] = { 0.15f, 0.3f, 0.6f }, Small[3] = { 0.3f, 0.5f, 0.75f };
	static const int32 BigHead[3] = { 140, 130, 110 }, SmallHead[3] = { 130, 120, 105 };
	if (Stage < 1 || Stage > 3)
	{
		return false;
	}
	const bool bSmall = Base == TEXT("raptor");
	Size = (bSmall ? Small : Big)[Stage - 1];
	HeadPct = (bSmall ? SmallHead : BigHead)[Stage - 1];
	return true;
}

FString ABFLife::StageKey(const FString& Base, int32 Stage)
{
	float Size = 1.f;
	int32 Head = 100;
	return StageBody(Base, Stage, Size, Head) ? FString::Printf(TEXT("ed_%s_he%d_z%d"), *Base, Head, FMath::RoundToInt(100.f * Size)) : Base;     // compose.py's key
}

FString ABFLife::YoungKey(const FString& Base)
{
	return StageKey(Base, 2);
}

int32 ABFLife::BodyIndex(const FString& Key)
{
	// The species index of a body, or INDEX_NONE (and asked for: grown, made ready and brought in, in turn).
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	const int32 Have = GM ? GM->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& X) { return X.Key == Key; }) : INDEX_NONE;
	if (Have == INDEX_NONE && !BodyFailed.Contains(Key) && !BodyQueue.Contains(Key))
	{
		BodyQueue.Add(Key);
	}
	return Have;
}

void ABFLife::TickYoung(float Now)
{
	// Herds waiting for their young: spawned once the young's body is in.
	for (int32 i = YoungWanted.Num() - 1; i >= 0; --i)
	{
		const int32 Have = BodyIndex(YoungWanted[i].Key);
		if (Have != INDEX_NONE || BodyFailed.Contains(YoungWanted[i].Key))
		{
			if (Have != INDEX_NONE)
			{
				for (const int32 H : YoungWanted[i].Value)
				{
					SpawnYoung(H, Have, Now);
				}
			}
			YoungWanted.RemoveAt(i);
		}
	}
	// The bodies asked for, one at a time: grown the first time (compose.py, its size and head), made ready to walk
	// (build_glb.py) and brought into the game (BFRuntimeImport); kept after that.
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	if (!GM)
	{
		return;
	}
	auto Fail = [&](const FString& Why)
	{
		UE_LOG(LogTemp, Error, TEXT("[BoneForge] life: %s: %s"), *YoungNow, *Why);
		BodyFailed.Add(YoungNow);
		BodyQueue.Remove(YoungNow);
		YoungStage = 0;
	};
	if (YoungStage == 0)
	{
		if (BodyQueue.Num() == 0)
		{
			return;
		}
		YoungNow = BodyQueue[0];
		if (GM->GetSpecies().ContainsByPredicate([&](const FBFSpecies& X) { return X.Key == YoungNow; }))
		{
			BodyQueue.RemoveAt(0);
			return;
		}
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(GM->RigFolder, TEXT("..")));
		const FString Rig = FPaths::Combine(GM->RigFolder, YoungNow + TEXT("_rig.json"));
		if (!FPaths::FileExists(Rig))
		{
			// ed_<base>_he<head>_z<size>
			TArray<FString> Bits;
			YoungNow.ParseIntoArray(Bits, TEXT("_"));
			if (Bits.Num() != 4)
			{
				Fail(TEXT("not a growth stage's key"));
				return;
			}
			const FString Base = Bits[1];
			const float Head = FCString::Atof(*Bits[2].Mid(2)) / 100.f, Size = FCString::Atof(*Bits[3].Mid(1)) / 100.f;
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Young")));
			IFileManager::Get().MakeDirectory(*Dir, true);
			const FString In = FPaths::Combine(Dir, YoungNow + TEXT(".json")), Out = FPaths::Combine(Dir, YoungNow + TEXT("_out.json"));
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"base\": \"%s\", \"size\": %.2f, \"sections\": {\"Head\": {\"scale\": %.2f}}}"), *Base, Size, Head), *In);
			YoungProc = FPlatformProcess::CreateProc(TEXT("python"), *FString::Printf(TEXT("\"%s\" build \"%s\" \"%s\""),
				*FPaths::Combine(Root, TEXT("splice"), TEXT("compose.py")), *In, *Out), true, true, true, nullptr, 0, nullptr, nullptr);
			YoungStage = 1;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: growing a body: %s"), *YoungNow);
		}
		else
		{
			YoungProc = FPlatformProcess::CreateProc(TEXT("python"), *FString::Printf(TEXT("\"%s\" %s"), *FPaths::Combine(Root, TEXT("unreal"), TEXT("build_glb.py")), *YoungNow),
				true, true, true, nullptr, 0, nullptr, nullptr);
			YoungStage = 2;
		}
		if (!YoungProc.IsValid())
		{
			Fail(TEXT("couldn't start python"));
		}
		return;
	}
	if ((YoungStage == 1 || YoungStage == 2) && !FPlatformProcess::IsProcRunning(YoungProc))
	{
		int32 Code = -1;
		FPlatformProcess::GetProcReturnCode(YoungProc, &Code);
		FPlatformProcess::CloseProc(YoungProc);
		YoungProc.Reset();
		if (Code != 0)
		{
			Fail(FString::Printf(TEXT("%s exited %d"), YoungStage == 1 ? TEXT("compose.py") : TEXT("build_glb.py"), Code));
			return;
		}
		if (YoungStage == 1)
		{
			YoungStage = 0;            // grown: next time round, made ready to walk
			return;
		}
		YoungStage = 3;
		const FString Glb = FPaths::ConvertRelativePathToFull(FPaths::Combine(GM->RigFolder, TEXT(".."), TEXT("unreal"), TEXT("out"), YoungNow + TEXT("_skeletal.glb")));
		const FString Key = YoungNow;
		// (Interchange starts from the engine's core ticker, not from an actor's tick: see BFSpliceStage.)
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Glb, Key](float)
		{
			FString Why;
			YoungImport = BFRuntimeImport::StartImport(Key, Glb, Why);
			YoungStage = YoungImport.IsValid() ? 4 : 0;
			if (!YoungImport.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("[BoneForge] life: couldn't import %s: %s"), *Key, *Why);
				BodyFailed.Add(Key);
				BodyQueue.Remove(Key);
			}
			return false;
		}));
		return;
	}
	if (YoungStage == 4 && YoungImport.IsValid())
	{
		USkeletalMesh* Mesh = nullptr;
		FString Why;
		if (BFRuntimeImport::Finished(YoungImport, Mesh, Why))
		{
			YoungImport.Reset();
			YoungStage = 0;
			FString Err;
			if (!Mesh || GM->AddSpecies(YoungNow, Mesh, Err) == INDEX_NONE)
			{
				Fail(Why + TEXT(" ") + Err);
				return;
			}
			BodyQueue.Remove(YoungNow);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: body ready: %s"), *YoungNow);
		}
	}
}

void ABFLife::Prefetch(const FString& Key)
{
	BodyIndex(Key);
}

void ABFLife::WantYoung(int32 HerdI, int32 Stage)
{
	if (!Herds.IsValidIndex(HerdI) || Herds[HerdI].Members.Num() == 0)
	{
		return;
	}
	const FString Key = StageKey(Animals[Herds[HerdI].Members[0]].Base, Stage);
	TPair<FString, TArray<int32>>* Want = YoungWanted.FindByPredicate([&](const TPair<FString, TArray<int32>>& X) { return X.Key == Key; });
	if (!Want)
	{
		Want = &YoungWanted.Add_GetRef(TPair<FString, TArray<int32>>(Key, TArray<int32>()));
	}
	Want->Value.Add(HerdI);
}

void ABFLife::ApplyBody(FBFAnimal& A)
{
	// What its mind takes from its body, again for a new body (growing up): senses, mouthful, stomach (as full as it
	// was), reach, room, wading depth, speed, weapons and armour (a youngster's armour thin yet).
	ABFDino* D = A.Body.Get();
	const FBFSpecies& S = D->GetSpecies();
	const float Body = S.Bounds.GetSize().X;
	const float Full = A.StomachMaxKg > 0.f ? A.StomachKg / A.StomachMaxKg : 0.5f;
	A.MassKg = S.MassKg * FMath::Cube(A.Genes.Size);
	const float EyeM = 0.85f * S.Bounds.GetSize().Z / 100.f;
	A.SightCm = FMath::Clamp(9000.f + 4000.f * EyeM, 10000.f, 40000.f);
	A.SmellCm = 15000.f + 500000.f * S.NostrilShare;
	A.HearCm = 5000.f + 0.5f * Body;
	A.MouthfulKg = FMath::Max(0.2f, 0.0005f * S.MassKg);
	A.StomachMaxKg = FMath::Max(A.bHunter ? 1.f : 2.f, (A.bHunter ? 0.04f : 0.004f) * A.MassKg);
	// What its genes cost (step 7): a bigger body more food (its stomach by its weight), a faster or stronger one burns it
	// quicker (muscle and pace aren't free).
	A.DigestKgPerS = A.StomachMaxKg / (A.bHunter ? 1800.f : 900.f) * (0.5f + 0.5f * A.Genes.Speed) * (0.5f + 0.5f * A.Genes.Strength);
	A.StomachKg = Full * A.StomachMaxKg;
	A.ReachCm = 120.f + 0.5f * D->GetCapsuleComponent()->GetScaledCapsuleRadius();
	A.Spacing = 0.9f * Body;
	A.WadeCm = 0.45f * S.Bounds.GetSize().Z;
	A.FastCm = S.Fast.SpeedCm;
	const FString Base = S.BaseKey.IsEmpty() ? S.Key : S.BaseKey;
	const FString& Head = S.HeadKey.IsEmpty() ? S.Key : S.HeadKey;
	A.Weapon = Head == TEXT("trike") ? 1.6f : Base == TEXT("ankylo") ? 1.4f : Base == TEXT("brachio") ? 1.f : Head == TEXT("raptor") ? 0.9f : 1.f;
	const float Armour = Base == TEXT("ankylo") ? 0.55f : Base == TEXT("trike") ? 0.2f : 0.1f;
	A.Armour = Armour * (A.Stage >= 4 ? 1.f : A.Stage == 3 ? 0.8f : 0.5f);
	A.MassKg = S.MassKg * FMath::Cube(A.Genes.Size);
	A.Weapon *= A.Genes.Strength;
}

void ABFLife::Grow(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	// Its growth clock: from hatching, a stage after another, about ten hours of play from egg to adult (an hour in the
	// egg, an hour and a half a hatchling, two and a half a juvenile, five a subadult). At each it gets its next body
	// where it stands: the same mind, memory, needs and family.
	A.AgeS += Dt * GrowRate;         // (adults age too: old age, Toll)
	if (A.Stage >= 4 || A.Base.IsEmpty())
	{
		return;
	}
	static const float Next[4] = { 0.f, 5400.f, 14400.f, 32400.f };     // age at which stage 1, 2, 3 ends
	if (A.AgeS < Next[A.Stage])
	{
		return;
	}
	const int32 To = A.Stage + 1;
	const FString Key = To >= 4 ? A.Base : StageKey(A.Base, To);
	const int32 SpeciesI = BodyIndex(Key);
	if (SpeciesI == INDEX_NONE)
	{
		return;                // its body is on its way
	}
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	const FVector At = A.Body->GetActorLocation();
	const float Yaw = A.Body->GetActorRotation().Yaw;
	float Ground = At.Z;
	Island->GroundAt(FVector2D(At), Ground);
	ABFDino* New = GM->SpawnCompanion(SpeciesI, FVector(At.X, At.Y, Ground), Yaw);
	if (!New)
	{
		return;
	}
	if (A.bPlayer)
	{
		PendingPlayerBody = New;          // (the controller takes it, then the old one goes)
		OldPlayerBody = A.Body;
	}
	else
	{
		A.Body->Destroy();
	}
	A.Body = New;
	A.SpeciesIndex = SpeciesI;
	A.Stage = To;
	A.bYoung = To <= 2;
	New->SetIdleBehaviours(true);
	ApplyBody(A);
	ApplyGenes(A);
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(New);
	T->SetupAttachment(New->GetRootComponent());
	T->SetRelativeLocation(FVector(0.f, 0.f, 0.5f * New->GetSpecies().Bounds.GetSize().Z + 120.f));
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetWorldSize(FMath::Clamp(0.1f * New->BodyLengthCm(), 50.f, 150.f));
	T->SetTextRenderColor(FColor(255, 250, 230));
	T->SetCastShadow(false);
	T->RegisterComponent();
	A.Label = T;
	A.Path.Reset();
	A.bGoal = false;
	++A.Growths;
	// Its name says what it is now (Hatchling Trike 3, Young Trike 3, Subadult Trike 3, Trike 3).
	for (const TCHAR* Was : { TEXT("Hatchling "), TEXT("Young "), TEXT("Subadult ") })
	{
		A.Name.RemoveFromStart(Was);
	}
	if (To < 4)
	{
		A.Name = (To == 2 ? FString(TEXT("Young ")) : FString(TEXT("Subadult "))) + A.Name;
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s grew into %s (%.0f kg, %.1f h old)"), *A.Name, *StageName(To), A.MassKg, A.AgeS / 3600.f);
}

FString ABFLife::StageName(int32 Stage)
{
	static const TCHAR* Names[] = { TEXT("an egg"), TEXT("a hatchling"), TEXT("a juvenile"), TEXT("a subadult"), TEXT("an adult") };
	return Names[FMath::Clamp(Stage, 0, 4)];
}

FVector ABFLife::HerdCentre(int32 Herd) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	if (Herds.IsValidIndex(Herd))
	{
		for (const int32 M : Herds[Herd].Members)
		{
			if (Animals[M].Body.IsValid() && !Animals[M].bDead)
			{
				Sum += Animals[M].Body->GetActorLocation();
				++Count;
			}
		}
	}
	return Count > 0 ? Sum / Count : FVector::ZeroVector;
}

float ABFLife::HerdMassNear(const FBFAnimal& A, float RadiusCm) const
{
	float Sum = 0.f;
	for (const int32 M : Herds[A.Herd].Members)
	{
		const FBFAnimal& B = Animals[M];
		if (B.Body.IsValid() && !B.bDead && FVector::Dist2D(B.Body->GetActorLocation(), A.Body->GetActorLocation()) < RadiusCm)
		{
			Sum += B.MassKg;
		}
	}
	return Sum;
}

FVector ABFLife::Snout(const FBFAnimal& A) const
{
	FVector S = A.Body->GetBoneWorld(TEXT("head_end"));
	return S.Equals(A.Body->GetActorLocation()) ? A.Body->GetBoneWorld(TEXT("head")) : S;
}

void ABFLife::Remember(FBFAnimal& A, uint8 Kind, const FVector& At, float Value, float Now)
{
	for (FBFMemory& M : A.Memory)
	{
		if (M.Kind == Kind && FVector::Dist2D(M.At, At) < 3000.f)
		{
			M.At = At;
			M.Value = Value;
			M.Time = Now;
			return;
		}
	}
	A.Memory.Add({ At, Value, Now, Kind });
	if (A.Memory.Num() > 24)
	{
		// Forgets the oldest.
		int32 Oldest = 0;
		for (int32 i = 1; i < A.Memory.Num(); ++i)
		{
			Oldest = A.Memory[i].Time < A.Memory[Oldest].Time ? i : Oldest;
		}
		A.Memory.RemoveAtSwap(Oldest);
	}
}

const FBFMemory* ABFLife::Recall(const FBFAnimal& A, uint8 Kind, const FVector& From) const
{
	const FBFMemory* Best = nullptr;
	float BestScore = 1e18f;
	for (const FBFMemory& M : A.Memory)
	{
		if (M.Kind != Kind || (Kind == 0 && M.Value < 1.f))
		{
			continue;
		}
		const float Score = FVector::Dist2D(M.At, From) - (Kind == 0 ? 200.f * FMath::Min(M.Value, 30.f) : 0.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = &M;
		}
	}
	return Best;
}

bool ABFLife::GoTo(FBFAnimal& A, const FVector& To)
{
	if (!A.bGoal || FVector::Dist2D(A.Goal, To) > 1000.f)
	{
		A.ProgressDist = 1e9f;          // a new goal starts its progress afresh (the same one, found again, doesn't)
		A.ProgressAt = GetWorld()->GetTimeSeconds();
	}
	A.Path.Reset();
	A.PathI = 0;
	A.Goal = To;
	A.bGoal = true;
	A.Stucks = 0;
	const FVector Me = A.Body->GetActorLocation();
	// In a cave: out along its floor to its mouth first (the island's paths run over the hill above it).
	const int32 InCave = Island.IsValid() ? Island->CaveAt(Me + FVector(0.f, 0.f, 50.f)) : INDEX_NONE;
	if (InCave != INDEX_NONE && Island->CaveAt(To + FVector(0.f, 0.f, 50.f)) != InCave)
	{
		const TArray<FVector>& Fl = Island->CaveFloor(InCave);
		int32 Nearest = 0;
		for (int32 i = 1; i < Fl.Num(); ++i)
		{
			Nearest = FVector::DistSquared(Fl[i], Me) < FVector::DistSquared(Fl[Nearest], Me) ? i : Nearest;
		}
		for (int32 i = Nearest; i >= 0; --i)
		{
			A.Path.Add(Fl[i]);
		}
		A.Path.Add(Island->CaveMouth(InCave));
		TArray<FVector> On;
		if (FVector::Dist2D(Island->CaveMouth(InCave), To) > 3000.f)
		{
			Island->FindPath(Island->CaveMouth(InCave), To, A.Body->GetSpecies(), A.WadeCm, On);
		}
		A.Path.Append(On.Num() > 0 ? On : TArray<FVector>{ To });
		++A.Paths;
		return true;
	}
	if (FVector::Dist2D(Me, To) < 3000.f || !Island.IsValid())
	{
		A.Path.Add(To);
		return true;
	}
	Island->FindPath(Me, To, A.Body->GetSpecies(), A.WadeCm, A.Path);
	++A.Paths;
	if (A.Path.Num() == 0)
	{
		A.Path.Add(To);
	}
	return true;
}

void ABFLife::Choose(FBFAnimal& A, EBFAct Act, float Now, const FString& Why)
{
	if (A.Act == Act)
	{
		return;
	}
	A.Act = Act;
	A.ActSince = Now;
	if (A.Body.IsValid())
	{
		A.Body->ClearMouthTarget();          // (a new act: its mouth's next target is that act's)
	}
	A.Path.Reset();
	A.bGoal = false;
	A.bFood = false;
	A.WaterAt = FVector::ZeroVector;
	A.ProgressDist = 1e9f;
	A.ProgressAt = Now;
	A.Thought = Why;
	++A.Decisions;
	if (Act == EBFAct::Flee)
	{
		++A.Flights;
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s -> %s: %s"), *A.Name, ActName(Act), *Why);
}

void ABFLife::Sense(FBFAnimal& A, float Now)
{
	// Each meat eater big enough to matter (a tenth of its weight or more): seen, smelled or heard?
	const FVector Me = A.Body->GetActorLocation();
	const FVector Eye = Me + FVector(0.f, 0.f, 0.35f * A.Body->GetSpecies().Bounds.GetSize().Z);
	const FVector Fwd = A.Body->GetActorForwardVector();
	const float SenseCm = FMath::Max3(A.SightCm, A.SmellCm, A.HearCm);
	for (const TWeakObjectPtr<ABFDino>& HW : Hunters)
	{
		ABFDino* H = HW.Get();
		if (!H || FVector::DistSquared2D(Me, H->GetActorLocation()) > FMath::Square(SenseCm))
		{
			continue;              // beyond anything it could sense
		}
		const int32 HI = IndexOf(H);
		if (H->IsRagdoll() || H == A.Body.Get() || (HI != INDEX_NONE && Animals[HI].Herd == A.Herd))
		{
			continue;              // not itself, nor its own pack
		}
		const float Ratio = H->GetSpecies().MassKg / FMath::Max(A.MassKg, 1.f);
		if (Ratio < 0.1f)
		{
			continue;
		}
		const FVector There = H->GetActorLocation();
		const float Dist = FVector::Dist2D(Me, There);
		const FVector ToIt = (There - Me).GetSafeNormal2D();
		FString How;
		// Sight: wide-set eyes see all round but for a blind spot behind; hills and trunks get in the way. Head down (grazing,
		// drinking) it sees half as far; a hunter creeping or standing still is hard to make out, under trees harder.
		const int32 HAt = HI;
		const bool bStill = H->GetVelocity().Size2D() < 120.f || (HAt != INDEX_NONE && Animals[HAt].bCreeping);
		const float Dark = Island.IsValid() ? FMath::Lerp(0.35f, 1.f, Island->Daylight()) : 1.f;       // at night eyes see a third as far
		const float SeeCm = A.SightCm * Dark * (A.Act == EBFAct::Graze || A.Act == EBFAct::Drink ? 0.5f : 1.f) * (bStill ? 0.55f : 1.f) * (A.bWatch && A.Act == EBFAct::Alert ? 1.4f : 1.f)
			* (1.f - 0.6f * FMath::Clamp(Island.IsValid() ? Island->CoverAt(FVector2D(There)) : 0.f, 0.f, 1.f));
		if (Dist < SeeCm && FVector::DotProduct(Fwd, ToIt) > -0.85f)
		{
			FHitResult Hit;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(BFLifeSight), false, A.Body.Get());
			Q.AddIgnoredActor(H);
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, There + FVector(0.f, 0.f, 100.f), ECC_Visibility, Q))
			{
				How = TEXT("saw");
			}
		}
		// Smell: carried on the wind, so only from upwind (it can't smell what's downwind of it however near).
		if (How.IsEmpty() && Dist < A.SmellCm && FVector::DotProduct((Me - There).GetSafeNormal2D(), Wind) > 0.55f)
		{
			How = TEXT("smelled");
		}
		// Hearing: something big running near.
		if (How.IsEmpty() && Dist < A.HearCm * (1.f + Ratio) && H->GetVelocity().Size2D() > 350.f)
		{
			How = TEXT("heard");
		}
		if (How.IsEmpty())
		{
			continue;
		}
		const float Range = FMath::Max(A.SightCm, A.SmellCm);
		const float Near = 1.f - FMath::Clamp(Dist / Range, 0.f, 1.f);
		const float Closing = FMath::Clamp(FVector::DotProduct(H->GetVelocity(), (Me - There).GetSafeNormal2D()) / 600.f, 0.f, 1.f);
		float Want = FMath::Clamp(0.35f + 0.75f * Near + 0.3f * Closing, 0.f, 1.f) * FMath::Clamp(1.5f * Ratio, 0.3f, 1.f);
		if (How == TEXT("smelled"))
		{
			A.Suspicion = FMath::Max(A.Suspicion, 0.8f);
			Want *= 0.55f;           // a smell says something's about, not where it's going
		}
		A.Fear = FMath::Max(A.Fear, Want);
		A.Threat = H;
		A.ThreatAt = There;
		A.ThreatAgo = 0.f;
		A.ThreatHow = FString::Printf(TEXT("%s a %s %s off"), *How, *H->GetSpecies().Name, *Metres(Dist));
		Remember(A, 2, There, Want, Now);
	}
}

void ABFLife::Think(FBFAnimal& A, int32 Index, float Now)
{
	const FVector Me = A.Body->GetActorLocation();
	const FBFHerd& Herd = Herds[A.Herd];
	const FVector Centre = HerdCentre(A.Herd);
	// An alarm: one frightened, the rest of its herd near it take it up (and know where the danger is).
	if (A.Fear > 0.5f && A.ThreatAgo < 2.f)
	{
		if (Now - A.CalledAt > 12.f && A.Body.IsValid())
		{
			A.Body->Call(0.9f);          // the alarm: a call
			A.CalledAt = Now;
			++AlarmCalls;
		}
		for (const int32 M : Herd.Members)
		{
			FBFAnimal& B = Animals[M];
			if (M != Index && B.Body.IsValid() && B.Fear < 0.8f * A.Fear && FVector::Dist2D(B.Body->GetActorLocation(), Me) < 15000.f)
			{
				B.Fear = 0.8f * A.Fear;
				B.Threat = A.Threat;
				B.ThreatAt = A.ThreatAt;
				B.ThreatAgo = A.ThreatAgo;
				B.ThreatHow = FString::Printf(TEXT("%s called an alarm"), *A.Name);
				if (B.CallIn < 0.f && Now - B.CalledAt > 12.f)
				{
					B.CallIn = FMath::FRandRange(0.4f, 1.8f);          // answered
				}
			}
		}
	}
	// Settling: a hunter that stays still and far off (over 40 m) frightens it less and less; back to grazing, watchful.
	if (A.Threat.IsValid() && A.Fear > 0.f && FVector::Dist2D(A.Threat->GetActorLocation(), Me) > 4000.f && A.Threat->GetVelocity().Size2D() < 150.f)
	{
		A.Fear = FMath::Max(0.f, A.Fear - 0.03f);
		A.Suspicion = FMath::Max(A.Suspicion, 0.3f);
	}
	// Stand or run: a herd that outweighs the meat eater two and a half times faces it together; less, it runs.
	const float ThreatKg = A.Threat.IsValid() ? A.Threat->GetSpecies().MassKg : 0.f;
	const float HerdKg = HerdMassNear(A, 4000.f);
	A.bStandsGround = ThreatKg > 0.f && HerdKg >= 2.5f * ThreatKg;
	const float Hunger = 1.f - A.StomachKg / A.StomachMaxKg;
	const float FromHerd = FVector::Dist2D(Me, Centre);
	float Score[int32(EBFAct::Count)] = {};
	Score[int32(EBFAct::Flee)] = A.Fear * 2.1f * (A.bStandsGround && !A.bYoung ? 0.2f : 1.f);
	Score[int32(EBFAct::Defend)] = A.Fear * 2.1f * (A.bStandsGround && !A.bYoung ? 1.f : 0.f);     // the young run (to her)
	Score[int32(EBFAct::Alert)] = A.Suspicion * 0.7f;
	Score[int32(EBFAct::Drink)] = A.Thirst > 0.2f ? FMath::Pow(A.Thirst, 1.2f) * 1.45f : 0.f;
	Score[int32(EBFAct::Graze)] = Hunger > 0.15f || (A.Act == EBFAct::Graze && Hunger > 0.05f) ? Hunger + 0.1f : 0.f;
	Score[int32(EBFAct::Rest)] = A.Tired > 0.3f && A.Fear < 0.2f ? FMath::Pow(A.Tired, 1.5f) * 1.15f : 0.f;
	// Night: the herd sleeps together (near each other: rejoining first).
	const float Night = Island.IsValid() ? 1.f - Island->Daylight() : 0.f;
	if (A.Fear < 0.3f)
	{
		Score[int32(EBFAct::Rest)] += 0.75f * Night;
	}
	Score[int32(EBFAct::Regroup)] = Herd.Members.Num() > 1 ? FMath::Clamp((FromHerd - 3.f * A.Spacing) / (5.f * A.Spacing), 0.f, 1.f) * 1.3f : 0.f;
	// The young keep to their mother: more the further they stray; frightened, they run to her.
	const bool bHasMother = A.bYoung && Animals.IsValidIndex(A.Mother) && !Animals[A.Mother].bDead && Animals[A.Mother].Body.IsValid();
	if (bHasMother)
	{
		const float FromMother = FVector::Dist2D(Me, Animals[A.Mother].Body->GetActorLocation());
		Score[int32(EBFAct::Regroup)] = FMath::Max(Score[int32(EBFAct::Regroup)], FMath::Clamp((FromMother - 2.f * A.Spacing) / (4.f * A.Spacing), 0.f, 1.f) * 1.6f * (1.f - 0.6f * Hunger));
		if (Hunger > 0.5f && FromMother < 6000.f)
		{
			Score[int32(EBFAct::Graze)] += 0.4f;       // hungry, it grazes where it is, near her
		}
	}
	// A mother whose young is frightened or hurt near her stands for it, whatever the herd weighs.
	bool bGuarding = false;
	for (const FBFAnimal& Y : Animals)
	{
		if (Y.bYoung && Y.Mother == Index && !Y.bDead && Y.Body.IsValid() && Y.Fear > 0.5f && FVector::Dist2D(Y.Body->GetActorLocation(), Me) < 4000.f)
		{
			bGuarding = true;
		}
	}
	if (bGuarding && A.Threat.IsValid() && A.Threat->GetSpecies().MassKg < 1.5f * A.MassKg)
	{
		A.bStandsGround = true;
	}
	if (Nests.IsValidIndex(A.NestI) && Nests[A.NestI].Eggs > 0 && A.Threat.IsValid() && FVector::Dist2D(A.ThreatAt, Nests[A.NestI].At) < 5000.f
		&& A.Threat->GetSpecies().MassKg < 1.5f * A.MassKg)
	{
		A.bStandsGround = true;        // over her eggs
	}
	Score[int32(EBFAct::Wander)] = 0.12f;
	// Herd animals do what the others near them are doing: drinking, grazing and resting together keeps them together.
	bool bOthers[int32(EBFAct::Count)] = {};
	for (const int32 M : Herd.Members)
	{
		const FBFAnimal& B = Animals[M];
		if (M != Index && B.Body.IsValid() && FVector::Dist2D(B.Body->GetActorLocation(), Me) < 6.f * A.Spacing)
		{
			bOthers[int32(B.Act)] = true;
		}
	}
	// (together: they drink, graze and rest with the others even before they need to much)
	Score[int32(EBFAct::Drink)] += bOthers[int32(EBFAct::Drink)] && A.Thirst > 0.08f ? 0.3f : 0.f;
	Score[int32(EBFAct::Graze)] += bOthers[int32(EBFAct::Graze)] && Hunger > 0.05f ? 0.2f : 0.f;
	Score[int32(EBFAct::Rest)] += bOthers[int32(EBFAct::Rest)] && A.Tired > 0.05f ? 0.25f : 0.f;
	// The day's rhythm: most grazing at dawn and dusk, resting through the midday heat (unless hungry or thirsty), and a
	// rest seen through till rested.
	const float Hr = Island.IsValid() ? Island->GetHours() : 12.f;
	const bool bPeak = (Hr >= 5.f && Hr < 9.f) || (Hr >= 16.f && Hr < 20.f);
	const bool bHeat = Hr >= 11.f && Hr < 14.5f;
	if (bPeak && Hunger > 0.2f && A.Fear < 0.3f)
	{
		Score[int32(EBFAct::Graze)] = FMath::Max(Score[int32(EBFAct::Graze)], Hunger + 0.35f);
	}
	if (bHeat && Hunger < 0.45f && A.Thirst < 0.5f && A.Fear < 0.2f)
	{
		Score[int32(EBFAct::Rest)] += 0.45f;
	}
	if (A.Act == EBFAct::Rest && A.Tired > 0.05f && A.Fear < 0.3f)
	{
		Score[int32(EBFAct::Rest)] += 0.3f;
	}
	// B: the herd moving on: those not with it follow to its new grounds.
	if (Now - Herd.MovedAt < 90.f && FVector::Dist2D(Me, Herd.Home) > 6000.f && A.Fear < 0.3f)
	{
		Score[int32(EBFAct::Regroup)] = FMath::Max(Score[int32(EBFAct::Regroup)], 1.f);
	}
	// B: a watch: while the herd grazes one keeps watch, head up, by turns (every 45 s the next), unless it's hungry.
	A.bWatch = false;
	if (Herd.Members.Num() >= 3 && !A.bYoung && bOthers[int32(EBFAct::Graze)] && Hunger < 0.6f && A.Fear < 0.3f
		&& Herd.Members[int32(Now / 45.f) % Herd.Members.Num()] == Index)
	{
		Score[int32(EBFAct::Alert)] = FMath::Max(Score[int32(EBFAct::Alert)], 0.95f);
		A.bWatch = true;
	}
	// E: the young play when fed and safe in daylight: short chases with another young one of the herd.
	A.bPlay = false;
	if (A.bYoung && Hunger < 0.35f && A.Thirst < 0.4f && A.Fear < 0.1f && Island.IsValid() && Island->Daylight() > 0.5f)
	{
		for (const int32 M : Herd.Members)
		{
			const FBFAnimal& B = Animals[M];
			if (M != Index && B.bYoung && !B.bDead && B.Body.IsValid() && FVector::Dist2D(B.Body->GetActorLocation(), Me) < 3000.f)
			{
				Score[int32(EBFAct::Wander)] = FMath::Max(Score[int32(EBFAct::Wander)], 0.55f);
				A.bPlay = true;
				A.PlayWith = M;
				break;
			}
		}
	}
	BreedScores(A, Index, Now, Score);
	// Reluctance to drop what it's doing (more in its first few seconds), so it doesn't dither between two wants.
	const bool bUrgent = A.Act == EBFAct::Flee || A.Act == EBFAct::Defend || A.Act == EBFAct::Alert;
	Score[int32(A.Act)] += 0.15f + (Now - A.ActSince < 4.f && !bUrgent ? 0.25f : 0.f);
	// On its way to food or water (or eating, or drinking), it sees it through: hunger and thirst both high had it
	// turning back and forth between them, each turn dropping its path, reaching neither.
	// (it sees it through while it's getting closer: no 10 m nearer in 40 s and it's stuck)
	if (A.bGoal)
	{
		const float ToGoal = FVector::Dist2D(Me, A.Goal);
		if (ToGoal < A.ProgressDist - 1000.f)
		{
			A.ProgressDist = ToGoal;
			A.ProgressAt = Now;
		}
	}
	else
	{
		A.ProgressDist = 1e9f;
		A.ProgressAt = Now;
	}
	const bool bStuckOnWay = A.bGoal && Now - A.ProgressAt > 40.f;
	if ((A.Act == EBFAct::Graze || A.Act == EBFAct::Drink) && (A.bGoal || A.bFood || !A.WaterAt.IsZero()) && !bStuckOnWay)
	{
		Score[int32(A.Act)] += 0.45f;
	}
	if (A.Act == EBFAct::Drink && bStuckOnWay && !A.WaterAt.IsZero() && FVector::Dist2D(Me, A.Goal) < 1200.f)
	{
		A.Path.Reset();
		A.bGoal = false;          // a bank it can't get closer on: it drinks from where it is (its neck reaches)
	}
	else if (A.Act == EBFAct::Drink && bStuckOnWay && !A.WaterAt.IsZero() && A.Thirst > 0.3f)
	{
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s couldn't get to the water: looking for other water (%.0f m short of it, path %d of %d, stuck %d times)"), *A.Name,
			FVector::Dist2D(Me, A.Goal) / 100.f, A.PathI, A.Path.Num(), A.Stucks);
		// Couldn't get to it: that water is forgotten (its own memory and the herd's), remembered as out of reach, and it
		// looks for other water.
		A.Memory.RemoveAll([&](const FBFMemory& M) { return M.Kind == 1 && FVector::Dist2D(M.At, A.WaterAt) < 8000.f; });
		Remember(A, 3, A.WaterAt, 1.f, Now);
		Herds[A.Herd].Waters.RemoveAll([&](const FVector& W) { return FVector::Dist2D(W, A.WaterAt) < 8000.f; });
		A.WaterAt = FVector::ZeroVector;
		A.Path.Reset();
		A.bGoal = false;
		A.ActSince = Now;
		A.ProgressDist = 1e9f;
		A.ProgressAt = Now;
	}
	int32 Best = 0;
	for (int32 i = 1; i < int32(EBFAct::Count); ++i)
	{
		Best = Score[i] > Score[Best] ? i : Best;
	}
	const EBFAct Act = EBFAct(Best);
	if (Act == A.Act)
	{
		return;
	}
	const FString ThreatName = A.Threat.IsValid() ? A.Threat->GetSpecies().Name : FString(TEXT("something"));
	FString Why;
	switch (Act)
	{
	case EBFAct::Flee: Why = FString::Printf(TEXT("%s: running (fear %.2f; the herd near it weighs %.0f t, the %s %.0f t)"), *A.ThreatHow, A.Fear, HerdKg / 1000.f, *ThreatName, ThreatKg / 1000.f); break;
	case EBFAct::Defend: Why = FString::Printf(TEXT("%s: the herd near it (%.0f t) outweighs the %s (%.0f t), facing it together"), *A.ThreatHow, HerdKg / 1000.f, *ThreatName, ThreatKg / 1000.f); break;
	case EBFAct::Alert: Why = A.bWatch ? FString(TEXT("keeping watch while the herd grazes")) : FString::Printf(TEXT("%s: head up, looking"), *A.ThreatHow); break;
	case EBFAct::Drink: Why = FString::Printf(TEXT("thirsty (%.2f)"), A.Thirst); break;
	case EBFAct::Graze: Why = bPeak ? FString::Printf(TEXT("%s: the time to graze (%.0f of %.0f kg in its stomach)"), Hr < 12.f ? TEXT("dawn") : TEXT("dusk"), A.StomachKg, A.StomachMaxKg)
		: FString::Printf(TEXT("hungry: %.0f of %.0f kg in its stomach"), A.StomachKg, A.StomachMaxKg); break;
	case EBFAct::Rest: Why = Night > 0.5f ? TEXT("night: sleeping with the herd") : bHeat ? TEXT("the heat of the day: resting") : FString::Printf(TEXT("tired (%.2f)"), A.Tired); break;
	case EBFAct::Regroup: Why = Now - Herd.MovedAt < 90.f && FVector::Dist2D(Me, Herd.Home) > 6000.f ? FString(TEXT("the herd is moving on: following it"))
		: FString::Printf(TEXT("%s from the herd"), *Metres(FromHerd)); break;
	case EBFAct::Wander: Why = A.bPlay && Animals.IsValidIndex(A.PlayWith) ? FString::Printf(TEXT("playing with %s"), *Animals[A.PlayWith].Name) : FString(TEXT("nothing pressing: looking about")); break;
	case EBFAct::Court: case EBFAct::Nest: Why = BreedWhy; break;
	default: Why = TEXT("nothing pressing: looking about"); break;
	}
	Choose(A, Act, Now, Why);
}

void ABFLife::Do(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	const float Rate = A.bHunter ? 0.f : NeedRate * Dt;         // a hunter keeps its own needs (HunterDo)
	// Needs: it digests, it dries, it tires (more for running: its own winding), and rests it off.
	A.StomachKg = FMath::Max(0.f, A.StomachKg - A.DigestKgPerS * Rate);
	A.Thirst = FMath::Min(1.f, A.Thirst + Rate / 420.f);
	A.Tired = A.Act == EBFAct::Rest ? FMath::Max(0.f, A.Tired - Rate / 45.f) : FMath::Min(1.f, A.Tired + Rate * (1.f / 900.f + 0.012f * FMath::Max(D->GetExertion() - 0.3f, 0.f)));
	A.ThreatAgo += A.bHunter ? 0.f : Dt;
	if (A.ThreatAgo > 3.f)
	{
		A.Fear = FMath::Max(0.f, A.Fear - 0.06f * Dt);
	}
	A.Suspicion = FMath::Max(0.f, A.Suspicion - 0.12f * Dt);
	if (!A.bHunter) { D->SetRunning(A.Act == EBFAct::Flee && D->GetExertion() < 0.85f); }
	// Resting, once it has stood a few seconds where it is, it lies down (asleep at night, or dozing by day); anything
	// else, it gets up first.
	D->SetLying(A.Act == EBFAct::Rest && Now - A.ActSince > 4.f && !A.bGoal && A.Path.Num() == 0 && !A.bFlier,
		!Island.IsValid() || Island->Daylight() < 0.4f || A.Tired > 0.6f);
	if (A.CallIn >= 0.f)
	{
		A.CallIn -= Dt;
		if (A.CallIn < 0.f)
		{
			D->Call(0.7f);          // answering the alarm
			A.CalledAt = Now;
			++AlarmAnswers;
		}
	}
	// A mother with her young near and nothing pressing: now and then she noses one (a touch of her snout to its head,
	// a few seconds), then gets on.
	if (Now < A.NuzzleUntil && Animals.IsValidIndex(A.NuzzleWho) && Animals[A.NuzzleWho].Body.IsValid() && !Animals[A.NuzzleWho].bDead && A.Fear < 0.3f)
	{
		ABFDino* Y = Animals[A.NuzzleWho].Body.Get();
		A.Path.Reset();
		A.bGoal = false;
		MouthTo(A, Y->GetBoneWorld(TEXT("head")) + FVector(0.f, 0.f, 10.f), Dt);
		A.Thought = FString::Printf(TEXT("nosing her young %s"), *Animals[A.NuzzleWho].Name);
		return;
	}
	if (!A.bYoung && A.Fear < 0.2f && (A.Act == EBFAct::Idle || A.Act == EBFAct::Wander || A.Act == EBFAct::Graze) && Now - A.NuzzleUntil > 35.f
		&& FMath::Frac(Now / 6.f + Index * 0.29f) < Dt / 6.f)
	{
		for (int32 y = 0; y < Animals.Num(); ++y)
		{
			const FBFAnimal& Y = Animals[y];
			if (Y.Mother == Index && !Y.bDead && Y.Body.IsValid() && FVector::Dist2D(Y.Body->GetActorLocation(), Me) < 900.f + 0.5f * D->BodyLengthCm())
			{
				A.NuzzleUntil = Now + 4.f;
				A.NuzzleWho = y;
				++Nuzzles;
				break;
			}
		}
	}
	// Something's about (a smell, a meat eater settled far off): it looks up and toward it now and then.
	const FVector Watched = A.Threat.IsValid() ? A.Threat->GetActorLocation() : A.ThreatAt;
	if (!A.bHunter && (A.Suspicion > 0.25f || (A.Fear > 0.2f && A.Act != EBFAct::Flee)) && !Watched.IsZero()
		&& (A.Act == EBFAct::Wander || A.Act == EBFAct::Idle || A.Act == EBFAct::Regroup || (A.Act == EBFAct::Graze && FMath::Frac(Now / 9.f + Index * 0.31f) < 0.3f)))
	{
		D->SetGaze(Watched, 0.5f);
	}
	const float TopZ = Snout(A).Z + 0.3f * FMath::Max(Snout(A).Z - (Me.Z - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), 0.f);
	switch (A.Act)
	{
	case EBFAct::Graze:
	{
		if (!A.bFood)
		{
			// The nearest plant worth cropping round it and its herd; else a place it remembers food; else search.
			const FVector Look = FMath::Lerp(Me, HerdCentre(A.Herd), 0.4f);
			float Kg = 0.f;
			// (Not a plant whose leaves are all lower than its mouth gets down to, once it has found that out.)
			const float FeetZ = float(Me.Z) - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float MinZ = D->GetMouthLowCm() > 0.f ? FeetZ + D->GetMouthLowCm() : -1e9f;
			if (Island->FindPlant(Look, A.StomachKg < 0.4f * A.StomachMaxKg ? 9000.f : 5000.f, TopZ, A.FoodAt, A.FoodReach, Kg, MinZ, &A.FoodLowZ, &A.FoodHighZ))
			{
				A.bFood = true;
				Remember(A, 0, A.FoodAt, Kg, Now);
				GoTo(A, A.FoodAt);
				A.Thought = FString::Printf(TEXT("hungry: going to a plant %s off (%.0f kg on it)"), *Metres(FVector::Dist2D(Me, A.FoodAt)), Kg);
			}
			else if (!A.bGoal)
			{
				const FBFMemory* M = Recall(A, 0, Me);
				if (M && FVector::Dist2D(M->At, Me) > 4000.f && M->Value > 2.f * A.MouthfulKg)
				{
					GoTo(A, M->At);
					A.Thought = FString::Printf(TEXT("nothing to eat here: going back to food it remembers %s away"), *Metres(FVector::Dist2D(Me, M->At)));
				}
				else
				{
					// Eaten out here: the herd moves on to fresh grazing, 150 to 500 m off (somewhere with leaves left on
					// the plants), and makes that its home; the others follow (they keep to the herd). Once a minute at most.
					FBFHerd& Herd = Herds[A.Herd];
					bool bMoved = false;
					if (Now - Herd.MovedAt > 30.f)
					{
						// Its range: this ground eaten out now; a ground it knows that has had longest to grow back (half an
						// hour of the island's time at least, and leaves on it), else somewhere new.
						int32 Known = INDEX_NONE;
						for (int32 p = 0; p < Herd.Patches.Num(); ++p)
						{
							if (FVector::Dist2D(Herd.Patches[p], Herd.Home) < 15000.f)
							{
								Known = p;
							}
						}
						if (Known == INDEX_NONE && Herd.Patches.Num() < 10)
						{
							Herd.Patches.Add(Herd.Home);
							Herd.PatchAt.Add(Now);
						}
						else if (Known != INDEX_NONE)
						{
							Herd.PatchAt[Known] = Now;
						}
						int32 Back = INDEX_NONE;
						for (int32 p = 0; p < Herd.Patches.Num(); ++p)
						{
							FVector PlantAt;
							float Reach = 0.f, PlantKg = 0.f;
							if (Now - Herd.PatchAt[p] > 1800.f && FVector::Dist2D(Herd.Patches[p], Herd.Home) > 15000.f && (Back == INDEX_NONE || Herd.PatchAt[p] < Herd.PatchAt[Back])
								&& Island->FindPlant(Herd.Patches[p], 6000.f, TopZ, PlantAt, Reach, PlantKg) && PlantKg > 4.f * A.MouthfulKg)
							{
								Back = p;
							}
						}
						if (Back != INDEX_NONE)
						{
							UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: the %s herd goes back to grazing it knows, %.0f m off, grown back"), *Herd.Kind,
								FVector::Dist2D(Herd.Home, Herd.Patches[Back]) / 100.f);
							Herd.Home = Herd.Patches[Back];
							Herd.MovedAt = Now;
							++HerdMoves;
							GoTo(A, Herd.Home);
							A.Thought = TEXT("eaten out here: the herd goes back to grazing it knows, grown back now");
							bMoved = true;
						}
						for (int32 Try = 0; Try < 12 && !bMoved; ++Try)
						{
							const float Ang = FMath::FRandRange(0.f, 2.f * PI);
							const FVector Cand = Herd.Home + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * FMath::FRandRange(15000.f, 50000.f);
							FVector PlantAt;
							float Reach = 0.f, PlantKg = 0.f;
							if (Island->Walkable(Cand, D->GetSpecies(), A.WadeCm) && Island->FindPlant(Cand, 6000.f, TopZ, PlantAt, Reach, PlantKg) && PlantKg > 4.f * A.MouthfulKg)
							{
								UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: the %s herd moves on to fresh grazing %.0f m off (%s found it)"), *Herd.Kind,
									FVector::Dist2D(Herd.Home, Cand) / 100.f, *A.Name);
								Herd.Home = Cand;
								Herd.MovedAt = Now;
								++HerdMoves;
								GoTo(A, Cand);
								A.Thought = FString::Printf(TEXT("eaten out here: the herd moves on to fresh grazing %s off"), *Metres(FVector::Dist2D(Me, Cand)));
								bMoved = true;
							}
						}
					}
					if (!bMoved && FVector::Dist2D(Me, Herd.Home) > 6000.f)
					{
						GoTo(A, Herd.Home);          // to where the herd has gone
						A.Thought = TEXT("going to the herd's new grazing");
					}
					else if (!bMoved)
					{
						const float Ang = FMath::FRandRange(0.f, 2.f * PI);
						GoTo(A, Herd.Home + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * FMath::FRandRange(5000.f, 20000.f));
						A.Thought = TEXT("nothing to eat here: searching");
					}
				}
			}
			break;
		}
		const FVector S = Snout(A);
		const float Ahead = FVector::Dist2D(S, Me);       // its mouth's reach ahead of its middle, head down
		if (FVector::Dist2D(S, A.FoodAt) - A.FoodReach <= A.ReachCm + 150.f || FVector::Dist2D(Me, A.FoodAt) - A.FoodReach <= Ahead)
		{
			A.Path.Reset();
			A.bGoal = false;
			{
				// Its mouth to the leaves: at the height in their band nearest where its head is, on the near side of the plant.
				const float FeetZ = float(Me.Z) - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				if (D->GetMouthLowCm() > 0.f && A.FoodHighZ < FeetZ + D->GetMouthLowCm())
				{
					A.bFood = false;          // its leaves are lower than it can get its mouth: another plant
					A.Thought = TEXT("can't get its mouth down to those leaves: looking for others");
					break;
				}
				const float Want = FMath::Clamp(float(S.Z), A.FoodLowZ + 15.f, FMath::Max(A.FoodHighZ - 15.f, A.FoodLowZ + 15.f));
				const FVector Toward = (Me - A.FoodAt).GetSafeNormal2D();
				FVector Spot = FVector(A.FoodAt.X, A.FoodAt.Y, Want) + Toward * (0.6f * A.FoodReach);
				if (A.FoodHighZ < FeetZ + 120.f)
				{
					// Low plants: mouthful by mouthful across the patch (left, middle, right), then a step on into it.
					const FVector Side = FVector::CrossProduct(Toward, FVector::UpVector);
					const int32 k = A.CropN % 6;
					const float Sweep = FMath::Clamp(0.4f * A.FoodReach, 40.f, 110.f);
					Spot += Side * float(k % 3 - 1) * Sweep - Toward * float(k / 3) * FMath::Clamp(0.5f * A.FoodReach, 70.f, 150.f);
				}
				if (!MouthTo(A, Spot, Dt))
				{
					A.Thought = TEXT("reaching for the leaves");
					break;
				}
			}
			A.BiteClock -= Dt;
			if (A.BiteClock <= 0.f)
			{
				A.BiteClock = 1.7f;
				FString What;
				float Left = 0.f;
				const float Took = Island->EatPlants(S, FMath::Max(A.ReachCm + 150.f, Ahead + 100.f), TopZ, A.MouthfulKg, What, Left);
				if (Took <= 0.f)
				{
					A.bFood = false;          // stripped: the next one
					break;
				}
				A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + Took);
				A.EatenKg += Took;
				++A.CropN;
				Remember(A, 0, A.FoodAt, Left, Now);
				if (D->GetIdle() != EBFIdle::Feed)
				{
					D->StartIdle(EBFIdle::Feed);
				}
				A.Thought = FString::Printf(TEXT("cropping %s: %.0f of %.0f kg in its stomach"), *What, A.StomachKg, A.StomachMaxKg);
				if (A.StomachKg >= 0.95f * A.StomachMaxKg)
				{
					++A.Meals;
					Choose(A, EBFAct::Idle, Now, TEXT("full"));
				}
			}
		}
		else if (!A.bGoal)
		{
			// Walk on until its mouth (not its middle) is at the plant: the body stands its snout's reach short of it.
			const FVector Way = (A.FoodAt - Me).GetSafeNormal2D();
			A.Path = { A.FoodAt - Way * FMath::Max(Ahead - 100.f, 0.f) };
			A.PathI = 0;
			A.bGoal = true;
		}
		break;
	}
	case EBFAct::Drink:
	{
		if (A.WaterAt.IsZero())
		{
			FVector Stand, Water;
			if (!Recall(A, 1, Me) && Herds[A.Herd].Waters.Num() > 0)
			{
				FVector Near = Herds[A.Herd].Waters[0];
				for (const FVector& W : Herds[A.Herd].Waters)
				{
					Near = FVector::Dist2D(W, Me) < FVector::Dist2D(Near, Me) ? W : Near;
				}
				Remember(A, 1, Near, 1.f, Now);          // the water the herd knows
			}
			const FBFMemory* M = Recall(A, 1, Me);
			if (M && FVector::Dist2D(M->At, Me) < 60000.f && Island->NearestShore(M->At, 2000.f, Stand, Water))
			{
				A.Thought = FString::Printf(TEXT("thirsty: going to water it remembers %s away"), *Metres(FVector::Dist2D(Me, Stand)));
			}
			else if (Island->NearestShore(Me, 60000.f, Stand, Water))
			{
				// Not water it couldn't get to before: then the nearest from somewhere else round it.
				auto OutOfReach = [&](const FVector& W) { return A.Memory.ContainsByPredicate([&](const FBFMemory& M) { return M.Kind == 3 && FVector::Dist2D(M.At, W) < 8000.f; }); };
				for (int32 k = 0; k < 8 && OutOfReach(Water); ++k)
				{
					FVector S2, W2;
					if (Island->NearestShore(Me + FRotator(0.f, 45.f * k, 0.f).Vector() * 25000.f, 30000.f, S2, W2) && !OutOfReach(W2))
					{
						Stand = S2;
						Water = W2;
					}
				}
				A.Thought = FString::Printf(TEXT("thirsty: going to the nearest fresh water, %s away"), *Metres(FVector::Dist2D(Me, Stand)));
			}
			else
			{
				A.Thought = TEXT("thirsty, and no fresh water within 600 m");
				break;
			}
			A.WaterAt = Water;
			{
				// It stands a neck's length back from the edge, so its mouth (not its middle) is at the water: as far as its
				// snout reached ahead when it last had its mouth down (or, not yet learned, as far as it's ahead now).
				const FVector Way = (Water - Stand).GetSafeNormal2D();
				const float Ahead = A.MouthAheadCm > 0.f ? A.MouthAheadCm : float(FVector::Dist2D(Snout(A), Me));
				const FVector Back = Stand - Way * FMath::Max(Ahead - 150.f, 0.f);
				GoTo(A, Island->Walkable(Back, D->GetSpecies(), A.WadeCm) ? Back : Stand);
			}
			Remember(A, 1, Stand, 1.f, Now);
			{
				TArray<FVector>& Waters = Herds[A.Herd].Waters;
				if (!Waters.ContainsByPredicate([&](const FVector& W) { return FVector::Dist2D(W, Stand) < 5000.f; }) && Waters.Num() < 6)
				{
					Waters.Add(Stand);
				}
			}
			break;
		}
		if (A.bGoal && FVector::Dist2D(Me, A.Goal) >= 350.f + D->GetCapsuleComponent()->GetScaledCapsuleRadius())
		{
			A.Thought = FString::Printf(TEXT("thirsty (%.2f): walking to water, %s to go"), A.Thirst, *Metres(FVector::Dist2D(Me, A.Goal)));
		}
		if (!A.bGoal || FVector::Dist2D(Me, A.Goal) < 350.f + D->GetCapsuleComponent()->GetScaledCapsuleRadius())
		{
			A.Path.Reset();
			A.bGoal = false;
			D->AddMovementInput((A.WaterAt - Me).GetSafeNormal2D(), 0.05f);     // turns to face the water
			{
				float Surface = float(A.WaterAt.Z);
				Island->WaterAt(A.WaterAt + FVector(0.f, 0.f, 10.f), Surface);
				if (!MouthTo(A, FVector(A.WaterAt.X, A.WaterAt.Y, Surface), Dt))     // its mouth down to the water
				{
					A.Thought = TEXT("reaching down to the water");
					break;
				}
			}
			D->SetSipping();
			if (D->GetIdle() != EBFIdle::Feed)
			{
				D->StartIdle(EBFIdle::Feed);
			}
			A.Thirst = FMath::Max(0.f, A.Thirst - Dt / 6.f);
			A.Thought = FString::Printf(TEXT("drinking (thirst %.2f)"), A.Thirst);
			if (A.Thirst <= 0.02f)
			{
				++A.Drinks;
				Choose(A, EBFAct::Idle, Now, TEXT("drank its fill"));
			}
		}
		break;
	}
	case EBFAct::Rest:
	{
		// A small hunter at night sleeps in a den if there's one within 600 m of its pack: to its mouth, in along its floor
		// to the chamber at the back (each a little apart), and to sleep there.
		const bool bNight = Island.IsValid() && Island->Daylight() < 0.35f;
		int32 Den = INDEX_NONE;
		if (bNight && A.bHunter && A.MassKg < 500.f && !A.bFlier && !A.bPlayer)
		{
			float Best = 60000.f;
			for (int32 c = 0; c < Island->NumCaves(); ++c)
			{
				const float Dc = Island->CaveKind(c) == 0 ? FVector::Dist2D(Island->CaveMouth(c), HerdCentre(A.Herd)) : 1e9f;
				if (Dc < Best)
				{
					Best = Dc;
					Den = c;
				}
			}
		}
		if (Den != INDEX_NONE)
		{
			const TArray<FVector>& Fl = Island->CaveFloor(Den);
			const FVector Side = FVector::CrossProduct(Island->CaveOut(Den), FVector::UpVector);
			const int32 Slot = Herds[A.Herd].Members.Find(Index);
			const FVector Bed = Fl.Last() + Island->CaveOut(Den) * (120.f * (Slot / 3)) + Side * (160.f * ((Slot % 3) - 1));
			const bool bInside = Island->CaveAt(Me + FVector(0.f, 0.f, 50.f)) == Den;
			if (FVector::Dist2D(Me, Bed) < 250.f + D->GetCapsuleComponent()->GetScaledCapsuleRadius())
			{
				A.Path.Reset();
				A.bGoal = false;
				A.Thought = FString::Printf(TEXT("asleep in its den (night; tired %.2f)"), A.Tired);
				break;
			}
			// (Out beyond its mouth: there first. Not "is it inside": in the first metres, under a low roof, it isn't yet.)
			if (!bInside && FVector::Dist2D(Me, Fl[0]) > 900.f)
			{
				if (!A.bGoal || FVector::Dist2D(A.Goal, Island->CaveMouth(Den)) > 300.f)
				{
					GoTo(A, Island->CaveMouth(Den));
				}
				A.Thought = FString::Printf(TEXT("night: going to its den, %s off"), *Metres(FVector::Dist2D(Me, Island->CaveMouth(Den))));
				break;
			}
			if (!A.bGoal || FVector::Dist2D(A.Goal, Bed) > 100.f)
			{
				// In along the floor from wherever it is.
				A.Path.Reset();
				int32 From = 0;
				for (int32 i = 0; i < Fl.Num(); ++i)
				{
					From = FVector::DistSquared2D(Fl[i], Island->CaveMouth(Den)) < FVector::DistSquared2D(Me, Island->CaveMouth(Den)) ? i + 1 : From;
				}
				for (int32 i = FMath::Min(From, Fl.Num() - 1); i < Fl.Num() - 1; ++i)
				{
					A.Path.Add(Fl[i]);
				}
				A.Path.Add(Bed);
				A.PathI = 0;
				A.Goal = Bed;
				A.bGoal = true;
			}
			A.Thought = TEXT("night: going into its den");
			break;
		}
		A.Path.Reset();
		A.Thought = Island.IsValid() && Island->Daylight() < 0.3f ? FString::Printf(TEXT("asleep (night; tired %.2f)"), A.Tired)
			: FString::Printf(TEXT("resting (tired %.2f)"), A.Tired);
		break;
	}
	case EBFAct::Flee:
	{
		// Away from the danger, a long way: the way that leads furthest from it that its body can go; again every few
		// seconds while the danger is still about. It runs while it has the wind for it.
		// A youngster runs to its mother (to her far side from the danger) and keeps there.
		if (A.bYoung && Animals.IsValidIndex(A.Mother) && !Animals[A.Mother].bDead && Animals[A.Mother].Body.IsValid())
		{
			const FVector M = Animals[A.Mother].Body->GetActorLocation();
			const FVector Shelter = M + (M - A.ThreatAt).GetSafeNormal2D() * (0.4f * Animals[A.Mother].Body->BodyLengthCm());
			if (!A.bGoal || FVector::Dist2D(A.Goal, Shelter) > 300.f)
			{
				A.Path = { Shelter };
				A.PathI = 0;
				A.Goal = Shelter;
				A.bGoal = true;
			}
			A.Thought = FString::Printf(TEXT("frightened: running to %s (%s)"), *Animals[A.Mother].Name, *A.ThreatHow);
			break;
		}
		{
			// Startled: for a moment it freezes, head up, eyes on the danger (an animal places it before it bolts), unless
			// it's already close.
			const float Startle = 0.4f + 0.6f * FMath::Frac(Index * 0.618f);
			if (Now - A.ActSince < Startle && FVector::Dist2D(Me, A.ThreatAt) > 2500.f)
			{
				if (A.bGoal || A.Path.Num() > 0)
				{
					A.Path.Reset();
					A.bGoal = false;
					++Startles;
				}
				D->SetGaze(A.Threat.IsValid() ? A.Threat->GetActorLocation() : A.ThreatAt, 1.f);
				A.Thought = FString::Printf(TEXT("startled: frozen, staring (%s)"), *A.ThreatHow);
				break;
			}
		}
		if (!A.bGoal || (A.ThreatAgo < 1.f && Now - A.ActSince > 0.f && FMath::Frac(Now / 3.f) < Dt / 3.f))
		{
			const FVector Away = (Me - A.ThreatAt).GetSafeNormal2D();
			const FVector HerdWay = (HerdCentre(A.Herd) - Me).GetSafeNormal2D();
			const FVector Base = (Away + 0.3f * HerdWay).GetSafeNormal2D();
			for (const float Turn : { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f })
			{
				const FVector Dir = Base.RotateAngleAxis(Turn, FVector::UpVector);
				const FVector To = Me + Dir * 25000.f;
				if (Island->Walkable(To, D->GetSpecies(), A.WadeCm))
				{
					GoTo(A, To);
					break;
				}
			}
		}
		A.Thought = FString::Printf(TEXT("fleeing: %s%s"), *A.ThreatHow, D->GetExertion() >= 0.85f ? TEXT(" (winded: walking)") : TEXT(""));
		break;
	}
	case EBFAct::Defend:
	{
		const FVector Centre = HerdCentre(A.Herd);
		const float ToThreat = FVector::Dist2D(Me, A.ThreatAt);
		if (ToThreat < 2000.f && A.Fear > 0.6f)
		{
			// Too close: it charges.
			A.Path = { A.ThreatAt };
			A.PathI = 0;
			A.bGoal = true;
			D->SetRunning(true);
			A.Thought = FString::Printf(TEXT("charging the %s"), A.Threat.IsValid() ? *A.Threat->GetSpecies().Name : TEXT("danger"));
			ABFDino* T = A.Threat.Get();
			A.AttackClock -= Dt;
			if (T && A.AttackClock <= 0.f && FVector::Dist2D(Snout(A), T->GetActorLocation()) < A.ReachCm + T->GetCapsuleComponent()->GetScaledCapsuleRadius() + 100.f)
			{
				A.AttackClock = 1.5f;
				++A.Bites;
				const int32 Victim = IndexOf(T);
				if (Victim != INDEX_NONE)
				{
					Hurt(Victim, HitShare(A.MassKg, A.Weapon, Animals[Victim]), A.Name, D, Now);
				}
				else if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->GetPawn() == T)
				{
					FBFAnimal You;
					You.Armour = 0.1f;
					PlayerHealth -= HitShare(A.MassKg, A.Weapon, You) * 1000.f / FMath::Max(T->GetSpecies().MassKg, 1.f) * FMath::Sqrt(FMath::Max(T->GetSpecies().MassKg, 1.f) / 1000.f);
					PlayerNote = FString::Printf(TEXT("%s hit you: your health %.0f%%"), *A.Name, 100.f * FMath::Max(PlayerHealth, 0.f));
					PlayerNoteUntil = FPlatformTime::Seconds() + 4.0;
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s"), *PlayerNote);
					if (PlayerHealth <= 0.f)
					{
						T->StartRagdoll();
						PlayerNote = FString::Printf(TEXT("%s killed you (K to get up again)"), *A.Name);
						PlayerHealth = 1.f;
					}
				}
			}
		}
		else if (FVector::Dist2D(Me, Centre) > 1.5f * A.Spacing)
		{
			if (!A.bGoal)
			{
				GoTo(A, Centre);
			}
			A.Thought = TEXT("closing up with the herd to face the danger");
		}
		else
		{
			A.Path.Reset();
			A.bGoal = false;
			D->AddMovementInput((A.ThreatAt - Me).GetSafeNormal2D(), 0.06f);
			// A display: horns lowered at it (the Triceratops), the tail club swung (the Ankylosaurus), the head up
			// (the rest); a bellow every few seconds.
			const FVector It = A.Threat.IsValid() ? A.Threat->GetActorLocation() : A.ThreatAt;
			const bool bHorns = A.Base == TEXT("trike");
			D->SetGaze(It, bHorns ? -0.8f : 0.7f);
			if (A.Base == TEXT("ankylo") && D->GetIdle() != EBFIdle::Swish && FMath::Frac(Now / 5.f + Index * 0.27f) < Dt / 5.f)
			{
				D->StartIdle(EBFIdle::Swish);
			}
			if (Now - A.CalledAt > 5.f && FMath::Frac(Now / 4.f + Index * 0.41f) < Dt / 4.f)
			{
				D->Call(1.f);
				A.CalledAt = Now;
			}
			A.Thought = FString::Printf(TEXT("facing it with the herd, %s: %s"), bHorns ? TEXT("horns lowered") : TEXT("head up"), *A.ThreatHow);
		}
		break;
	}
	case EBFAct::Regroup:
		if (!A.bGoal || FMath::Frac(Now / 5.f) < Dt / 5.f)
		{
			const FBFHerd& Hd = Herds[A.Herd];
			const bool bMoving = !A.bHunter && Now - Hd.MovedAt < 90.f && FVector::Dist2D(Me, Hd.Home) > 6000.f;
			FVector To = bMoving ? Hd.Home : HerdCentre(A.Herd);
			if (bMoving)
			{
				// In a line: behind the nearest one of the herd that's ahead of it (nearer the new grazing), a body's
				// length and a gap back, in its tracks; the one in front goes straight there.
				const float MyLeft = FVector::Dist2D(Me, Hd.Home);
				float Best = 1e12f;
				for (const int32 Mi : Hd.Members)
				{
					const FBFAnimal& B = Animals[Mi];
					if (Mi == Index || B.bDead || !B.Body.IsValid())
					{
						continue;
					}
					const FVector BP = B.Body->GetActorLocation();
					const float Left = FVector::Dist2D(BP, Hd.Home);
					const float Gap = FVector::Dist2D(BP, Me);
					if (Left < MyLeft - 200.f && Gap < Best)
					{
						Best = Gap;
						To = BP - (Hd.Home - BP).GetSafeNormal2D() * (B.Body->BodyLengthCm() + 0.5f * A.Spacing);
					}
				}
			}
			GoTo(A, To);
		}
		break;
	case EBFAct::Wander:
		if (A.bPlay && Animals.IsValidIndex(A.PlayWith) && Animals[A.PlayWith].Body.IsValid())
		{
			// A chase: after the other one, at a run, for a few seconds at a time.
			const FVector Other = Animals[A.PlayWith].Body->GetActorLocation();
			if (!A.bGoal || FMath::Frac(Now / 2.f) < Dt / 2.f)
			{
				A.Path = { Other + (Other - Me).GetSafeNormal2D() * 300.f };
				A.PathI = 0;
				A.bGoal = true;
			}
			D->SetRunning(FMath::Frac(Now / 8.f) < 0.5f);
			A.Thought = FString::Printf(TEXT("playing: chasing %s"), *Animals[A.PlayWith].Name);
			break;
		}
		if (A.bHunter && A.MassKg < 500.f && !A.bGoal)
		{
			// D: a small hunter shadows a big one that's hunting or feeding, for the scraps (from downwind, 60-100 m off).
			for (const FBFAnimal& B : Animals)
			{
				if (B.bHunter && !B.bDead && B.Body.IsValid() && B.MassKg > 2000.f && (B.Act == EBFAct::Hunt || B.Act == EBFAct::Feast)
					&& FVector::Dist2D(B.Body->GetActorLocation(), Me) < 100000.f)
				{
					if (GoTo(A, B.Body->GetActorLocation() + Wind * FMath::FRandRange(6000.f, 10000.f)))
					{
						A.Thought = FString::Printf(TEXT("shadowing %s for scraps"), *B.Name);
					}
					break;
				}
			}
		}
		if (!A.bGoal)
		{
			for (int32 Try = 0; Try < 6; ++Try)
			{
				const float Ang = FMath::FRandRange(0.f, 2.f * PI);
				const FVector To = HerdCentre(A.Herd) + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * FMath::FRandRange(2000.f, 6000.f);
				if (Island->Walkable(To, D->GetSpecies(), 0.f))
				{
					GoTo(A, To);
					break;
				}
			}
		}
		break;
	case EBFAct::Court:
	case EBFAct::Nest:
		DoBreed(A, Index, Dt, Now);
		break;
	case EBFAct::Alert:
		A.Path.Reset();
		if (A.bWatch)
		{
			if (D->GetIdle() != EBFIdle::Look && Now - A.ActSince < 0.5f)
			{
				D->StartIdle(EBFIdle::Look);
			}
		}
		else
		{
			// Frozen, head high, staring at it.
			D->SetGaze(A.Threat.IsValid() ? A.Threat->GetActorLocation() : A.ThreatAt, 1.f);
		}
		break;
	default:
		A.Path.Reset();
		break;
	}
}

void ABFLife::Steer(FBFAnimal& A, int32 Index, float Dt)
{
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	const float Step = FVector::Dist2D(Me, A.LastStep);
	A.WalkedCm += Step < 2000.f ? Step : 0.f;         // (not a jump: put somewhere)
	A.LastStep = Me;
	if (A.PathI >= A.Path.Num())
	{
		A.LastPos = Me;
		A.bGoal = A.bGoal && A.Path.Num() > 0 ? false : A.bGoal;
		return;
	}
	const FVector Target = A.Path[A.PathI];
	const float Arrive = (A.Act == EBFAct::Graze && A.bFood ? 120.f : 300.f) + 0.5f * D->GetCapsuleComponent()->GetScaledCapsuleRadius();
	if (FVector::Dist2D(Me, Target) < Arrive)
	{
		if (++A.PathI >= A.Path.Num())
		{
			A.bGoal = false;
		}
		A.LastPos = Me;
		return;
	}
	FVector Dir = (Target - Me).GetSafeNormal2D();
	// Its own room in the herd: away from any of the others closer than its spacing.
	for (const int32 M : Herds[A.Herd].Members)
	{
		const FBFAnimal& B = Animals[M];
		if (M == Index || !B.Body.IsValid())
		{
			continue;
		}
		const FVector Off = Me - B.Body->GetActorLocation();
		const float Dist = Off.Size2D();
		const float Room = A.Spacing * (A.Losses > A.Wins + 1 ? 1.3f : 1.f);      // the low-ranking keep to the edge
		if (Dist < Room && Dist > 1.f)
		{
			Dir += Off.GetSafeNormal2D() * 1.2f * (1.f - Dist / Room);
		}
	}
	// Stuck (a trunk, a bank): a step to one side, then on; four times running, it looks for another way.
	A.StuckClock += Dt;
	if (A.StuckClock > 2.f)
	{
		if (FVector::Dist2D(Me, A.LastPos) < 60.f)
		{
			A.SideStep = 1.2f;
			if (++A.Stucks >= 4 && A.Path.Num() > 0)
			{
				const FVector Final = A.Path.Last();
				GoTo(A, Final);
			}
		}
		else
		{
			A.Stucks = 0;
		}
		A.StuckClock = 0.f;
		A.LastPos = Me;
	}
	if (A.SideStep > 0.f)
	{
		A.SideStep -= Dt;
		Dir = Dir.RotateAngleAxis((A.Stucks % 2 == 0 ? 75.f : -75.f), FVector::UpVector);
	}
	const float Pace = A.Act == EBFAct::Wander ? 0.55f : A.Act == EBFAct::Graze ? 0.7f : A.Act == EBFAct::Hunt && A.HuntPhase == 0 && A.bCreeping ? 0.6f : A.Act == EBFAct::Hunt && A.HuntPhase == 0 && A.bFollowing ? 0.45f : 1.f;
	D->AddMovementInput(Dir.GetSafeNormal2D(), Pace);
}

void ABFLife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Island.IsValid() || Animals.Num() == 0)
	{
		return;
	}
	const double TickStart = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		const double Took = FPlatformTime::Seconds() - TickStart;
		TickSecs += Took;
		TickWorst = FMath::Max(TickWorst, Took);
		++TickFrames;
	};
	const float Now = GetWorld()->GetTimeSeconds();
	TickYoung(Now);
	AwaitClock -= DeltaSeconds;
	if (AwaitClock <= 0.f)
	{
		AwaitClock = 0.5f;
		TickAwaiting();
	}
	AliveClock -= DeltaSeconds;
	if (AliveClock <= 0.f)
	{
		AliveClock = 1.f;
		AliveNow = NumAlive();
		TickBalance(1.f, Now);
		ReadyFemales.Reset();
		for (int32 j = 0; j < Animals.Num(); ++j)
		{
			if (IsReady(Animals[j]))
			{
				ReadyFemales.Add(j);
			}
		}
	}
	TickNests(DeltaSeconds, Now);
	TickCarcasses(DeltaSeconds, Now);
	if (Shoals.Num() > 0)
	{
		DrawFish(Now);
	}
	HuntersClock -= DeltaSeconds;
	if (HuntersClock <= 0.f)
	{
		// The meat eaters about: any creature whose head doesn't eat plants (you, your companion, later hunters).
		HuntersClock = 1.f;
		Hunters.Reset();
		for (TActorIterator<ABFDino> It(GetWorld()); It; ++It)
		{
			if (It->GetSpecies().Feed == EBFFeed::None && !It->IsRagdoll())
			{
				Hunters.Add(*It);
			}
		}
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector Cam = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FVector You = PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : Cam;
	const FVector CamFwd = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
	// The one nearest the middle of the view says its whole thought; the rest just who they are and what they're doing.
	int32 Central = INDEX_NONE;
	float BestDot = 0.9f;
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		if (Animals[i].Body.IsValid() && FVector::Dist(Cam, Animals[i].Body->GetActorLocation()) < 12000.f)
		{
			const float Dot = FVector::DotProduct(CamFwd, (Animals[i].Body->GetActorLocation() - Cam).GetSafeNormal());
			if (Dot > BestDot)
			{
				BestDot = Dot;
				Central = i;
			}
		}
	}
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		FBFAnimal& A = Animals[i];
		if (A.bDead)
		{
			if (A.Label)
			{
				A.Label->SetVisibility(false);
			}
			// The body goes ten island minutes after death (its meat stays for the scavengers where it lay); every one
			// that lay on had the video memory exhausted after an hour and a half of generations.
			if (A.Body.IsValid())
			{
				A.DownFor += DeltaSeconds;
				if (A.DownFor > 600.f)
				{
					for (FBFCarcass& K : Carcasses)
					{
						if (K.Animal == i && K.BonesAt < 0.f)
						{
							CarcassBones(K, *A.Body);          // its bones stay where it lay
						}
					}
					A.Body->Destroy();
					A.Body = nullptr;
					A.Label = nullptr;
				}
			}
			continue;
		}
		if (A.bPlayer)
		{
			if (A.Body.IsValid() && !A.Body->IsRagdoll() && !bFrozen)
			{
				Grow(A, i, DeltaSeconds, Now);
			}
			if (A.Label)
			{
				A.Label->SetVisibility(false);
			}
			continue;          // you: your own mind
		}
		if (A.Body.IsValid() && A.Body->IsRagdoll())
		{
			A.DownFor += DeltaSeconds;
			if (A.DownFor > 5.f)
			{
				ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
				FVector At = A.Body->GetActorLocation();
				const float Yaw = A.Body->GetActorRotation().Yaw;
				{
					const float Half = 0.4f * A.Body->BodyLengthCm();
					const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
					auto Rough = [&](const FVector& P)
					{
						float Lo = 1e9f, Hi = -1e9f;
						for (const FVector& O : { Fwd * Half, -Fwd * Half, FVector::CrossProduct(Fwd, FVector::UpVector) * 0.5f * Half, -FVector::CrossProduct(Fwd, FVector::UpVector) * 0.5f * Half, FVector::ZeroVector })
						{
							float Z = 0.f;
							if (!Island->GroundAt(FVector2D(P + O), Z))
							{
								return 1e9f;
							}
							Lo = FMath::Min(Lo, Z);
							Hi = FMath::Max(Hi, Z);
						}
						float W = 0.f;
						return Hi - Lo + (Island->WaterAt(FVector(P.X, P.Y, Lo + 10.f), W) ? 1e6f : 0.f);
					};
					FVector Best = At;
					float BestR = Rough(At);
					for (int32 k = 0; k < 8; ++k)
					{
						const FVector P = At + FRotator(0.f, 45.f * k, 0.f).Vector() * 400.f;
						const float R = Rough(P);
						if (R < BestR)
						{
							BestR = R;
							Best = P;
						}
					}
					At = Best;
				}
				float Ground = At.Z;
				Island->GroundAt(FVector2D(At), Ground);
				UTextRenderComponent* OldLabel = A.Label;
				A.Body->Destroy();
				A.Body = GM ? GM->SpawnCompanion(A.SpeciesIndex, FVector(At.X, At.Y, Ground), Yaw) : nullptr;
				A.DownFor = 0.f;
				++A.Falls;
				if (A.Body.IsValid())
				{
					A.Body->SetIdleBehaviours(true);
					ApplyGenes(A);
					UTextRenderComponent* T = NewObject<UTextRenderComponent>(A.Body.Get());
					T->SetupAttachment(A.Body->GetRootComponent());
					T->SetRelativeLocation(OldLabel ? OldLabel->GetRelativeLocation() : FVector(0.f, 0.f, 400.f));
					T->SetHorizontalAlignment(EHTA_Center);
					T->SetWorldSize(OldLabel ? OldLabel->WorldSize : 60.f);
					T->SetTextRenderColor(FColor(255, 250, 230));
					T->SetCastShadow(false);
					T->RegisterComponent();
					A.Label = T;
					A.Path.Reset();
					A.bGoal = false;
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s got up again"), *A.Name);
				}
			}
			continue;
		}
		if (!A.Body.IsValid())
		{
			continue;
		}
		if (bFrozen)
		{
			continue;
		}
		Grow(A, i, DeltaSeconds, Now);
		if (!A.Body.IsValid())
		{
			continue;
		}
		// Step 8: far from you, its body put away and a simpler life; back in full as you come near.
		if (FarCm > 0.f && !A.Body->IsRagdoll())
		{
			const float FromYou = FVector::Dist2D(A.Body->GetActorLocation(), You);
			const bool bFar = FromYou > (A.bDormant ? WakeCm : FarCm);
			if (bFar != A.bDormant || (A.bDormant && !A.Body->IsHidden()))
			{
				SetDormant(A, bFar);          // (and a new body grown while far is put away too)
			}
		}
		else if (A.bDormant)
		{
			SetDormant(A, false);
		}
		if (A.bDormant)
		{
			TickFar(A, i, DeltaSeconds, Now);
			Toll(A, i, DeltaSeconds, Now);
			continue;
		}
		if (A.bFlier)
		{
			FlierTick(A, i, DeltaSeconds, Now);
			Toll(A, i, DeltaSeconds, Now);
			continue;
		}
		A.ThinkIn -= DeltaSeconds;
		if (A.ThinkIn <= 0.f)
		{
			A.ThinkIn = 0.25f;
			Sense(A, Now);
			if (A.bHunter)
			{
				HunterThink(A, i, Now);
			}
			else
			{
				Think(A, i, Now);
			}
		}
		if (A.bHunter)
		{
			HunterDo(A, i, DeltaSeconds, Now);
		}
		else
		{
			Do(A, i, DeltaSeconds, Now);
		}
		Steer(A, i, DeltaSeconds);
		Toll(A, i, DeltaSeconds, Now);
		if (A.bDead)
		{
			continue;
		}
		{
			// Its body's detail: whole near you and in view; further off or out of view its pose every few frames.
			const FVector To = A.Body->GetActorLocation() - Cam;
			const float Far = To.Size();
			const bool bInView = FVector::DotProduct(CamFwd, To.GetSafeNormal()) > 0.35f || Far < 3000.f;
			A.Body->DetailEvery = !bInView ? 8 : Far < 10000.f ? 1 : Far < 30000.f ? 2 : Far < 80000.f ? 4 : 8;
		}
		if (A.Label)
		{
			const bool bShow = bShowThoughts && (FVector::Dist(Cam, A.Body->GetActorLocation()) < (A.Stage < 4 ? 4000.f : 12000.f) || i == Central);
			A.Label->SetVisibility(bShow);
			if (bShow)
			{
				A.Label->SetText(FText::FromString(i == Central ? FString::Printf(TEXT("%s: %s\n%s"), *A.Name, ActName(A.Act), *A.Thought)
					: FString::Printf(TEXT("%s: %s"), *A.Name, ActName(A.Act))));
				A.Label->SetRelativeLocation(FVector(0.f, 0.f, 0.5f * A.Body->GetSpecies().Bounds.GetSize().Z + 120.f + 70.f * (i % 3)));
				A.Label->SetWorldRotation(FRotator(0.f, (Cam - A.Label->GetComponentLocation()).Rotation().Yaw, 0.f));
			}
		}
	}
}

FString ABFLife::Report() const
{
	FString Out;
	for (const FBFAnimal& A : Animals)
	{
		if (!A.Body.IsValid())
		{
			continue;
		}
		if (A.Stage < 4)
		{
			Out += FString::Printf(TEXT("[%s, %.1f h old, %.0f kg] "), *StageName(A.Stage), A.AgeS / 3600.f, A.MassKg);
		}
		Out += FString::Printf(TEXT("[%s, gen %d, %s] "), A.bFemale ? TEXT("F") : TEXT("M"), A.Generation, *GenesText(A.Genes));
		Out += FString::Printf(TEXT("%s: %s (hunger %.2f, thirst %.2f, tired %.2f, fear %.2f); %d choices, ate %.0f kg (%d full meals), drank %d times, fled %d times, %d paths, walked %.0f m, fell %d times, health %.0f%%, %d kills; \"%s\"\n"),
			*A.Name, A.bDead ? TEXT("DEAD") : ActName(A.Act), 1.f - A.StomachKg / A.StomachMaxKg, A.Thirst, A.Tired, A.Fear, A.Decisions, A.EatenKg, A.Meals, A.Drinks, A.Flights, A.Paths,
			A.WalkedCm / 100.f, A.Falls, 100.f * FMath::Max(A.Health, 0.f), A.Kills, *A.Thought);
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------- hunting (step 4)

float ABFLife::HitShare(float AttackerKg, float Weapon, const FBFAnimal& Victim)
{
	// A bite or a blow takes a share of the victim's health: more the heavier the attacker against it (by the square
	// root: weight behind the jaws or the horns), times the weapon, less its armour.
	return 0.15f * FMath::Sqrt(AttackerKg / FMath::Max(Victim.MassKg, 1.f)) * Weapon * (1.f - Victim.Armour);
}

int32 ABFLife::IndexOf(const ABFDino* Body) const
{
	// (Asked for every hunter by every animal's senses: a scan of them all each time grew with the cube of their numbers.)
	if (!Body)
	{
		return INDEX_NONE;
	}
	if (const int32* Hit = IndexCache.Find(Body))
	{
		if (Animals.IsValidIndex(*Hit) && Animals[*Hit].Body.Get() == Body && !Animals[*Hit].bPlayer)
		{
			return *Hit;
		}
	}
	// (you're not found this way: fights with you go through your own health, as before)
	const int32 I = Animals.IndexOfByPredicate([&](const FBFAnimal& A) { return A.Body.Get() == Body && !A.bPlayer; });
	IndexCache.Add(Body, I);
	return I;
}

float ABFLife::PackKg(const FBFAnimal& A) const
{
	float Sum = 0.f;
	for (const int32 M : Herds[A.Herd].Members)
	{
		const FBFAnimal& B = Animals[M];
		if (B.Body.IsValid() && !B.bDead && FVector::Dist2D(B.Body->GetActorLocation(), A.Body->GetActorLocation()) < 6000.f)
		{
			Sum += B.MassKg;
		}
	}
	return FMath::Max(Sum, A.MassKg);
}

int32 ABFLife::SmellCarcass(const FBFAnimal& A) const
{
	// A kill it knows of: near (150 m), or smelled on the wind from upwind of it, from further than it smells a live animal.
	const FVector Me = A.Body->GetActorLocation();
	int32 Best = INDEX_NONE;
	float BestD = 1e9f;
	for (int32 i = 0; i < Carcasses.Num(); ++i)
	{
		const float D = FVector::Dist2D(Carcasses[i].At, Me);
		const bool bUpwind = FVector::DotProduct((Me - Carcasses[i].At).GetSafeNormal2D(), Wind) > 0.5f;
		const bool bNest = Nests.IsValidIndex(Carcasses[i].Nest);
		if (bNest && (Nests[Carcasses[i].Nest].Herd == A.Herd || Nests[Carcasses[i].Nest].Eggs <= 0))
		{
			continue;              // not its own pack's eggs
		}
		if (Carcasses[i].MeatKg > (bNest ? 0.01f : 1.f) && (D < 15000.f || (bUpwind && D < 1.5f * A.SmellCm)) && D < BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	return Best;
}

const ABFDino* ABFLife::BiggerAt(const FBFAnimal& A, const FVector& At, float RadiusCm) const
{
	for (const TWeakObjectPtr<ABFDino>& H : Hunters)
	{
		const int32 HI = IndexOf(H.Get());
		if (H.IsValid() && H.Get() != A.Body.Get() && (HI == INDEX_NONE || Animals[HI].Herd != A.Herd)
			&& H->GetSpecies().MassKg > 2.f * PackKg(A) && FVector::Dist2D(H->GetActorLocation(), At) < RadiusCm)
		{
			return H.Get();
		}
	}
	return nullptr;
}

int32 ABFLife::NearestCarcass(const FVector& At, float RadiusCm) const
{
	int32 Best = INDEX_NONE;
	float BestD = RadiusCm;
	for (int32 i = 0; i < Carcasses.Num(); ++i)
	{
		const float D = FVector::Dist2D(Carcasses[i].At, At);
		if (Carcasses[i].MeatKg > 1.f && D < BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	return Best;
}

void ABFLife::Hurt(int32 Victim, float Share, const FString& By, ABFDino* Attacker, float Now, bool bScare)
{
	FBFAnimal& V = Animals[Victim];
	if (V.bDead || !V.Body.IsValid())
	{
		return;
	}
	V.Health -= Share;
	++V.HitsTaken;
	V.Body->SetLimp(FMath::Clamp((0.75f - V.Health) / 0.5f, 0.f, 1.f));          // badly bitten, it limps till it heals
	if (Attacker && bScare)
	{
		V.Body->AddWound(Attacker->SnoutNow(), FMath::Clamp(12.f + 60.f * Share, 12.f, 45.f) * FMath::Sqrt(FMath::Max(V.Body->BodyLengthCm() / 500.f, 0.2f)));
	}
	if (bScare)
	{
		V.Fear = 1.f;
		V.Threat = Attacker;
		V.ThreatAt = Attacker ? Attacker->GetActorLocation() : V.Body->GetActorLocation();
		V.ThreatAgo = 0.f;
		V.ThreatHow = FString::Printf(TEXT("hurt by %s (health %.0f%%)"), *By, 100.f * FMath::Max(V.Health, 0.f));
	}
	else
	{
		V.Health = FMath::Max(V.Health, 0.05f);        // (a shove never kills)
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s hurt %s: %.0f%% of its health, %.0f%% left"), *By, *V.Name, 100.f * Share, 100.f * FMath::Max(V.Health, 0.f));
	if (V.Health <= 0.f)
	{
		V.bDead = true;
		V.Body->StartRagdoll();
		Carcasses.Add({ V.Body->GetActorLocation(), 0.6f * V.MassKg, Now, V.Name });
		DressCarcass(Carcasses.Num() - 1, Victim);
		const int32 Killer = IndexOf(Attacker);
		if (Killer != INDEX_NONE)
		{
			++Animals[Killer].Kills;
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s killed %s (%.0f kg of meat)"), *By, *V.Name, 0.6f * V.MassKg);
	}
}

int32 ABFLife::ChoosePrey(FBFAnimal& A, int32 Index, float Now, FString& Why)
{
	// A pack hunts together: if one of it has prey, so does it.
	for (const int32 M : Herds[A.Herd].Members)
	{
		const FBFAnimal& B = Animals[M];
		if (M != Index && B.Body.IsValid() && !B.bDead && B.Act == EBFAct::Hunt && Animals.IsValidIndex(B.Prey) && !Animals[B.Prey].bDead)
		{
			Why = FString::Printf(TEXT("joining %s on %s"), *B.Name, *Animals[B.Prey].Name);
			return B.Prey;
		}
	}
	// What it knows of (seen, or smelled: the prey upwind of it), weighed: the meat, whether it can catch it (its speed
	// against the prey's, more if the prey is winded or hurt), what it would have to fight (the prey's weapon and
	// weight, or its whole herd's if that would stand together), and how far.
	const FVector Me = A.Body->GetActorLocation();
	const float Pack = PackKg(A);
	int32 Count = 0;
	for (const int32 M : Herds[A.Herd].Members)
	{
		Count += Animals[M].Body.IsValid() && !Animals[M].bDead ? 1 : 0;
	}
	int32 Best = INDEX_NONE;
	float BestScore = 0.f;
	for (int32 j = 0; j < Animals.Num(); ++j)
	{
		const FBFAnimal& P = Animals[j];
		if (P.bHunter || P.bDead || !P.Body.IsValid() || P.bDormant || P.bPlayer || (A.GaveUp.Contains(j) && Now < A.GaveUp[j]))
		{
			continue;
		}
		const FVector There = P.Body->GetActorLocation();
		const float Dist = FVector::Dist2D(Me, There);
		const bool bSmelled = Dist < A.SmellCm && FVector::DotProduct((Me - There).GetSafeNormal2D(), Wind) > 0.55f;
		if (Dist > A.SightCm && !bSmelled)
		{
			continue;
		}
		if (j == A.Prey)
		{
			A.PreyKnownAt = Now;              // still in its senses: where it is now
			A.PreyLastAt = There;
		}
		const float PerBite = HitShare(A.MassKg, A.Weapon, P) * Count;
		if (PerBite < 0.02f)
		{
			continue;              // its teeth would barely scratch it
		}
		const float HerdKg = HerdMassNear(P, 4000.f);
		const bool bDefended = HerdKg >= 2.5f * Pack;
		if (bDefended && A.StomachKg > 0.1f * A.StomachMaxKg)
		{
			continue;          // a herd that would stand together: not worth the horns unless starving
		}
		const float Fight = (bDefended ? HerdKg : P.MassKg) * P.Weapon * (1.f + P.Armour) / Pack;
		const float Catch = FMath::Clamp(A.FastCm / FMath::Max(P.FastCm * (1.f - 0.4f * P.Body->GetExertion()), 1.f), 0.3f, 2.f) * (1.f + 1.5f * (1.f - P.Health));
		// The easy ones first: the young, the old, the hurt (Catch), and stragglers away from their herd.
		const bool bStraggler = FVector::Dist2D(There, HerdCentre(P.Herd)) > 4.f * P.Spacing;
		const float Easy = bDefended ? 1.f : (P.Stage < 4 ? 1.6f : 1.f) * (P.Stage >= 4 && Lifespan > 0.f && P.AgeS > 0.8f * Lifespan ? 1.5f : 1.f) * (bStraggler ? 1.5f : 1.f);
		const float Score = FMath::Min(0.6f * P.MassKg, 3.f * A.StomachMaxKg) * Catch * Easy / (1.f + 2.f * Fight) / (1.f + Dist / 20000.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = j;
			Why = FString::Printf(TEXT("picked %s: %.1f t, %s off (%s), %s, it runs %.1f m/s to its %.1f"), *P.Name, P.MassKg / 1000.f, *Metres(Dist),
				bSmelled && Dist > A.SightCm ? TEXT("smelled") : TEXT("seen"), bDefended ? TEXT("its herd would stand together") : TEXT("its herd wouldn't hold it off"),
				P.FastCm / 100.f, A.FastCm / 100.f);
		}
	}
	return Best;
}

void ABFLife::HunterThink(FBFAnimal& A, int32 Index, float Now)
{
	const FVector Me = A.Body->GetActorLocation();
	const float Hunger = 1.f - A.StomachKg / A.StomachMaxKg;
	const int32 Carcass = SmellCarcass(A);
	// Its own danger: hurt (by horns or a club), or a bigger meat eater about (raptors keep out of a T. rex's way).
	const bool bBigger = A.Threat.IsValid() && A.Threat->GetSpecies().MassKg > 2.f * PackKg(A) && A.ThreatAgo < 4.f
		&& FVector::Dist2D(A.Threat->GetActorLocation(), Me) < 3000.f     // only when it's close: a scavenger waits nearby
		&& !(IndexOf(A.Threat.Get()) != INDEX_NONE && Animals[IndexOf(A.Threat.Get())].Act == EBFAct::Rest);   // and not while it dozes
	float Score[int32(EBFAct::Count)] = {};
	int32 Defenders = 0;
	for (const FBFAnimal& B : Animals)
	{
		Defenders += !B.bDead && !B.bHunter && B.Body.IsValid() && B.Act == EBFAct::Defend && FVector::Dist2D(B.Body->GetActorLocation(), Me) < 3000.f ? 1 : 0;
	}
	bool bCharged = false;
	for (const FBFAnimal& B : Animals)
	{
		if (!B.bDead && !B.bHunter && B.Body.IsValid() && B.Act == EBFAct::Defend && B.Threat.Get() == A.Body.Get() && B.MassKg > 2.f * PackKg(A)
			&& FVector::Dist2D(B.Body->GetActorLocation(), Me) < 4000.f)
		{
			bCharged = true;
			A.ThreatAt = B.Body->GetActorLocation();       // away from it
		}
	}
	const bool bGored = (A.ThreatAgo < 6.f && (A.Health < 0.75f || Defenders >= 2)) || bCharged;
	Score[int32(EBFAct::Retreat)] = FMath::Max(bGored ? 2.2f : A.Health < 0.45f && A.ThreatAgo < 6.f ? 2.f * (1.f - A.Health) : 0.f, bBigger ? 1.6f * A.Fear : 0.f);
	Score[int32(EBFAct::Feast)] = Carcass != INDEX_NONE && !bGored && (Hunger > 0.35f || (A.Act == EBFAct::Feast && Hunger > 0.05f)) ? Hunger * 1.4f + 0.3f : 0.f;   // gorged, it lies up a while
	Score[int32(EBFAct::Hunt)] = Hunger > 0.3f ? Hunger * 1.2f : 0.f;
	Score[int32(EBFAct::Drink)] = A.Thirst > 0.2f ? FMath::Pow(A.Thirst, 1.2f) * 1.45f : 0.f;
	Score[int32(EBFAct::Rest)] = A.Tired > 0.3f ? FMath::Pow(A.Tired, 1.5f) * 1.15f : 0.f;
	// Night: a T. rex sleeps unless it's starving; small hunters less so (they hunt by smell in the dark).
	const float Night = Island.IsValid() ? 1.f - Island->Daylight() : 0.f;
	Score[int32(EBFAct::Rest)] += (A.MassKg > 1000.f ? 0.6f : 0.25f) * Night * (Hunger < 0.8f ? 1.f : 0.3f);
	{
		// The hunters' day: raptors hunt most at dawn, dusk and by night; a full belly lies up and sleeps it off (the
		// bigger the meal the longer: till it's down to 70%); a rest seen through.
		const float Hr = Island.IsValid() ? Island->GetHours() : 12.f;
		if (A.MassKg < 500.f && (Hr < 8.f || Hr >= 17.f))
		{
			Score[int32(EBFAct::Hunt)] *= 1.25f;
		}
		if (A.StomachKg > 0.7f * A.StomachMaxKg && A.Fear < 0.3f)
		{
			Score[int32(EBFAct::Rest)] += 0.8f;
		}
		if (A.Act == EBFAct::Rest && A.Tired > 0.05f && A.Fear < 0.3f)
		{
			Score[int32(EBFAct::Rest)] += 0.3f;
		}
	}
	const float FromPack = FVector::Dist2D(Me, HerdCentre(A.Herd));
	Score[int32(EBFAct::Regroup)] = Herds[A.Herd].Members.Num() > 1 ? FMath::Clamp((FromPack - 4.f * A.Spacing) / (6.f * A.Spacing), 0.f, 1.f) * 0.9f : 0.f;
	if (A.Act == EBFAct::Feast && Carcass != INDEX_NONE && Hunger > 0.05f)
	{
		Score[int32(EBFAct::Regroup)] *= 0.25f;          // a hunter doesn't leave meat to find its pack: they come to the kill
	}
	if (A.Stage <= 1)
	{
		Score[int32(EBFAct::Hunt)] = 0.f;        // a hatchling feeds from kills, its mother's or any it finds
	}
	if (A.bYoung && Animals.IsValidIndex(A.Mother) && !Animals[A.Mother].bDead && Animals[A.Mother].Body.IsValid())
	{
		const float FromMother = FVector::Dist2D(Me, Animals[A.Mother].Body->GetActorLocation());
		Score[int32(EBFAct::Regroup)] = FMath::Max(Score[int32(EBFAct::Regroup)], FMath::Clamp((FromMother - 2.f * A.Spacing) / (4.f * A.Spacing), 0.f, 1.f) * 1.6f);
	}
	Score[int32(EBFAct::Wander)] = 0.12f;
	BreedScores(A, Index, Now, Score);
	Score[int32(A.Act)] += 0.15f + (Now - A.ActSince < 4.f ? 0.25f : 0.f);
	if (A.Act == EBFAct::Drink && (A.bGoal || !A.WaterAt.IsZero()) && !(A.bGoal && Now - A.ProgressAt > 40.f))
	{
		Score[int32(EBFAct::Drink)] += 0.45f;       // (it sees the walk to water through, for a while)
	}
	int32 Best = 0;
	for (int32 i = 1; i < int32(EBFAct::Count); ++i)
	{
		Best = Score[i] > Score[Best] ? i : Best;
	}
	const EBFAct Act = EBFAct(Best);
	if (Act == A.Act)
	{
		return;
	}
	FString Why;
	switch (Act)
	{
	case EBFAct::Hunt: Why = FString::Printf(TEXT("hungry (%.0f of %.0f kg in its stomach): hunting"), A.StomachKg, A.StomachMaxKg); break;
	case EBFAct::Feast: Why = FString::Printf(TEXT("hungry, and there's a kill %s away"), *Metres(FVector::Dist2D(Me, Carcasses[Carcass].At))); break;
	case EBFAct::Retreat: Why = bBigger ? FString::Printf(TEXT("a %s about: keeping out of its way"), *A.Threat->GetSpecies().Name)
		: FString::Printf(TEXT("hurt (health %.0f%%): backing off"), 100.f * A.Health); break;
	case EBFAct::Drink: Why = FString::Printf(TEXT("thirsty (%.2f)"), A.Thirst); break;
	case EBFAct::Rest: Why = FString::Printf(TEXT("tired (%.2f)"), A.Tired); break;
	case EBFAct::Regroup: Why = FString::Printf(TEXT("%s from the pack"), *Metres(FromPack)); break;
	case EBFAct::Court: case EBFAct::Nest: Why = BreedWhy; break;
	default: Why = TEXT("nothing pressing: roaming its range"); break;
	}
	A.Prey = INDEX_NONE;
	A.bFollowing = false;
	A.HuntPhase = 0;
	Choose(A, Act, Now, Why);
}

void ABFLife::HunterDo(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	const float Rate = NeedRate * Dt;
	A.StomachKg = FMath::Max(0.f, A.StomachKg - A.DigestKgPerS * Rate);
	A.Thirst = FMath::Min(1.f, A.Thirst + Rate / 1200.f);
	A.Tired = A.Act == EBFAct::Rest ? FMath::Max(0.f, A.Tired - Rate / 45.f) : FMath::Min(1.f, A.Tired + Rate * (1.f / 1200.f + 0.01f * FMath::Max(D->GetExertion() - 0.3f, 0.f)));
	A.ThreatAgo += Dt;
	D->SetLying(A.Act == EBFAct::Rest && Now - A.ActSince > 4.f && !A.bGoal && A.Path.Num() == 0 && !A.bFlier,
		!Island.IsValid() || Island->Daylight() < 0.4f || A.Tired > 0.6f);          // lying up, as the plant eaters do
	if (A.ThreatAgo > 3.f)
	{
		A.Fear = FMath::Max(0.f, A.Fear - 0.06f * Dt);
	}
	D->SetRunning((A.Act == EBFAct::Hunt && (A.HuntPhase == 1 || A.bFollowing)) || A.Act == EBFAct::Retreat);
	switch (A.Act)
	{
	case EBFAct::Hunt:
	{
		A.HuntClock -= Dt;
		if (!Animals.IsValidIndex(A.Prey) || Animals[A.Prey].bDead || !Animals[A.Prey].Body.IsValid() || (A.HuntPhase == 0 && A.HuntClock <= 0.f))
		{
			FString Why;
			const int32 Was = A.Prey;
			A.Prey = ChoosePrey(A, Index, Now, Why);
			A.HuntClock = 6.f;
			// Out of its senses for now, but it remembers its quarry and where it last had it, for 40 s.
			if (A.Prey == INDEX_NONE && Animals.IsValidIndex(Was) && !Animals[Was].bDead && Now - A.PreyKnownAt < 40.f)
			{
				A.Prey = Was;
				Why = FString::Printf(TEXT("lost %s from its senses: going to where it last had it"), *Animals[Was].Name);
			}
			else if (A.Prey != INDEX_NONE && A.Prey != Was)
			{
				A.PreyKnownAt = Now;
				A.PreyLastAt = Animals[A.Prey].Body->GetActorLocation();
			}
			if (A.Prey == INDEX_NONE)
			{
				A.HuntPhase = 0;
				// Nothing sensed: it knows its range, where the herds graze; it makes for the nearest herd's grounds (coming
				// at them from downwind, so its scent doesn't go ahead of it), and searches into the wind once there.
				int32 Grounds = INDEX_NONE;
				float GroundsD = 1e9f;
				for (int32 H = 0; H < Herds.Num(); ++H)
				{
					const FVector C = HerdCentre(H);
					bool bPrey = false;
					for (const int32 M : Herds[H].Members)
					{
						bPrey |= !Animals[M].bDead && Animals[M].Body.IsValid() && !Animals[M].bHunter;
					}
					const float Dist = FVector::Dist2D(Me, C);
					if (bPrey && !C.IsZero() && Dist > 15000.f && Dist < GroundsD)
					{
						GroundsD = Dist;
						Grounds = H;
					}
				}
				if (A.AmbushUntil > Now)
				{
					// Lying in wait at the water: still (hard to see), watching for what comes to drink.
					if (FVector::Dist2D(Me, A.AmbushAt) > 1000.f)
					{
						if (!A.bGoal)
						{
							GoTo(A, A.AmbushAt);
						}
						A.Thought = TEXT("hungry: going to lie in wait at the water");
					}
					else
					{
						A.Path.Reset();
						A.bGoal = false;
						A.bCreeping = true;
						A.Thought = FString::Printf(TEXT("lying in wait at the water (%.0f s more)"), A.AmbushUntil - Now);
					}
					break;
				}
				A.bCreeping = false;
				float GroundsKg = 0.f;
				if (Grounds != INDEX_NONE)
				{
					for (const int32 M : Herds[Grounds].Members)
					{
						GroundsKg += !Animals[M].bDead && Animals[M].Body.IsValid() && Animals[M].Stage >= 3 ? Animals[M].MassKg : 0.f;
					}
				}
				if (Grounds != INDEX_NONE && !A.bGoal && GroundsKg < 2.5f * PackKg(A) && FMath::FRand() < 0.35f)
				{
					FVector Stand, Water;
					if (Island->NearestShore(HerdCentre(Grounds), 40000.f, Stand, Water) && GoTo(A, Stand))
					{
						A.AmbushAt = Stand;
						A.AmbushUntil = Now + 150.f;
						++Ambushes;
						A.Thought = FString::Printf(TEXT("hungry: going to lie in wait at the water the %s drink at"), *Herds[Grounds].Kind);
						break;
					}
				}
				if (Grounds != INDEX_NONE && (!A.bGoal || FMath::Frac(Now / 20.f) < Dt / 20.f))
				{
					const FVector To = HerdCentre(Grounds) + Wind * 10000.f;
					if (GoTo(A, To))
					{
						A.Thought = FString::Printf(TEXT("hungry: making for the %s grazing grounds, %s off"), *Herds[Grounds].Kind, *Metres(GroundsD));
						break;
					}
				}
				if (Grounds != INDEX_NONE && A.bGoal)
				{
					A.Thought = FString::Printf(TEXT("hungry: making for the %s grazing grounds, %s off"), *Herds[Grounds].Kind, *Metres(GroundsD));
					break;
				}
				if (!A.bGoal)
				{
					// Searching: into the wind, so the scent of whatever is ahead comes to it (zig-zagging across it a little).
					for (int32 Try = 0; Try < 6; ++Try)
					{
						const FVector To = Me - Wind.RotateAngleAxis(FMath::FRandRange(-40.f, 40.f), FVector::UpVector) * FMath::FRandRange(12000.f, 25000.f);
						if (Island->Walkable(To, D->GetSpecies(), A.WadeCm))
						{
							GoTo(A, To);
							break;
						}
					}
				}
				A.Thought = TEXT("hungry: nothing worth hunting in sight or on the wind; searching into the wind");
				D->SetSniffing(FMath::Frac(Now / 7.f + Index * 0.37f) < 0.5f ? -1.f : 0.7f);          // nose up into the wind, now and then down to the ground
				break;
			}
			if (A.Prey != Was)
			{
				A.HuntPhase = 0;
				A.Thought = Why;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s %s"), *A.Name, *Why);
			}
		}
		FBFAnimal& P = Animals[A.Prey];
		// Where it believes its quarry is: where it last sensed it (exactly, once it's charging: it has it in sight).
		const FVector There = A.HuntPhase == 1 || Now - A.PreyKnownAt < 1.f ? P.Body->GetActorLocation() : A.PreyLastAt;
		const float Dist = FVector::Dist2D(Me, There);
		const float ChargeCm = FMath::Clamp(6.f * A.FastCm, 2500.f, 5000.f);    // a few seconds' burst
		if (A.HuntPhase == 0)
		{
			// Stalking: round to downwind of it (its scent comes to the hunter, the hunter's is carried away), each of a pack
			// from its own side, walking quietly; then the rush.
			int32 Slot = 0;
			for (const int32 M : Herds[A.Herd].Members)
			{
				if (M == Index)
				{
					break;
				}
				++Slot;
			}
			const FVector Downwind = Wind.RotateAngleAxis((Slot % 2 == 0 ? 1.f : -1.f) * 35.f * ((Slot + 1) / 2), FVector::UpVector);
			// Seen from further off than it can rush: no use creeping; it follows at a walk and lets the prey tire.
			const bool bSeen = P.Fear > 0.6f;
			const FVector Spot = bSeen ? There : There + Downwind * (ChargeCm * 0.9f);
			if (!A.bGoal || FVector::Dist2D(A.Goal, Spot) > 2500.f)
			{
				GoTo(A, Spot);
			}
			A.bCreeping = !bSeen && Dist < 2.5f * ChargeCm;          // walks up, then creeps the last of it
			A.Thought = FString::Printf(TEXT("stalking %s from downwind: %s off%s"), *P.Name, *Metres(Dist), A.bCreeping ? TEXT(", creeping") : TEXT(""));
			if (Now - A.PreyKnownAt > 3.f && !bSeen)
			{
				D->SetSniffing(0.6f);          // out of sight: on its scent, nose to the ground
			}
			else if (A.bCreeping)
			{
				D->SetGaze(There, -0.4f);       // creeping: head low, eyes on it
			}
			A.bFollowing = bSeen && Dist >= 1.6f * ChargeCm;
			if (!A.bFollowing)
			{
				A.FollowSince = Now;
			}
			else if (Now - A.FollowSince > 25.f)
			{
				// Following it gets nowhere: let the herd settle and come again later, unseen.
				A.GaveUp.Add(A.Prey, Now + 60.f);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s gives up following %s: it saw it coming; waiting for them to settle"), *A.Name, *P.Name);
				A.Prey = INDEX_NONE;
				A.bFollowing = false;
				A.Path.Reset();
				A.bGoal = false;
				break;
			}
			if (A.bFollowing)
			{
				A.Thought = FString::Printf(TEXT("%s saw it: loping after it till it tires (%s off, its wind %.0f%%)"), *P.Name, *Metres(Dist),
					100.f * (1.f - P.Body->GetExertion()));
			}
			if (Dist < ChargeCm || (bSeen && Dist < 1.6f * ChargeCm))
			{
				const bool bCatch = A.FastCm > 0.95f * P.FastCm * (1.f - 0.4f * P.Body->GetExertion()) || P.Health < 0.5f;
				if (bCatch)
				{
					A.HuntPhase = 1;
					A.ChargeAt = Now;
					++Charges;
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s charges %s from %.0f m%s"), *A.Name, *P.Name, Dist / 100.f, P.Fear > 0.6f ? TEXT(" (it was seen)") : TEXT(""));
				}
				else
				{
					A.GaveUp.Add(A.Prey, Now + 40.f);
					A.Thought = FString::Printf(TEXT("%s saw it and runs faster: letting it go"), *P.Name);
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s gives up on %s: it's faster"), *A.Name, *P.Name);
					A.Prey = INDEX_NONE;
				}
			}
			break;
		}
		// The rush: straight at it; the bite when its jaws reach it. It gives up when the chase goes on too long or it's spent.
		A.Path = { There };
		A.PathI = 0;
		A.bGoal = true;
		A.Thought = FString::Printf(TEXT("charging %s: %s"), *P.Name, *Metres(Dist));
		A.AttackClock -= Dt;
		if (A.AttackClock <= 0.f && FVector::Dist2D(Snout(A), There) < A.ReachCm + P.Body->GetCapsuleComponent()->GetScaledCapsuleRadius() + 150.f)
		{
			A.AttackClock = 1.2f;
			++A.Bites;
			D->Bite();
			Hurt(A.Prey, HitShare(A.MassKg, A.Weapon, P), A.Name, D, Now);
			if (P.bDead)
			{
				Choose(A, EBFAct::Feast, Now, FString::Printf(TEXT("killed %s: feeding"), *P.Name));
				break;
			}
		}
		if (Now - A.ChargeAt > 30.f || (D->GetExertion() > 0.95f && Dist > 3.f * A.ReachCm))
		{
			A.GaveUp.Add(A.Prey, Now + 40.f);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s gives up the chase of %s (%s)"), *A.Name, *P.Name, Now - A.ChargeAt > 30.f ? TEXT("too long") : TEXT("spent"));
			A.Prey = INDEX_NONE;
			A.HuntPhase = 0;
		}
		break;
	}
	case EBFAct::Feast:
	{
		const int32 C = SmellCarcass(A);
		if (C == INDEX_NONE)
		{
			Choose(A, EBFAct::Idle, Now, TEXT("nothing left of the kill"));
			break;
		}
		FBFCarcass& K = Carcasses[C];
		// A scavenger: while a much bigger meat eater is at the kill it waits out of its reach, on the far side, and goes
		// in once it has left it (gorged and resting away, or gone).
		const ABFDino* Big = BiggerAt(A, K.At, 4000.f);
		if (!Big && Nests.IsValidIndex(K.Nest) && Nests[K.Nest].Care > 0 && Animals.IsValidIndex(Nests[K.Nest].Mother))
		{
			const FBFAnimal& Mum = Animals[Nests[K.Nest].Mother];
			if (!Mum.bDead && Mum.Body.IsValid() && Mum.Herd != A.Herd && FVector::Dist2D(Mum.Body->GetActorLocation(), K.At) < 3500.f && Mum.MassKg > 2.f * PackKg(A))
			{
				Big = Mum.Body.Get();        // she's over her eggs: wait for her to leave them
			}
		}
		const int32 BigI = Big ? IndexOf(Big) : INDEX_NONE;
		const bool bBigDozing = BigI != INDEX_NONE && Animals[BigI].Act == EBFAct::Rest;
		if (Big && bBigDozing && FVector::Dist2D(Me, K.At) < 1500.f)
		{
			A.Thought = FString::Printf(TEXT("the %s is resting: stealing from the kill"), *Big->GetSpecies().Name);
		}
		if (Big && !bBigDozing)
		{
			const FVector Away = (K.At - Big->GetActorLocation()).GetSafeNormal2D();
			const FVector Wait = K.At + (Away.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Away) * 5500.f;
			if (!A.bGoal || FVector::Dist2D(A.Goal, Wait) > 1500.f)
			{
				GoTo(A, Wait);
			}
			A.Thought = FString::Printf(TEXT("waiting for the %s to leave the kill (%s), %s from it"), *Big->GetSpecies().Name, *K.Of,
				*Metres(FVector::Dist2D(Me, K.At)));
			break;
		}
		if (FVector::Dist2D(Snout(A), K.At) > A.ReachCm + 300.f)
		{
			if (!A.bGoal)
			{
				GoTo(A, K.At);
			}
			A.Thought = FString::Printf(TEXT("going to the kill (%s), %s off"), *K.Of, *Metres(FVector::Dist2D(Me, K.At)));
			break;
		}
		A.Path.Reset();
		A.bGoal = false;
		// its mouth down to the kill: the top of its flank as it lies
		if (!MouthTo(A, FVector(K.At.X, K.At.Y, K.FleshZ > 0.f ? FMath::Lerp(K.At.Z + 40.f, K.FleshZ, 0.7f) : K.At.Z + 40.f), Dt))
		{
			A.Thought = TEXT("reaching down to the kill");
			break;
		}
		A.AttackClock -= Dt;
		if (A.AttackClock <= 0.f)
		{
			A.AttackClock = 1.5f;
			D->Bite();
			D->BloodyMuzzle(4.f);
			const float Took = FMath::Min(K.MeatKg, FMath::Max(1.f, 0.004f * A.MassKg));
			K.MeatKg -= Took;
			if (Nests.IsValidIndex(K.Nest))
			{
				// Eggs: as many gone as the mouthfuls took.
				FBFNest& N = Nests[K.Nest];
				const int32 Left = FMath::Clamp(FMath::FloorToInt(K.MeatKg / N.EggKg + 0.001f), 0, N.Eggs);     // (an egg broken into is gone)
				if (Left < N.Eggs)
				{
					N.Eaten += N.Eggs - Left;
					EggsEaten += N.Eggs - Left;
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s ate %d %s egg%s (%d left in the nest)"), *A.Name, N.Eggs - Left, *N.Base, N.Eggs - Left == 1 ? TEXT("") : TEXT("s"), Left);
					N.Eggs = Left;
					DrawEggs(N);
					if (N.Eggs == 0 && Animals.IsValidIndex(N.Mother) && Animals[N.Mother].NestI == K.Nest)
					{
						Animals[N.Mother].NestI = INDEX_NONE;          // her clutch is gone: free to breed again
					}
				}
			}
			A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + Took);
			A.EatenKg += Took;
			A.Thought = FString::Printf(TEXT("tearing at %s: %.0f of %.0f kg in its stomach, %.0f kg left of it"), *K.Of, A.StomachKg, A.StomachMaxKg, K.MeatKg);
			if (A.StomachKg >= 0.95f * A.StomachMaxKg)
			{
				++A.Meals;
				Choose(A, EBFAct::Rest, Now, TEXT("gorged"));
			}
		}
		break;
	}
	case EBFAct::Retreat:
	{
		if (!A.bGoal)
		{
			const FVector Away = (Me - A.ThreatAt).GetSafeNormal2D();
			for (const float Turn : { 0.f, 40.f, -40.f, 80.f, -80.f })
			{
				const FVector To = Me + Away.RotateAngleAxis(Turn, FVector::UpVector) * 15000.f;
				if (Island->Walkable(To, D->GetSpecies(), A.WadeCm))
				{
					GoTo(A, To);
					break;
				}
			}
		}
		break;
	}
	default:
		// Drinking, resting, rejoining and roaming the plant eaters' way.
		Do(A, Index, Dt, Now);
		break;
	}
}

FString ABFLife::PlayerBite(ABFDino* Player)
{
	if (!Player)
	{
		return FString();
	}
	FVector S = Player->GetBoneWorld(TEXT("head_end"));
	if (S.Equals(Player->GetActorLocation()))
	{
		S = Player->GetBoneWorld(TEXT("head"));
	}
	const float Reach = 150.f + 0.5f * Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 j = 0; j < Animals.Num(); ++j)
	{
		FBFAnimal& P = Animals[j];
		if (P.bDead || !P.Body.IsValid() || P.Body.Get() == Player)
		{
			continue;
		}
		if (FVector::Dist2D(S, P.Body->GetActorLocation()) < Reach + P.Body->GetCapsuleComponent()->GetScaledCapsuleRadius())
		{
			Hurt(j, HitShare(Player->GetSpecies().MassKg, 1.f, P), TEXT("you"), Player, Now);
			return P.bDead ? FString::Printf(TEXT("You killed %s (E by it to eat)"), *P.Name)
				: FString::Printf(TEXT("You bit %s: its health %.0f%%"), *P.Name, 100.f * P.Health);
		}
	}
	return FString();
}

FString ABFLife::PlayerEat(ABFDino* Player, float* OutKg)
{
	if (!Player)
	{
		return FString();
	}
	const int32 C = NearestCarcass(Player->GetBoneWorld(TEXT("head")), 150.f + Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + 300.f);
	if (C == INDEX_NONE)
	{
		return FString();
	}
	FBFCarcass& K = Carcasses[C];
	const float Took = FMath::Min(K.MeatKg, FMath::Max(1.f, 0.004f * Player->GetSpecies().MassKg));
	K.MeatKg -= Took;
	if (OutKg)
	{
		*OutKg = Took;
	}
	Player->Bite();
	return FString::Printf(TEXT("Eating %s: %.0f kg this mouthful, %.0f kg left of it"), *K.Of, Took, K.MeatKg);
}

FString ABFLife::WatchText(const FString& Clock) const
{
	// The island's life at a glance: the time, how many of each kind live (and how many have died), the kills, then each.
	TMap<FString, FIntPoint> Kinds;     // alive, dead
	TArray<FString> Order;
	int32 Kills = 0;
	for (const FBFAnimal& A : Animals)
	{
		const FString Kind = Herds.IsValidIndex(A.Herd) ? Herds[A.Herd].Kind : A.Base;
		if (!Kinds.Contains(Kind))
		{
			Order.Add(Kind);
		}
		FIntPoint& K = Kinds.FindOrAdd(Kind);
		(A.bDead ? K.Y : K.X) += 1;
		Kills += A.Kills;
	}
	FString Out = Clock + TEXT("\n");
	for (const FString& K : Order)
	{
		Out += FString::Printf(TEXT("%s %d%s   "), *K, Kinds[K].X, Kinds[K].Y > 0 ? *FString::Printf(TEXT(" (%d dead)"), Kinds[K].Y) : TEXT(""));
	}
	float Meat = 0.f;
	for (const FBFCarcass& C : Carcasses)
	{
		Meat += C.MeatKg;
	}
	Out += FString::Printf(TEXT("\nKills %d, carcasses %d (%.1f t of meat left)\n%s\n\n"), Kills, Carcasses.Num(), Meat / 1000.f, *BreedingText());
	Out += FString::Printf(TEXT("Far off (living simply): %d\n\n"), NumDormant());
	// Each living one: what it's doing, its needs, its thought; its family (generation, mother, father, young living).
	TArray<int32> Young;
	Young.Init(0, Animals.Num());
	for (const FBFAnimal& A : Animals)
	{
		if (!A.bDead)
		{
			if (Animals.IsValidIndex(A.Mother)) { ++Young[A.Mother]; }
			if (Animals.IsValidIndex(A.Father)) { ++Young[A.Father]; }
		}
	}
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		const FBFAnimal& A = Animals[i];
		if (A.bDead)
		{
			continue;
		}
		const FString Family = FString::Printf(TEXT("%s, gen %d%s%s%s"), A.bFemale ? TEXT("F") : TEXT("M"), A.Generation,
			Animals.IsValidIndex(A.Mother) ? *FString::Printf(TEXT(", mother %s%s"), *Animals[A.Mother].Name, Animals[A.Mother].bDead ? TEXT(" (dead)") : TEXT("")) : TEXT(""),
			Animals.IsValidIndex(A.Father) ? *FString::Printf(TEXT(", father %s%s"), *Animals[A.Father].Name, Animals[A.Father].bDead ? TEXT(" (dead)") : TEXT("")) : TEXT(""),
			Young[i] > 0 ? *FString::Printf(TEXT(", %d young living"), Young[i]) : TEXT(""));
		Out += FString::Printf(TEXT("%-10s %-20s hunger %.1f  thirst %.1f  tired %.1f  fear %.1f  health %.0f%%%s\n      %s; %s\n"), *A.Name, ActName(A.Act),
			1.f - A.StomachKg / A.StomachMaxKg, A.Thirst, A.Tired, A.Fear, 100.f * A.Health, A.bDormant ? TEXT("  (far off)") : TEXT(""), *Family, *A.Thought);
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------- saving (life cycle step 3)

namespace
{
	TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V)
	{
		return { MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V.X)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V.Y)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V.Z)) };
	}

	FVector VecOf(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O->TryGetArrayField(Field, A) && A->Num() == 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : FVector::ZeroVector;
	}

	float NumOf(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Default = 0.f)
	{
		double V = 0.0;
		return O->TryGetNumberField(Field, V) ? float(V) : Default;
	}
}

int32 ABFLife::NumAwaiting() const
{
	int32 Count = 0;
	for (const FBFAnimal& A : Animals)
	{
		Count += A.bAwaiting ? 1 : 0;
	}
	return Count;
}

int32 ABFLife::NumAlive() const
{
	int32 Count = 0;
	for (const FBFAnimal& A : Animals)
	{
		Count += !A.bDead && (A.Body.IsValid() || A.bAwaiting) ? 1 : 0;
	}
	return Count;
}

TSharedRef<FJsonObject> ABFLife::SaveState() const
{
	const float Now = GetWorld()->GetTimeSeconds();
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	// The living, numbered afresh (the dead are carcasses now); their mothers by the new numbers.
	TArray<int32> NewIndex;
	NewIndex.Init(INDEX_NONE, Animals.Num());
	int32 Next = 0;
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		if (!Animals[i].bDead && !Animals[i].bPlayer && (Animals[i].Body.IsValid() || Animals[i].bAwaiting))
		{
			NewIndex[i] = Next++;
		}
	}
	TArray<int32> NestNew;
	NestNew.Init(INDEX_NONE, Nests.Num());
	TArray<TSharedPtr<FJsonValue>> NestList;
	for (int32 n = 0; n < Nests.Num(); ++n)
	{
		const FBFNest& N = Nests[n];
		if (N.Eggs <= 0)
		{
			continue;
		}
		NestNew[n] = NestList.Num();
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("at"), Vec(N.At));
		O->SetStringField(TEXT("kind"), N.Base);
		O->SetNumberField(TEXT("herd"), N.Herd);
		O->SetNumberField(TEXT("mother"), Animals.IsValidIndex(N.Mother) ? NewIndex[N.Mother] : INDEX_NONE);
		O->SetNumberField(TEXT("father"), Animals.IsValidIndex(N.Father) ? NewIndex[N.Father] : INDEX_NONE);
		O->SetNumberField(TEXT("eggs"), N.Eggs);
		O->SetNumberField(TEXT("laid"), N.Laid);
		O->SetNumberField(TEXT("hatched"), N.Hatched);
		O->SetNumberField(TEXT("eaten"), N.Eaten);
		O->SetNumberField(TEXT("egg_kg"), N.EggKg);
		O->SetNumberField(TEXT("incub_s"), FMath::RoundToDouble(N.IncubS));
		O->SetNumberField(TEXT("care"), N.Care);
		O->SetNumberField(TEXT("young_herd"), N.YoungHerd);
		NestList.Add(MakeShared<FJsonValueObject>(O));
	}
	Out->SetArrayField(TEXT("nests"), NestList);
	TArray<TSharedPtr<FJsonValue>> HerdList;
	for (const FBFHerd& H : Herds)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), H.Kind);
		O->SetArrayField(TEXT("home"), Vec(H.Home));
		O->SetNumberField(TEXT("young_born"), H.YoungBorn);
		HerdList.Add(MakeShared<FJsonValueObject>(O));
	}
	Out->SetArrayField(TEXT("herds"), HerdList);
	TArray<TSharedPtr<FJsonValue>> List;
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		if (NewIndex[i] == INDEX_NONE)
		{
			continue;
		}
		const FBFAnimal& A = Animals[i];
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("body"), A.Body.IsValid() ? A.Body->GetSpecies().Key : A.AwaitKey);
		O->SetStringField(TEXT("name"), A.Name);
		O->SetStringField(TEXT("kind"), A.Base);
		O->SetNumberField(TEXT("herd"), A.Herd);
		O->SetNumberField(TEXT("mother"), Animals.IsValidIndex(A.Mother) ? NewIndex[A.Mother] : INDEX_NONE);
		O->SetBoolField(TEXT("hunter"), A.bHunter);
		O->SetNumberField(TEXT("stage"), A.Stage);
		O->SetNumberField(TEXT("age_s"), FMath::RoundToDouble(A.AgeS));
		O->SetNumberField(TEXT("growths"), A.Growths);
		O->SetArrayField(TEXT("at"), Vec(A.Body.IsValid() ? A.Body->GetActorLocation() : A.AwaitAt));
		O->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(A.Body.IsValid() ? A.Body->GetActorRotation().Yaw : A.AwaitYaw));
		O->SetNumberField(TEXT("full"), A.StomachMaxKg > 0.f ? A.StomachKg / A.StomachMaxKg : 0.5f);
		O->SetNumberField(TEXT("thirst"), A.Thirst);
		O->SetNumberField(TEXT("tired"), A.Tired);
		O->SetNumberField(TEXT("health"), A.Health);
		O->SetNumberField(TEXT("kills"), A.Kills);
		O->SetNumberField(TEXT("eaten_kg"), A.EatenKg);
		O->SetBoolField(TEXT("female"), A.bFemale);
		O->SetBoolField(TEXT("gravid"), A.bGravid);
		O->SetNumberField(TEXT("lay_in"), FMath::RoundToDouble(A.LayIn));
		O->SetNumberField(TEXT("clutch_in"), FMath::RoundToDouble(A.ClutchIn));
		O->SetNumberField(TEXT("mate_in"), FMath::RoundToDouble(A.MateIn));
		O->SetNumberField(TEXT("father"), Animals.IsValidIndex(A.Father) ? NewIndex[A.Father] : INDEX_NONE);
		O->SetNumberField(TEXT("clutch_father"), Animals.IsValidIndex(A.ClutchFather) ? NewIndex[A.ClutchFather] : INDEX_NONE);
		O->SetNumberField(TEXT("nest"), Nests.IsValidIndex(A.NestI) ? NestNew[A.NestI] : INDEX_NONE);
		O->SetNumberField(TEXT("matings"), A.Matings);
		O->SetNumberField(TEXT("clutches"), A.Clutches);
		O->SetNumberField(TEXT("fate"), A.Fate);
		O->SetNumberField(TEXT("generation"), A.Generation);
		O->SetNumberField(TEXT("condition"), A.Condition);
		O->SetBoolField(TEXT("flier"), A.bFlier);
		O->SetArrayField(TEXT("genes"), { MakeShared<FJsonValueNumber>(A.Genes.Size), MakeShared<FJsonValueNumber>(A.Genes.Hue), MakeShared<FJsonValueNumber>(A.Genes.Shade),
			MakeShared<FJsonValueNumber>(A.Genes.Strength), MakeShared<FJsonValueNumber>(A.Genes.Speed) });
		O->SetNumberField(TEXT("risk"), A.Risk);
		TArray<TSharedPtr<FJsonValue>> Mem;
		for (const FBFMemory& M : A.Memory)
		{
			TSharedRef<FJsonObject> MO = MakeShared<FJsonObject>();
			MO->SetNumberField(TEXT("kind"), M.Kind);
			MO->SetArrayField(TEXT("at"), Vec(M.At));
			MO->SetNumberField(TEXT("value"), M.Value);
			MO->SetNumberField(TEXT("ago"), FMath::RoundToDouble(Now - M.Time));
			Mem.Add(MakeShared<FJsonValueObject>(MO));
		}
		O->SetArrayField(TEXT("memory"), Mem);
		List.Add(MakeShared<FJsonValueObject>(O));
	}
	Out->SetArrayField(TEXT("animals"), List);
	TArray<TSharedPtr<FJsonValue>> Kills;
	for (const FBFCarcass& C : Carcasses)
	{
		if (C.Nest != INDEX_NONE)
		{
			continue;          // (a nest's eggs: saved with the nest)
		}
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("at"), Vec(C.At));
		O->SetNumberField(TEXT("meat_kg"), C.MeatKg);
		O->SetNumberField(TEXT("ago"), FMath::RoundToDouble(Now - C.Time));
		O->SetStringField(TEXT("of"), C.Of);
		Kills.Add(MakeShared<FJsonValueObject>(O));
	}
	Out->SetArrayField(TEXT("carcasses"), Kills);
	Out->SetNumberField(TEXT("young_number"), YoungNumber);
	Out->SetNumberField(TEXT("births"), Births);
	Out->SetNumberField(TEXT("eggs_laid"), EggsLaid);
	Out->SetNumberField(TEXT("eggs_eaten"), EggsEaten);
	Out->SetNumberField(TEXT("matings"), NumMatings);
	Out->SetNumberField(TEXT("contests"), Contests);
	Out->SetNumberField(TEXT("died_young"), DiedYoung);
	Out->SetNumberField(TEXT("starved"), Starved);
	Out->SetNumberField(TEXT("old_age"), OldAge);
	Out->SetArrayField(TEXT("colony"), Vec(Colony));
	TArray<TSharedPtr<FJsonValue>> ShoalList;
	for (const FShoal& Sh : Shoals)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("at"), Vec(Sh.At));
		O->SetNumberField(TEXT("fish_kg"), Sh.FishKg);
		O->SetNumberField(TEXT("max_kg"), Sh.MaxKg);
		ShoalList.Add(MakeShared<FJsonValueObject>(O));
	}
	Out->SetArrayField(TEXT("shoals"), ShoalList);
	Out->SetNumberField(TEXT("newcomers"), Newcomers);
	return Out;
}

void ABFLife::LoadState(ABFWorld* World, const TSharedPtr<FJsonObject>& In)
{
	Clear();
	Island = World;
	if (!In.IsValid() || !World)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	{
		// The colony's nest trees and ledges first (the fliers' bodies come back onto them).
		const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
		if (In->TryGetArrayField(TEXT("colony"), C) && C->Num() == 3)
		{
			Colony = FVector((*C)[0]->AsNumber(), (*C)[1]->AsNumber(), (*C)[2]->AsNumber());
			if (!Colony.IsZero())
			{
				BuildPerches();
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* HerdList = nullptr;
	if (In->TryGetArrayField(TEXT("herds"), HerdList))
	{
		for (const TSharedPtr<FJsonValue>& V : *HerdList)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FBFHerd& H = Herds.AddDefaulted_GetRef();
			if (O.IsValid())
			{
				O->TryGetStringField(TEXT("kind"), H.Kind);
				H.Home = VecOf(O, TEXT("home"));
				H.YoungBorn = int32(NumOf(O, TEXT("young_born")));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (In->TryGetArrayField(TEXT("animals"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FBFAnimal& A = Animals.AddDefaulted_GetRef();
			A.bAwaiting = true;
			O->TryGetStringField(TEXT("body"), A.AwaitKey);
			O->TryGetStringField(TEXT("name"), A.Name);
			O->TryGetStringField(TEXT("kind"), A.Base);
			A.Herd = int32(NumOf(O, TEXT("herd")));
			if (!Herds.IsValidIndex(A.Herd))
			{
				A.Herd = Herds.Num();
				Herds.AddDefaulted_GetRef().Kind = A.Base;
			}
			Herds[A.Herd].Members.Add(Animals.Num() - 1);
			A.Mother = int32(NumOf(O, TEXT("mother"), -1.f));
			O->TryGetBoolField(TEXT("hunter"), A.bHunter);
			A.Stage = FMath::Clamp(int32(NumOf(O, TEXT("stage"), 4.f)), 1, 4);
			A.bYoung = A.Stage <= 2;
			A.AgeS = NumOf(O, TEXT("age_s"));
			A.Growths = int32(NumOf(O, TEXT("growths")));
			A.AwaitAt = VecOf(O, TEXT("at"));
			A.AwaitYaw = NumOf(O, TEXT("yaw"));
			A.StomachMaxKg = 1.f;                   // (its body sets the real size, keeping how full it is)
			A.StomachKg = FMath::Clamp(NumOf(O, TEXT("full"), 0.5f), 0.f, 1.f);
			A.Thirst = NumOf(O, TEXT("thirst"));
			A.Tired = NumOf(O, TEXT("tired"));
			A.Health = FMath::Clamp(NumOf(O, TEXT("health"), 1.f), 0.05f, 1.f);
			A.Kills = int32(NumOf(O, TEXT("kills")));
			A.EatenKg = NumOf(O, TEXT("eaten_kg"));
			O->TryGetBoolField(TEXT("female"), A.bFemale);
			O->TryGetBoolField(TEXT("gravid"), A.bGravid);
			A.LayIn = NumOf(O, TEXT("lay_in"));
			A.ClutchIn = NumOf(O, TEXT("clutch_in"));
			A.MateIn = NumOf(O, TEXT("mate_in"));
			A.Father = int32(NumOf(O, TEXT("father"), -1.f));
			A.ClutchFather = int32(NumOf(O, TEXT("clutch_father"), -1.f));
			A.NestI = int32(NumOf(O, TEXT("nest"), -1.f));
			A.Matings = int32(NumOf(O, TEXT("matings")));
			A.Clutches = int32(NumOf(O, TEXT("clutches")));
			A.Fate = NumOf(O, TEXT("fate"), -1.f);
			A.Generation = int32(NumOf(O, TEXT("generation")));
			A.Condition = NumOf(O, TEXT("condition"), 0.6f);
			O->TryGetBoolField(TEXT("flier"), A.bFlier);
			const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
			if (O->TryGetArrayField(TEXT("genes"), G) && G->Num() == 5)
			{
				A.Genes.Size = float((*G)[0]->AsNumber());
				A.Genes.Hue = float((*G)[1]->AsNumber());
				A.Genes.Shade = float((*G)[2]->AsNumber());
				A.Genes.Strength = float((*G)[3]->AsNumber());
				A.Genes.Speed = float((*G)[4]->AsNumber());
			}
			A.Risk = NumOf(O, TEXT("risk"));
			const TArray<TSharedPtr<FJsonValue>>* Mem = nullptr;
			if (O->TryGetArrayField(TEXT("memory"), Mem))
			{
				for (const TSharedPtr<FJsonValue>& MV : *Mem)
				{
					const TSharedPtr<FJsonObject> MO = MV->AsObject();
					if (MO.IsValid())
					{
						FBFMemory& M = A.Memory.AddDefaulted_GetRef();
						M.Kind = uint8(NumOf(MO, TEXT("kind")));
						M.At = VecOf(MO, TEXT("at"));
						M.Value = NumOf(MO, TEXT("value"));
						M.Time = Now - NumOf(MO, TEXT("ago"));
					}
				}
			}
		}
	}
	for (FBFAnimal& A : Animals)
	{
		A.Mother = Animals.IsValidIndex(A.Mother) ? A.Mother : INDEX_NONE;
	}
	for (FBFHerd& H : Herds)
	{
		H.Leader = H.Members.Num() > 0 ? H.Members[0] : 0;
	}
	const TArray<TSharedPtr<FJsonValue>>* Kills = nullptr;
	if (In->TryGetArrayField(TEXT("carcasses"), Kills))
	{
		for (const TSharedPtr<FJsonValue>& V : *Kills)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid())
			{
				FBFCarcass& C = Carcasses.AddDefaulted_GetRef();
				C.At = VecOf(O, TEXT("at"));
				C.MeatKg = NumOf(O, TEXT("meat_kg"));
				C.Time = Now - NumOf(O, TEXT("ago"));
				O->TryGetStringField(TEXT("of"), C.Of);
			}
		}
	}
	YoungNumber = int32(NumOf(In, TEXT("young_number")));
	Births = int32(NumOf(In, TEXT("births")));
	EggsLaid = int32(NumOf(In, TEXT("eggs_laid")));
	EggsEaten = int32(NumOf(In, TEXT("eggs_eaten")));
	NumMatings = int32(NumOf(In, TEXT("matings")));
	Contests = int32(NumOf(In, TEXT("contests")));
	DiedYoung = int32(NumOf(In, TEXT("died_young")));
	Starved = int32(NumOf(In, TEXT("starved")));
	OldAge = int32(NumOf(In, TEXT("old_age")));
	Colony = VecOf(In, TEXT("colony"));
	Shoals.Reset();
	const TArray<TSharedPtr<FJsonValue>>* ShoalList = nullptr;
	if (In->TryGetArrayField(TEXT("shoals"), ShoalList))
	{
		for (const TSharedPtr<FJsonValue>& V : *ShoalList)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid())
			{
				FShoal& Sh = Shoals.AddDefaulted_GetRef();
				Sh.At = VecOf(O, TEXT("at"));
				Sh.FishKg = NumOf(O, TEXT("fish_kg"));
				Sh.MaxKg = NumOf(O, TEXT("max_kg"), 10.f);
			}
		}
	}
	Newcomers = int32(NumOf(In, TEXT("newcomers")));
	const TArray<TSharedPtr<FJsonValue>>* NestList = nullptr;
	if (In->TryGetArrayField(TEXT("nests"), NestList))
	{
		for (const TSharedPtr<FJsonValue>& V : *NestList)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			const int32 NI = Nests.Num();
			FBFNest& N = Nests.AddDefaulted_GetRef();
			N.At = VecOf(O, TEXT("at"));
			O->TryGetStringField(TEXT("kind"), N.Base);
			N.Herd = int32(NumOf(O, TEXT("herd")));
			N.Mother = int32(NumOf(O, TEXT("mother"), -1.f));
			N.Father = int32(NumOf(O, TEXT("father"), -1.f));
			N.Mother = Animals.IsValidIndex(N.Mother) ? N.Mother : INDEX_NONE;
			N.Father = Animals.IsValidIndex(N.Father) ? N.Father : INDEX_NONE;
			N.Eggs = int32(NumOf(O, TEXT("eggs")));
			N.Laid = int32(NumOf(O, TEXT("laid"), float(N.Eggs)));
			N.Hatched = int32(NumOf(O, TEXT("hatched")));
			N.Eaten = int32(NumOf(O, TEXT("eaten")));
			N.EggKg = FMath::Max(0.05f, NumOf(O, TEXT("egg_kg"), 1.f));
			N.IncubS = NumOf(O, TEXT("incub_s"));
			N.Care = int32(NumOf(O, TEXT("care"), 1.f));
			N.YoungHerd = int32(NumOf(O, TEXT("young_herd"), -1.f));
			N.Carcass = Carcasses.Num();
			FBFCarcass Eggs;
			Eggs.At = N.At;
			Eggs.MeatKg = N.Eggs * N.EggKg;
			Eggs.Time = Now;
			Eggs.Of = FString::Printf(TEXT("a %s nest"), Herds.IsValidIndex(N.Herd) ? *Herds[N.Herd].Kind : *N.Base);
			Eggs.Nest = NI;
			Carcasses.Add(Eggs);
			DrawEggs(N);
			BodyIndex(StageKey(N.Base, 1));
		}
	}
	for (FBFAnimal& A : Animals)
	{
		A.NestI = Nests.IsValidIndex(A.NestI) ? A.NestI : INDEX_NONE;
		A.Father = Animals.IsValidIndex(A.Father) ? A.Father : INDEX_NONE;
		A.ClutchFather = Animals.IsValidIndex(A.ClutchFather) ? A.ClutchFather : INDEX_NONE;
	}
	TickAwaiting();
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: back: %d animals in %d herds (%d waiting for their bodies), %d carcasses, %d nests with eggs"),
		Animals.Num(), Herds.Num(), NumAwaiting(), Carcasses.Num(), Nests.Num());
}

void ABFLife::TickAwaiting()
{
	// Animals back from a save: each into its body once that's in (a growth stage's body may have to be brought in
	// first); one whose body can't be had is lost.
	for (FBFAnimal& A : Animals)
	{
		if (!A.bAwaiting)
		{
			continue;
		}
		const int32 SpeciesI = BodyIndex(A.AwaitKey);
		if (SpeciesI != INDEX_NONE)
		{
			if (SpawnBody(A, SpeciesI, A.AwaitAt, A.AwaitYaw))
			{
				A.bAwaiting = false;
			}
		}
		else if (BodyFailed.Contains(A.AwaitKey))
		{
			A.bAwaiting = false;
			A.bDead = true;
			UE_LOG(LogTemp, Warning, TEXT("[BoneForge] life: %s couldn't come back (no body %s)"), *A.Name, *A.AwaitKey);
		}
	}
}

bool ABFLife::SpawnBody(FBFAnimal& A, int32 SpeciesI, const FVector& At, float Yaw)
{
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	if (!GM || !Island.IsValid())
	{
		return false;
	}
	float Ground = At.Z;
	Island->GroundAt(FVector2D(At), Ground);
	float CaveZ = 0.f;
	if (Island->CaveFloorAt(At, CaveZ))
	{
		Ground = CaveZ;          // in a cave (a den, an alcove): on its floor, not on the hill over it
	}
	const int32 Up = PerchNear(At, 450.f);          // up on a perch (a hatchling in its nest, a flier brought back): there
	if (Up != INDEX_NONE)
	{
		Ground = Perches[Up].Top.Z;
	}
	ABFDino* D = GM->SpawnCompanion(SpeciesI, FVector(At.X, At.Y, Ground), Yaw);
	if (!D)
	{
		return false;
	}
	if (Up != INDEX_NONE)
	{
		D->SetOnPerch(Perches[Up].Out);
	}
	D->SetIdleBehaviours(true);
	A.Body = D;
	A.SpeciesIndex = SpeciesI;
	ApplyBody(A);
	ApplyGenes(A);
	if (A.bYoung && Animals.IsValidIndex(A.Mother) && Animals[A.Mother].Body.IsValid())
	{
		A.Spacing = 0.6f * Animals[A.Mother].Body->BodyLengthCm();       // close to her
	}
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(D);
	T->SetupAttachment(D->GetRootComponent());
	T->SetRelativeLocation(FVector(0.f, 0.f, 0.5f * D->GetSpecies().Bounds.GetSize().Z + 120.f));
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetWorldSize(FMath::Clamp(0.1f * D->BodyLengthCm(), 50.f, 150.f));
	T->SetTextRenderColor(FColor(255, 250, 230));
	T->SetCastShadow(false);
	T->RegisterComponent();
	A.Label = T;
	A.LastPos = A.LastStep = D->GetActorLocation();
	A.ThinkIn = FMath::FRandRange(0.f, 0.4f);
	A.Path.Reset();
	A.bGoal = false;
	return true;
}

// ---------------------------------------------------------------------------------------------- breeding (life cycle steps 4-6)

namespace
{
	/** How a kind cares for its eggs, from the fossils: 0 lays and leaves (the sauropods' scattered clutches), 1 guards
	 *  (the big plant eaters, T. rex), 2 broods (the small feathered hunters sat on their nests). */
	int32 CareOf(const FString& Base)
	{
		return Base == TEXT("brachio") ? 0 : Base == TEXT("raptor") || Base == TEXT("ptera") ? 2 : 1;
	}
}

bool ABFLife::IsReady(const FBFAnimal& F) const
{
	// A female in condition: grown, well fed and watered, healthy, not carrying or minding eggs, her time come round; and
	// the land not too crowded for the game (MaxAnimals).
	return !F.bDead && F.Body.IsValid() && F.bFemale && F.Stage >= 4 && !F.bGravid && F.ClutchIn <= 0.f && !Nests.IsValidIndex(F.NestI)
		&& F.Condition > 0.45f && F.Health > 0.7f && F.Thirst < 0.7f && AliveNow < MaxAnimals
		&& (!KindInfo.Contains(F.Base) || KindInfo[F.Base].Alive < RoomFor(F.Base));
}

void ABFLife::MakeReady(int32 HerdI)
{
	if (!Herds.IsValidIndex(HerdI))
	{
		return;
	}
	for (const int32 M : Herds[HerdI].Members)
	{
		FBFAnimal& F = Animals[M];
		if (F.Stage >= 4)
		{
			F.StomachKg = 0.9f * F.StomachMaxKg;
			F.Thirst = 0.1f;
			F.Health = 1.f;
			F.ClutchIn = 0.f;
			F.MateIn = 0.f;
		}
	}
	AliveClock = 0.f;
}

void ABFLife::BreedScores(FBFAnimal& A, int32 Index, float Now, float* Score)
{
	BreedWhy.Reset();
	if (A.bYoung || A.Stage < 4)
	{
		return;
	}
	const FVector Me = A.Body->GetActorLocation();
	if (A.bFemale)
	{
		if (A.bGravid && A.LayIn <= 0.f)
		{
			Score[int32(EBFAct::Nest)] = 1.25f;
			BreedWhy = TEXT("her eggs are due: making a nest");
		}
		else if (Nests.IsValidIndex(A.NestI) && Nests[A.NestI].Eggs > 0 && Nests[A.NestI].Care > 0)
		{
			const FBFNest& N = Nests[A.NestI];
			Score[int32(EBFAct::Nest)] = (N.Care == 2 ? 0.95f : 0.7f) + (FVector::Dist2D(Me, N.At) > 4000.f ? 0.2f : 0.f);     // a brooder leaves her eggs only near starving
			BreedWhy = FString::Printf(TEXT("%s her %d eggs"), N.Care == 2 ? TEXT("brooding") : TEXT("guarding"), N.Eggs);
		}
		return;
	}
	// A male: the nearest female of his kind in condition, within 600 m (seen, smelled, heard calling), not one he lost to
	// a rival lately; only when he's fit and not hungry.
	A.CourtTarget = INDEX_NONE;
	if (A.MateIn > 0.f || A.Health < 0.6f || A.Condition < 0.3f)
	{
		return;
	}
	float BestD = 120000.f;          // (breeding males range wide: a herd's split can leave the sexes apart)
	for (const int32 j : ReadyFemales)
	{
		if (!Animals.IsValidIndex(j))
		{
			continue;
		}
		const FBFAnimal& F = Animals[j];
		if (j == Index || F.Base != A.Base || !IsReady(F) || (A.IgnoreFemale == j && Now < A.IgnoreUntil))
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Me, F.Body->GetActorLocation());
		if (Dist < BestD)
		{
			BestD = Dist;
			A.CourtTarget = j;
		}
	}
	if (A.CourtTarget != INDEX_NONE)
	{
		// Near her it comes before the day's routine (grazing peaks, the midday rest, sleep), not before real need or fear.
		Score[int32(EBFAct::Court)] = (BestD < 20000.f ? 1.1f : 0.8f) + (A.Act == EBFAct::Court ? 0.1f : 0.f);
		BreedWhy = FString::Printf(TEXT("%s is in condition, %s away: courting her"), *Animals[A.CourtTarget].Name, *Metres(BestD));
	}
}

void ABFLife::DoBreed(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	if (A.Act == EBFAct::Court)
	{
		const int32 F = A.CourtTarget;
		if (!Animals.IsValidIndex(F) || !IsReady(Animals[F]))
		{
			A.DisplayS = 0.f;
			Choose(A, EBFAct::Idle, Now, TEXT("she's no longer in condition"));
			return;
		}
		const FVector Her = Animals[F].Body->GetActorLocation();
		const float Dist = FVector::Dist2D(Me, Her);
		const float Close = 0.5f * (A.Spacing + Animals[F].Spacing) + 800.f;
		if (Dist > Close + 600.f)
		{
			if (!A.bGoal || FMath::Frac(Now / 5.f) < Dt / 5.f)
			{
				GoTo(A, Her);
			}
			A.DisplayS = 0.f;
			A.Thought = FString::Printf(TEXT("courting %s: going to her, %s away"), *Animals[F].Name, *Metres(Dist));
			return;
		}
		// Beside her: he displays (head up, jaws wide, looking round: a frill, a crest, a roar), facing her.
		A.Path.Reset();
		A.bGoal = false;
		D->AddMovementInput((Her - Me).GetSafeNormal2D(), 0.05f);
		A.LookClock -= Dt;
		if (A.LookClock <= 0.f)
		{
			A.LookClock = 3.f;
			D->StartIdle(int32(A.DisplayS / 3.f) % 2 == 0 ? EBFIdle::Yawn : EBFIdle::Look);
		}
		A.DisplayS += Dt;
		// A rival courting her too: they face off, and once both have displayed the weaker gives way; matched near
		// enough, they shove first (a hurt, rarely more).
		bool bRival = false;
		for (int32 j = 0; j < Animals.Num(); ++j)
		{
			FBFAnimal& B = Animals[j];
			// (any male of his kind courting near him: over her or another female close by)
			if (j == Index || B.bDead || !B.Body.IsValid() || B.Act != EBFAct::Court || B.Base != A.Base || FVector::Dist2D(B.Body->GetActorLocation(), Me) > 5000.f)
			{
				continue;
			}
			bRival = true;
			const bool bHorns = A.Base == TEXT("trike");
			const FVector HisHead = B.Body->GetBoneWorld(TEXT("head"));
			if (A.DisplayS < 4.f || B.DisplayS < 4.f)
			{
				D->SetGaze(HisHead, bHorns ? -0.6f : 0.8f);          // eyes on him: horns lowered, or head up
				A.Thought = FString::Printf(TEXT("facing a rival, %s, over %s"), *B.Name, *Animals[F].Name);
				continue;
			}
			const float Luck = 0.85f + 0.3f * FMath::Frac(FMath::Sin(float(Index * 7 + j * 13 + F) * 12.9898f) * 43758.5453f);
			const float RankA = 1.f + 0.05f * FMath::Clamp(A.Wins - A.Losses, -4, 4), RankB = 1.f + 0.05f * FMath::Clamp(B.Wins - B.Losses, -4, 4);
			const float Mine = A.MassKg * A.Genes.Strength * A.Health * Luck * RankA, Theirs = B.MassKg * B.Genes.Strength * B.Health * (2.f - Luck) * RankB;
			if (FMath::Max(Mine, Theirs) < 1.25f * FMath::Min(Mine, Theirs) && A.SparS < 6.f)
			{
				// Matched: a bout first. Head to head, they butt and shove (each butt pushes the other back a step),
				// for a few seconds before one gives way.
				A.SparS += Dt;
				D->SetGaze(HisHead, bHorns ? -0.8f : 0.3f);
				MouthTo(A, HisHead, Dt);
				A.AttackClock -= Dt;
				if (A.AttackClock <= 0.f && FVector::Dist(D->SnoutNow(), HisHead) < 200.f + 0.15f * D->BodyLengthCm())
				{
					A.AttackClock = FMath::FRandRange(1.f, 1.6f);
					D->Bite();
					B.Body->AddActorWorldOffset((B.Body->GetActorLocation() - Me).GetSafeNormal2D() * 35.f, true);
					++Butts;
				}
				A.Thought = FString::Printf(TEXT("sparring with %s over %s"), *B.Name, *Animals[F].Name);
				continue;
			}
			A.SparS = 0.f;
			B.SparS = 0.f;
			const int32 Loser = Mine < Theirs ? Index : j, Winner = Mine < Theirs ? j : Index;
			FBFAnimal& L = Animals[Loser];
			FBFAnimal& W = Animals[Winner];
			++Contests;
			++W.Wins;
			++L.Losses;
			const bool bShove = FMath::Max(Mine, Theirs) < 1.25f * FMath::Min(Mine, Theirs);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: rivals over %s: %s and %s%s; %s gives way"), *Animals[F].Name, *W.Name, *L.Name,
				bShove ? TEXT(" are matched: they shove") : TEXT(" size each other up"), *L.Name);
			L.IgnoreFemale = F;
			L.IgnoreUntil = Now + 180.f;
			L.MateIn = FMath::Max(L.MateIn, 600.f);          // beaten, he keeps out of courting a while (not just away from her)
			L.DisplayS = 0.f;
			Choose(L, EBFAct::Idle, Now, FString::Printf(TEXT("gave way to %s over %s"), *W.Name, *Animals[F].Name));
			if (bShove)
			{
				Hurt(Loser, FMath::FRandRange(0.03f, 0.12f), W.Name, W.Body.Get(), Now, false);
			}
			if (Loser == Index)
			{
				return;
			}
		}
		if (!bRival && A.DisplayS >= 10.f)
		{
			// She takes him: carrying eggs now, due after her gestation; her next clutch a while off.
			FBFAnimal& She = Animals[F];
			She.bGravid = true;
			She.LayIn = Gestation;
			She.ClutchFather = Index;
			She.ClutchIn = BreedEvery;
			++She.Matings;
			++A.Matings;
			++NumMatings;
			A.MateIn = 600.f;
			A.DisplayS = 0.f;
			A.CourtTarget = INDEX_NONE;
			She.Thought = FString::Printf(TEXT("mated with %s: carrying eggs"), *A.Name);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s mated with %s (she lays in %.0f min of the island's time)"), *A.Name, *She.Name, Gestation / 60.f);
			Choose(A, EBFAct::Idle, Now, FString::Printf(TEXT("mated with %s"), *She.Name));
			return;
		}
		if (!bRival)
		{
			A.Thought = FString::Printf(TEXT("displaying to %s (%.0f s)"), *Animals[F].Name, A.DisplayS);
			FBFAnimal& She = Animals[F];
			if (She.Act == EBFAct::Idle || She.Act == EBFAct::Graze || She.Act == EBFAct::Wander || She.Act == EBFAct::Rest || She.Act == EBFAct::Regroup)
			{
				She.Thought = FString::Printf(TEXT("%s is courting her"), *A.Name);
			}
		}
		return;
	}
	// Nesting: a site, a scrape, the eggs laid; then (a guarding or brooding kind) staying with them.
	if (A.bGravid)
	{
		const int32 Care = CareOf(A.Base);
		if (A.NestSite.IsZero())
		{
			// Dry, gentle ground 15 to 70 m off, away from other nests: open ground for the big ones (to see what comes),
			// cover for the brooders (to hide).
			float Best = -1.f;
			A.NestSite = Me;
			for (int32 k = 0; k < 24; ++k)
			{
				const float Ang = FMath::FRandRange(0.f, 2.f * PI);
				const FVector P = Me + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * FMath::FRandRange(1500.f, 7000.f);
				if (!Island->Walkable(P, D->GetSpecies(), 0.f))
				{
					continue;
				}
				bool bCrowded = false;
				for (const FBFNest& O : Nests)
				{
					bCrowded |= O.Eggs > 0 && FVector::Dist2D(O.At, P) < 1500.f;
				}
				const float Cover = FMath::Clamp(Island->CoverAt(FVector2D(P)), 0.f, 1.f);
				const float Worth = bCrowded ? 0.f : Care == 2 ? 0.2f + Cover : 1.2f - FMath::Abs(Cover - 0.2f);
				if (Worth > Best)
				{
					Best = Worth;
					A.NestSite = P;
				}
			}
			A.ScrapeS = 0.f;
			if (!GoTo(A, A.NestSite))
			{
				A.NestSite = Me;          // no way there: here will do
			}
		}
		const float Far = FVector::Dist2D(Me, A.NestSite);
		if (Far > 400.f + D->GetCapsuleComponent()->GetScaledCapsuleRadius() && A.ScrapeS <= 0.f)
		{
			if (!A.bGoal)
			{
				// Arrived as near as the way goes (or it can't be reached): she nests where she is if it's close.
				if (Far < 2500.f || !GoTo(A, A.NestSite))
				{
					A.NestSite = Me;
				}
			}
			A.Thought = FString::Printf(TEXT("carrying eggs: going to a nest site %s off"), *Metres(Far));
			return;
		}
		A.Path.Reset();
		A.bGoal = false;
		if (A.ScrapeS <= 0.f || D->GetIdle() == EBFIdle::None)
		{
			D->StartIdle(EBFIdle::Sniff);
		}
		A.ScrapeS += Dt;
		A.Thought = FString::Printf(TEXT("scraping out a nest (%.0f s)"), A.ScrapeS);
		if (A.ScrapeS > 12.f)
		{
			A.NestSite = Me;
			LayEggs(A, Index, Now);
		}
		return;
	}
	if (Nests.IsValidIndex(A.NestI) && Nests[A.NestI].Eggs > 0)
	{
		const FBFNest& N = Nests[A.NestI];
		const float Far = FVector::Dist2D(Me, N.At);
		const float Left = FMath::Max(0.f, Incubation - N.IncubS) / FMath::Max(GrowRate, 0.001f) / 60.f;
		if (Far > 2500.f)
		{
			if (!A.bGoal || FMath::Frac(Now / 5.f) < Dt / 5.f)
			{
				GoTo(A, N.At);
			}
			A.Thought = FString::Printf(TEXT("going back to her nest, %s off"), *Metres(Far));
			return;
		}
		A.Path.Reset();
		A.bGoal = false;
		if (N.Care == 2)
		{
			// On the eggs: a brooder rests there, warming them.
			A.Tired = FMath::Max(0.f, A.Tired - Dt * NeedRate / 90.f);
			if (Far > 300.f)
			{
				D->AddMovementInput((N.At - Me).GetSafeNormal2D(), 0.4f);
			}
			A.Thought = FString::Printf(TEXT("brooding %d eggs (hatching in %.0f min)"), N.Eggs, Left);
		}
		else
		{
			// A guard stands beside her eggs (about her own length off), not on them, facing out.
			const float Off = 0.8f * D->BodyLengthCm();
			if (Far < Off)
			{
				D->AddMovementInput((Me - N.At).GetSafeNormal2D(), 0.5f);
			}
			A.LookClock -= Dt;
			if (A.LookClock <= 0.f)
			{
				A.LookClock = 8.f;
				D->StartIdle(EBFIdle::Look);
			}
			A.Thought = FString::Printf(TEXT("guarding her nest: %d eggs, hatching in %.0f min"), N.Eggs, Left);
		}
		return;
	}
	Choose(A, EBFAct::Idle, Now, TEXT("nothing to nest for"));
}

void ABFLife::LayEggs(FBFAnimal& A, int32 Index, float Now)
{
	// The clutch by her body: more the bigger she is (3 for a raptor, 6 for a Triceratops or a T. rex, 7 for a
	// Brachiosaurus); each egg about 1/3000 of her weight (a Triceratops' 4 kg).
	const int32 Count = FMath::Clamp(FMath::RoundToInt(1.5f * FMath::LogX(10.f, FMath::Max(A.MassKg, 10.f))), 3, 8);
	const int32 NI = Nests.Num();
	FBFNest& N = Nests.AddDefaulted_GetRef();
	float Ground = A.NestSite.Z;
	Island->GroundAt(FVector2D(A.NestSite), Ground);
	N.At = FVector(A.NestSite.X, A.NestSite.Y, Ground);
	const int32 Up = PerchNear(A.NestSite, 450.f);
	if (Up != INDEX_NONE)
	{
		N.At = Perches[Up].Top;          // up on her perch, in the middle of it
	}
	N.Base = A.Base;
	N.Herd = A.Herd;
	N.Mother = Index;
	N.Father = A.ClutchFather;
	N.Eggs = N.Laid = Count;
	N.EggKg = FMath::Clamp(0.0003f * A.MassKg, 0.2f, 6.f);
	N.Care = CareOf(A.Base);
	N.Carcass = Carcasses.Num();
	FBFCarcass Eggs;
	Eggs.At = N.At;
	Eggs.MeatKg = Count * N.EggKg;
	Eggs.Time = Now;
	Eggs.Of = FString::Printf(TEXT("a %s nest"), *Herds[A.Herd].Kind);
	Eggs.Nest = NI;
	Carcasses.Add(Eggs);
	DrawEggs(N);
	A.bGravid = false;
	A.ScrapeS = 0.f;
	A.NestSite = FVector::ZeroVector;
	++A.Clutches;
	EggsLaid += Count;
	A.NestI = N.Care > 0 ? NI : INDEX_NONE;
	BodyIndex(StageKey(A.Base, 1));         // the hatchlings' body, ready by the time they hatch
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s laid %d eggs (%.1f kg each) at %s; she %s them"), *A.Name, Count, N.EggKg, *N.At.ToString(),
		N.Care == 0 ? TEXT("leaves") : N.Care == 2 ? TEXT("broods") : TEXT("guards"));
	if (N.Care == 0)
	{
		Choose(A, EBFAct::Regroup, Now, FString::Printf(TEXT("laid %d eggs and left them, as her kind does"), Count));
	}
	else
	{
		A.Thought = FString::Printf(TEXT("laid %d eggs: staying with them"), Count);
	}
}

void ABFLife::DrawEggs(FBFNest& N)
{
	// The eggs in a ring in a shallow scrape (the engine's own sphere and cylinder: eggshell white, earth brown).
	if (!N.EggComp)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UStaticMesh* Disc = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Sphere || !Disc)
		{
			return;
		}
		N.EggComp = NewObject<UInstancedStaticMeshComponent>(this);
		N.EggComp->SetStaticMesh(Sphere);
		N.EggComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		N.EggComp->SetCanEverAffectNavigation(false);
		N.Scrape = NewObject<UStaticMeshComponent>(this);
		N.Scrape->SetStaticMesh(Disc);
		N.Scrape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		N.Scrape->SetCanEverAffectNavigation(false);
		N.Scrape->SetCastShadow(false);
		if (Base)
		{
			UMaterialInstanceDynamic* Shell = UMaterialInstanceDynamic::Create(Base, this);
			Shell->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.82f, 0.78f, 0.68f));
			N.EggComp->SetMaterial(0, Shell);
			UMaterialInstanceDynamic* Earth = UMaterialInstanceDynamic::Create(Base, this);
			Earth->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.07f, 0.045f, 0.025f));
			Earth->SetScalarParameterValue(TEXT("Roughness"), 1.f);          // (smooth, the flat disc mirrored the sky: grey)
			Shell->SetScalarParameterValue(TEXT("Roughness"), 0.8f);
			N.Scrape->SetMaterial(0, Earth);
		}
		N.EggComp->SetupAttachment(GetRootComponent());
		N.Scrape->SetupAttachment(GetRootComponent());
		N.EggComp->RegisterComponent();
		N.Scrape->RegisterComponent();
		const float EggCm = 12.4f * FMath::Pow(N.EggKg, 1.f / 3.f);
		N.Scrape->SetWorldLocation(N.At + FVector(0.f, 0.f, 1.f));
		const float NestCm = N.Base == TEXT("ptera") ? 150.f : EggCm * 4.4f;          // (a flier's: a bowl of twigs it can sit in)
		N.Scrape->SetWorldScale3D(FVector(NestCm / 100.f, NestCm / 100.f, N.Base == TEXT("ptera") ? 0.25f : 0.1f));
	}
	N.EggComp->ClearInstances();
	const float EggCm = 12.4f * FMath::Pow(N.EggKg, 1.f / 3.f);
	for (int32 e = 0; e < N.Eggs; ++e)
	{
		// The first five round the middle, the rest in an outer ring; long axis outward.
		const int32 Ring = e < 5 ? 0 : 1;
		const int32 InRing = Ring == 0 ? FMath::Min(N.Laid, 5) : FMath::Max(N.Laid - 5, 1);
		const float Ang = 2.f * PI * ((Ring == 0 ? e : e - 5) + 0.5f * Ring) / InRing;
		const float Rad = EggCm * (Ring == 0 ? 0.9f : 1.8f);
		const FVector P = N.At + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * Rad + FVector(0.f, 0.f, 0.4f * EggCm);
		N.EggComp->AddInstance(FTransform(FRotator(0.f, FMath::RadiansToDegrees(Ang), 0.f), P, FVector(1.35f, 1.f, 1.f) * EggCm / 100.f), true);
	}
}

void ABFLife::TickNests(float Dt, float Now)
{
	for (int32 i = 0; i < Nests.Num(); ++i)
	{
		FBFNest& N = Nests[i];
		if (N.Eggs <= 0)
		{
			continue;
		}
		N.IncubS += Dt * GrowRate;
		if (N.IncubS >= Incubation)
		{
			N.HatchClock -= Dt;
			if (N.HatchClock <= 0.f)
			{
				N.HatchClock = 0.4f;          // one at a time
				HatchOne(i, Now);
			}
		}
	}
}

void ABFLife::HatchOne(int32 NI, float Now)
{
	const FString Key = StageKey(Nests[NI].Base, 1);
	const int32 SI = BodyIndex(Key);
	if (SI == INDEX_NONE)
	{
		if (BodyFailed.Contains(Key))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BoneForge] life: %s eggs can't hatch: no hatchling body %s"), *Nests[NI].Base, *Key);
			Nests[NI].Eggs = 0;
			DrawEggs(Nests[NI]);
			if (Animals.IsValidIndex(Nests[NI].Mother) && Animals[Nests[NI].Mother].NestI == NI)
			{
				Animals[Nests[NI].Mother].NestI = INDEX_NONE;
			}
		}
		return;            // its body is on its way
	}
	// Its mother's herd if she minds her young (and lives); a lay-and-leave clutch keeps together as its own group.
	const int32 Mum = Nests[NI].Mother;
	const bool bMother = Nests[NI].Care > 0 && Animals.IsValidIndex(Mum) && !Animals[Mum].bDead && Animals[Mum].Body.IsValid();
	int32 HerdI = bMother ? Animals[Mum].Herd : Nests[NI].YoungHerd;
	if (!Herds.IsValidIndex(HerdI))
	{
		HerdI = Herds.Num();
		FBFHerd& H = Herds.AddDefaulted_GetRef();
		H.Kind = Herds.IsValidIndex(Nests[NI].Herd) ? Herds[Nests[NI].Herd].Kind : Nests[NI].Base;
		H.Home = Nests[NI].At;
		Nests[NI].YoungHerd = HerdI;
	}
	const bool bHunterKind = Nests[NI].Base == TEXT("trex") || Nests[NI].Base == TEXT("raptor") || (Animals.IsValidIndex(Mum) && Animals[Mum].bHunter);
	FRandomStream R(NI * 131 + Nests[NI].Hatched * 17 + 3);
	const float Ang = R.FRandRange(0.f, 2.f * PI);
	const bool bUp = PerchNear(Nests[NI].At, 100.f) != INDEX_NONE;          // (in a nest up a tree: close by, on the shelf)
	const FVector At = Nests[NI].At + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * (bUp ? R.FRandRange(40.f, 110.f) : R.FRandRange(100.f, 300.f));
	const int32 I = AddAnimal(SI, HerdI, At, R.FRandRange(0.f, 360.f), TEXT("hatchling"), bHunterKind, R, false);
	if (I == INDEX_NONE)
	{
		return;
	}
	FBFNest& N = Nests[NI];
	FBFAnimal& Y = Animals[I];
	Y.Stage = 1;
	Y.AgeS = 0.f;
	Y.bYoung = true;
	Y.Base = N.Base;
	Y.Mother = bMother ? Mum : INDEX_NONE;
	Y.Father = N.Father;
	if (bMother)
	{
		Y.Memory = Animals[Mum].Memory;          // it learns its mother's range (her food and water and danger)
	}
	Y.bFemale = R.FRand() < 0.5f;
	Y.bFlier = (bMother && Animals[Mum].bFlier) || N.Base == TEXT("ptera");       // a flier's young fly (and are fed till they can)
	{
		const bool bMumKnown = Animals.IsValidIndex(N.Mother), bDadKnown = Animals.IsValidIndex(N.Father);
		const FBFGenes MumG = bMumKnown ? Animals[N.Mother].Genes : FounderGenes(R);
		const FBFGenes DadG = bDadKnown ? Animals[N.Father].Genes : MumG;
		Y.Genes = ChildGenes(MumG, DadG, R);
		Y.Generation = 1 + FMath::Max(bMumKnown ? Animals[N.Mother].Generation : 0, bDadKnown ? Animals[N.Father].Generation : 0);
	}
	++Herds[HerdI].YoungBorn;
	Y.Name = FString::Printf(TEXT("Hatchling %s %d"), *Herds[HerdI].Kind, NextNumber(Herds[HerdI].Kind));
	ApplyBody(Y);
	ApplyGenes(Y);
	if (bMother)
	{
		Y.Spacing = 0.6f * Animals[Mum].Body->BodyLengthCm();
	}
	--N.Eggs;
	++N.Hatched;
	++Births;
	if (Carcasses.IsValidIndex(N.Carcass))
	{
		Carcasses[N.Carcass].MeatKg = N.Eggs * N.EggKg;
	}
	DrawEggs(N);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s hatched (%s, %d of %d; %s; generation %d; %s; mother %s, father %s)"), *Y.Name, Y.bFemale ? TEXT("female") : TEXT("male"),
		N.Hatched, N.Laid, bMother ? *FString::Printf(TEXT("its mother %s is with it"), *Animals[Mum].Name) : TEXT("on its own with its clutch"), Y.Generation, *GenesText(Y.Genes),
		Animals.IsValidIndex(N.Mother) ? *GenesText(Animals[N.Mother].Genes) : TEXT("-"), Animals.IsValidIndex(N.Father) ? *GenesText(Animals[N.Father].Genes) : TEXT("-"));
	if (N.Eggs == 0 && Animals.IsValidIndex(Mum) && Animals[Mum].NestI == NI)
	{
		Animals[Mum].NestI = INDEX_NONE;          // her young are out: she minds them now
	}
}

void ABFLife::Toll(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	// The breeding clocks (on the growth clock: the checks run them faster), and what kills without a hunter: hunger and
	// thirst (an empty stomach or a parched throat wears its health down; fed and watered it heals slowly), and, for the
	// young, the chances of the wild (most hatchlings never grow up).
	const float G = Dt * GrowRate;
	A.ClutchIn = FMath::Max(0.f, A.ClutchIn - G);
	A.MateIn = FMath::Max(0.f, A.MateIn - G);
	if (A.bGravid)
	{
		A.LayIn = FMath::Max(0.f, A.LayIn - G);
	}
	// Old age: a lifespan about three times its growing up, each its own (85% to 115%).
	if (A.Stage >= 4 && Lifespan > 0.f && A.AgeS > Lifespan * (0.85f + 0.3f * float(GetTypeHash(A.Name) % 1000) / 1000.f))
	{
		++OldAge;
		Die(Index, TEXT("died of old age"), Now);
		return;
	}
	const float Rate = NeedRate * Dt;
	A.Condition = FMath::Lerp(A.Condition, A.StomachMaxKg > 0.f ? A.StomachKg / A.StomachMaxKg : 0.f, FMath::Min(1.f, Rate / 1200.f));
	if (A.StomachKg <= 0.f || A.Thirst >= 1.f)
	{
		// Two island days from an empty stomach to death, one parched (10 minutes was far too quick: a check's young that
		// spent it running starved).
		A.Health -= Rate * (A.Thirst >= 1.f ? 1.f / 1440.f : 1.f / 2880.f);
		if (A.Health <= 0.f)
		{
			++Starved;
			Die(Index, A.StomachKg <= 0.f ? TEXT("starved") : TEXT("died of thirst"), Now);
			return;
		}
	}
	else if (A.StomachKg > 0.4f * A.StomachMaxKg && A.Thirst < 0.6f && A.Health < 1.f)
	{
		A.Health = FMath::Min(1.f, A.Health + Rate / 1200.f);
		if (A.Body.IsValid())
		{
			A.Body->SetLimp(FMath::Clamp((0.75f - A.Health) / 0.5f, 0.f, 1.f));
		}
	}
	if (A.Stage <= 2 && YoungDeathRate > 0.f)
	{
		if (A.Fate < 0.f)
		{
			A.Fate = -FMath::Loge(FMath::Max(FMath::FRand(), 1e-4f));
		}
		A.Risk += (A.Stage == 1 ? 1.7e-4f : 4e-5f) * YoungDeathRate * G;     // a hatchling's stage survived 2 in 5, a juvenile's 7 in 10
		if (A.Risk >= A.Fate)
		{
			++DiedYoung;
			Die(Index, TEXT("didn't make it"), Now);
		}
	}
}

int32 ABFLife::TestKill(int32 Victim, int32 Killer)
{
	if (!Animals.IsValidIndex(Victim) || !Animals.IsValidIndex(Killer) || !Animals[Killer].Body.IsValid())
	{
		return INDEX_NONE;
	}
	Hurt(Victim, 5.f, Animals[Killer].Name, Animals[Killer].Body.Get(), GetWorld()->GetTimeSeconds(), true);
	return Carcasses.Num() - 1;
}

void ABFLife::DressCarcass(int32 C, int32 Animal)
{
	if (!Carcasses.IsValidIndex(C) || !Animals.IsValidIndex(Animal) || !Animals[Animal].Body.IsValid())
	{
		return;
	}
	FBFCarcass& K = Carcasses[C];
	K.Animal = Animal;
	K.StartKg = K.MeatKg;
	K.LenCm = Animals[Animal].Body->BodyLengthCm();
}

void ABFLife::CarcassBones(FBFCarcass& K, const ABFDino& Body)
{
	// Each bone from its parent to itself, where the body lies now: a rod as thick as a tenth of its length (thicker
	// in the legs and spine than a thin rig's toes); the skull an egg from the head to the snout's tip; and on the trunk's
	// bones, ribs, arching out and down each side.
	const USkinnedMeshComponent* M = Body.PoseMesh();
	const USkeletalMesh* Mesh = Body.GetSpecies().Mesh;
	if (!M || !Mesh)
	{
		return;
	}
	const FReferenceSkeleton& RS = Mesh->GetRefSkeleton();
	const float L = FMath::Max(K.LenCm, 50.f);
	auto Rod = [&](const FVector& P, const FVector& Q, float R)
	{
		const float Len = float(FVector::Dist(P, Q));
		if (Len < 3.f)
		{
			return;
		}
		K.BoneXf.Add(FTransform(FRotationMatrix::MakeFromZ(Q - P).Rotator(), 0.5f * (P + Q), FVector(2.f * R / 100.f, 2.f * R / 100.f, Len / 100.f)));
	};
	K.BoneXf.Reset();
	const FVector Right = M->GetRightVector(), Down = -M->GetUpVector();
	for (int32 i = 0; i < RS.GetNum(); ++i)
	{
		const int32 Par = RS.GetParentIndex(i);
		const FName Name = RS.GetBoneName(i);
		const FString S = Name.ToString();
		if (Par == INDEX_NONE || RS.GetBoneName(Par) == TEXT("root") || S == TEXT("jaw_end"))
		{
			continue;
		}
		const FVector P = M->GetBoneLocation(RS.GetBoneName(Par)), Q = M->GetBoneLocation(Name);
		const float Len = float(FVector::Dist(P, Q));
		if (S == TEXT("head_end"))
		{
			// the skull: an egg from just behind the head joint to the snout's tip, narrower than it's long
			K.SkullXf = FTransform(FRotationMatrix::MakeFromX(Q - P).Rotator(), FMath::Lerp(P, Q, 0.45f), FVector(1.15f * Len / 100.f, 0.42f * Len / 100.f, 0.38f * Len / 100.f));
			continue;
		}
		Rod(P, Q, FMath::Clamp(0.1f * Len, 1.f, 0.025f * L));
		if (S.StartsWith(TEXT("spine")) || S == TEXT("chest"))
		{
			// The ribs: a cage round where its trunk was, each an arc out from the backbone, round and in under it, the
			// deepest in the middle of the chest.
			const float Half = 0.5f * float(Body.GetSpecies().Bounds.GetSize().Y) * float(M->GetComponentScale().X);
			const float R = FMath::Clamp(0.006f * L, 0.6f, 4.f);
			for (const float Side : { -1.f, 1.f })
			{
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector Base = FMath::Lerp(P, Q, 0.17f + 0.33f * k);
					const FVector A1 = Base + Right * Side * 0.55f * Half + Down * 0.15f * Half;
					const FVector A2 = Base + Right * Side * 0.85f * Half + Down * 0.65f * Half;
					const FVector A3 = Base + Right * Side * 0.6f * Half + Down * 1.2f * Half;
					Rod(Base, A1, R);
					Rod(A1, A2, R);
					Rod(A2, A3, 0.8f * R);
				}
			}
		}
	}
	K.BonesAt = GetWorld()->GetTimeSeconds();
}

void ABFLife::TickCarcasses(float Dt, float Now)
{
	int32 BonesShown = 0, PoolsShown = 0;
	for (int32 c = 0; c < Carcasses.Num(); ++c)
	{
		FBFCarcass& K = Carcasses[c];
		if (K.Animal == INDEX_NONE || K.StartKg <= 0.f)
		{
			continue;
		}
		BonesShown += K.Bones ? 1 : 0;
		PoolsShown += K.Pools ? 1 : 0;
		K.DrawClock -= Dt;
		if (K.DrawClock > 0.f)
		{
			continue;
		}
		K.DrawClock = 0.5f;
		const float Age = Now - K.Time;
		const float Eaten = 1.f - FMath::Clamp(K.MeatKg / K.StartKg, 0.f, 1.f);
		FBFAnimal* A = Animals.IsValidIndex(K.Animal) ? &Animals[K.Animal] : nullptr;
		// Where the meat is: on the body as it fell (the ragdoll slides and rolls metres from where it was struck), the
		// middle of its trunk, on the ground.
		if (A && A->Body.IsValid() && K.BonesAt < 0.f)
		{
			if (const USkinnedMeshComponent* PM = A->Body->PoseMesh())
			{
				const FVector Mid = 0.5f * (PM->GetBoneLocation(TEXT("pelvis")) + PM->GetBoneLocation(TEXT("chest")));
				if (!Mid.IsNearlyZero())
				{
					float Z = float(Mid.Z);
					if (Island.IsValid() && Island->GroundAt(FVector2D(Mid), Z))
					{
						K.At = FVector(Mid.X, Mid.Y, Z);
						K.FleshZ = FMath::Max(float(Mid.Z), Z + 20.f);
					}
				}
			}
		}
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		auto MakeISM = [&](const TCHAR* MeshPath, UMaterialInstanceDynamic*& Mat)
		{
			UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
			I->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
			I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			I->SetCanEverAffectNavigation(false);
			if (Base)
			{
				Mat = UMaterialInstanceDynamic::Create(Base, this);
				I->SetMaterial(0, Mat);
			}
			I->SetupAttachment(GetRootComponent());
			I->RegisterComponent();
			return I;
		};
		// The blood: pools under the body that spread over the first minute and a half, more of them the more it's
		// been torn into; bright and wet, drying dark over ten minutes; gone after an hour.
		if (!K.Pools)
		{
			UMaterialInstanceDynamic* Mat = nullptr;
			K.Pools = MakeISM(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), Mat);
			K.Pools->SetCastShadow(false);
			K.PoolMat = Mat;
		}
		{
			const float Dry = FMath::SmoothStep(60.f, 600.f, Age);
			if (K.PoolMat)
			{
				K.PoolMat->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor(0.045f, 0.0015f, 0.0015f), FLinearColor(0.018f, 0.006f, 0.004f), Dry));
				K.PoolMat->SetScalarParameterValue(TEXT("Roughness"), FMath::Lerp(0.12f, 0.9f, Dry));
			}
			const float Fade = 1.f - FMath::SmoothStep(3000.f, 3600.f, Age);
			const float Spread = FMath::Min(Age / 90.f, 1.f);
			const float Yaw = A && A->Body.IsValid() ? float(A->Body->GetActorRotation().Yaw) : 0.f;
			const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector(), Side = FVector::CrossProduct(Fwd, FVector::UpVector);
			FRandomStream R(c * 7919 + 11);
			K.Pools->ClearInstances();
			for (int32 p = 0; p < 7 && Fade > 0.01f; ++p)
			{
				const float U = R.FRandRange(-0.3f, 0.3f), V = R.FRandRange(-0.15f, 0.15f), Size = R.FRandRange(0.05f, 0.11f);
				if (p >= 3 && Eaten < 0.12f * (p - 2))
				{
					continue;
				}
				const FVector At = K.At + Fwd * U * K.LenCm + Side * V * K.LenCm;
				float Z = float(At.Z);
				if (!Island.IsValid() || !Island->GroundAt(FVector2D(At), Z))
				{
					continue;
				}
				const float Rad = Size * K.LenCm * (0.3f + 0.7f * Spread) * (0.7f + 0.6f * Eaten) * Fade;
				K.Pools->AddInstance(FTransform(FRotator(0.f, R.FRandRange(0.f, 180.f), 0.f), FVector(At.X, At.Y, Z + 1.5f),
					FVector(2.f * Rad / 100.f, 1.4f * Rad / 100.f, 0.01f)), true);
			}
		}
		// The bones: once it's eaten down past a third, the body gives way to its skeleton as it lay.
		if (K.BonesAt < 0.f && A && A->Body.IsValid() && Eaten > 0.65f)
		{
			CarcassBones(K, *A->Body);
			A->Body->Destroy();
			A->Body = nullptr;
			A->Label = nullptr;
		}
		if (K.BonesAt >= 0.f)
		{
			if (!K.Bones)
			{
				UMaterialInstanceDynamic* Mat = nullptr;
				K.Bones = MakeISM(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), Mat);
				K.BoneMat = Mat;
				UMaterialInstanceDynamic* Unused = nullptr;
				K.Skull = MakeISM(TEXT("/Engine/BasicShapes/Sphere.Sphere"), Unused);
				if (K.BoneMat)
				{
					K.Skull->SetMaterial(0, K.BoneMat);
				}
			}
			const float BoneAge = Now - K.BonesAt;
			const float Clean = FMath::Max(FMath::SmoothStep(0.65f, 1.f, Eaten), FMath::SmoothStep(300.f, 1500.f, BoneAge));
			if (K.BoneMat)
			{
				K.BoneMat->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor(0.12f, 0.02f, 0.016f), FLinearColor(0.5f, 0.45f, 0.36f), Clean));
				K.BoneMat->SetScalarParameterValue(TEXT("Roughness"), FMath::Lerp(0.7f, 0.9f, Clean));          // (smooth, they mirrored the sky: blue-grey)
			}
			// Gone into the ground after an hour and a half.
			const float Sink = FMath::SmoothStep(4000.f, 5400.f, BoneAge);
			K.Bones->ClearInstances();
			K.Skull->ClearInstances();
			if (Sink < 0.99f && !K.SkullXf.GetScale3D().IsNearlyZero())
			{
				FTransform S = K.SkullXf;
				S.AddToTranslation(FVector(0.f, 0.f, -60.f * Sink));
				K.Skull->AddInstance(S, true);
			}
			if (Sink < 0.99f)
			{
				for (const FTransform& T : K.BoneXf)
				{
					FTransform S = T;
					S.AddToTranslation(FVector(0.f, 0.f, -60.f * Sink));
					K.Bones->AddInstance(S, true);
				}
			}
		}
	}
	CarcassBonesShown = BonesShown;
	CarcassPoolsShown = PoolsShown;
}

void ABFLife::Die(int32 Index, const FString& How, float Now)
{
	FBFAnimal& V = Animals[Index];
	if (V.bDead || !V.Body.IsValid())
	{
		return;
	}
	V.bDead = true;
	V.Health = 0.f;
	if (!V.bDormant)
	{
		V.Body->StartRagdoll();         // (far off its body is put away: it just lies where it was)
	}
	Carcasses.Add({ V.Body->GetActorLocation(), 0.6f * V.MassKg, Now, V.Name });
	DressCarcass(Carcasses.Num() - 1, Index);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s %s (%.0f kg)"), *V.Name, *How, V.MassKg);
}

FString ABFLife::BreedingText() const
{
	int32 WithEggs = 0, Gravid = 0;
	for (const FBFNest& N : Nests)
	{
		WithEggs += N.Eggs > 0 ? 1 : 0;
	}
	for (const FBFAnimal& A : Animals)
	{
		Gravid += !A.bDead && A.bGravid ? 1 : 0;
	}
	int32 Gen = 0;
	for (const FBFAnimal& A : Animals)
	{
		Gen = !A.bDead ? FMath::Max(Gen, A.Generation) : Gen;
	}
	FString Kinds;
	TArray<FString> Keys;
	KindInfo.GetKeys(Keys);
	Keys.Sort();
	for (const FString& K : Keys)
	{
		Kinds += FString::Printf(TEXT("%s%s %d/%d"), Kinds.IsEmpty() ? TEXT("") : TEXT(", "), *K, KindInfo[K].Alive, RoomFor(K));
	}
	int32 Kills = 0;
	for (const FBFAnimal& A : Animals)
	{
		Kills += A.Kills;
	}
	return FString::Printf(TEXT("Matings %d (rival contests %d), %d carrying eggs, %d nests with eggs; eggs laid %d, hatched %d, eaten %d; died young %d, starved %d, of old age %d; %d alive (room for %d: %s), generations to %d, newcomers %d; hunters' charges %d, kills %d, ambushes %d; herd moves %d, splits %d"),
		NumMatings, Contests, Gravid, WithEggs, EggsLaid, Births, EggsEaten, DiedYoung, Starved, OldAge, AliveNow, MaxAnimals, *Kinds, Gen, Newcomers, Charges, Kills, Ambushes, HerdMoves, Splits);
}

int32 ABFLife::NextNumber(const FString& Kind) const
{
	int32 Most = 0;
	const FString Tail = Kind + TEXT(" ");
	for (const FBFAnimal& A : Animals)
	{
		const int32 At = A.Name.Find(Tail, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At != INDEX_NONE && (At == 0 || A.Name[At - 1] == TEXT(' ')))
		{
			const FString Num = A.Name.Mid(At + Tail.Len());
			if (Num.IsNumeric())
			{
				Most = FMath::Max(Most, FCString::Atoi(*Num));
			}
		}
	}
	return Most + 1;
}

// ---------------------------------------------------------------------------------------------- inheritance (life cycle step 7)

namespace
{
	/** A spread round 0 like a bell (three uniforms summed), of about Sigma. */
	float Bell(FRandomStream& R, float Sigma)
	{
		return Sigma * (R.FRandRange(-1.f, 1.f) + R.FRandRange(-1.f, 1.f) + R.FRandRange(-1.f, 1.f));
	}

	FBFGenes Clamped(FBFGenes G)
	{
		G.Size = FMath::Clamp(G.Size, 0.9f, 1.1f);
		G.Hue = FMath::Clamp(G.Hue, -25.f, 25.f);
		G.Shade = FMath::Clamp(G.Shade, -0.2f, 0.2f);
		G.Strength = FMath::Clamp(G.Strength, 0.8f, 1.2f);
		G.Speed = FMath::Clamp(G.Speed, 0.88f, 1.12f);
		return G;
	}
}

FBFGenes ABFLife::FounderGenes(FRandomStream& R)
{
	FBFGenes G;
	G.Size = 1.f + Bell(R, 0.03f);
	G.Hue = Bell(R, 7.f);
	G.Shade = Bell(R, 0.05f);
	G.Strength = 1.f + Bell(R, 0.05f);
	G.Speed = 1.f + Bell(R, 0.03f);
	return Clamped(G);
}

FBFGenes ABFLife::ChildGenes(const FBFGenes& Mum, const FBFGenes& Dad, FRandomStream& R)
{
	// The mean of the two, and a little either way (less than the founders' spread: a family resembles itself).
	FBFGenes G;
	G.Size = 0.5f * (Mum.Size + Dad.Size) + Bell(R, 0.015f);
	G.Hue = 0.5f * (Mum.Hue + Dad.Hue) + Bell(R, 3.f);
	G.Shade = 0.5f * (Mum.Shade + Dad.Shade) + Bell(R, 0.025f);
	G.Strength = 0.5f * (Mum.Strength + Dad.Strength) + Bell(R, 0.025f);
	G.Speed = 0.5f * (Mum.Speed + Dad.Speed) + Bell(R, 0.015f);
	return Clamped(G);
}

FString ABFLife::GenesText(const FBFGenes& G)
{
	return FString::Printf(TEXT("size %.2f, hue %+.0f, shade %+.2f, strength %.2f, speed %.2f"), G.Size, G.Hue, G.Shade, G.Strength, G.Speed);
}

void ABFLife::ApplyGenes(FBFAnimal& A)
{
	ABFDino* D = A.Body.Get();
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	if (!D || !GM)
	{
		return;
	}
	D->SetActorScale3D(FVector(A.Genes.Size));
	D->SpeedGene = A.Genes.Speed;
	D->ApplyPace();
	FBFLook L = GM->LookOf(A.Base.IsEmpty() ? D->GetSpecies().Key : A.Base);
	L.HueDeg += A.Genes.Hue;
	L.Shade += A.Genes.Shade;
	D->ApplyLook(L);
	A.FastCm = D->GetSpecies().Fast.SpeedCm;
}

FString ABFLife::GeneRows(float Clock) const
{
	// One CSV row a kind: time, kind, how many live, the generations (mean, most), and each gene's mean and spread.
	TMap<FString, TArray<const FBFAnimal*>> ByKind;
	for (const FBFAnimal& A : Animals)
	{
		if (!A.bDead && A.Body.IsValid())
		{
			ByKind.FindOrAdd(A.Base).Add(&A);
		}
	}
	FString Out;
	TArray<FString> Kinds;
	ByKind.GetKeys(Kinds);
	Kinds.Sort();
	for (const FString& K : Kinds)
	{
		const TArray<const FBFAnimal*>& L = ByKind[K];
		const float N = float(L.Num());
		float Gen = 0.f, MaxGen = 0.f, Adults = 0.f;
		float Sum[5] = {}, Sq[5] = {};
		for (const FBFAnimal* A : L)
		{
			Gen += A->Generation;
			MaxGen = FMath::Max(MaxGen, float(A->Generation));
			Adults += A->Stage >= 4 ? 1.f : 0.f;
			const float V[5] = { A->Genes.Size, A->Genes.Hue, A->Genes.Shade, A->Genes.Strength, A->Genes.Speed };
			for (int32 g = 0; g < 5; ++g)
			{
				Sum[g] += V[g];
				Sq[g] += V[g] * V[g];
			}
		}
		Out += FString::Printf(TEXT("%.0f,%s,%.0f,%.0f,%.2f,%.0f"), Clock, *K, N, Adults, Gen / N, MaxGen);
		for (int32 g = 0; g < 5; ++g)
		{
			const float Mean = Sum[g] / N;
			Out += FString::Printf(TEXT(",%.4f,%.4f"), Mean, FMath::Sqrt(FMath::Max(Sq[g] / N - Mean * Mean, 0.f)));
		}
		Out += TEXT("\n");
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------- balance

int32 ABFLife::RoomFor(const FString& Kind) const
{
	int32 HunterKinds = 0, EaterKinds = 0;
	for (const TPair<FString, FKindInfo>& K : KindInfo)
	{
		(K.Value.bHunter ? HunterKinds : EaterKinds) += 1;
	}
	const FKindInfo* Mine = KindInfo.Find(Kind);
	if (!Mine)
	{
		return MaxAnimals;
	}
	const float Side = Mine->bHunter ? HunterShare : 1.f - HunterShare;
	const int32 Share = FMath::Max(4, FMath::RoundToInt(MaxAnimals * Side / FMath::Max(Mine->bHunter ? HunterKinds : EaterKinds, 1)));
	if (!Mine->bHunter)
	{
		return Share;
	}
	// Hunters live on the plant eaters: room for about one to every seven of them (shared between the hunting kinds).
	int32 Prey = 0;
	for (const TPair<FString, FKindInfo>& K : KindInfo)
	{
		Prey += K.Value.bHunter ? 0 : K.Value.Alive;
	}
	return FMath::Clamp(FMath::RoundToInt(Prey / 7.f / FMath::Max(HunterKinds, 1)), 2, Share);
}

void ABFLife::TickBalance(float Dt, float Now)
{
	// Count each kind (every kind ever seen stays known: one that has died out is still owed its newcomers).
	for (TPair<FString, FKindInfo>& K : KindInfo)
	{
		K.Value.Alive = K.Value.Females = K.Value.Males = 0;
	}
	for (const FBFAnimal& A : Animals)
	{
		if (A.Base.IsEmpty() || (A.bDead && KindInfo.Contains(A.Base)))
		{
			continue;
		}
		FKindInfo& K = KindInfo.FindOrAdd(A.Base);
		if (A.bDead)
		{
			continue;
		}
		K.bHunter = A.bHunter;
		if (A.Stage >= 4 && K.SpeciesI == INDEX_NONE && A.Body.IsValid())
		{
			K.SpeciesI = A.SpeciesIndex;
		}
		K.Alive += A.Body.IsValid() || A.bAwaiting ? 1 : 0;
		if (A.Stage >= 4)
		{
			(A.bFemale ? K.Females : K.Males) += 1;
		}
	}
	// A plant eater herd grown past 10: the newer half of its grown animals, with their young, leave to found their own
	// herd 300 to 600 m off (a crowded herd strips its ground faster than it grows back).
	for (int32 H = 0; H < Herds.Num(); ++H)
	{
		TArray<int32> Live, Grown;
		for (const int32 M : Herds[H].Members)
		{
			if (!Animals[M].bDead && Animals[M].Body.IsValid() && !Animals[M].bHunter)
			{
				Live.Add(M);
				if (Animals[M].Stage >= 3)
				{
					Grown.Add(M);
				}
			}
		}
		if (Live.Num() <= 10 || Grown.Num() < 4)
		{
			continue;
		}
		const int32 NewH = Herds.Num();
		FBFHerd& NewHerd = Herds.AddDefaulted_GetRef();
		NewHerd.Kind = Herds[H].Kind;
		FRandomStream R(int32(Now * 7.f) + H * 31);
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		NewHerd.Home = Herds[H].Home + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * R.FRandRange(30000.f, 60000.f);
		NewHerd.MovedAt = Now;
		TArray<int32> Going(Grown.GetData() + Grown.Num() / 2, Grown.Num() - Grown.Num() / 2);
		for (const int32 M : Live)
		{
			if (Animals[M].Stage < 3 && Going.Contains(Animals[M].Mother))
			{
				Going.Add(M);         // her young go with her
			}
		}
		for (const int32 M : Going)
		{
			Herds[H].Members.Remove(M);
			Herds[NewH].Members.Add(M);
			Animals[M].Herd = NewH;
			Choose(Animals[M], EBFAct::Wander, Now, TEXT("leaving with part of the herd for fresh grazing"));
			GoTo(Animals[M], Herds[NewH].Home + FVector(R.FRandRange(-1500.f, 1500.f), R.FRandRange(-1500.f, 1500.f), 0.f));
		}
		Herds[NewH].Leader = Herds[NewH].Members[0];
		Herds[H].Leader = Herds[H].Members.Num() > 0 ? Herds[H].Members[0] : 0;
		++Splits;
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: the %s herd split: %d left to graze %.0f m off"), *Herds[NewH].Kind, Going.Num(), FVector::Dist2D(Herds[NewH].Home, Herds[H].Home) / 100.f);
		break;          // one a second
	}
	// A kind down to its last one, or with no grown animal of a sex (and no young growing up to fill it): a newcomer.
	for (TPair<FString, FKindInfo>& K : KindInfo)
	{
		FKindInfo& I = K.Value;
		int32 YoungComing = 0;
		for (const FBFAnimal& A : Animals)
		{
			YoungComing += !A.bDead && A.Base == K.Key && A.Stage < 4 ? 1 : 0;
		}
		const bool bShort = I.Alive < 2 || ((I.Females == 0 || I.Males == 0) && YoungComing < 2);
		if (!bShort || I.SpeciesI == INDEX_NONE)
		{
			I.Wait = 0.f;
			continue;
		}
		I.Wait += Dt * GrowRate;
		if (I.Wait < ImmigrateS)
		{
			continue;
		}
		I.Wait = 0.f;
		// Near the kind's own herd if any is left (300 m off), else somewhere 500 m from the island's middle.
		FVector Near = Island.IsValid() ? Island->StartPoint() : FVector::ZeroVector;
		for (const FBFAnimal& A : Animals)
		{
			if (!A.bDead && A.Base == K.Key && A.Body.IsValid())
			{
				Near = A.Body->GetActorLocation();
				break;
			}
		}
		FRandomStream R(int32(Now * 13.f) + Newcomers * 101);
		const int32 Before = Animals.Num();
		const bool bWantMale = I.Males == 0 && I.Females > 0;
		if (AddHerd(GetWorld()->GetAuthGameMode<ABFGameMode>()->GetSpecies()[I.SpeciesI].Key, 1, Near, I.Alive > 0 ? 30000.f : 50000.f, R.FRandRange(0.f, 360.f), I.bHunter, R, false) > 0
			&& Animals.IsValidIndex(Before))
		{
			FBFAnimal& New = Animals[Before];
			New.bFemale = !bWantMale && (I.Females == 0 || R.FRand() < 0.5f);
			New.Name = FString::Printf(TEXT("%s %d"), *Herds[New.Herd].Kind, NextNumber(Herds[New.Herd].Kind));
			New.AgeS = 32400.f;      // just grown
			New.ClutchIn = 0.f;
			++Newcomers;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: a newcomer: %s (%s, %s) came over from across the island (its kind: %d alive, %d grown females, %d grown males)"),
				*New.Name, New.bFemale ? TEXT("female") : TEXT("male"), *GenesText(New.Genes), I.Alive, I.Females, I.Males);
		}
	}
}

FString ABFLife::WhyNotText() const
{
	// Why the grown females aren't in condition, kind by kind; how the grown males stand; what the hunters are doing.
	TMap<FString, FString> Out;
	TMap<FString, TArray<int32>> Counts;     // ready, gravid, nest, clutch wait, hungry, thirsty, unwell, no room | males: ready, cooling, hungry
	for (const FBFAnimal& A : Animals)
	{
		if (A.bDead || !A.Body.IsValid() || A.Stage < 4)
		{
			continue;
		}
		TArray<int32>& C = Counts.FindOrAdd(A.Base);
		C.SetNumZeroed(11);
		if (A.bFemale)
		{
			const bool bRoom = !KindInfo.Contains(A.Base) || KindInfo[A.Base].Alive < RoomFor(A.Base);
			const int32 Why = IsReady(A) ? 0 : A.bGravid ? 1 : Nests.IsValidIndex(A.NestI) ? 2 : A.ClutchIn > 0.f ? 3 : A.Condition <= 0.45f ? 4
				: A.Thirst >= 0.7f ? 5 : A.Health <= 0.7f ? 6 : !bRoom || AliveNow >= MaxAnimals ? 7 : 6;
			++C[Why];
		}
		else
		{
			++C[A.MateIn > 0.f ? 9 : A.Condition < 0.3f || A.Health < 0.6f ? 10 : 8];
		}
	}
	FString Text;
	TArray<FString> Keys;
	Counts.GetKeys(Keys);
	Keys.Sort();
	for (const FString& K : Keys)
	{
		const TArray<int32>& C = Counts[K];
		Text += FString::Printf(TEXT("\n  %s females: %d ready, %d carrying, %d minding a nest, %d between clutches, %d hungry, %d thirsty, %d unwell, %d no room; males: %d ready, %d cooling off, %d hungry or hurt"),
			*K, C[0], C[1], C[2], C[3], C[4], C[5], C[6], C[7], C[8], C[9], C[10]);
	}
	for (const FBFAnimal& A : Animals)
	{
		if (!A.bDead && A.Body.IsValid() && A.bHunter && A.Stage >= 4)
		{
			Text += FString::Printf(TEXT("\n  %s: %s, %.0f%% full, \"%s\""), *A.Name, ActName(A.Act), 100.f * A.StomachKg / FMath::Max(A.StomachMaxKg, 0.01f), *A.Thought);
		}
	}
	return Text;
}

FString ABFLife::ActsText() const
{
	// How many of each kind are at each act now (the day check's hourly line).
	TMap<FString, TMap<FString, int32>> ByKind;
	for (const FBFAnimal& A : Animals)
	{
		if (!A.bDead && A.Body.IsValid())
		{
			const FString What = A.bWatch && A.Act == EBFAct::Alert ? FString(TEXT("watching")) : A.bPlay && A.Act == EBFAct::Wander ? FString(TEXT("playing"))
				: A.AmbushUntil > GetWorld()->GetTimeSeconds() && A.Act == EBFAct::Hunt ? FString(TEXT("in ambush")) : FString(ActName(A.Act));
			ByKind.FindOrAdd(A.Base).FindOrAdd(What) += 1;
		}
	}
	FString Out;
	TArray<FString> Keys;
	ByKind.GetKeys(Keys);
	Keys.Sort();
	for (const FString& K : Keys)
	{
		Out += TEXT("\n  ") + K + TEXT(":");
		for (const TPair<FString, int32>& P : ByKind[K])
		{
			Out += FString::Printf(TEXT(" %s %d,"), *P.Key, P.Value);
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------- far off (life cycle step 8)

int32 ABFLife::NumDormant() const
{
	int32 Count = 0;
	for (const FBFAnimal& A : Animals)
	{
		Count += !A.bDead && A.bDormant ? 1 : 0;
	}
	return Count;
}

void ABFLife::SetDormant(FBFAnimal& A, bool bOn)
{
	ABFDino* D = A.Body.Get();
	if (!D)
	{
		return;
	}
	if (bOn && A.bFlier && D->IsFlying())
	{
		D->Land();
		float G = 0.f;
		const FVector Roost = Colony + FVector(FMath::FRandRange(-2000.f, 2000.f), FMath::FRandRange(-2000.f, 2000.f), 0.f);
		if (Perches.IsValidIndex(A.PerchI))
		{
			D->SetActorLocation(PerchSpot(A.PerchI, D, false), false, nullptr, ETeleportType::TeleportPhysics);
			D->SetOnPerch(Perches[A.PerchI].Out);
		}
		else if (Island.IsValid() && Island->GroundAt(FVector2D(Roost), G))
		{
			D->SetActorLocation(FVector(Roost.X, Roost.Y, G + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	A.bDormant = bOn;
	D->SetActorHiddenInGame(bOn);
	D->SetActorEnableCollision(!bOn);
	D->SetActorTickEnabled(!bOn);
	D->GetCharacterMovement()->SetComponentTickEnabled(!bOn);
	TInlineComponentArray<USkinnedMeshComponent*> Meshes(D);
	for (USkinnedMeshComponent* Mesh : Meshes)
	{
		Mesh->SetComponentTickEnabled(!bOn);
	}
	if (A.Label)
	{
		A.Label->SetVisibility(false);
	}
	A.Path.Reset();
	A.bGoal = false;
	A.FarClock = 0.f;
	if (!bOn)
	{
		// Back: on the ground where its simple life left it, its mind picking up afresh.
		float Ground = 0.f;
		const FVector At = D->GetActorLocation();
		if (Island.IsValid() && Island->GroundAt(FVector2D(At), Ground))
		{
			D->SetActorLocation(FVector(At.X, At.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		A.ThinkIn = FMath::FRandRange(0.f, 0.3f);
		A.Act = EBFAct::Idle;
		A.ActSince = GetWorld()->GetTimeSeconds();
	}
}

bool ABFLife::MoveFar(FBFAnimal& A, const FVector& To, float StepCm)
{
	// A step toward To over the ground its body can stand on (or round whatever is in the way); its body moved there.
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	const float Dist = FVector::Dist2D(Me, To);
	if (Dist < 100.f)
	{
		return true;
	}
	const FVector Dir = (To - Me).GetSafeNormal2D();
	const float Step = FMath::Min(StepCm, Dist);
	for (const float Turn : { 0.f, 35.f, -35.f, 70.f, -70.f, 110.f, -110.f })
	{
		const FVector Cand = Me + Dir.RotateAngleAxis(Turn, FVector::UpVector) * Step;
		float Ground = 0.f;
		if (Island->Walkable(Cand, D->GetSpecies(), A.WadeCm) && Island->GroundAt(FVector2D(Cand), Ground))
		{
			D->SetActorLocationAndRotation(FVector(Cand.X, Cand.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f),
				FRotator(0.f, Dir.RotateAngleAxis(Turn, FVector::UpVector).Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			A.WalkedCm += Step;
			return Step >= Dist - 1.f;
		}
	}
	return false;
}

void ABFLife::TickFar(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	// Once a second: its needs, then the one thing it most needs to do, done simply on the island's real plants and water:
	// water when thirsty, plants when hungry (a plant eater), prey (a hunter: its odds a chase like the real ones), its
	// mate or its nest, else keeping to its herd. It walks there at its walking pace over ground it can stand on.
	A.FarClock += Dt;
	if (A.FarClock < 1.f)
	{
		return;
	}
	const float T = A.FarClock;
	A.FarClock = 0.f;
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	const float Rate = NeedRate * T;
	A.StomachKg = FMath::Max(0.f, A.StomachKg - A.DigestKgPerS * Rate);
	A.Thirst = FMath::Min(1.f, A.Thirst + Rate / (A.bHunter ? 1200.f : 420.f));
	A.Tired = FMath::Clamp(A.Tired + Rate * (Island->Daylight() < 0.3f ? -1.f / 45.f : 1.f / 1500.f), 0.f, 1.f);
	A.Fear = FMath::Max(0.f, A.Fear - 0.06f * T);
	const float Walk = D->GetSpecies().Walk.SpeedCm * T;
	const float Hunger = 1.f - A.StomachKg / FMath::Max(A.StomachMaxKg, 0.01f);
	FBFHerd& Herd = Herds[A.Herd];
	// Sleeping at night (unless in real need).
	if (Island->Daylight() < 0.3f && A.Thirst < 0.7f && Hunger < 0.7f)
	{
		A.Act = EBFAct::Rest;
		A.Thought = TEXT("far off: asleep");
		return;
	}
	// Water.
	if (A.Thirst > 0.45f)
	{
		A.Act = EBFAct::Drink;
		if (A.WaterAt.IsZero())
		{
			FVector Stand, Water;
			const FBFMemory* M = Recall(A, 1, Me);
			if ((M && Island->NearestShore(M->At, 3000.f, Stand, Water)) || Island->NearestShore(Me, 80000.f, Stand, Water))
			{
				A.WaterAt = Stand;
			}
		}
		if (!A.WaterAt.IsZero())
		{
			if (MoveFar(A, A.WaterAt, Walk) || FVector::Dist2D(D->GetActorLocation(), A.WaterAt) < 1500.f)
			{
				A.Thirst = 0.f;
				A.WaterAt = FVector::ZeroVector;
				++A.Drinks;
				++FarDrinks;
				Remember(A, 1, D->GetActorLocation(), 1.f, Now);
			}
		}
		A.Thought = TEXT("far off: going to water");
		return;
	}
	// A gravid female due: her nest where she is; a mother minding eggs: by them.
	if (A.bFemale && A.bGravid && A.LayIn <= 0.f)
	{
		A.NestSite = Me;
		LayEggs(A, Index, Now);
		A.Thought = TEXT("far off: laid her eggs");
		return;
	}
	if (Nests.IsValidIndex(A.NestI) && Nests[A.NestI].Eggs > 0 && Nests[A.NestI].Care > 0 && Hunger < 0.7f)
	{
		A.Act = EBFAct::Nest;
		MoveFar(A, Nests[A.NestI].At + FVector(600.f, 0.f, 0.f), Walk);
		A.Thought = TEXT("far off: minding her eggs");
		return;
	}
	if (A.bFlier && NeedsFeeding(A))
	{
		const bool bParent = (Animals.IsValidIndex(A.Mother) && !Animals[A.Mother].bDead) || (Animals.IsValidIndex(A.Father) && !Animals[A.Father].bDead);
		A.Thirst = 0.f;
		if (bParent && Hunger > 0.4f && FMath::FRand() < 0.1f)
		{
			A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + 0.6f);
			++Feeds;
		}
		A.Thought = bParent ? TEXT("far off: at the colony, fed by its parents") : TEXT("far off: at the colony, no parent left");
		return;
	}
	if (A.bFlier)
	{
		A.Thirst = 0.f;           // (its water comes with its fish)
		if (Hunger > 0.35f)
		{
			for (FShoal& Sh : Shoals)
			{
				if (Sh.FishKg > 0.5f && FVector::Dist2D(Sh.At, Me) < 200000.f && FMath::FRand() < 0.15f)
				{
					const float Kg = FMath::Min(Sh.FishKg, 0.6f);
					Sh.FishKg -= Kg;
					A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + Kg);
					++Catches;
					break;
				}
			}
			A.Thought = TEXT("far off: fishing");
		}
		else
		{
			A.Thought = TEXT("far off: at the colony");
		}
		return;
	}
	// Food.
	if (A.bHunter && Hunger > 0.4f && A.Stage >= 2)
	{
		// Hunting far off: a carcass first; else the easiest prey near it (not under a herd that would stand), walked to,
		// and a chase that takes it one time in eight.
		A.Act = EBFAct::Hunt;
		const int32 C = SmellCarcass(A);
		if (C != INDEX_NONE)
		{
			if (MoveFar(A, Carcasses[C].At, Walk) || FVector::Dist2D(D->GetActorLocation(), Carcasses[C].At) < 800.f)
			{
				const float Took = FMath::Min(Carcasses[C].MeatKg, A.StomachMaxKg - A.StomachKg);
				Carcasses[C].MeatKg -= Took;
				A.StomachKg += Took;
				A.EatenKg += Took;
				++FarMeals;
			}
			A.Thought = TEXT("far off: at a kill");
			return;
		}
		int32 Best = INDEX_NONE;
		float BestScore = 0.f;
		for (int32 j = 0; j < Animals.Num(); ++j)
		{
			const FBFAnimal& P = Animals[j];
			if (P.bHunter || P.bDead || !P.Body.IsValid() || !P.bDormant)
			{
				continue;
			}
			const float Dist = FVector::Dist2D(P.Body->GetActorLocation(), Me);
			if (Dist > 150000.f || HerdMassNear(P, 4000.f) >= 2.5f * PackKg(A) || HitShare(A.MassKg, A.Weapon, P) < 0.05f)
			{
				continue;
			}
			const float Score = (P.Stage < 4 ? 1.6f : 1.f) * (1.f + (1.f - P.Health)) / (1.f + Dist / 20000.f);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = j;
			}
		}
		if (Best != INDEX_NONE)
		{
			const FVector There = Animals[Best].Body->GetActorLocation();
			if ((FVector::Dist2D(Me, There) < 3000.f || MoveFar(A, There, 1.6f * Walk)) && Now - A.ChargeAt > 90.f)
			{
				A.ChargeAt = Now;          // one chase in a while (each a real one's odds)
				++Charges;
				if (FMath::FRand() < 0.125f)
				{
					++A.Kills;
					++FarKills;
					Die(Best, FString::Printf(TEXT("was killed by %s (far off)"), *A.Name), Now);
				}
				else
				{
					Animals[Best].Fear = 1.f;
				}
			}
			A.Thought = FString::Printf(TEXT("far off: hunting %s"), *Animals[Best].Name);
			return;
		}
	}
	else if (!A.bHunter && Hunger > 0.3f)
	{
		A.Act = EBFAct::Graze;
		FVector PlantAt;
		float Reach = 0.f, Kg = 0.f;
		const float TopZ = Me.Z + 1.2f * D->GetSpecies().Bounds.GetSize().Z * A.Genes.Size;
		if (Island->FindPlant(Me, 8000.f, TopZ, PlantAt, Reach, Kg))
		{
			if (FVector::Dist2D(Me, PlantAt) - Reach < 500.f || MoveFar(A, PlantAt, Walk))
			{
				FString What;
				float Left = 0.f;
				const float Took = Island->EatPlants(PlantAt, Reach + 500.f, TopZ, A.MouthfulKg * T / 1.7f, What, Left);
				A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + Took);
				A.EatenKg += Took;
				FarMeals += Took > 0.f ? 1 : 0;
			}
			A.Thought = TEXT("far off: grazing");
			return;
		}
		// Eaten out round here: the herd moves on (as the full mind does), 150-500 m.
		if (Now - Herd.MovedAt > 30.f)
		{
			for (int32 Try = 0; Try < 8; ++Try)
			{
				const float Ang = FMath::FRandRange(0.f, 2.f * PI);
				const FVector Cand = Herd.Home + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * FMath::FRandRange(15000.f, 50000.f);
				if (Island->Walkable(Cand, D->GetSpecies(), A.WadeCm) && Island->FindPlant(Cand, 6000.f, TopZ, PlantAt, Reach, Kg))
				{
					Herd.Home = Cand;
					Herd.MovedAt = Now;
					++HerdMoves;
					break;
				}
			}
		}
	}
	// A male to a female in condition near (and mating with her when he's there: rivals settle it the simple way, the
	// stronger first).
	if (!A.bFemale && A.Stage >= 4 && A.MateIn <= 0.f && A.Condition >= 0.3f && A.Health >= 0.6f)
	{
		float BestD = 120000.f;
		int32 F = INDEX_NONE;
		for (const int32 j : ReadyFemales)
		{
			if (Animals.IsValidIndex(j) && j != Index && Animals[j].Base == A.Base && IsReady(Animals[j]))
			{
				const float Dist = FVector::Dist2D(Animals[j].Body->GetActorLocation(), Me);
				if (Dist < BestD)
				{
					BestD = Dist;
					F = j;
				}
			}
		}
		if (F != INDEX_NONE)
		{
			A.Act = EBFAct::Court;
			if (FVector::Dist2D(Me, Animals[F].Body->GetActorLocation()) < 3000.f || MoveFar(A, Animals[F].Body->GetActorLocation(), Walk))
			{
				FBFAnimal& She = Animals[F];
				She.bGravid = true;
				She.LayIn = Gestation;
				She.ClutchFather = Index;
				She.ClutchIn = BreedEvery;
				++She.Matings;
				++A.Matings;
				++NumMatings;
				A.MateIn = 600.f;
			}
			A.Thought = FString::Printf(TEXT("far off: courting %s"), *Animals[F].Name);
			return;
		}
	}
	// Else with its herd (or its mother): near its home.
	A.Act = EBFAct::Wander;
	const FVector Keep = A.bYoung && Animals.IsValidIndex(A.Mother) && Animals[A.Mother].Body.IsValid() ? Animals[A.Mother].Body->GetActorLocation() : Herd.Home;
	if (FVector::Dist2D(Me, Keep) > 3000.f + 0.5f * A.Spacing)
	{
		MoveFar(A, Keep + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * A.Spacing, 0.6f * Walk);
	}
	A.Thought = TEXT("far off: with the herd");
}

// ---------------------------------------------------------------------------------------------- you in the cycle (step 9)

void ABFLife::SyncPlayer(ABFDino* Pawn, float Full, float PThirst, float PHealth, float Dt)
{
	if (!Pawn || !Island.IsValid() || PendingPlayerBody.IsValid())
	{
		return;          // (a body you've grown into waits for the controller to take it)
	}
	if (!Animals.IsValidIndex(PlayerI) || Animals[PlayerI].Body.Get() != Pawn || Animals[PlayerI].bDead)
	{
		if (Animals.IsValidIndex(PlayerI) && Animals[PlayerI].bPlayer)
		{
			Animals[PlayerI].bPlayer = false;          // the you that was (a body gone: started again)
			Animals[PlayerI].bDead = true;
		}
		// You, among them: your kind, your stage (from your body), a herd of your own (your young join it), a sex.
		ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
		const FBFSpecies& S = Pawn->GetSpecies();
		const int32 I = Animals.Num();
		FBFAnimal& A = Animals.AddDefaulted_GetRef();
		A.bPlayer = true;
		A.Body = Pawn;
		A.Name = TEXT("You");
		A.Base = S.BaseKey.IsEmpty() ? S.Key : S.BaseKey;
		for (int32 St = 1; St <= 3; ++St)
		{
			A.Stage = StageKey(A.Base, St) == S.Key ? St : A.Stage;
		}
		A.bYoung = A.Stage <= 2;
		A.AgeS = A.Stage >= 4 ? 32400.f : 0.f;
		A.bHunter = S.Feed == EBFFeed::None;
		A.SpeciesIndex = GM ? GM->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& X) { return X.Key == S.Key; }) : INDEX_NONE;
		A.Herd = Herds.Num();
		FBFHerd& H = Herds.AddDefaulted_GetRef();
		H.Kind = S.Key == TEXT("trike") ? TEXT("Trike") : S.Key == TEXT("ankylo") ? TEXT("Ankylo") : S.Key == TEXT("brachio") ? TEXT("Brachio") : S.Key == TEXT("trex") ? TEXT("T. rex")
			: S.Key == TEXT("raptor") ? TEXT("Raptor") : S.Name;
		H.Home = Pawn->GetActorLocation();
		H.Members.Add(I);
		A.StomachMaxKg = 1.f;
		A.StomachKg = Full;
		ApplyBody(A);
		A.Condition = Full;
		PlayerI = I;
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: you join the island's life: a %s %s"), bPlayerFemale ? TEXT("female") : TEXT("male"), *S.Key);
	}
	FBFAnimal& A = Animals[PlayerI];
	A.bFemale = bPlayerFemale;
	A.StomachKg = FMath::Clamp(Full, 0.f, 1.f) * A.StomachMaxKg;
	A.Thirst = PThirst;
	A.Health = PHealth;
	const float G = Dt * GrowRate;
	A.ClutchIn = FMath::Max(0.f, A.ClutchIn - G);
	A.MateIn = FMath::Max(0.f, A.MateIn - G);
	A.LayIn = A.bGravid ? FMath::Max(0.f, A.LayIn - G) : A.LayIn;
	A.Condition = FMath::Lerp(A.Condition, Full, FMath::Min(1.f, NeedRate * Dt / 1200.f));
	Herds[A.Herd].Home = Pawn->GetActorLocation();
	// Courting (a male, after X by her): displaying beside her; ten seconds and she takes you, unless a rival beats you.
	if (Animals.IsValidIndex(PlayerCourt))
	{
		FBFAnimal& She = Animals[PlayerCourt];
		if (!IsReady(She) || FVector::Dist2D(She.Body->GetActorLocation(), Pawn->GetActorLocation()) > 3000.f)
		{
			PlayerNote = FString::Printf(TEXT("%s isn't taking any notice now"), *She.Name);
			PlayerNoteUntil = FPlatformTime::Seconds() + 4.0;
			PlayerCourt = INDEX_NONE;
			return;
		}
		PlayerDisplayS += Dt;
		if (FMath::Frac(PlayerDisplayS / 3.f) < Dt / 3.f)
		{
			Pawn->StartIdle(int32(PlayerDisplayS / 3.f) % 2 == 0 ? EBFIdle::Yawn : EBFIdle::Look);
		}
		PlayerNote = FString::Printf(TEXT("Displaying to %s (%.0f of 10 s)"), *She.Name, PlayerDisplayS);
		PlayerNoteUntil = FPlatformTime::Seconds() + 1.0;
		if (PlayerDisplayS >= 10.f)
		{
			// A rival courting her too: the stronger (weight, strength, health, a little luck) has her.
			for (int32 j = 0; j < Animals.Num(); ++j)
			{
				FBFAnimal& B = Animals[j];
				if (j != PlayerI && !B.bDead && B.Body.IsValid() && B.Act == EBFAct::Court && B.CourtTarget == PlayerCourt
					&& B.MassKg * B.Genes.Strength * B.Health * FMath::FRandRange(0.85f, 1.15f) > A.MassKg * A.Genes.Strength * A.Health)
				{
					++Contests;
					PlayerNote = FString::Printf(TEXT("%s was the stronger: he drove you off from %s"), *B.Name, *She.Name);
					PlayerNoteUntil = FPlatformTime::Seconds() + 5.0;
					A.MateIn = 600.f;
					PlayerCourt = INDEX_NONE;
					return;
				}
			}
			She.bGravid = true;
			She.LayIn = Gestation;
			She.ClutchFather = PlayerI;
			She.ClutchIn = BreedEvery;
			++She.Matings;
			++A.Matings;
			++NumMatings;
			A.MateIn = 600.f;
			PlayerNote = FString::Printf(TEXT("%s took you: she's carrying your eggs"), *She.Name);
			PlayerNoteUntil = FPlatformTime::Seconds() + 6.0;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: you mated with %s"), *She.Name);
			PlayerCourt = INDEX_NONE;
		}
	}
}

FString ABFLife::PlayerAct(ABFDino* Pawn)
{
	if (!Animals.IsValidIndex(PlayerI) || !Pawn)
	{
		return TEXT("");
	}
	FBFAnimal& A = Animals[PlayerI];
	const FVector Me = Pawn->GetActorLocation();
	if (A.Stage < 4)
	{
		return TEXT("You're too young to breed yet");
	}
	if (A.bFemale)
	{
		if (A.bGravid && A.LayIn <= 0.f)
		{
			// Your nest: here; your clutch laid; minded by you as your kind does.
			A.NestSite = Me;
			Pawn->StartIdle(EBFIdle::Sniff);
			LayEggs(A, PlayerI, GetWorld()->GetTimeSeconds());
			const FBFNest& N = Nests.Last();
			return FString::Printf(TEXT("You scraped a nest and laid %d eggs (%.1f kg each); they hatch in %.0f min of play%s"), N.Laid, N.EggKg,
				Incubation / FMath::Max(GrowRate, 0.001f) / 60.f, N.Care == 0 ? TEXT(" (your kind leaves them)") : TEXT(": guard them"));
		}
		if (A.bGravid)
		{
			return FString::Printf(TEXT("You're carrying eggs: due in %.0f min"), A.LayIn / FMath::Max(GrowRate, 0.001f) / 60.f + 0.5f);
		}
		return IsReady(A) ? FString(TEXT("You're in condition: a male of your kind will court you")) : FString(TEXT("You're not in condition yet (eat well, drink, heal; time between clutches)"));
	}
	// A male: the nearest female of your kind in condition within 20 m.
	if (A.MateIn > 0.f)
	{
		return TEXT("Not again so soon");
	}
	int32 Best = INDEX_NONE;
	float BestD = 2000.f;
	for (int32 j = 0; j < Animals.Num(); ++j)
	{
		const FBFAnimal& F = Animals[j];
		if (j != PlayerI && F.Base == A.Base && IsReady(F))
		{
			const float Dist = FVector::Dist2D(F.Body->GetActorLocation(), Me) - 0.5f * F.Body->BodyLengthCm();
			if (Dist < BestD)
			{
				BestD = Dist;
				Best = j;
			}
		}
	}
	if (Best == INDEX_NONE)
	{
		return TEXT("No female of your kind in condition within 20 m");
	}
	PlayerCourt = Best;
	PlayerDisplayS = 0.f;
	return FString::Printf(TEXT("Courting %s: stay by her"), *Animals[Best].Name);
}

FString ABFLife::PlayerStatus() const
{
	if (!Animals.IsValidIndex(PlayerI))
	{
		return TEXT("");
	}
	const FBFAnimal& A = Animals[PlayerI];
	const int32 Young = YourYoung();
	FString S = FString::Printf(TEXT("%s%s"), A.bFemale ? TEXT("female") : TEXT("male"), A.Generation > 0 ? *FString::Printf(TEXT(", generation %d"), A.Generation) : TEXT(""));
	if (A.Stage < 4)
	{
		S += FString::Printf(TEXT(", %s"), *StageName(A.Stage));
	}
	else if (A.bFemale && A.bGravid)
	{
		S += A.LayIn <= 0.f ? TEXT(", your eggs are due (X to lay)") : *FString::Printf(TEXT(", carrying eggs (due in %.0f min)"), A.LayIn / FMath::Max(GrowRate, 0.001f) / 60.f + 0.5f);
	}
	else if (IsReady(A))
	{
		S += A.bFemale ? TEXT(", in condition") : TEXT("");
	}
	if (!A.bFemale && A.Stage >= 4)
	{
		S += A.MateIn > 0.f ? TEXT("") : TEXT(" (X by a female in condition)");
	}
	if (Young > 0)
	{
		S += FString::Printf(TEXT(", %d young living"), Young);
	}
	return S;
}

int32 ABFLife::YourYoung() const
{
	int32 Count = 0;
	for (const FBFAnimal& Y : Animals)
	{
		Count += !Y.bDead && !Y.bPlayer && Y.Body.IsValid() && Animals.IsValidIndex(PlayerI) && (Y.Mother == PlayerI || Y.Father == PlayerI) ? 1 : 0;
	}
	return Count;
}

ABFDino* ABFLife::CarryOn(FString& Note)
{
	// Your oldest living young: you carry on as it (its body, its age and needs, its genes); your old body stays where it fell.
	int32 Best = INDEX_NONE;
	for (int32 j = 0; j < Animals.Num(); ++j)
	{
		const FBFAnimal& Y = Animals[j];
		if (!Y.bDead && !Y.bPlayer && Y.Body.IsValid() && Animals.IsValidIndex(PlayerI) && (Y.Mother == PlayerI || Y.Father == PlayerI)
			&& (Best == INDEX_NONE || Y.AgeS > Animals[Best].AgeS))
		{
			Best = j;
		}
	}
	if (Best == INDEX_NONE)
	{
		return nullptr;
	}
	FBFAnimal& Old = Animals[PlayerI];
	Old.bPlayer = false;
	Old.bDead = true;
	FBFAnimal& Y = Animals[Best];
	if (Y.bDormant)
	{
		SetDormant(Y, false);
	}
	Y.bPlayer = true;
	Y.Act = EBFAct::Idle;
	Y.Path.Reset();
	Y.bGoal = false;
	if (Y.Label)
	{
		Y.Label->SetVisibility(false);
	}
	bPlayerFemale = Y.bFemale;
	Note = FString::Printf(TEXT("You carry on as %s (%s, generation %d, %s)"), *Y.Name, Y.bFemale ? TEXT("female") : TEXT("male"), Y.Generation, *StageName(Y.Stage));
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s"), *Note);
	PlayerI = Best;
	return Y.Body.Get();
}

// ---------------------------------------------------------------------------------------------- the Pteranodons (step 10)

void ABFLife::PlaceColony(const FVector& Near, FRandomStream& R)
{
	// A colony on the highest gentle ground within 1.5 km of you that has the sea within 600 m; fish shoals in the sea
	// along that coast and in the lakes near; six Pteranodons (three females, three males).
	ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>();
	ABFWorld* World = Island.Get();
	const int32 Index = GM ? GM->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("ptera") && !S.IsSplice(); }) : INDEX_NONE;
	if (Index == INDEX_NONE || !World)
	{
		return;
	}
	const FBFSpecies& S = GM->GetSpecies()[Index];
	float BestZ = -1e9f;
	Colony = FVector::ZeroVector;
	for (int32 k = 0; k < 160; ++k)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		const FVector P = Near + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * R.FRandRange(20000.f, 150000.f);
		float Z = 0.f;
		FVector Sea;
		const EBFBiome B = World->BiomeAt(FVector2D(P));
		if ((B == EBFBiome::Plains || B == EBFBiome::Forest) && World->Walkable(P, S, 0.f) && World->GroundAt(FVector2D(P), Z) && Z > BestZ && World->NearestWater(FVector2D(P), EBFBiome::Sea, Sea) && FVector::Dist2D(Sea, P) < 60000.f)
		{
			BestZ = Z;
			Colony = FVector(P.X, P.Y, Z);
		}
	}
	if (Colony.IsZero())
	{
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: no place for a Pteranodon colony near you"));
		return;
	}
	Shoals.Reset();
	for (int32 k = 0; k < 16; ++k)
	{
		const FVector P = Colony + FRotator(0.f, 22.5f * k, 0.f).Vector() * R.FRandRange(20000.f, 120000.f);
		FVector W;
		if (World->NearestWater(FVector2D(P), k % 4 == 3 ? EBFBiome::Lake : EBFBiome::Sea, W) && FVector::Dist2D(W, Colony) < 180000.f
			&& !Shoals.ContainsByPredicate([&](const FShoal& O) { return FVector::Dist2D(O.At, W) < 8000.f; }))
		{
			// Out from the edge into the deepest water within 40 m (the fish shoal there, not in the shallows).
			FVector Out = W;
			float Deep = 0.f;
			for (int32 d = 0; d < 16; ++d)
			{
				const FVector P2 = W + FRotator(0.f, 22.5f * d, 0.f).Vector() * (2000.f + 2000.f * (d % 2));
				float G2 = 0.f, S2 = 0.f;
				if (World->GroundAt(FVector2D(P2), G2) && World->WaterAt(FVector(P2.X, P2.Y, G2 + 10.f), S2) && S2 - G2 > Deep)
				{
					Deep = S2 - G2;
					Out = FVector(P2.X, P2.Y, S2);
				}
			}
			if (Deep < 150.f)
			{
				continue;            // too shallow for a shoal
			}
			FShoal& Sh = Shoals.AddDefaulted_GetRef();
			Sh.At = Out;
			Sh.MaxKg = Sh.FishKg = R.FRandRange(8.f, 20.f);
		}
	}
	BuildPerches();
	const int32 Before = Animals.Num();
	AddHerd(TEXT("ptera"), 6, Colony, 0.f, 0.f, true, R);
	for (int32 i = Before; i < Animals.Num(); ++i)
	{
		Animals[i].bFlier = true;
		Animals[i].Thirst = 0.f;
		Animals[i].CircleAng = R.FRandRange(0.f, 2.f * PI);
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: the colony's perches: %d (%d nest trees, %d ledges, %d alcoves)"), Perches.Num(), NestTrees,
		Perches.FilterByPredicate([](const FPerch& P) { return P.bLedge; }).Num(), Perches.FilterByPredicate([](const FPerch& P) { return P.bAlcove; }).Num());
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: a Pteranodon colony of %d at %s (%.0f m up), %d fish shoals near"), Animals.Num() - Before, *Colony.ToString(), BestZ / 100.f, Shoals.Num());
}

void ABFLife::BuildPerches()
{
	// Up to four giant redwoods round the colony (redwoods grew in the Late Cretaceous), 65 to 78 m tall, each with three
	// broad shelf branches 28 to 55 m up (a thick limb out from the trunk, a fork under it, a flat mat of twigs on its
	// end: room for a Pteranodon and its nest), more limbs with sprays of foliage up the trunk (none over a shelf), a
	// narrow crown; and up to four rock ledges jutting from the steep high ground within 300 m (at least 35 degrees,
	// no lower than the colony). The engine's own shapes, the same each time from the colony's place.
	ABFWorld* World = Island.Get();
	Perches.Reset();
	NestTrees = 0;
	if (World && LedgeRocks != INDEX_NONE)
	{
		World->RemoveRocks(LedgeRocks);
	}
	LedgeRocks = INDEX_NONE;
	TArray<FBFRock> LedgeShapes;
	if (!World || Colony.IsZero())
	{
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Make = [&](TObjectPtr<UInstancedStaticMeshComponent>& C, const TCHAR* MeshPath, const FLinearColor& Colour, bool bSolid)
	{
		if (C)
		{
			C->ClearInstances();
			return;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!Mesh)
		{
			return;
		}
		C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetCanEverAffectNavigation(false);
		C->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bSolid)
		{
			C->SetCollisionResponseToAllChannels(ECR_Block);
		}
		if (Base)
		{
			UMaterialInstanceDynamic* Mi = UMaterialInstanceDynamic::Create(Base, this);
			Mi->SetVectorParameterValue(TEXT("Color"), Colour);
			Mi->SetScalarParameterValue(TEXT("Roughness"), 0.95f);
			C->SetMaterial(0, Mi);
		}
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
	};
	Make(BarkComp, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.20f, 0.075f, 0.04f), true);
	Make(ShelfComp, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.10f, 0.07f, 0.035f), true);
	Make(LeafComp, TEXT("/Engine/BasicShapes/Sphere.Sphere"), FLinearColor(0.03f, 0.09f, 0.04f), false);
	Make(StoneComp, TEXT("/Engine/BasicShapes/Cube.Cube"), FLinearColor(0.24f, 0.23f, 0.21f), true);
	Make(SpireComp, TEXT("/Engine/BasicShapes/Cone.Cone"), FLinearColor(0.03f, 0.09f, 0.04f), false);
	TArray<FTransform> Bark, Shelf, Leaf, Stone, Spire;
	FRandomStream R(int32(Colony.X * 0.01) * 7919 + int32(Colony.Y * 0.01));
	const FVector Up(0.f, 0.f, 1.f);
	auto Along = [](const FVector& Axis) { return FRotationMatrix::MakeFromZ(Axis.GetSafeNormal()).Rotator(); };
	TArray<FVector> TreeAt;
	for (int32 k = 0; k < 60 && NestTrees < 4; ++k)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		const FVector2D P(Colony.X + FMath::Cos(Ang) * R.FRandRange(1500.f, 5500.f), Colony.Y + FMath::Sin(Ang) * R.FRandRange(1500.f, 5500.f));
		float G = 0.f, W = 0.f;
		const EBFBiome B = World->BiomeAt(P);
		if (B == EBFBiome::Sea || B == EBFBiome::Lake || B == EBFBiome::River || B == EBFBiome::Beach || !World->GroundAt(P, G)
			|| World->WaterAt(FVector(P.X, P.Y, G + 10.f), W) || TreeAt.ContainsByPredicate([&](const FVector& T) { return FVector::Dist2D(T, FVector(P, 0.f)) < 2800.f; }))
		{
			continue;
		}
		TreeAt.Add(FVector(P.X, P.Y, G));
		++NestTrees;
		World->ClearTreesNear(P, 1500.f);          // room round it: no island tree's crown in its shelves' way
		const float Ht = R.FRandRange(6500.f, 7800.f);
		const FVector Foot(P.X, P.Y, G - 150.f);
		// The trunk: five lengths, 4.6 m across at the foot to 1.2 m at the top; a flared foot.
		for (int32 s = 0; s < 5; ++s)
		{
			const float Z0 = Ht * s / 5.f, Z1 = Ht * (s + 1) / 5.f + 60.f;
			const float Rad = FMath::Lerp(230.f, 60.f, (s + 0.5f) / 5.f);
			Bark.Add(FTransform(FRotator::ZeroRotator, Foot + FVector(0.f, 0.f, 0.5f * (Z0 + Z1)), FVector(Rad / 50.f, Rad / 50.f, (Z1 - Z0) / 100.f)));
		}
		Bark.Add(FTransform(FRotator::ZeroRotator, Foot + FVector(0.f, 0.f, 200.f), FVector(6.2f, 6.2f, 4.f)));
		// The shelves.
		const float Yaw0 = R.FRandRange(0.f, 360.f);
		TArray<FVector2D> ShelfHY;          // (height, yaw) of each: no foliage over them
		for (int32 sh = 0; sh < 3; ++sh)
		{
			const float H = Ht * (0.42f + 0.14f * sh) + R.FRandRange(-150.f, 150.f);
			const float Yaw = Yaw0 + 125.f * sh + R.FRandRange(-15.f, 15.f);
			const FVector Out = FRotator(0.f, Yaw, 0.f).Vector();
			const FVector Side = FVector::CrossProduct(Out, Up);
			const FVector Root = Foot + FVector(0.f, 0.f, H);
			Bark.Add(FTransform(Along(Out + FVector(0.f, 0.f, 0.06f)), Root + Out * 380.f, FVector(1.0f, 1.0f, 7.6f)));          // the limb, 1 m thick
			Bark.Add(FTransform(Along(Out + FVector(0.f, 0.f, 0.6f)), Root + Out * 280.f - FVector(0.f, 0.f, 200.f), FVector(0.6f, 0.6f, 5.2f)));   // the fork under it
			const FVector Mid = Root + Out * 640.f + FVector(0.f, 0.f, 80.f);
			Shelf.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), Mid, FVector(6.6f, 6.6f, 0.6f)));     // the mat, 6.6 m across, its top 30 cm up
			Shelf.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), Mid + FVector(0.f, 0.f, -25.f), FVector(7.4f, 7.4f, 0.4f)));   // its ragged rim
			FPerch& Pc = Perches.AddDefaulted_GetRef();
			Pc.Top = Mid + FVector(0.f, 0.f, 30.f);
			Pc.Out = Out;
			ShelfHY.Add(FVector2D(H, Yaw));
			// Foliage round it: out past its end, low, and off each side (not over it).
			Leaf.Add(FTransform(FRotator(0.f, Yaw, 0.f), Root + Out * 1100.f + FVector(0.f, 0.f, -160.f), FVector(5.f, 4.f, 2.2f)));
			Leaf.Add(FTransform(FRotator(0.f, Yaw + 40.f, 0.f), Root + Out * 1250.f + Side * 150.f + FVector(0.f, 0.f, -260.f), FVector(3.5f, 2.8f, 1.8f)));
			Leaf.Add(FTransform(FRotator(0.f, Yaw, 0.f), Root + Out * 560.f + Side * 540.f + FVector(0.f, 0.f, -170.f), FVector(3.8f, 3.2f, 2.f)));
			Leaf.Add(FTransform(FRotator(0.f, Yaw, 0.f), Root + Out * 560.f - Side * 540.f + FVector(0.f, 0.f, -170.f), FVector(3.8f, 3.2f, 2.f)));
		}
		// More limbs up the trunk, each with a spray of foliage; shorter higher up.
		for (int32 b = 0; b < 14; ++b)
		{
			const float U = R.FRandRange(0.3f, 0.86f);
			const float H = Ht * U, Yaw = R.FRandRange(0.f, 360.f);
			if (ShelfHY.ContainsByPredicate([&](const FVector2D& S) { return H > S.X - 300.f && H < S.X + 1400.f && FMath::Abs(FRotator::NormalizeAxis(Yaw - S.Y)) < 55.f; }))
			{
				continue;
			}
			const float L = FMath::Lerp(800.f, 300.f, (U - 0.3f) / 0.56f);
			const FVector Out = FRotator(0.f, Yaw, 0.f).Vector();
			const FVector Root = Foot + FVector(0.f, 0.f, H);
			Bark.Add(FTransform(Along(Out + FVector(0.f, 0.f, -0.15f)), Root + Out * (0.5f * L), FVector(0.35f, 0.35f, L / 100.f)));
			Leaf.Add(FTransform(FRotator(0.f, Yaw, 0.f), Root + Out * L + FVector(0.f, 0.f, -100.f), FVector(L / 150.f, L / 200.f, FMath::Max(1.6f, L / 350.f))));
			Leaf.Add(FTransform(FRotator(0.f, Yaw + 50.f, 0.f), Root + Out * (0.7f * L) + FVector(0.f, 0.f, -180.f), FVector(L / 220.f, L / 260.f, 1.4f)));
		}
		// The crown: a narrow conifer spire over the top, in tiers (the engine's cone, its point up).
		for (int32 c = 0; c < 4; ++c)
		{
			const float U = c / 3.f;
			const float Rad = FMath::Lerp(520.f, 220.f, U), Tall = FMath::Lerp(900.f, 650.f, U);
			Spire.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), Foot + FVector(0.f, 0.f, Ht * 0.84f + c * 520.f + 0.5f * Tall), FVector(Rad / 50.f, Rad / 50.f, Tall / 100.f)));
		}
	}
	// The ledges.
	int32 Ledges = 0;
	TArray<FVector> LedgeAt;
	for (int32 k = 0; k < 3000 && Ledges < 4; ++k)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		const float Dist = R.FRandRange(3000.f, 60000.f);
		const FVector2D P(Colony.X + FMath::Cos(Ang) * Dist, Colony.Y + FMath::Sin(Ang) * Dist);
		float G = 0.f, GX0 = 0.f, GX1 = 0.f, GY0 = 0.f, GY1 = 0.f, W = 0.f;
		if (!World->GroundAt(P, G) || G < Colony.Z - 500.f || World->WaterAt(FVector(P.X, P.Y, G + 10.f), W)
			|| !World->GroundAt(P + FVector2D(300.f, 0.f), GX1) || !World->GroundAt(P - FVector2D(300.f, 0.f), GX0)
			|| !World->GroundAt(P + FVector2D(0.f, 300.f), GY1) || !World->GroundAt(P - FVector2D(0.f, 300.f), GY0))
		{
			continue;
		}
		const FVector2D Grad((GX1 - GX0) / 600.f, (GY1 - GY0) / 600.f);
		if (FMath::RadiansToDegrees(FMath::Atan(Grad.Size())) < 35.f || LedgeAt.ContainsByPredicate([&](const FVector& L) { return FVector::Dist2D(L, FVector(P, 0.f)) < 2500.f; }))
		{
			continue;
		}
		const FVector Down = FVector(-Grad.X, -Grad.Y, 0.f).GetSafeNormal();
		// A cliff, not a bank: the ground 10 m out from it at least 9 m lower (and 20 m out, 15 m).
		float G10 = 0.f, G20 = 0.f;
		if (!World->GroundAt(P + FVector2D(Down) * 1000.f, G10) || !World->GroundAt(P + FVector2D(Down) * 2000.f, G20) || G - G10 < 900.f || G - G20 < 1500.f)
		{
			continue;
		}
		const FVector At(P.X, P.Y, G);
		LedgeAt.Add(At);
		++Ledges;
		const float Yaw = Down.Rotation().Yaw;
		// A slab of rock jutting 5.5 m out by 4.5 m wide, 2 m thick, its back sunk in the slope, its top flat to stand on;
		// a lump of rock under it holding it up.
		FBFRock Slab;
		Slab.Kind = 1;
		Slab.Seed = R.RandRange(0, 65535);
		Slab.Half = FVector(300.f, 240.f, 110.f);
		Slab.At = At + Down * 140.f + FVector(0.f, 0.f, -60.f);
		Slab.Rot = FRotator(R.FRandRange(-2.f, 2.f), Yaw, R.FRandRange(-2.f, 2.f));
		Slab.TopZ = float(At.Z) + 40.f;
		LedgeShapes.Add(Slab);
		FBFRock Under;
		Under.Kind = 0;
		Under.Seed = R.RandRange(0, 65535);
		Under.Half = FVector(220.f, 210.f, 170.f);
		Under.At = At + Down * 70.f + FVector(0.f, 0.f, -290.f);
		Under.Rot = FRotator(6.f, Yaw + 15.f, 4.f);
		LedgeShapes.Add(Under);
		FPerch& Pc = Perches.AddDefaulted_GetRef();
		Pc.Top = At + Down * 170.f + FVector(0.f, 0.f, 40.f);
		Pc.Out = Down;
		Pc.bLedge = true;
	}
	// Alcoves: up to three shallow caves high in the cliffs within 600 m (6 m under a cliff's top edge, 15 m or more above
	// the ground below it; 8.5 m deep), a perch at the back of each (kept for nests: out of the weather, out of reach).
	// The island keeps them, so a load finds them again.
	{
		TArray<int32> AlcoveI;
		for (int32 c = 0; c < World->NumCaves(); ++c)
		{
			if (World->CaveKind(c) == 1 && FVector::Dist2D(World->CaveMouth(c), Colony) < 70000.f)
			{
				AlcoveI.Add(c);
			}
		}
		for (int32 k = 0; k < 40 && AlcoveI.Num() < 3; ++k)
		{
			FVector Top, Dn;
			if (!World->FindCliff(Colony, 60000.f, Colony.Z - 3000.f, R, Top, Dn))
			{
				continue;
			}
			float G20 = 0.f, Gf = 0.f;
			const FVector Face = Top + Dn * 600.f;
			if (!World->GroundAt(FVector2D(Top + Dn * 2000.f), G20) || Top.Z - G20 < 1500.f || !World->GroundAt(FVector2D(Face), Gf)
				|| AlcoveI.ContainsByPredicate([&](int32 c) { return FVector::Dist2D(World->CaveMouth(c), Face) < 2500.f; })
				|| Perches.ContainsByPredicate([&](const FPerch& Pc) { return FVector::Dist2D(Pc.Top, Face) < 2000.f; }))
			{
				continue;
			}
			const float R0 = 300.f;
			const FVector In = -Dn;
			const FVector M0 = FVector(Face.X, Face.Y, Gf + 0.55f * R0 + 30.f) + Dn * 150.f;
			const int32 C = World->AddCave({ M0, M0 + In * 450.f + FVector(0.f, 0.f, 20.f), M0 + In * 850.f + FVector(0.f, 0.f, 40.f) }, { R0, 330.f, 360.f }, 1);
			if (C != INDEX_NONE)
			{
				AlcoveI.Add(C);
			}
		}
		for (const int32 C : AlcoveI)
		{
			const TArray<FVector>& Fl = World->CaveFloor(C);
			FPerch& Pc = Perches.AddDefaulted_GetRef();
			Pc.Top = Fl.Last() + World->CaveOut(C) * 150.f;
			Pc.Out = World->CaveOut(C);
			Pc.bAlcove = true;
		}
	}
	auto Put = [](UInstancedStaticMeshComponent* C, const TArray<FTransform>& X)
	{
		if (C && X.Num() > 0)
		{
			C->AddInstances(X, false, true, false);
		}
	};
	Put(BarkComp, Bark);
	Put(ShelfComp, Shelf);
	Put(LeafComp, Leaf);
	Put(StoneComp, Stone);
	Put(SpireComp, Spire);
	LedgeRocks = World->AddRocks(LedgeShapes);
}

bool ABFLife::StageNest(int32 MotherI, int32 FatherI, float HatchInS)
{
	if (!Animals.IsValidIndex(MotherI) || !Animals[MotherI].Body.IsValid())
	{
		return false;
	}
	FBFAnimal& A = Animals[MotherI];
	A.bGravid = true;          // (choosing as a mother-to-be: an alcove first)
	const int32 P = PerchFor(A, MotherI);
	if (P == INDEX_NONE)
	{
		A.bGravid = false;
		return false;
	}
	ABFDino* D = A.Body.Get();
	if (D->IsFlying())
	{
		D->Land();
	}
	D->SetActorLocation(PerchSpot(P, D, false), false, nullptr, ETeleportType::TeleportPhysics);
	D->SetOnPerch(Perches[P].Out);
	A.ClutchFather = FatherI;
	A.NestSite = Perches[P].Top;
	const float Now = GetWorld()->GetTimeSeconds();
	LayEggs(A, MotherI, Now);
	if (!Nests.IsValidIndex(A.NestI))
	{
		return false;
	}
	Nests[A.NestI].IncubS = FMath::Max(0.f, Incubation - HatchInS * GrowRate);
	Choose(A, EBFAct::Nest, Now, TEXT("brooding her eggs"));
	return true;
}

int32 ABFLife::PerchNear(const FVector& At, float RadiusCm) const
{
	int32 Best = INDEX_NONE;
	float BestD = RadiusCm;
	for (int32 p = 0; p < Perches.Num(); ++p)
	{
		const float D = FVector::Dist2D(At, Perches[p].Top);
		if (D < BestD && At.Z > Perches[p].Top.Z - 300.f && At.Z < Perches[p].Top.Z + 800.f)
		{
			BestD = D;
			Best = p;
		}
	}
	return Best;
}

int32 ABFLife::PerchFor(FBFAnimal& A, int32 Index)
{
	// Its own; else the one its nest is on; a young one that's fed has its mother's; else by what it wants: a mother-to-be
	// an alcove (sheltered, out of reach), then a ledge, then a tree; the others roost half on ledges, half in the trees
	// (the alcoves kept for the nests). A female carrying eggs moves to a free alcove.
	if (Perches.Num() == 0)
	{
		return INDEX_NONE;
	}
	auto Free = [&](int32 p)
	{
		const int32 O = Perches[p].Owner;
		return O == INDEX_NONE || O == Index || !Animals.IsValidIndex(O) || Animals[O].bDead || Animals[O].PerchI != p;
	};
	const bool bNesting = A.bFemale && (A.bGravid || Nests.IsValidIndex(A.NestI));
	if (A.bGravid && !Nests.IsValidIndex(A.NestI) && Perches.IsValidIndex(A.PerchI) && Perches[A.PerchI].Owner == Index && !Perches[A.PerchI].bAlcove)
	{
		for (int32 p = 0; p < Perches.Num(); ++p)
		{
			if (Perches[p].bAlcove && Free(p))
			{
				Perches[A.PerchI].Owner = INDEX_NONE;
				Perches[p].Owner = Index;
				A.PerchI = p;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s, carrying eggs, takes an alcove in the cliff for her nest"), *A.Name);
				return p;
			}
		}
	}
	if (Perches.IsValidIndex(A.PerchI) && Perches[A.PerchI].Owner == Index)
	{
		return A.PerchI;
	}
	if (NeedsFeeding(A))
	{
		return Animals.IsValidIndex(A.Mother) && Perches.IsValidIndex(Animals[A.Mother].PerchI) ? Animals[A.Mother].PerchI : INDEX_NONE;
	}
	int32 P = INDEX_NONE;
	if (Nests.IsValidIndex(A.NestI))
	{
		P = PerchNear(Nests[A.NestI].At, 300.f);
	}
	auto Pick = [&](TFunctionRef<bool(const FPerch&)> Want)
	{
		for (int32 p = 0; p < Perches.Num() && P == INDEX_NONE; ++p)
		{
			P = Want(Perches[p]) && Free(p) ? p : INDEX_NONE;
		}
	};
	auto Alcove = [](const FPerch& Pc) { return Pc.bAlcove; };
	auto Ledge = [](const FPerch& Pc) { return Pc.bLedge; };
	auto Tree = [](const FPerch& Pc) { return !Pc.bLedge && !Pc.bAlcove; };
	if (bNesting)
	{
		Pick(Alcove);
		Pick(Ledge);
		Pick(Tree);
	}
	else if (Index % 2 == 1)
	{
		Pick(Ledge);
		Pick(Tree);
	}
	else
	{
		Pick(Tree);
		Pick(Ledge);
	}
	Pick([](const FPerch&) { return true; });
	if (P == INDEX_NONE)
	{
		P = Index % Perches.Num();          // more fliers than perches: sharing
	}
	Perches[P].Owner = Index;
	A.PerchI = P;
	return P;
}

FVector ABFLife::PerchSpot(int32 P, const ABFDino* D, bool bVisitor) const
{
	// Its owner a metre to one side of the middle (the nest in the middle), a visitor a little more to the other.
	const FPerch& Pc = Perches[P];
	const FVector Side = FVector::CrossProduct(Pc.Out, FVector::UpVector).GetSafeNormal();
	const float HH = D ? D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 100.f;
	return Pc.Top + Side * (bVisitor ? 130.f : -100.f) + FVector(0.f, 0.f, HH + 5.f);
}

bool ABFLife::AtPerch(const FBFAnimal& A, int32 P) const
{
	const ABFDino* D = A.Body.Get();
	if (!D || D->IsFlying() || !Perches.IsValidIndex(P))
	{
		return false;
	}
	const FVector Me = D->GetActorLocation();
	return FVector::Dist2D(Me, Perches[P].Top) < 450.f && Me.Z > Perches[P].Top.Z - 100.f && Me.Z < Perches[P].Top.Z + 600.f;
}

bool ABFLife::GoPerch(FBFAnimal& A, int32 P, bool bVisitor)
{
	ABFDino* D = A.Body.Get();
	if (AtPerch(A, P))
	{
		return true;
	}
	if (!D->IsFlying())
	{
		D->TakeOff();
		return false;
	}
	const FVector Stand = PerchSpot(P, D, bVisitor);
	const FVector Me = D->GetActorLocation();
	if (D->IsPerching() || FVector::Dist2D(Me, Stand) < 5000.f)
	{
		D->PerchOn(Stand, Perches[P].Out);
		return false;
	}
	// Out to it at about its height (a little over), then in.
	float Under = Me.Z - 100000.f;
	Island->GroundAt(FVector2D(Me), Under);
	FlyTo(A, Stand + Perches[P].Out * 1800.f, FMath::Clamp(Stand.Z + 700.f - Under, 1500.f, 9000.f), false);
	return false;
}

bool ABFLife::MouthTo(FBFAnimal& A, const FVector& To, float Dt)
{
	ABFDino* D = A.Body.Get();
	if (!D)
	{
		return false;
	}
	if (FVector::Dist(A.MouthAt, To) > 100.f)
	{
		A.MouthAt = To;
		A.MouthS = 0.f;
	}
	A.MouthS += Dt;
	D->SetMouthTarget(To);
	const FVector Me = D->GetActorLocation();
	const FVector Snout = D->SnoutNow();
	const FVector Dir = (To - Me).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		return true;
	}
	// Facing it.
	const FRotator Want(0.f, Dir.Rotation().Yaw, 0.f);
	const float Off = FMath::Abs(FRotator::NormalizeAxis(Want.Yaw - D->GetActorRotation().Yaw));
	D->SetActorRotation(FMath::RInterpTo(D->GetActorRotation(), Want, Dt, 4.f));
	// Its snout over it: as far ahead of its middle as its snout reaches now (the neck's reach ahead, as it bends).
	const float Ahead = float(FVector::Dist2D(Snout, Me));
	const float Short = float(FVector::Dist2D(Me, To)) - Ahead;          // + : a step nearer; - : a step back
	if (Off < 25.f && FMath::Abs(Short) > 30.f)
	{
		if (Short > 0.f)
		{
			D->AddMovementInput(Dir, FMath::Clamp(Short / 400.f, 0.2f, 0.5f));
		}
		else
		{
			// Backing a step (not turning round to walk away): eased back, held to the ground by its movement.
			// (Not stopped by another animal's body: a hunter stood over its kill backs off the carcass it's on.)
			const FVector Step = -Dir * FMath::Min(-Short, 120.f * Dt);
			FHitResult Hit;
			D->AddActorWorldOffset(Step, true, &Hit);
			if (Hit.bBlockingHit && Cast<ABFDino>(Hit.GetActor()))
			{
				D->AddActorWorldOffset(Step * (1.f - Hit.Time), false);
			}
		}
	}
	if (FVector::Dist(Snout, To) < 60.f)
	{
		A.MouthAheadCm = float(FVector::Dist2D(Snout, Me));
		return true;
	}
	return A.MouthS > 8.f;
}

void ABFLife::FlyTo(FBFAnimal& A, const FVector& To, float AltCm, bool bLand)
{
	// Steering in the air: toward To, holding AltCm above the ground or water under it (beating when low or slow);
	// coming in to land, a long descent onto it.
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	float Under = Me.Z - 100000.f;
	float W = 0.f;
	if (Island->GroundAt(FVector2D(Me), Under) && Island->WaterAt(FVector(Me.X, Me.Y, Under + 10.f), W))
	{
		Under = FMath::Max(Under, W);
	}
	const float Dist = FVector::Dist2D(Me, To);
	float WantZ = Under + AltCm;
	if (bLand)
	{
		WantZ = FMath::Lerp(To.Z, Under + AltCm, FMath::Clamp((Dist - 3000.f) / 15000.f, 0.f, 1.f));
	}
	const bool bSkim = AltCm < 300.f;          // a fishing dive: steep, easy on the wingbeats, down to the surface
	const float Pitch = FMath::Clamp((WantZ - Me.Z) / (bSkim ? 800.f : 1500.f) * 25.f, (bLand && Dist < 4000.f) || bSkim ? -35.f : -20.f, 25.f);
	const float Yaw = (To - Me).GetSafeNormal2D().Rotation().Yaw;
	D->SetFlightInput(bSkim ? 0.3f : 0.6f, Me.Z < WantZ - 500.f || D->GetVelocity().Size() < 900.f * ABFDino::GamePace, FRotator(Pitch, Yaw, 0.f));
}

void ABFLife::FlierTick(FBFAnimal& A, int32 Index, float Dt, float Now)
{
	// A Pteranodon's day: roosting at the colony (at night, tired, or full), fishing when hungry (out over a shoal,
	// diving to the surface, about two in five dives taking a fish; back to roost to rest), otherwise circling over the
	// colony on the wind; on the ground at the colony its courting and nesting are everyone's.
	ABFDino* D = A.Body.Get();
	const FVector Me = D->GetActorLocation();
	const float Rate = NeedRate * Dt;
	A.StomachKg = FMath::Max(0.f, A.StomachKg - A.DigestKgPerS * Rate);
	A.Thirst = 0.f;
	A.Tired = FMath::Clamp(A.Tired + (D->IsFlying() ? Rate / 900.f : -Rate / 60.f), 0.f, 1.f);
	for (FShoal& Sh : Shoals)
	{
		Sh.FishKg = FMath::Min(Sh.MaxKg, Sh.FishKg + Sh.MaxKg * Rate / 1800.f);       // (the shoal fills again in half an island hour)
	}
	const float Hunger = 1.f - A.StomachKg / FMath::Max(A.StomachMaxKg, 0.01f);
	const bool bNight = Island->Daylight() < 0.3f;
	const FVector Roost = Colony + FVector(FMath::Cos(float(Index)), FMath::Sin(float(Index)), 0.f) * 1500.f;
	if (NeedsFeeding(A))
	{
		// It can't fish yet: it waits at the colony (by its mother if she's down), calling when hungry; its parents bring
		// it fish. (Young fliers that can fly fish for themselves.)
		if (D->IsFlying())
		{
			D->Land();
		}
		A.Act = EBFAct::Rest;
		if (D->IsOnPerch())
		{
			// In the nest up a tree or on a ledge: it stays there till it can fly.
			A.Thought = Hunger > 0.4f ? TEXT("calling for food in the nest") : TEXT("waiting in the nest");
			if (A.Label)
			{
				A.Label->SetVisibility(false);
			}
			return;
		}
		A.Thought = Hunger > 0.4f ? TEXT("calling for food") : TEXT("waiting at the colony");
		const FVector Nest = Animals.IsValidIndex(A.Mother) && Animals[A.Mother].Body.IsValid() && !Animals[A.Mother].Body->IsFlying() ? Animals[A.Mother].Body->GetActorLocation() : Roost;
		if (FVector::Dist2D(Me, Nest) > 1500.f)
		{
			D->AddMovementInput((Nest - Me).GetSafeNormal2D(), 0.5f);
		}
		if (A.Label)
		{
			A.Label->SetVisibility(false);
		}
		return;
	}
	A.ThinkIn -= Dt;
	if (A.ThinkIn <= 0.f)
	{
		A.ThinkIn = 0.5f;
		// A parent with a hungry young one that can't fish: off fishing for it (unless itself starving or it's night).
		A.FeedFor = INDEX_NONE;
		if (!bNight && Hunger < 0.6f && A.Stage >= 4)
		{
			for (int32 j = 0; j < Animals.Num(); ++j)
			{
				const FBFAnimal& Y = Animals[j];
				if (!Y.bDead && Y.Body.IsValid() && (Y.Mother == Index || Y.Father == Index) && NeedsFeeding(Y) && Y.StomachKg < 0.6f * Y.StomachMaxKg)
				{
					A.FeedFor = j;
					break;
				}
			}
		}
		float Score[int32(EBFAct::Count)] = {};
		Score[int32(EBFAct::Rest)] = (bNight ? 1.2f : 0.f) + (A.Tired > 0.6f ? 1.f : 0.f) + (Hunger < 0.15f && A.FeedFor == INDEX_NONE && A.CarryKg <= 0.f ? 0.4f : 0.f);
		Score[int32(EBFAct::Hunt)] = !bNight && Shoals.Num() > 0 ? (A.FeedFor != INDEX_NONE || A.CarryKg > 0.f ? 1.f : Hunger > 0.35f ? Hunger + 0.3f : 0.f) : 0.f;
		Score[int32(EBFAct::Wander)] = bNight ? 0.f : 0.35f;
		BreedScores(A, Index, Now, Score);          // courting and nesting (in the air it lands at the colony first)
		if (A.bFemale && IsReady(A) && !bNight)
		{
			Score[int32(EBFAct::Rest)] += 0.6f;       // in condition she keeps to the colony (the males come to her there)
		}
		if (A.bFemale && A.bGravid && A.LayIn <= 0.f)
		{
			Score[int32(EBFAct::Nest)] = 2.6f;        // her eggs are due: laying comes before night, tiredness, hunger
		}
		else if (Nests.IsValidIndex(A.NestI) && Nests[A.NestI].Eggs > 0 && Hunger < 0.7f)
		{
			Score[int32(EBFAct::Nest)] = 1.5f;        // brooding: on the eggs day and night, off them only quite hungry
		}
		Score[int32(A.Act)] += 0.2f;
		int32 Best = 0;
		for (int32 i = 1; i < int32(EBFAct::Count); ++i)
		{
			Best = Score[i] > Score[Best] ? i : Best;
		}
		if (EBFAct(Best) != A.Act)
		{
			A.Passes = 0;
			Choose(A, EBFAct(Best), Now, EBFAct(Best) == EBFAct::Hunt ? FString::Printf(TEXT("hungry (%.0f of %.1f kg): off fishing"), A.StomachKg, A.StomachMaxKg)
				: EBFAct(Best) == EBFAct::Rest ? FString(bNight ? TEXT("night: roosting with the colony") : TEXT("tired: roosting")) : EBFAct(Best) == EBFAct::Wander ? FString(TEXT("soaring over the colony"))
				: BreedWhy);
		}
	}
	switch (A.Act)
	{
	case EBFAct::Hunt:
	{
		if (A.CarryKg > 0.f)
		{
			// A fish for its young: back to it, down beside it, the fish given.
			const int32 Y = Animals.IsValidIndex(A.FeedFor) && !Animals[A.FeedFor].bDead && Animals[A.FeedFor].Body.IsValid() ? A.FeedFor : INDEX_NONE;
			if (Y == INDEX_NONE)
			{
				A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + A.CarryKg);      // (no young to give it to: eaten)
				A.CarryKg = 0.f;
				break;
			}
			const FVector At = Animals[Y].Body->GetActorLocation();
			const int32 YP = PerchNear(At, 450.f);
			if (YP != INDEX_NONE && !AtPerch(A, YP))
			{
				GoPerch(A, YP, true);          // up to the nest it's in
				A.Thought = FString::Printf(TEXT("carrying a fish up to %s"), *Animals[Y].Name);
			}
			else if (D->IsFlying())
			{
				FlyTo(A, At, 3000.f, true);
				A.Thought = FString::Printf(TEXT("carrying a fish back to %s"), *Animals[Y].Name);
			}
			else if (FVector::Dist2D(Me, At) > 1200.f)
			{
				D->AddMovementInput((At - Me).GetSafeNormal2D(), 0.6f);
				A.Thought = FString::Printf(TEXT("bringing %s a fish"), *Animals[Y].Name);
			}
			else
			{
				FBFAnimal& Young = Animals[Y];
				Young.StomachKg = FMath::Min(Young.StomachMaxKg, Young.StomachKg + A.CarryKg);
				Young.EatenKg += A.CarryKg;
				++Feeds;
				D->StartIdle(EBFIdle::Sniff);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s fed %s a fish (%.1f kg; %.0f%% full now)"), *A.Name, *Young.Name, A.CarryKg, 100.f * Young.StomachKg / Young.StomachMaxKg);
				A.Thought = FString::Printf(TEXT("fed %s"), *Young.Name);
				A.CarryKg = 0.f;
				A.FeedFor = INDEX_NONE;
				Choose(A, EBFAct::Idle, Now, TEXT("fed its young"));
			}
			break;
		}
		// The nearest shoal with fish; out to it high, then a dive to the surface over it, and up again.
		int32 Sh = INDEX_NONE;
		for (int32 k = 0; k < Shoals.Num(); ++k)
		{
			Sh = Shoals[k].FishKg > 0.5f && (Sh == INDEX_NONE || FVector::Dist2D(Shoals[k].At, Me) < FVector::Dist2D(Shoals[Sh].At, Me)) ? k : Sh;
		}
		if (Sh == INDEX_NONE)
		{
			Choose(A, EBFAct::Wander, Now, TEXT("no fish showing: soaring"));
			break;
		}
		if (!D->IsFlying())
		{
			D->SetActorRotation(FRotator(0.f, (Shoals[Sh].At - Me).GetSafeNormal2D().Rotation().Yaw, 0.f));
			D->TakeOff();
			++A.Decisions;
			break;
		}
		const float Dist = FVector::Dist2D(Me, Shoals[Sh].At);
		const bool bDive = Dist < 6000.f;
		FlyTo(A, Shoals[Sh].At, bDive ? 50.f : 4000.f, false);
		A.Thought = bDive ? FString::Printf(TEXT("diving on a shoal (%.0f m up)"), (Me.Z - Shoals[Sh].At.Z) / 100.f) : FString::Printf(TEXT("hungry: flying out to a shoal %s off"), *Metres(Dist));
		if (bDive && Me.Z - Shoals[Sh].At.Z < 900.f && Dist < 4000.f && A.BiteClock <= 0.f)
		{
			A.BiteClock = 4.f;
			++Dives;
			++A.Passes;
			if (FMath::FRand() < 0.4f)
			{
				const float Kg = FMath::Min(Shoals[Sh].FishKg, 0.6f);
				Shoals[Sh].FishKg -= Kg;
				++Catches;
				if (A.FeedFor != INDEX_NONE)
				{
					A.CarryKg = Kg;           // for its young: carried back
				}
				else
				{
					A.StomachKg = FMath::Min(A.StomachMaxKg, A.StomachKg + Kg);
					A.EatenKg += Kg;
					++A.Meals;
				}
				A.Thought = FString::Printf(TEXT("caught a fish (%.1f kg): %.1f of %.1f kg in its stomach"), Kg, A.StomachKg, A.StomachMaxKg);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %s caught a fish (%.1f kg) on its %d%s dive"), *A.Name, Kg, A.Passes, A.Passes == 1 ? TEXT("st") : A.Passes == 2 ? TEXT("nd") : A.Passes == 3 ? TEXT("rd") : TEXT("th"));
			}
		}
		A.BiteClock -= Dt;
		break;
	}
	case EBFAct::Rest:
	{
		const int32 P = PerchFor(A, Index);
		if (P != INDEX_NONE)
		{
			// Up on its perch: a shelf in a nest tree, or a ledge.
			const TCHAR* Where = Perches[P].bLedge ? TEXT("on its ledge") : TEXT("up in its nest tree");
			if (GoPerch(A, P, false))
			{
				A.Path.Reset();
				A.Thought = FString::Printf(TEXT("roosting %s%s"), Where, bNight ? TEXT(" (night)") : TEXT(""));
			}
			else
			{
				A.Thought = FString::Printf(TEXT("flying back to roost %s"), Where);
			}
			break;
		}
		if (D->IsFlying())
		{
			FlyTo(A, FVector(Roost.X, Roost.Y, Colony.Z), 3000.f, true);
			A.Thought = TEXT("flying back to roost");
		}
		else
		{
			A.Path.Reset();
			A.Thought = bNight ? TEXT("roosting (night)") : TEXT("roosting");
		}
		break;
	}
	case EBFAct::Wander:
	{
		if (!D->IsFlying())
		{
			D->TakeOff();
			break;
		}
		// A wide circle over the colony (soaring: few wingbeats).
		A.CircleAng += Dt * 0.12f;
		const FVector Ahead = Colony + FVector(FMath::Cos(A.CircleAng), FMath::Sin(A.CircleAng), 0.f) * 9000.f;
		FlyTo(A, Ahead, 5000.f, false);
		A.Thought = TEXT("soaring over the colony");
		break;
	}
	case EBFAct::Court:
	{
		// Courting by air: to her, down beside her; then the display on the ground.
		const bool bHer = Animals.IsValidIndex(A.CourtTarget) && Animals[A.CourtTarget].Body.IsValid();
		const FVector Her = bHer ? Animals[A.CourtTarget].Body->GetActorLocation() : Roost;
		const float ToHer = FVector::Dist2D(Me, Her);
		const int32 HerPerch = bHer && !Animals[A.CourtTarget].Body->IsFlying() ? PerchNear(Her, 450.f) : INDEX_NONE;
		if (HerPerch != INDEX_NONE)
		{
			// She's up on her perch: in beside her there, then the display.
			if (GoPerch(A, HerPerch, true))
			{
				DoBreed(A, Index, Dt, Now);
				Steer(A, Index, Dt);
			}
			else
			{
				A.Thought = FString::Printf(TEXT("flying up to %s on her perch to court her"), *Animals[A.CourtTarget].Name);
			}
			break;
		}
		if (!D->IsFlying() && ToHer > 2500.f)
		{
			D->SetActorRotation(FRotator(0.f, (Her - Me).GetSafeNormal2D().Rotation().Yaw, 0.f));
			D->TakeOff();
		}
		else if (D->IsFlying())
		{
			float G = Her.Z;
			Island->GroundAt(FVector2D(Her), G);
			FlyTo(A, FVector(Her.X, Her.Y, G), 2500.f, true);
			A.Thought = bHer ? FString::Printf(TEXT("flying to %s to court her"), *Animals[A.CourtTarget].Name) : TEXT("flying back to the colony");
		}
		else
		{
			DoBreed(A, Index, Dt, Now);
			Steer(A, Index, Dt);
		}
		break;
	}
	case EBFAct::Nest:
	{
		// Her nest up on her perch (a tree's shelf, a ledge): there first; the eggs laid in the middle of it.
		const int32 P = PerchFor(A, Index);
		if (P != INDEX_NONE && !AtPerch(A, P))
		{
			GoPerch(A, P, false);
			A.Thought = A.bGravid ? TEXT("carrying eggs: flying up to her nest") : TEXT("flying back to her nest");
			break;
		}
		if (P == INDEX_NONE && D->IsFlying())
		{
			FlyTo(A, FVector(Roost.X, Roost.Y, Colony.Z), 3000.f, true);
			break;
		}
		if (A.bGravid && A.NestSite.IsZero())
		{
			A.NestSite = P != INDEX_NONE ? Perches[P].Top : Me;          // (no perches: where it lands at the colony)
		}
		DoBreed(A, Index, Dt, Now);
		Steer(A, Index, Dt);
		break;
	}
	default:
		break;
	}
	if (A.Label)
	{
		A.Label->SetVisibility(false);
	}
}

bool ABFLife::NeedsFeeding(const FBFAnimal& Y) const
{
	// A hatchling, or a young one whose body can't fly yet, can't fish.
	return Y.bFlier && Y.Stage <= 2 && Y.Body.IsValid() && (Y.Stage == 1 || Y.Body->GetSpecies().Flight != EBFFlight::Flies || !Y.Body->CanFly());
}

void ABFLife::DrawFish(float Now)
{
	// One drawn fish to every 0.6 kg in a shoal (up to 12): circling a few metres out, a hand's depth under the surface,
	// now and then one leaping clear (a second's arc, a metre up). A slim silver body, long axis along its way.
	if (!FishComp)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Sphere)
		{
			return;
		}
		FishComp = NewObject<UInstancedStaticMeshComponent>(this);
		FishComp->SetStaticMesh(Sphere);
		FishComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FishComp->SetCanEverAffectNavigation(false);
		FishComp->SetCastShadow(false);
		if (Base)
		{
			UMaterialInstanceDynamic* Scales = UMaterialInstanceDynamic::Create(Base, this);
			Scales->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.08f, 0.11f, 0.14f));
			Scales->SetScalarParameterValue(TEXT("Roughness"), 0.6f);      // (shinier, they mirrored the sky: white pills)
			FishComp->SetMaterial(0, Scales);
		}
		FishComp->SetupAttachment(GetRootComponent());
		FishComp->RegisterComponent();
		// A tail fin each (the engine's cone, flattened into an upright fan behind the body).
		UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		if (Cone)
		{
			FinComp = NewObject<UInstancedStaticMeshComponent>(this);
			FinComp->SetStaticMesh(Cone);
			FinComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			FinComp->SetCanEverAffectNavigation(false);
			FinComp->SetCastShadow(false);
			FinComp->SetMaterial(0, FishComp->GetMaterial(0));
			FinComp->SetupAttachment(GetRootComponent());
			FinComp->RegisterComponent();
		}
	}
	TArray<FTransform> Xf, Fins;
	for (int32 k = 0; k < Shoals.Num(); ++k)
	{
		const FShoal& Sh = Shoals[k];
		const int32 Count = FMath::Clamp(FMath::RoundToInt(Sh.FishKg / 0.6f), 0, 12);
		for (int32 f = 0; f < Count; ++f)
		{
			const float Seed = float(k * 31 + f * 7);
			const float Radius = 300.f + 350.f * FMath::Frac(FMath::Sin(Seed) * 437.585f);
			const float Speed = 0.35f + 0.25f * FMath::Frac(FMath::Sin(Seed * 1.7f) * 912.31f);
			const float Ang = Seed + Now * Speed * (f % 2 == 0 ? 1.f : -1.f);
			FVector P = Sh.At + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * Radius + FVector(0.f, 0.f, -20.f - 12.f * (f % 3));
			const float Tangent = FMath::RadiansToDegrees(Ang) + (f % 2 == 0 ? 90.f : -90.f);
			float Pitch = 0.f;
			// A leap: each fish about every 6 s, for 1.2 s, up to 1.5 m.
			const float Cycle = FMath::Fmod(Now + 6.f * FMath::Frac(FMath::Sin(Seed * 3.1f) * 77.7f), 6.f);
			if (Cycle < 1.2f)
			{
				const float U = Cycle / 1.2f;
				P.Z += 30.f + 150.f * 4.f * U * (1.f - U);
				Pitch = FMath::Lerp(40.f, -40.f, U);
			}
			const FQuat Body = FRotator(Pitch, Tangent, 0.f).Quaternion();
			Xf.Add(FTransform(Body, P, FVector(0.6f, 0.14f, 0.18f)));
			// The tail: behind the body, its point toward it, a beat side to side.
			const float Beat = 18.f * FMath::Sin(Now * 9.f + Seed);
			const FQuat TailRot = Body * FRotator(0.f, Beat, 0.f).Quaternion() * FQuat(FVector::RightVector, PI / 2.f);
			Fins.Add(FTransform(TailRot, P - Body.GetForwardVector() * 34.f, FVector(0.22f, 0.03f, 0.16f)));
		}
	}
	if (FinComp)
	{
		if (Fins.Num() != FinComp->GetInstanceCount())
		{
			FinComp->ClearInstances();
			FinComp->AddInstances(Fins, false, true, false);
		}
		else if (Fins.Num() > 0)
		{
			FinComp->BatchUpdateInstancesTransforms(0, Fins, true, true, false);
		}
	}
	if (Xf.Num() != FishComp->GetInstanceCount())
	{
		FishComp->ClearInstances();
		FishComp->AddInstances(Xf, false, true, false);
	}
	else if (Xf.Num() > 0)
	{
		FishComp->BatchUpdateInstancesTransforms(0, Xf, true, true, false);
	}
}
