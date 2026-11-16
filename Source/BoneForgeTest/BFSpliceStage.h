// The splicing editor's screen: pick a base species, swap and resize its sections, and see mass, balance, legs, flight
// and lineage update on a capsule mannequin coloured by species. The rules live in bone_forge/splice/compose.py, which
// this runs for each change (the rig alone, about a second); Grow runs it in full and shows the real body; Walk It
// imports the creature and restarts the testing ground with it (splice/walk_it.py).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "BFRuntimeImport.h"
#include "BFSpliceStage.generated.h"

class SVerticalBox;
class SWidget;
class UCameraComponent;
class UMaterialInterface;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class APlayerController;

UCLASS()
class ABFSpliceStage : public AActor
{
	GENERATED_BODY()

public:
	ABFSpliceStage();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void Open(APlayerController* Controller, const FString& RigFolder);
	void Close();
	bool IsOpen() const { return bOpen; }
	bool WantsBack() const { return bWantBack; }

	void SetBase(const FString& Species);
	void SetDonor(const FString& Section, const FString& Species);
	void ChangeScale(const FString& Section, float By);
	void FitSize(const FString& Section);
	void ChangeThickness(const FString& Section, float By);
	/** Slide legs or arms along the body, by a share of its length (+ forward). */
	void ChangeMove(const FString& Section, float By);
	/** Put on (or, asked again, take off) a third pair of limbs: Part "Hip" or "Shoulder", from Species. */
	void SetExtraPair(const FString& Part, const FString& Species);
	/** Where along the body the extra pair sits: a share of the way from hips to chest. */
	void ChangeExtraAt(float By);
	void ToggleClub();
	/** Advanced mode (the design doc: "open a section and swap or move single bones inside it"): opens a section's
	 *  bones on the panel (again: closes it). Neck, Tail, Hip, Shoulder. */
	void OpenBones(const FString& Section);
	/** One bone (the left one stands for a pair): from another species (Species = the section's own takes it back),
	 *  its length, fitted to its own length, and its joint slid along the bone above. compose.py edit_bones. */
	void SetBoneDonor(const FString& Bone, const FString& Species);
	void ChangeBoneScale(const FString& Bone, float By);
	void FitBone(const FString& Bone);
	void ChangeBoneMove(const FString& Bone, float By);
	/** -bfclicks with -bfadvanced: the Advanced mode's buttons, pressed through Slate's mouse path. */
	void StartAdvancedClickTest();
	/** -bfclicks: presses the panel's buttons through Slate's own mouse path (hit test and all), and the view's keys,
	 *  mouse and wheel, logging what each did. */
	void StartClickTest();
	void StartWingsClickTest();
	bool IsClickTestDone() const { return ClickStep >= ClickScript.Num(); }
	/** Presses one of the panel's buttons through Slate's mouse path, once the editor isn't busy (-bftestit). */
	void PressButton(const FString& Label);
	void Grow(bool bThenWalk, bool bThenTest = false);
	void Orbit(float YawDegrees);
	bool IsBusy() const;
	bool IsBodyShown() const { return bBodyShown; }
	FString Summary() const;
	/** The recipe as it stands (what compose.py is sent). */
	FString GetRecipe() const { return RecipeJson(); }

	/** Your creatures: opens a creature already made to change it. Its own recipe if the editor made it, a catalog splice's
	 *  one swap, or one of the six as the base. Changing it makes a new creature; the original stays. */
	bool LoadCreature(const FString& CreatureKey, const FString& ShownName);
	/** Undo and redo (Ctrl+Z, Ctrl+Y): every change the recipe takes, the last 50. */
	void Undo();
	void Redo();
	bool CanUndo() const { return UndoStack.Num() > 0; }
	bool CanRedo() const { return RedoStack.Num() > 0; }
	/** The name typed in the editor's name box (given to the creature when it is grown). */
	void SetGivenName(const FString& Name);
	/** A removed creature put back: its .glb built and imported in the background, then handed to the controller. */
	void BringBack(APlayerController* Controller, const FString& RigFolder, const FString& CreatureKey);
	bool IsBringingBack() const { return !BackKey.IsEmpty(); }
	/** -bfmine: the undo and redo buttons and the name box, pressed and typed through Slate. */
	void StartUndoClickTest();
	/** Whether a text box (the editor's or the menu's) has the keyboard: its keys aren't the game's then. */
	static bool IsTyping();
	/** For the scripted checks: the Nth button under Root whose text is Label, and a click on a widget through Slate's
	 *  own mouse path (hit test and all), as a player's would be. */
	static TSharedPtr<SWidget> FindButtonIn(TSharedRef<SWidget> Root, const FString& Label, int32 Nth);
	static void ClickWidget(const TSharedPtr<SWidget>& W);
	static void SendMouse(const FVector2D& At, FKey Button, bool bDown);

private:
	struct FSection
	{
		FString Donor;           // empty: the base's own
		float Scale = 1.f;
		float Thick = 1.f;       // the flesh's width; length and joints stay
		float Move = 0.f;        // along the body, as a share of its length (legs and arms)
		bool bFit = false;       // sized to the base's own section by compose.py, which reports the size back
	};

	void BuildPanel();
	TSharedRef<SWidget> MakeButton(TFunction<FString()> Label, TFunction<void()> Click, const FSlateFontInfo& Font,
		TFunction<bool()> Enabled = nullptr) const;
	void RecipeChanged();
	FString RecipeJson() const;
	void StartJob(bool bBuild);
	void FinishJob();
	void ReadResult();
	void ShowMannequin();
	void ClearMannequin();
	bool ShowBody(const FString& ObjPath);
	void HideBody();
	void LaunchWalkIt();
	void UpdateCamera();
	UMaterialInterface* Tint(const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Fill;     // so the side away from the sun can be read
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Body;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY()
	TObjectPtr<APlayerController> PC;

	TSharedPtr<SWidget> Panel;
	TSharedPtr<SVerticalBox> LineageBox;

	bool bOpen = false;
	bool bWantBack = false;
	FString Rigs, Base = TEXT("trex");
	struct FClick { FString Label; int32 Nth; };
	TArray<FClick> ClickScript;
	int32 ClickStep = 0, ClickWait = 0;
	TSharedPtr<class SWidget> ClickTarget;
	TSharedPtr<class SScrollBox> Scroll;
	void TickClickTest();
	TSharedPtr<SWidget> FindButton(const FString& Label, int32 Nth) const;
	TMap<FString, FSection> Sections;
	// Advanced mode: the bone edits asked for, and each open-able section's bones as compose.py last reported them.
	struct FBoneEdit
	{
		FString Donor;           // empty: the section's own
		float Scale = 1.f;       // its length
		bool bFit = false;       // a swapped bone at this one's own length
		float Move = 0.f;        // its joint along the bone above, a share of that bone's length
	};
	struct FBoneRow
	{
		FString Bone, Label, Role, From;
		bool bPaired = false, bMovable = false;
		float LenCm = 0.f;
		TArray<FString> Donors;
	};
	TMap<FString, FBoneEdit> BoneEdits;
	TMap<FString, TArray<FBoneRow>> BoneRows;
	TMap<FString, TSharedPtr<SVerticalBox>> BoneBoxes;
	FString OpenSection;
	void RebuildBones();
	FString SourceOf(const FString& Section) const;
	FString ExtraPart;           // "Hip" or "Shoulder", empty for none; its donor and sizes are Sections["Pair"]
	float ExtraAt = 0.5f;
	bool bClub = false;          // its size is Sections["Club"]
	float WholeSize = 1.f;       // the whole creature (compose.py's "size"): every length times it
	FString WorkDir;

	// Jobs: one python at a time; a change made during a job waits for it.
	FProcHandle Proc;
	FProcHandle GlbProc;         // Walk it: its .glb being built (unreal/build_glb.py)
	BFRuntimeImport::FImportHandle WalkImport;   // then Unreal's import of it, polled in Tick
	bool bImportQueued = false;  // the import waiting for the core ticker to start it
	bool bJobBuild = false;
	bool bDirty = false, bWantBuild = false, bWalkAfter = false, bTestAfter = false;
	FString Swim, SwimVerdict, Climb;
	float ClimbDeg = 0.f;
	FString JobRecipe;

	// The last result
	struct FCapsule { FVector A, B; float R; FString From; };
	TArray<FCapsule> Capsules;
	FString Name, Key, Legs, LegsVerdict, Balance, Flight, FlightVerdict, Notes, Flags, Error, BodyPath, Status;
	bool bBalanceOk = true;
	double MassKg = 0.0;
	float LegsRatio = 0.f;
	bool bBodyShown = false;
	FString BuiltRecipe;

	// Before and after: the result shown before the last change, drawn as thin pale bones over the mannequin (what moved
	// pokes out of it), and its numbers beside the new ones. Only for the same base: another animal isn't a "before".
	TArray<FCapsule> PrevCapsules;
	double PrevMassKg = 0.0;
	float PrevLegsRatio = 0.f, PrevClimbDeg = 0.f;
	FString PrevKey, PrevBase, ResultBase;
	bool bShowBefore = true;
	bool HasBefore() const { return !PrevKey.IsEmpty() && PrevKey != Key && PrevBase == ResultBase; }

	// Undo: the editor's whole state at each change (compared by its recipe).
	struct FEditState
	{
		FString Base;
		TMap<FString, FSection> Sections;
		TMap<FString, FBoneEdit> BoneEdits;
		FString ExtraPart;
		float ExtraAt = 0.5f;
		bool bClub = false;
		float WholeSize = 1.f;
	};
	FString FlightHelp() const;
	FEditState Snapshot() const;
	void Restore(const FEditState& S);
	void ApplyRecipe(const TSharedPtr<class FJsonObject>& Recipe);
	TArray<FEditState> UndoStack, RedoStack;
	FEditState Committed;            // the state at the last change
	FString CommittedRecipe;
	bool bRestoring = false;

	// Your creatures: the one being changed (it stays as it was), and the name the next build gets.
	FString EditingKey, EditingName, GivenName;
	TSharedPtr<class SEditableTextBox> NameBox;
	void NameBuilt();                // after a build: the typed name goes on the creature
	FString BackKey;                 // a removed creature being put back (BringBack)

	// Camera
	float Yaw = 215.f, Pitch = -12.f, Zoom = 1.f;
	FVector Focus = FVector::ZeroVector;
	float Reach = 500.f;
};
