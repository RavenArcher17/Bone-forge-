#include "BFSpliceStage.h"

#include <cwchar>

#include "BFGameMode.h"
#include "BFPlayerController.h"
#include "Containers/Ticker.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	struct FKind { const TCHAR* Key; const TCHAR* Short; const TCHAR* Full; FLinearColor Color; };
	const FKind Kinds[] =
	{
		{ TEXT("trex"), TEXT("T. rex"), TEXT("Tyrannosaurus rex"), FLinearColor(0.62f, 0.20f, 0.14f) },
		{ TEXT("trike"), TEXT("Trike"), TEXT("Triceratops"), FLinearColor(0.16f, 0.42f, 0.16f) },
		{ TEXT("raptor"), TEXT("Raptor"), TEXT("Velociraptor"), FLinearColor(0.70f, 0.50f, 0.10f) },
		{ TEXT("ankylo"), TEXT("Ankylo"), TEXT("Ankylosaurus"), FLinearColor(0.34f, 0.26f, 0.50f) },
		{ TEXT("brachio"), TEXT("Brachio"), TEXT("Brachiosaurus"), FLinearColor(0.14f, 0.38f, 0.52f) },
		{ TEXT("ptera"), TEXT("Ptera"), TEXT("Pteranodon"), FLinearColor(0.62f, 0.30f, 0.46f) },
	};
	struct FRow { const TCHAR* Section; const TCHAR* Label; };
	const FRow Rows[] = { { TEXT("Head"), TEXT("Head") }, { TEXT("Neck"), TEXT("Neck") }, { TEXT("Tail"), TEXT("Tail") },
		{ TEXT("Hip"), TEXT("Legs") }, { TEXT("Shoulder"), TEXT("Arms") } };
	// Advanced mode: the sections whose single bones can change (feet, hands and skulls are bone parts: whole sections
	// only), and each species' button in a bone's row (short, so a row fits; and unlike the sections' buttons).
	bool HasBones(const FString& Section)
	{
		return Section == TEXT("Neck") || Section == TEXT("Tail") || Section == TEXT("Hip") || Section == TEXT("Shoulder");
	}
	const TCHAR* BoneCode(const FString& Key)
	{
		return Key == TEXT("trex") ? TEXT("Rex") : Key == TEXT("trike") ? TEXT("Tri") : Key == TEXT("raptor") ? TEXT("Rap")
			: Key == TEXT("ankylo") ? TEXT("Ank") : Key == TEXT("brachio") ? TEXT("Bra") : TEXT("Pte");
	}
	const TCHAR* SectionWord(const FString& Section)
	{
		return Section == TEXT("Hip") ? TEXT("legs") : Section == TEXT("Shoulder") ? TEXT("arms") : Section == TEXT("Neck") ? TEXT("neck") : TEXT("tail");
	}

	const FVector StageAt(-3000.f, 0.f, 0.f);       // on the flat field

	FLinearColor ColorOf(const FString& FullName)
	{
		for (const FKind& K : Kinds)
		{
			if (FullName == K.Full)
			{
				return K.Color;
			}
		}
		return FLinearColor(0.5f, 0.45f, 0.37f);
	}

	FVector ReadVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = O->GetArrayField(Field);
		return FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
	}

	FSlateColor VerdictColor(const FString& V)
	{
		if (V == TEXT("buckles") || V == TEXT("grounded"))
		{
			return FLinearColor(1.f, 0.42f, 0.32f);
		}
		if (V == TEXT("stands") || V == TEXT("glides"))
		{
			return FLinearColor(1.f, 0.78f, 0.3f);
		}
		return FLinearColor(0.55f, 0.92f, 0.55f);
	}
}

ABFSpliceStage::ABFSpliceStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Fill = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fill"));
	Fill->SetupAttachment(Camera);
	Fill->SetCastShadows(false);
	Fill->SetVisibility(false);
	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetVisibility(false);
}

void ABFSpliceStage::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Panel.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Panel.ToSharedRef());
	}
	if (Proc.IsValid())
	{
		FPlatformProcess::TerminateProc(Proc);
		FPlatformProcess::CloseProc(Proc);
	}
	Super::EndPlay(Reason);
}

void ABFSpliceStage::Open(APlayerController* Controller, const FString& RigFolder)
{
	PC = Controller;
	Rigs = RigFolder;
	WorkDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Splice")));
	IFileManager::Get().MakeDirectory(*WorkDir, true);
	SetActorLocation(StageAt);
	if (!Panel.IsValid())
	{
		BuildPanel();
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->AddViewportWidgetContent(Panel.ToSharedRef(), 30);
		}
	}
	Panel->SetVisibility(EVisibility::Visible);
	Fill->SetVisibility(true);
	bOpen = true;
	bWantBack = false;
	SetActorHiddenInGame(false);
	PC->SetViewTarget(this);
	PC->SetShowMouseCursor(true);
	FInputModeGameAndUI In;
	In.SetHideCursorDuringCapture(false);
	PC->SetInputMode(In);
	RecipeChanged();
}

void ABFSpliceStage::Close()
{
	bOpen = false;
	if (Panel.IsValid())
	{
		Panel->SetVisibility(EVisibility::Collapsed);
	}
	Fill->SetVisibility(false);
	SetActorHiddenInGame(true);
}

void ABFSpliceStage::SetExtraPair(const FString& Part, const FString& Species)
{
	FSection& S = Sections.FindOrAdd(TEXT("Pair"));
	if (ExtraPart == Part && S.Donor == Species)
	{
		ExtraPart.Reset();
	}
	else
	{
		ExtraPart = Part;
		S.Donor = Species;
		S.bFit = true;
		S.Scale = 1.f;
	}
	RecipeChanged();
}

void ABFSpliceStage::ChangeExtraAt(float By)
{
	ExtraAt = FMath::Clamp(FMath::RoundToFloat((ExtraAt + By) * 100.f) / 100.f, 0.f, 1.f);
	RecipeChanged();
}

void ABFSpliceStage::ToggleClub()
{
	bClub = !bClub;
	if (bClub)
	{
		FSection& S = Sections.FindOrAdd(TEXT("Club"));
		S.bFit = true;
		S.Scale = 1.f;
	}
	RecipeChanged();
}

TSharedPtr<SWidget> ABFSpliceStage::FindButton(const FString& Label, int32 Nth) const
{
	return Panel.IsValid() ? FindButtonIn(Panel.ToSharedRef(), Label, Nth) : nullptr;
}

void ABFSpliceStage::ClickWidget(const TSharedPtr<SWidget>& W)
{
	if (W.IsValid())
	{
		const FVector2D At = W->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
		SendMouse(At, EKeys::LeftMouseButton, true);
		SendMouse(At, EKeys::LeftMouseButton, false);
	}
}

TSharedPtr<SWidget> ABFSpliceStage::FindButtonIn(TSharedRef<SWidget> Root, const FString& Label, int32 Nth)
{
	int32 Seen = 0;
	TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>, TSharedPtr<SWidget>)> Walk = [&](TSharedRef<SWidget> W, TSharedPtr<SWidget> Button) -> TSharedPtr<SWidget>
	{
		if (W->GetType() == FName(TEXT("SButton")))
		{
			Button = W;
		}
		if (Button.IsValid() && W->GetType() == FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(W)->GetText().ToString() == Label && Seen++ == Nth)
		{
			return Button;
		}
		FChildren* Kids = W->GetChildren();
		for (int32 i = 0; Kids && i < Kids->Num(); ++i)
		{
			if (TSharedPtr<SWidget> Found = Walk(Kids->GetChildAt(i), Button))
			{
				return Found;
			}
		}
		return nullptr;
	};
	return Walk(Root, nullptr);
}

void ABFSpliceStage::SendMouse(const FVector2D& At, FKey Button, bool bDown)
{
	FSlateApplication& A = FSlateApplication::Get();
	TSet<FKey> Pressed;
	if (bDown)
	{
		Pressed.Add(Button);
	}
	const FPointerEvent Event(FSlateApplication::CursorPointerIndex, At, At, Pressed, Button, 0.f, A.GetModifierKeys());
	A.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, At, At, TSet<FKey>(), EKeys::Invalid, 0.f, A.GetModifierKeys()));
	if (bDown)
	{
		A.ProcessMouseButtonDownEvent(nullptr, Event);
	}
	else
	{
		A.ProcessMouseButtonUpEvent(Event);
	}
}

void ABFSpliceStage::StartClickTest()
{
	// Row order on the panel: Head, Neck, Tail, Legs, Arms; then the extra pair's Legs, Arms. A selected donor shows as
	// [Name], so it leaves the count of plain Names.
	ClickScript = {
		{ TEXT("Trike"), 0 },            // start from a Triceratops
		{ TEXT("Raptor"), 1 },           // its head: a raptor's (0 was "start from")
		{ TEXT(" + "), 1 },              // head bigger (0 is the Whole body's)
		{ TEXT(" + "), 2 },              // head thicker
		{ TEXT(" - "), 3 },              // neck smaller
		{ TEXT("Fit"), 3 },              // legs' Fit
		{ TEXT(" + "), 9 },              // legs: the Move button (Whole body; Size, Thick for head, neck, tail; then Size, Thick, Move)
		{ TEXT("Brachio"), 5 },          // its arms from a Brachiosaurus
		{ TEXT("Ankylo"), 6 },           // extra pair of legs, from an Ankylosaurus
		{ TEXT("Tail club"), 0 },
		{ TEXT("[Tail club]"), 0 },      // and off again
		{ TEXT("Tail club"), 0 },
		{ TEXT("Grow body"), 0 },
		{ TEXT("@shot"), 0 },
		{ TEXT("@view"), 0 },
		{ TEXT("Start over"), 0 },
		{ TEXT("@esc"), 0 },
	};
	ClickStep = 0;
	ClickWait = 0;
	ClickTarget.Reset();
}

void ABFSpliceStage::StartWingsClickTest()
{
	// Wings (-bfclicks -bfwings, 2026-11-15): a raptor given a Pteranodon's arms (the row then reads Wings), what it says
	// about flying; its wings made bigger; then started from a Pteranodon, made bigger all over (Whole body).
	ClickScript = {
		{ TEXT("Raptor"), 0 },           // start from a raptor
		{ TEXT("Ptera"), 5 },            // its arms a Pteranodon's (0 start from; 1-4 head, neck, tail, legs)
		{ TEXT("@shot"), 0 },
		{ TEXT(" + "), 10 },             // the wings' Size (0 whole body; 1-9 head, neck, tail, legs)
		{ TEXT(" + "), 10 },
		{ TEXT(" + "), 10 },
		{ TEXT("@shot"), 0 },
		{ TEXT("Ptera"), 0 },            // start from a Pteranodon
		{ TEXT("@shot"), 0 },
		{ TEXT(" + "), 0 },              // whole body bigger
		{ TEXT(" + "), 0 },
		{ TEXT(" + "), 0 },
		{ TEXT(" + "), 0 },
		{ TEXT("@shot"), 0 },
		{ TEXT("@esc"), 0 },
	};
	ClickStep = 0;
	ClickWait = 0;
	ClickTarget.Reset();
}

void ABFSpliceStage::PressButton(const FString& Label)
{
	ClickScript = { { Label, 0 } };
	ClickStep = 0;
	ClickWait = 0;
	ClickTarget.Reset();
}

void ABFSpliceStage::TickClickTest()
{
	if (ClickStep >= ClickScript.Num() || IsBusy())
	{
		return;
	}
	const FClick& C = ClickScript[ClickStep];
	if (C.Label == TEXT("@view"))
	{
		// The animal's side of the screen: right-drag turns the view, the wheel zooms, D turns it.
		const TSharedPtr<SWindow> Win = GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		const FVector2D Over = Win.IsValid() ? Win->GetPositionInScreen() + Win->GetSizeInScreen() * FVector2D(0.8f, 0.5f) : FVector2D::ZeroVector;
		FSlateApplication& A = FSlateApplication::Get();
		const TSet<FKey> Right = { EKeys::RightMouseButton };
		switch (ClickWait++)
		{
		case 0:
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] view before: yaw %.1f pitch %.1f zoom %.2f"), Yaw, Pitch, Zoom);
			SendMouse(Over, EKeys::RightMouseButton, true);
			break;
		case 1: case 2: case 3: case 4: case 5: case 6:
			A.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, Over + FVector2D(ClickWait * 20.f, 0.f), Over + FVector2D((ClickWait - 1) * 20.f, 0.f), Right, EKeys::Invalid, 0.f, A.GetModifierKeys()));
			break;
		case 7:
			SendMouse(Over, EKeys::RightMouseButton, false);
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] view after right-drag: yaw %.1f pitch %.1f"), Yaw, Pitch);
			A.ProcessMouseWheelOrGestureEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, Over, Over, TSet<FKey>(), EKeys::Invalid, 1.f, A.GetModifierKeys()), nullptr);
			break;
		case 10:
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] view after wheel: zoom %.2f"), Zoom);
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::D, A.GetModifierKeys(), 0, false, 0, 0));
			break;
		case 30:
			A.ProcessKeyUpEvent(FKeyEvent(EKeys::D, A.GetModifierKeys(), 0, false, 0, 0));
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] view after D key: yaw %.1f"), Yaw);
			ClickWait = 0;
			++ClickStep;
			break;
		default:
			break;
		}
		return;
	}
	if (C.Label == TEXT("@shot"))
	{
		if (ClickWait++ == 30)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AutoTest"),
				FString::Printf(TEXT("clicks_panel_%02d.png"), ClickStep))), true, false);
		}
		else if (ClickWait > 60)
		{
			ClickWait = 0;
			++ClickStep;
		}
		return;
	}
	if (C.Label == TEXT("@name"))
	{
		// A name typed into the name box as a player would: a click into it, the letters, Enter.
		FSlateApplication& A = FSlateApplication::Get();
		const FString Typed = TEXT("Tricerex");
		if (ClickWait == 0 && NameBox.IsValid())
		{
			if (Scroll.IsValid())
			{
				Scroll->ScrollDescendantIntoView(NameBox, false, EDescendantScrollDestination::IntoView);
			}
		}
		else if (ClickWait == 4 && NameBox.IsValid())
		{
			const FVector2D At = NameBox->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
			SendMouse(At, EKeys::LeftMouseButton, true);
			SendMouse(At, EKeys::LeftMouseButton, false);
		}
		else if (ClickWait == 6)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] name box has the keys: %s"), IsTyping() ? TEXT("yes") : TEXT("NO"));
			for (TCHAR Ch : Typed)
			{
				A.ProcessKeyCharEvent(FCharacterEvent(Ch, A.GetModifierKeys(), 0, false));
			}
		}
		else if (ClickWait == 8)
		{
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, A.GetModifierKeys(), 0, false, 0, 0));
			A.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, A.GetModifierKeys(), 0, false, 0, 0));
		}
		else if (ClickWait > 10)
		{
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] typed a name: \"%s\" (keys back with the view: %s)"), *GivenName, IsTyping() ? TEXT("NO") : TEXT("yes"));
			ClickWait = 0;
			++ClickStep;
			return;
		}
		++ClickWait;
		return;
	}
	if (C.Label == TEXT("@esc"))
	{
		FSlateApplication& A = FSlateApplication::Get();
		if (ClickWait++ == 0)
		{
			A.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, A.GetModifierKeys(), 0, false, 0, 0));
			return;
		}
		A.ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, A.GetModifierKeys(), 0, false, 0, 0));
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] click Esc sent"));
		ClickWait = 0;
		++ClickStep;
		return;
	}
	if (ClickWait == 0)
	{
		ClickTarget = FindButton(C.Label, C.Nth);
		if (!ClickTarget.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("[BoneForge] click '%s' #%d: no such button"), *C.Label, C.Nth);
			++ClickStep;
			return;
		}
		if (Scroll.IsValid())
		{
			Scroll->ScrollDescendantIntoView(ClickTarget, false, EDescendantScrollDestination::IntoView);
		}
		++ClickWait;
		return;
	}
	if (++ClickWait < 4)      // the scroll takes effect, the panel lays out
	{
		return;
	}
	const FVector2D At = ClickTarget->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
	const FString Before = RecipeJson();
	const bool bWasBody = bBodyShown;
	const bool bWasBuilding = bWantBuild;
	SendMouse(At, EKeys::LeftMouseButton, true);
	SendMouse(At, EKeys::LeftMouseButton, false);
	const bool bDid = RecipeJson() != Before || bWantBuild != bWasBuilding || bBodyShown != bWasBody;
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] click '%s' #%d at (%.0f, %.0f): %s -> %s"), *C.Label, C.Nth, At.X, At.Y, bDid ? TEXT("took") : TEXT("NOTHING HAPPENED"), *RecipeJson());
	ClickWait = 0;
	++ClickStep;
}

void ABFSpliceStage::SetBase(const FString& Species)
{
	if (Species != Base)
	{
		EditingKey.Reset();       // another animal: not a change to the creature that was opened
		EditingName.Reset();
	}
	Base = Species;
	Sections.Reset();
	ExtraPart.Reset();
	ExtraAt = 0.5f;
	bClub = false;
	WholeSize = 1.f;
	BoneEdits.Reset();
	RecipeChanged();
}

void ABFSpliceStage::SetDonor(const FString& Section, const FString& Species)
{
	FSection& S = Sections.FindOrAdd(Section);
	S.Donor = Species == Base ? FString() : Species;
	if (S.bFit)
	{
		S.Scale = 1.f;       // refitted to the new part
	}
	// The section's single bones were the old part's: they start again from the new one's.
	if (const TArray<FBoneRow>* Rows_ = BoneRows.Find(Section))
	{
		for (const FBoneRow& R : *Rows_)
		{
			BoneEdits.Remove(R.Bone);
		}
	}
	RecipeChanged();
}

FString ABFSpliceStage::SourceOf(const FString& Section) const
{
	const FSection* S = Sections.Find(Section);
	return S && !S->Donor.IsEmpty() ? S->Donor : Base;
}

void ABFSpliceStage::OpenBones(const FString& Section)
{
	OpenSection = OpenSection == Section ? FString() : Section;
	RebuildBones();
}

void ABFSpliceStage::StartAdvancedClickTest()
{
	// Only the open section has bone rows, and their buttons are their own: species as Rex, Tri...; "-" and "+";
	// "Own length". The legs' rows: thigh, then shin (its knee movable).
	ClickScript = {
		{ TEXT("Bones"), 2 },            // open the legs (Neck 0, Tail 1, Legs 2, Arms 3)
		{ TEXT("Tri"), 1 },              // the shins from a Triceratops
		{ TEXT("Own length"), 1 },       // at the T. rex's own shin length
		{ TEXT("+"), 1 },                // shins a step longer
		{ TEXT("-"), 2 },                // knees a step up the thigh
		{ TEXT("@shot"), 0 },            // the legs' bones on the panel
		{ TEXT("[Bones]"), 0 },          // close the legs
		{ TEXT("Bones"), 0 },            // open the neck
		{ TEXT("+"), 1 },                // neck bone 2 longer
		{ TEXT("@shot"), 0 },
		{ TEXT("Grow body"), 0 },
		{ TEXT("@shot"), 0 },
		{ TEXT("Start over"), 0 },
		{ TEXT("@esc"), 0 },
	};
	ClickStep = 0;
	ClickWait = 0;
	ClickTarget.Reset();
}

void ABFSpliceStage::SetBoneDonor(const FString& Bone, const FString& Species)
{
	FString Section;
	for (const TPair<FString, TArray<FBoneRow>>& It : BoneRows)
	{
		if (It.Value.ContainsByPredicate([&](const FBoneRow& R) { return R.Bone == Bone; }))
		{
			Section = It.Key;
		}
	}
	FBoneEdit& E = BoneEdits.FindOrAdd(Bone);
	E.Donor = Species == SourceOf(Section) ? FString() : Species;
	RecipeChanged();
}

void ABFSpliceStage::ChangeBoneScale(const FString& Bone, float By)
{
	FBoneEdit& E = BoneEdits.FindOrAdd(Bone);
	const float Step = FMath::Max(FMath::Abs(By) * E.Scale, 0.01f);     // in proportion, as a section's size
	E.Scale = FMath::Clamp(FMath::RoundToFloat((E.Scale + FMath::Sign(By) * Step) * 100.f) / 100.f, 0.25f, 3.f);
	RecipeChanged();
}

void ABFSpliceStage::FitBone(const FString& Bone)
{
	FBoneEdit& E = BoneEdits.FindOrAdd(Bone);
	E.bFit = !E.bFit;
	RecipeChanged();
}

void ABFSpliceStage::ChangeBoneMove(const FString& Bone, float By)
{
	FBoneEdit& E = BoneEdits.FindOrAdd(Bone);
	E.Move = FMath::Clamp(FMath::RoundToFloat((E.Move + By) * 100.f) / 100.f, -0.4f, 0.4f);
	RecipeChanged();
}

void ABFSpliceStage::ChangeScale(const FString& Section, float By)
{
	FSection& S = Sections.FindOrAdd(Section);
	// In proportion to its size, so a part at 10% can be fine-tuned as well as one at 200%.
	const float Step = FMath::Max(FMath::Abs(By) * S.Scale, 0.01f);
	S.Scale = FMath::Clamp(FMath::RoundToFloat((S.Scale + FMath::Sign(By) * Step) * 100.f) / 100.f, 0.03f, 4.f);
	S.bFit = false;
	RecipeChanged();
}

void ABFSpliceStage::ChangeThickness(const FString& Section, float By)
{
	FSection& S = Sections.FindOrAdd(Section);
	S.Thick = FMath::Clamp(FMath::RoundToFloat((S.Thick + By) * 100.f) / 100.f, 0.5f, 3.f);
	RecipeChanged();
}

void ABFSpliceStage::ChangeMove(const FString& Section, float By)
{
	FSection& S = Sections.FindOrAdd(Section);
	S.Move = FMath::Clamp(FMath::RoundToFloat((S.Move + By) * 100.f) / 100.f, -1.f, 1.f);
	RecipeChanged();
}

void ABFSpliceStage::FitSize(const FString& Section)
{
	Sections.FindOrAdd(Section).bFit = true;
	RecipeChanged();
}

void ABFSpliceStage::Grow(bool bThenWalk, bool bThenTest)
{
	bWalkAfter = bThenWalk;
	bTestAfter = bThenTest;
	if (BuiltRecipe == RecipeJson() && bBodyShown)
	{
		if (bWalkAfter)
		{
			LaunchWalkIt();
		}
		return;
	}
	bWantBuild = true;
	Status = TEXT("Growing the body (about a minute)...");
}

void ABFSpliceStage::Orbit(float YawDegrees)
{
	Yaw += YawDegrees;
}

bool ABFSpliceStage::IsBusy() const
{
	return Proc.IsValid() || bDirty || bWantBuild || GlbProc.IsValid() || bImportQueued || WalkImport.IsValid();
}

FString ABFSpliceStage::Summary() const
{
	return Error.IsEmpty()
		? FString::Printf(TEXT("%s (%s): %.0f kg | %s | %s | %s | %s %s"), *Name, *Key, MassKg, *Legs, *Balance, *Flight, *Flags, *Notes)
		: Error;
}

FString ABFSpliceStage::FlightHelp() const
{
	// What to change for it to fly (Raven, 2026-11-15: "I don't know how to make the wings larger so it can fly").
	// Two things decide it (bone_forge/splice/splice.py flight_check): wing loading (its weight over its wings' area, at
	// most 245 N/m2) and flight muscle (at least 15% of its weight; the muscle is the body's, the animal started from).
	if (FlightVerdict.IsEmpty() || FlightVerdict == TEXT("flies") || MassKg <= 0.0)
	{
		return FString();
	}
	const FSection* Arms = Sections.Find(TEXT("Shoulder"));
	const bool bWings = (Arms && !Arms->Donor.IsEmpty() ? Arms->Donor : Base) == TEXT("ptera") || (ExtraPart == TEXT("Shoulder") && Sections.Contains(TEXT("Pair")) && Sections[TEXT("Pair")].Donor == TEXT("ptera"));
	if (Base != TEXT("ptera"))
	{
		return TEXT("To fly: flight muscle is in the body, and only a Pteranodon's body has enough (a fifth of its weight). Start from Pte, then build onto it.")
			+ FString(bWings ? TEXT("") : TEXT(" It needs wings too: Pte in the Arms row."));
	}
	if (!bWings)
	{
		return TEXT("To fly: it needs wings. Pick Pte in the Arms row (they become Wings), then Size + to make them bigger.");
	}
	if (FlightVerdict == TEXT("glides"))
	{
		return TEXT("To fly: it's too heavy for its flight muscle. Make the rest lighter: a smaller head, tail or legs, less Thick, or Whole body Size - (smaller animals fly more easily).");
	}
	return TEXT("To fly: bigger wings. In the Wings row press Size + (or Bones, and lengthen the finger bones), or make the rest lighter.");
}

FString ABFSpliceStage::RecipeJson() const
{
	FString Out = FString::Printf(TEXT("{\"base\":\"%s\",%s\"sections\":{"), *Base,
		FMath::IsNearlyEqual(WholeSize, 1.f) ? TEXT("") : *FString::Printf(TEXT("\"size\":%.2f,"), WholeSize));
	bool bFirst = true;
	for (const FRow& R : Rows)
	{
		if (const FSection* S = Sections.Find(R.Section))
		{
			Out += FString::Printf(TEXT("%s\"%s\":{\"donor\":\"%s\",\"scale\":%s,\"thick\":%.2f,\"move\":%.2f}"), bFirst ? TEXT("") : TEXT(","), R.Section,
				S->Donor.IsEmpty() ? *Base : *S->Donor, S->bFit ? TEXT("\"fit\"") : *FString::Printf(TEXT("%.2f"), S->Scale), S->Thick, S->Move);
			bFirst = false;
		}
	}
	Out += TEXT("}");
	FString Bones;
	TArray<FString> Names;
	BoneEdits.GetKeys(Names);
	Names.Sort();
	for (const FString& N : Names)
	{
		const FBoneEdit& E = BoneEdits[N];
		if (E.Donor.IsEmpty() && FMath::IsNearlyEqual(E.Scale, 1.f) && FMath::IsNearlyZero(E.Move))
		{
			continue;
		}
		Bones += FString::Printf(TEXT("%s\"%s\":{\"donor\":\"%s\",\"scale\":%.2f,\"fit\":%s,\"move\":%.2f}"), Bones.IsEmpty() ? TEXT("") : TEXT(","),
			*N, *E.Donor, E.Scale, E.bFit ? TEXT("true") : TEXT("false"), E.Move);
	}
	if (!Bones.IsEmpty())
	{
		Out += TEXT(",\"bones\":{") + Bones + TEXT("}");
	}
	if (!ExtraPart.IsEmpty() || bClub)
	{
		const auto SizeOf = [](const FSection& S) { return S.bFit ? FString(TEXT("\"fit\"")) : FString::Printf(TEXT("%.2f"), S.Scale); };
		Out += TEXT(",\"add\":{");
		if (!ExtraPart.IsEmpty())
		{
			const FSection& S = Sections[TEXT("Pair")];
			Out += FString::Printf(TEXT("\"pair\":{\"part\":\"%s\",\"donor\":\"%s\",\"scale\":%s,\"thick\":%.2f,\"at\":%.2f}%s"),
				*ExtraPart, *S.Donor, *SizeOf(S), S.Thick, ExtraAt, bClub ? TEXT(",") : TEXT(""));
		}
		if (bClub)
		{
			Out += FString::Printf(TEXT("\"club\":{\"scale\":%s}"), *SizeOf(Sections[TEXT("Club")]));
		}
		Out += TEXT("}");
	}
	return Out + TEXT("}");
}

void ABFSpliceStage::RecipeChanged()
{
	// Undo: the state before this change goes on the stack (a click that changed nothing adds nothing).
	const FString Now = RecipeJson();
	if (!bRestoring && Now != CommittedRecipe)
	{
		if (!CommittedRecipe.IsEmpty())
		{
			UndoStack.Add(Committed);
			if (UndoStack.Num() > 50)
			{
				UndoStack.RemoveAt(0);
			}
			RedoStack.Reset();
		}
	}
	Committed = Snapshot();
	CommittedRecipe = Now;
	bDirty = true;
	if (bBodyShown)
	{
		HideBody();
	}
}

ABFSpliceStage::FEditState ABFSpliceStage::Snapshot() const
{
	FEditState S;
	S.Base = Base;
	S.Sections = Sections;
	S.BoneEdits = BoneEdits;
	S.ExtraPart = ExtraPart;
	S.ExtraAt = ExtraAt;
	S.bClub = bClub;
	S.WholeSize = WholeSize;
	return S;
}

void ABFSpliceStage::Restore(const FEditState& S)
{
	Base = S.Base;
	Sections = S.Sections;
	BoneEdits = S.BoneEdits;
	ExtraPart = S.ExtraPart;
	ExtraAt = S.ExtraAt;
	bClub = S.bClub;
	WholeSize = S.WholeSize;
	bRestoring = true;
	RecipeChanged();
	bRestoring = false;
}

void ABFSpliceStage::Undo()
{
	if (UndoStack.Num() == 0)
	{
		return;
	}
	RedoStack.Add(Committed);
	Restore(UndoStack.Pop());
}

void ABFSpliceStage::Redo()
{
	if (RedoStack.Num() == 0)
	{
		return;
	}
	UndoStack.Add(Committed);
	Restore(RedoStack.Pop());
}

void ABFSpliceStage::ApplyRecipe(const TSharedPtr<FJsonObject>& R)
{
	// The recipe as RecipeJson() writes it (and compose.py saves it in the creature's rig): base; sections with donor,
	// scale (a number, or "fit"), thick, move; single bones; added parts. Missing fields are the defaults.
	auto ReadSize = [](const TSharedPtr<FJsonObject>& O, FSection& S)
	{
		FString Word;
		double V = 1.0;
		if (O->TryGetStringField(TEXT("scale"), Word) && Word == TEXT("fit"))
		{
			S.bFit = true;
			S.Scale = 1.f;
		}
		else if (O->TryGetNumberField(TEXT("scale"), V))
		{
			S.Scale = (float)V;
		}
	};
	auto Num = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Default)
	{
		double V = Default;
		return O->TryGetNumberField(Field, V) ? (float)V : Default;
	};
	Base = R->GetStringField(TEXT("base"));
	WholeSize = Num(R, TEXT("size"), 1.f);
	Sections.Reset();
	BoneEdits.Reset();
	ExtraPart.Reset();
	ExtraAt = 0.5f;
	bClub = false;
	const TSharedPtr<FJsonObject>* Secs = nullptr;
	if (R->TryGetObjectField(TEXT("sections"), Secs))
	{
		for (const auto& It : (*Secs)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FSection S;
			FString Donor;
			O->TryGetStringField(TEXT("donor"), Donor);
			S.Donor = Donor == Base ? FString() : Donor;
			ReadSize(O, S);
			S.Thick = Num(O, TEXT("thick"), 1.f);
			S.Move = Num(O, TEXT("move"), 0.f);
			Sections.Add(FString(It.Key), S);
		}
	}
	const TSharedPtr<FJsonObject>* Bones = nullptr;
	if (R->TryGetObjectField(TEXT("bones"), Bones))
	{
		for (const auto& It : (*Bones)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FBoneEdit E;
			O->TryGetStringField(TEXT("donor"), E.Donor);
			E.Scale = Num(O, TEXT("scale"), 1.f);
			O->TryGetBoolField(TEXT("fit"), E.bFit);
			E.Move = Num(O, TEXT("move"), 0.f);
			BoneEdits.Add(FString(It.Key), E);
		}
	}
	const TSharedPtr<FJsonObject>* Add = nullptr;
	if (R->TryGetObjectField(TEXT("add"), Add))
	{
		const TSharedPtr<FJsonObject>* Pair = nullptr;
		if ((*Add)->TryGetObjectField(TEXT("pair"), Pair))
		{
			FSection& S = Sections.FindOrAdd(TEXT("Pair"));
			ExtraPart = (*Pair)->GetStringField(TEXT("part"));
			(*Pair)->TryGetStringField(TEXT("donor"), S.Donor);
			ReadSize(*Pair, S);
			S.Thick = Num(*Pair, TEXT("thick"), 1.f);
			ExtraAt = Num(*Pair, TEXT("at"), 0.5f);
		}
		const TSharedPtr<FJsonObject>* Club = nullptr;
		if ((*Add)->TryGetObjectField(TEXT("club"), Club))
		{
			bClub = true;
			ReadSize(*Club, Sections.FindOrAdd(TEXT("Club")));
		}
	}
}

bool ABFSpliceStage::LoadCreature(const FString& CreatureKey, const FString& ShownName)
{
	FString Text;
	TSharedPtr<FJsonObject> Rig;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Rigs, CreatureKey + TEXT("_rig.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rig) || !Rig.IsValid())
	{
		Error = FString::Printf(TEXT("Couldn't read %s's rig to open it."), *ShownName);
		return false;
	}
	TSharedPtr<FJsonObject> Recipe;
	const TSharedPtr<FJsonObject>* Splice = nullptr;
	if (Rig->TryGetObjectField(TEXT("splice"), Splice))
	{
		const TSharedPtr<FJsonObject>* Own = nullptr;
		if ((*Splice)->TryGetObjectField(TEXT("recipe"), Own))
		{
			Recipe = *Own;                    // made in this editor
		}
		else
		{
			// A catalog splice (splice/splice.py): one section from a donor at the donor's own size.
			Recipe = MakeShared<FJsonObject>();
			Recipe->SetStringField(TEXT("base"), (*Splice)->GetStringField(TEXT("base")));
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetStringField(TEXT("donor"), (*Splice)->GetStringField(TEXT("donor")));
			TSharedPtr<FJsonObject> Secs = MakeShared<FJsonObject>();
			Secs->SetObjectField((*Splice)->GetStringField(TEXT("section")), One);
			Recipe->SetObjectField(TEXT("sections"), Secs);
		}
	}
	else
	{
		Recipe = MakeShared<FJsonObject>();   // one of the six: start from it
		Recipe->SetStringField(TEXT("base"), CreatureKey);
	}
	ApplyRecipe(Recipe);
	const bool bYours = ABFGameMode::IsYours(CreatureKey);
	EditingKey = Rig->HasField(TEXT("splice")) ? CreatureKey : FString();
	EditingName = EditingKey.IsEmpty() ? FString() : ShownName;
	SetGivenName(bYours ? ShownName : FString());
	OpenSection.Reset();
	RecipeChanged();          // (undoable: Undo goes back to what the editor had before)
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] opened %s (%s) in the editor: %s"), *CreatureKey, *ShownName, *RecipeJson());
	return true;
}

void ABFSpliceStage::SetGivenName(const FString& InName)
{
	GivenName = InName;
	if (NameBox.IsValid() && NameBox->GetText().ToString() != InName)
	{
		NameBox->SetText(FText::FromString(InName));
	}
}

void ABFSpliceStage::NameBuilt()
{
	ABFGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABFGameMode>() : nullptr;
	const FString Wanted = GivenName.TrimStartAndEnd();
	const FString Has = GM ? GM->GivenNameOf(Key) : FString();
	// Already called that, or that with a number after it (another creature had the name: RenameCreature added it).
	if (!GM || Key.IsEmpty() || Wanted.IsEmpty() || Has == Wanted || (Has.StartsWith(Wanted + TEXT(" ")) && Has.Mid(Wanted.Len() + 1).IsNumeric()))
	{
		return;
	}
	const FString Got = GM->RenameCreature(Key, Wanted);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] %s named \"%s\""), *Key, *Got);
}

bool ABFSpliceStage::IsTyping()
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
	return Focus.IsValid() && Focus->GetType().ToString().Contains(TEXT("EditableText"));
}

void ABFSpliceStage::BringBack(APlayerController* Controller, const FString& RigFolder, const FString& CreatureKey)
{
	if (GlbProc.IsValid() || WalkImport.IsValid() || bImportQueued)
	{
		return;
	}
	PC = Controller;              // (the editor may never have been opened)
	if (Rigs.IsEmpty())
	{
		Rigs = RigFolder;
	}
	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(Rigs, TEXT(".."), TEXT("unreal"), TEXT("build_glb.py")));
	GlbProc = FPlatformProcess::CreateProc(TEXT("python"), *FString::Printf(TEXT("\"%s\" %s"), *Script, *CreatureKey), true, true, true, nullptr, 0, nullptr, nullptr);
	if (GlbProc.IsValid())
	{
		BackKey = CreatureKey;
	}
}

void ABFSpliceStage::StartUndoClickTest()
{
	// On the T. rex with a Triceratops head (opened with Edit a copy): an Ankylosaurus tail, undone twice (the tail, then
	// the opening itself), redone twice, a name typed, and Walk it.
	ClickScript = {
		{ TEXT("Ankylo"), 3 },           // its tail from an Ankylosaurus (the base row's is 0, then head, neck, tail)
		{ TEXT("Undo"), 0 },             // the tail back
		{ TEXT("Undo"), 0 },             // what the editor had before the creature was opened
		{ TEXT("Redo"), 0 },             // the Triceratops head again
		{ TEXT("Redo"), 0 },             // and the tail
		{ TEXT("@shot"), 0 },            // the T. rex tail it had before, as pale bones
		{ TEXT("@name"), 0 },            // a name typed in the box
		{ TEXT("Walk it"), 0 },
	};
	ClickStep = 0;
	ClickWait = 0;
	ClickTarget.Reset();
}

void ABFSpliceStage::StartJob(bool bBuild)
{
	bJobBuild = bBuild;
	bDirty = false;
	bWantBuild = false;
	JobRecipe = RecipeJson();
	const FString In = FPaths::Combine(WorkDir, TEXT("recipe.json"));
	const FString Out = FPaths::Combine(WorkDir, TEXT("result.json"));
	FFileHelper::SaveStringToFile(JobRecipe, *In, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	IFileManager::Get().Delete(*Out);
	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(Rigs, TEXT(".."), TEXT("splice"), TEXT("compose.py")));
	const FString Args = FString::Printf(TEXT("\"%s\" %s \"%s\" \"%s\""), *Script, bBuild ? TEXT("build") : TEXT("preview"), *In, *Out);
	Proc = FPlatformProcess::CreateProc(TEXT("python"), *Args, true, true, true, nullptr, 0, nullptr, nullptr);
	if (!Proc.IsValid())
	{
		Error = TEXT("Couldn't start python, which the editor's rules run in.");
		Status.Reset();
	}
}

void ABFSpliceStage::FinishJob()
{
	FPlatformProcess::CloseProc(Proc);
	Proc.Reset();
	ReadResult();
	const bool bStillCurrent = JobRecipe == RecipeJson();
	if (bJobBuild)
	{
		Status.Reset();
		if (bStillCurrent && Error.IsEmpty())
		{
			if (BodyPath.IsEmpty())
			{
				Status = TEXT("Nothing changed yet: swap or resize something first.");
				bWalkAfter = false;
			}
			else if (ShowBody(BodyPath))
			{
				BuiltRecipe = JobRecipe;
				NameBuilt();
				if (bWalkAfter)
				{
					LaunchWalkIt();
				}
			}
		}
		bWalkAfter = false;
	}
	if (bStillCurrent && !bBodyShown)
	{
		ShowMannequin();
	}
}

void ABFSpliceStage::ReadResult()
{
	FString Text;
	Error.Reset();
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(WorkDir, TEXT("result.json"))))
	{
		Error = TEXT("The rules script gave no result (is python on the PATH?).");
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Error = TEXT("Couldn't read the rules script's result.");
		return;
	}
	if (Root->TryGetStringField(TEXT("error"), Error))
	{
		return;
	}
	// Before and after: a different creature from the one shown, so what was shown becomes the "before".
	if (const FString NewKey = Root->GetStringField(TEXT("key")); NewKey != Key)
	{
		if (!Key.IsEmpty())
		{
			PrevCapsules = Capsules;
			PrevMassKg = MassKg;
			PrevLegsRatio = LegsRatio;
			PrevClimbDeg = ClimbDeg;
			PrevKey = Key;
			PrevBase = ResultBase;
		}
		TSharedPtr<FJsonObject> Asked;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JobRecipe), Asked) && Asked.IsValid())
		{
			ResultBase = Asked->GetStringField(TEXT("base"));
		}
	}
	{
		double V = 0.0;
		LegsRatio = Root->TryGetNumberField(TEXT("legs_ratio"), V) ? (float)V : 0.f;
	}
	Name = Root->GetStringField(TEXT("name"));
	const TSharedPtr<FJsonObject>* Sizes = nullptr;
	if (Root->TryGetObjectField(TEXT("scales"), Sizes))
	{
		for (TPair<FString, FSection>& S : Sections)
		{
			double Size = 1.0;
			if (S.Value.bFit && (*Sizes)->TryGetNumberField(S.Key, Size))
			{
				S.Value.Scale = (float)Size;
			}
		}
	}
	const TSharedPtr<FJsonObject>* BonesObj = nullptr;
	if (Root->TryGetObjectField(TEXT("bones"), BonesObj))
	{
		BoneRows.Reset();
		for (const auto& It : (*BonesObj)->Values)
		{
			const FString SecKey(It.Key);         // (the JSON object's own key type)
			TArray<FBoneRow>& List = BoneRows.FindOrAdd(SecKey);
			for (const TSharedPtr<FJsonValue>& V : It.Value->AsArray())
			{
				const TSharedPtr<FJsonObject>& B = V->AsObject();
				FBoneRow R;
				R.Bone = B->GetStringField(TEXT("bone"));
				R.Label = B->GetStringField(TEXT("label"));
				R.Role = B->GetStringField(TEXT("role"));
				R.From = B->GetStringField(TEXT("from"));
				R.bPaired = B->GetBoolField(TEXT("paired"));
				R.bMovable = B->GetBoolField(TEXT("movable"));
				R.LenCm = (float)B->GetNumberField(TEXT("len_cm"));
				for (const TSharedPtr<FJsonValue>& D : B->GetArrayField(TEXT("donors")))
				{
					R.Donors.Add(D->AsString());
				}
				List.Add(R);
			}
		}
		RebuildBones();
	}
	Key = Root->GetStringField(TEXT("key"));
	MassKg = Root->GetNumberField(TEXT("mass_kg"));
	Legs = Root->GetStringField(TEXT("legs"));
	LegsVerdict = Root->GetStringField(TEXT("legs_verdict"));
	Balance = Root->GetStringField(TEXT("balance"));
	bBalanceOk = Root->GetBoolField(TEXT("balance_ok"));
	Flight = Root->GetStringField(TEXT("flight"));
	FlightVerdict = Root->GetStringField(TEXT("flight_verdict"));
	Root->TryGetStringField(TEXT("swim"), Swim);
	Root->TryGetStringField(TEXT("swim_verdict"), SwimVerdict);
	Root->TryGetStringField(TEXT("climb"), Climb);
	{
		double V = 0.0;
		ClimbDeg = Root->TryGetNumberField(TEXT("climb_deg"), V) ? V : 0.f;
	}
	BodyPath = Root->GetStringField(TEXT("body"));
	auto Join = [&](const TCHAR* Field)
	{
		FString Out;
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(Field))
		{
			FString Line = V->AsString();
			Line[0] = FChar::ToUpper(Line[0]);
			Out += (Out.IsEmpty() ? TEXT("") : TEXT("\n")) + Line;
		}
		return Out;
	};
	Notes = Join(TEXT("notes"));
	Flags = Join(TEXT("flags"));
	Capsules.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("capsules")))
	{
		const TSharedPtr<FJsonObject>& C = V->AsObject();
		Capsules.Add({ ReadVec(C, TEXT("a")), ReadVec(C, TEXT("b")), (float)C->GetNumberField(TEXT("r")), C->GetStringField(TEXT("from")) });
	}
	if (LineageBox.IsValid())
	{
		LineageBox->ClearChildren();
		TArray<TPair<FString, double>> Share;
		for (const auto& It : Root->GetObjectField(TEXT("lineage"))->Values)
		{
			Share.Add(TPair<FString, double>(It.Key, It.Value->AsNumber()));
		}
		Share.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value > B.Value; });
		for (const TPair<FString, double>& S : Share)
		{
			LineageBox->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 2.f, 8.f, 2.f)
				[
					SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
					[
						SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(ColorOf(S.Key))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
					.Text(FText::FromString(S.Value < 0.01 ? FString::Printf(TEXT("%s  %.1f%%"), *S.Key, 100.0 * S.Value) : FString::Printf(TEXT("%s  %.0f%%"), *S.Key, 100.0 * S.Value)))
				]
			];
		}
	}
	// Frame the animal: to the right of the panel, whatever its size.
	FBox Box(ForceInit);
	for (const FCapsule& C : Capsules)
	{
		Box += C.A;
		Box += C.B;
	}
	if (Box.IsValid)
	{
		Focus = Box.GetCenter();
		Reach = (float)FMath::Max3(Box.GetSize().X, Box.GetSize().Z, 120.0);
	}
}

UMaterialInterface* ABFSpliceStage::Tint(const FLinearColor& Color)
{
	static UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	return M;
}

void ABFSpliceStage::ClearMannequin()
{
	for (UStaticMeshComponent* P : Pieces)
	{
		if (P)
		{
			P->DestroyComponent();
		}
	}
	Pieces.Reset();
}

void ABFSpliceStage::ShowMannequin()
{
	ClearMannequin();
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	TMap<FString, UMaterialInterface*> Materials;
	auto Add = [&](UStaticMesh* Mesh, const FVector& At, const FRotator& Turn, const FVector& Size, UMaterialInterface* Mat)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetRelativeLocationAndRotation(At, Turn);
		C->SetRelativeScale3D(Size);
		C->SetMaterial(0, Mat);
		Pieces.Add(C);
	};
	for (const FCapsule& C : Capsules)
	{
		UMaterialInterface*& Mat = Materials.FindOrAdd(C.From);
		if (!Mat)
		{
			Mat = Tint(ColorOf(C.From));
		}
		const FVector Along = C.B - C.A;
		const float Len = Along.Size();
		const FVector Ball(C.R / 50.f);
		Add(Sphere, C.A, FRotator::ZeroRotator, Ball, Mat);
		Add(Sphere, C.B, FRotator::ZeroRotator, Ball, Mat);
		if (Len > 0.1f)
		{
			Add(Cylinder, (C.A + C.B) * 0.5f, FRotationMatrix::MakeFromZ(Along / Len).Rotator(), FVector(C.R / 50.f, C.R / 50.f, Len / 100.f), Mat);
		}
	}
	// The creature before the last change, as thin pale bones: where nothing changed they sit inside the mannequin, and
	// what grew, shrank or moved shows where they poke out of it (or the mannequin out past them).
	if (bShowBefore && HasBefore())
	{
		UMaterialInterface* Ghost = Tint(FLinearColor(0.85f, 0.85f, 0.8f));
		const float R = FMath::Max(Reach * 0.004f, 1.2f);
		for (const FCapsule& C : PrevCapsules)
		{
			const FVector Along = C.B - C.A;
			const float Len = Along.Size();
			Add(Sphere, C.A, FRotator::ZeroRotator, FVector(2.f * R / 50.f), Ghost);
			Add(Sphere, C.B, FRotator::ZeroRotator, FVector(2.f * R / 50.f), Ghost);
			if (Len > 0.1f)
			{
				Add(Cylinder, (C.A + C.B) * 0.5f, FRotationMatrix::MakeFromZ(Along / Len).Rotator(), FVector(R / 50.f, R / 50.f, Len / 100.f), Ghost);
			}
		}
	}
}

bool ABFSpliceStage::ShowBody(const FString& ObjPath)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ObjPath))
	{
		Error = FString::Printf(TEXT("Couldn't read %s"), *ObjPath);
		return false;
	}
	TArray<FVector> Verts, Normals;
	TArray<int32> Tris;
	for (const FString& L : Lines)
	{
		const TCHAR* S = *L;
		if (S[0] == 'v' && S[1] == ' ')
		{
			TCHAR* End = nullptr;
			const double X = wcstod(S + 2, &End);
			const double Y = wcstod(End, &End);
			const double Z = wcstod(End, &End);
			Verts.Add(FVector(X, Z, Y));       // OBJ: x forward, y up, z right
		}
		else if (S[0] == 'f' && S[1] == ' ')
		{
			TCHAR* End = const_cast<TCHAR*>(S + 2);
			int32 Face[8], N = 0;
			while (N < 8)
			{
				TCHAR* Before = End;
				const int32 I = FCString::Strtoi(End, &End, 10);
				if (End == Before)
				{
					break;
				}
				Face[N++] = I - 1;
				while (*End && *End != ' ')
				{
					++End;      // past any /texture/normal
				}
			}
			for (int32 k = 1; k + 1 < N; ++k)
			{
				Tris.Append({ Face[0], Face[k], Face[k + 1] });
			}
		}
	}
	Normals.Init(FVector::ZeroVector, Verts.Num());
	for (int32 t = 0; t + 2 < Tris.Num(); t += 3)
	{
		const FVector N = FVector::CrossProduct(Verts[Tris[t + 2]] - Verts[Tris[t]], Verts[Tris[t + 1]] - Verts[Tris[t]]);
		Normals[Tris[t]] += N;
		Normals[Tris[t + 1]] += N;
		Normals[Tris[t + 2]] += N;
	}
	for (FVector& N : Normals)
	{
		N = N.GetSafeNormal();
	}
	Body->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	Body->SetMaterial(0, Tint(FLinearColor(0.52f, 0.45f, 0.37f)));
	Body->SetVisibility(true);
	ClearMannequin();
	bBodyShown = true;
	return true;
}

void ABFSpliceStage::HideBody()
{
	Body->SetVisibility(false);
	bBodyShown = false;
	BuiltRecipe.Reset();
	ShowMannequin();
}

void ABFSpliceStage::LaunchWalkIt()
{
	// Into the game as it runs, nothing imported in the editor and no restart: its skinned .glb is built in the background
	// (unreal/build_glb.py), then the controller imports it (BFRuntimeImport), adds it and spawns it (WalkIt, in Tick).
	NameBuilt();              // (a name typed after its body was grown)
	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(Rigs, TEXT(".."), TEXT("unreal"), TEXT("build_glb.py")));
	GlbProc = FPlatformProcess::CreateProc(TEXT("python"), *FString::Printf(TEXT("\"%s\" %s"), *Script, *Key), true, true, true, nullptr, 0, nullptr, nullptr);
	if (GlbProc.IsValid())
	{
		Status = TEXT("Making it ready to walk (a few seconds)...");
	}
	else
	{
		Error = TEXT("Couldn't start python to make it ready to walk.");
	}
}

void ABFSpliceStage::UpdateCamera()
{
	const FRotator Turn(Pitch, Yaw, 0.f);
	const float Dist = Reach * 1.5f * Zoom;
	// Look to the left of the animal so it stands clear of the panel on the left of the screen.
	const FVector Right = FRotationMatrix(Turn).GetUnitAxis(EAxis::Y);
	const FVector Target = Focus - Right * Dist * 0.4f;
	Camera->SetRelativeLocationAndRotation(Target - Turn.Vector() * Dist, Turn);
	Fill->SetAttenuationRadius(Dist * 4.f);
	Fill->SetIntensityUnits(ELightUnits::Candelas);
	Fill->SetIntensity(Dist * Dist * 0.0007f);
}

void ABFSpliceStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Proc.IsValid() && !FPlatformProcess::IsProcRunning(Proc))
	{
		FinishJob();
	}
	// Walk it: the .glb written, Unreal's import of it started and left to run, and once it's done the creature handed to
	// the game. Started from the engine's core ticker, not from here: Interchange waits on the game thread's tasks while
	// it starts, and an actor's tick is one of them (it asserted, synchronous or not).
	if (GlbProc.IsValid() && !FPlatformProcess::IsProcRunning(GlbProc))
	{
		int32 Code = -1;
		FPlatformProcess::GetProcReturnCode(GlbProc, &Code);
		FPlatformProcess::CloseProc(GlbProc);
		GlbProc.Reset();
		const FString JobKey = BackKey.IsEmpty() ? Key : BackKey;      // Walk it's creature, or one being put back
		if (Code == 0)
		{
			Status = BackKey.IsEmpty() ? TEXT("Bringing it into the game...") : TEXT("");
			bImportQueued = true;
			const FString Glb = FPaths::ConvertRelativePathToFull(FPaths::Combine(Rigs, TEXT(".."), TEXT("unreal"), TEXT("out"), JobKey + TEXT("_skeletal.glb")));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Glb, JobKey](float)
			{
				FString Why;
				bImportQueued = false;
				WalkImport = BFRuntimeImport::StartImport(JobKey, Glb, Why);
				if (!WalkImport.IsValid())
				{
					Status.Reset();
					Error = TEXT("Couldn't bring it into the game: ") + Why;
					if (ABFPlayerController* Player = Cast<ABFPlayerController>(PC); Player && !BackKey.IsEmpty())
					{
						Player->CreatureBack(BackKey, nullptr, Error);
					}
					BackKey.Reset();
				}
				return false;        // once
			}));
		}
		else
		{
			Status.Reset();
			Error = FString::Printf(TEXT("Couldn't make it ready to walk (build_glb.py exited %d)."), Code);
			if (ABFPlayerController* Player = Cast<ABFPlayerController>(PC); Player && !BackKey.IsEmpty())
			{
				Player->CreatureBack(BackKey, nullptr, Error);
			}
			BackKey.Reset();
		}
	}
	else if (WalkImport.IsValid())
	{
		USkeletalMesh* Built = nullptr;
		FString Why;
		if (BFRuntimeImport::Finished(WalkImport, Built, Why))
		{
			WalkImport.Reset();
			ABFPlayerController* Player = Cast<ABFPlayerController>(PC);
			if (!BackKey.IsEmpty())        // put back: into the species, and the menu shows it again
			{
				const FString Back = BackKey;
				BackKey.Reset();
				if (Player)
				{
					Player->CreatureBack(Back, Built, Why);
				}
				return;
			}
			if (Built && Player && Player->WalkIt(Key, Built, bTestAfter, Why))
			{
				return;              // the editor is closed and the creature walks
			}
			Status.Reset();
			Error = TEXT("Couldn't bring it into the game: ") + Why;
		}
	}
	if (!Proc.IsValid())
	{
		if (bWantBuild)
		{
			StartJob(true);
		}
		else if (bDirty)
		{
			StartJob(false);
		}
	}
	if (!bOpen || !PC)
	{
		return;
	}
	TickClickTest();
	if (PC->GetViewTarget() != this)      // the pawn's going resets it
	{
		PC->SetViewTarget(this);
	}
	if (IsTyping())                       // the name box has the keys
	{
		UpdateCamera();
		return;
	}
	if (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl))
	{
		const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
		if (PC->WasInputKeyJustPressed(EKeys::Z))
		{
			bShift ? Redo() : Undo();
		}
		if (PC->WasInputKeyJustPressed(EKeys::Y))
		{
			Redo();
		}
		UpdateCamera();
		return;                           // (Ctrl+S isn't zoom)
	}
	if (PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float DX = 0.f, DY = 0.f;
		PC->GetInputMouseDelta(DX, DY);
		Yaw += DX * 2.5f;
		Pitch = FMath::Clamp(Pitch + DY * 2.5f, -70.f, 10.f);
	}
	Yaw += ((PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f)) * 70.f * DeltaSeconds;
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) || PC->IsInputKeyDown(EKeys::W))
	{
		Zoom = FMath::Max(Zoom * (PC->IsInputKeyDown(EKeys::W) ? 1.f - DeltaSeconds : 0.92f), 0.3f);
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) || PC->IsInputKeyDown(EKeys::S))
	{
		Zoom = FMath::Min(Zoom * (PC->IsInputKeyDown(EKeys::S) ? 1.f + DeltaSeconds : 1.08f), 3.f);
	}
	UpdateCamera();
}

TSharedRef<SWidget> ABFSpliceStage::MakeButton(TFunction<FString()> Label, TFunction<void()> Click, const FSlateFontInfo& Font,
	TFunction<bool()> Enabled) const
{
	return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(4.f, 1.f))
		.IsEnabled_Lambda([Enabled]() { return !Enabled || Enabled(); })
		.OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); })
		[
			SNew(STextBlock).Font(Font).Text_Lambda([Label]() { return FText::FromString(Label()); })
		];
}

void ABFSpliceStage::RebuildBones()
{
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 12);
	for (TPair<FString, TSharedPtr<SVerticalBox>>& It : BoneBoxes)
	{
		const FString Sec = It.Key;
		const TSharedPtr<SVerticalBox> Box = It.Value;
		if (!Box.IsValid())
		{
			continue;
		}
		Box->ClearChildren();
		if (Sec != OpenSection)
		{
			continue;            // only the open section has rows (a closed one's buttons would still be in the tree)
		}
		Box->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 3.f)
		[
			SNew(STextBlock).Font(Small).WrapTextAt(660.f).ColorAndOpacity(FLinearColor(0.75f, 0.85f, 1.f))
			.Text(FText::FromString(FString::Printf(TEXT("The %s bone by bone: each from any species that has one, how long it is, and where its joint sits along the bone above (left and right change together)."),
				SectionWord(Sec))))
		];
		const TArray<FBoneRow>* List = BoneRows.Find(Sec);
		if (!List || List->Num() == 0)
		{
			Box->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("None of its bones change on their own.")))];
			continue;
		}
		for (const FBoneRow& R : *List)
		{
			const FString Bone = R.Bone, From = R.From;
			const bool bLimb = Sec == TEXT("Hip") || Sec == TEXT("Shoulder");
			TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150.f)
				[
					SNew(STextBlock).Font(Bold).Text(FText::FromString(FString::Printf(TEXT("%s%s  %.0f cm"), *R.Label, R.bPaired ? TEXT("s") : TEXT(""), R.LenCm)))
				]
			];
			for (const FKind& K : Kinds)
			{
				const FString KKey = K.Key;
				const bool bHas = R.Donors.Contains(KKey);
				Line->AddSlot().AutoWidth().Padding(1.f)
				[
					MakeButton([KKey, From]() { return From == KKey ? FString::Printf(TEXT("[%s]"), BoneCode(KKey)) : FString(BoneCode(KKey)); },
						[this, Bone, KKey]() { SetBoneDonor(Bone, KKey); }, Small, [bHas]() { return bHas; })
				];
			}
			Box->AddSlot().AutoHeight()[Line];
			TSharedRef<SHorizontalBox> Steps = SNew(SHorizontalBox);
			Steps->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(150.f)];
			auto Stepper = [&](const FString& Word, TFunction<FString()> Value, TFunction<void(float)> By)
			{
				Steps->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(Word))];
				Steps->AddSlot().AutoWidth().Padding(1.f)[MakeButton([]() { return FString(TEXT("-")); }, [By]() { By(-0.05f); }, Small)];
				Steps->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)
				[
					SNew(SBox).WidthOverride(50.f).HAlign(HAlign_Center)[SNew(STextBlock).Font(Small).Text_Lambda([Value]() { return FText::FromString(Value()); })]
				];
				Steps->AddSlot().AutoWidth().Padding(1.f, 1.f, 8.f, 1.f)[MakeButton([]() { return FString(TEXT("+")); }, [By]() { By(0.05f); }, Small)];
			};
			Stepper(TEXT("Length"), [this, Bone]() { const FBoneEdit* E = BoneEdits.Find(Bone); return FString::Printf(TEXT("%.0f%%"), 100.f * (E ? E->Scale : 1.f)); },
				[this, Bone](float By) { ChangeBoneScale(Bone, By); });
			Steps->AddSlot().AutoWidth().Padding(0.f, 1.f, 10.f, 1.f)
			[
				MakeButton([this, Bone]() { const FBoneEdit* E = BoneEdits.Find(Bone); return FString(E && E->bFit ? TEXT("[Own length]") : TEXT("Own length")); },
					[this, Bone]() { FitBone(Bone); }, Small,
					[this, Bone]() { const FBoneEdit* E = BoneEdits.Find(Bone); return E && !E->Donor.IsEmpty(); })      // for a swapped bone
			];
			if (R.bMovable)
			{
				Stepper(FString::Printf(TEXT("Move %s"), R.Role.IsEmpty() ? TEXT("joint") : *R.Role),
					[this, Bone, bLimb]() { const FBoneEdit* E = BoneEdits.Find(Bone); const float Pct = 100.f * (E ? E->Move : 0.f);
					// minus slides it toward the bone above's root: up a leg or arm, in toward the body on a neck or tail
					return FMath::Abs(Pct) < 0.5f ? FString(TEXT("0%")) : FString::Printf(TEXT("%s %.0f%%"),
						Pct < 0.f ? (bLimb ? TEXT("up") : TEXT("in")) : (bLimb ? TEXT("down") : TEXT("out")), FMath::Abs(Pct)); },
					[this, Bone](float By) { ChangeBoneMove(Bone, By); });
			}
			Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[Steps];
		}
	}
}

void ABFSpliceStage::BuildPanel()
{
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 22);
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 14);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	const FSlateFontInfo Line = FCoreStyle::GetDefaultFontStyle("Regular", 13);
	const float Wrap = 700.f;

	auto Button = [&](TFunction<FString()> Label, TFunction<void()> Click, const FSlateFontInfo& Font) -> TSharedRef<SWidget>
	{
		return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(6.f, 2.f))
			.OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Font).Text_Lambda([Label]() { return FText::FromString(Label()); })
			];
	};

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[SNew(STextBlock).Font(Title).Text(FText::FromString(TEXT("Splice a creature")))];
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(STextBlock).Font(Line).WrapTextAt(Wrap).ColorAndOpacity(FLinearColor(0.6f, 0.8f, 1.f))
		.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Changing %s: what you change becomes a new creature, and %s stays in the menu as it was."), *EditingName, *EditingName)); })
		.Visibility_Lambda([this]() { return EditingKey.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
	];
	List->AddSlot().AutoHeight().Padding(0.f, 2.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Start from")))];
	TSharedRef<SHorizontalBox> Bases = SNew(SHorizontalBox);
	for (const FKind& K : Kinds)
	{
		const FString KKey = K.Key, Short = K.Short;
		Bases->AddSlot().AutoWidth().Padding(2.f)
		[
			Button([this, KKey, Short]() { return Base == KKey ? FString::Printf(TEXT("[ %s ]"), *Short) : Short; },
				[this, KKey]() { SetBase(KKey); }, Big)
		];
	}
	List->AddSlot().AutoHeight()[Bases];
	// (Raven, 2026-11-15: "I don't know how to ... edit the base body". The body itself (the trunk) is the animal you
	// start from; say so, and that another start is a fresh start (Undo brings the changes back). The whole creature can
	// be made bigger or smaller.)
	List->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(STextBlock).Font(Small).WrapTextAt(Wrap).ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f))
		.Text(FText::FromString(TEXT("The body (the trunk, with its flight muscle) is the animal you start from. Picking another starts over: Undo brings your changes back.")))
	];
	{
		TSharedRef<SHorizontalBox> Whole = SNew(SHorizontalBox);
		Whole->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)[SNew(STextBlock).Font(Big).Text(FText::FromString(TEXT("Whole body")))];
		Whole->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Size")))];
		Whole->AddSlot().AutoWidth().Padding(1.f)[Button([]() { return FString(TEXT(" - ")); }, [this]() { WholeSize = FMath::Clamp(WholeSize * 0.95f, 0.1f, 3.f); RecipeChanged(); }, Small)];
		Whole->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)
		[
			SNew(SBox).WidthOverride(38.f).HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Small).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f%%"), 100.f * WholeSize)); })
			]
		];
		Whole->AddSlot().AutoWidth().Padding(1.f)[Button([]() { return FString(TEXT(" + ")); }, [this]() { WholeSize = FMath::Clamp(WholeSize / 0.95f, 0.1f, 3.f); RecipeChanged(); }, Small)];
		Whole->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f)).Text(FText::FromString(TEXT("everything at once, body and all")))
		];
		List->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[Whole];
	}
	List->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Each section comes from, at this size (Fit sizes it to this animal)")))];
	for (const FRow& R : Rows)
	{
		const FString Sec = R.Section;
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(58.f)[SNew(STextBlock).Font(Big).Text_Lambda([this, Sec, Label = FString(R.Label)]()
			{
				// (the arms are wings when they come from a Pteranodon: say so, it's where their size is changed)
				const FSection* S = Sections.Find(Sec);
				const FString From = S && !S->Donor.IsEmpty() ? S->Donor : Base;
				return FText::FromString(Sec == TEXT("Shoulder") && From == TEXT("ptera") ? FString(TEXT("Wings")) : Label);
			})]
		];
		for (const FKind& K : Kinds)
		{
			const FString KKey = K.Key, Short = K.Short;
			Row->AddSlot().AutoWidth().Padding(1.f)
			[
				Button([this, Sec, KKey, Short]()
				{
					const FSection* S = Sections.Find(Sec);
					const FString From = S && !S->Donor.IsEmpty() ? S->Donor : Base;
					return From == KKey ? FString::Printf(TEXT("[%s]"), *Short) : Short;
				}, [this, Sec, KKey]() { SetDonor(Sec, KKey); }, Small)
			];
		}
		if (HasBones(Sec))      // Advanced mode: open the section and change its bones one by one
		{
			Row->AddSlot().AutoWidth().Padding(14.f, 1.f, 0.f, 1.f)
			[
				Button([this, Sec]() { return FString(OpenSection == Sec ? TEXT("[Bones]") : TEXT("Bones")); }, [this, Sec]() { OpenBones(Sec); }, Small)
			];
		}
		List->AddSlot().AutoHeight()[Row];
		// Below the donors: its size, and how thick it is at that size.
		TSharedRef<SHorizontalBox> Sizes = SNew(SHorizontalBox);
		auto Stepper = [&](const TCHAR* Word, TFunction<float()> Get, TFunction<void(float)> By)
		{
			Sizes->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(Word))];
			Sizes->AddSlot().AutoWidth().Padding(1.f)[Button([]() { return FString(TEXT(" - ")); }, [By]() { By(-0.05f); }, Small)];
			Sizes->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)
			[
				SNew(SBox).WidthOverride(38.f).HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Small).Text_Lambda([Get]() { return FText::FromString(FString::Printf(TEXT("%.0f%%"), 100.f * Get())); })
				]
			];
			Sizes->AddSlot().AutoWidth().Padding(1.f)[Button([]() { return FString(TEXT(" + ")); }, [By]() { By(0.05f); }, Small)];
		};
		Sizes->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(58.f)];
		Stepper(TEXT("Size"), [this, Sec]() { const FSection* S = Sections.Find(Sec); return S ? S->Scale : 1.f; }, [this, Sec](float By) { ChangeScale(Sec, By); });
		Sizes->AddSlot().AutoWidth().Padding(3.f, 1.f, 10.f, 1.f)
		[
			Button([this, Sec]()
			{
				const FSection* S = Sections.Find(Sec);
				return FString(S && S->bFit ? TEXT("[Fit]") : TEXT("Fit"));
			}, [this, Sec]() { FitSize(Sec); }, Small)
		];
		Stepper(TEXT("Thick"), [this, Sec]() { const FSection* S = Sections.Find(Sec); return S ? S->Thick : 1.f; }, [this, Sec](float By) { ChangeThickness(Sec, By); });
		if (Sec == TEXT("Hip") || Sec == TEXT("Shoulder"))
		{
			Sizes->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(10.f)];
			Stepper(TEXT("Move"), [this, Sec]() { const FSection* S = Sections.Find(Sec); return S ? S->Move : 0.f; }, [this, Sec](float By) { ChangeMove(Sec, By); });
		}
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)[Sizes];
		if (HasBones(Sec))
		{
			List->AddSlot().AutoHeight().Padding(26.f, 0.f, 0.f, 6.f)
			[
				SAssignNew(BoneBoxes.FindOrAdd(Sec), SVerticalBox)
				.Visibility_Lambda([this, Sec]() { return OpenSection == Sec ? EVisibility::Visible : EVisibility::Collapsed; })
			];
		}
	}

	// Added parts: a third pair of limbs, and a tail club.
	List->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Add a third pair of limbs (press it again to take it off)")))];
	struct FPairRow { const TCHAR* Part; const TCHAR* Label; };
	for (const FPairRow& PR : { FPairRow{ TEXT("Hip"), TEXT("Legs") }, FPairRow{ TEXT("Shoulder"), TEXT("Arms") } })
	{
		const FString Part = PR.Part;
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(46.f)[SNew(STextBlock).Font(Big).Text(FText::FromString(PR.Label))]];
		for (const FKind& K : Kinds)
		{
			const FString KKey = K.Key, Short = K.Short;
			Row->AddSlot().AutoWidth().Padding(1.f)
			[
				Button([this, Part, KKey, Short]()
				{
					const FSection* S = Sections.Find(TEXT("Pair"));
					return ExtraPart == Part && S && S->Donor == KKey ? FString::Printf(TEXT("[%s]"), *Short) : Short;
				}, [this, Part, KKey]() { SetExtraPair(Part, KKey); }, Small)
			];
		}
		List->AddSlot().AutoHeight()[Row];
	}
	auto AddStepper = [&](TSharedRef<SHorizontalBox> Box, const TCHAR* Word, TFunction<float()> Get, TFunction<void(float)> By)
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[SNew(STextBlock).Font(Small).Text(FText::FromString(Word))];
		Box->AddSlot().AutoWidth().Padding(1.f)[Button([]() { return FString(TEXT(" - ")); }, [By]() { By(-0.05f); }, Small)];
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)
		[
			SNew(SBox).WidthOverride(38.f).HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Small).Text_Lambda([Get]() { return FText::FromString(FString::Printf(TEXT("%.0f%%"), 100.f * Get())); })
			]
		];
		Box->AddSlot().AutoWidth().Padding(1.f)[Button([]() { return FString(TEXT(" + ")); }, [By]() { By(0.05f); }, Small)];
	};
	auto FitButton = [&](TSharedRef<SHorizontalBox> Box, const FString& Sec)
	{
		Box->AddSlot().AutoWidth().Padding(3.f, 1.f, 10.f, 1.f)
		[
			Button([this, Sec]()
			{
				const FSection* S = Sections.Find(Sec);
				return FString(S && S->bFit ? TEXT("[Fit]") : TEXT("Fit"));
			}, [this, Sec]() { FitSize(Sec); }, Small)
		];
	};
	TSharedRef<SHorizontalBox> PairSizes = SNew(SHorizontalBox);
	PairSizes->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(46.f)];
	AddStepper(PairSizes, TEXT("Size"), [this]() { const FSection* S = Sections.Find(TEXT("Pair")); return S ? S->Scale : 1.f; }, [this](float By) { ChangeScale(TEXT("Pair"), By); });
	FitButton(PairSizes, TEXT("Pair"));
	AddStepper(PairSizes, TEXT("Thick"), [this]() { const FSection* S = Sections.Find(TEXT("Pair")); return S ? S->Thick : 1.f; }, [this](float By) { ChangeThickness(TEXT("Pair"), By); });
	PairSizes->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(10.f)];
	AddStepper(PairSizes, TEXT("Along"), [this]() { return ExtraAt; }, [this](float By) { ChangeExtraAt(By); });
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)[PairSizes];
	TSharedRef<SHorizontalBox> ClubRow = SNew(SHorizontalBox);
	ClubRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)[Button([this]() { return FString(bClub ? TEXT("[Tail club]") : TEXT("Tail club")); }, [this]() { ToggleClub(); }, Small)];
	AddStepper(ClubRow, TEXT("Size"), [this]() { const FSection* S = Sections.Find(TEXT("Club")); return S ? S->Scale : 1.f; }, [this](float By) { ChangeScale(TEXT("Club"), By); });
	FitButton(ClubRow, TEXT("Club"));
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)[ClubRow];

	auto Show = [&](TFunction<FString()> Get, TFunction<FSlateColor()> Color, const FSlateFontInfo& Font)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(STextBlock).Font(Font).WrapTextAt(Wrap)
			.Text_Lambda([Get]() { return FText::FromString(Get()); })
			.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda(Color))
			.Visibility_Lambda([Get]() { return Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		];
	};
	const auto White = []() { return FSlateColor(FLinearColor::White); };
	List->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(Big).WrapTextAt(Wrap).Text_Lambda([this]() { return FText::FromString(Name); })
	];
	auto Mass = [](double Kg) { return Kg >= 1000.0 ? FString::Printf(TEXT("%.1f t"), Kg / 1000.0) : FString::Printf(TEXT("%.0f kg"), Kg); };
	Show([this, Mass]()
	{
		if (MassKg <= 0.0)
		{
			return FString();
		}
		// Before and after: what the last change did to it.
		const bool bWas = HasBefore() && FMath::Abs(PrevMassKg - MassKg) > 0.005 * FMath::Max(MassKg, 1.0);
		return TEXT("Mass ") + Mass(MassKg) + (bWas ? TEXT("   (was ") + Mass(PrevMassKg) + TEXT(")") : FString());
	}, White, Big);
	List->AddSlot().AutoHeight().Padding(0.f, 3.f)[SAssignNew(LineageBox, SVerticalBox)];
	Show([this]()
	{
		const bool bWas = HasBefore() && PrevLegsRatio > 0.f && FMath::Abs(PrevLegsRatio - LegsRatio) >= 0.05f;
		return Legs + (bWas ? FString::Printf(TEXT("   (the weakest legs were %.1f times)"), PrevLegsRatio) : FString());
	}, [this]() { return VerdictColor(LegsVerdict); }, Line);
	Show([this]() { return Balance; }, [this]() { return bBalanceOk ? VerdictColor(TEXT("walks")) : VerdictColor(TEXT("buckles")); }, Line);
	Show([this]() { return Flight; }, [this]() { return VerdictColor(FlightVerdict); }, Line);
	Show([this]() { return FlightHelp(); }, []() { return FSlateColor(FLinearColor(0.6f, 0.8f, 1.f)); }, Small);
	Show([this]()
	{
		const bool bWas = HasBefore() && !Climb.IsEmpty() && FMath::Abs(PrevClimbDeg - ClimbDeg) >= 0.5f;
		return Climb + (bWas ? FString::Printf(TEXT("   (was %.0f°)"), PrevClimbDeg) : FString());
	}, [this]() { return VerdictColor(ClimbDeg >= 30.f ? TEXT("walks") : ClimbDeg >= 10.f ? TEXT("stands") : TEXT("buckles")); }, Line);
	Show([this]() { return Swim; }, [this]() { return VerdictColor(SwimVerdict == TEXT("swims") ? TEXT("walks") : SwimVerdict == TEXT("sinks") ? TEXT("buckles") : TEXT("stands")); }, Line);
	Show([this]() { return Flags; }, []() { return FSlateColor(FLinearColor(1.f, 0.78f, 0.3f)); }, Line);
	Show([this]() { return Notes; }, []() { return FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)); }, Small);
	Show([this]() { return Error; }, []() { return FSlateColor(FLinearColor(1.f, 0.42f, 0.32f)); }, Line);
	Show([this]() { return !Status.IsEmpty() ? Status : IsBusy() ? FString(TEXT("Working it out...")) : FString(); },
		[]() { return FSlateColor(FLinearColor(0.6f, 0.8f, 1.f)); }, Line);

	// Undo, redo, and the creature before the last change shown over it.
	TSharedRef<SHorizontalBox> History = SNew(SHorizontalBox);
	History->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[MakeButton([]() { return FString(TEXT("Undo")); }, [this]() { Undo(); }, Big, [this]() { return CanUndo(); })];
	History->AddSlot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)[MakeButton([]() { return FString(TEXT("Redo")); }, [this]() { Redo(); }, Big, [this]() { return CanRedo(); })];
	History->AddSlot().AutoWidth()
	[
		MakeButton([this]() { return FString(bShowBefore ? TEXT("[Before shown]") : TEXT("Show before")); },
			[this]() { bShowBefore = !bShowBefore; if (!bBodyShown && !Capsules.IsEmpty()) { ShowMannequin(); } }, Small)
	];
	History->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f)).Text(FText::FromString(TEXT("pale bones: before the last change")))
		.Visibility_Lambda([this]() { return bShowBefore && HasBefore() && !bBodyShown ? EVisibility::Visible : EVisibility::Collapsed; })
	];
	List->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[History];

	// Its name: given to it when it's grown (Grow body, Walk it, Test it); the menu can change it later.
	List->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[SNew(STextBlock).Font(Big).Text(FText::FromString(TEXT("Name")))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(380.f)
			[
				SAssignNew(NameBox, SEditableTextBox).Font(Line)
				.HintText(FText::FromString(TEXT("optional: it's given when the body is grown")))
				.OnTextChanged_Lambda([this](const FText& T) { GivenName = T.ToString(); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
				{
					GivenName = T.ToString();
					FSlateApplication::Get().SetAllUserFocusToGameViewport();     // the keys back to the view
				})
			]
		]
	];

	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[Button([this]() { return FString(bBodyShown ? TEXT("Body grown") : TEXT("Grow body")); }, [this]() { Grow(false); }, Big)];
	Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[Button([]() { return FString(TEXT("Walk it")); }, [this]() { Grow(true); }, Big)];
	Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[Button([]() { return FString(TEXT("Test it")); }, [this]() { Grow(true, true); }, Big)];
	Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[Button([]() { return FString(TEXT("Start over")); }, [this]() { SetBase(Base); }, Big)];
	Actions->AddSlot().AutoWidth()[Button([]() { return FString(TEXT("Back")); }, [this]() { bWantBack = true; }, Big)];
	List->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)[Actions];
	List->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("Right mouse or A / D turns the view    Wheel or W / S zooms    Ctrl+Z undo    Ctrl+Y redo    Esc goes back")))];

	Panel = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(12.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.8f))
			.Padding(16.f)
			[
				SNew(SBox).WidthOverride(740.f)
				[
					SAssignNew(Scroll, SScrollBox) + SScrollBox::Slot()[List]
				]
			]
		];
	RebuildBones();
}
