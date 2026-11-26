#include "BFGameMode.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "BFDino.h"
#include "BFPlayerController.h"
#include "BFWorld.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Arena layout (cm). The flat field is west of the origin; the ramps climb toward +X, side by side along Y.
static constexpr float RampStartX = 6000.f;
static constexpr float RampRise = 1000.f;
static constexpr float RampWidth = 3500.f;
static constexpr float RampGap = 1000.f;
static constexpr float PlateauLength = 4000.f;

float ABFGameMode::RampMiddleX(int32 Ramp)
{
	return RampStartX + 0.5f * RampRise / FMath::Tan(FMath::DegreesToRadians(RampAngle(Ramp)));
}

float ABFGameMode::RampFootX()
{
	return RampStartX;
}

int32 ABFGameMode::NumBaseSpecies() const
{
	return Species.FilterByPredicate([](const FBFSpecies& S) { return !S.IsSplice(); }).Num();
}

bool ABFGameMode::WaterAt(const FVector& At, float& SurfaceZ)
{
	if (ABFWorld::Active && ABFWorld::Active->WaterAt(At, SurfaceZ))
	{
		return true;               // the island's sea, lakes and rivers
	}
	SurfaceZ = WaterZ;
	return At.X > PoolMinX && At.X < PoolMaxX && At.Y > PoolMinY && At.Y < PoolMaxY && At.Z < WaterZ + 2000.f;
}

FVector ABFGameMode::CurrentAt(const FVector& At)
{
	return ABFWorld::Active ? ABFWorld::Active->CurrentAt(At) : FVector::ZeroVector;
}

static constexpr float CliffDepth = 4000.f;
static constexpr float CliffWidth = 3000.f;

static float RampY(int32 Ramp)
{
	return (Ramp - (ABFGameMode::NumRamps - 1) * 0.5f) * (RampWidth + RampGap);
}

ABFGameMode::ABFGameMode()
{
	PlayerControllerClass = ABFPlayerController::StaticClass();
	DefaultPawnClass = nullptr;   // the controller spawns the dinosaur
}

void ABFGameMode::StartPlay()
{
	if (RigFolder.IsEmpty())
	{
		RigFolder = FPaths::Combine(FPaths::ProjectDir(), TEXT("Rigs"));
	}
	LoadNames();
	for (const FString& Key : BFSpeciesKeys(RigFolder))
	{
		FBFSpecies S;
		FString Error;
		if (S.Load(RigFolder, Key, Error))
		{
			KeepAssets.Append({ S.Mesh, S.Locomotion, S.FlightBlend, S.BiteAnim, S.RoarAnim });
			ApplyName(S);
			Species.Add(MoveTemp(S));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[BoneForge] %s"), *Error);
			if (!Key.StartsWith(TEXT("ed_")))        // a creature grown in the editor but not walked yet isn't an error
			{
				LoadErrors.Add(Error);
			}
		}
	}
	BuildArena();
	Super::StartPlay();
}

int32 ABFGameMode::AddSpecies(const FString& Key, USkeletalMesh* Mesh, FString& Error)
{
	FBFSpecies S;
	S.PrebuiltMesh = Mesh;          // its body was just grown: never an older mesh of the same name
	if (!S.Load(RigFolder, Key, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[BoneForge] %s"), *Error);
		return INDEX_NONE;
	}
	KeepAssets.Append({ S.Mesh, S.Locomotion, S.FlightBlend, S.BiteAnim, S.RoarAnim });     // (a built ragdoll is rooted)
	ApplyName(S);
	const int32 Old = Species.IndexOfByPredicate([&](const FBFSpecies& X) { return X.Key == Key; });
	if (Old != INDEX_NONE)
	{
		Species[Old] = MoveTemp(S);
		return Old;
	}
	return Species.Add(MoveTemp(S));
}

// ---- Your creatures: names, removing and putting back ----

namespace
{
	// Every file a creature is made of, by folder under bone_forge and the end of its name after the key. Exact names, not
	// a pattern: one creature's key can begin another's (ed_trex_shbrachio, ed_trex_shbrachio90).
	struct FCreatureFile { const TCHAR* Folder; const TCHAR* Suffix; };
	const FCreatureFile CreatureFiles[] =
	{
		{ TEXT("rig_joints"), TEXT("_rig.json") }, { TEXT("rig_joints"), TEXT("_joints.png") },
		{ TEXT("body_shapes"), TEXT("_body.obj") }, { TEXT("body_shapes"), TEXT("_body_chains.json") },
		{ TEXT("body_shapes"), TEXT("_features.obj") }, { TEXT("body_shapes"), TEXT("_grown.obj") },
		{ TEXT("splice"), TEXT("_report.txt") },
		{ TEXT("unreal/out"), TEXT("_skeletal.glb") }, { TEXT("unreal/out"), TEXT("_unreal_report.txt") },
	};

	FString ReadCreatureName(const FString& RigPath)
	{
		FString Text, Name;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *RigPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
		{
			Root->TryGetStringField(TEXT("creature"), Name);
		}
		return Name;
	}
}

FString ABFGameMode::BoneForgeDir() const
{
	FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(RigFolder, TEXT("..")));
	FPaths::CollapseRelativeDirectories(Dir);
	return Dir;
}

void ABFGameMode::LoadNames()
{
	GivenNames.Reset();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Names = nullptr;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(BoneForgeDir(), TEXT("my_creatures.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid() && Root->TryGetObjectField(TEXT("names"), Names))
	{
		for (const auto& It : (*Names)->Values)
		{
			GivenNames.Add(FString(It.Key), It.Value->AsString());      // (the JSON object's own key type)
		}
	}
	double Pace = 0.0;
	if (Root.IsValid() && Root->TryGetNumberField(TEXT("game_pace"), Pace))
	{
		ABFDino::GamePace = FMath::Clamp(float(Pace), 1.f, 3.f);
	}
	Looks.Reset();
	const TSharedPtr<FJsonObject>* LookObj = nullptr;
	if (Root.IsValid() && Root->TryGetObjectField(TEXT("looks"), LookObj))
	{
		for (const auto& It : (*LookObj)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FBFLook L;
			double V = 0.0;
			O->TryGetStringField(TEXT("scheme"), L.Scheme);
			O->TryGetStringField(TEXT("covering"), L.Covering);
			L.HueDeg = O->TryGetNumberField(TEXT("hue"), V) ? (float)V : 0.f;
			L.Shade = O->TryGetNumberField(TEXT("shade"), V) ? (float)V : 0.f;
			Looks.Add(FString(It.Key), L);
		}
	}
}

ABFWorld* ABFGameMode::MakeIsland(int32 Seed)
{
	if (Island)
	{
		Island->Destroy();
		Island = nullptr;
	}
	Island = GetWorld()->SpawnActor<ABFWorld>();
	Island->Generate(Seed);
	return Island;
}

TArray<int32> ABFGameMode::GetKeptIslands() const
{
	TArray<int32> Seeds;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(BoneForgeDir(), TEXT("my_worlds.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid() && Root->TryGetArrayField(TEXT("islands"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			Seeds.AddUnique(int32(V->AsNumber()));
		}
	}
	return Seeds;
}

void ABFGameMode::KeepIsland(int32 Seed)
{
	TArray<int32> Seeds = GetKeptIslands();
	Seeds.AddUnique(Seed);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("about"), TEXT("Islands kept in the Bone Forge testing ground, by seed: the same seed makes the same island. Written by the game."));
	TArray<TSharedPtr<FJsonValue>> List;
	for (const int32 S : Seeds)
	{
		List.Add(MakeShared<FJsonValueNumber>(S));
	}
	Root->SetArrayField(TEXT("islands"), List);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *FPaths::Combine(BoneForgeDir(), TEXT("my_worlds.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FBFLook ABFGameMode::LookOf(const FString& Key) const
{
	const FBFLook* L = Looks.Find(Key);
	return L ? *L : FBFLook();
}

void ABFGameMode::SetLook(const FString& Key, const FBFLook& Look)
{
	if (Look.IsOwn())
	{
		Looks.Remove(Key);
	}
	else
	{
		Looks.Add(Key, Look);
	}
	SaveNames();
}

void ABFGameMode::SaveNames() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Names = MakeShared<FJsonObject>();
	TArray<FString> Keys;
	GivenNames.GetKeys(Keys);
	Keys.Sort();
	for (const FString& K : Keys)
	{
		Names->SetStringField(K, GivenNames[K]);
	}
	Root->SetStringField(TEXT("about"), TEXT("Names given to creatures in the Bone Forge testing ground (key: name), and their looks. Written by the game."));
	Root->SetObjectField(TEXT("names"), Names);
	TSharedRef<FJsonObject> LookObj = MakeShared<FJsonObject>();
	TArray<FString> LookKeys;
	Looks.GetKeys(LookKeys);
	LookKeys.Sort();
	for (const FString& K : LookKeys)
	{
		const FBFLook& L = Looks[K];
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("scheme"), L.Scheme);
		O->SetNumberField(TEXT("hue"), L.HueDeg);
		O->SetNumberField(TEXT("shade"), L.Shade);
		O->SetStringField(TEXT("covering"), L.Covering);
		LookObj->SetObjectField(K, O);
	}
	Root->SetObjectField(TEXT("looks"), LookObj);
	Root->SetNumberField(TEXT("game_pace"), ABFDino::GamePace);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *FPaths::Combine(BoneForgeDir(), TEXT("my_creatures.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void ABFGameMode::ApplyName(FBFSpecies& S)
{
	BuiltNames.Add(S.Key, S.Name);
	if (const FString* Given = GivenNames.Find(S.Key))
	{
		S.Name = *Given;
	}
}

FString ABFGameMode::GivenNameOf(const FString& Key) const
{
	const FString* Given = GivenNames.Find(Key);
	return Given ? *Given : FString();
}

FString ABFGameMode::RenameCreature(const FString& Key, const FString& InName)
{
	FString Name = InName.TrimStartAndEnd().Replace(TEXT("\n"), TEXT(" "));
	if (Name.IsEmpty())
	{
		GivenNames.Remove(Key);
	}
	else
	{
		// No two of your creatures share a name (the menu would show two the same).
		auto Taken = [&](const FString& N)
		{
			return Species.ContainsByPredicate([&](const FBFSpecies& S) { return S.Key != Key && S.Name.Equals(N, ESearchCase::IgnoreCase); });
		};
		const FString Stem = Name;
		for (int32 n = 2; Taken(Name) && n < 1000; ++n)
		{
			Name = FString::Printf(TEXT("%s %d"), *Stem, n);
		}
		GivenNames.Add(Key, Name);
	}
	SaveNames();
	for (FBFSpecies& S : Species)
	{
		if (S.Key == Key)
		{
			const FString* Built = BuiltNames.Find(Key);
			S.Name = Name.IsEmpty() ? (Built ? *Built : Key) : Name;
			return S.Name;
		}
	}
	return Name;
}

bool ABFGameMode::RemoveCreature(const FString& Key, FString& Error)
{
	if (!IsYours(Key))
	{
		Error = TEXT("Only creatures made in the splicing editor can be removed.");
		return false;
	}
	const FString Dir = BoneForgeDir();
	const FString Bin = FPaths::Combine(Dir, TEXT("removed"), Key);
	IFileManager& FM = IFileManager::Get();
	int32 Moved = 0;
	for (const FCreatureFile& F : CreatureFiles)
	{
		const FString From = FPaths::Combine(Dir, F.Folder, Key + F.Suffix);
		if (!FM.FileExists(*From))
		{
			continue;
		}
		const FString To = FPaths::Combine(Bin, F.Folder, Key + F.Suffix);
		FM.MakeDirectory(*FPaths::GetPath(To), true);
		if (!FM.Move(*To, *From, true, true))
		{
			Error = FString::Printf(TEXT("Couldn't move %s into the removed folder (is it open somewhere?)."), *FPaths::GetCleanFilename(From));
			return false;
		}
		++Moved;
	}
	if (Moved == 0)
	{
		Error = TEXT("Its files weren't found.");
		return false;
	}
	Species.RemoveAll([&](const FBFSpecies& S) { return S.Key == Key; });
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] removed %s: %d files moved to %s"), *Key, Moved, *Bin);
	return true;
}

bool ABFGameMode::RestoreFiles(const FString& Key, FString& Error)
{
	const FString Dir = BoneForgeDir();
	const FString Bin = FPaths::Combine(Dir, TEXT("removed"), Key);
	IFileManager& FM = IFileManager::Get();
	if (FM.FileExists(*FPaths::Combine(Dir, TEXT("rig_joints"), Key + TEXT("_rig.json"))))
	{
		Error = TEXT("A creature with the same build is already there.");
		return false;
	}
	int32 Moved = 0;
	for (const FCreatureFile& F : CreatureFiles)
	{
		const FString From = FPaths::Combine(Bin, F.Folder, Key + F.Suffix);
		if (FM.FileExists(*From))
		{
			if (!FM.Move(*FPaths::Combine(Dir, F.Folder, Key + F.Suffix), *From, false, true))
			{
				Error = FString::Printf(TEXT("Couldn't move %s back."), *FPaths::GetCleanFilename(From));
				return false;
			}
			++Moved;
		}
	}
	if (Moved == 0)
	{
		Error = TEXT("Nothing of it was in the removed folder.");
		return false;
	}
	// The folders it left, if empty: one at a time and never a tree, so nothing still in them can go.
	for (const TCHAR* Sub : { TEXT("rig_joints"), TEXT("body_shapes"), TEXT("splice"), TEXT("unreal/out"), TEXT("unreal") })
	{
		FM.DeleteDirectory(*FPaths::Combine(Bin, Sub), false, false);
	}
	FM.DeleteDirectory(*Bin, false, false);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] put back %s: %d files"), *Key, Moved);
	return true;
}

TArray<ABFGameMode::FRemoved> ABFGameMode::GetRemoved() const
{
	TArray<FRemoved> Out;
	TArray<FString> Dirs;
	const FString Bin = FPaths::Combine(BoneForgeDir(), TEXT("removed"));
	IFileManager::Get().FindFiles(Dirs, *FPaths::Combine(Bin, TEXT("*")), false, true);
	for (const FString& K : Dirs)
	{
		const FString Rig = FPaths::Combine(Bin, K, TEXT("rig_joints"), K + TEXT("_rig.json"));
		if (IFileManager::Get().FileExists(*Rig))
		{
			const FString Given = GivenNameOf(K);
			Out.Add({ K, Given.IsEmpty() ? ReadCreatureName(Rig) : Given });
		}
	}
	Out.Sort([](const FRemoved& A, const FRemoved& B) { return A.Name < B.Name; });
	return Out;
}

AActor* ABFGameMode::SpawnBox(const FVector& Center, const FVector& SizeCm, const FRotator& Rotation, const FLinearColor& Tint)
{
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Center, Rotation);
	UStaticMeshComponent* C = A->GetStaticMeshComponent();
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	C->SetWorldScale3D(SizeCm / 100.f);
	C->SetMaterial(0, TintedMaterial(Tint, A));
	return A;
}

UMaterialInterface* ABFGameMode::TintedMaterial(const FLinearColor& Tint, UObject* Outer)
{
	static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Outer);
	M->SetVectorParameterValue(TEXT("Color"), Tint);
	return M;
}

void ABFGameMode::SpawnLabel(const FVector& At, const FString& Text, float Size)
{
	// Faces -X: readable from the spawn points, which all look toward +X.
	ATextRenderActor* T = GetWorld()->SpawnActor<ATextRenderActor>(At, FRotator(0.f, 180.f, 0.f));
	UTextRenderComponent* C = T->GetTextRender();
	C->SetText(FText::FromString(Text));
	C->SetWorldSize(Size);
	C->SetHorizontalAlignment(EHTA_Center);
	C->SetTextRenderColor(FColor(255, 240, 200));
}

void ABFGameMode::BuildArena()
{
	UWorld* W = GetWorld();

	// Ground: 600 m x 400 m, top surface at Z 0, a checkerboard of 5 m tiles so speed and stride are easy to judge.
	{
		const float Tile = 500.f;
		const int32 NX = 120, NY = 80;
		AActor* Floor = W->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		USceneComponent* Root = NewObject<USceneComponent>(Floor, TEXT("Root"));
		Floor->SetRootComponent(Root);
		Root->RegisterComponent();
		UInstancedStaticMeshComponent* Tiles[2];
		const FLinearColor Colors[2] = { FLinearColor(0.32f, 0.36f, 0.28f), FLinearColor(0.42f, 0.46f, 0.36f) };
		for (int32 c = 0; c < 2; ++c)
		{
			Tiles[c] = NewObject<UInstancedStaticMeshComponent>(Floor);
			Tiles[c]->SetMobility(EComponentMobility::Movable);
			Tiles[c]->SetupAttachment(Root);
			Tiles[c]->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Tiles[c]->SetMaterial(0, TintedMaterial(Colors[c], Floor));
			Tiles[c]->RegisterComponent();
		}
		for (int32 x = 0; x < NX; ++x)
		{
			for (int32 y = 0; y < NY; ++y)
			{
				const FVector C((x - NX * 0.5f + 0.5f) * Tile, (y - NY * 0.5f + 0.5f) * Tile, -50.f);
				if (C.X > PoolMinX && C.X < PoolMaxX && C.Y > PoolMinY && C.Y < PoolMaxY)
				{
					continue;     // the pool is dug here
				}
				Tiles[(x + y) & 1]->AddInstance(FTransform(FRotator::ZeroRotator, C, FVector(Tile / 100.f, Tile / 100.f, 1.f)), true);
			}
		}
	}

	// Ramps: each rises 10 m from X = 60 m, then a 40 m plateau. Labels stand at the foot of each ramp.
	for (int32 i = 0; i < NumRamps; ++i)
	{
		const float Angle = RampAngle(i);
		const float Rad = FMath::DegreesToRadians(Angle);
		const float Run = RampRise / FMath::Tan(Rad);
		const float SlopeLen = RampRise / FMath::Sin(Rad);
		const float Thick = 200.f;
		const float Y = RampY(i);
		// Slab whose top face runs from (RampStartX, 0) to (RampStartX + Run, RampRise).
		const FVector TopMid(RampStartX + Run * 0.5f, Y, RampRise * 0.5f);
		const FVector Normal(-FMath::Sin(Rad), 0.f, FMath::Cos(Rad));
		SpawnBox(TopMid - Normal * Thick * 0.5f, FVector(SlopeLen, RampWidth, Thick), FRotator(Angle, 0.f, 0.f), FLinearColor(0.55f, 0.45f, 0.3f));
		// Fill under the slab so nothing can walk underneath it.
		SpawnBox(FVector(RampStartX + Run + PlateauLength * 0.5f, Y, RampRise * 0.5f), FVector(PlateauLength, RampWidth, RampRise), FRotator::ZeroRotator, FLinearColor(0.5f, 0.42f, 0.3f));
		SpawnLabel(FVector(RampStartX - 600.f, Y, 500.f), FString::Printf(TEXT("%.0f°"), Angle), 600.f);
	}
	SpawnLabel(FVector(-1500.f, 0.f, 600.f), TEXT("FLAT"), 500.f);

	// The jump lane (lab plan item 4): three logs across the field south of the spawn, 0.5, 1 and 2 m high, run at from
	// the west; clear of the test run's paths (east from the spawn, then west and turning north).
	{
		const float LaneY = -5000.f;
		const float Logs[][2] = { { -1500.f, 50.f }, { 500.f, 100.f }, { 2500.f, 200.f } };     // x, height (cm)
		for (const auto& L : Logs)
		{
			SpawnBox(FVector(L[0], LaneY, L[1] * 0.5f), FVector(40.f, 800.f, L[1]), FRotator::ZeroRotator, FLinearColor(0.36f, 0.26f, 0.16f));
			SpawnLabel(FVector(L[0], LaneY - 500.f, L[1] + 150.f), L[1] < 100.f ? TEXT("0.5 m") : FString::Printf(TEXT("%.0f m"), L[1] / 100.f), 160.f);
		}
	}

	// The cliff (Prototype 3, glide tests): 20 m high, its edge facing +X over the flat field.
	SpawnBox(FVector(CliffEdgeX - CliffDepth * 0.5f, 0.f, CliffTopZ * 0.5f), FVector(CliffDepth, CliffWidth, CliffTopZ), FRotator::ZeroRotator, FLinearColor(0.45f, 0.42f, 0.38f));
	SpawnLabel(FVector(CliffEdgeX + 300.f, 0.f, CliffTopZ - 700.f), TEXT("20 m"), 400.f);

	// The pool (swim tests): 12 m deep with a 15 degree slope in from its west end, so an animal walks in, wades and
	// floats off its feet once the water is deep enough for it. Its walls and floor are grey, the water see-through.
	{
		const FVector2D Mid(0.5f * (PoolMinX + PoolMaxX), 0.5f * (PoolMinY + PoolMaxY));
		const FVector2D Size(PoolMaxX - PoolMinX, PoolMaxY - PoolMinY);
		const FLinearColor Stone(0.38f, 0.38f, 0.36f);
		const float Wall = 200.f;
		SpawnBox(FVector(Mid.X, Mid.Y, -PoolDepth - 100.f), FVector(Size.X + 2.f * Wall, Size.Y + 2.f * Wall, 200.f), FRotator::ZeroRotator, Stone);
		SpawnBox(FVector(Mid.X, PoolMinY - Wall * 0.5f, -PoolDepth * 0.5f), FVector(Size.X + 2.f * Wall, Wall, PoolDepth), FRotator::ZeroRotator, Stone);
		SpawnBox(FVector(Mid.X, PoolMaxY + Wall * 0.5f, -PoolDepth * 0.5f), FVector(Size.X + 2.f * Wall, Wall, PoolDepth), FRotator::ZeroRotator, Stone);
		SpawnBox(FVector(PoolMaxX + Wall * 0.5f, Mid.Y, -PoolDepth * 0.5f), FVector(Wall, Size.Y, PoolDepth), FRotator::ZeroRotator, Stone);
		SpawnBox(FVector(PoolMinX - Wall * 0.5f, Mid.Y, -PoolDepth * 0.5f), FVector(Wall, Size.Y, PoolDepth), FRotator::ZeroRotator, Stone);
		const float Rad = FMath::Atan2(PoolDepth, PoolEntryRun);
		const float Slope = FMath::Sqrt(PoolDepth * PoolDepth + PoolEntryRun * PoolEntryRun);
		const FVector Normal(FMath::Sin(Rad), 0.f, FMath::Cos(Rad));
		SpawnBox(FVector(PoolMinX + PoolEntryRun * 0.5f, Mid.Y, -PoolDepth * 0.5f) - Normal * 100.f, FVector(Slope, Size.Y, 200.f),
			FRotator(-FMath::RadiansToDegrees(Rad), 0.f, 0.f), FLinearColor(0.5f, 0.45f, 0.36f));
		for (int32 i = 0; i < 6; ++i)       // filled in under the slope, in steps that stay below it
		{
			const float X0 = PoolMinX + PoolEntryRun * i / 6.f, X1 = PoolMinX + PoolEntryRun * (i + 1) / 6.f;
			const float Top = -PoolDepth * (i + 1) / 6.f - 220.f;
			if (Top > -PoolDepth)
			{
				SpawnBox(FVector(0.5f * (X0 + X1), Mid.Y, 0.5f * (Top - PoolDepth)), FVector(X1 - X0, Size.Y, Top + PoolDepth), FRotator::ZeroRotator, Stone);
			}
		}
		AActor* Water = SpawnBox(FVector(Mid.X, Mid.Y, 0.5f * (WaterZ - PoolDepth)), FVector(Size.X, Size.Y, WaterZ + PoolDepth), FRotator::ZeroRotator, FLinearColor(0.05f, 0.2f, 0.25f));
		UStaticMeshComponent* WC = Cast<AStaticMeshActor>(Water)->GetStaticMeshComponent();
		WC->SetCollisionProfileName(TEXT("NoCollision"));
		WC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Water->SetActorEnableCollision(false);       // nothing stands on the water: TickSwim floats what floats
		WC->SetCastShadow(false);
		if (UMaterialInterface* WaterMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeWater.M_BoneForgeWater")))
		{
			WC->SetMaterial(0, WaterMat);
		}
		SpawnLabel(FVector(PoolMinX - 600.f, Mid.Y, 500.f), TEXT("POOL 12 m"), 450.f);
	}

	// Light and sky.
	ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-40.f, -60.f, 0.f));
	SunLight = Sun;
	Sun->GetComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetComponent()->SetIntensity(8.f);
	if (UDirectionalLightComponent* D = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		D->SetAtmosphereSunLight(true);
	}
	W->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	ASkyLight* Sky = W->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SetRealTimeCapture(true);
	Sky->GetLightComponent()->RecaptureSky();
	Fog = W->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator)->GetComponent();
	Fog->SetFogDensity(0.005f);
	// The moon (off until the island's night).
	MoonLight = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-35.f, 120.f, 0.f));
	MoonLight->GetComponent()->SetMobility(EComponentMobility::Movable);
	MoonLight->GetComponent()->SetIntensity(0.f);
	MoonLight->GetComponent()->SetLightColor(FLinearColor(0.55f, 0.65f, 1.f));
	MoonLight->GetComponent()->SetCastShadows(true);
}

void ABFGameMode::SetSkyAt(float Hours)
{
	if (!SunLight || !MoonLight)
	{
		return;
	}
	// Sunrise at 6, noon at 12 (62 deg up, in the south), sunset at 18; it comes up in the east and goes down in the west.
	const float Elev = 62.f * FMath::Sin((Hours - 6.f) / 24.f * 2.f * PI);
	const float Yaw = 90.f + (Hours - 6.f) * 15.f;
	SunLight->SetActorRotation(FRotator(-Elev, Yaw, 0.f));
	const float Day = FMath::SmoothStep(-4.f, 8.f, Elev);
	SunLight->GetComponent()->SetIntensity(8.f * Day);
	MoonLight->SetActorRotation(FRotator(-35.f, Yaw + 180.f, 0.f));
	MoonLight->GetComponent()->SetIntensity(0.35f * (1.f - Day));
	// The eye doesn't make night into day: the exposure darkens with the light.
	if (!NightPost)
	{
		NightPost = GetWorld()->SpawnActor<APostProcessVolume>();
		NightPost->bUnbound = true;
		NightPost->Settings.bOverride_AutoExposureBias = true;
	}
	NightPost->Settings.AutoExposureBias = -2.5f * (1.f - Day);
	// (2026-11-25, Raven: "nothing rough ... as natural as it can be". Down a gorge or behind a fall the eye opened up
	// three or four stops, so walls in shade came out near white and blue, like snow or plaster. By day it may open up
	// only so far below the open island's light: shade stays shade. -bfoldexposure: as before; -bfevmin=N to try others.)
	static const float DayEvMin = []()
	{
		float V = 0.5f;
		FParse::Value(FCommandLine::Get(), TEXT("bfevmin="), V);
		return FParse::Param(FCommandLine::Get(), TEXT("bfoldexposure")) ? -10.f : V;
	}();
	NightPost->Settings.bOverride_AutoExposureMinBrightness = true;
	NightPost->Settings.AutoExposureMinBrightness = FMath::Lerp(-10.f, DayEvMin, Day);
	// Mist (Raven's list, 34): it lies low over the island from before dawn and burns off by mid-morning; a thinner haze
	// at dusk. Low and thick (it hugs the ground and the water), the light through it warm at sunrise.
	if (Fog && !bUnderwaterView)
	{
		const float Mist = FMath::SmoothStep(3.5f, 5.5f, Hours) * (1.f - FMath::SmoothStep(7.5f, 9.5f, Hours));
		const float Haze = 0.35f * FMath::SmoothStep(17.f, 19.f, Hours) * (1.f - FMath::SmoothStep(21.f, 23.f, Hours));
		const float M = FMath::Max(Mist, Haze);
		Fog->SetFogDensity(0.005f + 0.16f * M);
		Fog->SetFogHeightFalloff(FMath::Lerp(0.2f, 0.7f, M));
		Fog->SetFogInscatteringColor(FMath::Lerp(FLinearColor(0.45f, 0.55f, 0.7f), FLinearColor(0.75f, 0.72f, 0.68f), M) * (0.15f + 0.85f * Day));
		Fog->SetDirectionalInscatteringColor(FLinearColor(1.f, 0.7f, 0.45f) * 0.6f * M * Day);
		MistNow = M;
	}
}

void ABFGameMode::SetUnderwaterView(bool bUnder)
{
	if (!Fog || bUnder == bUnderwaterView)
	{
		return;
	}
	bUnderwaterView = bUnder;
	if (bUnder)
	{
		AirFogDensity = Fog->FogDensity;
		AirFogFalloff = Fog->FogHeightFalloff;
		AirFogColour = Fog->FogInscatteringLuminance;
		AirSunFogColour = Fog->DirectionalInscatteringLuminance;
		AirSkyFogScale = Fog->SkyAtmosphereAmbientContributionColorScale;
		// Clear-ish water: about half the view gone at 15 m, most of it by 30 m; even all through (no height falloff),
		// lit a dim blue-green whichever way it looks.
		Fog->SetFogDensity(0.45f);
		Fog->SetFogHeightFalloff(0.001f);
		Fog->SetFogInscatteringColor(FLinearColor(0.025f, 0.11f, 0.13f));
		Fog->SetDirectionalInscatteringColor(FLinearColor(0.01f, 0.04f, 0.045f));
		Fog->SetSkyAtmosphereAmbientContributionColorScale(FLinearColor(0.f, 0.f, 0.f));
	}
	else
	{
		Fog->SetFogDensity(AirFogDensity);
		Fog->SetFogHeightFalloff(AirFogFalloff);
		Fog->SetFogInscatteringColor(AirFogColour);
		Fog->SetDirectionalInscatteringColor(AirSunFogColour);
		Fog->SetSkyAtmosphereAmbientContributionColorScale(AirSkyFogScale);
	}
}

ABFDino* ABFGameMode::SpawnDino(APlayerController* Controller, int32 Index, EBFSpawnArea Area, int32 Ramp)
{
	if (!Species.IsValidIndex(Index) || !Controller)
	{
		return nullptr;
	}
	ABFDino::MeasureStanding(GetWorld(), Species[Index]);     // the first time: it tries itself on slopes (its climb limit)
	const FBFSpecies& S = Species[Index];
	FVector At(-3000.f, 0.f, 0.f);
	if (Area == EBFSpawnArea::Slopes)
	{
		At = FVector(RampStartX - S.Bounds.GetSize().X * 0.6f - 800.f, RampY(FMath::Clamp(Ramp, 0, NumRamps - 1)), 0.f);
	}
	else if (Area == EBFSpawnArea::Cliff)
	{
		At = FVector(CliffEdgeX - 2500.f, 0.f, CliffTopZ);
	}
	else if (Area == EBFSpawnArea::Pool)
	{
		At = FVector(PoolMinX - S.Bounds.GetSize().X * 0.6f - 800.f, 0.5f * (PoolMinY + PoolMaxY), 0.f);
	}
	else if (Area == EBFSpawnArea::World && Island && Island->IsReady())
	{
		At = Island->StartPoint();
	}
	At.Z += S.Bounds.GetSize().Z * 0.5f + 50.f;

	if (APawn* Old = Controller->GetPawn())
	{
		Controller->UnPossess();
		Old->Destroy();
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ABFDino* Dino = GetWorld()->SpawnActorDeferred<ABFDino>(ABFDino::StaticClass(), FTransform(At), nullptr, nullptr, P.SpawnCollisionHandlingOverride);
	Dino->Setup(S);
	if (!LookOf(S.Key).IsOwn())
	{
		Dino->ApplyLook(LookOf(S.Key));    // its colours and covering, if they were changed (its own: its material as made)
	}
	Dino->FinishSpawning(FTransform(At));
	Controller->Possess(Dino);
	Controller->SetControlRotation(FRotator(-15.f, 0.f, 0.f));
	return Dino;
}

ABFDino* ABFGameMode::SpawnCompanion(int32 Index, const FVector& At, float Yaw)
{
	if (!Species.IsValidIndex(Index))
	{
		return nullptr;
	}
	ABFDino::MeasureStanding(GetWorld(), Species[Index]);
	const FBFSpecies& S = Species[Index];
	const FTransform Where(FRotator(0.f, Yaw, 0.f), At + FVector(0.f, 0.f, S.Bounds.GetSize().Z * 0.5f + 50.f));
	ABFDino* Dino = GetWorld()->SpawnActorDeferred<ABFDino>(ABFDino::StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	Dino->Setup(S);
	if (!LookOf(S.Key).IsOwn())
	{
		Dino->ApplyLook(LookOf(S.Key));
	}
	Dino->GetCharacterMovement()->bRunPhysicsWithNoController = true;     // it falls, walks and turns with nobody in it
	Dino->FinishSpawning(Where);
	return Dino;
}
