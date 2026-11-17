#include "BFPlayerController.h"
#include "BFEffects.h"
#include "Widgets/Layout/SBox.h"
#include "Misc/FileHelper.h"

#include "BFDino.h"
#include "BFRuntimeImport.h"
#include "BFSpliceStage.h"
#include "BFWorld.h"
#include "BFLife.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "HAL/PlatformMemory.h"
#include "Misc/DateTime.h"

static const FKey NumberKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };

ABFGameMode* ABFPlayerController::Mode() const
{
	return GetWorld()->GetAuthGameMode<ABFGameMode>();
}

ABFDino* ABFPlayerController::Dino() const
{
	return Cast<ABFDino>(GetPawn());
}

void ABFPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController() || !Mode())
	{
		return;
	}
	// -bfrtimport=<key>: build that creature's .glb and import it while the game runs (BFRuntimeImport), and log what
	// came out (the check that a creature can be made playable without the editor's import or a restart).
	FString RtKey;
	if (FParse::Value(FCommandLine::Get(), TEXT("bfrtimport="), RtKey))
	{
		const double T0 = FPlatformTime::Seconds();
		FString Glb, Err;
		const bool bBuilt = BFRuntimeImport::BuildGlb(Mode()->RigFolder, RtKey, Glb, Err);
		const double T1 = FPlatformTime::Seconds();
		USkeletalMesh* RtMesh = bBuilt ? BFRuntimeImport::ImportGlb(RtKey, Glb, Err) : nullptr;
		const double T2 = FPlatformTime::Seconds();
		FString Slots;
		if (RtMesh)
		{
			for (const FSkeletalMaterial& Mat : RtMesh->GetMaterials())
			{
				Slots += TEXT(" ") + Mat.MaterialSlotName.ToString();
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] runtime import %s: glb %s (%.1f s), import %s (%.1f s)%s%s"), *RtKey,
			bBuilt ? TEXT("built") : TEXT("FAILED"), T1 - T0, RtMesh ? *RtMesh->GetPathName() : TEXT("FAILED"), T2 - T1,
			RtMesh ? *FString::Printf(TEXT(", %d bones, slots:%s"), RtMesh->GetRefSkeleton().GetNum(), *Slots) : TEXT(""),
			Err.IsEmpty() ? TEXT("") : *(TEXT(" | ") + Err));
	}
	bAuto = FParse::Param(FCommandLine::Get(), TEXT("bfauto"));
	if (FParse::Param(FCommandLine::Get(), TEXT("bfragdoll")))
	{
		bAuto = true;
		AutoStep = 30;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfclips")))
	{
		bAuto = true;
		AutoStep = 40;
	}
	bShowBalance = !bAuto;
	if (FParse::Param(FCommandLine::Get(), TEXT("bfstance")))
	{
		bAuto = true;
		bShowBalance = true;
		AutoStep = 50;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfcatalog")))
	{
		bAuto = true;
		bShowBalance = true;
		AutoStep = 90;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfslopes")))
	{
		bAuto = true;
		bShowBalance = true;
		AutoStep = 147;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bftails")))
	{
		bAuto = true;
		AutoStep = 200;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfsprint")))
	{
		bAuto = true;
		AutoStep = 230;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfmouth")))
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 280;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfturn")))
	{
		bAuto = true;
		AutoStep = 80;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfsplice")))
	{
		bAuto = true;
		bShowBalance = true;
		AutoStep = 60;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfeditor")))
	{
		bAuto = true;
		AutoStep = 100;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bftestit")))       // the editor's Test it button, end to end
	{
		bAuto = true;
		AutoStep = 113;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bftest")))
	{
		bAuto = true;
		AutoStep = 159;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bftestone")))      // Test it, from the splicing editor
	{
		bAuto = true;
		AutoStep = 158;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfidle")))
	{
		bAuto = true;
		AutoStep = 180;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfswim")))
	{
		bAuto = true;
		AutoStep = 140;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfankles")))
	{
		bAuto = true;
		AutoStep = 130;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfclicks")))
	{
		bAuto = true;
		AutoStep = 120;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfmine")))       // your creatures and undo, by real clicks
	{
		bAuto = true;
		AutoStep = 320;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfpterayoung")))  // young Pteranodons: their bodies, their parents feeding them; the fish
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 630;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfriver")))      // rivers down slopes: no water standing up out of them
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 690;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bflandform")))   // ravines and waterfalls, and the O panel's places
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 740;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfcavelook")))   // every den's mouth from six sides, for its look
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 730;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfworldfx")))    // morning mist, footprints, dust, splashes, wet skin, foam
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 720;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfbehave")))     // lying down at night; startle, stare, alarm calls; a limp
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 710;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfcarcass")))    // a kill: blood, a wound, a red muzzle, the bones
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 700;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bffeed")))       // do their mouths reach what they eat and drink?
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 680;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfden")))        // raptors sleep in a den at night and come out by day
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 670;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfrocks")))      // rock shapes: the island's boulders and fins, a rock ledge
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 660;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfcave")))       // a rock piece: one cave carved into a cliff, looked at inside and out
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 650;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfpteranest")))  // quick: a staged nest up a tree hatching at once, the parents feeding
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 640;
	}
	{
		FString Chain;
		if (FParse::Value(FCommandLine::Get(), TEXT("bfchain="), Chain))
		{
			Chain.ParseIntoArray(ChainQueue, TEXT("+"));
		}
		float Speed = 1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("bfspeed="), Speed) && Speed > 0.f)
		{
			TestSpeed = FMath::Clamp(Speed, 0.25f, 8.f);
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfptera")))      // the wild Pteranodons: colony, fishing, roosting
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 620;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfyou")))        // you in the life cycle: court, be courted, lay, carry on
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 600;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bffar")))        // far off: the simpler life while you're away
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 590;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfday")))        // one island day: the rhythm, the herds, the hunting
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 580;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfgens")))       // many generations: do the genes change?
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 570;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfbreed")))      // courtship, nests, eggs, hatching
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 560;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfsave")))       // the island saved and brought back
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 540;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfstress")))     // how many animals the island can hold
	{
		ABFDino::bDetailByDistance = !FParse::Param(FCommandLine::Get(), TEXT("bffulldetail"));
		bAuto = true;
		bShowBalance = false;
		AutoStep = 550;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfgrow")))       // growing up, 600 times as fast
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 530;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfyoung")))      // young in the herds; raptors and a straggler
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 520;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfneeds")))      // your own needs: drink, eat, rest, starve
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 510;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfwatch")))      // watching: the panel, following an animal, day and night
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 500;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfhunt")))       // hunters (a T. rex, a raptor pack) among the herds
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 490;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bflife")))       // herds of plant eaters living on an island, then a T. rex
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 480;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfworld")))      // an island from a seed: made, walked on, waded in
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 440;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bffolds")))      // skin folds behind a running T. rex's and Triceratops' knees
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 420;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfpair")))       // a Velociraptor beside the T. rex: copy, follow, the person
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 400;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bflook")))       // the Look panel's buttons on a T. rex and a Brachiosaurus
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 380;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfjump")))       // Space on each animal (-bfonly works): crouch, air, landing
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 360;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("bfslowmo")))     // slow motion, pause, a frame, the free camera
	{
		bAuto = true;
		bShowBalance = false;
		AutoStep = 350;
	}
	FString Only;
	if (FParse::Value(FCommandLine::Get(), TEXT("bfonly="), Only, false))
	{
		Only.ParseIntoArray(AutoOnly, TEXT(","));
	}
	AutoOut = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AutoTest")));
	BuildWidgets();
	// -bfstart=key: Walk It coming back with the new creature.
	int32 First = 0;
	FString Start;
	if (FParse::Value(FCommandLine::Get(), TEXT("bfstart="), Start, false))
	{
		First = FMath::Max(Mode()->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Start; }), 0);
	}
	Pick(First);
	ShowPicker(!bAuto && Start.IsEmpty());
}

void ABFPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	SaveIsland(false);
	if (GEngine && GEngine->GameViewport)
	{
		if (PickerWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(PickerWidget.ToSharedRef());
		}
		if (ReportWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ReportWidget.ToSharedRef());
		}
		if (HudWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
		}
		if (LookWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(LookWidget.ToSharedRef());
		}
	}
	Super::EndPlay(Reason);
}

void ABFPlayerController::Pick(int32 Index)
{
	ABFGameMode* M = Mode();
	if (!M || !M->GetSpecies().IsValidIndex(Index))
	{
		return;
	}
	Current = Index;
	CurrentKey = M->GetSpecies()[Index].Key;
	SetFreeCam(false);          // (the camera follows the animal it was on)
	M->SpawnDino(this, Index, Area, Ramp);
	if (bWantClips && Dino())
	{
		Dino()->SetClips(true);
	}
	// The companion and the person come along, beside the animal wherever it now stands.
	if (Companion && Dino())
	{
		const FVector At = BesideYou(Companion->GetSpecies().Bounds.GetSize().Y, 1.f) + FVector(0.f, 0.f, Companion->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f);
		Companion->TeleportTo(At, FRotator(0.f, Dino()->GetActorRotation().Yaw, 0.f));
	}
	if (Person)
	{
		SetPerson(true);
	}
	bPickerDirty = true;        // the menu marks the one being played
}

void ABFPlayerController::StepCreature(int32 By)
{
	if (ABFGameMode* M = Mode(); M && M->GetSpecies().Num() > 0)
	{
		const int32 N = M->GetSpecies().Num();
		Pick(((Current + By) % N + N) % N);
	}
}

void ABFPlayerController::EditCreature(const FString& Key)
{
	ABFGameMode* M = Mode();
	const int32 I = M ? M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Key; }) : INDEX_NONE;
	if (I == INDEX_NONE)
	{
		return;
	}
	const FString Shown = M->GetSpecies()[I].Name;
	OpenSplicer();
	Stage->LoadCreature(Key, Shown);
}

void ABFPlayerController::RemoveCreature(const FString& Key)
{
	ABFGameMode* M = Mode();
	if (!M)
	{
		return;
	}
	const int32 I = M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Key; });
	const FString Shown = I != INDEX_NONE ? M->GetSpecies()[I].Name : Key;
	FString Error;
	if (!M->RemoveCreature(Key, Error))
	{
		PickerNote = FString::Printf(TEXT("Couldn't remove %s: %s"), *Shown, *Error);
	}
	else
	{
		PickerNote = FString::Printf(TEXT("Removed %s. It's at the bottom of the list if you want it back."), *Shown);
		if (CurrentKey == Key)
		{
			Pick(0);         // it was the one being played
		}
		else
		{
			Current = FMath::Max(M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == CurrentKey; }), 0);
		}
	}
	bPickerDirty = true;
}

void ABFPlayerController::PutBack(const FString& Key)
{
	ABFGameMode* M = Mode();
	if (!M || (Stage && Stage->IsBringingBack()))
	{
		return;
	}
	FString Error, Shown = Key;
	for (const ABFGameMode::FRemoved& R : M->GetRemoved())
	{
		if (R.Key == Key)
		{
			Shown = R.Name;
		}
	}
	if (!M->RestoreFiles(Key, Error))
	{
		PickerNote = FString::Printf(TEXT("Couldn't put %s back: %s"), *Shown, *Error);
	}
	else
	{
		if (!Stage)
		{
			Stage = GetWorld()->SpawnActor<ABFSpliceStage>();
			Stage->SetActorHiddenInGame(true);
		}
		Stage->BringBack(this, M->RigFolder, Key);       // built into the game in the background, then CreatureBack
		PickerNote = FString::Printf(TEXT("Putting %s back (a few seconds)..."), *Shown);
	}
	bPickerDirty = true;
}

void ABFPlayerController::CreatureBack(const FString& Key, USkeletalMesh* Mesh, const FString& Error)
{
	ABFGameMode* M = Mode();
	FString Why = Error;
	const int32 I = M && Mesh ? M->AddSpecies(Key, Mesh, Why) : INDEX_NONE;
	PickerNote = I != INDEX_NONE
		? FString::Printf(TEXT("%s is back."), *M->GetSpecies()[I].Name)
		: FString::Printf(TEXT("Its files are back, but it couldn't be built into the game: %s (it will be there next time the game starts)"), *Why);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] put back %s: %s"), *Key, *PickerNote);
	bPickerDirty = true;
}

FVector ABFPlayerController::BesideYou(float OtherWidthCm, float Side) const
{
	const ABFDino* D = Dino();
	if (!D)
	{
		return FVector::ZeroVector;
	}
	// Side by side, a couple of metres apart, level with its hips; on the ground under its capsule.
	const float Gap = 0.5f * D->GetSpecies().Bounds.GetSize().Y + 0.5f * OtherWidthCm + 250.f;
	FVector At = D->GetActorLocation() + D->GetActorRightVector() * Side * Gap;
	At.Z = D->GetActorLocation().Z - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return At;
}

void ABFPlayerController::BringCompanion(int32 Index)
{
	ABFGameMode* M = Mode();
	ABFDino* D = Dino();
	if (!M || !D || !M->GetSpecies().IsValidIndex(Index))
	{
		return;
	}
	SendCompanionAway();
	Companion = M->SpawnCompanion(Index, BesideYou(M->GetSpecies()[Index].Bounds.GetSize().Y, 1.f), D->GetActorRotation().Yaw);
	if (Companion)
	{
		Companion->SetBalanceShown(false);
		Companion->SetIdleBehaviours(true);
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] companion %s beside %s"), *Companion->GetSpecies().Key, *D->GetSpecies().Key);
	}
	bPickerDirty = true;
}

void ABFPlayerController::SendCompanionAway()
{
	if (Companion)
	{
		Companion->Destroy();
		Companion = nullptr;
	}
	CompanionMode = ECompanion::Stay;
	bPickerDirty = true;
}

void ABFPlayerController::SetPerson(bool bOn)
{
	if (Person)
	{
		Person->Destroy();
		Person = nullptr;
	}
	PersonLabel = nullptr;
	ABFDino* D = Dino();
	if (!bOn || !D)
	{
		bPickerDirty = true;
		return;
	}
	// A plain 1.8 m figure on the animal's left: legs and body one grey column, a head on top, its height written over it.
	Person = GetWorld()->SpawnActor<AActor>(BesideYou(50.f, -1.f), FRotator::ZeroRotator);
	USceneComponent* Root = NewObject<USceneComponent>(Person, TEXT("Root"));
	Person->SetRootComponent(Root);
	Root->RegisterComponent();
	Root->SetWorldLocation(BesideYou(50.f, -1.f));
	auto Part = [&](const TCHAR* Mesh, const FVector& At, const FVector& Size)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Person);
		C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
		C->SetupAttachment(Root);
		C->RegisterComponent();
		C->SetRelativeLocation(At);
		C->SetRelativeScale3D(Size);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Base, C);
			Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 0.35f, 0.2f));
			C->SetMaterial(0, Mat);
		}
	};
	Part(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.f, 0.f, 78.f), FVector(0.36f, 0.36f, 1.56f));    // 156 cm to the neck
	Part(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector(0.f, 0.f, 168.f), FVector(0.24f));                    // the head, to 180 cm
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Person);
	Label->SetupAttachment(Root);
	Label->RegisterComponent();
	Label->SetRelativeLocation(FVector(0.f, 0.f, 215.f));
	Label->SetText(FText::FromString(TEXT("1.8 m")));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(40.f);
	Label->SetTextRenderColor(FColor(240, 230, 210));
	PersonLabel = Label;
	bPickerDirty = true;
}

void ABFPlayerController::TickCompanion()
{
	if (Person && PersonLabel && PlayerCameraManager)
	{
		// The label faces the camera (a text render reads along its own +X: turned to look back at the view).
		const FVector To = PlayerCameraManager->GetCameraLocation() - PersonLabel->GetComponentLocation();
		PersonLabel->SetWorldRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}
	ABFDino* D = Dino();
	if (!Companion || !D || Companion->IsRagdoll())
	{
		return;
	}
	switch (CompanionMode)
	{
	case ECompanion::Copy:
		// Your move, your Shift and your Space, so the two gaits run side by side from the same command.
		Companion->SetRunning(bCopyRun);
		if (!CopyMove.IsNearlyZero())
		{
			Companion->AddMovementInput(CopyMove);
		}
		break;
	case ECompanion::Follow:
	{
		// To a spot beside you: walking when a few metres off, running when far behind, standing once there.
		const FVector To = BesideYou(Companion->GetSpecies().Bounds.GetSize().Y, 1.f) - Companion->GetActorLocation();
		const float Far = FVector(To.X, To.Y, 0.f).Size();
		const float Near = 0.25f * Companion->BodyLengthCm() + 150.f;
		Companion->SetRunning(Far > 4.f * Near);
		if (Far > Near)
		{
			Companion->AddMovementInput(FVector(To.X, To.Y, 0.f).GetSafeNormal());
		}
		break;
	}
	default:
		Companion->SetRunning(false);
		break;
	}
}

void ABFPlayerController::TickWatch()
{
	// The watch panel (O), and the camera following an animal (N; M stops): behind and above it, easing after it.
	ABFWorld* I = Mode() ? Mode()->GetIsland() : nullptr;
	if (bWatchPanel && WatchPanel.IsValid())
	{
		// (2026-11-15, Raven couldn't find any caves: the island's named places, nearest first, which way and how far.)
		FString Places;
		if (I && I->Places.Num() > 0 && PlayerCameraManager)
		{
			const FVector Me = PlayerCameraManager->GetCameraLocation();
			TArray<const ABFWorld::FBFPlace*> Sorted;
			for (const ABFWorld::FBFPlace& Pl : I->Places)
			{
				Sorted.Add(&Pl);
			}
			Sorted.Sort([&](const ABFWorld::FBFPlace& A, const ABFWorld::FBFPlace& B) { return FVector::DistSquared2D(A.At, Me) < FVector::DistSquared2D(B.At, Me); });
			static const TCHAR* Compass[] = { TEXT("E"), TEXT("NE"), TEXT("N"), TEXT("NW"), TEXT("W"), TEXT("SW"), TEXT("S"), TEXT("SE") };          // (+X east, +Y north)
			Places = TEXT("\n\nPlaces on this island:");
			for (const ABFWorld::FBFPlace* Pl : Sorted)
			{
				const FVector D = Pl->At - Me;
				const int32 Dir = int32(FMath::RoundToInt(FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) + 360.f, 360.f) / 45.f)) % 8;
				Places += FString::Printf(TEXT("\n  %-22s %5.0f m %s"), *Pl->Name, D.Size2D() / 100.f, Compass[Dir]);
			}
		}
		WatchPanel->SetText(FText::FromString((Life ? Life->WatchText(I ? I->ClockText() : FString()) : FString(I ? I->ClockText() : FString())) + Places));
	}
	if (WatchI == INDEX_NONE || !Life || !FreeCam)
	{
		return;
	}
	if (!Life->IsValidAnimal(WatchI) || Life->Get(WatchI).bDead)
	{
		WatchI = INDEX_NONE;
		SetFreeCam(false);
		return;
	}
	const ABFDino* A = Life->Get(WatchI).Body.Get();
	const float Len = FMath::Max(A->BodyLengthCm(), 300.f);
	const FVector Want = A->GetActorLocation() - A->GetActorForwardVector() * 2.4f * Len + A->GetActorRightVector() * 0.8f * Len + FVector(0.f, 0.f, 0.9f * Len);
	const float Dt = FApp::GetDeltaTime();
	const FVector At = FMath::VInterpTo(FreeCam->GetActorLocation(), Want, Dt, 2.f);
	FreeCam->SetActorLocationAndRotation(At, (A->GetActorLocation() + FVector(0.f, 0.f, 0.2f * Len) - At).Rotation());
}

void ABFPlayerController::TickNeeds(float Dt)
{
	// Your own needs (World and AI tab, step 6), on the island, from your creature's body as the animals' are: a stomach
	// (a plant eater's 0.4% of its weight, digested in a quarter of an hour; a meat eater's 4%, in half an hour), thirst
	// (ten minutes from slaked to parched), tiredness (from being up and from running), and health (lost slowly starving
	// or parched, won back fed and watered). E crops, tears or drinks; Z lies up to rest (faster at night).
	ABFDino* D = Dino();
	ABFWorld* I = Mode() ? Mode()->GetIsland() : nullptr;
	float Ground = 0.f;
	bNeeds = D && I && I->IsReady() && I->GroundAt(FVector2D(D->GetActorLocation()), Ground);
	if (!bNeeds)
	{
		return;
	}
	if (!Life)
	{
		Life = GetWorld()->SpawnActor<ABFLife>();        // (it keeps your health against the animals too)
	}
	const FBFSpecies& S = D->GetSpecies();
	if (NeedsKey != S.Key || NeedStomachMax <= 0.f)
	{
		NeedsKey = S.Key;
		NeedStomachMax = FMath::Max(1.f, (D->CanFeed() ? 0.004f : 0.04f) * S.MassKg);
		NeedStomach = 0.7f * NeedStomachMax;
		NeedThirst = 0.2f;
		NeedTired = 0.1f;
		Life->PlayerHealth = 1.f;
		bResting = false;
	}
	if (D->IsRagdoll())
	{
		return;
	}
	const float Rate = Dt * NeedsRate;
	NeedStomach = FMath::Max(0.f, NeedStomach - Rate * NeedStomachMax / (D->CanFeed() ? 900.f : 1800.f));
	NeedThirst = FMath::Min(1.f, NeedThirst + Rate / 600.f);
	const float Night = 1.f - I->Daylight();
	NeedTired = bResting ? FMath::Max(0.f, NeedTired - Rate / FMath::Lerp(40.f, 25.f, Night))
		: FMath::Min(1.f, NeedTired + Rate * (1.f / 1500.f + 0.012f * FMath::Max(D->GetExertion() - 0.3f, 0.f)));
	float& Health = Life->PlayerHealth;
	const bool bStarving = NeedStomach <= 0.f, bParched = NeedThirst >= 1.f;
	if (bStarving || bParched)
	{
		Health -= Rate / 300.f;
		if (Health <= 0.f)
		{
			D->StartRagdoll();
			Life->PlayerNote = FString::Printf(TEXT("You died of %s (%s)"), bStarving && bParched ? TEXT("hunger and thirst") : bStarving ? TEXT("hunger") : TEXT("thirst"),
				Life->YourYoung() > 0 ? TEXT("K to carry on as one of your young") : TEXT("K to start again"));
			Life->PlayerNoteUntil = FPlatformTime::Seconds() + 10.0;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] needs: %s"), *Life->PlayerNote);
			NeedStomachMax = 0.f;          // a fresh start when it gets up
		}
	}
	else if (NeedStomach > 0.4f * NeedStomachMax && NeedThirst < 0.6f)
	{
		Health = FMath::Min(1.f, Health + Rate / 120.f);
	}
	// You in the life cycle (step 9): your place among the animals, from your needs; a body grown into (you as a youngster
	// growing up) taken over.
	if (Life->Num() > 0)
	{
		if (Life->PendingPlayerBody.IsValid())
		{
			// A body grown into (you as a youngster growing up): taken over first, then the old one goes.
			ABFDino* New = Life->PendingPlayerBody.Get();
			ABFDino* Old = Life->OldPlayerBody.Get();
			UnPossess();
			Possess(New);
			Current = Mode()->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == New->GetSpecies().Key; });
			const float Full = NeedStomach / FMath::Max(NeedStomachMax, 0.01f);
			NeedsKey = New->GetSpecies().Key;
			NeedStomachMax = FMath::Max(1.f, (New->CanFeed() ? 0.004f : 0.04f) * New->GetSpecies().MassKg);
			NeedStomach = Full * NeedStomachMax;
			if (Old)
			{
				Old->Destroy();
			}
			Life->PendingPlayerBody.Reset();
			Life->OldPlayerBody.Reset();
			FeedNote = FString::Printf(TEXT("You grew into %s"), *New->GetSpecies().Name);
			FeedNoteUntil = FPlatformTime::Seconds() + 5.0;
			D = New;
		}
		Life->SyncPlayer(D, NeedStomach / FMath::Max(NeedStomachMax, 0.01f), NeedThirst, Health, Dt);
		if (YouBack.IsValid() && Life->PlayerI != INDEX_NONE)
		{
			FBFAnimal& Me = Life->Edit(Life->PlayerI);
			double V = 0.0;
			Me.Generation = YouBack->TryGetNumberField(TEXT("generation"), V) ? int32(V) : Me.Generation;
			Me.AgeS = YouBack->TryGetNumberField(TEXT("age_s"), V) ? float(V) : Me.AgeS;
			const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
			if (YouBack->TryGetArrayField(TEXT("genes"), G) && G->Num() == 5)
			{
				Me.Genes.Size = float((*G)[0]->AsNumber());
				Me.Genes.Hue = float((*G)[1]->AsNumber());
				Me.Genes.Shade = float((*G)[2]->AsNumber());
				Me.Genes.Strength = float((*G)[3]->AsNumber());
				Me.Genes.Speed = float((*G)[4]->AsNumber());
			}
			YouBack.Reset();
		}
	}
}

bool ABFPlayerController::TryDrink()
{
	// E with its mouth at fresh water (a lake, a river; not the sea): head down, a long draught.
	ABFDino* D = Dino();
	ABFWorld* I = Mode() ? Mode()->GetIsland() : nullptr;
	if (!bNeeds || !D || !I || D->IsSwimming() || D->IsRagdoll() || NeedThirst < 0.03f)
	{
		return false;
	}
	FVector Snout = D->GetBoneWorld(TEXT("head_end"));
	if (Snout.Equals(D->GetActorLocation()))
	{
		Snout = D->GetBoneWorld(TEXT("head"));
	}
	FVector Water;
	if (!I->FreshWaterNear(Snout, 250.f + 0.3f * D->GetCapsuleComponent()->GetScaledCapsuleRadius(), Water))
	{
		return false;
	}
	NeedThirst = FMath::Max(0.f, NeedThirst - 0.2f);
	D->StartIdle(D->CanFeed() ? EBFIdle::Feed : EBFIdle::Sniff);        // head down to the water
	FeedNote = FString::Printf(TEXT("Drinking: thirst %.0f%%"), 100.f * NeedThirst);
	FeedNoteUntil = FPlatformTime::Seconds() + 3.0;
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] needs: %s"), *FeedNote);
	return true;
}

void ABFPlayerController::ToggleLife()
{
	ABFGameMode* M = Mode();
	ABFWorld* I = M ? M->GetIsland() : nullptr;
	if (Life && Life->Num() > 0)
	{
		Life->Clear();
		return;
	}
	if (!I || !I->IsReady())
	{
		return;
	}
	if (!Life)
	{
		Life = GetWorld()->SpawnActor<ABFLife>();
	}
	// Round you if you're on the island, else round where creatures arrive there.
	float Ground = 0.f;
	const FVector Near = Dino() && I->GroundAt(FVector2D(Dino()->GetActorLocation()), Ground) ? Dino()->GetActorLocation() : I->StartPoint();
	Life->Populate(I, Near);
}

bool ABFPlayerController::TryFeed()
{
	// A plant eater (its head's species: FBFSpecies::Feed) crops a mouthful from the nearest plant its snout can reach:
	// round its snout by a little more than its own width, and up to a little above its head's height (it can stretch up).
	ABFDino* D = Dino();
	ABFGameMode* M = Mode();
	ABFWorld* I = M ? M->GetIsland() : nullptr;
	if (D && !D->CanFeed() && Life && !D->IsRagdoll())
	{
		// A meat eater by a kill eats from it.
		float Kg = 0.f;
		const FString Ate = Life->PlayerEat(D, &Kg);
		if (!Ate.IsEmpty())
		{
			NeedStomach = FMath::Min(NeedStomach + Kg, NeedStomachMax);
			FeedNote = Ate;
			FeedNoteUntil = FPlatformTime::Seconds() + 4.0;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] eat: %s"), *Ate);
			return true;
		}
	}
	if (!D || !D->CanFeed() || !I || !I->IsReady() || D->IsSwimming() || D->IsFlying() || D->IsRagdoll())
	{
		return false;
	}
	FVector Snout = D->GetBoneWorld(TEXT("head_end"));
	if (Snout.Equals(D->GetActorLocation()))
	{
		Snout = D->GetBoneWorld(TEXT("head"));
	}
	const float Feet = D->GetActorLocation().Z - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float TopZ = Snout.Z + 0.3f * FMath::Max(Snout.Z - Feet, 0.f);
	const float Reach = 120.f + 0.5f * D->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Want = FMath::Max(0.2f, 0.0005f * D->GetSpecies().MassKg);      // a mouthful: about 1/2000 of its weight
	FString What;
	float Left = 0.f;
	const float Took = I->EatPlants(Snout, Reach, TopZ, Want, What, Left);
	if (Took <= 0.f)
	{
		return false;
	}
	EatenKg += Took;
	NeedStomach = FMath::Min(NeedStomach + Took, NeedStomachMax);
	if (D->GetIdle() != EBFIdle::Feed)
	{
		D->StartIdle(EBFIdle::Feed);
	}
	FeedNote = FString::Printf(TEXT("Eating %s: %.1f kg this mouthful, %.1f kg left on it (%.0f kg eaten so far)"), *What, Took, Left, EatenKg);
	FeedNoteUntil = FPlatformTime::Seconds() + 4.0;
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] eat: %s"), *FeedNote);
	return true;
}

FString ABFPlayerController::IslandSavePath(int32 Seed) const
{
	return Mode() ? FPaths::Combine(Mode()->BoneForgeDir(), TEXT("islands"), FString::Printf(TEXT("island_%d.json"), Seed)) : FString();
}

bool ABFPlayerController::SaveIsland(bool bTell)
{
	ABFGameMode* M = Mode();
	ABFWorld* I = M ? M->GetIsland() : nullptr;
	if (!CanSaveIslands() || !I || !I->IsReady() || RestoredSeed != I->GetSeed())
	{
		return false;          // (not until its save, if it had one, has been brought back: it'd be written over)
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("about"), TEXT("An island in the Bone Forge testing ground: its clock, the plants eaten, its animals and you. Written by the game."));
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetNumberField(TEXT("seed"), I->GetSeed());
	Root->SetStringField(TEXT("saved"), FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	TSharedRef<FJsonObject> World = MakeShared<FJsonObject>();
	I->WriteState(World);
	Root->SetObjectField(TEXT("world"), World);
	int32 Count = 0;
	if (Life && Life->Num() > 0)
	{
		Root->SetObjectField(TEXT("life"), Life->SaveState());
		Count = Life->NumAlive();
	}
	ABFDino* D = Dino();
	float Ground = 0.f;
	if (D && !D->IsRagdoll() && I->GroundAt(FVector2D(D->GetActorLocation()), Ground))
	{
		TSharedRef<FJsonObject> You = MakeShared<FJsonObject>();
		You->SetStringField(TEXT("body"), D->GetSpecies().Key);
		const FVector P = D->GetActorLocation();
		You->SetArrayField(TEXT("at"), { MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.X)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Y)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Z)) });
		You->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(D->GetActorRotation().Yaw));
		You->SetNumberField(TEXT("full"), NeedStomachMax > 0.f ? NeedStomach / NeedStomachMax : 0.7f);
		You->SetNumberField(TEXT("thirst"), NeedThirst);
		You->SetNumberField(TEXT("tired"), NeedTired);
		You->SetNumberField(TEXT("health"), Life ? Life->PlayerHealth : 1.f);
		if (Life && Life->PlayerI != INDEX_NONE)
		{
			const FBFAnimal& Me = Life->Get(Life->PlayerI);
			You->SetBoolField(TEXT("female"), Me.bFemale);
			You->SetNumberField(TEXT("generation"), Me.Generation);
			You->SetNumberField(TEXT("age_s"), Me.AgeS);
			You->SetArrayField(TEXT("genes"), { MakeShared<FJsonValueNumber>(Me.Genes.Size), MakeShared<FJsonValueNumber>(Me.Genes.Hue), MakeShared<FJsonValueNumber>(Me.Genes.Shade),
				MakeShared<FJsonValueNumber>(Me.Genes.Strength), MakeShared<FJsonValueNumber>(Me.Genes.Speed) });
		}
		Root->SetObjectField(TEXT("you"), You);
	}
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	const FString Path = IslandSavePath(I->GetSeed());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const double T0 = FPlatformTime::Seconds();
	const bool bOk = FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] save: island %d %s: %s, %d animals, %d plants eaten, %.0f KB in %.0f ms"), I->GetSeed(), bOk ? TEXT("saved") : TEXT("NOT saved"),
		*I->ClockText(), Count, I->NumRegrowing(), Text.Len() / 1024.f, 1000.0 * (FPlatformTime::Seconds() - T0));
	if (bTell)
	{
		PickerNote = bOk ? FString::Printf(TEXT("Saved island %d: %s, %d animals."), I->GetSeed(), *I->ClockText(), Count) : FString(TEXT("Couldn't save the island."));
	}
	return bOk;
}

void ABFPlayerController::RestoreIsland()
{
	// The island has just been made: if it was saved, its clock, eaten plants and animals come back as they were, and
	// you (once you go there) where you were.
	ABFGameMode* M = Mode();
	ABFWorld* I = M ? M->GetIsland() : nullptr;
	if (!I || !I->IsReady())
	{
		return;
	}
	RestoredSeed = I->GetSeed();
	SaveClock = 0.f;
	PendingYou.Reset();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!CanSaveIslands() || !FFileHelper::LoadFileToString(Text, *IslandSavePath(RestoredSeed))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	const TSharedPtr<FJsonObject>* World = nullptr;
	if (Root->TryGetObjectField(TEXT("world"), World))
	{
		I->ReadState(*World);
	}
	const TSharedPtr<FJsonObject>* LifeObj = nullptr;
	if (Root->TryGetObjectField(TEXT("life"), LifeObj))
	{
		if (!Life)
		{
			Life = GetWorld()->SpawnActor<ABFLife>();
		}
		Life->LoadState(I, *LifeObj);
	}
	const TSharedPtr<FJsonObject>* You = nullptr;
	if (Root->TryGetObjectField(TEXT("you"), You))
	{
		PendingYou = *You;
	}
	FString Saved;
	Root->TryGetStringField(TEXT("saved"), Saved);
	PickerNote = FString::Printf(TEXT("Island %d back as it was (saved %s): %s, %d animals."), RestoredSeed, *Saved, *I->ClockText(), Life ? Life->Num() : 0);
	bPickerDirty = true;
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] save: %s (%.0f ms)"), *PickerNote, 1000.0 * (FPlatformTime::Seconds() - T0));
}

bool ABFPlayerController::CarryOnAsYoung()
{
	// Dead, with young living: you carry on as the oldest of them (its body, needs and genes).
	if (!Life)
	{
		return false;
	}
	FString Note;
	ABFDino* Young = Life->CarryOn(Note);
	if (!Young)
	{
		return false;
	}
	const FBFAnimal& Y = Life->Get(Life->PlayerI);
	UnPossess();
	Possess(Young);
	Young->SetIdleBehaviours(false);
	Current = Mode()->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Young->GetSpecies().Key; });
	NeedsKey = Young->GetSpecies().Key;
	NeedStomachMax = FMath::Max(1.f, (Young->CanFeed() ? 0.004f : 0.04f) * Young->GetSpecies().MassKg);
	NeedStomach = FMath::Clamp(Y.StomachKg / FMath::Max(Y.StomachMaxKg, 0.01f), 0.f, 1.f) * NeedStomachMax;
	NeedThirst = Y.Thirst;
	NeedTired = Y.Tired;
	Life->PlayerHealth = FMath::Max(Y.Health, 0.3f);
	bResting = false;
	SetControlRotation(FRotator(-15.f, Young->GetActorRotation().Yaw, 0.f));
	FeedNote = Note;
	FeedNoteUntil = FPlatformTime::Seconds() + 8.0;
	bPickerDirty = true;
	return true;
}

int32 ABFPlayerController::StartStepOf(const FString& Check)
{
	// The island checks that start from a fresh island (each makes it itself), so can follow one another.
	static const TMap<FString, int32> Steps = {
		{ TEXT("bfyoung"), 520 }, { TEXT("bfgrow"), 530 }, { TEXT("bfbreed"), 560 }, { TEXT("bfday"), 580 }, { TEXT("bffar"), 590 },
		{ TEXT("bfyou"), 600 }, { TEXT("bfptera"), 620 }, { TEXT("bfpterayoung"), 630 }, { TEXT("bfpteranest"), 640 }, { TEXT("bfcave"), 650 }, { TEXT("bfrocks"), 660 }, { TEXT("bfden"), 670 }, { TEXT("bffeed"), 680 }, { TEXT("bfriver"), 690 }, { TEXT("bfcarcass"), 700 }, { TEXT("bfbehave"), 710 }, { TEXT("bfworldfx"), 720 }, { TEXT("bfcavelook"), 730 }, { TEXT("bflandform"), 740 } };
	const int32* S = Steps.Find(Check.TrimStartAndEnd().Replace(TEXT("-"), TEXT("")));
	return S ? *S : INDEX_NONE;
}

void ABFPlayerController::MakeIsland(int32 Seed)
{
	if (ABFGameMode* M = Mode())
	{
		SaveIsland(false);
		if (Life)
		{
			Life->Clear();
		}
		RestoredSeed = INDEX_NONE;
		M->MakeIsland(Seed);
		bGoWhenReady = true;      // there as soon as it stands
		PickerNote = FString::Printf(TEXT("Making island %d..."), Seed);
		bPickerDirty = true;
	}
}

void ABFPlayerController::ChangeLook(TFunctionRef<void(FBFLook&)> Edit)
{
	ABFDino* D = Dino();
	if (!D || !Mode())
	{
		return;
	}
	FBFLook L = D->GetLook();
	Edit(L);
	D->ApplyLook(L);
	Mode()->SetLook(D->GetSpecies().Key, L);
}

void ABFPlayerController::ShowLook(bool bShow)
{
	bLookOpen = bShow && Dino() != nullptr;
	if (LookWidget.IsValid())
	{
		LookWidget->SetVisibility(bLookOpen ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (bPickerOpen)
	{
		return;                   // the menu keeps the mouse
	}
	SetShowMouseCursor(bLookOpen);
	if (bLookOpen)
	{
		FInputModeGameAndUI In;
		In.SetHideCursorDuringCapture(false);
		SetInputMode(In);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ABFPlayerController::BuildLookPanel()
{
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 18);
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	auto Button = [&](TFunction<FString()> Label, TFunction<void()> Click) -> TSharedRef<SWidget>
	{
		return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(5.f, 2.f))
			.OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Small).Text_Lambda([Label]() { return FText::FromString(Label()); })
			];
	};
	auto Heading = [&](TSharedRef<SVerticalBox> Box, const TCHAR* Text)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 3.f)[SNew(STextBlock).Font(Big).Text(FText::FromString(Text))];
	};
	auto Mark = [](bool bOn, const FString& Word) { return bOn ? FString::Printf(TEXT("[%s]"), *Word) : Word; };
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(Title).Text_Lambda([this]() { return FText::FromString(TEXT("Look: ") + (Dino() ? Dino()->GetSpecies().Name : FString())); })
	];

	// Colours: whose scheme.
	Heading(Box, TEXT("Colours"));
	TSharedRef<SWrapBox> Schemes = SNew(SWrapBox).UseAllottedSize(true);
	Schemes->AddSlot().Padding(2.f)[Button([this, Mark]() { return Mark(Dino() && Dino()->GetLook().Scheme.IsEmpty(), TEXT("Its own")); },
		[this]() { ChangeLook([](FBFLook& L) { L.Scheme.Reset(); }); })];
	for (const FBFSkinScheme& S : BFSkinSchemes())
	{
		const FString Key = S.Key, Short = S.Short;
		Schemes->AddSlot().Padding(2.f)[Button([this, Mark, Key, Short]() { return Mark(Dino() && Dino()->GetLook().Scheme == Key, Short); },
			[this, Key]() { ChangeLook([&Key](FBFLook& L) { L.Scheme = Key; }); })];
	}
	Box->AddSlot().AutoHeight()[Schemes];
	// Turned round the colour wheel, lighter or darker.
	auto Stepper = [&](const TCHAR* Word, TFunction<FString()> Value, TFunction<void(float)> By)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(60.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(Word))]]
			+ SHorizontalBox::Slot().AutoWidth()[Button([]() { return FString(TEXT(" - ")); }, [By]() { By(-1.f); })]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(64.f).HAlign(HAlign_Center)[SNew(STextBlock).Font(Small).Text_Lambda([Value]() { return FText::FromString(Value()); })]
			]
			+ SHorizontalBox::Slot().AutoWidth()[Button([]() { return FString(TEXT(" + ")); }, [By]() { By(1.f); })]
		];
	};
	Stepper(TEXT("Hue"), [this]() { return Dino() ? FString::Printf(TEXT("%+.0f°"), Dino()->GetLook().HueDeg) : FString(); },
		[this](float S) { ChangeLook([S](FBFLook& L) { L.HueDeg = FMath::Fmod(L.HueDeg + 15.f * S + 540.f, 360.f) - 180.f; }); });
	Stepper(TEXT("Shade"), [this]() { return Dino() ? FString::Printf(TEXT("%+.0f%%"), 100.f * Dino()->GetLook().Shade) : FString(); },
		[this](float S) { ChangeLook([S](FBFLook& L) { L.Shade = FMath::Clamp(FMath::RoundToFloat((L.Shade + 0.1f * S) * 10.f) / 10.f, -0.5f, 0.5f); }); });

	// Covering: whose scales, feathers or fuzz.
	Heading(Box, TEXT("Covering"));
	TSharedRef<SWrapBox> Covers = SNew(SWrapBox).UseAllottedSize(true);
	Covers->AddSlot().Padding(2.f)[Button([this, Mark]() { return Mark(Dino() && Dino()->GetLook().Covering.IsEmpty(), TEXT("Its own")); },
		[this]() { ChangeLook([](FBFLook& L) { L.Covering.Reset(); }); })];
	for (const FBFSkinScheme& S : BFSkinSchemes())
	{
		const FString Key = S.Key, Word = S.Covering;
		Covers->AddSlot().Padding(2.f)[Button([this, Mark, Key, Word]() { return Mark(Dino() && Dino()->GetLook().Covering == Key, Word); },
			[this, Key]() { ChangeLook([&Key](FBFLook& L) { L.Covering = Key; }); })];
	}
	Box->AddSlot().AutoHeight()[Covers];
	// What the fossils say: the default is the evidence; anything else is the player's.
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(Small).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f)).Text_Lambda([this]()
		{
			const FBFSkinScheme* Own = Dino() ? BFSkinScheme(Dino()->OwnSkinKey()) : nullptr;
			return FText::FromString(Own ? FString::Printf(TEXT("Its own look follows the fossils: %s. Anything else here is your choice."), Own->Fossils) : FString());
		})
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[Button([]() { return FString(TEXT("Its own look")); }, [this]() { ChangeLook([](FBFLook& L) { L = FBFLook(); }); })]
		+ SHorizontalBox::Slot().AutoWidth()[Button([]() { return FString(TEXT("Done")); }, [this]() { ShowLook(false); })]
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f)).Text(FText::FromString(TEXT("Kept for this creature. L or Esc closes this.")))
	];
	LookWidget = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(16.f)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.8f)).Padding(14.f)
			[
				SNew(SBox).WidthOverride(430.f)[Box]
			]
		];
	LookWidget->SetVisibility(EVisibility::Collapsed);
	GEngine->GameViewport->AddViewportWidgetContent(LookWidget.ToSharedRef(), 15);
}

float ABFPlayerController::TimeScale() const
{
	return bPaused && StepFrames == 0 ? 0.0001f : SlowStep >= 0 ? 1.f / float(1 << SlowStep) : float(1 << -SlowStep);      // (the engine's smallest is 0.0001)
}

void ABFPlayerController::ApplyTimeScale()
{
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), TimeScale());
}

void ABFPlayerController::SetFreeCam(bool bOn)
{
	ABFDino* D = Dino();
	if (bOn == bFreeCam || (bOn && !D))
	{
		return;
	}
	bFreeCam = bOn;
	if (bOn)
	{
		if (!FreeCam)
		{
			FreeCam = GetWorld()->SpawnActor<ACameraActor>();
			FreeCam->GetCameraComponent()->bConstrainAspectRatio = false;
		}
		// From where the view is now; the animal keeps going straight at the pace it had (if it was moving).
		FVector At;
		FRotator Turn;
		GetPlayerViewPoint(At, Turn);
		FreeCam->SetActorLocationAndRotation(At, Turn);
		FreeCamLastDino = D->GetActorLocation();
		bCruise = IsInputKeyDown(EKeys::W);
		bCruiseRun = bCruise && (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
		SetViewTarget(FreeCam);
	}
	else
	{
		bCruise = bCruiseRun = false;
		SetViewTarget(GetPawn());
	}
}

void ABFPlayerController::TickFreeCam(ABFDino* D)
{
	if (!FreeCam)
	{
		return;
	}
	// It moves with the animal (so a walking or running one stays in view) and on real time (so it moves at its own pace
	// in slow motion or paused): WASD, Space up, Ctrl down, Shift faster, the wheel its speed, the mouse to look.
	const float Dt = FMath::Min((float)FApp::GetDeltaTime(), 0.1f);
	FVector At = FreeCam->GetActorLocation() + (D->GetActorLocation() - FreeCamLastDino);
	FreeCamLastDino = D->GetActorLocation();
	float MX = 0.f, MY = 0.f;
	GetInputMouseDelta(MX, MY);
	FRotator Turn = FreeCam->GetActorRotation();
	Turn.Yaw += MX * 0.6f;
	Turn.Pitch = FMath::Clamp(Turn.Pitch + MY * 0.6f, -89.f, 89.f);
	Turn.Roll = 0.f;
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		FreeCamSpeed = FMath::Min(FreeCamSpeed * 1.25f, 20000.f);
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		FreeCamSpeed = FMath::Max(FreeCamSpeed / 1.25f, 50.f);
	}
	const FVector Move = FRotationMatrix(Turn).GetUnitAxis(EAxis::X) * ((IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f))
		+ FRotationMatrix(Turn).GetUnitAxis(EAxis::Y) * ((IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f))
		+ FVector::UpVector * ((IsInputKeyDown(EKeys::SpaceBar) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::LeftControl) ? 1.f : 0.f));
	const bool bFast = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	At += Move.GetClampedToMaxSize(1.f) * FreeCamSpeed * (bFast ? 3.f : 1.f) * Dt;
	FreeCam->SetActorLocationAndRotation(At, Turn);
	// The animal: straight on at the pace it had, or standing.
	D->SetMouthOpen(false);
	if (bCruise && !D->IsFlying())
	{
		D->SetRunning(bCruiseRun);
		D->AddMovementInput(D->GetActorForwardVector());
	}
	else
	{
		D->SetRunning(false);
	}
}

void ABFPlayerController::PressMenu(const FString& Label, int32 Nth, int32 Then)
{
	MenuPending = MenuButton(Label, Nth);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] menu button '%s' #%d %s"), *Label, Nth, MenuPending.IsValid() ? TEXT("found") : TEXT("NOT FOUND"));
	if (MenuPending.IsValid() && PickerScroll.IsValid())
	{
		PickerScroll->ScrollDescendantIntoView(MenuPending, false, EDescendantScrollDestination::IntoView);    // as a player scrolls to it
	}
	MenuAfter = Then;
	AutoStep = 334;
	AutoClock = 0.f;
	AutoFrame = 0;
}

TSharedPtr<SWidget> ABFPlayerController::MenuButton(const FString& Label, int32 Nth) const
{
	return PickerWidget.IsValid() ? ABFSpliceStage::FindButtonIn(PickerWidget.ToSharedRef(), Label, Nth) : nullptr;
}

void ABFPlayerController::RebuildPicker()
{
	bPickerDirty = false;
	ABFGameMode* M = Mode();
	if (!PickerList.IsValid() || !M)
	{
		return;
	}
	PickerList->ClearChildren();
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 13);
	auto Button = [&](const FString& Label, TFunction<void()> Click, const FSlateFontInfo& Font, bool bEnabled = true) -> TSharedRef<SWidget>
	{
		return SNew(SButton).IsFocusable(false).IsEnabled(bEnabled).ContentPadding(FMargin(6.f, 2.f))
			.OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Font).Text(FText::FromString(Label))
			];
	};
	auto Header = [&](const FString& Text)
	{
		PickerList->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 4.f)[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f)).Text(FText::FromString(Text))];
	};
	auto Stats = [](const FBFSpecies& S)
	{
		return FString::Printf(TEXT("%.1f m long, %s kg; walk %.1f m/s, %s %.1f m/s"), S.Bounds.GetSize().X / 100.f,
			*FText::AsNumber(FMath::RoundToInt(S.MassKg)).ToString(), S.Walk.SpeedCm / 100.f, *S.Fast.Name, S.Fast.SpeedCm / 100.f);
	};
	// One creature's row: play it (the label), then its other buttons.
	struct FAction { FString Label; TFunction<void()> Click; };
	auto Row = [&](int32 i, const FString& Prefix, TArray<FAction> More)
	{
		const FBFSpecies& S = M->GetSpecies()[i];
		const bool bOn = i == Current;
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		H->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[
			Button(FString::Printf(TEXT("%s%s%s   (%s)%s"), bOn ? TEXT("[ ") : TEXT(""), *Prefix, *S.Name, *Stats(S), bOn ? TEXT(" ]") : TEXT("")),
				[this, i]() { Pick(i); ShowPicker(false); }, Big)
		];
		More.Add({ TEXT("Beside you"), [this, i]() { BringCompanion(i); } });     // a second animal beside yours
		for (FAction& B : More)
		{
			H->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(B.Label, B.Click, Small)];
		}
		PickerList->AddSlot().AutoHeight().Padding(0.f, 2.f)[H];
	};

	if (!PickerNote.IsEmpty())
	{
		PickerList->AddSlot().AutoHeight().Padding(0.f, 4.f)[SNew(STextBlock).Font(Small).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.6f, 0.8f, 1.f)).Text(FText::FromString(PickerNote))];
	}
	// The world: a new island (a random seed, or one typed), going there, keeping it, and the kept ones.
	{
		ABFWorld* Isle = M->GetIsland();
		TSharedRef<SHorizontalBox> IsleRow = SNew(SHorizontalBox);
		IsleRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[SNew(STextBlock).Font(Big).Text(FText::FromString(TEXT("The world")))];
		IsleRow->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(TEXT("New island"), [this]() { MakeIsland(FMath::Rand() % 1000000); }, Small)];
		IsleRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 4.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("or seed")))];
		IsleRow->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(110.f)[SAssignNew(SeedBox, SEditableTextBox).Font(Small).HintText(FText::FromString(TEXT("a number")))]
		];
		IsleRow->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(TEXT("Make it"), [this]()
		{
			const FString Typed = SeedBox.IsValid() ? SeedBox->GetText().ToString().TrimStartAndEnd() : FString();
			MakeIsland(Typed.IsNumeric() ? FCString::Atoi(*Typed) : int32(GetTypeHash(Typed) % 1000000));
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}, Small)];
		if (Isle && Isle->IsReady())
		{
			const int32 Seed = Isle->GetSeed();
			IsleRow->AddSlot().AutoWidth().Padding(12.f, 0.f, 2.f, 0.f)[Button(FString::Printf(TEXT("Go to island %d"), Seed), [this]()
			{
				Area = EBFSpawnArea::World;
				Pick(Current);
				ShowPicker(false);
			}, Small)];
			if (!M->GetKeptIslands().Contains(Seed))
			{
				IsleRow->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(TEXT("Keep it"), [this, Seed]() { if (Mode()) { Mode()->KeepIsland(Seed); } bPickerDirty = true; }, Small)];
			}
			// Saving (life cycle step 3): now, or starting it afresh (the save gone, the island made new).
			IsleRow->AddSlot().AutoWidth().Padding(12.f, 0.f, 2.f, 0.f)[Button(TEXT("Save the island"), [this]() { SaveIsland(true); bPickerDirty = true; }, Small)];
			if (FPaths::FileExists(IslandSavePath(Seed)))
			{
				IsleRow->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(TEXT("Start it afresh"), [this, Seed]()
				{
					IFileManager::Get().Delete(*IslandSavePath(Seed));
					if (Life)
					{
						Life->Clear();
					}
					RestoredSeed = INDEX_NONE;
					PendingYou.Reset();
					if (Mode())
					{
						Mode()->MakeIsland(Seed);        // (not this class's MakeIsland: that would save it first)
					}
					bGoWhenReady = true;
					PickerNote = FString::Printf(TEXT("Island %d starts afresh: its save is gone."), Seed);
					bPickerDirty = true;
				}, Small)];
			}
			// You in the life cycle (step 9): female or male.
			{
				const bool bFemale = !Life || Life->bPlayerFemale;
				IsleRow->AddSlot().AutoWidth().Padding(12.f, 0.f, 2.f, 0.f)[Button(bFemale ? TEXT("You: female (switch)") : TEXT("You: male (switch)"), [this]()
				{
					if (!Life)
					{
						Life = GetWorld()->SpawnActor<ABFLife>();
					}
					Life->bPlayerFemale = !Life->bPlayerFemale;
					bPickerDirty = true;
				}, Small)];
			}
			// The island's animals (World and AI tab, step 3): herds of the plant eaters round you, or taken away again.
			const bool bLife = Life && Life->Num() > 0;
			IsleRow->AddSlot().AutoWidth().Padding(12.f, 0.f, 2.f, 0.f)[Button(bLife ? TEXT("Take the animals away") : TEXT("Animals: herds round you"), [this]()
			{
				ToggleLife();
				bPickerDirty = true;
			}, Small)];
		}
		PickerList->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[IsleRow];
		// The game pace: how much faster than their bodies' real speeds every animal moves (they'd be too slow to play).
		TSharedRef<SHorizontalBox> PaceRow = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(STextBlock).Font(Small).Text(FText::FromString(FString::Printf(TEXT("Game pace (now %.1fx real speed):"), ABFDino::GamePace)))
			];
		for (const float P : { 1.f, 1.4f, 1.8f, 2.2f, 2.6f })
		{
			PaceRow->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(FString::Printf(TEXT("%.1fx%s"), P, P == 1.f ? TEXT(" (real)") : P == 1.8f ? TEXT(" (default)") : TEXT("")),
				[this, P]()
			{
				ABFDino::GamePace = P;
				for (TActorIterator<ABFDino> It(GetWorld()); It; ++It)
				{
					It->ApplyPace();
				}
				if (Mode())
				{
					Mode()->SaveSettings();
				}
				bPickerDirty = true;
			}, Small)];
		}
		PickerList->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[PaceRow];
		if (Isle && !Isle->IsReady())
		{
			PickerList->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.6f, 0.8f, 1.f)).Text_Lambda([this]()
				{
					ABFWorld* I = Mode() ? Mode()->GetIsland() : nullptr;
					return FText::FromString(I ? FString::Printf(TEXT("Making island %d: %s, %.0f%%"), I->GetSeed(), *I->GetStage(), 100.f * I->GetProgress()) : FString());
				})
			];
		}
		const TArray<int32> Kept = M->GetKeptIslands();
		if (Kept.Num() > 0)
		{
			TSharedRef<SWrapBox> KeptRow = SNew(SWrapBox).UseAllottedSize(true);
			KeptRow->AddSlot().Padding(2.f).VAlign(VAlign_Center)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Kept islands:")))];
			for (const int32 S : Kept)
			{
				KeptRow->AddSlot().Padding(2.f)[Button(FString::Printf(TEXT("Island %d%s"), S, FPaths::FileExists(IslandSavePath(S)) ? TEXT(" (saved)") : TEXT("")), [this, S]() { MakeIsland(S); }, Small)];
			}
			PickerList->AddSlot().AutoHeight()[KeptRow];
		}
	}
	// A second animal beside yours: who, what it does, and the person for scale.
	{
		auto Mode_ = [&](ECompanion C, const TCHAR* Word) -> TSharedRef<SWidget>
		{
			return Button(CompanionMode == C ? FString::Printf(TEXT("[%s]"), Word) : FString(Word), [this, C]() { CompanionMode = C; bPickerDirty = true; }, Small);
		};
		TSharedRef<SHorizontalBox> Pair = SNew(SHorizontalBox);
		Pair->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(STextBlock).Font(Small).Text(FText::FromString(Companion ? TEXT("Beside you: ") + Companion->GetSpecies().Name
				: FString(TEXT("Beside you: nobody (Beside you on any creature below)"))))
		];
		if (Companion)
		{
			Pair->AddSlot().AutoWidth().Padding(2.f, 0.f)[Mode_(ECompanion::Stay, TEXT("Stay"))];
			Pair->AddSlot().AutoWidth().Padding(2.f, 0.f)[Mode_(ECompanion::Follow, TEXT("Follow"))];
			Pair->AddSlot().AutoWidth().Padding(2.f, 0.f)[Mode_(ECompanion::Copy, TEXT("Copy me"))];
			Pair->AddSlot().AutoWidth().Padding(2.f, 0.f, 12.f, 0.f)[Button(TEXT("Send away"), [this]() { SendCompanionAway(); }, Small)];
		}
		Pair->AddSlot().AutoWidth().Padding(2.f, 0.f)[Button(Person ? TEXT("[Person for scale]") : TEXT("Person for scale"), [this]() { SetPerson(Person == nullptr); }, Small)];
		PickerList->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[Pair];
	}
	TArray<int32> Six, Catalog, Yours;
	for (int32 i = 0; i < M->GetSpecies().Num(); ++i)
	{
		const FBFSpecies& S = M->GetSpecies()[i];
		(!S.IsSplice() ? Six : ABFGameMode::IsYours(S.Key) ? Yours : Catalog).Add(i);
	}
	// Your creatures newest first (by when each was last grown).
	TMap<int32, FDateTime> Made;
	for (int32 i : Yours)
	{
		Made.Add(i, IFileManager::Get().GetTimeStamp(*FPaths::Combine(M->RigFolder, M->GetSpecies()[i].Key + TEXT("_rig.json"))));
	}
	Yours.Sort([&](int32 A, int32 B)      // (the same time: by name, so the order doesn't shuffle)
	{
		return Made[A] != Made[B] ? Made[A] > Made[B] : M->GetSpecies()[A].Name < M->GetSpecies()[B].Name;
	});

	Header(TEXT("The six   (keys 1 to 6;  [ and ] step through every creature)"));
	for (int32 n = 0; n < Six.Num(); ++n)
	{
		const FString Key = M->GetSpecies()[Six[n]].Key;
		Row(Six[n], FString::Printf(TEXT("%d   "), n + 1), { { TEXT("Splice from it"), [this, Key]() { EditCreature(Key); } } });
	}
	Header(TEXT("Spliced for the catalog"));
	for (int32 i : Catalog)
	{
		const FString Key = M->GetSpecies()[i].Key;
		Row(i, FString(), { { TEXT("Edit a copy"), [this, Key]() { EditCreature(Key); } } });
	}
	Header(TEXT("Your creatures   (made in Splice a creature, newest first)"));
	if (Yours.Num() == 0)
	{
		PickerList->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("None yet: Splice a creature, then Walk it.")))];
	}
	for (int32 i : Yours)
	{
		const FString Key = M->GetSpecies()[i].Key;
		if (Key == RenamingKey)
		{
			// Its name being typed: Enter or Save keeps it, Esc or Cancel doesn't; empty gives back the name it was built with.
			auto Save = [this, Key]()
			{
				if (RenameBox.IsValid() && Mode())
				{
					Mode()->RenameCreature(Key, RenameBox->GetText().ToString());
				}
				RenamingKey.Reset();
				bPickerDirty = true;
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			};
			PickerList->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(420.f)
					[
						SAssignNew(RenameBox, SEditableTextBox).Font(Big).Text(FText::FromString(M->GetSpecies()[i].Name))
						.SelectAllTextWhenFocused(true)
						.HintText(FText::FromString(TEXT("empty: the name it was built with")))
						.OnTextCommitted_Lambda([this, Save](const FText&, ETextCommit::Type How)
						{
							if (How == ETextCommit::OnEnter)
							{
								Save();
							}
							else if (How == ETextCommit::OnCleared)      // Esc
							{
								RenamingKey.Reset();
								bPickerDirty = true;
							}
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 2.f, 0.f)[Button(TEXT("Save"), Save, Small)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(TEXT("Cancel"), [this]() { RenamingKey.Reset(); bPickerDirty = true; }, Small)]
			];
			continue;
		}
		Row(i, FString(), {
			{ TEXT("Edit"), [this, Key]() { EditCreature(Key); } },
			{ TEXT("Rename"), [this, Key]() { RenamingKey = Key; bPickerDirty = true; } },
			{ TEXT("Remove"), [this, Key]() { RemoveCreature(Key); } } });
	}
	const TArray<ABFGameMode::FRemoved> Removed = M->GetRemoved();
	if (Removed.Num() > 0)
	{
		Header(TEXT("Removed   (their files are in bone_forge/removed: nothing was deleted)"));
		const bool bBusy = Stage && Stage->IsBringingBack();
		for (const ABFGameMode::FRemoved& R : Removed)
		{
			const FString Key = R.Key;
			PickerList->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f)).Text(FText::FromString(R.Name))]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Put back"), [this, Key]() { PutBack(Key); }, Small, !bBusy)]
			];
		}
	}
	for (const FString& E : M->GetLoadErrors())
	{
		PickerList->AddSlot().AutoHeight().Padding(0.f, 3.f)[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(1.f, 0.4f, 0.3f)).Text(FText::FromString(E))];
	}
	if (!RenamingKey.IsEmpty() && RenameBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(RenameBox, EFocusCause::SetDirectly);
	}
}

void ABFPlayerController::OpenSplicer()
{
	ShowPicker(false);
	if (APawn* Old = GetPawn())
	{
		UnPossess();
		Old->Destroy();
	}
	if (HudWidget.IsValid())
	{
		HudWidget->SetVisibility(EVisibility::Collapsed);
	}
	if (!Stage)
	{
		Stage = GetWorld()->SpawnActor<ABFSpliceStage>();
	}
	Stage->Open(this, Mode()->RigFolder);
}

void ABFPlayerController::CloseSplicer()
{
	Stage->Close();
	if (HudWidget.IsValid())
	{
		HudWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	Pick(Current);
	ShowPicker(true);
}

bool ABFPlayerController::WalkIt(const FString& Key, USkeletalMesh* Mesh, bool bTest, FString& Error)
{
	ABFGameMode* M = Mode();
	const int32 Index = M ? M->AddSpecies(Key, Mesh, Error) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] walk it: %s built in the game (%s), species %d of %d"), *Key,
		M->GetSpecies()[Index].bRuntimeMesh ? TEXT("runtime mesh") : TEXT("its saved mesh"), Index + 1, M->GetSpecies().Num());
	Current = Index;
	Stage->Close();
	if (HudWidget.IsValid())
	{
		HudWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	Pick(Current);
	ShowPicker(false);
	if (FParse::Param(FCommandLine::Get(), TEXT("bfwalk")))      // the scripted check (-bfeditor -bfwalk): catalog it, then close
	{
		AutoOnly = { Key };
		AutoSpecies = 0;
		bAuto = true;
		AutoStep = 90;
		AutoClock = 0.f;
	}
	else if (bTest)                                               // Test it: the test run on it, here
	{
		bAuto = true;
		AutoStep = 158;
		AutoClock = 0.f;
	}
	return true;
}

void ABFPlayerController::StartTestRun(int32 Index, bool bThenQuit)
{
	ABFGameMode* M = Mode();
	if (!M || !M->GetSpecies().IsValidIndex(Index))
	{
		return;
	}
	Report.Reset();
	ReportTitle = M->GetSpecies()[Index].Name;
	bTestRun = true;
	bTestQuit = bThenQuit;
	bAuto = true;
	bShowBalance = false;
	AutoSpecies = Index;
	AutoStep = 160;
	AutoClock = 0.f;
	ShowPicker(false);
	ShowReport(false);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] test %s: starting"), *M->GetSpecies()[Index].Key);
}

FString ABFPlayerController::Versus(const FBFSpecies& S, TFunctionRef<float(const FBFSpecies&)> Of, TFunctionRef<FString(float)> Show) const
{
	ABFGameMode* M = Mode();
	if (!M)
	{
		return FString();
	}
	if (S.IsSplice())
	{
		const FBFSpecies* B = M->GetSpecies().FindByPredicate([&](const FBFSpecies& X) { return X.Key == S.BaseKey; });
		return B ? FString::Printf(TEXT(" (%s: %s)"), *B->Name, *Show(Of(*B))) : FString();
	}
	int32 Rank = 1;
	for (const FBFSpecies& X : M->GetSpecies())
	{
		Rank += !X.IsSplice() && X.Key != S.Key && Of(X) > Of(S) ? 1 : 0;
	}
	static const TCHAR* Nth[] = { TEXT("first"), TEXT("second"), TEXT("third"), TEXT("fourth"), TEXT("fifth"), TEXT("sixth") };
	return FString::Printf(TEXT(" (%s of the six)"), Nth[FMath::Clamp(Rank, 1, 6) - 1]);
}

void ABFPlayerController::AddResult(const FString& Test, int32 Result, const FString& Text)
{
	Report.Add({ Test, Result, Text });
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] test %s %s: %s  %s"), Mode() ? *Mode()->GetSpecies()[AutoSpecies].Key : TEXT("-"), *Test,
		Result > 0 ? TEXT("CAN") : Result == 0 ? TEXT("CAN'T") : TEXT("n/a"), *Text);
}

void ABFPlayerController::FinishTestRun()
{
	ABFGameMode* M = Mode();
	const FBFSpecies& S = M->GetSpecies()[AutoSpecies];
	// The card as text too: Saved/Reports/<key>.txt.
	FString Text = FString::Printf(TEXT("Test report: %s (%s)\n%.1f m long, %s kg\n\n"), *S.Name, *S.Key, S.Bounds.GetSize().X / 100.f,
		*FText::AsNumber(FMath::RoundToInt(S.MassKg)).ToString());
	for (const FTestLine& L : Report)
	{
		Text += FString::Printf(TEXT("%-6s %-6s %s\n"), *L.Test, L.Result > 0 ? TEXT("yes") : L.Result == 0 ? TEXT("NO") : TEXT("-"), *L.Text);
	}
	ReportFile = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Reports"), S.Key + TEXT(".txt")));
	FFileHelper::SaveStringToFile(Text, *ReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bTestRun = false;
	bAutoForward = false;
	ShowReport(true);
	if (bTestQuit)
	{
		AutoStep = 176;
		AutoClock = 0.f;
		return;
	}
	// Back on the flat with the report showing; the player drives again.
	bAuto = false;
	bShowBalance = true;
	Area = EBFSpawnArea::Flat;
	Pick(AutoSpecies);
}

void ABFPlayerController::ShowReport(bool bShow)
{
	bReportOpen = bShow;
	if (!ReportWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		ReportWidget = SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.86f))
				.Padding(26.f)
				[
					SNew(SBox).WidthOverride(1100.f)
					[
						SAssignNew(ReportBox, SVerticalBox)
					]
				]
			];
		GEngine->GameViewport->AddViewportWidgetContent(ReportWidget.ToSharedRef(), 40);
	}
	if (!ReportWidget.IsValid())
	{
		return;
	}
	ReportWidget->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
	if (!bShow)
	{
		return;
	}
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 24);
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FSlateFontInfo Line = FCoreStyle::GetDefaultFontStyle("Regular", 14);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	ReportBox->ClearChildren();
	ReportBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[SNew(STextBlock).Font(Title).Text(FText::FromString(TEXT("Test report: ") + ReportTitle))];
	for (const FTestLine& L : Report)
	{
		const FLinearColor Col = L.Result > 0 ? FLinearColor(0.45f, 0.9f, 0.45f) : L.Result == 0 ? FLinearColor(1.f, 0.45f, 0.35f) : FLinearColor(0.6f, 0.6f, 0.6f);
		ReportBox->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(90.f)[SNew(STextBlock).Font(Big).ColorAndOpacity(Col).Text(FText::FromString(L.Test))]]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(80.f)[SNew(STextBlock).Font(Big).ColorAndOpacity(Col)
				.Text(FText::FromString(L.Result > 0 ? TEXT("Can") : L.Result == 0 ? TEXT("Can't") : TEXT("-")))]]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Font(Line).AutoWrapText(true).Text(FText::FromString(L.Text))]
		];
	}
	ReportBox->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
		.Text(FText::FromString(FString::Printf(TEXT("Enter or Esc closes this    T tests again    Saved to %s"), *ReportFile)))
	];
}

void ABFPlayerController::ShowPicker(bool bShow)
{
	bPickerOpen = bShow;
	if (bShow)
	{
		RebuildPicker();          // the creatures as they are now (Walk it, Remove and Put back change them)
		if (bLookOpen)
		{
			ShowLook(false);      // one panel at a time
		}
	}
	else
	{
		RenamingKey.Reset();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetVisibility(bShow ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);   // (it showed through behind the menu)
	}
	if (PickerWidget.IsValid())
	{
		PickerWidget->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
	}
	SetShowMouseCursor(bShow);
	if (bShow)
	{
		FInputModeGameAndUI In;
		In.SetHideCursorDuringCapture(false);
		SetInputMode(In);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ABFPlayerController::BuildWidgets()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}
	ABFGameMode* M = Mode();
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 26);
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 17);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 13);

	// Where to spawn: the flat field or the foot of one of the ramps.
	TSharedRef<SHorizontalBox> Areas = SNew(SHorizontalBox);
	auto AddArea = [&](const FString& Label, EBFSpawnArea A, int32 R)
	{
		Areas->AddSlot().AutoWidth().Padding(4.f)
		[
			SNew(SButton)
			.IsFocusable(false)
			.OnClicked_Lambda([this, A, R]() { Area = A; Ramp = R; return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Big)
				.Text_Lambda([this, A, R, Label]()
				{
					const bool bOn = Area == A && (A != EBFSpawnArea::Slopes || Ramp == R);
					return FText::FromString(bOn ? FString::Printf(TEXT("[ %s ]"), *Label) : Label);
				})
			]
		];
	};
	AddArea(TEXT("Flat ground"), EBFSpawnArea::Flat, 0);
	for (int32 r = 0; r < ABFGameMode::NumRamps; ++r)
	{
		AddArea(FString::Printf(TEXT("Slope %.0f°"), ABFGameMode::RampAngle(r)), EBFSpawnArea::Slopes, r);
	}
	AddArea(TEXT("Cliff 20 m"), EBFSpawnArea::Cliff, 0);
	AddArea(TEXT("Pool"), EBFSpawnArea::Pool, 0);

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[SNew(STextBlock).Font(Title).Text(FText::FromString(TEXT("Bone Forge testing ground")))];
	List->AddSlot().AutoHeight().Padding(0.f, 4.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Start on")))];
	List->AddSlot().AutoHeight()[Areas];
	List->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Pick a creature")))];
	List->AddSlot().AutoHeight()[SAssignNew(PickerList, SVerticalBox)];      // RebuildPicker fills it
	List->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 12.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SButton)
			.IsFocusable(false)
			.OnClicked_Lambda([this]() { OpenSplicer(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Big).Text(FText::FromString(TEXT("Splice a creature")))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.IsFocusable(false)
			.OnClicked_Lambda([this]() { StartTestRun(Current, false); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Big).Text_Lambda([this]()
				{
					ABFGameMode* GM = Mode();
					return FText::FromString(GM && GM->GetSpecies().IsValidIndex(Current) ? TEXT("Test the ") + GM->GetSpecies()[Current].Name : FString(TEXT("Test it")));
				})
			]
		]
	];
	List->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Tab or Esc opens and closes this menu.")))];
	// Every key, grouped (the bottom of the screen keeps only the everyday ones).
	List->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(Small).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f)).Text(FText::FromString(TEXT(
			"Moving:  WASD move, mouse look, hold Shift to speed up (on land, in the air and in water), Space jump (with wings: take off, then flap), wheel zoom\n"
			"Its mouth:  E bite (a plant eater by plants eats a mouthful), Q or right mouse open the mouth (a roar in the baked clips)\n"
			"Your needs on the island:  E eats (plants or a kill) or drinks (at fresh water), Z rests (a move key gets up)\n"
			"The island's animals:  menu, The world row, Animals (herds and hunters round you); I what each is thinking, O the island's life at a glance, N follow the next animal (M stop); . past full speed: time x2, x4\n"
			"Creatures:  1 to 6 the six, [ and ] every creature, R respawn, K ragdoll (again: stand up), L its look, T test it\n"
			"Places:  F flat ground, G the next slope, H the cliff, J the pool\n"
			"Watching:  , slower and . faster, P pause, / one frame while paused, V free camera, U hide the text, F9 save a picture, B balance, C baked clips or live pose")))
	];
	List->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(SButton)
		.IsFocusable(false)
		.OnClicked_Lambda([this]() { ConsoleCommand(TEXT("quit")); return FReply::Handled(); })
		[
			SNew(STextBlock).Font(Big).Text(FText::FromString(TEXT("Quit")))
		]
	];

	PickerWidget = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.82f))
			.Padding(28.f)
			[
				SAssignNew(PickerScroll, SScrollBox) + SScrollBox::Slot()[List]      // the list grows with every creature spliced
			]
		];

	HudWidget = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(16.f)
		[
			SAssignNew(StatusText, STextBlock).Font(Big).ShadowOffset(FVector2D(1.f, 1.f)).AutoWrapText(true)
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(16.f)
		[
			SAssignNew(WatchBack, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.7f))
			.Padding(10.f)
			.Visibility(EVisibility::Collapsed)
			[
				SAssignNew(WatchPanel, STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 9)).ShadowOffset(FVector2D(1.f, 1.f))
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(16.f)
		[
			SNew(STextBlock).Font(Small).ShadowOffset(FVector2D(1.f, 1.f)).AutoWrapText(true)
			.Text(FText::FromString(TEXT("WASD move    Shift speed up    Space jump / take off    E bite / eat / drink    Z rest    Q mouth    L look    V free camera    , . slow motion    P pause    T test it    Tab menu (every key is listed there)")))
		];

	GEngine->GameViewport->AddViewportWidgetContent(HudWidget.ToSharedRef(), 10);
	GEngine->GameViewport->AddViewportWidgetContent(PickerWidget.ToSharedRef(), 20);
	BuildLookPanel();
}

void ABFPlayerController::UpdateStatus()
{
	ABFDino* D = Dino();
	if (!StatusText.IsValid() || !D)
	{
		return;
	}
	const FBFSpecies& S = D->GetSpecies();
	// Its name now (renamed in the menu since it appeared: the animal keeps the copy it was spawned with).
	const FBFSpecies* Now = Mode() ? Mode()->GetSpecies().FindByPredicate([&](const FBFSpecies& X) { return X.Key == S.Key; }) : nullptr;
	const FString Shown = Now ? Now->Name : S.Name;
	const FString Where = Area == EBFSpawnArea::Flat ? TEXT("flat ground") : Area == EBFSpawnArea::Cliff ? TEXT("cliff")
		: Area == EBFSpawnArea::Pool ? TEXT("pool")
		: Area == EBFSpawnArea::World ? FString::Printf(TEXT("island %d"), Mode() && Mode()->GetIsland() ? Mode()->GetIsland()->GetSeed() : 0)
		: FString::Printf(TEXT("%.0f° slope area"), ABFGameMode::RampAngle(Ramp));
	// Prototype 3: what the wings can do, and a splice's species by mass.
	FString Wings;
	if (S.CanFly() && !S.FlightSummary.IsEmpty())
	{
		Wings = TEXT("\nFlight check: ") + S.FlightSummary.Replace(TEXT(". "), TEXT(".\n    "));   // a sentence a line
	}
	if (S.IsSplice() || S.Legs != EBFLegs::Walks)
	{
		Wings += TEXT("\nLeg check: ") + S.LegSummary;
	}
	if (D->GetGroundSlopeDeg() > 5.f && !D->IsInWater() && !S.ClimbSummary.IsEmpty())
	{
		Wings += TEXT("\nClimb check: ") + S.ClimbSummary;
	}
	if (D->IsInWater() && !S.SwimSummary.IsEmpty())
	{
		Wings += TEXT("\nSwim check: ") + S.SwimSummary + (D->IsHeadUnderWater() ? TEXT("\n    Its head is under water.") : TEXT(""));
	}
	if (S.IsSplice())
	{
		Wings += TEXT("\nLineage: ") + S.Lineage;
	}
	if (D->IsRagdoll())
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s  |  ragdoll  |  K or R to stand up again%s%s"), *Shown,
			S.Legs == EBFLegs::Buckles ? TEXT("\nLeg check: ") : TEXT(""), S.Legs == EBFLegs::Buckles ? *S.LegSummary : TEXT(""))));
		return;
	}
	// Watching: slow motion, paused, the free camera, a picture just saved (on a line of their own, first).
	{
		FString Watch;
		if (bPaused)
		{
			Watch += TEXT("Paused (P goes on, / one frame)");
		}
		else if (SlowStep < 0)
		{
			Watch += FString::Printf(TEXT("Time x%d (, slower)"), 1 << -SlowStep);
		}
		else if (SlowStep > 0)
		{
			Watch += FString::Printf(TEXT("Slow motion 1/%d (. faster)"), 1 << SlowStep);
		}
		if (bFreeCam)
		{
			Watch += FString(Watch.IsEmpty() ? TEXT("") : TEXT("  |  ")) + TEXT("Free camera (V back): WASD, Space up, Ctrl down, Shift faster, wheel speed")
				+ (bCruise ? (bCruiseRun ? TEXT(", it keeps running") : TEXT(", it keeps walking")) : TEXT(""));
		}
		if (FPlatformTime::Seconds() < ShotNoteUntil)
		{
			Watch += FString(Watch.IsEmpty() ? TEXT("") : TEXT("  |  ")) + ShotNote;
		}
		if (FPlatformTime::Seconds() < JumpNoteUntil)
		{
			Watch += FString(Watch.IsEmpty() ? TEXT("") : TEXT("  |  ")) + S.JumpSummary;
		}
		if (FPlatformTime::Seconds() < FeedNoteUntil)
		{
			Watch += FString(Watch.IsEmpty() ? TEXT("") : TEXT("  |  ")) + FeedNote;
		}
		if (bNeeds)
		{
			// Its needs (step 6): on the island.
			const float Health = Life ? Life->PlayerHealth : 1.f;
			Watch += FString::Printf(TEXT("%sHunger %.0f%%  Thirst %.0f%%  Tired %.0f%%  Health %.0f%%%s"), Watch.IsEmpty() ? TEXT("") : TEXT("  |  "),
				100.f * (1.f - NeedStomach / FMath::Max(NeedStomachMax, 0.01f)), 100.f * NeedThirst, 100.f * NeedTired, 100.f * Health,
				bResting ? TEXT("  (resting: a move key gets up)") : NeedTired > 0.9f ? TEXT("  (exhausted: rest, Z)") : TEXT(""));
			if (Life && Life->PlayerI != INDEX_NONE)
			{
				Watch += TEXT("  |  You: ") + Life->PlayerStatus();
			}
		}
		if (Life && FPlatformTime::Seconds() < Life->PlayerNoteUntil)
		{
			Watch += FString(Watch.IsEmpty() ? TEXT("") : TEXT("  |  ")) + Life->PlayerNote;
		}
		if (Companion)
		{
			Watch += FString::Printf(TEXT("%sBeside you: %s, %s %.1f m/s (%s)"), Watch.IsEmpty() ? TEXT("") : TEXT("  |  "), *Companion->GetSpecies().Name,
				*Companion->GetGaitName(), Companion->GetSpeedMs(),
				CompanionMode == ECompanion::Copy ? TEXT("copying you") : CompanionMode == ECompanion::Follow ? TEXT("following") : TEXT("staying"));
		}
		if (!Watch.IsEmpty())
		{
			Wings += TEXT("\n") + Watch;
		}
	}
	const FString Sprint = D->GetSprint() > 0.01f ? FString::Printf(TEXT("  (sprint %.0f%%)"), 100.f * D->GetSprint()) : FString();
	if (D->IsFlying())
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s  |  %s  %.1f m/s%s  |  altitude %.0f m  |  %s%s"),
			*Shown, *D->GetGaitName(), D->GetSpeedMs(), *Sprint, D->GetAltitudeM(), *D->GetClipName(), *Wings)));
		return;
	}
	const FString Balance = bShowBalance ? D->GetBalanceLine() : FString();
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s  |  %s  %.2f m/s%s  |  ground %.0f°  |  %s  |  %s%s%s%s%s"),
		*Shown, *D->GetGaitName(), D->GetSpeedMs(), *Sprint, D->GetGroundSlopeDeg(), *Where,
		S.HasClips() ? *D->GetClipName() : TEXT("no baked clips"), D->CanFly() ? TEXT("  |  Space to take off") : TEXT(""),
		Balance.IsEmpty() ? TEXT("") : TEXT("\n"), *Balance, *Wings)));
}

void ABFPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
	{
		return;
	}
	if (ABFDino* Shown = Dino())
	{
		Shown->SetBalanceShown(bShowBalance);
		Shown->SetIdleBehaviours(!bAuto);      // the scripted checks start the ones they want
	}
	UpdateStatus();
	if (bPickerDirty && bPickerOpen)
	{
		RebuildPicker();
	}
	if (Mode() && Mode()->GetIsland() && Mode()->GetIsland()->IsReady() && RestoredSeed != Mode()->GetIsland()->GetSeed())
	{
		RestoreIsland();
	}
	if (bGoWhenReady && Mode() && Mode()->GetIsland() && Mode()->GetIsland()->IsReady())
	{
		// The island stands: there, on open ground near its middle (or back where you were, as what you were).
		bGoWhenReady = false;
		Area = EBFSpawnArea::World;
		FString YouKey;
		if (PendingYou.IsValid() && PendingYou->TryGetStringField(TEXT("body"), YouKey))
		{
			const int32 YouI = Mode()->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == YouKey; });
			Current = YouI != INDEX_NONE ? YouI : Current;
		}
		Pick(Current);
		PickerNote = TEXT("Island ") + Mode()->GetIsland()->GetSummary();
		if (PendingYou.IsValid() && Dino() && Dino()->GetSpecies().Key == YouKey)
		{
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			float Ground = 0.f;
			if (PendingYou->TryGetArrayField(TEXT("at"), At) && At->Num() == 3
				&& Mode()->GetIsland()->GroundAt(FVector2D((*At)[0]->AsNumber(), (*At)[1]->AsNumber()), Ground))
			{
				ABFDino* Me = Dino();
				Me->SetActorLocationAndRotation(FVector((*At)[0]->AsNumber(), (*At)[1]->AsNumber(), Ground + Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f),
					FRotator(0.f, PendingYou->GetNumberField(TEXT("yaw")), 0.f), false, nullptr, ETeleportType::ResetPhysics);
				SetControlRotation(FRotator(-15.f, PendingYou->GetNumberField(TEXT("yaw")), 0.f));
			}
			const FBFSpecies& S = Dino()->GetSpecies();
			NeedsKey = S.Key;
			NeedStomachMax = FMath::Max(1.f, (Dino()->CanFeed() ? 0.004f : 0.04f) * S.MassKg);
			NeedStomach = FMath::Clamp(float(PendingYou->GetNumberField(TEXT("full"))), 0.f, 1.f) * NeedStomachMax;
			NeedThirst = FMath::Clamp(float(PendingYou->GetNumberField(TEXT("thirst"))), 0.f, 1.f);
			NeedTired = FMath::Clamp(float(PendingYou->GetNumberField(TEXT("tired"))), 0.f, 1.f);
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->PlayerHealth = FMath::Clamp(float(PendingYou->GetNumberField(TEXT("health"))), 0.05f, 1.f);
			bool bFem = true;
			if (PendingYou->TryGetBoolField(TEXT("female"), bFem))
			{
				Life->bPlayerFemale = bFem;
			}
			YouBack = PendingYou;          // (your generation, age and genes: onto you once you're among them)
			PickerNote += FString::Printf(TEXT("\nCarrying on where you were, as %s."), *S.Name);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] save: you're back as %s at %s"), *S.Key, *Dino()->GetActorLocation().ToString());
		}
		PendingYou.Reset();
		bPickerDirty = true;
	}
	if (Mode() && Mode()->GetIsland() && Mode()->GetIsland()->IsReady() && RestoredSeed == Mode()->GetIsland()->GetSeed())
	{
		SaveClock += FApp::GetDeltaTime();
		if (SaveClock > 120.f)
		{
			SaveClock = 0.f;
			SaveIsland(false);
		}
	}
	TickCompanion();             // (last frame's input when it copies you; this frame's comes below)
	TickWatch();
	TickNeeds(GetWorld()->GetDeltaSeconds());
	CopyMove = FVector::ZeroVector;
	bCopyRun = false;
	if (bAuto)
	{
		TickAuto(DeltaTime);
		if (!bAutoKeys)      // (-bfsprint presses the real keys, so the ordinary input code below has to read them)
		{
			return;
		}
	}
	if (Stage && Stage->IsOpen())
	{
		if ((WasInputKeyJustPressed(EKeys::Escape) && !ABFSpliceStage::IsTyping()) || Stage->WantsBack())
		{
			CloseSplicer();
		}
		return;
	}
	if (ABFSpliceStage::IsTyping())        // a name being typed in the menu: its keys aren't the game's
	{
		return;
	}

	if (bReportOpen)
	{
		if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::Enter))
		{
			ShowReport(false);
			return;
		}
		if (WasInputKeyJustPressed(EKeys::T))
		{
			StartTestRun(Current, false);
			return;
		}
	}
	else if (WasInputKeyJustPressed(EKeys::T))
	{
		StartTestRun(Current, false);
		return;
	}
	if (WasInputKeyJustPressed(EKeys::L))
	{
		ShowLook(!bLookOpen);       // colours and covering
	}
	if (bLookOpen && WasInputKeyJustPressed(EKeys::Escape))
	{
		ShowLook(false);
		return;
	}
	if (WasInputKeyJustPressed(EKeys::Tab))
	{
		ShowPicker(!bPickerOpen);
	}
	if (WasInputKeyJustPressed(EKeys::Escape))
	{
		ShowPicker(!bPickerOpen);   // quitting is the menu's Quit button, so a stray Esc can't close the game
	}
	// 1 to 6: the six (they load first); [ and ]: step through every creature.
	for (int32 i = 0; i < FMath::Min((int32)UE_ARRAY_COUNT(NumberKeys), Mode() ? Mode()->NumBaseSpecies() : 0); ++i)
	{
		if (WasInputKeyJustPressed(NumberKeys[i]))
		{
			Pick(i);
			ShowPicker(false);
		}
	}
	if (WasInputKeyJustPressed(EKeys::LeftBracket))
	{
		StepCreature(-1);
	}
	if (WasInputKeyJustPressed(EKeys::RightBracket))
	{
		StepCreature(1);
	}
	if (WasInputKeyJustPressed(EKeys::F))
	{
		Area = EBFSpawnArea::Flat;
		Pick(Current);
	}
	if (WasInputKeyJustPressed(EKeys::G))
	{
		Ramp = Area == EBFSpawnArea::Slopes ? (Ramp + 1) % ABFGameMode::NumRamps : 0;
		Area = EBFSpawnArea::Slopes;
		Pick(Current);
	}
	if (WasInputKeyJustPressed(EKeys::H))
	{
		Area = EBFSpawnArea::Cliff;
		Pick(Current);
	}
	if (WasInputKeyJustPressed(EKeys::J))
	{
		Area = EBFSpawnArea::Pool;
		Pick(Current);
	}
	if (WasInputKeyJustPressed(EKeys::R))
	{
		Pick(Current);
	}
	if (WasInputKeyJustPressed(EKeys::B))
	{
		bShowBalance = !bShowBalance;
	}
	if (WasInputKeyJustPressed(EKeys::X) && Dino() && Life && Life->Num() > 0)
	{
		// The life key (step 9): a male courts the female of his kind in condition nearest; a female due lays her eggs.
		FeedNote = Life->PlayerAct(Dino());
		FeedNoteUntil = FPlatformTime::Seconds() + 5.0;
	}
	if (WasInputKeyJustPressed(EKeys::K) && Dino())
	{
		if (Dino()->IsRagdoll())
		{
			if (!CarryOnAsYoung())
			{
				Pick(Current);
			}
		}
		else
		{
			Dino()->StartRagdoll();
		}
	}
	// Watching: , slower and . faster (full speed, a half, a quarter, an eighth), P pause, / a frame while paused, V the
	// free camera, U the on-screen text, F9 a picture.
	if (WasInputKeyJustPressed(EKeys::Comma))
	{
		SlowStep = FMath::Min(SlowStep + 1, 3);
	}
	if (WasInputKeyJustPressed(EKeys::Period))
	{
		SlowStep = FMath::Max(SlowStep - 1, -2);       // past full speed: twice, four times (watching the island's life)
	}
	if (WasInputKeyJustPressed(EKeys::P))
	{
		bPaused = !bPaused;
		StepFrames = 0;
	}
	if (StepFrames > 0)
	{
		--StepFrames;           // the frame let through has run
	}
	if (bPaused && WasInputKeyJustPressed(EKeys::Slash))
	{
		StepFrames = 1;
	}
	ApplyTimeScale();
	if (WasInputKeyJustPressed(EKeys::V))
	{
		SetFreeCam(!bFreeCam);
	}
	if (WasInputKeyJustPressed(EKeys::U))
	{
		bHudHidden = !bHudHidden;
		if (HudWidget.IsValid())
		{
			HudWidget->SetVisibility(bHudHidden ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
		}
	}
	if (WasInputKeyJustPressed(EKeys::F9))
	{
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Pictures"),
			FString::Printf(TEXT("%s_%s.png"), Dino() ? *Dino()->GetSpecies().Key : TEXT("view"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))));
		FScreenshotRequest::RequestScreenshot(File, false, false);      // without the on-screen text
		ShotNote = TEXT("Picture saved: ") + File;
		ShotNoteUntil = FPlatformTime::Seconds() + 4.0;
	}

	ABFDino* D = Dino();
	if (!D)
	{
		return;
	}
	if (bFreeCam)
	{
		TickFreeCam(D);
		return;
	}
	if (WasInputKeyJustPressed(EKeys::C))
	{
		bWantClips = !D->IsClips();
		D->SetClips(bWantClips);
	}
	if (WasInputKeyJustPressed(EKeys::O) && WatchPanel.IsValid())
	{
		bWatchPanel = !bWatchPanel;          // the island's life at a glance
		WatchBack->SetVisibility(bWatchPanel ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
	if (WasInputKeyJustPressed(EKeys::N) && Life && Life->Num() > 0)
	{
		// Follow the next living animal with the camera.
		for (int32 k = 1; k <= Life->Num(); ++k)
		{
			const int32 I = (WatchI + k) % Life->Num();
			if (!Life->Get(I).bDead && Life->Get(I).Body.IsValid())
			{
				WatchI = I;
				break;
			}
		}
		SetFreeCam(true);
	}
	if (WasInputKeyJustPressed(EKeys::M) && WatchI != INDEX_NONE)
	{
		WatchI = INDEX_NONE;
		SetFreeCam(false);
	}
	if (WasInputKeyJustPressed(EKeys::I))
	{
		ABFLife::bShowThoughts = !ABFLife::bShowThoughts;       // what the island's animals are thinking, over their heads
	}
	if (WasInputKeyJustPressed(EKeys::Z) && bNeeds)
	{
		bResting = !bResting;               // lie up and rest (a move key gets up again)
	}
	if (WasInputKeyJustPressed(EKeys::E) && !TryDrink() && !TryFeed())
	{
		D->Bite();
		// Hunting (World and AI tab, step 4): your bite lands on an island animal your snout reaches.
		const FString Bit = Life ? Life->PlayerBite(D) : FString();
		if (!Bit.IsEmpty())
		{
			FeedNote = Bit;
			FeedNoteUntil = FPlatformTime::Seconds() + 4.0;
		}
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		D->Zoom(1.f);
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		D->Zoom(-1.f);
	}
	if (bPickerOpen)
	{
		return;
	}

	float MX = 0.f, MY = 0.f;
	GetInputMouseDelta(MX, MY);
	FRotator R = GetControlRotation();
	R.Yaw += MX * 0.6f;
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + MY * 0.6f), -70.f, D->IsFlying() ? 50.f : 25.f);
	SetControlRotation(R);

	D->SetMouthOpen(IsInputKeyDown(EKeys::Q) || IsInputKeyDown(EKeys::RightMouseButton));
	const float F = (IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	if (D->CanFly() && !D->IsFlying() && WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		D->TakeOff();
	}
	else if (!D->CanFly() && WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		D->StartJump();           // as high as its legs push it, or a heave that doesn't leave the ground
		JumpNoteUntil = FPlatformTime::Seconds() + 3.0;
		if (Companion && CompanionMode == ECompanion::Copy)
		{
			Companion->StartJump();
		}
	}
	// Exhausted (step 6), it can't run: it walks till it has rested.
	D->SetRunning((IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift)) && !(bNeeds && NeedTired > 0.9f));     // on land, in the air, in the water
	if (D->IsFlying())
	{
		// The camera looks a little down at the animal; level flight is that view raised 15 degrees.
		D->SetFlightInput(F, IsInputKeyDown(EKeys::SpaceBar), FRotator(R.Pitch + 15.f, R.Yaw, 0.f));
		return;
	}

	const FRotator Yaw(0.f, R.Yaw, 0.f);
	const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	const float S = (IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	if ((F != 0.f || S != 0.f) && bResting)
	{
		bResting = false;           // up again
	}
	if (F != 0.f || S != 0.f)
	{
		D->AddMovementInput((Fwd * F + Right * S).GetSafeNormal());
	}
	// A companion copying you gets the same next frame (TickCompanion).
	CopyMove = F != 0.f || S != 0.f ? (Fwd * F + Right * S).GetSafeNormal() : FVector::ZeroVector;
	bCopyRun = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

// Scripted run: for each species, run on the flat field, then up one ramp, with a side and a three-quarter shot of each.
void ABFPlayerController::TickAuto(float DeltaTime)
{
	ABFGameMode* M = Mode();
	ABFDino* D = Dino();
	AutoClock += DeltaTime;
	auto Shot = [&](const TCHAR* Tag)
	{
		const FString File = FPaths::Combine(AutoOut, FString::Printf(TEXT("%s_%s.png"), *M->GetSpecies()[AutoSpecies].Key, Tag));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] screenshot %s  slope %.1f  speed %.2f m/s  gait %s  %s"), *File, D ? D->GetGroundSlopeDeg() : 0.f, D ? D->GetSpeedMs() : 0.f, D ? *D->GetGaitName() : TEXT("-"), D ? *D->GetClipName() : TEXT(""));
		if (D && bShowBalance && D->GetBalance().bValid)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] balance %s %s: %s  (margin %.1f cm; foot joints %.0f..%.0f cm off the ground)"), *M->GetSpecies()[AutoSpecies].Key, Tag,
				*D->GetBalanceLine(), D->GetBalance().MarginCm, D->GetBalance().FootGapMinCm, D->GetBalance().FootGapMaxCm);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] leg IK %s %s: %s"), *M->GetSpecies()[AutoSpecies].Key, Tag, *D->GetLegIKNote());
		}
	};
	auto Look = [&](float YawFromBody, float Pitch)
	{
		if (D)
		{
			SetControlRotation(FRotator(Pitch, D->GetActorRotation().Yaw + YawFromBody, 0.f));
		}
	};
	auto Next = [&](int32 Step) { AutoStep = Step; AutoClock = 0.f; AutoFrame = 0; };
	++AutoFrame;
	if (TestSpeed != 1.f && GetWorldSettings() && !FMath::IsNearlyEqual(GetWorldSettings()->TimeDilation, TestSpeed))
	{
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), TestSpeed);
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] checks: the game runs %.1f times as fast"), TestSpeed);
	}
	// -bfsplice: follow a take-off attempt and log how it went.
	auto StartWatch = [&]()
	{
		WatchStart = D ? D->GetActorLocation() : FVector::ZeroVector;
		WatchPeak = WatchAir = 0.f;
	};
	auto Watch = [&]()
	{
		if (D)
		{
			WatchPeak = FMath::Max(WatchPeak, D->GetActorLocation().Z - WatchStart.Z);
			WatchAir += D->GetCharacterMovement()->IsMovingOnGround() ? 0.f : DeltaTime;
		}
	};
	auto LogFlight = [&](const TCHAR* How)
	{
		if (!D)
		{
			return;
		}
		const FVector Now = D->GetActorLocation();
		const float Dist = FVector::Dist2D(Now, WatchStart) / 100.f;
		const float Drop = (WatchStart.Z - Now.Z) / 100.f;
		const FString Ratio = Drop > 1.f ? FString::Printf(TEXT(", glide ratio %.1f"), Dist / Drop) : FString();
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] wings %s %s: %s; %.1f s off the ground, at most %.1f m above the start, %.1f m covered, ended %.1f m %s%s"),
			*M->GetSpecies()[AutoSpecies].Key, How, D->IsRagdoll() ? TEXT("collapsed, no take-off") : D->IsFlying() ? TEXT("still flying") : TEXT("down"), WatchAir, WatchPeak / 100.f, Dist,
			FMath::Abs(Drop), Drop >= 0.f ? TEXT("lower") : TEXT("higher"), *Ratio);
	};
	// A screenshot is taken at the end of the frame, so whatever comes next waits a moment after it.
	auto ShotThen = [&](float At, const TCHAR* Tag, TFunctionRef<void()> Then)
	{
		if (!bShotTaken && AutoClock > At) { Shot(Tag); bShotTaken = true; }
		else if (bShotTaken && AutoClock > At + 0.3f) { bShotTaken = false; Then(); }
	};
	// Close on the middle of the tail, low, from the side it can be seen from: going up, from behind it over the flat;
	// coming down, from below it (behind would put the camera in the slope).
	auto TailClose = [&](bool bOn, float Yaw = 40.f)
	{
		if (D && bOn && D->GetSpecies().Tail.Num() > 0)
		{
			D->FocusBone(D->GetSpecies().Tail[D->GetSpecies().Tail.Num() / 2], 0.4f * D->BodyLengthCm());
			Look(Yaw, -10.f);
		}
		else if (D)
		{
			D->FocusBone(NAME_None, 0.f);
		}
	};
	auto TailLog = [&](const TCHAR* When)
	{
		if (D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] tails %s on the %.0f deg ramp, %s: %s"), *D->GetSpecies().Key, ABFGameMode::RampAngle(Ramp),
				When, *D->GetTailNote());
		}
	};

	if (D && !bAutoKeys)
	{
		// The scripted checks want a gait, on the ground: straight to it (the sprint check builds up as a player does).
		D->SetRunning(bAutoForward && bAutoRun && !D->IsFlying() && !D->IsSwimming(), !bAutoRamp);
		if (bAutoForward)
		{
			D->AddMovementInput(AutoDir.IsNearlyZero() ? FVector::ForwardVector : AutoDir);
		}
	}
	// -bfsprint: a key pressed or let go as a player's is, through Slate.
	auto PressKey = [&](FKey K, bool bDown)
	{
		FSlateApplication& A = FSlateApplication::Get();
		if (bDown)
		{
			A.ProcessKeyDownEvent(FKeyEvent(K, A.GetModifierKeys(), 0, false, 0, 0));
		}
		else
		{
			A.ProcessKeyUpEvent(FKeyEvent(K, A.GetModifierKeys(), 0, false, 0, 0));
		}
	};
	auto MarksFrom = [&](float From, float To)
	{
		SprintFrom = From;
		SprintTo = To;
		SprintTop = 0.f;
		SprintMark = 0;
		SprintTimes.Reset();
	};
	// The time it passes each quarter of the way from one speed to the other (the last one: within 3% of it).
	auto Marks = [&]()
	{
		if (!D)
		{
			return;
		}
		const float V = D->GetSpeedMs();
		SprintTop = FMath::Max(SprintTop, V);
		const float U = (V - SprintFrom) / (FMath::Abs(SprintTo - SprintFrom) > 0.01f ? SprintTo - SprintFrom : 0.01f);
		while (SprintMark < 4 && U >= (SprintMark == 3 ? 0.97f : (SprintMark + 1) * 0.25f))
		{
			SprintTimes += FString::Printf(TEXT(" %d%% %.1fs"), (SprintMark + 1) * 25, AutoClock);
			++SprintMark;
		}
	};
	auto SprintU = [&]()
	{
		if (!D)
		{
			return 0.f;
		}
		const FBFSpecies& S = D->GetSpecies();
		return (100.f * D->GetSpeedMs() - S.Walk.SpeedCm) / FMath::Max(S.Fast.SpeedCm - S.Walk.SpeedCm, 1.f);
	};
	// On to the next animal in the sprint check.
	auto SprintNextAnimal = [&]()
	{
		bAutoKeys = bAutoRamp = bAutoForward = bAutoRun = false;
		if (++AutoSpecies < M->GetSpecies().Num()) { Next(230); } else { Next(9); }
	};
	const FString SprintKey = M->GetSpecies().IsValidIndex(AutoSpecies) ? M->GetSpecies()[AutoSpecies].Key : FString();
	// -bfmouth: start watching the jaw; watch it this frame (a breath counted each time it comes back down from a peak, by
	// more than a footfall's jolt does); say how it went.
	auto MouthStart = [&]()
	{
		MouthSpan = FVector2D(1.f, 0.f);
		MouthPeak = 0.f;
		MouthDip = 1.f;
		MouthBreaths = 0;
		bMouthUp = true;
		MouthStrides0 = D ? D->GetPhase() : 0.f;
	};
	auto MouthTrack = [&]()
	{
		if (!D)
		{
			return;
		}
		const float J = D->GetMouthNow();
		MouthSpan = FVector2D(FMath::Min(MouthSpan.X, J), FMath::Max(MouthSpan.Y, J));
		const float Back = FMath::Max(0.35f * (MouthSpan.Y - MouthSpan.X), 0.01f);
		if (bMouthUp)
		{
			MouthPeak = FMath::Max(MouthPeak, J);
			if (J < MouthPeak - Back) { ++MouthBreaths; bMouthUp = false; MouthDip = J; }
		}
		else
		{
			MouthDip = FMath::Min(MouthDip, J);
			if (J > MouthDip + Back) { bMouthUp = true; MouthPeak = J; }
		}
	};
	auto MouthLog = [&](const TCHAR* What)
	{
		if (!D)
		{
			return;
		}
		const float Deg = 0.8f * D->GetSpecies().LimitsOf(TEXT("jaw")).Bend.Y;
		const float Strides = D->GetPhase() - MouthStrides0;
		const FString Each = MouthBreaths > 0 ? FString::Printf(TEXT(" (one every %.2f s, %.2f strides)"), AutoClock / MouthBreaths, Strides / MouthBreaths) : FString();
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] mouth %s %s, %.2f m/s: jaw %.2f..%.2f (%.1f..%.1f deg), opened %d times in %.1f s%s, winded %.2f"), *SprintKey, What, D->GetSpeedMs(),
			MouthSpan.X, MouthSpan.Y, MouthSpan.X * Deg, MouthSpan.Y * Deg, MouthBreaths, AutoClock, *Each, D->GetExertion());
	};
	// (for the pictures: the jaw near the top or the bottom of what it has been doing, once it has opened and shut)
	auto MouthAt = [&](bool bTop)
	{
		if (!D || MouthBreaths < 1)
		{
			return false;
		}
		const float U = (D->GetMouthNow() - MouthSpan.X) / FMath::Max(MouthSpan.Y - MouthSpan.X, 0.001f);
		return bTop ? U > 0.95f : U < 0.08f;
	};

	if (AutoStep >= 700 && AutoStep < 800)
	{
		TickRealismChecks(DeltaTime);          // (their own function: one more and TickAuto grows too big to optimise)
		return;
	}
	switch (AutoStep)
	{
	// The mouth on the move (-bfmouth; -bfonly works): every animal that can walk, with the real keys. Standing: shut. W: the
	// jaw's range and how often it opens against its strides. W and Shift, at top speed, the same. E at speed: it must snap
	// shut. Q held at speed: wide open, and back to breathing. Let go and stopped: what's left of it. Then its head from the
	// side and the front, walking and at top speed, with the jaw open and shut.
	case 280:
		if ((AutoOnly.Num() && !AutoOnly.Contains(SprintKey)) || M->GetSpecies()[AutoSpecies].Legs != EBFLegs::Walks)
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; bAutoForward = false; bAutoKeys = true; Pick(AutoSpecies); Next(281); }
		break;
	case 281:
		if (AutoClock > 1.f && D)
		{
			D->SetActorLocation(FVector(-3000.f, -10000.f, D->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(-15.f, 90.f, 0.f));
			MouthStart();
			Next(282);
		}
		break;
	case 282:
		MouthTrack();
		if (AutoClock > 1.5f) { MouthLog(TEXT("standing")); PressKey(EKeys::W, true); Next(283); }
		break;
	case 283:
		if (AutoClock > 3.f) { MouthStart(); Next(284); }
		break;
	case 284:
		MouthTrack();
		if (MouthBreaths >= 4 || AutoClock > 16.f) { MouthLog(TEXT("walking")); PressKey(EKeys::LeftShift, true); Next(285); }
		break;
	case 285:
		if (SprintU() >= 0.97f || AutoClock > 12.f) { MouthStart(); Next(286); }
		break;
	case 286:
		MouthTrack();
		if (D && (AutoClock > 4.f || D->GetActorLocation().Y > 18000.f)) { MouthLog(TEXT("at top speed")); PressKey(EKeys::E, true); Next(287); }
		break;
	case 287:
		if (AutoClock > 0.05f) { PressKey(EKeys::E, false); MouthStart(); Next(288); }
		break;
	case 288:   // the bite snaps shut half way through it
		if (AutoClock > 0.25f) { MouthTrack(); }
		if (AutoClock > BFBiteSeconds - 0.15f) { MouthLog(TEXT("biting at speed")); PressKey(EKeys::Q, true); MouthStart(); Next(289); }
		break;
	case 289:
		MouthTrack();
		if (AutoClock > 1.2f) { MouthLog(TEXT("holding Q at speed")); PressKey(EKeys::Q, false); Next(290); }
		break;
	case 290:
		if (AutoClock > 1.5f) { MouthStart(); Next(291); }
		break;
	case 291:
		MouthTrack();
		if (AutoClock > 1.5f) { MouthLog(TEXT("Q let go")); PressKey(EKeys::W, false); PressKey(EKeys::LeftShift, false); MouthStopClock = 0.f; Next(292); }
		break;
	case 292:   // W let go: until it has stopped, and a moment more
		MouthStopClock += DeltaTime;
		if (D && (D->GetSpeedMs() < 0.05f || MouthStopClock > 15.f)) { MouthStart(); Next(293); }
		break;
	case 293:
		MouthTrack();
		if (AutoClock > 2.f) { MouthLog(TEXT("stopped")); Next(294); }
		break;
	case 294:   // the pictures, driven by the script so the camera can look from the side
		if (AutoClock > 0.3f) { bAutoKeys = false; Area = EBFSpawnArea::Flat; bAutoForward = false; Pick(AutoSpecies); Next(295); }
		break;
	case 295:
		if (AutoClock > 1.f && D)
		{
			D->SetActorLocation(FVector(-3000.f, -10000.f, D->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			AutoDir = FVector(0.f, 1.f, 0.f);
			bAutoForward = true;
			bAutoRun = false;
			bAutoRamp = true;
			Next(296);
		}
		break;
	case 296:
		if (AutoClock > 3.f && D)     // (on the jaw's hinge, with no camera lag: a head-close view left behind at a sprint shows the tail)
		{
			D->FocusBone(TEXT("jaw"), 2.2f * FVector::Dist(D->GetBoneWorld(TEXT("jaw")), D->GetBoneWorld(TEXT("jaw_end"))) + 30.f);
			Look(90.f, -6.f);
			MouthStart();
			Next(297);
		}
		break;
	case 297:
		MouthTrack();
		if (MouthAt(true) || AutoClock > 16.f) { Next(298); }
		break;
	case 298:
		ShotThen(0.f, TEXT("mouth_walk_open"), [&]() { Next(299); });
		break;
	case 299:
		MouthTrack();
		if (MouthAt(false) || AutoClock > 16.f) { Next(300); }
		break;
	case 300:
		ShotThen(0.f, TEXT("mouth_walk_shut"), [&]() { bAutoRun = true; Next(301); });
		break;
	case 301:
		if (SprintU() >= 0.97f || AutoClock > 12.f) { MouthStart(); Next(302); }
		break;
	case 302:
		MouthTrack();
		if ((AutoClock > 1.f && MouthAt(true)) || AutoClock > 8.f) { Next(303); }
		break;
	case 303:
		ShotThen(0.f, TEXT("mouth_run_open"), [&]() { Next(304); });
		break;
	case 304:
		MouthTrack();
		if (MouthAt(false) || AutoClock > 8.f) { Next(305); }
		break;
	case 305:
		ShotThen(0.f, TEXT("mouth_run_shut"), [&]() { Look(140.f, -4.f); Next(306); });
		break;
	case 306:
		MouthTrack();
		if ((AutoClock > 0.5f && MouthAt(true)) || AutoClock > 8.f) { Next(307); }
		break;
	case 307:
		ShotThen(0.f, TEXT("mouth_run_open_front"), [&]() { if (D) { D->FocusBone(NAME_None, 0.f); } Look(90.f, -8.f); Next(308); });
		break;
	case 308:
		MouthTrack();
		if ((AutoClock > 0.6f && MouthAt(true)) || AutoClock > 8.f) { Next(309); }
		break;
	case 309:
		ShotThen(0.f, TEXT("mouth_run_body"), [&]()
		{
			bAutoForward = bAutoRun = bAutoRamp = false;
			if (++AutoSpecies < M->GetSpecies().Num()) { Next(280); } else { Next(9); }
		});
		break;

	// Holding Shift (-bfsprint; -bfonly works): every animal that can walk, with the real keys, read by the same input code
	// as a player's. On the flat: Shift alone does nothing; W and Shift together, how long to each quarter of the way from
	// its walk to its top speed; let go, how long back to a walk; W let go at top speed, how long and how far to stop. From
	// the side for pictures, half way and at the top. In the deep end, its paddling and holding Shift. A flier: cruising and
	// holding Shift. A glider: two leaps off the cliff, without Shift and with it.
	case 230:
		if ((AutoOnly.Num() && !AutoOnly.Contains(SprintKey)) || M->GetSpecies()[AutoSpecies].Legs != EBFLegs::Walks)
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; bAutoForward = false; bAutoKeys = true; Pick(AutoSpecies); Next(231); }
		break;
	case 231:   // 300 m of open ground ahead of it (the far side of the field from the pool)
		if (AutoClock > 1.f && D)
		{
			D->SetActorLocation(FVector(-3000.f, -10000.f, D->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(-15.f, 90.f, 0.f));
			PressKey(EKeys::LeftShift, true);
			Next(232);
		}
		break;
	case 232:
		if (AutoClock > 1.f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: Shift alone, standing: %.2f m/s, built %.0f%%; it takes %.1f s to top speed"), *SprintKey, D->GetSpeedMs(), 100.f * D->GetSprint(), D->GetSprintSeconds());
			PressKey(EKeys::W, true);
			Next(233);
		}
		break;
	case 233:
		if (AutoClock > 0.6f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: 0.6 s after pressing W with Shift already held: %.2f m/s, built %.0f%%"), *SprintKey, D->GetSpeedMs(), 100.f * D->GetSprint());
			PressKey(EKeys::LeftShift, false);
			Next(234);
		}
		break;
	case 234:
		if (AutoClock > 4.f && D)
		{
			const FBFSpecies& S = D->GetSpecies();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: W only: %s %.2f m/s, built %.0f%%"), *SprintKey, *D->GetGaitName(), D->GetSpeedMs(), 100.f * D->GetSprint());
			PressKey(EKeys::LeftShift, true);
			MarksFrom(D->GetSpeedMs(), S.Fast.SpeedCm / 100.f);
			Next(235);
		}
		break;
	case 235:
		Marks();
		if (D && (AutoClock > D->GetSprintSeconds() + 1.2f || D->GetActorLocation().Y > 18000.f))
		{
			const FBFSpecies& S = D->GetSpecies();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: W + Shift from a walk: top %.2f m/s (fast gait %.2f, %s), now %s; passed%s"), *SprintKey, SprintTop, S.Fast.SpeedCm / 100.f, *S.Fast.Name, *D->GetGaitName(), *SprintTimes);
			PressKey(EKeys::LeftShift, false);
			MarksFrom(SprintTop, S.Walk.SpeedCm / 100.f);
			Next(236);
		}
		break;
	case 236:
		Marks();
		if (D && (SprintMark >= 4 || AutoClock > 12.f))
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: Shift let go, W still held: back at %.2f m/s; passed%s"), *SprintKey, D->GetSpeedMs(), *SprintTimes);
			PressKey(EKeys::LeftShift, true);
			Next(237);
		}
		break;
	case 237:
		if (D && (AutoClock > D->GetSprintSeconds() + 0.5f || D->GetActorLocation().Y > 18000.f))
		{
			SetControlRotation(FRotator(-15.f, 0.f, 0.f));     // at top speed, the view swung a quarter turn to the right
			WatchStart = D->GetActorLocation();
			WatchPeak = 0.f;
			WatchAir = -1.f;
			SprintTop = D->GetSpeedMs();
			Next(239);
		}
		break;
	case 239:   // how far its body and its path disagree on the way round
		if (D)
		{
			const FVector V = D->GetVelocity();
			const float Body = D->GetActorRotation().Yaw;
			WatchPeak = FMath::Max(WatchPeak, FMath::Abs(FMath::FindDeltaAngleDegrees(Body, V.Rotation().Yaw)));
			if (WatchAir < 0.f && FMath::Abs(FRotator::NormalizeAxis(V.Rotation().Yaw)) < 10.f && FMath::Abs(FRotator::NormalizeAxis(Body)) < 10.f)
			{
				WatchAir = AutoClock;
			}
			if (AutoClock > 4.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: a quarter turn at %.2f m/s: body and path up to %.0f deg apart, round in %.1f s, %.1f m from where it started turning, now %.2f m/s"), *SprintKey,
					SprintTop, WatchPeak, WatchAir, FVector::Dist2D(WatchStart, D->GetActorLocation()) / 100.f, D->GetSpeedMs());
				WatchStart = D->GetActorLocation();
				MarksFrom(D->GetSpeedMs(), 0.f);
				PressKey(EKeys::W, false);
				Next(238);
			}
		}
		break;
	case 238:
		if (D && (D->GetSpeedMs() < 0.02f || AutoClock > 15.f))
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: W let go at %.2f m/s: stopped in %.1f s and %.1f m"), *SprintKey, SprintFrom, AutoClock, FVector::Dist2D(WatchStart, D->GetActorLocation()) / 100.f);
			PressKey(EKeys::LeftShift, false);
			bAutoKeys = false;
			Area = EBFSpawnArea::Flat;
			bAutoForward = false;
			Pick(AutoSpecies);
			Next(240);
		}
		break;
	case 240:   // pictures, side on (driven by the script so the camera can look from the side)
		if (AutoClock > 1.f && D)
		{
			D->SetActorLocation(FVector(-3000.f, -10000.f, D->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			AutoDir = FVector(0.f, 1.f, 0.f);
			bAutoForward = true;
			bAutoRun = false;
			bAutoRamp = true;
			Next(241);
		}
		break;
	case 241:
		if (AutoClock > 3.f) { Look(90.f, -8.f); bAutoRun = true; Next(242); }
		break;
	case 242:
		if (SprintU() >= 0.5f || AutoClock > 12.f) { Next(243); }
		break;
	case 243:
		ShotThen(0.f, TEXT("sprint_half"), [&]() { Next(244); });
		break;
	case 244:
		if (SprintU() >= 0.97f || AutoClock > 12.f) { Next(245); }
		break;
	case 245:
		ShotThen(0.2f, TEXT("sprint_top"), [&]() { Next(247); });
		break;
	case 247:   // two more, a little further on in the stride
		ShotThen(0.f, TEXT("sprint_top_b"), [&]() { Next(248); });
		break;
	case 248:
		ShotThen(0.f, TEXT("sprint_top_c"), [&]() { Look(25.f, -12.f); Next(246); });
		break;
	case 246:
		ShotThen(0.4f, TEXT("sprint_top_34"), [&]()
		{
			bAutoForward = bAutoRun = bAutoRamp = false;
			Next(M->GetSpecies()[AutoSpecies].Swim == EBFSwim::Sinks ? 260 : 250);
		});
		break;
	case 250:
		if (AutoClock > 0.3f) { Area = EBFSpawnArea::Pool; bAutoKeys = true; Pick(AutoSpecies); Next(251); }
		break;
	case 251:   // into the deep end, heading for the far wall
		if (AutoClock > 1.f && D)
		{
			const float HH = D->GetSpecies().Bounds.GetSize().Z * 0.5f;
			D->SetActorLocation(FVector(-2000.f, 0.5f * (ABFGameMode::PoolMinY + ABFGameMode::PoolMaxY), ABFGameMode::WaterZ + HH + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(-15.f, 0.f, 0.f));
			Next(252);
		}
		break;
	case 252:
		if (AutoClock > 7.f && D)
		{
			if (D->IsSwimming()) { PressKey(EKeys::W, true); Next(253); } else { Next(260); }
		}
		break;
	case 253:
		if (AutoClock > 4.f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: paddling (W): %.2f m/s"), *SprintKey, D->GetSpeedMs());
			PressKey(EKeys::LeftShift, true);
			MarksFrom(D->GetSpeedMs(), 1.6f * D->GetSpeedMs());
			Next(254);
		}
		break;
	case 254:
		Marks();
		if (D && AutoClock > D->GetSprintSeconds() + 1.5f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: paddling (W + Shift): top %.2f m/s, %s; passed%s"), *SprintKey, SprintTop, *D->GetGaitName(), *SprintTimes);
			PressKey(EKeys::W, false);
			PressKey(EKeys::LeftShift, false);
			Next(260);
		}
		break;
	case 260:
	{
		const EBFFlight F = M->GetSpecies()[AutoSpecies].Flight;
		if (F == EBFFlight::Flies && M->GetSpecies()[AutoSpecies].CanFly() && AutoClock > 0.3f)
		{
			Area = EBFSpawnArea::Flat; bAutoKeys = true; Pick(AutoSpecies); Next(261);
		}
		else if (F == EBFFlight::Glides && M->GetSpecies()[AutoSpecies].CanFly() && AutoClock > 0.3f)
		{
			SprintGlideRun = 0; Next(270);
		}
		else if (F != EBFFlight::Flies && F != EBFFlight::Glides)
		{
			SprintNextAnimal();
		}
		break;
	}
	case 261:   // up, with Space and W
		if (AutoClock > 1.f && D)
		{
			D->SetActorLocation(FVector(-3000.f, -10000.f, D->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(0.f, 90.f, 0.f));
			PressKey(EKeys::SpaceBar, true);
			PressKey(EKeys::W, true);
			Next(262);
		}
		break;
	case 262:
		if (AutoClock > 3.f) { PressKey(EKeys::SpaceBar, false); Next(263); }
		break;
	case 263:   // level off, and cruise on W alone
		if (AutoClock > 3.f) { SetControlRotation(FRotator(-15.f, 90.f, 0.f)); Next(264); }
		break;
	case 264:
		if (AutoClock > 7.f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: flying (W): %s %.2f m/s at %.0f m"), *SprintKey, *D->GetGaitName(), D->GetSpeedMs(), D->GetAltitudeM());
			PressKey(EKeys::LeftShift, true);
			MarksFrom(D->GetSpeedMs(), 1.6f * D->GetSpeedMs());
			Next(265);
		}
		break;
	case 265:
		Marks();
		if (D && AutoClock > D->GetSprintSeconds() + 5.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: flying (W + Shift): top %.2f m/s, %s, at %.0f m; passed%s"), *SprintKey, SprintTop, *D->GetGaitName(), D->GetAltitudeM(), *SprintTimes);
			FlightAim = FRotator(0.f, D->GetActorRotation().Yaw, 0.f);
			PressKey(EKeys::W, false);
			PressKey(EKeys::LeftShift, false);
			bAutoKeys = false;      // (the camera can look from the side now: the script steers)
			bAutoRamp = true;
			Look(90.f, -5.f);
			Next(266);
		}
		break;
	case 266:
		if (D) { D->SetFlightInput(1.f, false, FlightAim); D->SetRunning(true); }
		ShotThen(0.8f, TEXT("sprint_fly_side"), [&]() { Look(25.f, -15.f); Next(267); });
		break;
	case 267:
		if (D) { D->SetFlightInput(1.f, false, FlightAim); D->SetRunning(true); }
		ShotThen(0.5f, TEXT("sprint_fly_34"), [&]() { Look(90.f, -5.f); bAutoKeys = true; bAutoRamp = false; MarksFrom(D ? D->GetSpeedMs() : 0.f, 14.f); PressKey(EKeys::W, true); Next(268); });
		break;
	case 268:   // Shift let go, still flying on W
		Marks();
		if (D && AutoClock > 8.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: flying, Shift let go after top speed: %.2f m/s 8 s later; passed%s"), *SprintKey, D->GetSpeedMs(), *SprintTimes);
			PressKey(EKeys::W, false);
			SprintNextAnimal();
		}
		break;
	case 270:   // a running leap off the cliff, without Shift and then with it held
		if (AutoClock > 0.3f) { Area = EBFSpawnArea::Cliff; bAutoKeys = true; Pick(AutoSpecies); Next(271); }
		break;
	case 271:
		if (AutoClock > 1.f)
		{
			SetControlRotation(FRotator(-15.f, 0.f, 0.f));
			PressKey(EKeys::W, true);
			PressKey(EKeys::LeftShift, true);
			Next(272);
		}
		break;
	case 272:
		if (D && ((AutoClock > 0.5f && D->GetCharacterMovement()->IsFalling()) || AutoClock > 30.f))
		{
			StartWatch();
			MarksFrom(0.f, 1.f);
			PressKey(EKeys::SpaceBar, true);
			if (SprintGlideRun == 0) { PressKey(EKeys::LeftShift, false); }
			Next(273);
		}
		break;
	case 273:
		Watch();
		Marks();
		if (AutoClock > 0.2f && AutoClock - DeltaTime <= 0.2f) { PressKey(EKeys::SpaceBar, false); }
		if (D && AutoClock > 0.5f && (!D->IsFlying() || AutoClock > 40.f))
		{
			const FVector Now = D->GetActorLocation();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] sprint %s: leap off the cliff %s: %.1f s in the air, top %.2f m/s, %.1f m covered"), *SprintKey,
				SprintGlideRun == 0 ? TEXT("(W only)") : TEXT("(W + Shift)"), WatchAir, SprintTop, FVector::Dist2D(Now, WatchStart) / 100.f);
			PressKey(EKeys::W, false);
			PressKey(EKeys::LeftShift, false);
			if (++SprintGlideRun < 2) { Next(270); } else { SprintNextAnimal(); }
		}
		break;
	case 0:   // warm up: shaders, sky capture; the first time round, a shot of the picker
		if (AutoSpecies == 0 && AutoClock > 2.f && AutoClock < 2.5f && !bPickerOpen) { ShowPicker(true); }
		if (AutoSpecies == 0 && AutoClock > 3.2f && AutoClock - DeltaTime <= 3.2f) { Shot(TEXT("picker")); }
		if (AutoClock > 4.f) { ShowPicker(false); Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = true; bAutoRun = false; Next(1); }
		break;
	case 1:   // walk on the flat
		if (AutoClock > 3.f) { Look(95.f, -12.f); Next(2); }
		break;
	case 2:
		ShotThen(1.2f, TEXT("flat_side"), [&]() { Next(3); });
		break;
	case 3:
		if (AutoClock > 0.3f) { Look(150.f, -20.f); Next(4); }
		break;
	case 4:
		ShotThen(1.2f, TEXT("flat_front"), [&]() { bAutoForward = false; if (D) { D->FocusHead(true); } Look(150.f, -15.f); Next(11); });
		break;
	case 11:   // close-up of the head from the front left
		ShotThen(1.5f, TEXT("head"), [&]() { if (D) { D->SetMouthOpen(true); } Look(125.f, -8.f); Next(13); });
		break;
	case 13:   // the same head with the mouth open
		ShotThen(1.5f, TEXT("mouth_open"), [&]() { if (D) { D->SetMouthOpen(false); } Look(180.f, -55.f); Next(12); });
		break;
	case 12:   // looking down on the skull from in front and above
		ShotThen(1.2f, TEXT("skull_top"), [&]() { Area = EBFSpawnArea::Slopes; Ramp = AutoSpecies % ABFGameMode::NumRamps; Pick(AutoSpecies); bAutoForward = true; bAutoRun = true; Next(5); });
		break;
	case 5:   // run until well onto the ramp
		if ((D && D->GetGroundSlopeDeg() > 5.f && AutoClock > 2.f) || AutoClock > 30.f) { Next(6); }
		break;
	case 6:
		if (AutoClock > 1.f) { bAutoRun = false; Look(90.f, -25.f); Next(7); }
		break;
	case 7:
		ShotThen(1.2f, TEXT("slope_side"), [&]() { Look(150.f, -35.f); Next(8); });
		break;
	case 8:
		ShotThen(1.2f, TEXT("slope_front"), [&]()
		{
			bAutoForward = false;
			if (D && D->CanFly()) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); Next(20); }
			else if (++AutoSpecies < M->NumBaseSpecies()) { Next(0); AutoClock = 3.5f; } else { Next(9); }
		});
		break;
	// Flight (Pteranodon): take off, climb away flapping, glide, bank round, come down and land.
	case 20:
		if (AutoClock > 1.f && D) { D->TakeOff(); FlightAim = FRotator(12.f, D->GetActorRotation().Yaw, 0.f); Next(21); }
		break;
	case 21:
		if (D) { D->SetFlightInput(1.f, false, FlightAim); }
		if (AutoClock > 4.f) { Look(95.f, -8.f); Next(22); }
		break;
	case 22:
		if (D) { D->SetFlightInput(1.f, false, FlightAim); }
		ShotThen(1.0f, TEXT("fly_side"), [&]() { Look(0.f, -35.f); Next(23); });
		break;
	case 23:   // from behind and above, wings spread
		if (D) { D->SetFlightInput(0.f, false, FRotator(-4.f, FlightAim.Yaw, 0.f)); }
		ShotThen(1.0f, TEXT("fly_above"), [&]() { Look(200.f, 10.f); Next(24); });
		break;
	case 24:   // from in front and below
		if (D) { D->SetFlightInput(0.f, false, FRotator(-4.f, FlightAim.Yaw, 0.f)); }
		ShotThen(1.0f, TEXT("fly_below"), [&]() { Next(25); });
		break;
	case 25:   // banked turn
		FlightAim.Yaw += 40.f * DeltaTime;
		if (D) { D->SetFlightInput(0.5f, false, FRotator(0.f, FlightAim.Yaw, 0.f)); }
		if (AutoClock > 1.5f) { Look(-20.f, -20.f); }
		ShotThen(2.2f, TEXT("fly_bank"), [&]() { Next(26); });
		break;
	case 26:   // glide down and land
		if (D) { D->SetFlightInput(-0.3f, false, FRotator(-22.f, FlightAim.Yaw, 0.f)); Look(90.f, -12.f); }
		if (D && (!D->IsFlying() || AutoClock > 25.f)) { Next(27); }
		break;
	case 27:
		ShotThen(1.5f, TEXT("landed"), [&]() { if (++AutoSpecies < M->NumBaseSpecies()) { Next(0); AutoClock = 3.5f; } else { Next(9); } });
		break;
	// Ragdoll check (-bfragdoll): each species goes limp at a run (the Pteranodon in flight), settles, and is
	// measured against its limits.
	case 30:
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = true; bAutoRun = true; Next(31); }
		break;
	case 31:
		if (AutoClock > 1.f && D)
		{
			if (D->CanFly()) { bAutoForward = false; D->TakeOff(); FlightAim = FRotator(12.f, D->GetActorRotation().Yaw, 0.f); Next(33); }
			else { Next(34); }
		}
		break;
	case 33:   // climb for a few seconds first
		if (D) { D->SetFlightInput(1.f, false, FlightAim); }
		if (AutoClock > 4.f) { Next(34); }
		break;
	case 34:
		if (D && (D->IsFlying() || AutoClock > 2.f))
		{
			bAutoForward = false;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] ragdoll %s starts at %.1f m/s, %s, altitude %.1f m"), *M->GetSpecies()[AutoSpecies].Key,
				D->GetSpeedMs(), *D->GetGaitName(), D->GetAltitudeM());
			D->StartRagdoll();
			Look(100.f, -25.f);
			Next(32);
		}
		break;
	case 32:
		ShotThen(5.f, TEXT("ragdoll"), [&]()
		{
			int32 Outside = 0;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] ragdoll %s: %s"), *M->GetSpecies()[AutoSpecies].Key, D ? *D->RagdollReport(Outside) : TEXT("no dino"));
			if (++AutoSpecies < M->NumBaseSpecies()) { Next(30); } else { Next(9); }
		});
		break;
	// Baked clips (-bfclips): each species walks, runs and bites playing the clips; the Pteranodon also flaps and glides.
	case 40:
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; bWantClips = true; Pick(AutoSpecies); bAutoForward = true; bAutoRun = false; Next(41); }
		break;
	case 41:
		if (AutoClock > 2.5f) { Look(95.f, -12.f); Next(42); }
		break;
	case 42:
		ShotThen(1.0f, TEXT("clip_walk_side"), [&]() { bAutoRun = true; Next(43); });
		break;
	case 43:
		if (AutoClock > 2.f) { Look(150.f, -20.f); Next(44); }
		break;
	case 44:
		ShotThen(1.0f, TEXT("clip_run_front"), [&]() { bAutoForward = false; Next(45); });
		break;
	case 45:
		if (AutoClock > 2.f && D) { D->Bite(); Look(110.f, -10.f); Next(46); }
		break;
	case 46:
		ShotThen(0.4f, TEXT("clip_bite"), [&]()
		{
			if (D && D->CanFly()) { D->TakeOff(); FlightAim = FRotator(12.f, D->GetActorRotation().Yaw, 0.f); Next(47); }
			else if (++AutoSpecies < M->NumBaseSpecies()) { Next(40); } else { bWantClips = false; Next(9); }
		});
		break;
	case 47:
		if (D) { D->SetFlightInput(1.f, false, FlightAim); }
		if (AutoClock > 3.f) { Look(95.f, -8.f); Next(48); }
		break;
	case 48:
		if (D) { D->SetFlightInput(1.f, false, FlightAim); }
		ShotThen(1.0f, TEXT("clip_flap"), [&]() { Look(0.f, -35.f); Next(49); });
		break;
	case 49:
		if (D) { D->SetFlightInput(0.f, false, FRotator(-4.f, FlightAim.Yaw, 0.f)); }
		ShotThen(1.5f, TEXT("clip_glide"), [&]() { if (++AutoSpecies < M->NumBaseSpecies()) { Next(40); } else { bWantClips = false; Next(9); } });
		break;
	// Balance check (-bfstance, Prototype 2): each species stands on the flat (side and above), walks, then stands on
	// the 30° ramp, with the centre of mass and support area drawn and logged at every shot.
	case 50:
		if (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > 4.f) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = false; Next(51); }
		break;
	case 51:
		if (AutoClock > 2.5f) { Look(90.f, -10.f); Next(52); }
		break;
	case 52:
		ShotThen(1.0f, TEXT("stance_side"), [&]() { Look(150.f, -50.f); Next(53); });
		break;
	case 53:
		ShotThen(1.0f, TEXT("stance_above"), [&]() { bAutoForward = true; bAutoRun = false; Next(54); });
		break;
	case 54:
		if (AutoClock > 3.f) { Look(90.f, -10.f); Next(55); }
		break;
	case 55:
		ShotThen(1.0f, TEXT("stance_walk"), [&]() { bAutoForward = false; Area = EBFSpawnArea::Slopes; Ramp = 2; Pick(AutoSpecies); bAutoForward = true; Next(56); });
		break;
	case 56:   // walk until the hips are halfway up the 30° ramp, then stand still on it
		if ((D && D->GetActorLocation().X > ABFGameMode::RampMiddleX(Ramp)) || AutoClock > 40.f) { bAutoForward = false; Next(59); }
		break;
	case 59:   // a side look as it comes to a stop, before one that can't stand there tips over (a second later)
		Look(90.f, -10.f);
		ShotThen(0.4f, TEXT("stance_stop"), [&]() { Next(57); });
		break;
	case 57:
		if (AutoClock > 1.8f) { Look(90.f, -10.f); Next(58); }
		break;
	case 58:
		ShotThen(1.0f, TEXT("stance_slope"), [&]() { if (++AutoSpecies < M->GetSpecies().Num()) { Next(50); } else { Next(9); } });
		break;
	// Slope sweep (-bfslopes; -bfonly works): each animal walks halfway up each ramp (10, 20, 30, 40°), stops and stands
	// 3.5 s. The log says whether it stood (its balance line), tipped over, or never got that far, beside the climb
	// check's limit, which the game makes its walkable slope (so the climb check's balance limit can be held to this).
	case 147:
		if (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > 1.f) { Area = EBFSpawnArea::Slopes; Ramp = SweepRamp; Pick(AutoSpecies); bAutoForward = true; bAutoRun = false; Next(148); }
		break;
	case 148:
		if ((D && (D->GetActorLocation().X > ABFGameMode::RampMiddleX(Ramp) || D->IsRagdoll())) || AutoClock > 30.f) { bAutoForward = false; Next(149); }
		break;
	case 149:
		if (AutoClock > 3.5f)
		{
			const FBFSpecies& S = M->GetSpecies()[AutoSpecies];
			const bool bThere = D && D->GetActorLocation().X > ABFGameMode::RampMiddleX(Ramp) - 300.f;
			const FString What = !D ? FString(TEXT("no animal")) : S.Legs == EBFLegs::Buckles ? FString(TEXT("its legs buckle"))
				: D->IsRagdoll() ? FString(TEXT("TIPPED OVER")) : !bThere ? FString(TEXT("didn't get halfway up"))
				: TEXT("stood: ") + D->GetBalanceLine();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slopes %s %.0f deg: %s | climb check %.1f deg | %s"), *S.Key,
				ABFGameMode::RampAngle(Ramp), *What, S.ClimbMaxDeg, D ? *D->GetTailNote() : TEXT(""));
			Look(90.f, -5.f);      // then where it stopped, from the side and at its feet
			Next(D && !D->IsRagdoll() && S.Legs != EBFLegs::Buckles ? 150 : 152);
		}
		break;
	case 150:
		ShotThen(0.5f, *FString::Printf(TEXT("slopes_%.0f_side"), ABFGameMode::RampAngle(Ramp)), [&]() { if (D) { D->LookAtFeet(); } Look(70.f, -5.f); Next(151); });
		break;
	case 151:
		ShotThen(0.6f, *FString::Printf(TEXT("slopes_%.0f_feet"), ABFGameMode::RampAngle(Ramp)), [&]() { Next(152); });
		break;
	case 152:
		if (++SweepRamp < ABFGameMode::NumRamps) { Next(147); }
		else
		{
			SweepRamp = 0;
			if (++AutoSpecies < M->GetSpecies().Num()) { Next(147); } else { Next(9); }
		}
		break;
	// Tails on the ground (-bftails; -bfonly works): each animal walks up the steepest ramp it climbs, stops halfway, turns
	// and walks back down. From the side as its hips reach the ramp's foot going up (the body tilted up the ramp, the tail
	// still over the flat), standing halfway up, and with its hips at the foot coming down (the tail over the slope behind
	// it). The log has how far its skin would have gone into the ground as posed, and how far above it it is now.
	case 200:
		if ((AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key)) || M->GetSpecies()[AutoSpecies].Legs == EBFLegs::Buckles)
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > 1.f) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = false; Next(201); }   // (it tests itself on slopes)
		break;
	case 201:
		if (AutoClock > 1.f)
		{
			const FBFSpecies& S = M->GetSpecies()[AutoSpecies];
			Ramp = 0;
			for (int32 r = 0; r < ABFGameMode::NumRamps; ++r)
			{
				if (ABFGameMode::RampAngle(r) <= S.ClimbMaxDeg) { Ramp = r; }
			}
			Area = EBFSpawnArea::Slopes; Pick(AutoSpecies); AutoDir = FVector::ForwardVector; bAutoForward = true; bAutoRun = false; Next(202);
		}
		break;
	case 202:
		if (AutoClock > 0.5f && D) { D->ResetTailWorst(); Next(203); }
		break;
	case 203:
		if ((D && D->GetActorLocation().X > ABFGameMode::RampFootX()) || AutoClock > 30.f) { Look(90.f, -5.f); Next(204); }
		break;
	case 204:
		ShotThen(0.15f, TEXT("tails_up_foot"), [&]() { TailLog(TEXT("going up, its hips at the foot")); TailClose(true); Next(210); });
		break;
	case 210:   // and close on the tail from behind and to one side, low
		ShotThen(0.2f, TEXT("tails_up_close"), [&]() { TailClose(false); Next(205); });
		break;
	case 205:
		if ((D && (D->GetActorLocation().X > ABFGameMode::RampMiddleX(Ramp) || D->IsRagdoll())) || AutoClock > 30.f) { bAutoForward = false; Next(206); }
		break;
	case 206:
		if (AutoClock > 2.5f) { Look(90.f, -5.f); Next(207); }
		break;
	case 207:
		ShotThen(0.5f, TEXT("tails_up_middle"), [&]()
		{
			TailLog(TEXT("standing where it stopped"));
			AutoDir = -FVector::ForwardVector;
			bAutoForward = true;
			Next(208);
		});
		break;
	case 208:   // turned round and walking down: its hips just above the ramp's foot
		if ((D && D->GetActorForwardVector().X < -0.9f && D->GetActorLocation().X < ABFGameMode::RampFootX() + 0.05f * D->BodyLengthCm()) || AutoClock > 40.f)
		{
			Look(90.f, -5.f);
			Next(209);
		}
		break;
	case 209:
		ShotThen(0.15f, TEXT("tails_down_foot"), [&]() { TailLog(TEXT("coming down, its hips at the foot")); bAutoForward = false; TailClose(true, 150.f); Next(211); });
		break;
	case 211:
		ShotThen(0.2f, TEXT("tails_down_close"), [&]()
		{
			TailClose(false);
			if (D)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] tails %s worst on the way: %s"), *D->GetSpecies().Key, *D->GetTailNote(true));
			}
			bAutoForward = false;
			AutoDir = FVector::ZeroVector;
			if (++AutoSpecies < M->GetSpecies().Num()) { Next(200); } else { Next(9); }
		});
		break;
	// Wings (-bfsplice, Prototype 3): each species with wings (the Pteranodon and the splices) stands for a look,
	// tries a running take-off on the flat, flapping as hard as it can, then runs off the 20 m cliff and glides down.
	// The flight check and each attempt (time off the ground, height gained, distance, glide ratio) go to the log.
	case 60:
		if (!M->GetSpecies()[AutoSpecies].CanFly() || (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key)))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > 4.f) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = false; Next(61); }
		break;
	case 61:
		if (AutoClock > 2.5f)
		{
			const FBFSpecies& S = M->GetSpecies()[AutoSpecies];
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] flight check %s: %s%s%s"), *S.Key, *S.FlightSummary,
				S.IsSplice() ? TEXT("  Lineage: ") : TEXT(""), *S.Lineage);
			Look(100.f, -12.f);
			Next(62);
		}
		break;
	case 62:
		ShotThen(1.0f, TEXT("wings_stand"), [&]() { Look(150.f, -25.f); Next(63); });
		break;
	case 63:
		ShotThen(1.0f, TEXT("wings_34"), [&]() { bAutoForward = true; bAutoRun = true; Next(64); });
		break;
	case 64:   // a running take-off on the flat
		if (AutoClock > 2.5f && D)
		{
			bAutoForward = false;
			StartWatch();
			D->TakeOff();
			FlightAim = FRotator(D->GetSpecies().Flight == EBFFlight::Flies ? 12.f : -8.f, D->GetActorRotation().Yaw, 0.f);
			Look(95.f, -8.f);
			Next(65);
		}
		break;
	case 65:
		if (D) { D->SetFlightInput(1.f, true, FlightAim); }
		Watch();
		ShotThen(0.6f, TEXT("wings_takeoff"), [&]() { Look(20.f, -45.f); Next(72); });
		break;
	case 72:   // the same attempt from behind and above: the wings spread from their roots
		if (D) { D->SetFlightInput(1.f, true, FlightAim); }
		Watch();
		ShotThen(0.5f, TEXT("wings_takeoff_above"), [&]() { Next(66); });
		break;
	case 66:
		if (D) { D->SetFlightInput(1.f, true, FlightAim); }
		Watch();
		if (AutoClock > 1.0f && AutoClock - DeltaTime <= 1.0f) { Shot(TEXT("wings_late")); }    // later in the attempt (a failed one tiring)
		if (D && (!D->IsFlying() || AutoClock > 6.f))
		{
			LogFlight(TEXT("running take-off on the flat"));
			Area = EBFSpawnArea::Cliff;
			Pick(AutoSpecies);
			bAutoForward = true;
			bAutoRun = true;
			Next(67);
		}
		break;
	case 67:   // run for the cliff edge, and take off as the ground falls away
		if (D && ((AutoClock > 0.5f && D->GetCharacterMovement()->IsFalling()) || AutoClock > 40.f))
		{
			bAutoForward = false;
			StartWatch();
			D->TakeOff();
			FlightAim = FRotator(-10.f, D->GetActorRotation().Yaw, 0.f);
			Look(95.f, -5.f);
			Next(68);
		}
		break;
	case 68:
		if (D) { D->SetFlightInput(0.f, false, FlightAim); }
		Watch();
		ShotThen(1.5f, TEXT("wings_cliff"), [&]() { Look(20.f, -40.f); Next(71); });
		break;
	case 71:   // from behind and above: the wings spread (or, for a failed take-off, already on the ground)
		if (D) { D->SetFlightInput(0.f, false, FlightAim); }
		Watch();
		ShotThen(1.0f, TEXT("wings_above"), [&]() { Next(69); });
		break;
	case 69:
		if (D) { D->SetFlightInput(0.f, false, FlightAim); }
		Watch();
		if (D && (!D->IsFlying() || AutoClock > 40.f)) { LogFlight(TEXT("off the 20 m cliff")); Look(120.f, -15.f); Next(70); }
		break;
	case 70:
		ShotThen(1.5f, TEXT("wings_landed"), [&]() { if (++AutoSpecies < M->GetSpecies().Num()) { Next(60); } else { Next(9); } });
		break;
	// Turning (-bfturn): each species walks ahead, then is steered 90 degrees to its right; seen from above and
	// behind as the head leads, the body follows and the tail swings out, with the turn logged at each shot.
	case 80:
		if (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key))
		{
			if (++AutoSpecies >= M->NumBaseSpecies()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); AutoDir = FVector::ForwardVector; bAutoForward = true; bAutoRun = false; Next(81); }
		break;
	case 81:   // walk straight until settled, camera above and behind
		if (AutoClock > 3.5f) { Look(0.f, -55.f); Next(82); }
		break;
	case 82:
		if (AutoClock > 1.f) { AutoDir = FVector::RightVector; Next(83); }
		break;
	case 83:   // the head swings round first
		ShotThen(0.35f, TEXT("turn_head"), [&]() { Next(84); });
		if (bShotTaken && D && AutoClock - DeltaTime <= 0.35f) { UE_LOG(LogTemp, Display, TEXT("[BoneForge] turn %s head: %s"), *M->GetSpecies()[AutoSpecies].Key, *D->GetTurnNote()); }
		break;
	case 84:   // the body following, the tail out behind
		ShotThen(0.5f, TEXT("turn_body"), [&]() { Next(85); });
		if (bShotTaken && D && AutoClock - DeltaTime <= 0.5f) { UE_LOG(LogTemp, Display, TEXT("[BoneForge] turn %s body: %s"), *M->GetSpecies()[AutoSpecies].Key, *D->GetTurnNote()); }
		break;
	case 85:   // lined up on the new heading
		ShotThen(2.5f, TEXT("turn_done"), [&]()
		{
			if (D) { UE_LOG(LogTemp, Display, TEXT("[BoneForge] turn %s done: %s"), *M->GetSpecies()[AutoSpecies].Key, *D->GetTurnNote()); }
			bAutoForward = false;
			AutoDir = FVector::ZeroVector;
			if (++AutoSpecies < M->NumBaseSpecies()) { Next(80); } else { Next(9); }
		});
		break;
	// The test run (StartTestRun): stand, walk, run, climb, swim, glide, fly; each result in plain words (AddResult).
	case 158:      // -bftestone: the creature the game opened with
		if (AutoClock > 4.f) { StartTestRun(Current, FParse::Param(FCommandLine::Get(), TEXT("bftestit"))); }   // (the scripted check then closes)
		break;
	case 159:      // -bftest: every animal in the -bfonly list (default the six), each then its report card's picture
		if (M->GetSpecies()[AutoSpecies].IsSplice() ? !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key) : (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key)))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { StartTestRun(AutoSpecies, true); }
		break;
	case 160:
		Area = EBFSpawnArea::Flat;
		Pick(AutoSpecies);
		bAutoForward = false;
		Next(161);
		break;
	case 161:      // stand: a legs check that buckles gives way here
		if (AutoClock > 3.f && D)
		{
			const FBFSpecies& S = D->GetSpecies();
			if (D->IsRagdoll())
			{
				AddResult(TEXT("Stand"), 0, TEXT("Its legs give way. ") + S.LegSummary);
				for (const TCHAR* T : { TEXT("Walk"), TEXT("Run"), TEXT("Climb") })
				{
					AddResult(T, 0, TEXT("Can't: its legs gave way."));
				}
				Next(166);       // the water still holds it up
				break;
			}
			const FBFBalance& B = D->GetBalance();
			AddResult(TEXT("Stand"), B.bValid && B.MarginCm >= 0.f ? 1 : 0, !B.bValid ? TEXT("Stands.")
				: B.MarginCm >= 0.f ? FString::Printf(TEXT("Stands balanced: its centre of mass sits %.0f cm inside its feet%s."), B.MarginCm, B.bStable ? TEXT("") : TEXT(", only just"))
				: FString::Printf(TEXT("Tips: its centre of mass is %.0f cm outside its feet, so it has to keep stepping to stay up."), -B.MarginCm));
			if (S.Legs == EBFLegs::Stands)
			{
				AddResult(TEXT("Walk"), 0, TEXT("Stands but can't walk. ") + S.LegSummary);
				AddResult(TEXT("Run"), 0, TEXT("Can't: it can't walk."));
				AddResult(TEXT("Climb"), 0, TEXT("Can't: it can't walk."));
				Next(166);
				break;
			}
			D->StartJump();          // jump: standing, as Space does
			Next(370);
		}
		break;
	case 370:
		if (D && ((AutoClock > 0.5f && !D->IsJumping()) || AutoClock > 6.f))
		{
			const FBFSpecies& S = D->GetSpecies();
			const float Rise = D->GetJumpRiseCm();
			AddResult(TEXT("Jump"), D->GetJumpLeft() && Rise >= 2.f ? 1 : 0, D->GetJumpLeft() && Rise >= 2.f
				? FString::Printf(TEXT("Jumps %.0f cm straight up: its legs push %.1f times its weight%s."), Rise, S.JumpPush,
					*Versus(S, [](const FBFSpecies& X) { return X.JumpCm; }, [](float V) { return V > 0.f ? FString::Printf(TEXT("%.0f cm"), V) : FString(TEXT("can't")); }))
				: S.JumpSummary);
			bAutoForward = true;
			bAutoRun = false;
			TestSum = 0.f;
			Next(162);
		}
		break;
	case 162:      // walk: its speed over the last two of five seconds
		if (D && AutoClock > 3.f) { TestSum += D->GetSpeedMs() * DeltaTime; }
		if (AutoClock > 5.f && D)
		{
			const float Speed = TestSum / 2.f;
			AddResult(TEXT("Walk"), Speed > 0.2f ? 1 : 0, Speed > 0.2f ? FString::Printf(TEXT("Walks at %.1f m/s."), Speed) : TEXT("Doesn't get going: it barely moves."));
			bAutoRun = true;
			bAutoRamp = true;        // its speed builds as when a player holds Shift
			TestMax = 0.f;
			TestT95 = -1.f;
			Next(163);
		}
		break;
	case 163:      // run: Shift held from its walk until it has been at its top speed a while; when it got there
		if (D)
		{
			TestMax = FMath::Max(TestMax, D->GetSpeedMs());
			if (TestT95 < 0.f && D->GetSpeedMs() >= 0.95f * D->GetSpecies().Fast.SpeedCm / 100.f)
			{
				TestT95 = AutoClock;
			}
		}
		if (D && AutoClock > D->GetSprintSeconds() + 2.5f)
		{
			const FBFSpecies& S = D->GetSpecies();
			AddResult(TEXT("Run"), TestMax > S.Walk.SpeedCm / 100.f * 1.2f ? 1 : 0, FString::Printf(TEXT("Its fastest gait (%s) reaches %.1f m/s (%.0f km/h)%s%s."),
				*S.Fast.Name, TestMax, TestMax * 3.6f,
				TestT95 >= 0.f ? *FString::Printf(TEXT(", nearly all of it %.1f s after Shift is held from a walk"), TestT95) : TEXT(""),
				*Versus(S, [](const FBFSpecies& X) { return X.Fast.SpeedCm / 100.f; }, [](float V) { return FString::Printf(TEXT("%.1f m/s"), V); })));
			// Stop: let go at that speed, how far and how long it takes to stand.
			TestTop = D->GetSpeedMs();
			TestPos = D->GetActorLocation();
			bAutoForward = false;
			bAutoRun = false;
			bAutoRamp = false;
			Next(371);
		}
		break;
	case 371:
		if (D && ((AutoClock > 0.3f && D->GetSpeedMs() < 0.1f) || AutoClock > 15.f))
		{
			const float Dist = FVector::Dist2D(D->GetActorLocation(), TestPos) / 100.f;
			AddResult(TEXT("Stop"), 1, FString::Printf(TEXT("From %.1f m/s it stops in %.1f s and %.1f m."), TestTop, AutoClock, Dist));
			// Turn: back on the flat, running the other way at its top speed, then turned a quarter.
			Area = EBFSpawnArea::Flat;
			Pick(AutoSpecies);
			AutoDir = FVector(-1.f, 0.f, 0.f);
			bAutoForward = true;
			bAutoRun = true;
			Next(372);
		}
		break;
	case 372:
		if (D && AutoClock > 3.f)
		{
			AutoDir = FVector(0.f, 1.f, 0.f);        // a quarter turn to its right
			TestTop = D->GetSpeedMs();
			TestPos = D->GetActorLocation();
			TestPath = 0.f;
			Next(373);
		}
		break;
	case 373:
		if (D)
		{
			TestPath += D->GetVelocity().Size2D() * DeltaTime;
			const float Heading = FMath::RadiansToDegrees(FMath::Atan2(D->GetVelocity().Y, D->GetVelocity().X));      // 180 going -X, 90 going +Y
			if (FMath::Abs(Heading - 90.f) < 5.f || AutoClock > 12.f)
			{
				// The path of a quarter circle is a quarter of its circumference: the circle is 4 x path / pi across.
				const bool bDone = FMath::Abs(Heading - 90.f) < 5.f;
				AddResult(TEXT("Turn"), bDone ? 1 : 0, bDone
					? FString::Printf(TEXT("At %.1f m/s it turns a quarter circle in %.1f s, a circle about %.0f m across."), TestTop, AutoClock, 4.f * TestPath / 100.f / PI)
					: TEXT("Couldn't finish a quarter turn in 12 s."));
				AutoDir = FVector::ZeroVector;
				bAutoForward = false;
				bAutoRun = false;
				TestRamp = ABFGameMode::NumRamps - 1;
				TestFailedRamp = -1;
				Next(164);
			}
		}
		break;
	case 164:      // climb: the steepest ramp first, then gentler ones until one works
		Area = EBFSpawnArea::Slopes;
		Ramp = TestRamp;
		Pick(AutoSpecies);
		bAutoForward = true;
		bAutoRun = true;
		TestStartZ = D ? D->GetActorLocation().Z : 0.f;
		TestMax = 0.f;
		TestGainClock = 0.f;
		Next(165);
		break;
	case 165:
		if (D)
		{
			const float Rise = D->GetActorLocation().Z - TestStartZ;
			if (Rise > TestMax + 20.f || AutoClock < 1.f)
			{
				TestMax = FMath::Max(TestMax, Rise);
				TestGainClock = AutoClock;
			}
			const bool bUp = TestMax > 800.f;       // most of the way up the 10 m rise
			// Stuck: no height gained for eight seconds once it's had time to reach the slope (a slow climber keeps
			// gaining, so it isn't cut short).
			const bool bStuck = AutoClock > 12.f && AutoClock - TestGainClock > 8.f;
			if (bUp || bStuck || AutoClock > 120.f || D->IsRagdoll())
			{
				const FBFSpecies& S = D->GetSpecies();
				const float Angle = ABFGameMode::RampAngle(TestRamp);
				const FString Why = S.ClimbWhy.IsEmpty() ? FString() : FString::Printf(TEXT(" Its limit is %.0f degrees: %s."), S.ClimbMaxDeg, *S.ClimbWhy);
				if (bUp)
				{
					const float Pace = S.ClimbPaceAt(Angle);
					const FString Slow = Pace < 0.97f ? FString::Printf(TEXT(", slowing to %.0f%% of its speed on the slope"), 100.f * Pace) : FString();
					// The failure named is the next slope up from the one it climbed.
					const FString Stall = TestSum < 0.5f ? FString(TEXT("it slid back at the foot of it")) : FString::Printf(TEXT("it stalled %.1f m up"), TestSum);
					AddResult(TEXT("Climb"), 1, TestFailedRamp < 0 ? FString::Printf(TEXT("Climbs the steepest slope, %.0f degrees, in %.0f s%s.%s"), Angle, AutoClock, *Slow, *Why)
						: FString::Printf(TEXT("Climbs %.0f degree slopes%s, but not %.0f: %s.%s"), Angle, *Slow, ABFGameMode::RampAngle(TestFailedRamp), *Stall, *Why));
				}
				else if (TestRamp > 0)
				{
					TestFailedRamp = TestRamp;
					TestSum = TestMax / 100.f;
					--TestRamp;
					Next(164);
					break;
				}
				else
				{
					AddResult(TEXT("Climb"), 0, FString::Printf(TEXT("Can't climb even the %.0f degree slope: it got %.1f m up.%s"), Angle, TestMax / 100.f, *Why));
				}
				bAutoForward = false;
				Next(166);
			}
		}
		break;
	case 166:      // swim: into the deep end of the pool
		Area = EBFSpawnArea::Pool;
		Pick(AutoSpecies);
		bAutoForward = false;
		Next(167);
		break;
	case 167:
		if (AutoClock > 0.2f && D)      // before legs that buckle give way on the shore
		{
			const float HH = D->GetSpecies().Bounds.GetSize().Z * 0.5f;
			D->SetActorLocation(FVector(-2000.f, 0.5f * (ABFGameMode::PoolMinY + ABFGameMode::PoolMaxY), ABFGameMode::WaterZ + HH + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			Next(168);
		}
		break;
	case 168:
		if (AutoClock > 7.f && D)
		{
			StartWatch();
			TestUnder = 0.f;
			bAutoForward = true;
			bAutoRun = false;
			Next(169);
		}
		break;
	case 169:      // six seconds paddling: how fast, and how long its nostrils are under
		if (D && D->IsHeadUnderWater()) { TestUnder += DeltaTime; }
		if (AutoClock > 6.f && D)
		{
			const FBFSpecies& S = D->GetSpecies();
			const float Speed = FVector::Dist2D(D->GetActorLocation(), WatchStart) / 100.f / AutoClock;
			const float Under = TestUnder / AutoClock;
			if (S.Swim == EBFSwim::Sinks || (!D->IsSwimming() && D->IsInWater()))
			{
				AddResult(TEXT("Swim"), 0, FString::Printf(TEXT("Sinks: at %s kg per cubic metre it is denser than water. It sank 12 m to the bottom and %s, its head under water."),
					*FText::AsNumber(FMath::RoundToInt(S.SwimDensity)).ToString(),
					Speed >= 0.1f ? *FString::Printf(TEXT("walked along it at %.1f m/s"), Speed) : TEXT("stayed there")));
			}
			else if (S.Swim == EBFSwim::HeadUnder || Under > 0.5f)
			{
				AddResult(TEXT("Swim"), 0, FString::Printf(TEXT("Floats and paddles at %.1f m/s, but its nostrils were under water %.0f%% of the time: it can't breathe swimming."), Speed, 100.f * Under));
			}
			else
			{
				AddResult(TEXT("Swim"), 1, FString::Printf(TEXT("Swims at %.1f m/s, its nostrils clear%s%s."), Speed,
					Under > 0.05f ? *FString::Printf(TEXT(" (they dipped under %.0f%% of the time)"), 100.f * Under) : TEXT(""),
					S.bSwimTippy ? TEXT("; it's tippy, top-heavy in the water, and paddles to stay upright") : TEXT("")));
			}
			bAutoForward = false;
			Next(170);
		}
		break;
	case 170:      // glide: off the 20 m cliff
		if (D && !D->GetSpecies().CanFly())
		{
			AddResult(TEXT("Glide"), -1, TEXT("No wings to glide on."));
			AddResult(TEXT("Fly"), -1, TEXT("No wings."));
			FinishTestRun();
			break;
		}
		if (D && D->GetSpecies().Legs == EBFLegs::Buckles)
		{
			AddResult(TEXT("Glide"), 0, TEXT("Can't: its legs give way, so it can't run or leap."));
			AddResult(TEXT("Fly"), 0, TEXT("Can't: its legs give way, so it can't take off."));
			FinishTestRun();
			break;
		}
		Area = EBFSpawnArea::Cliff;
		Pick(AutoSpecies);
		bAutoForward = true;
		bAutoRun = true;
		Next(171);
		break;
	case 171:
		if (D && ((AutoClock > 0.5f && D->GetCharacterMovement()->IsFalling()) || AutoClock > 40.f))
		{
			bAutoForward = false;
			StartWatch();
			D->TakeOff();
			FlightAim = FRotator(-10.f, D->GetActorRotation().Yaw, 0.f);
			Next(172);
		}
		break;
	case 172:
		if (D) { D->SetFlightInput(0.f, false, FlightAim); }
		Watch();
		if (D && (!D->IsFlying() || AutoClock > 40.f))
		{
			const float Dist = FVector::Dist2D(D->GetActorLocation(), WatchStart) / 100.f;
			const float Drop = FMath::Max((WatchStart.Z - D->GetActorLocation().Z) / 100.f, 0.1f);
			AddResult(TEXT("Glide"), Dist / Drop >= 2.f ? 1 : 0, Dist / Drop >= 2.f
				? FString::Printf(TEXT("Glides %.0f m from the 20 m cliff: %.1f m forward for every metre down."), Dist, Dist / Drop)
				: FString::Printf(TEXT("Can't glide: it drops %.0f m and covers only %.0f m. %s"), Drop, Dist, *D->GetSpecies().FlightSummary));
			Next(173);
		}
		break;
	case 173:      // fly: a running take-off on the flat
		Area = EBFSpawnArea::Flat;
		Pick(AutoSpecies);
		bAutoForward = true;
		bAutoRun = true;
		Next(174);
		break;
	case 174:
		if (AutoClock > 2.5f && D)
		{
			bAutoForward = false;
			StartWatch();
			D->TakeOff();
			FlightAim = FRotator(D->GetSpecies().Flight == EBFFlight::Flies ? 12.f : -8.f, D->GetActorRotation().Yaw, 0.f);
			Next(175);
		}
		break;
	case 175:
		if (D) { D->SetFlightInput(1.f, true, FlightAim); }
		Watch();
		if (D && (!D->IsFlying() || AutoClock > 8.f))
		{
			AddResult(TEXT("Fly"), WatchPeak > 500.f ? 1 : 0, WatchPeak > 500.f
				? FString::Printf(TEXT("Flies: it takes off from the flat and climbs %.0f m in %.0f s."), WatchPeak / 100.f, AutoClock)
				: FString::Printf(TEXT("Can't take off from the flat (%.1f m at most). %s"), WatchPeak / 100.f, *D->GetSpecies().FlightSummary));
			FinishTestRun();
		}
		break;
	case 176:      // -bftest: a picture of the card, then the next animal
		ShotThen(1.5f, TEXT("test_report"), [&]()
		{
			ShowReport(false);
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); } else { Next(159); }
		});
		break;
	// Life (-bfidle): every animal in the -bfonly list (default the six) on the flat: each idle behaviour at its height
	// from the side and three-quarters, then walking (the head against the hips' bob), running and stopping short (the
	// neck and tail swinging on), and its breathing at rest and winded; numbers to the log.
	case 180:
		if (AutoOnly.Num() ? !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key) : AutoSpecies >= M->NumBaseSpecies())
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (M->GetSpecies()[AutoSpecies].Legs != EBFLegs::Walks)      // legs that can't walk collapse: nothing to see
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s: skipped, its legs can't walk (%s)"), *M->GetSpecies()[AutoSpecies].Key, *M->GetSpecies()[AutoSpecies].LegSummary);
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f))
		{
			Area = EBFSpawnArea::Flat;
			bShowBalance = false;
			Pick(AutoSpecies);
			bAutoForward = false;
			IdleWhich = 1;
			IdleView = 0;
			BreathLo = BreathHi = 1.f;
			Next(181);
		}
		break;
	case 181:      // breathing at rest, over 12 s (a T. rex's breath takes 13), then the first behaviour
		if (D)
		{
			BreathLo = FMath::Min(BreathLo, D->GetBreathScale());
			BreathHi = FMath::Max(BreathHi, D->GetBreathScale());
		}
		if (AutoClock > (IdleWhich == 1 && IdleView == 0 ? 12.f : 1.5f) && D)
		{
			if (IdleWhich == 1 && IdleView == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s breathing at rest: %.1f a minute, ribcage %.3f..%.3f"), *D->GetSpecies().Key,
					D->GetBreathsPerMinute(), BreathLo, BreathHi);
			}
			Look(IdleView == 0 ? 90.f : 145.f, IdleView == 0 ? -5.f : -22.f);
			D->StartIdle((EBFIdle)IdleWhich);
			FootL0 = D->GetBoneWorld(TEXT("foot_l"));
			WatchStart = D->GetActorLocation();
			FootR0 = D->GetBoneWorld(TEXT("foot_r"));
			FootMoved = 0.f;
			Burst = 0;
			Next(182);
		}
		break;
	case 182:
	{
		if (D)
		{
			FootMoved = FMath::Max(FootMoved, FMath::Max(FVector::Dist2D(D->GetBoneWorld(TEXT("foot_l")), FootL0), FVector::Dist2D(D->GetBoneWorld(TEXT("foot_r")), FootR0)));
		}
		const EBFIdle Which = (EBFIdle)IdleWhich;
		// Its height: the first turn of a look or a weight shift, the middle of the rest, early in a shake.
		const float Len = ABFDino::IdleSeconds(Which);
		const float Peak = Which == EBFIdle::Look || Which == EBFIdle::Shift ? 0.25f * Len : Which == EBFIdle::Shake ? 0.22f * Len
			: Which == EBFIdle::Feed ? 0.28f * Len : 0.5f * Len;      // feeding: the jaw open on the plants
		ShotThen(Peak, *FString::Printf(TEXT("life_%s_%s"), ABFDino::IdleName(Which), IdleView == 0 ? TEXT("side") : TEXT("34")), [&]()
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s %s (%s view): head at %s"), *D->GetSpecies().Key, ABFDino::IdleName((EBFIdle)IdleWhich),
				IdleView == 0 ? TEXT("side") : TEXT("3/4"), *(D->GetBoneWorld(TEXT("head")) - D->GetActorLocation()).ToCompactString());
			// A shake from the side: three more frames through it.
			Next((EBFIdle)IdleWhich == EBFIdle::Shake && IdleView == 0 ? 188 : 183);
		});
		break;
	}
	case 188:
		ShotThen(0.05f, *FString::Printf(TEXT("life_shake_side_%d"), Burst + 2), [&]() { Next(++Burst < 3 ? 188 : 183); });
		break;
	case 183:      // let it finish, then the other view, then the next behaviour
		if (D)
		{
			FootMoved = FMath::Max(FootMoved, FMath::Max(FVector::Dist2D(D->GetBoneWorld(TEXT("foot_l")), FootL0), FVector::Dist2D(D->GetBoneWorld(TEXT("foot_r")), FootR0)));
		}
		if (D && D->GetIdle() == EBFIdle::None && AutoClock > 0.5f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s %s: the feet moved %.1f cm (the body %.1f cm; foot now %s from the body)"), *D->GetSpecies().Key, ABFDino::IdleName((EBFIdle)IdleWhich), FootMoved,
				FVector::Dist2D(D->GetActorLocation(), WatchStart), *(D->GetBoneWorld(TEXT("foot_l")) - D->GetActorLocation()).ToCompactString());
			if (IdleView == 0) { IdleView = 1; }
			else
			{
				IdleView = 0;
				++IdleWhich;
				if ((EBFIdle)IdleWhich == EBFIdle::Feed && !D->CanFeed()) { ++IdleWhich; }      // meat and fish eaters don't graze
			}
			Next(IdleWhich < (int32)EBFIdle::Count ? 181 : 184);
		}
		break;
	case 184:      // walking: the head against the hips over the last 3 of 6 s
		if (AutoClock < 0.1f)
		{
			bAutoForward = true;
			bAutoRun = false;
			Look(90.f, -5.f);
			HeadZ = FVector2D(1e9f, -1e9f);
			HipZ = FVector2D(1e9f, -1e9f);
			HeadSide = HipSide = FVector2D(1e9f, -1e9f);
			LockGap = 0.f;
		}
		if (D && AutoClock > 3.f) { LockGap = FMath::Max(LockGap, D->GetFootLockGap()); }
		if (D && AutoClock > 3.f) { Slide = FMath::Max(Slide, D->GetFootSlide()); Miss = FMath::Max(Miss, D->GetFootReachMiss()); }
		if (D && AutoClock > 3.f)
		{
			const float H = D->GetBoneWorld(TEXT("head")).Z, P = D->GetBoneWorld(TEXT("pelvis")).Z;
			HeadZ = FVector2D(FMath::Min(HeadZ.X, H), FMath::Max(HeadZ.Y, H));
			HipZ = FVector2D(FMath::Min(HipZ.X, P), FMath::Max(HipZ.Y, P));
			// the head's sway across the way it walks (cm), against its hips'
			const FVector Across = FVector::CrossProduct(D->GetVelocity().GetSafeNormal2D(), FVector::UpVector);
			const float HS = float(FVector::DotProduct(D->GetBoneWorld(TEXT("head")) - D->GetActorLocation(), Across));
			const float PS = float(FVector::DotProduct(D->GetBoneWorld(TEXT("pelvis")) - D->GetActorLocation(), Across));
			HeadSide = FVector2D(FMath::Min(HeadSide.X, HS), FMath::Max(HeadSide.Y, HS));
			HipSide = FVector2D(FMath::Min(HipSide.X, PS), FMath::Max(HipSide.Y, PS));
		}
		ShotThen(5.f, TEXT("life_walk"), [&]()
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s walking: sways side to side, hips %.1f cm, head %.1f cm"), *D->GetSpecies().Key, HipSide.Y - HipSide.X, HeadSide.Y - HeadSide.X);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s walking: hips bob %.1f cm, head %.1f cm, planted feet held up to %.1f cm off the pose (they'd slide %.1f cm), out of reach by %.1f cm"), *D->GetSpecies().Key,
				HipZ.Y - HipZ.X, HeadZ.Y - HeadZ.X, LockGap, Slide, Miss);
			Slide = 0.f;
			Miss = 0.f;
			LockGap = 0.f;
			SagRange = FVector2D(0.f, 0.f);
			Look(150.f, -15.f);
			Next(189);
		});
		break;
	case 189:      // walking, from the front three-quarters
		ShotThen(1.2f, TEXT("life_walk_34"), [&]() { Look(90.f, -5.f); Next(185); });
		break;
	case 185:      // running for 6 s (it winds it), then stopping short
		if (AutoClock < 0.1f) { bAutoForward = true; bAutoRun = true; }
		if (D && AutoClock > 2.f)
		{
			LockGap = FMath::Max(LockGap, D->GetFootLockGap());
			SagRange = FVector2D(FMath::Min(SagRange.X, D->GetSag()), FMath::Max(SagRange.Y, D->GetSag()));
			RunTipMax = FMath::Max(RunTipMax, D->GetFeatherTip());
		}
		if (AutoClock > 6.f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s running: belly swings %.1f..%.1f cm, planted feet up to %.1f cm off the pose"), *D->GetSpecies().Key,
				SagRange.X, SagRange.Y, LockGap);
			if (D->HasFeathers())
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s running: feather tips (%.1f cm feathers) sway and flutter up to %.2f cm"), *D->GetSpecies().Key, D->GetFeatherCm(), RunTipMax);
			}
			RunTipMax = 0.f;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s running: winded %.2f, %.1f breaths a minute"), *D->GetSpecies().Key, D->GetExertion(),
				D->GetBreathsPerMinute() * (1.f + 1.5f * D->GetExertion()));
			bAutoForward = false;
			SurgeLo = 0.f;
			Burst = 0;
			Next(186);
		}
		break;
	case 186:      // stopping short: the head nods on and the tail swings up, then they swing back (two frames)
		if (D) { SurgeLo = FMath::Min(SurgeLo, D->GetSurge()); }
		ShotThen(Burst == 0 ? 0.2f : 0.05f, Burst == 0 ? TEXT("life_stop") : TEXT("life_stop_2"), [&]()
		{
			if (++Burst < 2)
			{
				Next(186);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s stopping: surge down to %.2f, now %.2f"), *D->GetSpecies().Key, SurgeLo, D->GetSurge());
			BreathLo = BreathHi = 1.f;
			Next(187);
		});
		break;
	case 187:      // winded breathing over 6 s
		if (D)
		{
			BreathLo = FMath::Min(BreathLo, D->GetBreathScale());
			BreathHi = FMath::Max(BreathHi, D->GetBreathScale());
		}
		if (AutoClock > 6.f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s winded: ribcage %.3f..%.3f, still %.2f winded"), *D->GetSpecies().Key, BreathLo, BreathHi, D->GetExertion());
			Burst = 0;
			Next(190);
		}
		break;
	// Close-ups (roadmap 7, second part): the nostrils at rest and sniffing, a plant eater cropping and chewing, the
	// feathers before and during a shake, and the same stride with and without the skin sliding.
	case 190:      // the snout close from the side: the nostrils held shut, then a tenth of a second later at a sniff's full
		// flare (only they should differ), then at rest breathing, then sniffing
		if (!D) { break; }
		if (AutoClock <= DeltaTime + 1e-4f) { D->FocusHead(true, 0.85f, 0.45f); Look(90.f, -5.f); NoseMax = 0.f; D->SetNostrilOverride(0.f); Burst = 0; }
		if (Burst == 0 && !bShotTaken && AutoClock > 2.f)
		{
			Shot(TEXT("life_nose_shut"));
			ShotAt = AutoClock;
			bShotTaken = true;
		}
		else if (Burst == 0 && bShotTaken && AutoClock > ShotAt + 0.03f)
		{
			D->SetNostrilOverride(0.35f * 0.5f * D->GetNostrilCm());
			bShotTaken = false;
			Burst = 1;
		}
		else if (Burst == 1 && !bShotTaken && AutoClock > ShotAt + 0.1f)
		{
			Shot(TEXT("life_nose_flared"));
			bShotTaken = true;
		}
		else if (Burst == 1 && bShotTaken && AutoClock > ShotAt + 0.4f)
		{
			bShotTaken = false;
			D->SetNostrilOverride(-1.f);
			D->FocusHead(true);
			Burst = 2;
		}
		else if (Burst == 2)
		{
			ShotThen(ShotAt + 1.5f, TEXT("life_nose_rest"), [&]()
			{
				NoseRest = D->GetNostrilFlare();
				D->StartIdle(EBFIdle::Sniff);
				Next(191);
			});
		}
		break;
	case 191:      // sniffing: a picture at a full flare (the sniffs pulse four times a second)
		if (D) { NoseMax = FMath::Max(NoseMax, D->GetNostrilFlare()); }
		if (!bShotTaken && D && AutoClock > 1.3f && (D->GetNostrilFlare() > 0.95f * 0.35f * 0.5f * D->GetNostrilCm() || AutoClock > 2.6f))
		{
			Shot(TEXT("life_nose_sniff"));
			NoseShot = D->GetNostrilFlare();
			ShotAt = AutoClock;
			bShotTaken = true;
		}
		else if (bShotTaken && AutoClock > ShotAt + 0.3f)
		{
			bShotTaken = false;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s nostrils (%.1f cm): rims %.2f cm out at rest (breathing), %.2f sniffing (%.2f in the picture)"), *D->GetSpecies().Key,
				D->GetNostrilCm(), NoseRest, NoseMax, NoseShot);
			Burst = 0;
			Next(197);
		}
		break;
	case 197:      // the sniff finishing
		if (D && D->GetIdle() == EBFIdle::None && AutoClock > 0.2f) { Next(192); }
		break;
	case 192:      // a plant eater feeding, its head close from the side: cropping, then the jaw open and shut chewing
	{
		if (!D || !D->CanFeed())
		{
			Next(193);
			break;
		}
		if (AutoClock <= DeltaTime + 1e-4f)
		{
			D->StartIdle(EBFIdle::Feed);
			Look(80.f, -8.f);
			JawRange = FVector2D(1.f, 0.f);
		}
		const float Len = ABFDino::IdleSeconds(EBFIdle::Feed);
		if (AutoClock > 0.43f * Len && AutoClock < 0.85f * Len)
		{
			JawRange = FVector2D(FMath::Min(JawRange.X, D->GetMouthNow()), FMath::Max(JawRange.Y, D->GetMouthNow()));
		}
		const bool bShear = D->GetSpecies().Feed == EBFFeed::Shear;
		// The chew's first open and shut (FBFSpecies::Feed: the Triceratops a stroke every 0.9 s, the others 0.6 s).
		const float At[3] = { 0.28f * Len, 0.42f * Len + (bShear ? 0.4545f : 0.3125f), 0.42f * Len + (bShear ? 0.909f : 0.625f) };
		const TCHAR* FeedShots[3] = { TEXT("life_feed_crop"), TEXT("life_feed_chew_open"), TEXT("life_feed_chew_shut") };
		if (Burst < 3)
		{
			ShotThen(At[Burst], FeedShots[Burst], [&]() { ++Burst; });
		}
		else if (D->GetIdle() == EBFIdle::None)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s feeding (%s): the jaw %.2f..%.2f open while it chews"), *D->GetSpecies().Key,
				bShear ? TEXT("crops and chews") : D->GetSpecies().Feed == EBFFeed::Browse ? TEXT("strips leaves up high and swallows") : TEXT("crops and swallows"),
				JawRange.X, JawRange.Y);
			Burst = 0;
			Next(193);
		}
		break;
	}
	case 193:      // feathered wings, close from behind and above the shoulder: at rest, then shaking (ruffled up, fluttering)
		if (!D || !D->HasFeathers())
		{
			Next(195);
			break;
		}
		if (AutoClock <= DeltaTime + 1e-4f) { D->FocusBone(TEXT("hand_l"), 7.f * D->GetFeatherCm()); Look(60.f, -20.f); TipMax = 0.f; }
		ShotThen(1.5f, TEXT("life_feathers_rest"), [&]() { D->StartIdle(EBFIdle::Shake); Next(194); });
		break;
	case 194:
		if (D) { TipMax = FMath::Max(TipMax, D->GetFeatherTip()); }
		ShotThen(0.55f, TEXT("life_feathers_shake"), [&]()
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s feathers (%.1f cm): shaking, the tips ruffle out and flutter up to %.2f cm"), *D->GetSpecies().Key,
				D->GetFeatherCm(), TipMax);
			D->FocusBone(NAME_None, 0.f);
			Next(195);
		});
		break;
	case 195:      // skin sliding: walking, the hips close from the side, a quarter into a stride with it and a stride later without
		if (!D) { break; }
		if (AutoClock <= DeltaTime + 1e-4f)
		{
			if (D->GetSpecies().Limbs.Num() > 0)
			{
				D->FocusBone(D->GetSpecies().Limbs[0].Upper, 1.4f * D->GetSpecies().Limbs[0].LengthCm + 40.f);
			}
			Look(90.f, -5.f);
			bAutoForward = true;
			bAutoRun = false;
			SlideMaxDeg = SlideMaxCm = 0.f;
			Burst = 0;
			LastPhase = D->GetPhase();
		}
		if (AutoClock > 2.f)
		{
			SlideMaxDeg = FMath::Max(SlideMaxDeg, D->GetSlideDeg());
			SlideMaxCm = FMath::Max(SlideMaxCm, D->GetSlideCm());
		}
		{
			const float Ph = D->GetPhase();
			const bool bCross = FMath::Frac(LastPhase) < 0.25f && FMath::Frac(Ph) >= 0.25f && FMath::FloorToFloat(LastPhase) == FMath::FloorToFloat(Ph);
			LastPhase = Ph;
			// With it, a stride later without it, and a stride after that with it again: the last against the first is
			// how much the same point of two strides differs anyway.
			if (!bShotTaken && AutoClock > 4.f && bCross && Burst < 3)
			{
				Shot(Burst == 0 ? TEXT("life_slide_on") : Burst == 1 ? TEXT("life_slide_off") : TEXT("life_slide_on2"));
				ShotAt = AutoClock;
				bShotTaken = true;
			}
			else if (bShotTaken && AutoClock > ShotAt + 0.1f)
			{
				bShotTaken = false;
				D->SetSlideScale(Burst == 0 ? 0.f : 1.f);      // the next stride without it (the shot is taken at the end of its frame)
				++Burst;
			}
		}
		if (Burst >= 3 && !bShotTaken)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life %s skin sliding: limbs swing up to %.0f deg from rest, the pattern slides up to %.1f cm near the joints"), *D->GetSpecies().Key,
				SlideMaxDeg, SlideMaxCm);
			D->SetSlideScale(1.f);
			D->FocusBone(NAME_None, 0.f);
			bAutoForward = false;
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); } else { Next(180); }
		}
		break;
	// Swimming (-bfswim): every animal in the -bfonly list (default the six) dropped into the deep end of the pool;
	// how it floats (or sinks), then paddling along; its state and depth go to the log.
	case 140:
		if (M->GetSpecies()[AutoSpecies].IsSplice() ? !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key) : (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key)))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Pool; Pick(AutoSpecies); bAutoForward = false; Next(141); }
		break;
	case 141:
		if (AutoClock > 1.f && D)
		{
			// Into the deep end, dropped from just above the water.
			const float HH = D->GetSpecies().Bounds.GetSize().Z * 0.5f;
			D->SetActorLocation(FVector(-2000.f, 0.5f * (ABFGameMode::PoolMinY + ABFGameMode::PoolMaxY), ABFGameMode::WaterZ + HH + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			Next(142);
		}
		break;
	case 142:
		if (AutoClock > 7.f && D) { Look(90.f, -5.f); Next(143); }
		break;
	case 143:
		ShotThen(1.0f, TEXT("swim_side"), [&]() { Look(150.f, -20.f); Next(144); });
		break;
	case 144:
		ShotThen(1.0f, TEXT("swim_34"), [&]()
		{
			if (D)
			{
				const FVector L = D->GetActorLocation();
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] swim %s: %s, head %s; capsule %.0f cm under the surface | %s"), *D->GetSpecies().Key,
					D->IsSwimming() ? TEXT("floating") : D->IsInWater() ? TEXT("on the bottom") : TEXT("out of the water"),
					D->IsHeadUnderWater() ? TEXT("under") : TEXT("clear"), ABFGameMode::WaterZ - L.Z, *D->GetSpecies().SwimSummary);
				const FHitResult& Floor = D->GetCharacterMovement()->CurrentFloor.HitResult;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] swim %s floor: %s (%s) at z %.0f, movement mode %d"), *D->GetSpecies().Key,
					Floor.GetActor() ? *Floor.GetActor()->GetName() : TEXT("none"), Floor.GetComponent() ? *Floor.GetComponent()->GetName() : TEXT("-"),
					Floor.ImpactPoint.Z, (int32)D->GetCharacterMovement()->MovementMode.GetValue());
			}
			Look(70.f, 22.f);        // from below the surface, looking up at it
			Next(146);
		});
		break;
	case 146:
		ShotThen(1.5f, TEXT("swim_under"), [&]()
		{
			bAutoForward = true;
			bAutoRun = false;
			StartWatch();
			Look(90.f, -5.f);
			Next(145);
		});
		break;
	case 145:
		ShotThen(5.f, TEXT("swim_move"), [&]()
		{
			if (D)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] swim %s moving: %.2f m/s, %s, head %s"), *D->GetSpecies().Key, D->GetSpeedMs(),
					*D->GetGaitName(), D->IsHeadUnderWater() ? TEXT("under") : TEXT("clear"));
			}
			bAutoForward = false;
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); } else { Next(140); }
		});
		break;
	// Ankles close up (-bfankles): every animal in the -bfonly list (default the six), standing and in mid-stride.
	case 130:
		if (M->GetSpecies()[AutoSpecies].IsSplice() ? !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key) : (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key)))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > 2.f) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = false; Next(131); }
		break;
	case 131:
		if (AutoClock > 2.f && D) { D->LookAtFeet(); Look(80.f, -8.f); Next(132); }
		break;
	case 132:
		ShotThen(0.6f, TEXT("ankle_stand"), [&]() { Look(200.f, -8.f); Next(133); });
		break;
	case 133:
		ShotThen(0.6f, TEXT("ankle_stand_far"), [&]() { Look(215.f, -8.f); Next(137); });
		break;
	case 137:
		ShotThen(0.6f, TEXT("ankle_stand_far2"), [&]() { bAutoForward = true; bAutoRun = false; Look(80.f, -8.f); Next(134); });
		break;
	case 134:
		ShotThen(2.5f, TEXT("ankle_walk_a"), [&]() { Next(135); });
		break;
	case 135:
		ShotThen(0.45f, TEXT("ankle_walk_b"), [&]() { Next(136); });
		break;
	case 136:
		ShotThen(0.45f, TEXT("ankle_walk_c"), [&]()
		{
			bAutoForward = false;
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); } else { Next(130); }
		});
		break;
	// The editor by hand (-bfclicks): its buttons pressed through Slate's mouse path, then its view controls and Esc.
	case 120:
		if (AutoClock > 4.f)
		{
			OpenSplicer();
			if (FParse::Param(FCommandLine::Get(), TEXT("bfadvanced")))
			{
				Stage->StartAdvancedClickTest();      // the single-bone Advanced mode
			}
			else if (FParse::Param(FCommandLine::Get(), TEXT("bfwings")))
			{
				Stage->StartWingsClickTest();         // wings and flying
			}
			else
			{
				Stage->StartClickTest();
			}
			Next(121);
		}
		break;
	case 121:
		if (WasInputKeyJustPressed(EKeys::Escape) || Stage->WantsBack())
		{
			CloseSplicer();
		}
		if (Stage->IsClickTestDone() || !Stage->IsOpen())
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] clicks done: %s"), *Stage->Summary());
			Next(122);
		}
		break;
	case 122:
		if (AutoClock > 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] after Esc the editor is %s"), Stage->IsOpen() ? TEXT("still open") : TEXT("closed"));
			Next(124);
		}
		break;
	case 124:
		if (AutoClock > 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("clicks_end.png")), true, false);
			Next(123);
		}
		break;
	case 123:
		if (AutoClock > 1.f)
		{
			Next(9);
		}
		break;
	// Splicing editor (-bfeditor): a swap, a resize, then the body grown; the readout goes to the log.
	case 100:
		if (AutoClock > 4.f)
		{
			OpenSplicer();
			Stage->SetBase(TEXT("trex"));
			Stage->SetDonor(TEXT("Head"), TEXT("trike"));
			Stage->SetDonor(TEXT("Tail"), TEXT("ankylo"));
			Next(101);
		}
		break;
	case 101:
		if (AutoClock > 1.f && !Stage->IsBusy())
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] editor swaps: %s"), *Stage->Summary());
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("editor_swaps.png")), true, false);
			Stage->ChangeScale(TEXT("Hip"), 0.2f);
			Next(102);
		}
		break;
	case 102:
		if (AutoClock > 1.f && !Stage->IsBusy())
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] editor scaled: %s"), *Stage->Summary());
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("editor_scaled.png")), true, false);
			Next(110);
		}
		break;
	case 113:      // -bftestit: straight to the Ankylosaurus with Brachiosaurus legs, grown, then Test it pressed
		if (AutoClock > 4.f)
		{
			OpenSplicer();
			Next(110);
		}
		break;
	case 110:
		if (AutoClock > 1.f)
		{
			Stage->SetBase(TEXT("ankylo"));          // a Brachiosaurus' back legs, sized to fit an Ankylosaurus
			Stage->SetDonor(TEXT("Hip"), TEXT("brachio"));
			Stage->FitSize(TEXT("Hip"));
			Stage->ChangeThickness(TEXT("Hip"), 0.1f);   // thick enough to walk
			Next(111);
		}
		break;
	case 111:
		if (AutoClock > 1.f && !Stage->IsBusy())
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] editor fitted: %s"), *Stage->Summary());
			Next(112);
		}
		break;
	case 112:
		if (AutoClock > 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("editor_fit.png")), true, false);
			Next(103);
		}
		break;
	case 103:
		if (AutoClock > 1.f)
		{
			Stage->Grow(false);
			Next(104);
		}
		break;
	case 104:
		if (AutoClock > 1.f && !Stage->IsBusy())
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] editor grown (body shown %d): %s"), Stage->IsBodyShown() ? 1 : 0, *Stage->Summary());
			Next(105);
		}
		break;
	case 105:
		if (AutoClock > 2.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("editor_body.png")), true, false);
			Next(106);
		}
		break;
	case 106:
		if (AutoClock > 1.f)
		{
			Stage->Orbit(-110.f);
			Next(108);
		}
		break;
	case 108:
		if (AutoClock > 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("editor_body2.png")), true, false);
			Next(107);
		}
		break;
	case 107:
		if (AutoClock > 1.f)
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("bfwalk")))
			{
				Stage->Grow(true);       // Walk it: brought into this game as it runs (WalkIt), then catalogued
				Next(10);
			}
			else if (FParse::Param(FCommandLine::Get(), TEXT("bftestit")))
			{
				Stage->PressButton(TEXT("Test it"));     // brought into this game and tested here (WalkIt)
				Next(10);
			}
			else
			{
				CloseSplicer();          // back to the field: the animal returns
				Next(109);
			}
		}
		break;
	case 109:
		if (AutoClock > 2.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] editor closed: pawn %s"), D ? *D->GetSpecies().Key : TEXT("none"));
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("editor_closed.png")), true, false);
			Next(9);
		}
		break;
	// Catalog of six (-bfcatalog): every splice stands for a side and a three-quarter look, then tries to walk; its
	// leg check, flight check and balance go to the log. First a side look just after it appears (cat_stand), before
	// legs that buckle give way (1.2 s) or a body that can't balance tips over (2.5 s).
	case 90:
		if (!M->GetSpecies()[AutoSpecies].IsSplice() || (AutoOnly.Num() && !AutoOnly.Contains(M->GetSpecies()[AutoSpecies].Key)))
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; Pick(AutoSpecies); bAutoForward = false; Next(96); }
		break;
	case 96:
		Look(90.f, -10.f);
		ShotThen(0.6f, TEXT("cat_stand"), [&]() { Next(91); });
		break;
	case 91:
		if (AutoClock > 1.6f)
		{
			const FBFSpecies& S = M->GetSpecies()[AutoSpecies];
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] catalog %s: %s | leg check: %s%s%s | lineage %s"), *S.Key, *S.Name, *S.LegSummary,
				S.CanFly() ? TEXT(" | flight check: ") : TEXT(""), *S.FlightSummary, *S.Lineage);
			Look(90.f, -10.f);
			Next(92);
		}
		break;
	case 92:
		ShotThen(1.0f, TEXT("cat_side"), [&]() { Look(150.f, -22.f); Next(93); });
		break;
	case 93:
		ShotThen(1.0f, TEXT("cat_34"), [&]() { bAutoForward = true; bAutoRun = false; Next(94); });
		break;
	case 94:
		if (AutoClock > 3.f) { Look(90.f, -10.f); Next(95); }
		break;
	case 95:
		ShotThen(1.0f, TEXT("cat_walk"), [&]()
		{
			if (D) { UE_LOG(LogTemp, Display, TEXT("[BoneForge] catalog %s walk: %.2f m/s, %s"), *M->GetSpecies()[AutoSpecies].Key, D->GetSpeedMs(), D->IsRagdoll() ? TEXT("collapsed") : *D->GetGaitName()); }
			bAutoForward = false;
			if (++AutoSpecies < M->GetSpecies().Num()) { Next(90); } else { Next(9); }
		});
		break;
	// Your creatures and undo (-bfmine), by real clicks and typing: a catalog splice opened with Edit a copy, changed,
	// undone and redone, named and walked; then renamed, removed and put back from the menu, and ] pressed.
	case 320:
		if (AutoClock > 4.f)
		{
			ShowPicker(true);
			int32 Six = 0, Cat = 0, Yours = 0;
			for (const FBFSpecies& S : M->GetSpecies())
			{
				++(!S.IsSplice() ? Six : ABFGameMode::IsYours(S.Key) ? Yours : Cat);
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: menu lists %d of the six, %d catalog, %d yours, %d removed"), Six, Cat, Yours, M->GetRemoved().Num());
			Next(321);
		}
		break;
	case 321:
		if (AutoClock > 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("mine_menu.png")), true, false);
			Next(335);
		}
		break;
	case 335:
		if (AutoClock > 0.5f)
		{
			int32 Nth = 0;      // rextrikehead's "Edit a copy": the catalog splices in species order
			for (const FBFSpecies& S : M->GetSpecies())
			{
				if (S.Key == TEXT("rextrikehead"))
				{
					break;
				}
				Nth += S.IsSplice() && !ABFGameMode::IsYours(S.Key) ? 1 : 0;
			}
			PressMenu(TEXT("Edit a copy"), Nth, 322);
		}
		break;
	case 334:          // PressMenu: the button scrolled into view, now clicked
		if (AutoClock > 0.3f)
		{
			ABFSpliceStage::ClickWidget(MenuPending);
			MenuPending.Reset();
			Next(MenuAfter);
		}
		break;
	case 322:
		if (AutoClock > 2.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: editor %s, recipe %s"), Stage && Stage->IsOpen() ? TEXT("open") : TEXT("NOT OPEN"), Stage ? *Stage->GetRecipe() : TEXT("-"));
			if (!Stage || !Stage->IsOpen())
			{
				Next(9);
				break;
			}
			Stage->StartUndoClickTest();
			Next(323);
		}
		break;
	case 323:
		if (Stage->IsOpen() && AutoClock > 300.f)
		{
			UE_LOG(LogTemp, Error, TEXT("[BoneForge] mine: Walk it never finished: %s"), *Stage->Summary());
			Next(9);
		}
		else if (!Stage->IsOpen() && D)
		{
			MineKey = D->GetSpecies().Key;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: walked %s as \"%s\" (given name \"%s\")"), *MineKey, *D->GetSpecies().Name, *M->GivenNameOf(MineKey));
			Next(324);
		}
		break;
	case 324:
		if (AutoClock > 3.f)
		{
			ShowPicker(true);
			Next(325);
		}
		break;
	case 325:
		if (AutoClock > 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("mine_list.png")), true, false);
			PressMenu(TEXT("Rename"), 0, 326);       // the newest of your creatures: this one
		}
		break;
	case 326:
		if (AutoClock > 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: renaming %s, its box %s, has the keys: %s"), *RenamingKey, RenameBox.IsValid() ? TEXT("shown") : TEXT("MISSING"),
				ABFSpliceStage::IsTyping() ? TEXT("yes") : TEXT("NO"));
			FSlateApplication& A = FSlateApplication::Get();
			for (TCHAR Ch : FString(TEXT("Rexatops")))    // the old name is selected, so this replaces it
			{
				A.ProcessKeyCharEvent(FCharacterEvent(Ch, A.GetModifierKeys(), 0, false));
			}
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, A.GetModifierKeys(), 0, false, 0, 0));
			A.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, A.GetModifierKeys(), 0, false, 0, 0));
			Next(327);
		}
		break;
	case 327:
		if (AutoClock > 1.f)
		{
			const int32 I = M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == MineKey; });
			FString Saved;
			FFileHelper::LoadFileToString(Saved, *FPaths::Combine(M->BoneForgeDir(), TEXT("my_creatures.json")));
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: renamed: \"%s\", keys back with the game: %s, my_creatures.json has it: %s"),
				I != INDEX_NONE ? *M->GetSpecies()[I].Name : TEXT("-"), ABFSpliceStage::IsTyping() ? TEXT("NO") : TEXT("yes"),
				Saved.Contains(TEXT("Rexatops")) ? TEXT("yes") : TEXT("NO"));
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("mine_renamed.png")), true, false);
			Next(328);
		}
		break;
	case 328:
		if (AutoClock > 1.f)
		{
			PressMenu(TEXT("Remove"), 0, 329);
		}
		break;
	case 329:
		if (AutoClock > 1.f)
		{
			const bool bListed = M->GetSpecies().ContainsByPredicate([&](const FBFSpecies& S) { return S.Key == MineKey; });
			const TArray<ABFGameMode::FRemoved> Removed = M->GetRemoved();
			const int32 At = Removed.IndexOfByPredicate([&](const ABFGameMode::FRemoved& R) { return R.Key == MineKey; });
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: after Remove: in the species %s, rig in place %s, in removed/ %s (as \"%s\"), playing %s"),
				bListed ? TEXT("STILL") : TEXT("no"),
				IFileManager::Get().FileExists(*FPaths::Combine(M->RigFolder, MineKey + TEXT("_rig.json"))) ? TEXT("STILL") : TEXT("no"),
				At != INDEX_NONE ? TEXT("yes") : TEXT("NO"), At != INDEX_NONE ? *Removed[At].Name : TEXT("-"), D ? *D->GetSpecies().Key : TEXT("-"));
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("mine_removed.png")), true, false);
			PressMenu(TEXT("Put back"), FMath::Max(At, 0), 330);
		}
		break;
	case 330:
	{
		const int32 I = M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == MineKey; });
		if (I != INDEX_NONE || AutoClock > 90.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: put back after %.1f s: %s, rig in place %s, note: %s"), AutoClock,
				I != INDEX_NONE ? *FString::Printf(TEXT("in the species as \"%s\""), *M->GetSpecies()[I].Name) : TEXT("NOT BACK"),
				IFileManager::Get().FileExists(*FPaths::Combine(M->RigFolder, MineKey + TEXT("_rig.json"))) ? TEXT("yes") : TEXT("NO"), *PickerNote);
			Next(331);
		}
		break;
	}
	case 331:
		if (AutoClock > 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(AutoOut, TEXT("mine_back.png")), true, false);
			Next(332);
		}
		break;
	case 332:
		if (AutoClock > 1.f)
		{
			// ] through Slate, read by the ordinary input code as a player's would be.
			ShowPicker(false);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: before ] playing #%d %s"), Current, *CurrentKey);
			bAutoKeys = true;
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::RightBracket, A.GetModifierKeys(), 0, false, 0, 0));
			Next(333);
		}
		break;
	case 333:
		if (AutoClock > 0.3f)
		{
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyUpEvent(FKeyEvent(EKeys::RightBracket, A.GetModifierKeys(), 0, false, 0, 0));
			bAutoKeys = false;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] mine: after ] playing #%d %s"), Current, *CurrentKey);
			Next(9);
		}
		break;
	// The world (-bfworld [-bfseed=N]): an island made from a seed, seen from the air from two sides, a T. rex put on it
	// and walked (how far its feet stay from the ground's height), then set by the nearest river and the nearest lake
	// (whether the swim system finds the water).
	case 440:
		if (AutoClock > 4.f)
		{
			int32 Seed = 20261112;
			FParse::Value(FCommandLine::Get(), TEXT("bfseed="), Seed);
			MakeIsland(Seed);
			bGoWhenReady = false;
			Next(441);
		}
		break;
	case 441:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: %s"), *M->GetIsland()->GetSummary());
			Area = EBFSpawnArea::World;
			AutoSpecies = 0;
			Pick(AutoSpecies);
			Next(442);
		}
		else if (AutoClock > 180.f)
		{
			UE_LOG(LogTemp, Error, TEXT("[BoneForge] world: not ready after 3 minutes (%s, %.0f%%)"), M->GetIsland() ? *M->GetIsland()->GetStage() : TEXT("-"),
				M->GetIsland() ? 100.f * M->GetIsland()->GetProgress() : 0.f);
			Next(9);
		}
		break;
	case 442:      // from the air: from the south, high, then from the west, lower
	case 443:
		if (AutoFrame == 1 && D)
		{
			SetFreeCam(true);
			const ABFWorld* I = M->GetIsland();
			const FVector C = I->GetCentre();
			const double S = I->SizeCm();
			const FVector From = AutoStep == 442 ? C + FVector(0.0, -0.75 * S, 0.35 * S) : C + FVector(-0.7 * S, 0.1 * S, 0.12 * S);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
		}
		if (AutoClock > 3.f)
		{
			Shot(AutoStep == 442 ? TEXT("world_air_south") : TEXT("world_air_west"));
			Next(AutoStep + 1);
		}
		break;
	case 444:      // on the ground beside the T. rex, then it walks
		if (AutoFrame == 1 && D && FreeCam)
		{
			const FVector From = D->GetActorLocation() - D->GetActorForwardVector() * 2500.f + D->GetActorRightVector() * 1200.f + FVector(0.f, 0.f, 600.f);
			FreeCam->SetActorLocationAndRotation(From, (D->GetActorLocation() + D->GetActorForwardVector() * 3000.f - From).Rotation());
		}
		if (AutoClock > 2.f && D)
		{
			Shot(TEXT("world_ground"));
			SetFreeCam(false);
			WorldWalk0 = D->GetActorLocation();
			WorldClearLo = 1e9f;
			WorldClearHi = -1e9f;
			bAutoForward = true;
			AutoDir = D->GetActorForwardVector();
			Next(445);
		}
		break;
	case 445:
		if (D)
		{
			// The capsule's bottom against the ground's height under it.
			float Ground = 0.f;
			if (M->GetIsland()->GroundAt(FVector2D(D->GetActorLocation()), Ground))
			{
				const float Clear = D->GetActorLocation().Z - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Ground;
				WorldClearLo = FMath::Min(WorldClearLo, Clear);
				WorldClearHi = FMath::Max(WorldClearHi, Clear);
			}
			if (AutoClock > 10.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] world walk: %.0f m in 10 s, %.2f m/s now, the capsule %.0f..%.0f cm above the ground, on %s"),
					FVector::Dist2D(D->GetActorLocation(), WorldWalk0) / 100.f, D->GetSpeedMs(), WorldClearLo, WorldClearHi,
					*UEnum::GetValueAsString(M->GetIsland()->BiomeAt(FVector2D(D->GetActorLocation()))));
				Look(160.f, -12.f);
				Shot(TEXT("world_walk"));
				bAutoForward = false;
				AutoDir = FVector::ZeroVector;
				Next(446);
			}
		}
		break;
	case 446:      // by the nearest river, then the nearest lake: in the water?
	case 448:
		if (AutoFrame == 1 && D)
		{
			FVector At;
			const EBFBiome Kind = AutoStep == 446 ? EBFBiome::River : EBFBiome::Lake;
			if (M->GetIsland()->NearestWater(FVector2D(D->GetActorLocation()), Kind, At))
			{
				D->TeleportTo(At + FVector(0.f, 0.f, D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 200.f), D->GetActorRotation());
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: put by the nearest %s at %s"), AutoStep == 446 ? TEXT("river") : TEXT("lake"), *At.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: no %s on this island"), AutoStep == 446 ? TEXT("river") : TEXT("lake"));
				Next(AutoStep == 446 ? 448 : 449);
			}
		}
		if (AutoFrame == 30 && D)
		{
			WorldWalk0 = D->GetActorLocation();       // from here: how far the current carries it, untouched
			WorldClock0 = AutoClock;
		}
		if (AutoClock > 5.f && D)
		{
			float Surface = 0.f;
			const bool bWater = ABFGameMode::WaterAt(D->GetActorLocation(), Surface);
			const FVector Flowing = ABFGameMode::CurrentAt(D->GetActorLocation());
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] world %s: in the water %d (surface %.0f, its middle %.0f), swimming %d; current %.2f m/s, carried %.2f m in %.1f s"),
				AutoStep == 446 ? TEXT("river") : TEXT("lake"), D->IsInWater() ? 1 : 0, bWater ? Surface : 0.f, D->GetActorLocation().Z, D->IsSwimming() ? 1 : 0,
				Flowing.Size2D() / 100.f, FVector::Dist2D(D->GetActorLocation(), WorldWalk0) / 100.f, AutoClock - WorldClock0);
			Look(120.f, -15.f);
			Shot(AutoStep == 446 ? TEXT("world_river") : TEXT("world_lake"));
			WorldWater = D->GetActorLocation();
			Next(AutoStep == 446 ? 447 : 449);
		}
		break;
	case 447:      // low over the river, looking downstream: its surface, its ripples carried by the current
		if (AutoFrame == 1)
		{
			SetFreeCam(true);
			FVector Along = ABFGameMode::CurrentAt(WorldWater).GetSafeNormal2D();
			if (Along.IsNearlyZero())
			{
				Along = FVector(1.f, 0.f, 0.f);
			}
			float Surface = 0.f;
			ABFGameMode::WaterAt(WorldWater, Surface);
			const FVector From = FVector(WorldWater.X, WorldWater.Y, Surface) - Along * 2200.f + FVector::CrossProduct(Along, FVector::UpVector) * 900.f + FVector(0.f, 0.f, 380.f);
			FreeCam->SetActorLocationAndRotation(From, (FVector(WorldWater.X, WorldWater.Y, Surface) + Along * 2500.f - From).Rotation());
		}
		if (AutoClock > 2.f && AutoClock < 3.f)
		{
			Shot(TEXT("world_river_close"));
			AutoClock = 3.f;
		}
		if (AutoClock > 3.6f)
		{
			Shot(TEXT("world_river_close_later"));       // 0.6 s on: the ripples have moved down the river
			SetFreeCam(false);
			Next(448);
		}
		break;
	case 449:      // the sea: from the beach looking out, from the water looking in, then the T. rex in it
	case 450:
		if (AutoFrame == 1 && D)
		{
			const ABFWorld* I = M->GetIsland();
			FVector At = FVector::ZeroVector;
			if (AutoStep == 449 && !I->NearestWater(FVector2D(I->StartPoint()), EBFBiome::Sea, At))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: no sea?"));
				Next(9);
				break;
			}
			if (AutoStep == 449)
			{
				WorldWater = At;
			}
			const FVector Out = (FVector(WorldWater.X, WorldWater.Y, 0.f) - FVector(I->GetCentre().X, I->GetCentre().Y, 0.f)).GetSafeNormal();
			SetFreeCam(true);
			float Ground = 0.f;
			I->GroundAt(FVector2D(WorldWater - Out * 2500.f), Ground);
			const FVector From = AutoStep == 449 ? WorldWater - Out * 2500.f + FVector(0.f, 0.f, FMath::Max(Ground, 0.f) + 350.f - WorldWater.Z)
				: FVector(WorldWater.X, WorldWater.Y, 0.f) + Out * 7000.f + FVector(0.f, 0.f, 600.f);
			const FVector To = AutoStep == 449 ? FVector(WorldWater.X, WorldWater.Y, 0.f) + Out * 9000.f : FVector(WorldWater.X, WorldWater.Y, 0.f) - Out * 1500.f;
			FreeCam->SetActorLocationAndRotation(From, (To - From).Rotation());
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] world sea: shore at %s, the sea %.2f m above its level there now"), *WorldWater.ToString(),
				I->SeaWaveM(FVector2D(WorldWater + Out * 3000.f), GetWorld()->GetTimeSeconds()));
		}
		if (AutoClock > 2.5f && AutoClock < 3.5f)
		{
			Shot(AutoStep == 449 ? TEXT("world_sea_out") : TEXT("world_sea_in"));
			AutoClock = 3.5f;
		}
		if (AutoClock > 4.5f)
		{
			Shot(AutoStep == 449 ? TEXT("world_sea_out_later") : TEXT("world_sea_in_later"));
			Next(AutoStep + 1);
			if (AutoStep == 451)
			{
				SetFreeCam(false);
			}
		}
		break;
	case 451:      // the T. rex 60 m out to sea: does it float, and ride the waves?
		if (AutoFrame == 1 && D)
		{
			const ABFWorld* I = M->GetIsland();
			const FVector Out = (FVector(WorldWater.X, WorldWater.Y, 0.f) - FVector(I->GetCentre().X, I->GetCentre().Y, 0.f)).GetSafeNormal();
			D->TeleportTo(FVector(WorldWater.X, WorldWater.Y, 0.f) + Out * 6000.f + FVector(0.f, 0.f, 100.f), Out.Rotation());
			WorldClearLo = 1e9f;
			WorldClearHi = -1e9f;
		}
		if (AutoClock > 5.f && D)
		{
			WorldClearLo = FMath::Min(WorldClearLo, float(D->GetActorLocation().Z));
			WorldClearHi = FMath::Max(WorldClearHi, float(D->GetActorLocation().Z));
		}
		if (AutoClock > 12.f && D)
		{
			float Surface = 0.f;
			ABFGameMode::WaterAt(D->GetActorLocation(), Surface);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] world sea swim: in the water %d, swimming %d, rose and fell %.0f cm over 7 s (surface now %.0f cm)"),
				D->IsInWater() ? 1 : 0, D->IsSwimming() ? 1 : 0, WorldClearHi - WorldClearLo, Surface);
			Look(150.f, -10.f);
			Shot(TEXT("world_sea_swim"));
			PlantShot = 0;
			Next(460);
		}
		break;
	// The plants: each kind close, the plains, inside a forest, the swamp, the beach, and from the air; how long a frame
	// takes at each (the frame time over the last second before the picture).
	case 460:
		{
			const ABFWorld* I = M->GetIsland();
			struct FView { const TCHAR* Name; int32 Kind; float Back, Up, LookUp; };
			static const FView Views[] = {
				{ TEXT("plants_plains"), -1, 0.f, 250.f, 0.f }, { TEXT("plants_forest"), int32(EBFPlant::Redwood), 3200.f, 200.f, 900.f },
				{ TEXT("plants_forest_floor"), int32(EBFPlant::Redwood), -900.f, 170.f, 120.f }, { TEXT("plants_cycad"), int32(EBFPlant::Cycad), 520.f, 180.f, 110.f },
				{ TEXT("plants_treefern"), int32(EBFPlant::TreeFern), 800.f, 200.f, 300.f }, { TEXT("plants_swamp"), int32(EBFPlant::Cypress), 2600.f, 260.f, 700.f },
				{ TEXT("plants_beach"), int32(EBFPlant::Palm), 1500.f, 200.f, 400.f }, { TEXT("plants_horsetail"), int32(EBFPlant::Horsetail), 420.f, 150.f, 80.f },
				{ TEXT("plants_shrub"), int32(EBFPlant::Shrub), 450.f, 170.f, 110.f }, { TEXT("plants_bennettite"), int32(EBFPlant::Bennettite), 380.f, 150.f, 70.f },
				{ TEXT("plants_fern"), int32(EBFPlant::Fern), 260.f, 130.f, 30.f }, { TEXT("plants_air"), -2, 0.f, 15000.f, 0.f },
				{ TEXT("mouth_air"), -3, 0.f, 0.f, 0.f }, { TEXT("mouth_sea"), -4, 0.f, 0.f, 0.f }, { TEXT("mouth_top"), -5, 0.f, 0.f, 0.f },
				{ TEXT("trees_top"), -6, 0.f, 30000.f, 0.f }, { TEXT("trees_top_high"), -6, 0.f, 80000.f, 0.f }, { TEXT("plains_low_air"), -7, 0.f, 6000.f, 0.f } };
			if (PlantShot >= UE_ARRAY_COUNT(Views))
			{
				SetFreeCam(false);
				Next(470);
				break;
			}
			const FView& Vw = Views[PlantShot];
			if (AutoFrame == 1)
			{
				SetFreeCam(true);
				const FVector Start = I->StartPoint();
				const FVector Mid = I->GetCentre();
				FVector From, To;
				if (Vw.Kind == -1)
				{
					From = Start + FVector(0.f, 0.f, Vw.Up);
					To = From + (FVector(Mid.X, Mid.Y, From.Z) - From).GetSafeNormal() * 3000.f - FVector(0.f, 0.f, 200.f);
				}
				else if (Vw.Kind == -2)
				{
					From = Start + FVector(-6000.f, -6000.f, Vw.Up);
					To = Start + FVector(4000.f, 4000.f, 0.f);
				}
				else if (Vw.Kind >= -5 && Vw.Kind <= -3)
				{
					// The river mouth nearest where creatures arrive: from the air inland of it, low from the sea, straight down.
					FVector Mouth, Sea;
					if (!I->RiverMouth(FVector2D(Start), Mouth, Sea))
					{
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] plants: no river mouth on this island"));
						++PlantShot;
						Next(460);
						break;
					}
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] river mouth at %s, the sea toward %s"), *Mouth.ToString(), *Sea.ToString());
					const FVector M0(Mouth.X, Mouth.Y, 0.f);
					const FVector Side = FVector::CrossProduct(Sea, FVector::UpVector);
					From = Vw.Kind == -3 ? M0 - Sea * 9000.f + Side * 3000.f + FVector(0.f, 0.f, 6000.f)
						: Vw.Kind == -4 ? M0 + Sea * 3500.f + Side * 800.f + FVector(0.f, 0.f, 350.f) : M0 + FVector(0.f, 0.f, 12000.f);
					To = Vw.Kind == -5 ? M0 + FVector(1.f, 0.f, -12000.f) : M0 + FVector(0.f, 0.f, 100.f);
				}
				else if (Vw.Kind == -6)
				{
					// Straight down over the nearest forest: do the trees stand in rows?
					FVector At;
					I->NearestPlant(FVector2D(Start), EBFPlant::Redwood, At);
					From = FVector(At.X, At.Y, At.Z + Vw.Up);
					To = FVector(At.X + 1.f, At.Y, At.Z);
				}
				else if (Vw.Kind == -7)
				{
					// Low over the open ground: the fern prairie.
					From = Start + FVector(-5000.f, 0.f, Vw.Up);
					To = Start + FVector(3000.f, 0.f, 0.f);
				}
				else
				{
					FVector At;
					if (!I->NearestPlant(FVector2D(Start), EBFPlant(Vw.Kind), At))
					{
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] plants: no %s on this island"), BFPlants::Kind(EBFPlant(Vw.Kind)).Name);
						++PlantShot;
						Next(460);
						break;
					}
					// From the side the creature arrives on, Back cm off (negative: past it, looking away), at a person's height or so.
					FVector Dir = (FVector(Start.X, Start.Y, 0.f) - FVector(At.X, At.Y, 0.f)).GetSafeNormal();
					Dir = Dir.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Dir;
					const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector);
					const FVector XY = At + Dir * Vw.Back + Side * (0.25f * FMath::Abs(Vw.Back));
					float Ground = At.Z;
					I->GroundAt(FVector2D(XY), Ground);
					From = FVector(XY.X, XY.Y, FMath::Max(Ground, 0.f) + Vw.Up);
					To = Vw.Back < 0.f ? From + Dir * 3000.f : At + FVector(0.f, 0.f, Vw.LookUp);
				}
				FreeCam->SetActorLocationAndRotation(From, (To - From).Rotation());
				FrameSum = 0.f;
				FrameN = 0;
			}
			if (AutoClock > 1.5f)
			{
				FrameSum += FApp::GetDeltaTime();
				++FrameN;
			}
			if (AutoClock > 2.6f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] plants view %s: %.1f ms a frame"), Vw.Name, 1000.f * FrameSum / FMath::Max(FrameN, 1));
				Shot(Vw.Name);
				++PlantShot;
				Next(460);
			}
		}
		break;
	// Life (-bflife [-bfseed=N]): an island, the T. rex at its middle, herds of Triceratops, Ankylosaurus and Brachiosaurus
	// round it with their needs four times as fast; 100 s watched (a report every 20 s, pictures of each herd), then the
	// T. rex walks at the Triceratops (they outweigh it: they should stand together) and then at the Ankylosaurus (they
	// don't: they should run).
	case 480:
		if (AutoClock > 4.f)
		{
			int32 Seed = 20261112;
			FParse::Value(FCommandLine::Get(), TEXT("bfseed="), Seed);
			MakeIsland(Seed);
			bGoWhenReady = false;
			Next(481);
		}
		break;
	case 481:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = 0;
			Pick(AutoSpecies);
			Next(482);
		}
		break;
	case 482:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Life->NeedRate = 2.f;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: %d animals in %d herds"), Life->Num(), Life->NumHerds());
			SetFreeCam(true);
			LifeShot = 0;
			Next(Life->Num() > 0 ? 483 : 9);
		}
		break;
	case 483:
		{
			// The camera follows one herd at a time, from 35 m off and 14 m up.
			const int32 Herd = int32(AutoClock / 50.f) % FMath::Max(Life->NumHerds(), 1);
			const FVector C = Life->HerdCentre(Herd);
			const FVector From = C + FVector(-3000.f, -2000.f, 1400.f);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
			if (int32(AutoClock / 20.f) > LifeShot / 2)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life at %.0f s:\n%s"), AutoClock, *Life->Report());
				LifeShot += 2;
			}
			const float Clocks[] = { 15.f, 40.f, 60.f, 85.f, 110.f, 140.f };
			for (int32 k = 0; k < 6; ++k)
			{
				if (AutoClock >= Clocks[k] && AutoClock - FApp::GetDeltaTime() < Clocks[k])
				{
					Shot(*FString::Printf(TEXT("life_herd%d_%d"), Herd, k));
				}
			}
			if (AutoClock > 150.f)
			{
				LifeShot = 0;
				Next(484);
			}
		}
		break;
	case 484:      // the T. rex walks at a herd (Triceratops first, then Ankylosaurus) from 90 m, from upwind
	case 485:
		{
			const int32 Herd = AutoStep == 484 ? 0 : 1;
			const FVector C = Life->HerdCentre(Herd);
			if (AutoFrame == 1 && D)
			{
				const FVector Wind(FMath::Cos(FMath::DegreesToRadians(ABFWorld::WindDeg)), FMath::Sin(FMath::DegreesToRadians(ABFWorld::WindDeg)), 0.f);
				FVector At = C - Wind * 9000.f;
				float Ground = 0.f;
				M->GetIsland()->GroundAt(FVector2D(At), Ground);
				D->TeleportTo(FVector(At.X, At.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 50.f), Wind.Rotation());
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life: the T. rex 90 m upwind of the %s, walking at them"), Herd == 0 ? TEXT("Triceratops") : TEXT("Ankylosaurus"));
			}
			if (D && AutoClock > 1.f)
			{
				bAutoForward = true;
				AutoDir = (C - D->GetActorLocation()).GetSafeNormal2D();
				const FVector Mid = 0.5f * (C + D->GetActorLocation());
				const FVector From = Mid + FVector::CrossProduct(AutoDir, FVector::UpVector) * 6000.f + FVector(0.f, 0.f, 2500.f);
				FreeCam->SetActorLocationAndRotation(From, (Mid - From).Rotation());
			}
			if (int32(AutoClock / 5.f) > LifeShot)
			{
				LifeShot = int32(AutoClock / 5.f);
				FString Line;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.Herd == Herd && A.Body.IsValid() && D)
					{
						Line += FString::Printf(TEXT("  %s %s %.0f m from the T. rex (fear %.2f);"), *A.Name, ABFLife::ActName(A.Act),
							FVector::Dist2D(A.Body->GetActorLocation(), D->GetActorLocation()) / 100.f, A.Fear);
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] life threat at %.0f s:%s"), AutoClock, *Line);
				if (LifeShot == 2 || LifeShot == 4)
				{
					Shot(*FString::Printf(TEXT("life_threat%d_%d"), Herd, LifeShot));
				}
			}
			if (AutoClock > 26.f)
			{
				bAutoForward = false;
				AutoDir = FVector::ZeroVector;
				LifeShot = 0;
				if (AutoStep == 485)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] life at the end:\n%s"), *Life->Report());
					SetFreeCam(false);
				}
				Next(AutoStep == 484 ? 485 : 9);
			}
		}
		break;
	// Hunting (-bfhunt [-bfseed=N]): the island, you a Brachiosaurus (no threat to anyone), the herds and the hunters round
	// you, needs twice as fast; the camera follows the T. rex for up to five minutes (a report every 30 s, a picture every
	// 20 s, and two more the moment it charges and the moment it kills), then the raptor pack for a minute.
	case 490:
		if (AutoClock > 4.f)
		{
			int32 Seed = 20261112;
			FParse::Value(FCommandLine::Get(), TEXT("bfseed="), Seed);
			MakeIsland(Seed);
			bGoWhenReady = false;
			Next(491);
		}
		break;
	case 491:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(492);
		}
		break;
	case 492:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Life->NeedRate = 2.f;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] hunt: %d animals in %d herds and packs"), Life->Num(), Life->NumHerds());
			// The check starts the T. rex 120 m downwind of the Ankylosaurus (in the game it finds them itself, slowly).
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				if (Life->Get(i).Name == TEXT("T. rex 1") && Life->Get(i).Body.IsValid() && Life->NumHerds() > 1)
				{
					const FVector Wind(FMath::Cos(FMath::DegreesToRadians(ABFWorld::WindDeg)), FMath::Sin(FMath::DegreesToRadians(ABFWorld::WindDeg)), 0.f);
					const FVector To = Life->HerdCentre(1) + Wind * 12000.f;
					float Ground = 0.f;
					M->GetIsland()->GroundAt(FVector2D(To), Ground);
					ABFDino* Rex = Life->Get(i).Body.Get();
					Rex->TeleportTo(FVector(To.X, To.Y, Ground + Rex->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 50.f), (-Wind).Rotation());
				}
			}
			SetFreeCam(true);
			LifeShot = 0;
			HuntSeen = 0;
			Next(Life->Num() > 0 ? 493 : 9);
		}
		break;
	case 493:
	case 494:
		{
			const TCHAR* Who = AutoStep == 493 ? TEXT("T. rex 1") : TEXT("Raptor 1");
			if (AutoStep == 494 && AutoFrame == 1 && Life->NumCarcasses() > 0)
			{
				// The check puts the raptors 200 m downwind of the kill (the scent comes to them); in the game they come on it.
				const FVector Wind(FMath::Cos(FMath::DegreesToRadians(ABFWorld::WindDeg)), FMath::Sin(FMath::DegreesToRadians(ABFWorld::WindDeg)), 0.f);
				int32 k = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					if (Life->Get(i).Name.StartsWith(TEXT("Raptor")) && Life->Get(i).Body.IsValid())
					{
						const FVector To = Life->CarcassAt(0) + Wind * 20000.f + FVector(0.f, 400.f * k++, 0.f);
						float Ground = 0.f;
						M->GetIsland()->GroundAt(FVector2D(To), Ground);
						ABFDino* R = Life->Get(i).Body.Get();
						R->TeleportTo(FVector(To.X, To.Y, Ground + R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 50.f), (-Wind).Rotation());
					}
				}
			}
			int32 H = INDEX_NONE;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				H = Life->Get(i).Name == Who ? i : H;
			}
			if (H == INDEX_NONE || !Life->Get(H).Body.IsValid())
			{
				Next(AutoStep == 493 ? 494 : 9);
				break;
			}
			const FBFAnimal& A = Life->Get(H);
			const FVector At = A.Body->GetActorLocation();
			// Beside and behind it, 40 m off, 15 m up, looking where it's going (at its prey when it has one).
			const FVector Aim = Life->Get(H).Prey != INDEX_NONE && Life->Get(A.Prey).Body.IsValid() ? 0.5f * (At + Life->Get(A.Prey).Body->GetActorLocation()) : At;
			const FVector From = Aim - A.Body->GetActorForwardVector() * 3000.f + A.Body->GetActorRightVector() * 2500.f + FVector(0.f, 0.f, 1500.f);
			FreeCam->SetActorLocationAndRotation(From, (Aim - From).Rotation());
			if (int32(AutoClock / 30.f) > LifeShot)
			{
				LifeShot = int32(AutoClock / 30.f);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] hunt at %.0f s (%s):\n%s"), AutoClock, Who, *Life->Report());
			}
			const int32 Phase = A.Act == EBFAct::Hunt ? 1 + A.HuntPhase : A.Act == EBFAct::Feast ? 3 : 0;
			if (Phase > HuntSeen)
			{
				HuntSeen = Phase;
				Shot(*FString::Printf(TEXT("hunt_%s_%s"), AutoStep == 493 ? TEXT("rex") : TEXT("raptors"), Phase == 1 ? TEXT("stalking") : Phase == 2 ? TEXT("charging") : TEXT("feeding")));
			}
			if (int32((AutoClock - FApp::GetDeltaTime()) / 20.f) != int32(AutoClock / 20.f))
			{
				Shot(*FString::Printf(TEXT("hunt_%s_%03d"), AutoStep == 493 ? TEXT("rex") : TEXT("raptors"), int32(AutoClock)));
			}
			const bool bDone = AutoStep == 493 ? (AutoClock > 300.f || (A.Act == EBFAct::Feast && A.Thought.StartsWith(TEXT("tearing")) && AutoClock - FeastSince > 20.f))
				: AutoClock > 180.f;
			if (A.Act != EBFAct::Feast)
			{
				FeastSince = AutoClock;
			}
			if (bDone)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] hunt ends (%s, %.0f s):\n%s"), Who, AutoClock, *Life->Report());
				LifeShot = 0;
				HuntSeen = 0;
				if (AutoStep == 494)
				{
					SetFreeCam(false);
				}
				Next(AutoStep == 493 ? 494 : 9);
			}
		}
		break;
	// Watching (-bfwatch): the island, you a Brachiosaurus, the herds and hunters; the panel open (O) and the camera on
	// Trike 1 (N); the clock set to 11:00, 18:20, 22:30 (40 s to settle for the night: how many asleep) and 06:20.
	case 500:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(501);
		}
		break;
	case 501:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(502);
		}
		break;
	case 502:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Life->NeedRate = 2.f;
			bWatchPanel = true;
			WatchBack->SetVisibility(EVisibility::HitTestInvisible);
			WatchI = 0;
			SetFreeCam(true);
			M->GetIsland()->SetHours(11.f);
			Next(503);
		}
		break;
	case 503:
		{
			ABFWorld* I = M->GetIsland();
			struct FAt { float Clock; float Hours; const TCHAR* Shot; };
			static const FAt Plan[] = { { 20.f, -1.f, TEXT("watch_day") }, { 21.f, 18.33f, nullptr }, { 27.f, -1.f, TEXT("watch_dusk") }, { 28.f, 22.5f, nullptr },
				{ 70.f, -1.f, TEXT("watch_night") }, { 71.f, 6.33f, nullptr }, { 77.f, -1.f, TEXT("watch_dawn") } };
			for (const FAt& P : Plan)
			{
				if (AutoClock >= P.Clock && AutoClock - FApp::GetDeltaTime() < P.Clock)
				{
					if (P.Hours >= 0.f)
					{
						I->SetHours(P.Hours);
					}
					else
					{
						int32 Asleep = 0, Alive = 0;
						for (int32 k = 0; k < Life->Num(); ++k)
						{
							Alive += Life->Get(k).bDead ? 0 : 1;
							Asleep += !Life->Get(k).bDead && Life->Get(k).Act == EBFAct::Rest ? 1 : 0;
						}
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] watch %s: %s; resting or asleep %d of %d; daylight %.2f\n%s"), P.Shot, *I->ClockText(), Asleep, Alive,
							I->Daylight(), *Life->WatchText(I->ClockText()));
						Shot(P.Shot);
					}
				}
			}
			if (AutoClock > 78.f)
			{
				SetFreeCam(false);
				WatchI = INDEX_NONE;
				Next(9);
			}
		}
		break;
	// Your needs (-bfneeds): a Triceratops on the island; at the nearest lake shore it drinks four times; by the nearest
	// cycad it crops three mouthfuls; it rests 10 s with its needs twenty times as fast; then, its stomach empty and
	// parched, thirty times as fast: its health runs out.
	case 510:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(511);
		}
		break;
	case 511:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = 1;
			Pick(AutoSpecies);
			Next(512);
		}
		break;
	case 512:
	case 513:
		{
			ABFWorld* I = M->GetIsland();
			if (AutoFrame == 1 && D)
			{
				FVector Stand = FVector::ZeroVector, Water = FVector::ZeroVector, At = FVector::ZeroVector;
				const bool bWater = AutoStep == 512;
				if (bWater ? I->NearestShore(I->StartPoint(), 60000.f, Stand, Water) : I->NearestPlant(FVector2D(I->StartPoint()), EBFPlant::Cycad, At))
				{
					const FVector Target = bWater ? Water : At;
					const FVector From = bWater ? Stand : At + FVector(1.f, 0.f, 0.f) * (D->GetCapsuleComponent()->GetScaledCapsuleRadius() + 260.f);
					const FVector Dir = (Target - From).GetSafeNormal2D();
					// Its snout over the water (or the plant): the body stands its head's reach back from it.
					const float Ahead = FVector::Dist2D(D->GetBoneWorld(TEXT("head")), D->GetActorLocation());
					const FVector Body = Target - Dir * (Ahead + (bWater ? 100.f : 150.f));
					float Ground = 0.f;
					I->GroundAt(FVector2D(Body), Ground);
					D->TeleportTo(FVector(Body.X, Body.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), Dir.Rotation());
				}
				NeedsLog = 0;
			}
			if (AutoClock > 2.f && int32((AutoClock - 2.f) / 1.5f) >= NeedsLog && NeedsLog < (AutoStep == 512 ? 4 : 3))
			{
				++NeedsLog;
				const bool bDid = AutoStep == 512 ? TryDrink() : TryFeed();
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] needs %s %d: %s; hunger %.0f%%, thirst %.0f%%, tired %.0f%%"), AutoStep == 512 ? TEXT("drink") : TEXT("eat"), NeedsLog,
					bDid ? TEXT("done") : TEXT("nothing in reach"), 100.f * (1.f - NeedStomach / FMath::Max(NeedStomachMax, 0.01f)), 100.f * NeedThirst, 100.f * NeedTired);
			}
			if (AutoClock > 9.f)
			{
				Look(150.f, -10.f);
				Shot(AutoStep == 512 ? TEXT("needs_drink") : TEXT("needs_eat"));
				Next(AutoStep + 1);
			}
		}
		break;
	case 514:      // rest
		if (AutoFrame == 1)
		{
			bResting = true;
			NeedTired = 0.8f;
			NeedsRate = 20.f;
		}
		if (AutoClock > 10.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] needs rest: 10 s at 20x: tired %.0f%% (from 80%%), resting %d"), 100.f * NeedTired, bResting ? 1 : 0);
			bResting = false;
			Next(515);
		}
		break;
	case 515:      // starve and parch
		if (AutoFrame == 1)
		{
			NeedStomach = 0.f;
			NeedThirst = 1.f;
			NeedsRate = 30.f;
		}
		if (int32(AutoClock) != int32(AutoClock - FApp::GetDeltaTime()) && Life)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] needs starving at %.0f s: health %.0f%%, limp %d"), AutoClock, 100.f * Life->PlayerHealth, D && D->IsRagdoll() ? 1 : 0);
		}
		if ((D && D->IsRagdoll()) || AutoClock > 20.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] needs end: %s"), Life ? *Life->PlayerNote : TEXT("-"));
			Shot(TEXT("needs_dead"));
			NeedsRate = 1.f;
			Next(9);
		}
		break;
	// The young (-bfyoung): the island, you a Brachiosaurus, the herds and hunters; the young's bodies grown and brought in
	// (up to 8 minutes the first time); a picture of each herd with its young; then Young Trike 1 strays 60 m from its
	// herd and the raptor pack is put 120 m downwind of it: two minutes watched (a line every 5 s).
	case 520:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(521);
		}
		break;
	case 521:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(522);
		}
		break;
	case 522:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Life->NeedRate = 1.f;
			SetFreeCam(true);
			LifeShot = 0;
			Next(523);
		}
		break;
	case 523:
		{
			if (int32(AutoClock / 30.f) > LifeShot)
			{
				LifeShot = int32(AutoClock / 30.f);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] young: waiting for %d kinds of young (%.0f s)"), Life->YoungWaiting(), AutoClock);
			}
			if (Life->YoungWaiting() == 0 || AutoClock > 600.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] young: ready after %.0f s:\n%s"), AutoClock, *Life->Report());
				LifeShot = 0;
				Next(524);
			}
		}
		break;
	case 524:      // each herd with its young
		{
			const int32 Herd = int32(AutoClock / 4.f);
			if (Herd >= 3)
			{
				Next(525);
				break;
			}
			const FVector C = Life->HerdCentre(Herd);
			const FVector From = C + FVector(-2500.f, -1800.f, 900.f);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
			if (int32((AutoClock - FApp::GetDeltaTime()) / 4.f) != Herd && FMath::Frac(AutoClock / 4.f) > 0.7f)
			{
			}
			if (FMath::Frac(AutoClock / 4.f) > 0.8f && LifeShot == Herd)
			{
				Shot(*FString::Printf(TEXT("young_herd%d"), Herd));
				++LifeShot;
			}
		}
		break;
	case 525:
		{
			int32 Young = INDEX_NONE;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				Young = Life->Get(i).Name == TEXT("Young Trike 1") ? i : Young;
			}
			if (Young == INDEX_NONE || !Life->Get(Young).Body.IsValid())
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] young: no Young Trike 1"));
				Next(9);
				break;
			}
			const FVector Wind(FMath::Cos(FMath::DegreesToRadians(ABFWorld::WindDeg)), FMath::Sin(FMath::DegreesToRadians(ABFWorld::WindDeg)), 0.f);
			if (AutoFrame == 1)
			{
				// It strays 60 m from its herd (across the wind), and the raptors are 120 m downwind of it.
				ABFDino* Y = Life->Get(Young).Body.Get();
				const FVector Stray = Life->HerdCentre(0) + FVector::CrossProduct(Wind, FVector::UpVector) * 6000.f;
				float Ground = 0.f;
				M->GetIsland()->GroundAt(FVector2D(Stray), Ground);
				Y->TeleportTo(FVector(Stray.X, Stray.Y, Ground + Y->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), Y->GetActorRotation());
				int32 k = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					if (Life->Get(i).Name.StartsWith(TEXT("Raptor")) && Life->Get(i).Body.IsValid())
					{
						const FVector To = Stray + Wind * 12000.f + FVector(0.f, 400.f * k++, 0.f);
						M->GetIsland()->GroundAt(FVector2D(To), Ground);
						ABFDino* R = Life->Get(i).Body.Get();
						R->TeleportTo(FVector(To.X, To.Y, Ground + R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), (-Wind).Rotation());
					}
				}
				LifeShot = 0;
			}
			const FBFAnimal& Y = Life->Get(Young);
			const FVector YAt = Y.Body.IsValid() ? Y.Body->GetActorLocation() : FVector::ZeroVector;
			const FVector From = YAt + FVector(-3200.f, 2600.f, 4200.f);      // high enough to clear the crowns
			FreeCam->SetActorLocationAndRotation(From, (YAt - From).Rotation());
			if (int32(AutoClock / 5.f) > LifeShot)
			{
				LifeShot = int32(AutoClock / 5.f);
				FString Line = FString::Printf(TEXT("Young Trike 1 %s (fear %.2f, health %.0f%%)"), Y.bDead ? TEXT("DEAD") : ABFLife::ActName(Y.Act), Y.Fear, 100.f * Y.Health);
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if ((A.Name.StartsWith(TEXT("Raptor")) || i == Y.Mother) && A.Body.IsValid())
					{
						Line += FString::Printf(TEXT("; %s %s %.0f m from it"), *A.Name, ABFLife::ActName(A.Act), FVector::Dist2D(A.Body->GetActorLocation(), YAt) / 100.f);
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] young at %.0f s: %s"), AutoClock, *Line);
				if (LifeShot % 2 == 0)
				{
					Shot(*FString::Printf(TEXT("young_stray_%03d"), int32(AutoClock)));
				}
			}
			if (AutoClock > 120.f || Y.bDead)
			{
				if (Y.bDead)
				{
					Shot(TEXT("young_taken"));
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] young end:\n%s"), *Life->Report());
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Growing up (-bfgrow): the island, you a Brachiosaurus, the herds (with their juveniles) and two hatchlings more for
	// each herd; growing up 600 times as fast (ten hours in a minute); a picture of the Triceratops herd every 15 s and a
	// report every 15 s, for 150 s once every body is in.
	case 530:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(531);
		}
		break;
	case 531:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(532);
		}
		break;
	case 532:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Life->bHunters = false;
			Life->YoungDeathRate = 0.f;
			for (int32 H = 0; H < Life->NumHerds(); ++H)
			{
				Life->WantYoung(H, 1);
			}
			SetFreeCam(true);
			LifeShot = 0;
			Next(533);
		}
		break;
	case 533:      // every body in (the hatchlings', and the next stages' asked for ahead)
		if (AutoFrame == 1)
		{
			for (const TCHAR* B : { TEXT("trike"), TEXT("ankylo"), TEXT("brachio") })
			{
				for (int32 St = 1; St <= 3; ++St)
				{
					Life->Prefetch(ABFLife::StageKey(B, St));
				}
			}
		}
		if ((Life->BodiesWaiting() == 0 && Life->YoungWaiting() == 0) || AutoClock > 900.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] grow: every body in after %.0f s:\n%s"), AutoClock, *Life->Report());
			Life->GrowRate = 600.f;
			LifeShot = 0;
			Next(534);
		}
		break;
	case 534:
		{
			const FVector C = Life->HerdCentre(0);
			const FVector From = C + FVector(-2600.f, -2000.f, 1200.f);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
			if (int32(AutoClock / 15.f) > LifeShot)
			{
				LifeShot = int32(AutoClock / 15.f);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] grow at %.0f s (%.1f h of growing):\n%s"), AutoClock, AutoClock * 600.f / 3600.f, *Life->Report());
				Shot(*FString::Printf(TEXT("grow_%03d"), int32(AutoClock)));
			}
			if (AutoClock > 150.f)
			{
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Saving (-bfsave): the island, you a Brachiosaurus, the herds with their young (growing 30 times as fast, eating 4
	// times as fast for a minute, so ages, plants and places all change); a snapshot, saved; then everything gone (the
	// animals, the island, you back in the lab) and the island made again: is it all back as it was?
	case 540:
		if (AutoClock > 4.f)
		{
			const FString Path = IslandSavePath(20261112);
			SaveBackup.Reset();
			if (FPaths::FileExists(Path))
			{
				SaveBackup = Path + TEXT(".bak");
				IFileManager::Get().Move(*SaveBackup, *Path);
			}
			bSaveCheck = true;
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(541);
		}
		break;
	case 541:
		if (M->GetIsland() && M->GetIsland()->IsReady() && RestoredSeed == M->GetIsland()->GetSeed())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(542);
		}
		break;
	case 542:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Next(543);
		}
		break;
	case 543:
		if ((Life->BodiesWaiting() == 0 && Life->YoungWaiting() == 0 && AutoClock > 2.f) || AutoClock > 900.f)
		{
			Life->GrowRate = 30.f;
			Life->NeedRate = 4.f;
			M->GetIsland()->SetHours(15.5f);
			Next(544);
		}
		break;
	case 544:
		if (AutoClock > 60.f)
		{
			ABFWorld* I = M->GetIsland();
			SaveSnapshot.Reset();
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (A.Body.IsValid() && !A.bDead && !A.bPlayer)
				{
					const FVector P = A.Body->GetActorLocation();
					SaveSnapshot.Add(FString::Printf(TEXT("%s|%d|%.0f|%.0f|%.0f|%s|%.2f"), *A.Name, A.Stage, A.AgeS, P.X, P.Y, *A.Body->GetSpecies().Key, A.Health));
				}
			}
			SaveSnapClock = I->ClockText();
			SaveSnapEaten = I->NumRegrowing();
			SaveSnapFood = I->FoodLeftKg();
			SaveSnapYou = D ? D->GetActorLocation() : FVector::ZeroVector;
			Life->GrowRate = 1.f;
			Life->NeedRate = 1.f;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: before: %s, %d plants eaten, %.0f kg of leaves left, %d animals, you at %s:\n%s"), *SaveSnapClock, SaveSnapEaten,
				SaveSnapFood, SaveSnapshot.Num(), *SaveSnapYou.ToString(), *Life->Report());
			const bool bSaved = SaveIsland(true);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s saved: %s"), bSaved ? TEXT("PASS") : TEXT("FAIL"), *IslandSavePath(I->GetSeed()));
			Shot(TEXT("save_before"));
			Next(545);
		}
		break;
	case 545:
		if (AutoClock > 2.f)
		{
			// Gone: the animals, the island (made again from its seed), you back in the lab as something else.
			Life->Clear();
			RestoredSeed = INDEX_NONE;
			Area = EBFSpawnArea::Flat;
			Pick(0);
			M->MakeIsland(20261112);
			bGoWhenReady = true;
			Next(546);
		}
		break;
	case 546:
		if (M->GetIsland() && M->GetIsland()->IsReady() && RestoredSeed == M->GetIsland()->GetSeed() && AutoFrame > 0 && SaveSnapEaten >= 0)
		{
			ABFWorld* I = M->GetIsland();
			if (SaveSnapClock.Len() > 0)
			{
				// The island itself, at once (before its clock moves on).
				const bool bClock = I->ClockText() == SaveSnapClock;
				const bool bEaten = I->NumRegrowing() == SaveSnapEaten;
				const bool bFood = FMath::Abs(I->FoodLeftKg() - SaveSnapFood) <= 0.005f * SaveSnapFood + 1.f;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s clock %s (was %s)"), bClock ? TEXT("PASS") : TEXT("FAIL"), *I->ClockText(), *SaveSnapClock);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s plants eaten %d (was %d), leaves left %.0f kg (was %.0f)"), bEaten && bFood ? TEXT("PASS") : TEXT("FAIL"),
					I->NumRegrowing(), SaveSnapEaten, I->FoodLeftKg(), SaveSnapFood);
				SaveSnapClock.Reset();
				AutoClock = 0.f;
				SaveBackAt.Reset();
			}
			// Where each stands the frame its body is back (they walk on afterwards).
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				if (Life->Get(i).Body.IsValid() && !SaveBackAt.Contains(Life->Get(i).Name))
				{
					SaveBackAt.Add(Life->Get(i).Name, Life->Get(i).Body->GetActorLocation());
				}
			}
			if (Life->NumAwaiting() == 0 && !bGoWhenReady && AutoClock > 3.f)
			{
				int32 Found = 0, Bad = 0;
				for (const FString& Line : SaveSnapshot)
				{
					TArray<FString> F;
					Line.ParseIntoArray(F, TEXT("|"));
					int32 Hit = INDEX_NONE;
					for (int32 i = 0; i < Life->Num(); ++i)
					{
						Hit = Life->Get(i).Name == F[0] ? i : Hit;
					}
					if (Hit == INDEX_NONE || !Life->Get(Hit).Body.IsValid())
					{
						++Bad;
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: FAIL %s not back"), *F[0]);
						continue;
					}
					++Found;
					const FBFAnimal& A = Life->Get(Hit);
					const FVector* Back = SaveBackAt.Find(A.Name);
					const FVector P = Back ? *Back : A.Body->GetActorLocation();
					const float Moved = FVector::Dist2D(P, FVector(FCString::Atof(*F[3]), FCString::Atof(*F[4]), 0.f));
					const bool bOk = A.Stage == FCString::Atoi(*F[1]) && FMath::Abs(A.AgeS - FCString::Atof(*F[2])) < 200.f && Moved < 300.f
						&& A.Body->GetSpecies().Key == F[5] && FMath::Abs(A.Health - FCString::Atof(*F[6])) < 0.02f;
					Bad += bOk ? 0 : 1;
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s %s: stage %d (was %s), %.2f h old (was %.2f), %s, %.1f m from where it was, health %.2f (was %s)"),
						bOk ? TEXT("PASS") : TEXT("FAIL"), *A.Name, A.Stage, *F[1], A.AgeS / 3600.f, FCString::Atof(*F[2]) / 3600.f, *A.Body->GetSpecies().Key, Moved / 100.f, A.Health, *F[6]);
				}
				const float YouMoved = D ? FVector::Dist2D(D->GetActorLocation(), SaveSnapYou) : 1e9f;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s you: back as %s, %.1f m from where you were"), D && D->GetSpecies().Key == TEXT("brachio") && YouMoved < 300.f ? TEXT("PASS") : TEXT("FAIL"),
					D ? *D->GetSpecies().Key : TEXT("-"), YouMoved / 100.f);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s animals back: %d of %d (%d wrong); %d now on the island"), Bad == 0 && Found == SaveSnapshot.Num() ? TEXT("PASS") : TEXT("FAIL"),
					Found, SaveSnapshot.Num(), Bad, Life->NumAlive());
				Shot(TEXT("save_after"));
				Next(547);
			}
		}
		break;
	case 547:
		if (AutoClock > 30.f)
		{
			// Thirty seconds on, they're living again (thinking, moving); then the check's save goes (and any kept one comes back).
			int32 Busy = 0;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				Busy += Life->Get(i).Body.IsValid() && Life->Get(i).Decisions > 0 ? 1 : 0;
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] save check: %s %d of %d have decided something since coming back:\n%s"), Busy > Life->NumAlive() / 2 ? TEXT("PASS") : TEXT("FAIL"),
				Busy, Life->NumAlive(), *Life->Report());
			IFileManager::Get().Delete(*IslandSavePath(20261112));
			if (!SaveBackup.IsEmpty())
			{
				IFileManager::Get().Move(*IslandSavePath(20261112), *SaveBackup);
			}
			bSaveCheck = false;
			Next(9);
		}
		break;

	// How many animals the island holds (-bfstress): the island, you a Brachiosaurus, the herds and hunters; every 20 s
	// fifteen more (a herd each of Triceratops, Ankylosaurus, Brachiosaurus, raptors and a T. rex, from 300 m to 2 km off);
	// over each step's last 15 s the frame's time (whole, game thread, render thread, graphics card), their minds' share,
	// memory; until a frame takes 50 ms (20 a second) or there are 600. Then what the plants could feed.
	case 550:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(551);
		}
		break;
	case 551:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(552);
		}
		break;
	case 552:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			ToggleLife();
			StressStep = StressFrames = StressHerds = 0;
			StressTable = TEXT("animals | frame ms (avg / 99th / worst) | game thread | render thread | GPU | their minds ms | memory MB");
			StressLast = 0.0;
			Next(553);
		}
		break;
	case 553:
		{
			const double NowT = FPlatformTime::Seconds();
			if (AutoClock > 5.f)
			{
				if (StressFrames == 0)
				{
					Life->TickSecs = Life->TickWorst = 0.0;
					Life->TickFrames = 0;
					StressFrameSum = StressWorst = StressGame = StressRender = StressGpu = 0.0;
					StressFrameList.Reset();
				}
				if (StressLast > 0.0)
				{
					const double Ms = 1000.0 * (NowT - StressLast);
					StressFrameSum += Ms;
					StressWorst = FMath::Max(StressWorst, Ms);
					StressFrameList.Add(Ms);
					StressGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
					StressRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
					StressGpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
					++StressFrames;
				}
			}
			StressLast = NowT;
			if (AutoClock > 20.f && StressFrames > 0)
			{
				StressFrameList.Sort();
				const double Avg = StressFrameSum / StressFrames, P99 = StressFrameList[FMath::Min(StressFrameList.Num() - 1, int32(0.99 * StressFrameList.Num()))];
				const double Minds = Life->TickFrames > 0 ? 1000.0 * Life->TickSecs / Life->TickFrames : 0.0;
				const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
				const int32 Alive = Life->NumAlive();
				const FString Row = FString::Printf(TEXT("%d | %.1f / %.1f / %.1f | %.1f | %.1f | %.1f | %.2f (worst %.1f) | %.0f"), Alive, Avg, P99, StressWorst,
					StressGame / StressFrames, StressRender / StressFrames, StressGpu / StressFrames, Minds, 1000.0 * Life->TickWorst, Mem.UsedPhysical / 1048576.0);
				StressTable += TEXT("\n") + Row;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] stress: %s  (%d herds, %d carcasses, leaves left %.1f%%)"), *Row, Life->NumHerds(), Life->NumCarcasses(),
					100.f * M->GetIsland()->FoodLeftKg() / FMath::Max(M->GetIsland()->FoodWholeKg(), 1.f));
				Shot(*FString::Printf(TEXT("stress_%03d"), Alive));
				if (Avg > 50.0 || Alive >= 600 || StressStep >= 40)
				{
					Next(554);
					break;
				}
				// Fifteen more: a herd of each, somewhere 300 m to 2 km from you.
				FRandomStream R(9000 + StressStep);
				const FVector Near = D ? D->GetActorLocation() : M->GetIsland()->StartPoint();
				struct FMore { const TCHAR* Key; int32 Count; bool bHunter; };
				for (const FMore& More : { FMore{ TEXT("trike"), 5, false }, FMore{ TEXT("ankylo"), 3, false }, FMore{ TEXT("brachio"), 3, false }, FMore{ TEXT("raptor"), 3, true }, FMore{ TEXT("trex"), 1, true } })
				{
					StressHerds += Life->AddHerd(More.Key, More.Count, Near, R.FRandRange(30000.f, 200000.f), R.FRandRange(0.f, 360.f), More.bHunter, R, false) > 0 ? 1 : 0;
				}
				++StressStep;
				StressFrames = 0;
				StressLast = 0.0;
				Next(553);
			}
		}
		break;
	case 554:
		{
			// What the plants could feed: each plant eater's need (its stomach's worth every quarter hour) against the most
			// the leaves in its reach can grow back.
			ABFWorld* I = M->GetIsland();
			FString Food = FString::Printf(TEXT("leaves on the island %.0f t whole, %.1f%% left now; %d plants eaten and growing back"), I->FoodWholeKg() / 1000.f,
				100.f * I->FoodLeftKg() / FMath::Max(I->FoodWholeKg(), 1.f), I->NumRegrowing());
			for (const TCHAR* Key : { TEXT("trike"), TEXT("ankylo"), TEXT("brachio") })
			{
				const int32 Si = M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Key; });
				if (Si == INDEX_NONE)
				{
					continue;
				}
				const FBFSpecies& S = M->GetSpecies()[Si];
				const float Need = FMath::Max(2.f, 0.004f * S.MassKg) / 900.f;
				const float Reach = 1.1f * S.Bounds.GetSize().Z;
				const float Grow = I->RegrowKgPerS(Reach);
				Food += FString::Printf(TEXT("\n  %s (%.1f t): eats %.2f kg a minute; leaves in its reach (up to %.1f m) grow back %.0f kg a minute at most: the island could feed %.0f of them alone"),
					Key, S.MassKg / 1000.f, 60.f * Need, Reach / 100.f, 60.f * Grow, Grow / Need);
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] stress: done after %d steps:\n%s\n%s"), StressStep, *StressTable, *Food);
			Next(9);
		}
		break;
	// Breeding (-bfbreed): the island, you a Brachiosaurus, the herds and hunters (no young); every adult female in
	// condition, the breeding and growing clocks 60 times as fast (from mating to laying 10 s, in the egg a minute); the
	// camera on whatever is happening (a male displaying, a nest, the hatchlings); once a Brachiosaurus has laid and left
	// her eggs, the raptors are put 100 m downwind of them, hungry. Seven minutes at most; a line every 15 s.
	case 560:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(561);
		}
		break;
	case 561:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(562);
		}
		break;
	case 562:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			ToggleLife();
			Life->GrowRate = FParse::Param(FCommandLine::Get(), TEXT("bfslow")) ? 1.f : 60.f;     // -bfslow: at the game's own pace
			for (int32 H = 0; H < Life->NumHerds(); ++H)
			{
				Life->MakeReady(H);
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] breed: start:\n%s"), *Life->Report());
			SetFreeCam(true);
			LifeShot = 0;
			StressStep = 0;        // (which pictures are taken: 1 courting, 2 a nest, 4 hatchlings, 8 the raiders)
			StressHerds = 0;       // the raptors moved
			Next(563);
		}
		break;
	case 563:
		{
			// The camera: hatchlings, else a nest with eggs, else a male displaying.
			int32 Court = INDEX_NONE;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				Court = A.Body.IsValid() && !A.bDead && A.Act == EBFAct::Court && A.DisplayS > 1.f ? i : Court;
			}
			int32 Nest = INDEX_NONE, Raid = INDEX_NONE;
			for (int32 n = 0; n < Life->NumNests(); ++n)
			{
				Nest = Life->GetNest(n).Care == 1 && (Life->GetNest(n).Eggs > 0 || Life->GetNest(n).Hatched > 0) && (Nest == INDEX_NONE || !(StressStep & 2)) ? n : Nest;     // a guarded one (open ground)
				Raid = Raid == INDEX_NONE && Life->GetNest(n).Care == 0 && Life->GetNest(n).Eggs > 0 ? n : Raid;
			}
			if (StressHerds == 1)
			{
				for (int32 n = 0; n < Life->NumNests(); ++n)
				{
					Nest = Life->GetNest(n).Care == 0 && (Life->GetNest(n).Eggs > 0 || Life->GetNest(n).Eaten > 0) ? n : Nest;     // the raiders' nest
				}
			}
			FVector CamAt = Life->HerdCentre(0);
			float Back = 2600.f;
			const TCHAR* Tag = nullptr;
			int32 Bit = 0;
			if (Nest != INDEX_NONE)
			{
				CamAt = Life->GetNest(Nest).At;
				Back = Life->GetNest(Nest).Hatched > 0 ? 1400.f : 900.f;
				Tag = Life->GetNest(Nest).Hatched > 0 ? TEXT("breed_hatch") : TEXT("breed_nest");
				Bit = Life->GetNest(Nest).Hatched > 0 ? 4 : 2;
			}
			else if (Court != INDEX_NONE)
			{
				CamAt = Life->Get(Court).Body->GetActorLocation();
				Back = 2000.f;
				Tag = TEXT("breed_court");
				Bit = 1;
			}
			// A clear view: round the target in eighths and up to twice as high, the first from which nothing (a trunk, a
			// crown) stands between the camera and it; the second nest picture starts from the other side.
			FVector From = CamAt + FVector(-0.8f * Back, -0.6f * Back, 0.45f * Back);
			{
				FCollisionQueryParams Q(SCENE_QUERY_STAT(BFBreedCam), false);
				for (TActorIterator<ABFDino> It(GetWorld()); It; ++It)
				{
					Q.AddIgnoredActor(*It);        // (animals don't block the view of themselves)
				}
				const float Start = StressFrames == 3 ? 180.f : 0.f;
				bool bClear = false;
				for (int32 Up = 0; Up < 3 && !bClear; ++Up)
				{
					for (int32 k = 0; k < 8 && !bClear; ++k)
					{
						const float Ang = FMath::DegreesToRadians(217.f + Start + 45.f * k);
						const FVector Try = CamAt + FVector(FMath::Cos(Ang) * Back, FMath::Sin(Ang) * Back, (0.45f + 0.5f * Up) * Back);
						FHitResult Hit;
						if (!GetWorld()->LineTraceSingleByChannel(Hit, CamAt + FVector(0.f, 0.f, 150.f), Try, ECC_Visibility, Q))
						{
							From = Try;
							bClear = true;
						}
					}
				}
			}
			FreeCam->SetActorLocationAndRotation(From, (CamAt - From).Rotation());
			if (Tag && !(StressStep & Bit))
			{
				StressStep |= Bit;
				LifeShot = int32(AutoClock) + 2;          // a picture two seconds on (the camera settled)
				StressFrames = Bit;
			}
			if (LifeShot > 0 && AutoClock >= LifeShot)
			{
				Shot(StressFrames == 4 ? TEXT("breed_hatch") : StressFrames == 2 ? TEXT("breed_nest") : StressFrames == 3 ? TEXT("breed_nest2") : StressFrames == 8 ? TEXT("breed_raid") : TEXT("breed_court"));
				LifeShot = 0;
				if (StressFrames == 2)
				{
					StressFrames = 3;
					LifeShot = int32(AutoClock) + 2;
				}
			}
			// A Brachiosaurus nest left to itself: the raptors put 100 m downwind of it, hungry.
			if (Raid != INDEX_NONE && StressHerds == 0 && Life->GrowRate > 1.f)
			{
				StressHerds = 1;
				const FVector Wind(FMath::Cos(FMath::DegreesToRadians(ABFWorld::WindDeg)), FMath::Sin(FMath::DegreesToRadians(ABFWorld::WindDeg)), 0.f);
				const FVector At0 = Life->GetNest(Raid).At;
				int32 k = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					FBFAnimal& R = Life->Edit(i);
					if (R.Base == TEXT("raptor") && R.Stage >= 4 && R.Body.IsValid() && !R.bDead)
					{
						const FVector To = At0 + Wind * 10000.f + FVector(0.f, 400.f * k++, 0.f);
						float Ground = 0.f;
						M->GetIsland()->GroundAt(FVector2D(To), Ground);
						R.Body->TeleportTo(FVector(To.X, To.Y, Ground + R.Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), (-Wind).Rotation());
						R.StomachKg = 0.1f * R.StomachMaxKg;
						R.Path.Reset();
						R.bGoal = false;
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] breed: %d raptors put 100 m downwind of a Brachiosaurus nest (%d eggs), hungry"), k, Life->GetNest(Raid).Eggs);
			}
			if (Life->EggsEaten > 0 && !(StressStep & 8))
			{
				StressStep |= 8;
				LifeShot = int32(AutoClock) + 1;
				StressFrames = 8;
			}
			if (int32(AutoClock / 15.f) != int32((AutoClock - DeltaTime) / 15.f))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] breed at %.0f s: %s"), AutoClock, *Life->BreedingText());
			}
			if (AutoClock > (Life->GrowRate > 1.f ? 420.f : 1260.f) || (Life->GrowRate > 1.f && Life->Births >= 6 && Life->EggsEaten > 0 && AutoClock > 240.f))
			{
				Next(564);
			}
		}
		break;
	case 564:
		{
			auto Line = [&](bool bOk, const FString& What) { UE_LOG(LogTemp, Display, TEXT("[BoneForge] breed check: %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What); };
			Line(Life->NumMatings > 0, FString::Printf(TEXT("matings: %d"), Life->NumMatings));
			Line(Life->Contests > 0, FString::Printf(TEXT("rival contests: %d"), Life->Contests));
			Line(Life->EggsLaid > 0, FString::Printf(TEXT("eggs laid: %d in %d nests"), Life->EggsLaid, Life->NumNests()));
			Line(Life->Births > 0 || Life->GrowRate <= 1.f, FString::Printf(TEXT("hatched: %d%s"), Life->Births, Life->GrowRate <= 1.f ? TEXT(" (at the game's pace eggs take an hour)") : TEXT("")));
			Line(Life->EggsEaten > 0, FString::Printf(TEXT("eggs eaten by raiders: %d"), Life->EggsEaten));
			{
				// Inheritance: the young's genes spread, each near its parents' mean.
				int32 Kids = 0, Near = 0, Both = 0, MaxGen = 0;
				float Lo = 9.f, Hi = 0.f;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& Y = Life->Get(i);
					if (Y.Generation < 1)
					{
						continue;
					}
					++Kids;
					MaxGen = FMath::Max(MaxGen, Y.Generation);
					Lo = FMath::Min(Lo, Y.Genes.Size);
					Hi = FMath::Max(Hi, Y.Genes.Size);
					if (Y.Mother != INDEX_NONE && Y.Father != INDEX_NONE)
					{
						++Both;
						Near += FMath::Abs(Y.Genes.Size - 0.5f * (Life->Get(Y.Mother).Genes.Size + Life->Get(Y.Father).Genes.Size)) < 0.06f ? 1 : 0;
					}
				}
				Line(Kids > 0 && Hi - Lo > 0.005f && Near == Both, FString::Printf(TEXT("inheritance: %d young of generation 1 to %d, sizes %.2f to %.2f; %d of the %d with both parents known near their mean"), Kids, MaxGen, Lo, Hi, Near, Both));
			}
			FString Nests;
			for (int32 n = 0; n < Life->NumNests(); ++n)
			{
				const FBFNest& N = Life->GetNest(n);
				Nests += FString::Printf(TEXT("\n  %s nest (care %d): laid %d, hatched %d, eaten %d, %d left, %.0f%% of the way"), *N.Base, N.Care, N.Laid, N.Hatched, N.Eaten, N.Eggs,
					100.f * N.IncubS / Life->Incubation);
			}
			const FString Saved = [&]() { FString T; FJsonSerializer::Serialize(Life->SaveState(), TJsonWriterFactory<>::Create(&T)); return T; }();
			Line(Saved.Contains(TEXT("\"nests\"")) && Saved.Contains(TEXT("\"female\"")), FString::Printf(TEXT("the save holds nests and sexes (%d KB)"), Saved.Len() / 1024));
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] breed: done after %.0f s: %s%s\n%s"), AutoClock, *Life->BreedingText(), *Nests, *Life->Report());
			SetFreeCam(false);
			Next(9);
		}
		break;
	// Many generations (-bfgens [-bfgenmin=90]): the island, you a Brachiosaurus, the herds and hunters, the growth clock 120
	// times as fast (growing up in 4.5 minutes, a life about 15), room for 260; each kind's genes logged every minute to
	// Saved/AutoTest/gens.csv (mean and spread of size, hue, shade, strength, speed; the generations), a picture of the
	// biggest herd every 15 minutes.
	case 570:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(571);
		}
		break;
	case 571:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(572);
		}
		break;
	case 572:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			ToggleLife();
			// Lives 60 times as fast (growing up in 9 minutes, a life about 30), needs and plants at the game's own pace (faster
			// needs outran the walking: animals died walking to water); a hunter's meal still lasts about half a life.
			Life->GrowRate = 60.f;
			Life->MaxAnimals = 260;
			for (int32 H = 0; H < Life->NumHerds(); ++H)
			{
				Life->MakeReady(H);
			}
			const FString Csv = FPaths::Combine(AutoOut, TEXT("gens.csv"));
			FFileHelper::SaveStringToFile(TEXT("seconds,kind,alive,adults,mean_gen,max_gen,size,size_sd,hue,hue_sd,shade,shade_sd,strength,strength_sd,speed,speed_sd\n") + Life->GeneRows(0.f), *Csv);
			SetFreeCam(true);
			LifeShot = 0;
			Next(573);
		}
		break;
	case 573:
		{
			int32 Minutes = 90;
			FParse::Value(FCommandLine::Get(), TEXT("bfgenmin="), Minutes);
			if (int32(AutoClock / 60.f) != int32((AutoClock - DeltaTime) / 60.f))
			{
				const FString Rows = Life->GeneRows(AutoClock);
				FFileHelper::SaveStringToFile(Rows, *FPaths::Combine(AutoOut, TEXT("gens.csv")), FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] gens at %.0f min: %s%s\n%s"), AutoClock / 60.f, *Life->BreedingText(), *Life->WhyNotText(), *Rows);
			}
			// The line-ups: at 20 s, every 30 minutes and at the end, one grown animal of each kind (its latest generation)
			// put side by side on open ground near the island's middle, all facing the same way, minds held still; a
			// picture from the same place each time; then each back where it was.
			if (AutoClock > 60.f * Minutes && !bLineupFinal && LineupPhase == 0)
			{
				bLineupFinal = true;
				LineupNextS = AutoClock;
			}
			if (LineupPhase == 0 && AutoClock >= LineupNextS)
			{
				LineupNextS += 1800.f;
				LineupBack.Reset();
				TArray<int32> Pick;
				for (const TCHAR* Kind : { TEXT("brachio"), TEXT("trike"), TEXT("trex"), TEXT("ankylo"), TEXT("raptor") })
				{
					int32 Best = INDEX_NONE;
					for (int32 i = 0; i < Life->Num(); ++i)
					{
						const FBFAnimal& A = Life->Get(i);
						if (!A.bDead && A.Body.IsValid() && !A.Body->IsRagdoll() && A.Stage >= 4 && A.Base == Kind && (Best == INDEX_NONE || A.Generation > Life->Get(Best).Generation))
						{
							Best = i;
						}
					}
					if (Best != INDEX_NONE)
					{
						Pick.Add(Best);
					}
				}
				float Row = 0.f;
				for (const int32 i : Pick)
				{
					Row += Life->Get(i).Body->BodyLengthCm() * Life->Get(i).Genes.Size + 400.f;
				}
				if (LineupSpot.IsZero())
				{
					// Once: open, dry, gentle ground with no trees along an 80 m row (a Brachiosaurus can stand on every
					// point of it) and 60 m clear in front of it for the camera; outward from the island's middle.
					ABFWorld* I = M->GetIsland();
					const FBFSpecies& Big = M->GetSpecies()[FMath::Max(AutoSpecies, 0)];
					auto Open = [&](const FVector& P, bool bStand)
					{
						return I->CoverAt(FVector2D(P)) < 0.02f && (!bStand || I->Walkable(P, Big, 0.f));
					};
					LineupSpot = I->StartPoint();
					bool bFound = false;
					for (int32 Ring = 0; Ring < 30 && !bFound; ++Ring)
					{
						for (int32 k = 0; k < 24 && !bFound; ++k)
						{
							const float Ang = 2.f * PI * k / 24.f;
							const FVector P = I->StartPoint() + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * (2500.f * Ring);
							bool bOk = true;
							for (float t = -4000.f; t <= 4000.f && bOk; t += 500.f)
							{
								bOk = Open(P + FVector(t, 0.f, 0.f), true) && Open(P + FVector(t, 1500.f, 0.f), false);
							}
							for (float d = 500.f; d <= 6000.f && bOk; d += 500.f)
							{
								bOk = Open(P + FVector(0.f, -d, 0.f), false) && Open(P + FVector(-2500.f, -d, 0.f), false) && Open(P + FVector(2500.f, -d, 0.f), false);
							}
							if (bOk)
							{
								LineupSpot = P;
								bFound = true;
							}
						}
					}
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] gens lineup: %s place at %s"), bFound ? TEXT("an open") : TEXT("NO open"), *LineupSpot.ToString());
				}
				const FVector Spot = LineupSpot;
				float X = Spot.X - 0.5f * Row;
				Life->bFrozen = true;
				LineupHours = M->GetIsland()->GetHours();
				M->GetIsland()->SetHours(12.f);
				FString Who;
				for (const int32 i : Pick)
				{
					ABFDino* B = Life->Get(i).Body.Get();
					LineupBack.Add(TPair<int32, FTransform>(i, B->GetActorTransform()));
					const float Len = B->BodyLengthCm() * Life->Get(i).Genes.Size;
					float Ground = Spot.Z;
					M->GetIsland()->GroundAt(FVector2D(X + 0.5f * Len, Spot.Y), Ground);
					B->TeleportTo(FVector(X + 0.5f * Len, Spot.Y, Ground + B->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f), FRotator::ZeroRotator);
					X += Len + 400.f;
					const FBFAnimal& A = Life->Get(i);
					Who += FString::Printf(TEXT("\n  %s: generation %d, %s, %.0f kg"), *A.Name, A.Generation, *ABFLife::GenesText(A.Genes), A.MassKg);
				}
				float GroundC = Spot.Z;
				M->GetIsland()->GroundAt(FVector2D(Spot), GroundC);
				const float Dist = FMath::Max(0.7f * Row, 3500.f);
				LineupLook = FVector(Spot.X, Spot.Y, GroundC + 500.f);
				LineupCam = FVector(Spot.X, Spot.Y - Dist, GroundC + 300.f + 0.12f * Dist);
				++LineupNo;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] gens lineup %d at %.0f min:%s"), LineupNo, AutoClock / 60.f, *Who);
				LineupPhase = 1;
				LineupClock = 0.f;
			}
			if (LineupPhase > 0)
			{
				LineupClock += DeltaTime;
				FreeCam->SetActorLocationAndRotation(LineupCam, (LineupLook - LineupCam).Rotation());
				if (LineupPhase == 1 && LineupClock > 2.f)
				{
					Shot(*FString::Printf(TEXT("gens_lineup%d_%03dmin"), LineupNo, int32(AutoClock / 60.f)));
					LineupPhase = 2;
					LineupClock = 0.f;
				}
				else if (LineupPhase == 2 && LineupClock > 0.5f)
				{
					for (const TPair<int32, FTransform>& Back : LineupBack)
					{
						if (Life->IsValidAnimal(Back.Key))
						{
							Life->Get(Back.Key).Body->TeleportTo(Back.Value.GetLocation(), Back.Value.Rotator());
						}
					}
					Life->bFrozen = false;
					M->GetIsland()->SetHours(LineupHours);
					LineupPhase = 0;
				}
				break;
			}
			// The camera over the biggest herd; a picture every 15 minutes.
			int32 Big = 0;
			int32 Most = 0;
			TArray<int32> Count;
			Count.Init(0, Life->NumHerds());
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (!A.bDead && A.Body.IsValid() && Count.IsValidIndex(A.Herd) && ++Count[A.Herd] > Most)
				{
					Most = Count[A.Herd];
					Big = A.Herd;
				}
			}
			const FVector C = Life->HerdCentre(Big);
			const FVector From = C + FVector(-3000.f, -2200.f, 1600.f);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
			if (int32(AutoClock / 900.f) > LifeShot)
			{
				LifeShot = int32(AutoClock / 900.f);
				Shot(*FString::Printf(TEXT("gens_%03dmin"), int32(AutoClock / 60.f)));
			}
			if ((AutoClock > 60.f * Minutes && bLineupFinal) || Life->NumAlive() == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] gens: done after %.0f min: %s\n%s"), AutoClock / 60.f, *Life->BreedingText(), *Life->Report());
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// One island day (-bfday): the island from 4:30 in the morning, you a Brachiosaurus, the herds with their young and
	// the hunters, everything at the game's own pace; each island hour what every kind is doing, the counts (the herds'
	// moves, watches, play, ambushes, charges and kills); pictures of the first herd at 6:00, 12:00, 18:00 and 22:00.
	case 580:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(581);
		}
		break;
	case 581:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(582);
		}
		break;
	case 582:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Next(583);
		}
		break;
	case 583:
		if (Life->YoungWaiting() == 0 || AutoClock > 600.f)
		{
			M->GetIsland()->SetHours(4.5f);
			SetFreeCam(true);
			LifeShot = 4;
			Next(584);
		}
		break;
	case 584:
		{
			const float Hr = M->GetIsland()->GetHours();
			if (int32(Hr) != LifeShot)
			{
				LifeShot = int32(Hr);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] day at %02d:00 (%s): %s%s"), LifeShot, *M->GetIsland()->ClockText(), *Life->BreedingText(), *Life->ActsText());
				if (LifeShot == 6 || LifeShot == 12 || LifeShot == 18 || LifeShot == 22)
				{
					const FVector C = Life->HerdCentre(0);
					const FVector From = C + FVector(-2600.f, -2000.f, 1300.f);
					FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
					Shot(*FString::Printf(TEXT("day_%02dh"), LifeShot));
				}
			}
			const FVector C = Life->HerdCentre(0);
			const FVector From = C + FVector(-2600.f, -2000.f, 1300.f);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
			if (AutoClock > 1470.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] day: done: %s\n%s"), *Life->BreedingText(), *Life->Report());
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Far off (-bffar): the island, you a Brachiosaurus, the herds and hunters with their young; you go about 2 km away (all
	// of them far off: their bodies put away, the simpler life) for six minutes, a line every 30 s; then back: they wake
	// where their simple life took them, on the ground, and carry on; pictures of the herds after.
	case 590:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(591);
		}
		break;
	case 591:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(592);
		}
		break;
	case 592:
		if (AutoClock > 3.f)
		{
			ToggleLife();
			Next(593);
		}
		break;
	case 593:
		if ((Life->YoungWaiting() == 0 && AutoClock > 5.f) || AutoClock > 600.f)
		{
			// Away: the walkable place about 2 km from the island's middle that's furthest from every herd.
			ABFWorld* I = M->GetIsland();
			FVector Best = D->GetActorLocation();
			float BestD = 0.f;
			for (int32 k = 0; k < 16; ++k)
			{
				const FVector P = I->StartPoint() + FRotator(0.f, 22.5f * k, 0.f).Vector() * 200000.f;
				if (!I->Walkable(P, D->GetSpecies(), 0.f))
				{
					continue;
				}
				float Nearest = 1e9f;
				for (int32 H = 0; H < Life->NumHerds(); ++H)
				{
					Nearest = FMath::Min(Nearest, float(FVector::Dist2D(P, Life->HerdCentre(H))));
				}
				if (Nearest > BestD)
				{
					BestD = Nearest;
					Best = P;
				}
			}
			float Ground = 0.f;
			I->GroundAt(FVector2D(Best), Ground);
			WorldWalk0 = D->GetActorLocation();
			D->TeleportTo(FVector(Best.X, Best.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), D->GetActorRotation());
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] far: you're %.0f m from the nearest herd"), BestD / 100.f);
			LifeShot = 0;
			Next(594);
		}
		break;
	case 594:
		if (int32(AutoClock / 30.f) != int32((AutoClock - DeltaTime) / 30.f))
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] far at %.0f s: %d of %d far off; drinks %d, meals %d, kills %d; %s"), AutoClock, Life->NumDormant(), Life->NumAlive(),
				Life->FarDrinks, Life->FarMeals, Life->FarKills, *Life->BreedingText());
		}
		if (AutoClock > 360.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] far: before going back:\n%s"), *Life->Report());
			float Ground = 0.f;
			M->GetIsland()->GroundAt(FVector2D(WorldWalk0), Ground);
			D->TeleportTo(FVector(WorldWalk0.X, WorldWalk0.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), D->GetActorRotation());
			Next(595);
		}
		break;
	case 595:
		if (AutoClock > 8.f)
		{
			// Back: how many woke, and whether each stands on the ground (its feet within 1.5 m of it).
			int32 Woke = 0, OnGround = 0, Near = 0;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (A.bDead || !A.Body.IsValid() || FVector::Dist2D(A.Body->GetActorLocation(), D->GetActorLocation()) > Life->WakeCm)
				{
					continue;
				}
				++Near;
				Woke += A.bDormant ? 0 : 1;
				float Ground = 0.f;
				M->GetIsland()->GroundAt(FVector2D(A.Body->GetActorLocation()), Ground);
				const float Feet = A.Body->GetActorLocation().Z - A.Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				OnGround += FMath::Abs(Feet - Ground) < 150.f ? 1 : 0;
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] far check: %s %d of %d near you woke, %d on the ground; %d still far off"), Woke == Near && OnGround == Near && Near > 0 ? TEXT("PASS") : TEXT("FAIL"),
				Woke, Near, OnGround, Life->NumDormant());
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] far check: %s while far off they drank %d times, ate %d times"), Life->FarDrinks > 0 && Life->FarMeals > 0 ? TEXT("PASS") : TEXT("FAIL"),
				Life->FarDrinks, Life->FarMeals);
			SetFreeCam(true);
			LifeShot = 0;
			Next(596);
		}
		break;
	case 596:
		{
			const int32 H = int32(AutoClock / 5.f);
			if (H >= FMath::Min(Life->NumHerds(), 4))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] far: after waking:\n%s"), *Life->Report());
				SetFreeCam(false);
				Next(9);
				break;
			}
			const FVector C = Life->HerdCentre(H);
			const FVector From = C + FVector(-2400.f, -1800.f, 1100.f);
			FreeCam->SetActorLocationAndRotation(From, (C - From).Rotation());
			if (FMath::Frac(AutoClock / 5.f) > 0.8f && LifeShot == H)
			{
				Shot(*FString::Printf(TEXT("far_awake_herd%d"), H));
				++LifeShot;
			}
		}
		break;
	// You in the cycle (-bfyou): the island, you a Triceratops, the herds (no hunters), the breeding clocks 60 times as
	// fast. A male first: by a female in condition, X, ten seconds' display: is she carrying your eggs? Then female: in
	// condition, courted by a male of the herd; X when due: your nest; the hatchlings keep to you; then you die and K
	// carries you on as your oldest young; then you grow up as it.
	case 600:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(601);
		}
		break;
	case 601:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("trike"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(602);
		}
		break;
	case 602:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bHunters = false;
			Life->bYoung = false;
			Life->bPlayerFemale = false;
			ToggleLife();
			Life->GrowRate = 60.f;
			for (int32 H = 0; H < Life->NumHerds(); ++H)
			{
				Life->MakeReady(H);
			}
			Next(603);
		}
		break;
	case 603:
		if (Life->PlayerI != INDEX_NONE && AutoClock > 3.f && AutoFrame > 2)
		{
			// Beside the nearest Triceratops female in condition: X.
			int32 F = INDEX_NONE;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (!A.bDead && !A.bPlayer && A.bFemale && A.Base == TEXT("trike") && A.Body.IsValid() && A.Stage >= 4 && !A.bGravid)
				{
					F = i;
				}
			}
			if (F == INDEX_NONE)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: FAIL no Triceratops female"));
				Next(9);
				break;
			}
			Life->Edit(F).ClutchIn = 0.f;
			Life->Edit(F).Condition = 0.9f;
			const FVector Her = Life->Get(F).Body->GetActorLocation();
			const FVector At = Her + Life->Get(F).Body->GetActorRightVector() * 700.f;
			float Ground = 0.f;
			M->GetIsland()->GroundAt(FVector2D(At), Ground);
			D->TeleportTo(FVector(At.X, At.Y, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), (Her - At).Rotation());
			NeedStomach = NeedStomachMax;
			StressStep = F;
			LifeShot = 0;
			Next(604);
		}
		break;
	case 604:
		if (AutoFrame == 3)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you: X: %s"), *Life->PlayerAct(D));
		}
		if (AutoClock > 14.f)
		{
			const FBFAnimal& She = Life->Get(StressStep);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: %s as a male: %s %s carrying your eggs (her father mark %d, you %d)"),
				She.bGravid && She.ClutchFather == Life->PlayerI ? TEXT("PASS") : TEXT("FAIL"), *She.Name, She.bGravid ? TEXT("is") : TEXT("isn't"), She.ClutchFather, Life->PlayerI);
			Shot(TEXT("you_male"));
			// Now female, in condition: a male of the herd should court you.
			Life->bPlayerFemale = true;
			FBFAnimal& Me = Life->Edit(Life->PlayerI);
			Me.ClutchIn = 0.f;
			Me.Condition = 0.95f;
			Me.MateIn = 0.f;
			NeedStomach = NeedStomachMax;
			NeedThirst = 0.f;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				if (!Life->Get(i).bFemale && Life->Get(i).Base == TEXT("trike"))
				{
					Life->Edit(i).MateIn = 0.f;
					Life->Edit(i).Condition = 0.9f;
				}
			}
			Next(605);
		}
		break;
	case 605:
		{
			NeedStomach = NeedStomachMax;
			NeedThirst = 0.f;
			const FBFAnimal& Me = Life->Get(Life->PlayerI);
			if (Me.bGravid || AutoClock > 120.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: %s as a female: courted and mated (by %s) after %.0f s"), Me.bGravid ? TEXT("PASS") : TEXT("FAIL"),
					Life->IsValidAnimal(Me.Father) ? *Life->Get(Me.Father).Name : TEXT("nobody"), AutoClock);
				Next(Me.bGravid ? 606 : 9);
			}
		}
		break;
	case 606:
		NeedStomach = NeedStomachMax;
		NeedThirst = 0.f;
		if (Life->Get(Life->PlayerI).LayIn <= 0.f && AutoClock > 2.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you: %s"), *Life->PlayerStatus());
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you: X: %s"), *Life->PlayerAct(D));
			const bool bNest = Life->NumNests() > 0 && Life->GetNest(Life->NumNests() - 1).Mother == Life->PlayerI;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: %s you laid your eggs"), bNest ? TEXT("PASS") : TEXT("FAIL"));
			Shot(TEXT("you_nest"));
			Next(607);
		}
		break;
	case 607:
		NeedStomach = NeedStomachMax;
		NeedThirst = 0.f;
		if (AutoClock > 100.f)
		{
			// Hatched: your young near you?
			int32 Young = 0, Near = 0;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& Y = Life->Get(i);
				if (!Y.bDead && Y.Body.IsValid() && Y.Mother == Life->PlayerI)
				{
					++Young;
					Near += FVector::Dist2D(Y.Body->GetActorLocation(), D->GetActorLocation()) < 4000.f ? 1 : 0;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: %s %d of your young living, %d within 40 m of you; %s"), Young > 0 && Near * 2 >= Young ? TEXT("PASS") : TEXT("FAIL"),
				Young, Near, *Life->PlayerStatus());
			SetFreeCam(true);
			const FVector From = D->GetActorLocation() + FVector(-2200.f, -1600.f, 900.f);
			FreeCam->SetActorLocationAndRotation(From, (D->GetActorLocation() - From).Rotation());
			Next(608);
		}
		break;
	case 608:
		if (AutoClock > 2.f && AutoFrame > 2)
		{
			Shot(TEXT("you_young"));
			SetFreeCam(false);
			// You die; K carries you on.
			Life->PlayerHealth = 0.f;
			D->StartRagdoll();
			WorldWalk0 = D->GetActorLocation();
			Next(609);
		}
		break;
	case 609:
		if (AutoClock > 3.f)
		{
			ABFDino* Was = D;
			const bool bOn = CarryOnAsYoung();
			ABFDino* Now = Dino();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: %s carried on as one of your young: %s, %s, playing it now: %s"), bOn && Now && Now != Was ? TEXT("PASS") : TEXT("FAIL"),
				Now ? *Now->GetSpecies().Key : TEXT("-"), *FeedNote, Life->PlayerI != INDEX_NONE && Life->Get(Life->PlayerI).bPlayer ? TEXT("yes") : TEXT("no"));
			Life->GrowRate = 3000.f;           // and grows up (a body to take over) in seconds
			StressHerds = 0;
			Next(610);
		}
		break;
	case 610:
		{
			NeedStomach = NeedStomachMax;
			NeedThirst = 0.f;
			const FString Key = D ? D->GetSpecies().Key : FString();
			if (AutoFrame == 1)
			{
				SaveSnapClock = Key;
			}
			if ((D && Key != SaveSnapClock && AutoClock > 2.f) || AutoClock > 30.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] you check: %s you grew up from %s to %s and still play it; %s"), D && Key != SaveSnapClock ? TEXT("PASS") : TEXT("FAIL"),
					*SaveSnapClock, *Key, *Life->PlayerStatus());
				Life->GrowRate = 60.f;
				Shot(TEXT("you_grown"));
				Next(611);
			}
		}
		break;
	case 611:
		if (AutoClock > 5.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] you: done: %s"), *Life->BreedingText());
			SaveSnapClock.Reset();
			Next(9);
		}
		break;
	// The Pteranodons (-bfptera): the island at 9:00, you a Brachiosaurus, the animals with the colony; six minutes watched
	// (the camera following one in the air, a line every 30 s: what each is doing, dives, catches), pictures; then night:
	// do they come back to roost?
	case 620:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(621);
		}
		break;
	case 621:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(622);
		}
		break;
	case 622:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			ToggleLife();
			M->GetIsland()->SetHours(9.f);
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				if (Life->Get(i).bFlier)
				{
					Life->Edit(i).StomachKg = 0.2f * Life->Get(i).StomachMaxKg;      // hungry: off fishing
				}
			}
			SetFreeCam(true);
			LifeShot = 0;
			Next(623);
		}
		break;
	case 623:
		{
			// The camera behind and above the first Pteranodon that's flying (else over the colony).
			int32 Fly = INDEX_NONE;
			for (int32 i = 0; i < Life->Num() && Fly == INDEX_NONE; ++i)
			{
				Fly = Life->Get(i).bFlier && Life->Get(i).Body.IsValid() && !Life->Get(i).bDead && Life->Get(i).Body->IsFlying() ? i : INDEX_NONE;
			}
			const FVector At = Fly != INDEX_NONE ? Life->Get(Fly).Body->GetActorLocation() : Life->Colony;
			const FVector Back = Fly != INDEX_NONE ? -Life->Get(Fly).Body->GetActorForwardVector() : FVector(-1.f, 0.f, 0.f);
			const FVector From = At + Back * 2500.f + FVector(0.f, 0.f, 800.f);
			FreeCam->SetActorLocationAndRotation(From, (At - From).Rotation());
			if (int32(AutoClock / 30.f) != int32((AutoClock - DeltaTime) / 30.f))
			{
				FString Acts;
				int32 Flying = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.bFlier && !A.bDead && A.Body.IsValid())
					{
						Flying += A.Body->IsFlying() ? 1 : 0;
						Acts += FString::Printf(TEXT("\n  %s: %s, %.0f%% full, %.0f m up, \"%s\""), *A.Name, ABFLife::ActName(A.Act), 100.f * A.StomachKg / A.StomachMaxKg,
							(A.Body->GetActorLocation().Z - Life->Colony.Z) / 100.f, *A.Thought);
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera at %.0f s: %d flying, dives %d, catches %d%s"), AutoClock, Flying, Life->Dives, Life->Catches, *Acts);
			}
			if (int32(AutoClock / 60.f) > LifeShot && AutoClock > 20.f)
			{
				LifeShot = int32(AutoClock / 60.f);
				Shot(*FString::Printf(TEXT("ptera_%03ds"), int32(AutoClock)));
			}
			if (AutoClock > 360.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera check: %s they fished: %d dives, %d catches"), Life->Dives > 0 && Life->Catches > 0 ? TEXT("PASS") : TEXT("FAIL"), Life->Dives, Life->Catches);
				M->GetIsland()->SetHours(21.5f);
				Next(624);
			}
		}
		break;
	case 624:
		{
			const FVector From = Life->Colony + FVector(-4000.f, -3000.f, 2500.f);
			FreeCam->SetActorLocationAndRotation(From, (Life->Colony - From).Rotation());
			if (AutoClock > 90.f)
			{
				int32 Fliers = 0, Down = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.bFlier && !A.bDead && A.Body.IsValid())
					{
						++Fliers;
						Down += !A.Body->IsFlying() && (A.Body->IsOnPerch() || FVector::Dist2D(A.Body->GetActorLocation(), Life->Colony) < 6000.f) ? 1 : 0;
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera check: %s night: %d of %d roosting at the colony"), Fliers > 0 && Down * 2 >= Fliers ? TEXT("PASS") : TEXT("FAIL"), Down, Fliers);
				Shot(TEXT("ptera_night"));
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Young Pteranodons (-bfpterayoung): the island at 9:00 with the colony (no other hunters, no young); close pictures of
	// a shoal's fish; the young Pteranodons' bodies grown (each body's flight verdict logged); then the colony breeding 60
	// times as fast: nests, hatching, the young fed by their parents (a line every 20 s); pictures at the colony.
	case 630:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(631);
		}
		break;
	case 631:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(632);
		}
		break;
	case 632:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bHunters = false;
			ToggleLife();
			M->GetIsland()->SetHours(9.f);
			for (int32 St = 1; St <= 3; ++St)
			{
				Life->Prefetch(ABFLife::StageKey(TEXT("ptera"), St));
			}
			SetFreeCam(true);
			LifeShot = 0;
			Next(633);
		}
		break;
	case 633:
		{
			// The fish: close over the nearest shoal, near the water (three pictures a second apart, for the leaps).
			if (Life->Shoals.Num() == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young check: FAIL no fish shoals"));
				Next(634);
				break;
			}
			const FVector S0 = Life->Shoals[0].At;
			const FVector From = S0 + FVector(-500.f, -380.f, 420.f);
			FreeCam->SetActorLocationAndRotation(From, (S0 - From).Rotation());
			if (AutoClock > 2.f && int32(AutoClock) > LifeShot && LifeShot < 6)
			{
				LifeShot = int32(AutoClock);
				Shot(*FString::Printf(TEXT("fish_%d"), LifeShot));
			}
			if (AutoClock > 8.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young: %d shoals, %.0f kg of fish, %d fish drawn"), Life->Shoals.Num(),
					[&]() { float K = 0.f; for (const auto& S : Life->Shoals) { K += S.FishKg; } return K; }(), Life->FishComp ? Life->FishComp->GetInstanceCount() : 0);
				LifeShot = 0;
				Next(636);
			}
		}
		break;
	case 636:
		{
			// The nest trees and ledges: each of the first tree's shelves and the first ledge from two sides (out and to the
			// side, a little above; and from below, out and the other side).
			const int32 NumP = Life->Perches.Num();
			if (NumP == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young check: FAIL no perches"));
				Next(634);
				break;
			}
			int32 Ledge = INDEX_NONE;
			for (int32 p = 0; p < NumP && Ledge == INDEX_NONE; ++p)
			{
				Ledge = Life->Perches[p].bLedge ? p : INDEX_NONE;
			}
			TArray<int32> PerchLook = { 0, FMath::Min(1, NumP - 1) };
			if (Ledge != INDEX_NONE)
			{
				PerchLook.Add(Ledge);
			}
			const int32 Shot2 = int32(AutoClock / 1.5f);
			if (AutoClock > 1.f && Shot2 > LifeShot && LifeShot < PerchLook.Num() * 2 + 1)
			{
				LifeShot = Shot2;
				if (LifeShot <= PerchLook.Num() * 2)
				{
					const int32 k = LifeShot - 1;
					const ABFLife::FPerch& Pc = Life->Perches[PerchLook[k / 2]];
					const FVector Side = FVector::CrossProduct(Pc.Out, FVector::UpVector);
					const FVector From = k % 2 == 0 ? Pc.Top + Pc.Out * 2400.f + Side * 1100.f + FVector(0.f, 0.f, 500.f)
						: Pc.Top + Pc.Out * 1600.f - Side * 1400.f + FVector(0.f, 0.f, -900.f);
					FreeCam->SetActorLocationAndRotation(From, (Pc.Top - From).Rotation());
				}
			}
			if (LifeShot >= 1 && AutoClock > 1.5f * LifeShot + 1.f && LifeShot <= PerchLook.Num() * 2 && SaveSnapClock != FString::FromInt(LifeShot))
			{
				SaveSnapClock = FString::FromInt(LifeShot);
				const int32 k = LifeShot - 1;
				Shot(*FString::Printf(TEXT("perch_%s%d_%s"), Life->Perches[PerchLook[k / 2]].bLedge ? TEXT("ledge") : TEXT("shelf"), PerchLook[k / 2], k % 2 == 0 ? TEXT("above") : TEXT("below")));
			}
			// The whole grove from far off.
			if (AutoClock > 1.5f * (PerchLook.Num() * 2 + 1) + 1.f)
			{
				const FVector From = Life->Colony + FVector(-9000.f, -6000.f, 4500.f);
				FreeCam->SetActorLocationAndRotation(From, (Life->Colony + FVector(0.f, 0.f, 3500.f) - From).Rotation());
			}
			if (AutoClock > 1.5f * (PerchLook.Num() * 2 + 1) + 3.f)
			{
				Shot(TEXT("perch_grove"));
				SaveSnapClock.Reset();
				FString Where;
				for (int32 p = 0; p < NumP; ++p)
				{
					float G = 0.f;
					M->GetIsland()->GroundAt(FVector2D(Life->Perches[p].Top + (Life->Perches[p].bLedge ? Life->Perches[p].Out * 500.f : FVector::ZeroVector)), G);     // (a ledge: the drop off its edge)
					Where += FString::Printf(TEXT(" %s %.0f m up;"), Life->Perches[p].bLedge ? TEXT("ledge") : TEXT("shelf"), (Life->Perches[p].Top.Z - G) / 100.f);
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young: %d perches on %d nest trees:%s"), NumP, Life->NestTrees, *Where);
				LifeShot = 0;
				Next(634);
			}
		}
		break;
	case 634:
		if ((Life->BodiesWaiting() == 0 && AutoClock > 5.f) || AutoClock > 1200.f)
		{
			for (int32 St = 1; St <= 3; ++St)
			{
				const FString Key = ABFLife::StageKey(TEXT("ptera"), St);
				const int32 Si = M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Key; });
				if (Si == INDEX_NONE)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young: %s: no body"), *Key);
					continue;
				}
				const FBFSpecies& S = M->GetSpecies()[Si];
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young: %s: %.1f kg, wing loading %.0f N/m2, flight %s, can fly %s"), *Key, S.MassKg, S.WingLoading,
					S.Flight == EBFFlight::Flies ? TEXT("flies") : S.Flight == EBFFlight::Glides ? TEXT("glides") : S.Flight == EBFFlight::Grounded ? TEXT("grounded") : TEXT("none"),
					S.CanFly() ? TEXT("yes") : TEXT("no"));
			}
			// The colony breeding fast (and hungry: off fishing).
			Life->GrowRate = 60.f;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				if (Life->Get(i).bFlier)
				{
					Life->MakeReady(Life->Get(i).Herd);
					break;
				}
			}
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				if (Life->Get(i).bFlier)
				{
					Life->Edit(i).StomachKg = 0.35f * Life->Get(i).StomachMaxKg;
				}
			}
			LifeShot = 0;
			Next(635);
		}
		break;
		case 635:
		{
			NeedStomach = NeedStomachMax;          // (you, watching: kept fed and watered, or you'd die and the camera go back to you)
			NeedThirst = 0.f;
			int32 NestWatch = 0;
			for (int32 n = Life->NumNests() - 1; n >= 0; --n)
			{
				const int32 P = Life->PerchNear(Life->GetNest(n).At, 100.f);
				if (P != INDEX_NONE && Life->GetNest(n).Base == TEXT("ptera"))
				{
					NestWatch = P;
					break;
				}
			}
			if (Life->Perches.IsValidIndex(NestWatch))
			{
				const ABFLife::FPerch& Pc = Life->Perches[NestWatch];
				const FVector Side = FVector::CrossProduct(Pc.Out, FVector::UpVector);
				const FVector From = Pc.Top + Pc.Out * 1500.f + Side * ((LifeShot % 2 == 0) ? 900.f : -900.f) + FVector(0.f, 0.f, 450.f);
				FreeCam->SetActorLocationAndRotation(From, (Pc.Top - From).Rotation());
			}
			else
			{
				const FVector From = Life->Colony + FVector(-2500.f, -1900.f, 1200.f);
				FreeCam->SetActorLocationAndRotation(From, (Life->Colony - From).Rotation());
			}
			if (int32(AutoClock / 20.f) != int32((AutoClock - DeltaTime) / 20.f))
			{
				FString Young;
				int32 Alive = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.bFlier && !A.bDead && A.Body.IsValid())
					{
						Alive += A.Stage < 4 ? 1 : 0;
						Young += FString::Printf(TEXT("\n  %s: %s, %.0f%% full, %s, \"%s\""), *A.Name, *ABFLife::StageName(A.Stage), 100.f * A.StomachKg / A.StomachMaxKg,
							A.Body->IsFlying() ? TEXT("flying") : TEXT("on the ground"), *A.Thought);
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young at %.0f s: %d young alive; catches %d, feeds %d; %s%s"), AutoClock, Alive, Life->Catches, Life->Feeds, *Life->BreedingText(), *Young);
			}
			if (int32(AutoClock / 60.f) > LifeShot && AutoClock > 30.f)
			{
				LifeShot = int32(AutoClock / 60.f);
				Shot(*FString::Printf(TEXT("ptera_colony_%03ds"), int32(AutoClock)));
			}
			// Ten minutes; on through the night to the first feeds (they don't fish in the dark) up to 25.
			if ((AutoClock > 600.f && Life->Feeds > 0) || AutoClock > 1500.f)
			{
				int32 PteraHatched = 0, Landings = 0, Snaps = 0, Perched = 0, Fliers = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					PteraHatched += A.bFlier && A.Generation > 0 ? 1 : 0;
					if (A.bFlier && A.Body.IsValid())
					{
						Landings += A.Body->PerchLandings;
						Snaps += A.Body->PerchSnaps;
						Fliers += A.bDead ? 0 : 1;
						Perched += !A.bDead && A.Body->IsOnPerch() ? 1 : 0;
					}
				}
				int32 NestsUp = 0, NestsDown = 0;
				for (int32 n = 0; n < Life->NumNests(); ++n)
				{
					if (Life->GetNest(n).Base == TEXT("ptera"))
					{
						float G = 0.f;
						M->GetIsland()->GroundAt(FVector2D(Life->GetNest(n).At), G);
						(Life->GetNest(n).At.Z - G > 1000.f ? NestsUp : NestsDown) += 1;
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera young check: %s Pteranodons hatched %d, fed by their parents %d times, %d catches"), PteraHatched > 0 && Life->Feeds > 0 ? TEXT("PASS") : TEXT("FAIL"),
					PteraHatched, Life->Feeds, Life->Catches);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera nests check: %s nests up high %d, on the ground %d; perch landings %d (finished by placing %d); %d of %d on a perch now"),
					NestsUp > 0 && NestsDown == 0 && Landings > 0 ? TEXT("PASS") : TEXT("FAIL"), NestsUp, NestsDown, Landings, Snaps, Perched, Fliers);
				Shot(TEXT("ptera_colony_end"));
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Quick nest (-bfpteranest, about 3 minutes after the island stands): the colony at 7:30 with nothing else on the
	// island, growing 60 times as fast; a mother put on her perch with eggs about to hatch; then, watched from two sides
	// of her nest: the hatching, both parents flying out to fish and bringing it back up the tree.
	case 640:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(641);
		}
		break;
	case 641:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(642);
		}
		break;
	case 642:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bHunters = false;
			ToggleLife();
			M->GetIsland()->SetHours(7.5f);
			Life->Prefetch(ABFLife::StageKey(TEXT("ptera"), 1));
			Life->GrowRate = 60.f;
			Life->YoungDeathRate = 0.f;          // (the young all live: it's the feeding that's watched)
			SetFreeCam(true);
			LifeShot = 0;
			Next(643);
		}
		break;
	case 643:
		if ((Life->BodiesWaiting() == 0 && AutoClock > 3.f) || AutoClock > 600.f)
		{
			int32 Mum = INDEX_NONE, Dad = INDEX_NONE;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (A.bFlier && !A.bDead && A.Body.IsValid() && A.Stage >= 4)
				{
					Mum = Mum == INDEX_NONE && A.bFemale ? i : Mum;
					Dad = Dad == INDEX_NONE && !A.bFemale ? i : Dad;
				}
			}
			const bool bStaged = Mum != INDEX_NONE && Life->StageNest(Mum, Dad, 15.f);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera nest: %s"), bStaged ? *FString::Printf(TEXT("%s on her nest up %s, eggs by %s hatching in 15 s"),
				*Life->Get(Mum).Name, !Life->Perches.IsValidIndex(Life->Get(Mum).PerchI) ? TEXT("?") : Life->Perches[Life->Get(Mum).PerchI].bAlcove ? TEXT("in an alcove in a cliff")
					: Life->Perches[Life->Get(Mum).PerchI].bLedge ? TEXT("on a ledge") : TEXT("a tree"),
				Dad != INDEX_NONE ? *Life->Get(Dad).Name : TEXT("nobody")) : TEXT("FAIL couldn't stage a nest"));
			Life->Feeds = 0;
			SaveSnapClock = FString::FromInt(Mum);
			LifeShot = 0;
			Next(bStaged ? 644 : 9);
		}
		break;
	case 644:
		{
			NeedStomach = NeedStomachMax;
			NeedThirst = 0.f;
			const int32 Mum = FCString::Atoi(*SaveSnapClock);
			const int32 NI = Life->IsValidAnimal(Mum) ? Life->Get(Mum).NestI : INDEX_NONE;
			const int32 P = Life->IsValidAnimal(Mum) ? Life->Get(Mum).PerchI : INDEX_NONE;
			if (Life->Perches.IsValidIndex(P))
			{
				const ABFLife::FPerch& Pc = Life->Perches[P];
				const FVector Side = FVector::CrossProduct(Pc.Out, FVector::UpVector);
				const bool bOther = int32(AutoClock / 20.f) % 2 == 1;
				const FVector From = Pc.Top + Pc.Out * 900.f + Side * (bOther ? -700.f : 700.f) + FVector(0.f, 0.f, bOther ? 250.f : 450.f);
				FreeCam->SetActorLocationAndRotation(From, (Pc.Top + FVector(0.f, 0.f, 60.f) - From).Rotation());
			}
			if (int32(AutoClock / 15.f) != int32((AutoClock - DeltaTime) / 15.f))
			{
				FString Lines;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.bFlier && !A.bDead && A.Body.IsValid())
					{
						Lines += FString::Printf(TEXT("\n  %s: %s, %.0f%% full, %s, \"%s\""), *A.Name, *ABFLife::StageName(A.Stage), 100.f * A.StomachKg / A.StomachMaxKg,
							A.Body->IsFlying() ? TEXT("flying") : A.Body->IsOnPerch() ? TEXT("on a perch") : TEXT("on the ground"), *A.Thought);
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera nest at %.0f s: feeds %d, catches %d%s"), AutoClock, Life->Feeds, Life->Catches, *Lines);
			}
			if (int32(AutoClock / 20.f) > LifeShot && AutoClock > 8.f)
			{
				LifeShot = int32(AutoClock / 20.f);
				Shot(*FString::Printf(TEXT("pteranest_%03ds"), int32(AutoClock)));
			}
			if ((AutoClock > 60.f && Life->Feeds > 0) || AutoClock > 300.f)
			{
				int32 Hatched = 0, OnNest = 0, Landings = 0, Snaps = 0;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.bFlier && A.Body.IsValid())
					{
						Hatched += A.Mother == Mum && A.Stage == 1 ? 1 : 0;
						OnNest += A.Mother == Mum && !A.bDead && A.Body->IsOnPerch() ? 1 : 0;
						Landings += A.Body->PerchLandings;
						Snaps += A.Body->PerchSnaps;
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] ptera nest check: %s in %.0f s: %d hatched up in the nest, %d still on it; fed %d times; perch landings %d (finished by placing %d)"),
					Hatched > 0 && OnNest == Hatched && Life->Feeds > 0 && Landings > 0 ? TEXT("PASS") : TEXT("FAIL"), AutoClock, Hatched, OnNest, Life->Feeds, Landings, Snaps);
				SaveSnapClock.Reset();
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// A cave (-bfcave, rock pieces step 1): the island at noon, the nearest cliff to the start, a cave carved into its foot
	// (a tunnel 3 m round going in and up a little to a chamber 5.5 m round); pictures outside (from the front, the side,
	// above, far off) and inside (out from the chamber, in from the tunnel); traces for its roof, floor, the way out and
	// the rock over it; you (a Velociraptor) put in the chamber: do you stand on its floor?
	case 650:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(651);
		}
		break;
	case 651:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(652);
		}
		break;
	case 652:
		if (AutoClock > 3.f)
		{
			ABFWorld* I = M->GetIsland();
			I->SetHours(12.f);
			FRandomStream R(7);
			FVector Top, Dn;
			CavePath.Reset();
			if (!I->FindCliff(I->StartPoint(), 120000.f, -1e9f, R, Top, Dn))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave check: FAIL no cliff within 1.2 km"));
				Next(9);
				break;
			}
			float G10 = 0.f;
			I->GroundAt(FVector2D(Top + Dn * 1000.f), G10);
			const FVector In = -Dn;
			const FVector M0 = FVector(Top.X, Top.Y, 0.f) + Dn * 1300.f + FVector(0.f, 0.f, G10 + 30.f + 0.55f * 300.f);
			CavePath = { M0, M0 + In * 900.f + FVector(0.f, 0.f, 50.f), M0 + In * 1900.f + FVector(0.f, 0.f, 120.f), M0 + In * 2700.f + FVector(0.f, 0.f, 150.f) };
			CaveOut = Dn;
			const int32 Cv = I->AddCave(CavePath, { 300.f, 320.f, 360.f, 550.f });
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave: a cliff %.0f m from the start (its top %.0f m up, its foot %.0f m lower); cave %d"),
				FVector::Dist2D(Top, I->StartPoint()) / 100.f, Top.Z / 100.f, (Top.Z - G10) / 100.f, Cv);
			if (Cv == INDEX_NONE)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave check: FAIL the cave couldn't be made"));
				Next(9);
				break;
			}
			SetFreeCam(true);
			LifeShot = 0;
			Next(653);
		}
		break;
	case 653:
		{
			// Six views, two seconds each (the picture at the end of each).
			const FVector M0 = CavePath[0], M1 = CavePath[1], M2 = CavePath[2], M3 = CavePath[3];
			const FVector In = -CaveOut, Side = FVector::CrossProduct(CaveOut, FVector::UpVector);
			const int32 View = FMath::Min(int32(AutoClock / 2.f), 6);
			struct FView { FVector From, To; const TCHAR* Tag; };
			const FView Views[6] = {
				{ M0 + CaveOut * 2500.f + FVector(0.f, 0.f, 700.f), M0, TEXT("cave_front") },
				{ M0 + CaveOut * 1400.f + Side * 1800.f + FVector(0.f, 0.f, 400.f), M0, TEXT("cave_side") },
				{ M2 + CaveOut * 1500.f + FVector(0.f, 0.f, 4000.f), M2, TEXT("cave_above") },
				{ M3 - In * 250.f + FVector(0.f, 0.f, 60.f), M0, TEXT("cave_inside_out") },
				{ M1 + FVector(0.f, 0.f, 40.f), M3, TEXT("cave_inside_in") },
				{ M0 + CaveOut * 6000.f - Side * 2500.f + FVector(0.f, 0.f, 2500.f), M1, TEXT("cave_far") } };
			if (View < 6)
			{
				FreeCam->SetActorLocationAndRotation(Views[View].From, (Views[View].To - Views[View].From).Rotation());
				if (AutoClock > View * 2.f + 1.6f && LifeShot == View)
				{
					Shot(Views[View].Tag);
					LifeShot = View + 1;
				}
			}
			else
			{
				// The traces.
				FCollisionQueryParams Q(SCENE_QUERY_STAT(BFCave), true);
				if (D)
				{
					Q.AddIgnoredActor(D);
				}
				FHitResult Hit;
				const bool bRoof = GetWorld()->LineTraceSingleByChannel(Hit, M3, M3 + FVector(0.f, 0.f, 3000.f), ECC_Visibility, Q);
				const float RoofD = bRoof ? float(Hit.Distance) : -1.f;
				const bool bFloor = GetWorld()->LineTraceSingleByChannel(Hit, M3, M3 - FVector(0.f, 0.f, 1500.f), ECC_Visibility, Q);
				const float FloorD = bFloor ? float(Hit.Distance) : -1.f;
				const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, M3, M0 + CaveOut * 800.f, ECC_Visibility, Q);
				const FString BlockedBy = bBlocked ? FString::Printf(TEXT(" (blocked %.1f m in, by %s)"), Hit.Distance / 100.f, Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?")) : FString();
				const bool bTop = GetWorld()->LineTraceSingleByChannel(Hit, M3 + FVector(0.f, 0.f, 30000.f), M3, ECC_Visibility, Q);
				const float Thick = bTop && bRoof ? float(Hit.ImpactPoint.Z - (M3.Z + RoofD)) : -1.f;
				const bool bPass = bRoof && RoofD > 100.f && RoofD < 1000.f && bFloor && FloorD > 50.f && FloorD < 700.f && !bBlocked && Thick > 150.f;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave check: %s roof %.1f m over the chamber's middle, floor %.1f m under it; the way out %s%s; rock over the roof %.1f m"),
					bPass ? TEXT("PASS") : TEXT("FAIL"), RoofD / 100.f, FloorD / 100.f, bBlocked ? TEXT("blocked") : TEXT("clear"), *BlockedBy, Thick / 100.f);
				// You in the chamber.
				if (D && bFloor)
				{
					const FVector Floor = M3 - FVector(0.f, 0.f, FloorD);
					D->TeleportTo(Floor + FVector(0.f, 0.f, D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), FRotator(0.f, CaveOut.Rotation().Yaw, 0.f));
					SaveSnapClock = FString::Printf(TEXT("%f"), Floor.Z);
				}
				SetFreeCam(false);
				Next(654);
			}
		}
		break;
	case 654:
		if (AutoClock > 4.f)
		{
			const float FloorZ = FCString::Atof(*SaveSnapClock);
			const float Feet = D ? float(D->GetActorLocation().Z - D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) : -1e9f;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave check: %s you in the chamber: your feet %.2f m over its floor after 4 s"),
				FMath::Abs(Feet - FloorZ) < 80.f ? TEXT("PASS") : TEXT("FAIL"), (Feet - FloorZ) / 100.f);
			Look(180.f, -5.f);
			Shot(TEXT("cave_you"));
			SaveSnapClock.Reset();
			Next(655);
		}
		break;
	case 655:
		if (AutoClock > 2.f)
		{
			Next(9);
		}
		break;
	// Rock shapes (-bfrocks, rock pieces step B): the island at noon with its rocks (how many, how long they took); the
	// nearest pile of boulders to the start and the nearest fin, each from two sides; the colony put in and one of its
	// rock ledges from two sides; the ground points the rocks stand on (the animals walk round them).
	case 660:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(661);
		}
		break;
	case 661:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(662);
		}
		break;
	case 662:
		if (AutoClock > 3.f)
		{
			ABFWorld* I = M->GetIsland();
			I->SetHours(12.f);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] rocks: %s; boulders %d, fins %d"), *I->RockText(), I->NumRocks(0), I->NumRocks(2));
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bHunters = false;
			ToggleLife();
			Life->bFrozen = true;
			SetFreeCam(true);
			LifeShot = 0;
			Next(663);
		}
		break;
	case 663:
		{
			ABFWorld* I = M->GetIsland();
			struct FView { FVector From, To; FString Tag; };
			TArray<FView> Views;
			FBFRock B, F;
			if (I->NearestRock(I->StartPoint(), 0, B))
			{
				const FVector Side = FVector::CrossProduct(B.Rot.Vector(), FVector::UpVector);
				Views.Add({ B.At + B.Rot.Vector() * 1300.f + Side * 500.f + FVector(0.f, 0.f, 250.f), B.At, TEXT("rocks_boulders_a") });
				Views.Add({ B.At - B.Rot.Vector() * 900.f + Side * 900.f + FVector(0.f, 0.f, 700.f), B.At, TEXT("rocks_boulders_b") });
			}
			if (I->NearestRock(I->StartPoint(), 2, F))
			{
				const FVector Fw = F.Rot.Vector(), Side = FVector::CrossProduct(Fw, FVector::UpVector);
				Views.Add({ F.At + Side * 1500.f + Fw * 400.f + FVector(0.f, 0.f, 200.f), F.At, TEXT("rocks_fin_a") });
				Views.Add({ F.At + Fw * 1600.f - Side * 600.f + FVector(0.f, 0.f, 600.f), F.At, TEXT("rocks_fin_b") });
			}
			for (const ABFLife::FPerch& Pc : Life->Perches)
			{
				if (Pc.bLedge)
				{
					const FVector Side = FVector::CrossProduct(Pc.Out, FVector::UpVector);
					Views.Add({ Pc.Top + Pc.Out * 1500.f + Side * 900.f + FVector(0.f, 0.f, 400.f), Pc.Top, TEXT("rocks_ledge_a") });
					Views.Add({ Pc.Top + Pc.Out * 1100.f - Side * 1100.f + FVector(0.f, 0.f, -500.f), Pc.Top, TEXT("rocks_ledge_b") });
					break;
				}
			}
			const int32 View = int32(AutoClock / 2.f);
			if (View < Views.Num())
			{
				FreeCam->SetActorLocationAndRotation(Views[View].From, (Views[View].To - Views[View].From).Rotation());
				if (AutoClock > View * 2.f + 1.6f && LifeShot == View)
				{
					Shot(*Views[View].Tag);
					LifeShot = View + 1;
				}
			}
			else
			{
				int32 Blocked = 0;
				for (int32 k = 0; k < 400; ++k)
				{
					FBFRock R;
					if (k == 0 && I->NearestRock(I->StartPoint(), 0, R))
					{
						Blocked += I->RockAt(R.At) ? 1 : 0;
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] rocks check: %s %d boulders, %d crag rocks on the island; %d views; the nearest boulder's ground %s; %.0f frames a second while looking (%.1f ms a frame)"),
					I->NumRocks(0) > 0 && I->NumRocks(2) > 0 && Views.Num() >= 6 && Blocked > 0 ? TEXT("PASS") : TEXT("FAIL"), I->NumRocks(0), I->NumRocks(2), Views.Num(),
					Blocked > 0 ? TEXT("is marked: animals walk round") : TEXT("is NOT marked"), AutoFrame / FMath::Max(AutoClock, 0.01f), 1000.f * AutoClock / FMath::Max(AutoFrame, 1));
				Life->bFrozen = false;
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// A den (-bfden, animals use caves): the island's dens; a pack of four raptors 60 m out from the nearest den's mouth
	// to the start, well fed and tired, at 21:00 (no plant eaters, no colony); watched till they're in (up to 4 min, the
	// camera at the den's mouth, then in its chamber); then morning (9:00): do they come out again (up to 3 min)?
	case 670:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(671);
		}
		break;
	case 671:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("brachio"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(672);
		}
		break;
	case 672:
		if (AutoClock > 3.f)
		{
			ABFWorld* I = M->GetIsland();
			int32 Den = INDEX_NONE;
			for (int32 c = 0; c < I->NumCaves(); ++c)
			{
				if (I->CaveKind(c) == 0 && (Den == INDEX_NONE || FVector::Dist2D(I->CaveMouth(c), I->StartPoint()) < FVector::Dist2D(I->CaveMouth(Den), I->StartPoint())))
				{
					Den = c;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] den: the island has %d dens; %s"), I->IslandDens, Den == INDEX_NONE ? TEXT("none") : *I->CaveText(Den));
			if (Den != INDEX_NONE)
			{
				// The way in, measured: the ground under the mouth and each floor point (a trace down), and a trace at a
				// raptor's chest height from the mouth to each floor point in turn.
				FString Way;
				TArray<FVector> Pts = { I->CaveMouth(Den) };
				Pts.Append(I->CaveFloor(Den));
				FVector Last = FVector::ZeroVector;
				for (int32 k = 0; k < Pts.Num(); ++k)
				{
					FHitResult H;
					const bool bDown = GetWorld()->LineTraceSingleByChannel(H, Pts[k] + FVector(0.f, 0.f, 250.f), Pts[k] - FVector(0.f, 0.f, 600.f), ECC_Visibility);
					const FVector Floor = bDown ? FVector(H.ImpactPoint) : Pts[k];
					Way += FString::Printf(TEXT(" [%d floor %.2f m (planned %.2f) by %s"), k, Floor.Z / 100.f, Pts[k].Z / 100.f, bDown && H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("nothing"));
					if (k > 0)
					{
						FHitResult B;
						const bool bBlocked = GetWorld()->LineTraceSingleByChannel(B, Last + FVector(0.f, 0.f, 80.f), Floor + FVector(0.f, 0.f, 80.f), ECC_Visibility);
						Way += bBlocked ? FString::Printf(TEXT("; from the last BLOCKED %.1f m on by %s at %.2f m"), B.Distance / 100.f, B.GetComponent() ? *B.GetComponent()->GetName() : TEXT("?"), B.ImpactPoint.Z / 100.f) : FString(TEXT("; clear from the last"));
					}
					Way += TEXT("]");
					Last = Floor;
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] den: the way in:%s"), *Way);
			}
			if (Den == INDEX_NONE)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] den check: FAIL no den on the island"));
				Next(9);
				break;
			}
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bHunters = false;
			Life->bFliers = false;
			Life->Clear();
			Life->Populate(I, I->CaveMouth(Den));          // (plant eaters there too: cleared again below)
			Life->Clear();
			FRandomStream R(3);
			Life->AddHerd(TEXT("raptor"), 4, I->CaveMouth(Den) + I->CaveOut(Den) * 6000.f, 0.f, 0.f, true, R);
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				Life->Edit(i).StomachKg = 0.9f * Life->Get(i).StomachMaxKg;
				Life->Edit(i).Tired = 0.6f;
				Life->Edit(i).Thirst = 0.f;
			}
			I->SetHours(21.f);
			SaveSnapClock = FString::FromInt(Den);
			SetFreeCam(true);
			LifeShot = 0;
			Next(673);
		}
		break;
	case 673:
	case 675:
		{
			NeedStomach = NeedStomachMax;
			NeedThirst = 0.f;
			ABFWorld* I = M->GetIsland();
			const int32 Den = FCString::Atoi(*SaveSnapClock);
			const FVector Mouth = I->CaveMouth(Den), Out = I->CaveOut(Den);
			const TArray<FVector>& Fl = I->CaveFloor(Den);
			int32 In = 0, Alive = 0;
			FString Lines;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (!A.bDead && A.Body.IsValid() && !A.bPlayer)
				{
					++Alive;
					const bool bIn = I->CaveAt(A.Body->GetActorLocation() + FVector(0.f, 0.f, 50.f)) == Den;
					In += bIn ? 1 : 0;
					Lines += FString::Printf(TEXT("\n  %s: %s, %s, %s from the mouth (feet at %.2f m, moving %.2f m/s, path %d at %d), \"%s\""), *A.Name, ABFLife::ActName(A.Act), bIn ? TEXT("in the den") : TEXT("outside"),
						*FString::Printf(TEXT("%.1f m"), FVector::Dist2D(A.Body->GetActorLocation(), Mouth) / 100.f),
						(A.Body->GetActorLocation().Z - A.Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) / 100.f, A.Body->GetVelocity().Size2D() / 100.f, A.Path.Num(), A.PathI, *A.Thought);
				}
			}
			// The camera: outside the mouth (the first minute and a half), then in the chamber looking back at the beds.
			const bool bNight = AutoStep == 673;
			const bool bInsideCam = bNight ? AutoClock > 90.f : AutoClock < 30.f;
			const FVector From = bInsideCam ? Fl[FMath::Max(Fl.Num() - 2, 0)] + FVector(0.f, 0.f, 220.f) : Mouth + Out * 1600.f + FVector(0.f, 0.f, 500.f);
			const FVector To = bInsideCam ? Fl.Last() + FVector(0.f, 0.f, 60.f) : Mouth;
			FreeCam->SetActorLocationAndRotation(From, (To - From).Rotation());
			if (int32(AutoClock / 20.f) != int32((AutoClock - DeltaTime) / 20.f))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] den at %.0f s (%s): %d of %d in the den%s"), AutoClock, bNight ? TEXT("night") : TEXT("morning"), In, Alive, *Lines);
			}
			if (int32(AutoClock / 30.f) > LifeShot && AutoClock > 20.f)
			{
				LifeShot = int32(AutoClock / 30.f);
				Shot(*FString::Printf(TEXT("den_%s_%03ds"), bNight ? TEXT("night") : TEXT("morning"), int32(AutoClock)));
			}
			if (bNight && ((In == Alive && Alive > 0 && AutoClock > 100.f) || AutoClock > 240.f))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] den check: %s night: %d of %d raptors asleep in the den after %.0f s"), Alive > 0 && In * 2 > Alive ? TEXT("PASS") : TEXT("FAIL"), In, Alive, AutoClock);
				I->SetHours(9.f);
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					Life->Edit(i).StomachKg = 0.25f * Life->Get(i).StomachMaxKg;      // morning, hungry: out hunting
					Life->Edit(i).Tired = 0.f;
				}
				LifeShot = 0;
				Next(675);
			}
			else if (!bNight && ((In == 0 && AutoClock > 30.f) || AutoClock > 180.f))
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] den check: %s morning: %d of %d out of the den after %.0f s"), Alive > 0 && (Alive - In) * 2 > Alive ? TEXT("PASS") : TEXT("FAIL"), Alive - In, Alive, AutoClock);
				SaveSnapClock.Reset();
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Mouths (-bffeed): the island at 7:00 with its herds, everyone hungry and thirsty; for three minutes, each second, every
	// plant eater cropping or drinking (in place a second or more) and every hunter at a kill: how far its snout is from
	// what it's eating; by kind at the end (the middle miss, how many within 50 cm, how low each found its mouth goes).
	// Pictures: a Brachiosaurus and a Triceratops cropping, each from the side and the front.
	case 680:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(681);
		}
		break;
	case 681:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(682);
		}
		break;
	case 682:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bFliers = false;
			ToggleLife();
			M->GetIsland()->SetHours(7.f);
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				Life->Edit(i).StomachKg = 0.25f * Life->Get(i).StomachMaxKg;
				Life->Edit(i).Thirst = Life->Get(i).bHunter ? 0.f : (i % 3 == 0 ? 0.6f : 0.f);
			}
			SaveSnapshot.Reset();
			SetFreeCam(true);
			LifeShot = 0;
			TearShots = 0;
			TearShotAt = -100.f;
			TearCamS = 0.f;
			Next(683);
		}
		break;
	case 683:
		{
			NeedStomach = NeedStomachMax;
			NeedThirst = 0.f;
			// The camera: on whoever is cropping, a Brachiosaurus first (side, then front), then a Triceratops.
			const TCHAR* Kinds[2] = { TEXT("brachio"), TEXT("trike") };
			const int32 Want = AutoClock < 90.f ? 0 : 1;
			int32 Cam = INDEX_NONE;
			bool bDrinkCam = false;
			for (int32 i = 0; i < Life->Num() && Cam == INDEX_NONE; ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				Cam = !A.bDead && A.Body.IsValid() && A.Base == TEXT("brachio") && A.Body->HasMouthTarget() && A.Thought.StartsWith(TEXT("drinking")) ? i : INDEX_NONE;
				bDrinkCam = Cam != INDEX_NONE;
			}
			for (int32 i = 0; i < Life->Num() && Cam == INDEX_NONE; ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				Cam = !A.bDead && A.Body.IsValid() && A.Base == Kinds[Want] && A.Body->HasMouthTarget() && A.Thought.StartsWith(TEXT("cropping")) ? i : INDEX_NONE;
			}
			// A meat eater tearing at a kill takes the camera while it pulls (its own pictures).
			bool bTearCam = false;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (!A.bDead && A.Body.IsValid() && A.Act == EBFAct::Feast && A.Body->HasMouthTarget() && (A.Body->GetTear() > 0.f || TearCamS > 0.f))
				{
					Cam = i;
					bTearCam = true;
					break;
				}
			}
			TearCamS = bTearCam ? (TearCamS > 0.f ? TearCamS - DeltaTime : 6.f) : 0.f;
			if (Cam != INDEX_NONE)
			{
				ABFDino* B = Life->Get(Cam).Body.Get();
				const FVector Fw = B->GetActorForwardVector(), Side = FVector::CrossProduct(Fw, FVector::UpVector);
				const float L = B->BodyLengthCm();
				const bool bFront = FMath::Fmod(AutoClock, 90.f) > 45.f;
				const FVector Aim = (B->SnoutNow() + B->GetActorLocation()) * 0.5f;
				const FVector From = bFront ? B->SnoutNow() + Fw * 0.9f * L + Side * 0.3f * L + FVector(0.f, 0.f, 0.15f * L) : Aim + Side * 1.3f * L + FVector(0.f, 0.f, 0.1f * L);
				FreeCam->SetActorLocationAndRotation(From, (Aim - From).Rotation());
				if (bTearCam)
				{
					// side and front by turns, a shot each time it hauls hardest
					const FVector TA = (B->SnoutNow() + B->GetActorLocation()) * 0.5f;
					const FVector TF = TA + (TearShots % 2 == 0 ? Side * 1.4f * L : Fw * 1.2f * L + Side * 0.4f * L) + FVector(0.f, 0.f, 0.15f * L);
					FreeCam->SetActorLocationAndRotation(TF, (TA - TF).Rotation());
					if (B->GetTear() > 0.6f && AutoClock - TearShotAt > 7.f && TearShots < 6)
					{
						TearShotAt = AutoClock;
						Shot(*FString::Printf(TEXT("feed_tear_%s_%s_%03ds"), *Life->Get(Cam).Base, TearShots % 2 == 0 ? TEXT("side") : TEXT("front"), int32(AutoClock)));
						++TearShots;
					}
				}
				const int32 Slot = int32(AutoClock / 15.f);
				if (!bTearCam && Slot > LifeShot && FMath::Fmod(AutoClock, 45.f) > 8.f)
				{
					LifeShot = Slot;
					Shot(*FString::Printf(TEXT("feed_%s_%s_%03ds"), bDrinkCam ? TEXT("brachio_drink") : Kinds[Want], bFront ? TEXT("front") : TEXT("side"), int32(AutoClock)));
				}
			}
			// The misses, once a second.
			if (int32(AutoClock) != int32(AutoClock - DeltaTime) && AutoClock > 5.f)
			{
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					const FBFAnimal& A = Life->Get(i);
					if (A.bDead || !A.Body.IsValid() || A.bPlayer || !A.Body->HasMouthTarget() || A.Body->GetVelocity().Size2D() > 60.f)
					{
						continue;
					}
					const bool bAt = A.Thought.StartsWith(TEXT("cropping")) || A.Thought.StartsWith(TEXT("drinking")) || A.Act == EBFAct::Feast;
					if (bAt)
					{
						SaveSnapshot.Add(FString::Printf(TEXT("%s|%s|%.0f|%.0f"), *A.Base, A.Thought.StartsWith(TEXT("drinking")) ? TEXT("drink") : A.Act == EBFAct::Feast ? TEXT("kill") : TEXT("plant"),
							A.Body->MouthMissCm(), A.Body->GetMouthLowCm()));
						if (SaveSnapshot.Num() % 25 == 1)
						{
							const FVector Sn = A.Body->SnoutNow(), Tg = A.Body->GetMouthTarget(), Bd = A.Body->GetActorLocation();
							UE_LOG(LogTemp, Display, TEXT("[BoneForge] feed sample %s: snout %.0f cm above the target, %.0f cm off it across; snout %.0f cm ahead of its middle, the target %.0f cm; neck %.2f, front dipped %.0f deg; snout over feet %.0f cm"),
								*A.Name, Sn.Z - Tg.Z, FVector::Dist2D(Sn, Tg), FVector::Dist2D(Sn, Bd), FVector::Dist2D(Tg, Bd), A.Body->GetFeedReach(), A.Body->GetFeedDip(),
								Sn.Z - (Bd.Z - A.Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
						}
					}
				}
			}
			if (AutoClock > 180.f)
			{
				// By kind and what: the middle miss, the share within 50 cm, the lowest its mouth goes (if it found a limit).
				TMap<FString, TArray<float>> Misses;
				TMap<FString, float> Low;
				for (const FString& Row : SaveSnapshot)
				{
					TArray<FString> P;
					Row.ParseIntoArray(P, TEXT("|"));
					Misses.FindOrAdd(P[0] + TEXT(" ") + P[1]).Add(FCString::Atof(*P[2]));
					Low.FindOrAdd(P[0]) = FMath::Max(Low.FindOrAdd(P[0]), FCString::Atof(*P[3]));
				}
				TArray<float> All;
				FString Text;
				for (TPair<FString, TArray<float>>& K : Misses)
				{
					K.Value.Sort();
					All.Append(K.Value);
					const int32 Near = K.Value.FilterByPredicate([](float V) { return V <= 50.f; }).Num();
					Text += FString::Printf(TEXT("\n  %s: %d looks, middle miss %.0f cm, %d%% within 50 cm, worst %.0f cm"), *K.Key, K.Value.Num(), K.Value[K.Value.Num() / 2],
						100 * Near / FMath::Max(K.Value.Num(), 1), K.Value.Last());
				}
				for (const TPair<FString, float>& L : Low)
				{
					Text += FString::Printf(TEXT("\n  %s: its mouth gets down to %s"), *L.Key, L.Value > 0.f ? *FString::Printf(TEXT("%.0f cm over its feet at the lowest"), L.Value) : TEXT("anything it went for"));
				}
				All.Sort();
				const float Mid = All.Num() > 0 ? All[All.Num() / 2] : 1e9f;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] feed check: %s mouths at their food: %d looks, middle miss %.0f cm%s"), All.Num() > 20 && Mid <= 50.f ? TEXT("PASS") : TEXT("FAIL"), All.Num(), Mid, *Text);
				SaveSnapshot.Reset();
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Rivers (-bfriver): the island at noon; how many points of its rivers stand up out of the surface round them; the
	// steepest river stretch within 1.2 km of the start from the side, from downstream and from above.
	case 690:
		if (AutoClock > 4.f)
		{
			{
				int32 RSeed = 20261112;
				FParse::Value(FCommandLine::Get(), TEXT("bfriverseed="), RSeed);
				MakeIsland(RSeed);
			}
			bGoWhenReady = false;
			Next(691);
		}
		break;
	case 691:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(692);
		}
		break;
	case 692:
		{
			// The places a river runs down a slope (up to four, the steepest first), each from ground level side-on (a
			// little up and down the stream too), and the water's numbers round each.
			ABFWorld* I = M->GetIsland();
			if (AutoFrame == 1)
			{
				I->SetHours(12.f);
				TArray<FVector> At, Dn;
				TArray<float> Drop;
				I->RiverSlopes(4, At, Dn, Drop);
				CavePath = At;
				SaveBackAt.Reset();
				for (int32 k = 0; k < At.Num(); ++k)
				{
					SaveBackAt.Add(FString::FromInt(k), Dn[k]);
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] river %d: at %s (%.0f m from the start), its surface drops %.0f cm in 12 m; ground (g) and water (w) in m round it, 4 m apart:%s"),
						k, *At[k].ToString(), FVector::Dist2D(At[k], I->StartPoint()) / 100.f, Drop[k], *I->WaterText(At[k]));
				}
				float Worst = 0.f;
				TArray<FVector> WorstAt;
				const int32 Perched = I->PerchedWater(Worst, &WorstAt);
				for (const FVector& Wp : WorstAt)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] river perched at %s (biome %d):%s"), *Wp.ToString(), int32(I->BiomeAt(FVector2D(Wp))), *I->WaterText(Wp));
				}
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] river check: %s %d slopes found on island %d; %d water points stand over 1 m above a point beside them (the worst %.0f cm); the spilling lowered %d; the runs lowered %d"),
					Perched == 0 ? TEXT("PASS") : TEXT("FAIL"), At.Num(), I->GetSeed(), Perched, Worst, I->SpillLowered, I->RunsLowered);
				SetFreeCam(true);
				LifeShot = 0;
			}
			const int32 Spots = CavePath.Num();
			const int32 View = int32(AutoClock / 2.f);
			if (View >= Spots * 3)
			{
				SetFreeCam(false);
				Next(9);
				break;
			}
			const int32 k = View / 3, Which = View % 3;
			const FVector At = CavePath[k], Dn = SaveBackAt.FindRef(FString::FromInt(k));
			const FVector Side = FVector::CrossProduct(Dn, FVector::UpVector);
			const FVector Spot = At + Dn * (Which == 0 ? 0.f : Which == 1 ? -800.f : 800.f);
			// From the lower bank, 12 m off, at least 2 m over the water (so the river's in sight whatever its banks).
			float GL = 0.f, GR = 0.f;
			I->GroundAt(FVector2D(Spot + Side * 1200.f), GL);
			I->GroundAt(FVector2D(Spot - Side * 1200.f), GR);
			FVector From = Spot + (GL <= GR ? Side : -Side) * 2000.f;
			float GC = 0.f;
			I->GroundAt(FVector2D(From), GC);
			From.Z = FMath::Max(GC + 300.f, float(At.Z) + 800.f);          // looking down on it a little
			FreeCam->SetActorLocationAndRotation(From, (Spot - From).Rotation());
			if (AutoClock > View * 2.f + 1.6f && LifeShot == View)
			{
				Shot(*FString::Printf(TEXT("river%d_%s"), k, Which == 0 ? TEXT("side") : Which == 1 ? TEXT("side_up") : TEXT("side_down")));
				LifeShot = View + 1;
			}
		}
		break;
	case 470:      // eating: a Triceratops by the nearest cycad crops five mouthfuls; pictures before and after
	case 471:
	case 472:
		{
			ABFWorld* I = M->GetIsland();
			if (AutoStep == 470 && AutoFrame == 1)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] eat: the T. rex by plants eats: %d (it is a meat eater)"), TryFeed() ? 1 : 0);
				AutoSpecies = 1;
				Pick(AutoSpecies);
				break;
			}
			if (AutoStep == 470 && AutoClock > 3.f && D)
			{
				FVector At;
				I->NearestPlant(FVector2D(I->StartPoint()), EBFPlant::Cycad, At);
				FVector Dir = (FVector(I->StartPoint().X, I->StartPoint().Y, 0.f) - FVector(At.X, At.Y, 0.f)).GetSafeNormal();
				Dir = Dir.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Dir;
				const float Off = D->GetCapsuleComponent()->GetScaledCapsuleRadius() + 260.f;
				float Ground = At.Z;
				I->GroundAt(FVector2D(At + Dir * Off), Ground);
				D->TeleportTo(FVector(At.X, At.Y, 0.f) + Dir * Off + FVector(0.f, 0.f, Ground + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), (-Dir).Rotation());
				WorldWater = At;
				SetFreeCam(true);
				const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector);
				const FVector From = At + Dir * (0.5f * Off) + Side * 900.f + FVector(0.f, 0.f, 260.f);
				FreeCam->SetActorLocationAndRotation(From, (At + Dir * (0.5f * Off) + FVector(0.f, 0.f, 120.f) - From).Rotation());
				Next(471);
				break;
			}
			if (AutoStep == 471 && AutoClock > 2.f && AutoClock < 3.f)
			{
				Shot(TEXT("eat_before"));
				AutoClock = 3.f;
			}
			if (AutoStep == 471 && AutoClock > 3.f)
			{
				// A mouthful a second, five of them.
				const int32 Due = FMath::FloorToInt(AutoClock - 3.f);
				if (Due >= FeedTries && FeedTries < 5)
				{
					++FeedTries;
					if (!TryFeed())
					{
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] eat: mouthful %d: nothing in reach (snout %s)"), FeedTries, *D->GetBoneWorld(TEXT("head_end")).ToString());
					}
				}
				if (AutoClock > 9.f)
				{
					Shot(TEXT("eat_after"));
					Next(472);
				}
			}
			if (AutoStep == 472 && AutoClock > 2.f)
			{
				SetFreeCam(false);
				Look(200.f, -10.f);
				Shot(TEXT("eat_trike"));
				Next(9);
			}
		}
		break;
	// Skin folds (-bffolds): the T. rex and the Triceratops running, close on a knee: a picture from the side with the
	// folds as the knee is most bent, the same with them off, and one from behind-side with them on.
	case 420:
		if (AutoSpecies > 1)
		{
			Next(9);
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f))
		{
			Area = EBFSpawnArea::Flat;
			Pick(AutoSpecies);
			bAutoForward = true;
			bAutoRun = true;
			TestMax = 0.f;
			Next(421);
		}
		break;
	case 421:
		if (D)
		{
			if (AutoClock > 1.f)
			{
				TestMax = FMath::Max(TestMax, D->GetFoldMax());
			}
			if (AutoClock > 3.f)
			{
				D->SetFoldScale(1.f);
				Next(422);
			}
		}
		break;
	case 422:      // as the knee is most bent: the world paused, the free camera close to that knee, from its outer side
		if (D && AutoClock > 0.3f && D->GetFoldMax() >= 0.92f * TestMax && D->GetFoldMaxBone() != NAME_None)
		{
			bPaused = true;
			ApplyTimeScale();
			SetFreeCam(true);
			FoldKnee = D->GetFoldMaxBone();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] folds %s: paused with %s bent %.0f deg past rest (most while running %.0f) | %s"), *D->GetSpecies().Key,
				*FoldKnee.ToString(), 60.f * D->GetFoldMax(), 60.f * TestMax, *D->GetFoldNote());
			Next(423);
		}
		else if (AutoClock > 8.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] folds %s: the knee never bent as far again (most %.2f)"), D ? *D->GetSpecies().Key : TEXT("-"), TestMax);
			Next(430);
		}
		break;
	case 423:      // (frames, not time: the world is paused) the same frame from two sides, the folds on then off
	case 424:
	case 425:
	case 426:
		if (D && FreeCam && AutoFrame == 4)
		{
			const int32 k = AutoStep - 423;              // 0 side on, 1 side off, 2 behind on, 3 behind off
			const FVector Knee = D->GetBoneWorld(FoldKnee);
			const float Side = FVector::DotProduct(Knee - D->GetActorLocation(), D->GetActorRightVector()) >= 0.f ? 1.f : -1.f;
			const float Dist = FMath::Max(0.6f * D->GetSpecies().Limbs[0].LengthCm, 60.f);
			const FVector From = k < 2 ? Knee + D->GetActorRightVector() * Side * Dist + FVector(0.f, 0.f, 0.15f * Dist)
				: Knee - D->GetActorForwardVector() * Dist + D->GetActorRightVector() * Side * 0.35f * Dist + FVector(0.f, 0.f, 0.15f * Dist);
			FreeCam->SetActorLocationAndRotation(From, (Knee - From).Rotation());
			D->SetFoldScale(k % 2 == 0 ? 1.f : 0.f);
		}
		if (D && AutoFrame == 10)
		{
			static const TCHAR* FoldTags[] = { TEXT("fold_side_on"), TEXT("fold_side_off"), TEXT("fold_back_on"), TEXT("fold_back_off") };
			Shot(FoldTags[AutoStep - 423]);
		}
		if (AutoFrame >= 16)
		{
			Next(AutoStep == 426 ? 430 : AutoStep + 1);
		}
		break;
	case 430:
		if (AutoFrame >= 2)
		{
			bPaused = false;
			ApplyTimeScale();
			SetFreeCam(false);
			if (D)
			{
				D->SetFoldScale(1.f);
			}
			bAutoForward = bAutoRun = false;
			++AutoSpecies;
			Next(420);
		}
		break;
	// A companion (-bfpair), by the menu's buttons and the real keys: a Velociraptor beside the T. rex, copying it (W and
	// Shift held 6 s), then following it (W held 8 s, then let go), the person for scale, and sent away.
	case 400:
		if (AutoClock > 4.f)
		{
			Area = EBFSpawnArea::Flat;
			AutoSpecies = 0;
			Pick(AutoSpecies);
			ShowPicker(true);
			Next(401);
		}
		break;
	case 401:
		if (AutoClock > 1.f)
		{
			PressMenu(TEXT("Beside you"), 2, 402);       // the six's rows first: T. rex 0, Triceratops 1, Velociraptor 2
		}
		break;
	case 402:
		if (AutoClock > 1.5f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] pair: beside you %s"), Companion ? *Companion->GetSpecies().Key : TEXT("NOBODY"));
			if (!Companion)
			{
				Next(9);
				break;
			}
			PressMenu(TEXT("Copy me"), 0, 403);
		}
		break;
	case 403:
		if (AutoClock > 0.5f && D)
		{
			ShowPicker(false);
			bAutoKeys = true;
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::W, A.GetModifierKeys(), 0, false, 0, 0));
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::LeftShift, A.GetModifierKeys(), 0, false, 0, 0));
			TestMax = TestTop = 0.f;
			Next(404);
		}
		break;
	case 404:
		if (D && Companion)
		{
			TestMax = FMath::Max(TestMax, D->GetSpeedMs());
			TestTop = FMath::Max(TestTop, Companion->GetSpeedMs());
			if (AutoClock > 6.f)
			{
				const FVector Gap = Companion->GetActorLocation() - D->GetActorLocation();
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] pair copying: T. rex up to %.1f m/s, Velociraptor up to %.1f m/s; headings %.0f and %.0f deg; the raptor %.1f m ahead, %.1f m beside"),
					TestMax, TestTop, D->GetActorRotation().Yaw, Companion->GetActorRotation().Yaw,
					FVector::DotProduct(Gap, D->GetActorForwardVector()) / 100.f, FVector::DotProduct(Gap, D->GetActorRightVector()) / 100.f);
				Look(70.f, -8.f);
				Shot(TEXT("pair_copy"));
				FSlateApplication& A = FSlateApplication::Get();
				A.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftShift, A.GetModifierKeys(), 0, false, 0, 0));
				A.ProcessKeyUpEvent(FKeyEvent(EKeys::W, A.GetModifierKeys(), 0, false, 0, 0));
				Next(405);
			}
		}
		break;
	case 405:
		if (AutoClock > 3.f)
		{
			bAutoKeys = false;
			ShowPicker(true);
			PressMenu(TEXT("Follow"), 0, 406);
		}
		break;
	case 406:
		if (AutoClock > 0.5f)
		{
			ShowPicker(false);
			bAutoKeys = true;
			Look(0.f, -15.f);
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::W, A.GetModifierKeys(), 0, false, 0, 0));
			Next(407);
		}
		break;
	case 407:
		if (AutoClock > 8.f && D && Companion)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] pair following, walking: %.1f m apart, the raptor at %.1f m/s (%s)"),
				FVector::Dist2D(D->GetActorLocation(), Companion->GetActorLocation()) / 100.f, Companion->GetSpeedMs(), *Companion->GetGaitName());
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyUpEvent(FKeyEvent(EKeys::W, A.GetModifierKeys(), 0, false, 0, 0));
			Next(408);
		}
		break;
	case 408:
		if (AutoClock > 4.f && D && Companion)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] pair following, stopped: %.1f m apart, the raptor at %.1f m/s"),
				FVector::Dist2D(D->GetActorLocation(), Companion->GetActorLocation()) / 100.f, Companion->GetSpeedMs());
			bAutoKeys = false;
			ShowPicker(true);
			PressMenu(TEXT("Person for scale"), 0, 409);
		}
		break;
	case 409:
		if (AutoClock > 0.5f)
		{
			ShowPicker(false);
			Look(-25.f, -6.f);
			Next(410);
		}
		break;
	case 410:
		ShotThen(1.f, TEXT("pair_person"), [&]()
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] pair: person for scale %s"), Person ? TEXT("standing") : TEXT("MISSING"));
			ShowPicker(true);
			PressMenu(TEXT("Send away"), 0, 411);
		});
		break;
	case 411:
		if (AutoClock > 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] pair: after Send away, beside you %s"), Companion ? TEXT("STILL THERE") : TEXT("nobody"));
			ShowPicker(false);
			Next(9);
		}
		break;
	// Look (-bflook): the panel's buttons clicked through Slate on a T. rex (Velociraptor colours and feathers; then turned
	// 60° round the colour wheel, 20% darker, in fuzz) and a Brachiosaurus (Pteranodon colours, armour plates), pictures
	// from the side and the front, then both back to their own look.
	case 380:
		if (AutoClock > 4.f)
		{
			Area = EBFSpawnArea::Flat;
			AutoSpecies = 0;
			Pick(AutoSpecies);
			Next(381);
		}
		break;
	case 381:
		if (AutoClock > 2.5f)
		{
			Look(90.f, -8.f);
			ShowLook(true);
			Next(382);
		}
		break;
	case 382:
		ShotThen(0.5f, TEXT("look_panel"), [&]()
		{
			auto Click = [&](const TCHAR* Label, int32 Nth) { ABFSpliceStage::ClickWidget(ABFSpliceStage::FindButtonIn(LookWidget.ToSharedRef(), Label, Nth)); };
			Click(TEXT("Raptor"), 0);
			Click(TEXT("feathers"), 0);
			Next(383);
		});
		break;
	case 383:
	case 387:
	case 392:
		if (AutoClock > 0.5f && D)
		{
			const FBFLook& L = D->GetLook();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] look %s: colours %s, hue %+.0f, shade %+.0f%%, covering %s"), *D->GetSpecies().Key,
				L.Scheme.IsEmpty() ? TEXT("its own") : *L.Scheme, L.HueDeg, 100.f * L.Shade, L.Covering.IsEmpty() ? TEXT("its own") : *L.Covering);
			ShowLook(false);
			Look(90.f, -8.f);
			Next(AutoStep + 1);
		}
		break;
	case 384:
		ShotThen(0.5f, TEXT("look_feathers_side"), [&]() { Look(155.f, -12.f); Next(385); });
		break;
	case 385:
		ShotThen(0.5f, TEXT("look_feathers_front"), [&]() { ShowLook(true); Next(386); });
		break;
	case 386:
		if (AutoClock > 0.5f)
		{
			auto Click = [&](const TCHAR* Label, int32 Nth) { ABFSpliceStage::ClickWidget(ABFSpliceStage::FindButtonIn(LookWidget.ToSharedRef(), Label, Nth)); };
			for (int32 k = 0; k < 4; ++k)
			{
				Click(TEXT(" + "), 0);         // hue, 15° a click
			}
			Click(TEXT(" - "), 1);             // shade, 10% a click
			Click(TEXT(" - "), 1);
			Click(TEXT("fuzz"), 0);
			Next(387);
		}
		break;
	case 388:
		ShotThen(0.5f, TEXT("look_hue_side"), [&]() { Look(155.f, -12.f); Next(389); });
		break;
	case 389:
		ShotThen(0.5f, TEXT("look_hue_front"), [&]()
		{
			AutoSpecies = 4;                   // the Brachiosaurus
			Pick(AutoSpecies);
			Next(390);
		});
		break;
	case 390:
		if (AutoClock > 2.5f)
		{
			ShowLook(true);
			Next(391);
		}
		break;
	case 391:
		if (AutoClock > 0.5f)
		{
			auto Click = [&](const TCHAR* Label, int32 Nth) { ABFSpliceStage::ClickWidget(ABFSpliceStage::FindButtonIn(LookWidget.ToSharedRef(), Label, Nth)); };
			Click(TEXT("Ptera"), 0);
			Click(TEXT("armour plates"), 0);
			Next(392);
		}
		break;
	case 393:
		ShotThen(0.8f, TEXT("look_armour_side"), [&]() { Look(155.f, -12.f); Next(394); });
		break;
	case 394:
		ShotThen(0.8f, TEXT("look_armour_front"), [&]() { ShowLook(true); Next(395); });
		break;
	case 395:      // back to their own looks, by the button
		if (AutoClock > 0.5f)
		{
			ABFSpliceStage::ClickWidget(ABFSpliceStage::FindButtonIn(LookWidget.ToSharedRef(), TEXT("Its own look"), 0));
			AutoSpecies = 0;
			Pick(AutoSpecies);
			Next(396);
		}
		break;
	case 396:
		if (AutoClock > 2.5f && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] look %s on its next appearance: %s"), *D->GetSpecies().Key, D->GetLook().IsOwn() ? TEXT("its own") : TEXT("the changed one"));
			ShowLook(true);
			Next(397);
		}
		break;
	case 397:
		if (AutoClock > 0.5f && D)
		{
			ABFSpliceStage::ClickWidget(ABFSpliceStage::FindButtonIn(LookWidget.ToSharedRef(), TEXT("Its own look"), 0));
			FString Saved;
			FFileHelper::LoadFileToString(Saved, *FPaths::Combine(M->BoneForgeDir(), TEXT("my_creatures.json")));
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] look reset: trex own %d, brachio own %d; my_creatures.json: %s"), M->LookOf(TEXT("trex")).IsOwn() ? 1 : 0,
				M->LookOf(TEXT("brachio")).IsOwn() ? 1 : 0, *Saved.Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\t"), TEXT("")));
			ShowLook(false);
			Next(9);
		}
		break;
	// Jumping (-bfjump): each animal that walks (the six, or a -bfonly list) stands on the flat and Space is pressed through
	// Slate; pictures at the deepest crouch (as it leaves the ground), the top of the jump and the landing's crouch.
	case 360:
	{
		const FBFSpecies& S = M->GetSpecies()[AutoSpecies];
		if ((AutoOnly.Num() ? !AutoOnly.Contains(S.Key) : S.IsSplice()) || S.Legs != EBFLegs::Walks)
		{
			if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); }
			break;
		}
		if (AutoClock > (AutoSpecies == 0 ? 4.f : 1.f)) { Area = EBFSpawnArea::Flat; bAutoForward = false; Pick(AutoSpecies); Next(361); }
		break;
	}
	case 361:
		// Standing, from the side: a picture first (the first one of a run stalls the game for a moment: not mid-jump).
		if (AutoClock > 2.f && D && !bShotTaken)
		{
			Look(90.f, -5.f);
			Shot(TEXT("jump_stand"));
			bShotTaken = true;
		}
		if (AutoClock > 3.5f && D)
		{
			Look(90.f, -5.f);
			if (D->CanFly())
			{
				D->StartJump();           // (Space takes a winged animal off; its legs' jump is measured straight)
			}
			else
			{
				bAutoKeys = true;
				FSlateApplication& A = FSlateApplication::Get();
				A.ProcessKeyDownEvent(FKeyEvent(EKeys::SpaceBar, A.GetModifierKeys(), 0, false, 0, 0));
			}
			bShotTaken = false;
			SlowKeyAt = 0;            // pictures taken: 1 crouch, 2 top, 3 landing
			TestTop = 0.f;
			Next(362);
		}
		break;
	case 362:
		if (D)
		{
			if (AutoFrame == 3)
			{
				FSlateApplication& A = FSlateApplication::Get();
				A.ProcessKeyUpEvent(FKeyEvent(EKeys::SpaceBar, A.GetModifierKeys(), 0, false, 0, 0));
			}
			const float Vz = D->GetVelocity().Z;
			if (SlowKeyAt == 0 && (D->IsJumpAirborne() || (D->IsJumpLanding() && !D->GetJumpLeft())))
			{
				Shot(TEXT("jump_crouch"));       // its deepest crouch: just leaving the ground (or the heave's bottom)
				SlowKeyAt = 1;
			}
			else if (SlowKeyAt == 1 && D->IsJumpAirborne() && Vz <= 0.f && AutoClock > 0.05f)
			{
				Shot(TEXT("jump_top"));
				SlowKeyAt = 2;
			}
			else if (SlowKeyAt >= 1 && D->IsJumpLanding() && D->GetJumpLeft() && SlowKeyAt < 3)
			{
				TestTop += DeltaTime;
				if (TestTop > 0.08f)
				{
					Shot(TEXT("jump_land"));     // the landing crouch at its deepest
					SlowKeyAt = 3;
				}
			}
			if ((AutoClock > 0.5f && !D->IsJumping()) || AutoClock > 8.f)
			{
				const FBFSpecies& S = D->GetSpecies();
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] jump %s: worked out %.0f cm, rose %.1f cm, left the ground %d (push %.2f times its weight, crouch %.0f cm) | %s"),
					*S.Key, S.JumpCm, D->GetJumpRiseCm(), D->GetJumpLeft() ? 1 : 0, S.JumpPush, S.JumpCrouchCm, *S.JumpSummary);
				bAutoKeys = false;
				if (++AutoSpecies >= M->GetSpecies().Num()) { Next(9); } else { Next(360); }
			}
		}
		break;
	// Watching (-bfslowmo): first each key tapped through Slate and read by the ordinary input code (, , . . P / P V V U U),
	// then the T. rex walked (W held) at full speed and a quarter, paused, a frame let through, and the free camera.
	case 350:
	{
		static const FKey Keys[] = { EKeys::Comma, EKeys::Comma, EKeys::Period, EKeys::Period, EKeys::P, EKeys::Slash, EKeys::P,
			EKeys::V, EKeys::V, EKeys::U, EKeys::U };
		FSlateApplication& A = FSlateApplication::Get();
		if (SlowKeyAt == 0 && AutoFrame == 1)
		{
			if (AutoClock < 4.f)
			{
				AutoFrame = 0;          // the field loads first
				break;
			}
			ShowPicker(false);
			bAutoKeys = true;
		}
		if (AutoFrame == 2)
		{
			A.ProcessKeyDownEvent(FKeyEvent(Keys[SlowKeyAt], A.GetModifierKeys(), 0, false, 0, 0));
		}
		else if (AutoFrame == 4)
		{
			A.ProcessKeyUpEvent(FKeyEvent(Keys[SlowKeyAt], A.GetModifierKeys(), 0, false, 0, 0));
		}
		else if (AutoFrame == 8)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo key %s: speed 1/%d, paused %d, time scale %.4f, free camera %d (view %s), text %s"),
				*Keys[SlowKeyAt].ToString(), 1 << SlowStep, bPaused ? 1 : 0, UGameplayStatics::GetGlobalTimeDilation(GetWorld()), bFreeCam ? 1 : 0,
				GetViewTarget() ? *GetViewTarget()->GetClass()->GetName() : TEXT("-"), bHudHidden ? TEXT("hidden") : TEXT("shown"));
			AutoFrame = 0;
			if (++SlowKeyAt >= UE_ARRAY_COUNT(Keys))
			{
				Next(351);
			}
		}
		break;
	}
	case 351:
		if (AutoClock > 0.5f)
		{
			Area = EBFSpawnArea::Flat;
			Pick(0);
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::W, A.GetModifierKeys(), 0, false, 0, 0));      // held from here on
			Next(352);
		}
		break;
	case 352:
		if (AutoClock > 4.f && D)
		{
			SlowReal0 = FPlatformTime::Seconds();
			SlowGame0 = GetWorld()->GetTimeSeconds();
			SlowPhase0 = D->GetPhase();
			Next(353);
		}
		break;
	case 353:
	case 354:
		if (AutoClock > 2.f && D)
		{
			const double R = FPlatformTime::Seconds() - SlowReal0;
			const float G = GetWorld()->GetTimeSeconds() - SlowGame0;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo at 1/%d: %.2f game s in %.2f real s (%.3f), %.3f strides a game second, %.2f m/s"),
				1 << SlowStep, G, R, G / FMath::Max(R, 1e-3), (D->GetPhase() - SlowPhase0) / FMath::Max(G, 1e-3f), D->GetSpeedMs());
			if (AutoStep == 353)
			{
				SlowStep = 2;           // a quarter
				SlowReal0 = FPlatformTime::Seconds();
				SlowGame0 = GetWorld()->GetTimeSeconds();
				SlowPhase0 = D->GetPhase();
				Next(354);
			}
			else
			{
				Shot(TEXT("slowmo_quarter"));      // (from behind: turning the view would steer it)
				bPaused = true;
				SlowAt0 = D->GetActorLocation();
				SlowReal0 = FPlatformTime::Seconds();
				Next(355);
			}
		}
		break;
	case 355:
		if (FPlatformTime::Seconds() - SlowReal0 > 1.5 && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo paused: it moved %.2f cm in %.1f real s"), FVector::Dist(D->GetActorLocation(), SlowAt0), FPlatformTime::Seconds() - SlowReal0);
			SlowAt0 = D->GetActorLocation();
			StepFrames = 2;             // as / does (set here, before this tick's key code counts one off)
			Next(356);
		}
		break;
	case 356:
		if (AutoFrame >= 6 && D)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo one frame: it moved %.2f cm (at %.2f m/s a frame is about %.2f cm)"),
				FVector::Dist(D->GetActorLocation(), SlowAt0), D->GetSpeedMs(), D->GetSpeedMs() * 100.f * FApp::GetDeltaTime());
			SetFreeCam(true);           // W is held: it flies forward, on real time, while the world stands still
			SlowAt0 = FreeCam ? FreeCam->GetActorLocation() : FVector::ZeroVector;
			SlowReal0 = FPlatformTime::Seconds();
			Next(357);
		}
		break;
	case 357:
		if (FPlatformTime::Seconds() - SlowReal0 > 1.0 && D && FreeCam)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo free camera, paused: it flew %.0f cm in %.2f real s (%.0f cm/s set), the animal keeps walking: %d"),
				FVector::Dist(FreeCam->GetActorLocation(), SlowAt0), FPlatformTime::Seconds() - SlowReal0, FreeCamSpeed, bCruise ? 1 : 0);
			FSlateApplication& A = FSlateApplication::Get();
			A.ProcessKeyUpEvent(FKeyEvent(EKeys::W, A.GetModifierKeys(), 0, false, 0, 0));
			bPaused = false;
			SlowStep = 0;
			// Beside it, looking at it, to see it walk on and the camera keep up.
			const FVector Side = D->GetActorLocation() + D->GetActorRightVector() * 1.6f * D->BodyLengthCm() + FVector(0.f, 0.f, 150.f);
			FreeCam->SetActorLocationAndRotation(Side, (D->GetActorLocation() - Side).Rotation());
			SlowAt0 = Side - D->GetActorLocation();
			Next(358);
		}
		break;
	case 358:
		if (AutoClock > 3.f && D && FreeCam)
		{
			const FVector Off = FreeCam->GetActorLocation() - D->GetActorLocation();
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo free camera following: the animal at %.2f m/s, the camera's offset changed by %.1f cm"),
				D->GetSpeedMs(), FVector::Dist(Off, SlowAt0));
			Shot(TEXT("slowmo_freecam"));
			Next(359);
		}
		break;
	case 359:
		if (AutoClock > 1.f)
		{
			SetFreeCam(false);
			bAutoKeys = false;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] slowmo done: view %s"), GetViewTarget() ? *GetViewTarget()->GetClass()->GetName() : TEXT("-"));
			Next(9);
		}
		break;
	case 9:
		if (AutoClock > 1.5f && ChainQueue.Num() > 0)
		{
			// The next check in this launch: back to a clean start (the island made again by it).
			const FString Check = ChainQueue[0];
			ChainQueue.RemoveAt(0);
			const int32 First = StartStepOf(Check);
			if (First != INDEX_NONE)
			{
				SetFreeCam(false);
				if (Life)
				{
					Life->GrowRate = 1.f;
					Life->NeedRate = 1.f;
					Life->bYoung = true;
					Life->bHunters = true;
					Life->bFrozen = false;
					Life->YoungDeathRate = 1.f;
					Life->bFliers = true;
				}
				LifeShot = 0;
				SaveSnapClock.Reset();
				bShowBalance = false;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] checks: next in this launch: %s"), *Check);
				Next(First);
				break;
			}
			UE_LOG(LogTemp, Warning, TEXT("[BoneForge] checks: %s can't follow in a launch"), *Check);
		}
		if (AutoClock > 1.5f) { ConsoleCommand(TEXT("quit")); Next(10); }
		break;
	default:
		break;
	}
}

// The realism checks (Raven's list, 2026-11-14): -bfcarcass, -bfbehave, -bfworldfx, -bfcavelook.
void ABFPlayerController::TickRealismChecks(float DeltaTime)
{
	ABFGameMode* M = Mode();
	ABFDino* D = Dino();
	auto Shot = [&](const TCHAR* Tag)
	{
		const FString File = FPaths::Combine(AutoOut, FString::Printf(TEXT("%s_%s.png"), *M->GetSpecies()[AutoSpecies].Key, Tag));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] screenshot %s"), *File);
	};
	auto Next = [&](int32 Step) { AutoStep = Step; AutoClock = 0.f; AutoFrame = 0; };
	switch (AutoStep)
	{
	// A kill (-bfcarcass): a Triceratops killed by a T. rex at 10:00, the T. rex hungry beside it. Pictures: the fresh kill
	// (blood pooling, the wound), the T. rex tearing (its muzzle reddening), then eaten down, the bones as it lay.
	case 700:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(701);
		}
		break;
	case 701:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(702);
		}
		break;
	case 702:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bFliers = false;
			ToggleLife();
			M->GetIsland()->SetHours(10.f);
			int32 V = INDEX_NONE, K = INDEX_NONE;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (A.bDead || !A.Body.IsValid() || A.bPlayer)
				{
					continue;
				}
				V = V == INDEX_NONE && A.Base == TEXT("trike") ? i : V;
				K = K == INDEX_NONE && A.Base == TEXT("trex") ? i : K;
			}
			if (V == INDEX_NONE || K == INDEX_NONE)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] carcass check: FAIL no Triceratops or T. rex"));
				Next(9);
				break;
			}
			ABFDino* VB = Life->Get(V).Body.Get();
			ABFDino* KB = Life->Get(K).Body.Get();
			const FVector At = VB->GetActorLocation() - VB->GetActorForwardVector() * (0.6f * VB->BodyLengthCm() + 0.5f * KB->BodyLengthCm());
			float G = float(At.Z);
			M->GetIsland()->GroundAt(FVector2D(At), G);
			KB->SetActorLocation(FVector(At.X, At.Y, G + KB->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), false, nullptr, ETeleportType::TeleportPhysics);
			KB->SetActorRotation(FRotator(0.f, (VB->GetActorLocation() - At).Rotation().Yaw, 0.f));
			Life->Edit(K).StomachKg = 0.05f * Life->Get(K).StomachMaxKg;
			CarcassI = Life->TestKill(V, K);
			CarcassVictim = V;
			CarcassKiller = K;
			CarcassWound = 0.f;
			CarcassMuzzle = 0.f;
			CarcassTears = 0;
			SetFreeCam(true);
			LifeShot = 0;
			Next(703);
		}
		break;
	case 703:
		{
			FVector At = Life->CarcassAt(CarcassI);
			if (Life->BonePieces(CarcassI) > 0)
			{
				At = Life->BoneCentre(CarcassI);
			}
			const FBFAnimal& V = Life->Get(CarcassVictim);
			const FBFAnimal& K = Life->Get(CarcassKiller);
			if (V.Body.IsValid())
			{
				CarcassWound = FMath::Max(CarcassWound, FMath::Max3(V.Body->BloodCm(1), V.Body->BloodCm(2), V.Body->BloodCm(3)));
			}
			if (K.Body.IsValid())
			{
				CarcassMuzzle = FMath::Max(CarcassMuzzle, K.Body->BloodCm(0));
			}
			const float L = V.Body.IsValid() ? V.Body->BodyLengthCm() : 600.f;
			const FVector Side = FVector(0.f, 1.f, 0.f);
			FVector From = At + Side * 1.6f * L + FVector(0.f, 0.f, 0.35f * L);
			FVector Aim = At;
			const bool bAbove = (AutoClock > 10.f && AutoClock < 14.f) || AutoClock > 66.f;
			if (bAbove)
			{
				From = At + FVector(-0.5f * L, 0.35f * L, 0.75f * L);
			}
			// The T. rex tearing: on its head, from the side.
			if (AutoClock > 16.f && AutoClock < 58.f && K.Body.IsValid())
			{
				Aim = (K.Body->SnoutNow() + At) * 0.5f;
				const FVector KSide = FVector::CrossProduct(K.Body->GetActorForwardVector(), FVector::UpVector);
				From = Aim + KSide * 1.1f * K.Body->BodyLengthCm() + FVector(0.f, 0.f, 0.15f * K.Body->BodyLengthCm());
				if (K.Body->GetTear() > 0.6f && CarcassTears < 3 && AutoClock - TearShotAt > 6.f)
				{
					TearShotAt = AutoClock;
					Shot(*FString::Printf(TEXT("carcass_tear%d"), CarcassTears++));
				}
			}
			{
				float G = float(From.Z);
				if (M->GetIsland()->GroundAt(FVector2D(From), G))
				{
					From.Z = FMath::Max(float(From.Z), G + 250.f);          // (on a hillside: out of the ground and the ferns)
				}
			}
			FreeCam->SetActorLocationAndRotation(From, (Aim - From).Rotation());
			auto ShotWhen = [&](float T, const TCHAR* Name)
			{
				if (AutoClock > T && AutoClock - DeltaTime <= T)
				{
					Shot(Name);
				}
			};
			ShotWhen(8.f, TEXT("carcass_fresh_side"));
			ShotWhen(12.f, TEXT("carcass_fresh_above"));
			if (AutoClock > 58.f && AutoClock - DeltaTime <= 58.f)
			{
				Life->TestEatDown(CarcassI, 0.25f);          // eaten down: the bones
			}
			ShotWhen(63.f, TEXT("carcass_bones_side"));
			ShotWhen(68.f, TEXT("carcass_bones_above"));
			if (AutoClock > 72.f)
			{
				const int32 Bones = Life->BonePieces(CarcassI);
				const bool bOk = Life->PoolsShown() > 0 && Bones > 10 && CarcassWound > 0.f && CarcassMuzzle > 0.f && CarcassTears > 0;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] carcass check: the bones' middle %.0f cm from where the blood is"), FVector::Dist2D(Life->BoneCentre(CarcassI), Life->CarcassAt(CarcassI)));
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] carcass check: %s blood pools under %d kills, %d bone pieces, wound %.0f cm on the Triceratops, muzzle blood %.0f cm on the T. rex, %d tears pictured"),
					bOk ? TEXT("PASS") : TEXT("FAIL"), Life->PoolsShown(), Bones, CarcassWound, CarcassMuzzle, CarcassTears);
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// Behaviour (-bfbehave): the island at 21:30, its herds asleep: how many lie down (pictures of one, two sides). Then at
	// 10:00 a T. rex set down 70 m from a Triceratops herd: startles, stares, alarm calls and answers (a picture of the
	// herd staring); a Triceratops bitten to a third of its health limps (its speed against its walk).
	case 710:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(711);
		}
		break;
	case 711:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(712);
		}
		break;
	case 712:
		if (AutoClock > 3.f)
		{
			if (!Life)
			{
				Life = GetWorld()->SpawnActor<ABFLife>();
			}
			Life->bYoung = false;
			Life->bFliers = false;
			ToggleLife();
			M->GetIsland()->SetHours(21.5f);
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				Life->Edit(i).Tired = 0.9f;
				Life->Edit(i).StomachKg = 0.8f * Life->Get(i).StomachMaxKg;
				Life->Edit(i).Thirst = 0.f;
			}
			SetFreeCam(true);
			BehaveLying = 0;
			Next(713);
		}
		break;
	case 713:
		{
			// Asleep: count those lying, the camera on a lying one (the biggest kind lying), two sides.
			int32 Lying = 0, Cam = INDEX_NONE, Herbs = 0;
			float CamKg = 0.f;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				if (A.bDead || !A.Body.IsValid() || A.bPlayer)
				{
					continue;
				}
				Herbs += 1;
				if (A.Body->GetLie() > 0.95f)
				{
					++Lying;
					if (A.MassKg > CamKg && A.MassKg < 30000.f)
					{
						CamKg = A.MassKg;
						Cam = i;
					}
				}
			}
			BehaveLying = FMath::Max(BehaveLying, Lying);
			if (Cam != INDEX_NONE)
			{
				ABFDino* B = Life->Get(Cam).Body.Get();
				const FVector Fw = B->GetActorForwardVector(), Side = FVector::CrossProduct(Fw, FVector::UpVector);
				const float L = B->BodyLengthCm();
				const FVector Aim = B->GetActorLocation() - FVector(0.f, 0.f, 0.3f * B->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				const FVector From = AutoClock < 34.f ? Aim + Side * 1.2f * L + FVector(0.f, 0.f, 0.25f * L) : Aim + Fw * 1.1f * L + Side * 0.4f * L + FVector(0.f, 0.f, 0.2f * L);
				FreeCam->SetActorLocationAndRotation(From, (Aim - From).Rotation());
				if ((AutoClock > 30.f && AutoClock - DeltaTime <= 30.f) || (AutoClock > 38.f && AutoClock - DeltaTime <= 38.f))
				{
					M->GetIsland()->SetHours(M->GetIsland()->GetHours());          // (keep the light)
					Shot(*FString::Printf(TEXT("behave_asleep_%s_%s"), *Life->Get(Cam).Base, AutoClock < 34.f ? TEXT("side") : TEXT("front")));
				}
			}
			if (AutoClock > 40.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] behave check: at night %d of %d animals lying down"), BehaveLying, Life->Num());
				// Morning: wake them, then a T. rex near a Triceratops herd.
				M->GetIsland()->SetHours(10.f);
				int32 Trike = INDEX_NONE, Rex = INDEX_NONE;
				for (int32 i = 0; i < Life->Num(); ++i)
				{
					FBFAnimal& A = Life->Edit(i);
					A.Tired = 0.f;
					if (!A.bDead && A.Body.IsValid() && !A.bPlayer)
					{
						Trike = Trike == INDEX_NONE && A.Base == TEXT("trike") ? i : Trike;
						Rex = Rex == INDEX_NONE && A.Base == TEXT("trex") ? i : Rex;
					}
				}
				BehaveTrike = Trike;
				BehaveRex = Rex;
				if (Trike != INDEX_NONE && Rex != INDEX_NONE)
				{
					ABFDino* T = Life->Get(Trike).Body.Get();
					ABFDino* R = Life->Get(Rex).Body.Get();
					const FVector At = T->GetActorLocation() + FVector(7000.f, 0.f, 0.f);
					float G = float(At.Z);
					M->GetIsland()->GroundAt(FVector2D(At), G);
					R->SetActorLocation(FVector(At.X, At.Y, G + R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), false, nullptr, ETeleportType::TeleportPhysics);
					Life->Edit(Rex).StomachKg = Life->Get(Rex).StomachMaxKg;          // fed: it just stands there
				}
				BehaveStare = 0;
				BehaveShot = false;
				Next(714);
			}
		}
		break;
	case 714:
		{
			if (BehaveTrike == INDEX_NONE || BehaveRex == INDEX_NONE)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] behave check: FAIL no Triceratops or T. rex"));
				Next(9);
				break;
			}
			const FBFAnimal& T = Life->Get(BehaveTrike);
			const FBFAnimal& R = Life->Get(BehaveRex);
			int32 Staring = 0;
			for (int32 i = 0; i < Life->Num(); ++i)
			{
				const FBFAnimal& A = Life->Get(i);
				Staring += !A.bDead && A.Body.IsValid() && A.Body->GetGaze() > 0.6f ? 1 : 0;
			}
			BehaveStare = FMath::Max(BehaveStare, Staring);
			if (T.Body.IsValid() && R.Body.IsValid())
			{
				// The herd from beside the T. rex: their faces to it.
				const FVector H = T.Body->GetActorLocation();
				const FVector Way = (R.Body->GetActorLocation() - H).GetSafeNormal2D();
				const FVector From = H + Way * 2500.f + FVector::CrossProduct(Way, FVector::UpVector) * 900.f + FVector(0.f, 0.f, 350.f);
				FreeCam->SetActorLocationAndRotation(From, (H - From).Rotation());
				if (!BehaveShot && Staring >= 2 && AutoClock > 1.f)
				{
					BehaveShot = true;
					Shot(TEXT("behave_stare"));
				}
			}
			if (AutoClock > 25.f && AutoClock - DeltaTime <= 25.f && T.Body.IsValid())
			{
				Life->TestHurt(BehaveTrike, BehaveRex, 0.6f);          // bitten: it limps
				BehaveSpeed = FVector2D::ZeroVector;
			}
			if (AutoClock > 27.f && T.Body.IsValid())
			{
				BehaveSpeed.X = FMath::Max(BehaveSpeed.X, float(T.Body->GetVelocity().Size2D()));
				BehaveSpeed.Y = FMath::Max(BehaveSpeed.Y, T.Body->GetLimp());
			}
			if (AutoClock > 45.f)
			{
				const bool bOk = BehaveLying > 0 && BehaveStare >= 2 && BehaveSpeed.Y > 0.f;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] behave check: %s up to %d staring at once; %s; the bitten Triceratops limps %.2f, at most %.1f m/s (its run %.1f)"),
					bOk ? TEXT("PASS") : TEXT("FAIL"), BehaveStare, *Life->CallsText(), BehaveSpeed.Y, BehaveSpeed.X / 100.f,
					T.Body.IsValid() ? T.Body->GetSpecies().Fast.SpeedCm / 100.f : 0.f);
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	// The world's look (-bfworldfx): the island at 6:30 in its mist (from up a slope, over the land); the raptor runs 10 s
	// across open ground, the camera on its trail (prints, dust); then into shallow water (splashes; it comes out wet);
	// then the steepest river stretch near the start (foam). Counts of each, and pictures.
	// Cave mouths (-bfcavelook): the island at 11:00; each den's mouth in turn from six places: an animal's eye walking up
	// to it, high in front, both front quarters, side on, and from far off. 2 s each (the picture at 1.5 s).
	case 730:
		if (AutoClock > 4.f)
		{
			int32 CaveSeed = 20261112;          // (-bfcaveseed=N: another island's)
			FParse::Value(FCommandLine::Get(), TEXT("bfcaveseed="), CaveSeed);
			MakeIsland(CaveSeed);
			bGoWhenReady = false;
			Next(731);
		}
		break;
	case 731:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			LifeShot = 0;
			Next(732);
		}
		break;
	case 732:
		{
			ABFWorld* I = M->GetIsland();
			if (AutoFrame == 1)
			{
				I->SetHours(11.f);
				SetFreeCam(true);
			}
			TArray<int32> Dens;
			for (int32 c = 0; c < I->NumCaves(); ++c)
			{
				if (I->CaveKind(c) != 1 && I->CaveKind(c) != 4)
				{
					Dens.Add(c);
				}
			}
			if (AutoFrame == 1)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave look: island %d: %s"), I->GetSeed(), *I->CaveSummary);
				for (const int32 c : Dens)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave look: %d (kind %d): %s"), c, int32(I->CaveKind(c)), *I->CaveText(c));
				}
			}
			const int32 Views = 8;
			const int32 K = int32(AutoClock / 2.f);
			if (K >= Dens.Num() * Views)
			{
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] cave look: %d dens pictured"), Dens.Num());
				SetFreeCam(false);
				Next(9);
				break;
			}
			const int32 Cv = Dens[K / Views], V = K % Views;
			const FVector Out = I->CaveOut(Cv), Side = FVector::CrossProduct(Out, FVector::UpVector);
			const FVector T = I->CaveFloor(Cv)[0] + FVector(0.f, 0.f, 220.f);
			static const TCHAR* Names[] = { TEXT("eye"), TEXT("high"), TEXT("left"), TEXT("right"), TEXT("side"), TEXT("far"), TEXT("in"), TEXT("back") };
			const float Big = I->CaveKind(Cv) == 3 ? 2.2f : 1.f;          // (a cavern's mouth: the camera as much further off)
			const FVector Offs[] = { Out * 1400.f + FVector(0, 0, 0), Out * 3000.f + FVector(0, 0, 1400), (Out + Side).GetSafeNormal() * 2200.f + FVector(0, 0, 350),
				(Out - Side).GetSafeNormal() * 2200.f + FVector(0, 0, 350), Side * 2600.f + Out * 400.f + FVector(0, 0, 250), Out * 7000.f - Side * 3500.f + FVector(0, 0, 2200) };
			const TArray<FVector>& Fl = I->CaveFloor(Cv);
			FVector P, Look = T;
			if (V < 6)
			{
				P = T + Offs[V] * Big;
				float G = float(P.Z);
				if (I->GroundAt(FVector2D(P), G))
				{
					P.Z = FMath::Max(float(P.Z), G + 170.f);
				}
			}
			else if (V == 6)
			{
				// just inside the mouth, looking in (the far end)
				P = Fl[1] + FVector(0.f, 0.f, 250.f) + Out * 300.f;
				Look = Fl.Last() + FVector(0.f, 0.f, 300.f);
			}
			else
			{
				// at the far end, looking about (back toward the mouth, a little up)
				P = Fl.Last() + FVector(0.f, 0.f, 300.f) + (Fl[Fl.Num() - 2] - Fl.Last()).GetSafeNormal2D() * 300.f;
				Look = Fl[FMath::Max(Fl.Num() - 3, 0)] + FVector(0.f, 0.f, 600.f);
			}
			FreeCam->SetActorLocationAndRotation(P, (Look - P).Rotation());
			if (AutoClock - K * 2.f > 1.5f && AutoClock - DeltaTime - K * 2.f <= 1.5f)
			{
				Shot(*FString::Printf(TEXT("cavelook_%d_k%d_%s"), K / Views, int32(I->CaveKind(Cv)), Names[V]));
			}
		}
		break;
	// Ravines and waterfalls (-bflandform, -bfcaveseed=N for the island): what the island has (logged); each ravine from
	// its rim (across, down into it), from its floor looking up it, and from above its head; the two biggest waterfalls
	// from in front and from the side; then the O panel with the island's places. 2.5 s a view.
	case 740:
		if (AutoClock > 4.f)
		{
			int32 LandSeed = 20261112;
			FParse::Value(FCommandLine::Get(), TEXT("bfcaveseed="), LandSeed);
			MakeIsland(LandSeed);
			bGoWhenReady = false;
			Next(741);
		}
		break;
	case 741:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([](const FBFSpecies& S) { return S.Key == TEXT("raptor"); });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(742);
		}
		break;
	case 742:
		{
			ABFWorld* I = M->GetIsland();
			// (2026-11-16: -bfterrain: a tour of the ground instead: two of each kind of land, the peak, a sea cliff and an
			// islet, each from near the ground and from high and far off on another side; 2.5 s a view)
			static const bool bTour = FParse::Param(FCommandLine::Get(), TEXT("bfterrain"));
			if (bTour)
			{
				static TArray<FVector> TourAt;
				static TArray<FString> TourNames;
				if (AutoFrame == 1)
				{
					I->SetHours(10.5f);
					SetFreeCam(true);
					TourAt.Reset();
					TourNames.Reset();
					I->TourPoints(TourAt, TourNames);
					for (int32 t = 0; t < TourAt.Num(); ++t)
					{
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] terrain tour: %s at %s"), *TourNames[t], *TourAt[t].ToString());
					}
				}
				const int32 K = int32(AutoClock / 2.5f);
				if (K >= 2 * TourAt.Num())
				{
					SetFreeCam(false);
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] terrain tour: %d views"), K);
					Next(9);
					break;
				}
				const FVector At = TourAt[K / 2];
				const bool bHigh = (K % 2) == 1;
				const float Az = float(K / 2) * 1.7f + (bHigh ? 2.4f : 0.f);
				const FVector Dir(FMath::Cos(Az), FMath::Sin(Az), 0.f);
				FVector P = At + Dir * (bHigh ? 9000.f : 2600.f);
				float Gp = float(P.Z);
				I->GroundAt(FVector2D(P), Gp);
				P.Z = FMath::Max(float(At.Z), FMath::Max(Gp, 0.f)) + (bHigh ? 4500.f : 260.f);
				const FVector Look = At + FVector(0.f, 0.f, bHigh ? 0.f : 150.f);
				FreeCam->SetActorLocationAndRotation(P, (Look - P).Rotation());
				if (AutoClock - K * 2.5f > 2.f && AutoClock - DeltaTime - K * 2.5f <= 2.f)
				{
					Shot(*FString::Printf(TEXT("tour_%s_%s"), *TourNames[K / 2], bHigh ? TEXT("high") : TEXT("low")));
				}
				break;
			}
			// (2026-11-17: -bftop: straight down over every fall (wide and close), every ravine and every cave mouth; the
			// way the water or the ravine runs is up the picture; 2.5 s a view)
			static const bool bTop = FParse::Param(FCommandLine::Get(), TEXT("bftop"));
			if (bTop)
			{
				static TArray<FVector> TopAt, TopDir;
				static TArray<float> TopH;
				static TArray<FString> TopNames;
				if (AutoFrame == 1)
				{
					I->SetHours(11.5f);
					SetFreeCam(true);
					TopAt.Reset(); TopDir.Reset(); TopH.Reset(); TopNames.Reset();
					int32 Fn = 0;
					for (const ABFWorld::FBFFall& Fa : I->Falls)
					{
						if (Fa.DropM < 6.f)
						{
							continue;
						}
						const FVector Mid = (Fa.Top + Fa.Bottom) * 0.5f;
						TopAt.Add(Mid); TopDir.Add(Fa.Dir); TopH.Add(9000.f + 120.f * Fa.DropM); TopNames.Add(FString::Printf(TEXT("fall%d_wide"), Fn));
						TopAt.Add(Fa.Top + Fa.Dir * 300.f); TopDir.Add(Fa.Dir); TopH.Add(3200.f); TopNames.Add(FString::Printf(TEXT("fall%d_lip"), Fn));
						TopAt.Add(FVector(Fa.Bottom.X, Fa.Bottom.Y, FMath::Max(Fa.Top.Z, Fa.Bottom.Z))); TopDir.Add(Fa.Dir); TopH.Add(2600.f + FMath::Max(Fa.PoolCm, 0.f)); TopNames.Add(FString::Printf(TEXT("fall%d_pool"), Fn));
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] top view: fall%d %.1f m, %.0f m wide, pool %.0f, top %s, bottom %s, dir %s"), Fn, Fa.DropM, Fa.WidthCm / 100.f, Fa.PoolCm, *Fa.Top.ToString(), *Fa.Bottom.ToString(), *Fa.Dir.ToString());
						I->LogAround(Fa.Bottom, 8);
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] top view: fall%d at the lip"), Fn);
						I->LogAround(Fa.Top, 3);
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] top view: fall%d 32 m downstream"), Fn);
						I->LogAround(Fa.Top + Fa.Dir * 3200.f, 6);
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] top view: fall%d 24 m upstream"), Fn);
						I->LogAround(Fa.Top - Fa.Dir * 2400.f, 6);
						++Fn;
					}
					for (int32 r = 0; r < I->Ravines.Num(); ++r)
					{
						const ABFWorld::FBFRavine& Rv = I->Ravines[r];
						if (Rv.Path.Num() < 2)
						{
							continue;
						}
						const float Len = float(FVector::Dist2D(Rv.Path[0], Rv.Path.Last()));
						TopAt.Add((Rv.Path[0] + Rv.Path.Last()) * 0.5f); TopDir.Add((Rv.Path.Last() - Rv.Path[0]).GetSafeNormal2D());
						TopH.Add(FMath::Max(0.9f * Len, 6000.f)); TopNames.Add(FString::Printf(TEXT("ravine%d_whole"), r));
						const int32 Mi = Rv.Path.Num() / 2;
						TopAt.Add(Rv.Path[Mi]); TopDir.Add((Rv.Path[FMath::Min(Mi + 1, Rv.Path.Num() - 1)] - Rv.Path[FMath::Max(Mi - 1, 0)]).GetSafeNormal2D());
						TopH.Add(4000.f + 100.f * Rv.DeepM); TopNames.Add(FString::Printf(TEXT("ravine%d_mid"), r));
					}
					int32 Cn = 0;
					for (const ABFWorld::FBFPlace& Pl : I->Places)
					{
						if (Pl.Kind < 10 && Cn < 6)
						{
							TopAt.Add(Pl.At); TopDir.Add(FVector(1.f, 0.f, 0.f)); TopH.Add(4500.f); TopNames.Add(FString::Printf(TEXT("cave%d"), Cn++));
						}
					}
					const float Half = 0.5f * (ABFWorld::N - 1) * ABFWorld::CellCm;
					TopAt.Add(I->GridOrigin() + FVector(Half, Half, 0.f)); TopDir.Add(FVector(1.f, 0.f, 0.f)); TopH.Add(2.3f * Half); TopNames.Add(TEXT("island"));
					for (int32 t = 0; t < TopAt.Num(); ++t)
					{
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] top view: %s at %s"), *TopNames[t], *TopAt[t].ToString());
					}
				}
				const int32 K = int32(AutoClock / 2.5f);
				if (K >= TopAt.Num())
				{
					SetFreeCam(false);
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] top views: %d"), K);
					Next(9);
					break;
				}
				float G = float(TopAt[K].Z);
				I->GroundAt(FVector2D(TopAt[K]), G);
				const FVector P = FVector(TopAt[K].X, TopAt[K].Y, FMath::Max(FMath::Max(G, float(TopAt[K].Z)), 0.f) + TopH[K]);
				FreeCam->SetActorLocationAndRotation(P, FRotator(-89.9f, TopDir[K].Rotation().Yaw, 0.f));
				if (AutoClock - K * 2.5f > 2.f && AutoClock - DeltaTime - K * 2.5f <= 2.f)
				{
					Shot(*FString::Printf(TEXT("top_%s"), *TopNames[K]));
				}
				break;
			}
			if (AutoFrame == 1)
			{
				I->SetHours(11.f);
				SetFreeCam(true);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] landform: island %d: %s; %s; %s; %d places"), I->GetSeed(), *I->LakeText, *I->LandformText, *I->CaveSummary, I->Places.Num());
				for (const ABFWorld::FBFPlace& Pl : I->Places)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] landform: place %s at %s"), *Pl.Name, *Pl.At.ToString());
				}
				for (const ABFWorld::FBFFall& Fa : I->Falls)
				{
					if (Fa.DropM >= 6.f)
					{
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] landform: fall %.1f m, %.0f m wide, top %s"), Fa.DropM, Fa.WidthCm / 100.f, *Fa.Top.ToString());
						UE_LOG(LogTemp, Display, TEXT("[BoneForge] landform: dir %s"), *Fa.Dir.ToString());
						I->LogAround(Fa.Top);
					}
				}
			}
			TArray<int32> Big;
			for (int32 f = 0; f < I->Falls.Num(); ++f)
			{
				Big.Add(f);
			}
			Big.Sort([&](int32 A, int32 B) { return I->Falls[A].DropM > I->Falls[B].DropM; });
			Big.SetNum(FMath::Min(Big.Num(), 2));
			const int32 RavViews = 3 * I->Ravines.Num(), FallViews = 6 * Big.Num();
			// (2026-11-17: a camera inside a rock piece (a narrow ravine's wall beds) saw only grey faces: brought in along the
			// line to what it looks at, to just short of the first rock between them)
			auto ClearOfRock = [&](const FVector& From, const FVector& To) -> FVector
			{
				FHitResult Hit;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(BFLandCam), false);
				if (GetPawn())
				{
					Q.AddIgnoredActor(GetPawn());
				}
				if (GetWorld()->LineTraceSingleByChannel(Hit, To, From, ECC_Visibility, Q) && Hit.Distance > 300.f)
				{
					return Hit.Location + (To - From).GetSafeNormal() * 120.f;
				}
				return From;
			};
			const int32 K = int32(AutoClock / 2.5f);
			FVector P, Look;
			FString Name;
			if (K < RavViews)
			{
				const ABFWorld::FBFRavine& Rv = I->Ravines[K / 3];
				const int32 Mid = Rv.Path.Num() / 2;
				const FVector A = Rv.Path[Mid], Dir = (Rv.Path[FMath::Min(Mid + 1, Rv.Path.Num() - 1)] - Rv.Path[FMath::Max(Mid - 1, 0)]).GetSafeNormal2D();
				const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector);
				float G = float(A.Z);
				I->GroundAt(FVector2D(A + Side * 3500.f), G);
				if (K % 3 == 0)
				{
					P = A + Side * 3500.f;
					P.Z = G + 600.f;
					Look = A + FVector(0.f, 0.f, 300.f);
					Name = FString::Printf(TEXT("ravine%d_rim"), K / 3);
				}
				else if (K % 3 == 1)
				{
					P = Rv.Path.Last() + (Rv.Path.Last() - Rv.Path[Rv.Path.Num() - 2]).GetSafeNormal2D() * 1500.f + FVector(0.f, 0.f, 400.f);
					float Gp = float(P.Z);
					I->GroundAt(FVector2D(P), Gp);
					P.Z = FMath::Max(float(P.Z), Gp + 250.f);
					Look = Rv.Path[FMath::Max(Mid - 1, 0)] + FVector(0.f, 0.f, 800.f);
					Name = FString::Printf(TEXT("ravine%d_floor"), K / 3);
				}
				else
				{
					P = Rv.Path[0] - Dir * 2500.f + FVector(0.f, 0.f, 3500.f);
					float Gh = float(P.Z);
					I->GroundAt(FVector2D(P), Gh);
					P.Z = FMath::Max(float(P.Z), Gh + 1500.f);          // (over the hill behind the head, not in it)
					Look = Rv.Path[FMath::Min(2, Rv.Path.Num() - 1)];
					Name = FString::Printf(TEXT("ravine%d_head"), K / 3);
				}
			}
			else if (K < RavViews + FallViews)
			{
				const ABFWorld::FBFFall& Fa = I->Falls[Big[(K - RavViews) / 6]];
				const FVector Mid = (Fa.Top + Fa.Bottom) * 0.5f;
				const FVector FSide = FVector::CrossProduct(Fa.Dir, FVector::UpVector);
				const int32 Fv = (K - RavViews) % 6;
				if (Fv == 2 || Fv == 3)
				{
					// the hollow behind it: from out in the pool, off to one side, low; and from inside it, looking out
					const FVector Ho = Fa.HollowAt.IsZero() ? Fa.Bottom - Fa.Dir * 300.f : Fa.HollowAt;
					if (Fv == 2)
					{
						P = Fa.Bottom + Fa.Dir * FMath::Max(1.1f * Fa.PoolCm, 900.f) + FSide * FMath::Max(0.8f * Fa.PoolCm, 700.f);
						P.Z = Fa.Bottom.Z + 380.f;
						Look = Ho + FVector(0.f, 0.f, 100.f);
						Name = FString::Printf(TEXT("fall%d_hollow"), (K - RavViews) / 6);
					}
					else
					{
						// (at one end of it, looking along it to the other end and out)
						P = Ho + FSide * 0.65f * Fa.HollowCm + Fa.Dir * 0.2f * Fa.HollowCm;
						P.Z = Fa.Bottom.Z + 170.f;
						Look = Ho - FSide * Fa.HollowCm + Fa.Dir * 0.5f * Fa.HollowCm + FVector(0.f, 0.f, 120.f);
						Name = FString::Printf(TEXT("fall%d_inside"), (K - RavViews) / 6);
					}
					FreeCam->SetActorLocationAndRotation(ClearOfRock(P, Look), (Look - ClearOfRock(P, Look)).Rotation());
					if (AutoClock - float(K) * 2.5f > 2.f && AutoClock - DeltaTime - float(K) * 2.5f <= 2.f)
					{
						Shot(*FString::Printf(TEXT("land_%s"), *Name));
					}
					break;
				}
				if (Fv == 4 || Fv == 5)
				{
					// (2026-11-16) its lip: from up the river, low, looking over the edge; and from the bank above, beside it
					P = Fv == 4 ? Fa.Top - Fa.Dir * 2600.f + FVector(0.f, 0.f, 450.f) : Fa.Top - Fa.Dir * 900.f + FSide * (1800.f + Fa.WidthCm) + FVector(0.f, 0.f, 1600.f);
					float Gl = float(P.Z);
					I->GroundAt(FVector2D(P), Gl);
					P.Z = FMath::Max(float(P.Z), Gl + 250.f);
					Look = Fv == 4 ? Fa.Top + Fa.Dir * 800.f - FVector(0.f, 0.f, 300.f) : Fa.Top + Fa.Dir * 300.f - FVector(0.f, 0.f, 500.f);
					FreeCam->SetActorLocationAndRotation(ClearOfRock(P, Look), (Look - ClearOfRock(P, Look)).Rotation());
					if (AutoClock - float(K) * 2.5f > 2.f && AutoClock - DeltaTime - float(K) * 2.5f <= 2.f)
					{
						Shot(*FString::Printf(TEXT("land_fall%d_%s"), (K - RavViews) / 6, Fv == 4 ? TEXT("lip") : TEXT("lipside")));
					}
					break;
				}
				if (Fv == 0)
				{
					P = Fa.Bottom + Fa.Dir * (1500.f + 100.f * Fa.DropM) + FVector(0.f, 0.f, 300.f);
					Name = FString::Printf(TEXT("fall%d_front"), (K - RavViews) / 6);
				}
				else
				{
					// (from down the gorge and off to one side, above its rim: beside it, the camera was in the rock)
					P = Fa.Bottom + Fa.Dir * (1500.f + 60.f * Fa.DropM) + FVector::CrossProduct(Fa.Dir, FVector::UpVector) * (500.f + 30.f * Fa.DropM) + FVector(0.f, 0.f, 600.f + 60.f * Fa.DropM);
					Name = FString::Printf(TEXT("fall%d_side"), (K - RavViews) / 6);
				}
				float Gp = float(P.Z);
				I->GroundAt(FVector2D(P), Gp);
				P.Z = FMath::Max(float(P.Z), Gp + 300.f);
				Look = Mid;
			}
			else if (K == RavViews + FallViews || K == RavViews + FallViews + 1)
			{
				// the steepest river near the start (its boulders), from beside it and from down it
				FVector At, Dn;
				if (!I->SteepRiver(I->StartPoint(), 150000.f, At, Dn))
				{
					At = I->StartPoint();
					Dn = FVector(1.f, 0.f, 0.f);
				}
				P = K == RavViews + FallViews ? At + FVector::CrossProduct(Dn, FVector::UpVector) * 1400.f + FVector(0.f, 0.f, 500.f) : At + Dn * 2200.f + FVector(0.f, 0.f, 350.f);
				float Gp = float(P.Z);
				I->GroundAt(FVector2D(P), Gp);
				P.Z = FMath::Max(float(P.Z), Gp + 250.f);
				Look = At;
				Name = K == RavViews + FallViews ? TEXT("rapids_side") : TEXT("rapids_down");
			}
			else if (K == RavViews + FallViews + 2)
			{
				// the O panel
				if (!bWatchPanel && WatchPanel.IsValid())
				{
					bWatchPanel = true;
					WatchBack->SetVisibility(EVisibility::HitTestInvisible);
				}
				P = FreeCam->GetActorLocation();
				Look = P + FreeCam->GetActorForwardVector() * 100.f;
				Name = TEXT("places_panel");
			}
			else if (K == RavViews + FallViews + 3 || K == RavViews + FallViews + 4)
			{
				// (2026-11-16) the sea round the island: islets, stacks and shoals; from high over the coast, looking out
				if (bWatchPanel)
				{
					bWatchPanel = false;
					WatchBack->SetVisibility(EVisibility::Collapsed);
				}
				const float Half = 0.5f * (ABFWorld::N - 1) * ABFWorld::CellCm;
				const FVector Mid = I->GridOrigin() + FVector(Half, Half, 0.f);
				const FVector Out = K == RavViews + FallViews + 3 ? FVector(1.f, 1.f, 0.f).GetSafeNormal() : FVector(-1.f, 0.4f, 0.f).GetSafeNormal();
				P = Mid + Out * 0.55f * Half + FVector(0.f, 0.f, 60000.f);
				Look = Mid + Out * 1.25f * Half;
				Name = K == RavViews + FallViews + 3 ? TEXT("sea_ne") : TEXT("sea_w");
			}
			else
			{
				if (bWatchPanel)
				{
					bWatchPanel = false;
					WatchBack->SetVisibility(EVisibility::Collapsed);
				}
				SetFreeCam(false);
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] landform: %d views"), K);
				Next(9);
				break;
			}
			FreeCam->SetActorLocationAndRotation(ClearOfRock(P, Look), (Look - ClearOfRock(P, Look)).Rotation());
			if (AutoClock - K * 2.5f > 2.f && AutoClock - DeltaTime - K * 2.5f <= 2.f)
			{
				Shot(*FString::Printf(TEXT("land_%s"), *Name));
			}
		}
		break;
	case 720:
		if (AutoClock > 4.f)
		{
			MakeIsland(20261112);
			bGoWhenReady = false;
			Next(721);
		}
		break;
	case 721:
		if (M->GetIsland() && M->GetIsland()->IsReady())
		{
			Area = EBFSpawnArea::World;
			FString Kind = TEXT("raptor");          // (-bffxkind=trex, trike, ...: another animal's tracks)
			FParse::Value(FCommandLine::Get(), TEXT("bffxkind="), Kind);
			AutoSpecies = M->GetSpecies().IndexOfByPredicate([&](const FBFSpecies& S) { return S.Key == Kind; });
			Pick(FMath::Max(AutoSpecies, 0));
			Next(722);
		}
		break;
	case 722:
		{
			ABFWorld* I = M->GetIsland();
			if (AutoFrame == 1)
			{
				I->SetHours(6.5f);
				SetFreeCam(true);
			}
			if (!D)
			{
				break;
			}
			// The mist: from above the raptor, over the land.
			if (AutoClock < 6.f)
			{
				const FVector P = D->GetActorLocation() + FVector(0.f, 0.f, 1500.f);
				FreeCam->SetActorLocationAndRotation(P, FRotator(-12.f, D->GetActorRotation().Yaw, 0.f));
				if (AutoClock > 4.f && AutoClock - DeltaTime <= 4.f)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] world fx: mist %.2f at 6:30"), M->GetMist());
					Shot(TEXT("fx_mist"));
				}
				break;
			}
			if (AutoClock < 6.1f)
			{
				I->SetHours(11.f);
				AutoDir = D->GetActorForwardVector().GetSafeNormal2D();
				bAutoForward = true;
				bAutoRun = true;
			}
			// Then it stops, and the camera looks straight down on the tracks it left (2026-11-15: prints with toes).
			if (AutoClock >= 9.f && AutoClock < 12.f)
			{
				if (AutoClock - DeltaTime < 9.f)
				{
					bAutoForward = false;
					bAutoRun = false;
					FxRiver = D->GetActorLocation() - D->GetActorForwardVector() * 400.f;          // (borrowed: where to look)
				}
				float G = float(FxRiver.Z);
				I->GroundAt(FVector2D(FxRiver), G);
				const FVector T(FxRiver.X, FxRiver.Y, G);
				const FVector P = T + D->GetActorForwardVector() * 60.f + FVector(0.f, 0.f, 330.f);
				FreeCam->SetActorLocationAndRotation(P, (T - P).Rotation());
				if (AutoClock > 11.f && AutoClock - DeltaTime <= 11.f)
				{
					Shot(TEXT("fx_prints_close"));
				}
				break;
			}
			if (AutoClock >= 12.f && AutoClock - DeltaTime < 12.f)
			{
				bAutoForward = true;          // on again
				bAutoRun = true;
				FxRiver = FVector::ZeroVector;
			}
			// Running: the camera behind and above, on its trail.
			if (AutoClock < 18.f)
			{
				const FVector Fw = D->GetActorForwardVector();
				// (behind and well above, looking down along its tracks)
				const FVector P = D->GetActorLocation() - Fw * 300.f + FVector::CrossProduct(Fw, FVector::UpVector) * 250.f + FVector(0.f, 0.f, 320.f);
				FreeCam->SetActorLocationAndRotation(P, (D->GetActorLocation() - Fw * 600.f - P).Rotation());
				if ((AutoClock > 8.f && AutoClock - DeltaTime <= 8.f) || (AutoClock > 14.f && AutoClock - DeltaTime <= 14.f))
				{
					Shot(*FString::Printf(TEXT("fx_trail_%02d"), int32(AutoClock)));
				}
				break;
			}
			// Into water: the nearest shore, a few metres in.
			if (AutoClock < 18.1f)
			{
				FVector Stand, Water;
				if (I->NearestShore(D->GetActorLocation(), 60000.f, Stand, Water))
				{
					const FVector In = Stand + (Water - Stand).GetSafeNormal2D() * 250.f;
					float G = float(In.Z);
					I->GroundAt(FVector2D(In), G);
					D->SetActorLocation(FVector(In.X, In.Y, G + D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f), false, nullptr, ETeleportType::TeleportPhysics);
					AutoDir = (FVector::CrossProduct((Water - Stand).GetSafeNormal2D(), FVector::UpVector) + 0.35f * (Water - Stand).GetSafeNormal2D()).GetSafeNormal();          // along the shore, edging out into the shallows
				}
				FxRiver = FVector::ZeroVector;
				bAutoForward = true;
				bAutoRun = false;          // (wading at a walk: it stays in the shallows for the picture)
			}
			if (AutoClock < 26.f)
			{
				const FVector Fw = D->GetActorForwardVector();
				FVector Wd = FVector::CrossProduct(Fw, FVector::UpVector);
				float WS = 0.f;
				if (!M->GetIsland()->WaterAt(D->GetActorLocation() + Wd * 600.f + FVector(0.f, 0.f, 50.f), WS))
				{
					Wd = -Wd;          // (from out over the water, clear of the trees on the bank)
				}
				const FVector P = D->GetActorLocation() + Wd * 380.f + Fw * 150.f + FVector(0.f, 0.f, 160.f);
				FreeCam->SetActorLocationAndRotation(P, (D->GetActorLocation() - P).Rotation());
				FxWet = FMath::Max(FxWet, D->GetWet());
				if (AutoClock > 21.f && AutoClock - DeltaTime <= 21.f)
				{
					Shot(TEXT("fx_splash"));
				}
				break;
			}
			// The steepest river stretch within 600 m: foam.
			if (AutoClock < 26.1f)
			{
				bAutoForward = false;
				bAutoRun = false;
				AutoDir = FVector::ZeroVector;
				FVector At, Dn;
				if (I->SteepRiver(D->GetActorLocation(), 60000.f, At, Dn))
				{
					FxRiver = At;
					FxRiverDn = Dn;
				}
			}
			if (!FxRiver.IsZero())
			{
				const FVector Side = FVector::CrossProduct(FxRiverDn, FVector::UpVector);
				FVector P = FxRiver + Side * 1500.f - FxRiverDn * 600.f;
				float G = float(P.Z);
				I->GroundAt(FVector2D(P), G);
				P.Z = FMath::Max(G + 300.f, float(FxRiver.Z) + 400.f);
				FreeCam->SetActorLocationAndRotation(P, (FxRiver - P).Rotation());
				if (AutoClock > 30.f && AutoClock - DeltaTime <= 30.f)
				{
					Shot(TEXT("fx_foam"));
				}
			}
			if (AutoClock > 32.f)
			{
				ABFEffects* E = ABFEffects::Get(GetWorld());
				const bool bOk = E && E->PrintsMade > 10 && E->SplashesMade > 0 && FxWet > 0.5f;
				UE_LOG(LogTemp, Display, TEXT("[BoneForge] world fx: %s %d prints made (%d now; %d feet too high off the ground, %d over no ground), %d dust puffs, %d splashes, %d puffs in the air; wet %.2f after the water"),
					bOk ? TEXT("PASS") : TEXT("FAIL"), E ? E->PrintsMade : 0, E ? E->NumPrints() : 0, E ? E->PrintsInAir : 0, E ? E->PrintsNoGround : 0, E ? E->DustMade : 0, E ? E->SplashesMade : 0, E ? E->NumPuffs() : 0, FxWet);
				SetFreeCam(false);
				Next(9);
			}
		}
		break;
	default:
		break;
	}
}
