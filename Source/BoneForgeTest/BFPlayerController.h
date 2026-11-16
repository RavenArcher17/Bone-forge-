// Player input for the testing ground: WASD + mouse to drive the dinosaur, a picker overlay (Tab),
// a status line (species, gait, speed, slope, balance, flight check), and scripted runs that save screenshots:
// -bfauto (walk, ramp, head, flight), -bfclips, -bfragdoll, -bfstance (balance), -bfsplice (wings), -bfturn (turning).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BFGameMode.h"
#include "BFPlayerController.generated.h"

class ABFDino;
class ABFSpliceStage;
class SWidget;
class STextBlock;

UCLASS()
class ABFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Walk it from the splicing editor: the creature Key, its Mesh just built in the running game, added to the
	 *  species, the editor closed and the creature spawned to play; bTest runs the test run on it. False with Error. */
	bool WalkIt(const FString& Key, USkeletalMesh* Mesh, bool bTest, FString& Error);
	/** A removed creature put back (BFSpliceStage::BringBack): its Mesh built in the game, or null with Error. */
	void CreatureBack(const FString& Key, USkeletalMesh* Mesh, const FString& Error);

private:
	// The menu's creature list (rebuilt whenever it opens or changes): the six, the catalog's splices, your creatures
	// (Edit, Rename, Remove) and the removed ones (Put back).
	void RebuildPicker();
	void EditCreature(const FString& Key);
	void RemoveCreature(const FString& Key);
	void PutBack(const FString& Key);
	/** [ and ]: the previous or next creature of all of them. */
	void StepCreature(int32 By);
	TSharedPtr<class SVerticalBox> PickerList;
	TSharedPtr<class SEditableTextBox> RenameBox;
	FString RenamingKey;        // the creature whose name is being typed in the menu
	FString PickerNote;         // what the last Remove or Put back did
	FString CurrentKey;         // the creature being played, by key (removing one moves the others' indices)
	bool bPickerDirty = false;  // rebuilt next tick, not inside a click on one of its own buttons
	/** -bfmine: your creatures and undo, by real clicks and typing. */
	TSharedPtr<SWidget> MenuButton(const FString& Label, int32 Nth) const;
	FString MineKey;            // the creature the check made
	TSharedPtr<class SScrollBox> PickerScroll;
	TSharedPtr<SWidget> MenuPending;   // a menu button scrolled into view, clicked at step 334
	int32 MenuAfter = 9;
	/** Finds the Nth menu button called Label, scrolls the menu to it, and clicks it a moment later (step 334, then Then). */
	void PressMenu(const FString& Label, int32 Nth, int32 Then);

	// A second animal beside yours (lab plan item 6), from the menu ("Beside you"). It stays and idles, follows you
	// (walking to a spot beside you, running when far behind), or copies you (the same move, Shift and Space as yours, so
	// the two gaits run side by side). Not AI: it only does what you do or where you go. And a 1.8 m person for scale.
	enum class ECompanion : uint8 { Stay, Follow, Copy };
	UPROPERTY()
	TObjectPtr<ABFDino> Companion;
	ECompanion CompanionMode = ECompanion::Stay;
	UPROPERTY()
	TObjectPtr<AActor> Person;
	UPROPERTY()
	TObjectPtr<class UTextRenderComponent> PersonLabel;   // turned to face the camera each frame
	FVector CopyMove = FVector::ZeroVector;   // this frame's move input (world), for Copy
	bool bCopyRun = false;
	void BringCompanion(int32 Index);
	void SendCompanionAway();
	/** Where the companion (or the person) stands beside the animal being played: Side +1 its right, -1 its left. */
	FVector BesideYou(float OtherWidthCm, float Side) const;
	void TickCompanion();
	void SetPerson(bool bOn);

	// The world (World and AI tab): an island made from a seed; the player goes there once it is ready.
	void MakeIsland(int32 Seed);
	TSharedPtr<class SEditableTextBox> SeedBox;
	bool bGoWhenReady = false;
	FVector WorldWalk0 = FVector::ZeroVector;   // -bfworld: where the walk began
	float WorldClearLo = 0.f, WorldClearHi = 0.f, WorldClock0 = 0.f;
	FVector WorldWater = FVector::ZeroVector;   // -bfworld: the river or shore being looked at
	// Eating (World and AI tab, step 2): E by plants, for a plant eater: a mouthful from the nearest it can reach.
	bool TryFeed();
	// Your needs (World and AI tab, step 6): on the island, from your creature's body.
	void TickNeeds(float Dt);
	/** Dead, with young living: you carry on as the oldest (step 9). False if none. */
	bool CarryOnAsYoung();
	bool TryDrink();
	bool bNeeds = false, bResting = false;
	float NeedStomach = 0.f, NeedStomachMax = 0.f, NeedThirst = 0.f, NeedTired = 0.f, NeedsRate = 1.f;
	FString NeedsKey;
	int32 NeedsLog = 0;
	// The island's animals (World and AI tab, step 3): herds that think for themselves (BFLife).
	void ToggleLife();
	UPROPERTY()
	TObjectPtr<class ABFLife> Life;
	int32 CarcassI = -1, CarcassVictim = -1, CarcassKiller = -1, CarcassTears = 0;   // the carcass check
	float CarcassWound = 0.f, CarcassMuzzle = 0.f;
	FVector2D HeadSide = FVector2D::ZeroVector, HipSide = FVector2D::ZeroVector;     // the walking check's sway
	int32 BehaveLying = 0, BehaveStare = 0, BehaveTrike = -1, BehaveRex = -1;     // the behaviour check
	bool BehaveShot = false;
	FVector2D BehaveSpeed = FVector2D::ZeroVector;
	float FxWet = 0.f;            // the world fx check
	FVector FxRiver = FVector::ZeroVector, FxRiverDn = FVector::ZeroVector;
	int32 TearShots = 0;          // the feed check's pictures of a hunter tearing at a kill
	float TearShotAt = -100.f, TearCamS = 0.f;
	int32 LifeShot = 0;
	// Shorter checks (2026-11-14): more than one check in one launch (-bfchain=bfptera+bfsave: after this one, those, in
	// turn: the engine starts once), and the game run faster (-bfspeed=2: everything, the island's clock too; the checks'
	// own clocks go with it, so a ten-minute watch takes five).
	TArray<FString> ChainQueue;
	// -bfcave: the test cave's path (mouth to chamber) and which way is out.
	TArray<FVector> CavePath;
	FVector CaveOut = FVector::ZeroVector;
	float TestSpeed = 1.f;
	static int32 StartStepOf(const FString& Check);
	// Saving the island (life cycle step 3): its clock, eaten plants, animals and you, in bone_forge/islands/island_<seed>.json;
	// every two minutes there, when you leave it for another island and when the game closes; back when you go there again.
	FString IslandSavePath(int32 Seed) const;
	bool SaveIsland(bool bTell);
	void RestoreIsland();
	bool CanSaveIslands() const { return !bAuto || bSaveCheck; }
	int32 RestoredSeed = INDEX_NONE;     // the island whose save has been brought back (or that had none)
	float SaveClock = 0.f;
	TSharedPtr<class FJsonObject> PendingYou;
	TSharedPtr<class FJsonObject> YouBack;
	bool bSaveCheck = false;
	// -bfsave / -bfstress
	TArray<FString> SaveSnapshot;
	TMap<FString, FVector> SaveBackAt;      // where each was on the frame it came back
	FString SaveSnapClock;
	int32 SaveSnapEaten = 0;
	float SaveSnapFood = 0.f;
	FVector SaveSnapYou = FVector::ZeroVector;
	FString SaveBackup;
	int32 StressStep = 0, StressFrames = 0, StressHerds = 0;
	double StressFrameSum = 0.0, StressWorst = 0.0, StressLast = 0.0, StressGame = 0.0, StressRender = 0.0, StressGpu = 0.0;
	TArray<double> StressFrameList;
	FString StressTable;
	// -bfgens: the line-ups (one grown animal of each kind, the latest generation, side by side, the same camera each time)
	int32 LineupPhase = 0, LineupNo = 0;
	float LineupClock = 0.f, LineupNextS = 20.f;
	bool bLineupFinal = false;
	TArray<TPair<int32, FTransform>> LineupBack;
	FVector LineupCam = FVector::ZeroVector, LineupLook = FVector::ZeroVector;
	FVector LineupSpot = FVector::ZeroVector;
	float LineupHours = 12.f;
	// Watching (step 5): the panel (O) and the animal the camera follows (N; M stops).
	void TickWatch();
	TSharedPtr<class STextBlock> WatchPanel;
	TSharedPtr<class SBorder> WatchBack;
	bool bWatchPanel = false;
	int32 WatchI = INDEX_NONE;
	int32 HuntSeen = 0;           // -bfhunt: the furthest the hunt has got (stalk, charge, feed) for its pictures
	float FeastSince = 0.f;
	FString FeedNote;
	double FeedNoteUntil = 0.0;
	float EatenKg = 0.f;
	int32 PlantShot = 0;                        // -bfworld: which plant picture is next
	float FrameSum = 0.f;
	int32 FrameN = 0, FeedTries = 0;

	// Look (lab plan item 3): L opens a panel for the animal being played: whose colours, turned round the colour wheel,
	// lighter or darker, and whose covering; what the fossils say. Changes show at once and are kept (my_creatures.json).
	void BuildLookPanel();
	void ShowLook(bool bShow);
	void ChangeLook(TFunctionRef<void(FBFLook&)> Edit);
	TSharedPtr<SWidget> LookWidget;
	bool bLookOpen = false;

	// Watching (lab plan item 7): slow motion (, and .), pause (P) and a frame at a time (/), a free camera (V), the
	// on-screen text hidden (U), a picture saved (F9). Time is the world's (global time dilation), so everything the
	// animals do slows together; the free camera runs on real time, so it moves at its own pace while the world is slow.
	void ApplyTimeScale();
	void SetFreeCam(bool bOn);
	void TickFreeCam(ABFDino* D);
	float TimeScale() const;
	int32 SlowStep = 0;         // 0 full speed, 1 half, 2 a quarter, 3 an eighth
	bool bPaused = false;
	int32 StepFrames = 0;       // paused: frames to let through (/)
	bool bFreeCam = false;
	bool bHudHidden = false;
	bool bCruise = false, bCruiseRun = false;   // moving when the free camera went on: it keeps going straight at that pace
	UPROPERTY()
	TObjectPtr<class ACameraActor> FreeCam;
	FVector FreeCamLastDino = FVector::ZeroVector;
	float FreeCamSpeed = 800.f; // cm a real second
	FString ShotNote;           // "Picture saved", shown a few seconds
	double ShotNoteUntil = 0.0;
	double JumpNoteUntil = 0.0; // Space pressed: how high it jumps, or why it can't, shown a few seconds
	// -bfslowmo: real and game time over a stretch, the stride's progress, where it stood when paused
	double SlowReal0 = 0.0;
	float SlowGame0 = 0.f, SlowPhase0 = 0.f;
	FVector SlowAt0 = FVector::ZeroVector;
	int32 SlowKeyAt = 0;        // the key being tapped
	FName FoldKnee;             // -bffolds: the knee the pictures are of
	int32 AutoFrame = 0;        // frames since the step began (time stands still when paused)
	ABFGameMode* Mode() const;
	ABFDino* Dino() const;
	void Pick(int32 Index);
	void ShowPicker(bool bShow);
	void BuildWidgets();
	void UpdateStatus();
	void TickAuto(float DeltaTime);
	void TickRealismChecks(float DeltaTime);
	void OpenSplicer();
	void CloseSplicer();

	/** The test run (roadmap step 6): stand, walk, run, climb, swim, glide and fly, one after another on one animal,
	 *  then a report card of what it can and can't do, and why. bThenQuit: the scripted check (-bftest). */
	void StartTestRun(int32 Index, bool bThenQuit);
	void AddResult(const FString& Test, int32 Result, const FString& Text);
	void FinishTestRun();
	void ShowReport(bool bShow);
	struct FTestLine { FString Test; int32 Result = 0; FString Text; };   // Result 1 can, 0 can't, -1 doesn't apply
	TArray<FTestLine> Report;
	FString ReportTitle, ReportFile;
	bool bTestRun = false, bTestQuit = false, bReportOpen = false;
	int32 TestRamp = 0, TestFailedRamp = -1;
	// -bfidle: which behaviour and view, and the head and hips' heights while walking (min, max)
	int32 IdleWhich = 1, IdleView = 0;
	FVector2D HeadZ = FVector2D::ZeroVector, HipZ = FVector2D::ZeroVector;
	float BreathLo = 1.f, BreathHi = 1.f;
	FVector FootL0 = FVector::ZeroVector, FootR0 = FVector::ZeroVector;
	float FootMoved = 0.f, SurgeLo = 0.f, LockGap = 0.f, Slide = 0.f, Miss = 0.f;
	FVector2D SagRange = FVector2D::ZeroVector;
	int32 Burst = 0;
	// -bfidle close-ups: nostrils, feeding, feathers, skin sliding.
	float NoseRest = 0.f, NoseMax = 0.f, NoseShot = 0.f, ShotAt = 0.f;
	FVector2D JawRange = FVector2D::ZeroVector;
	float TipMax = 0.f, RunTipMax = 0.f, SlideMaxDeg = 0.f, SlideMaxCm = 0.f, LastPhase = 0.f;
	float TestGainClock = 0.f;     // when the climb last gained height
	float TestSum = 0.f, TestMax = 0.f, TestUnder = 0.f, TestStartZ = 0.f;
	// The report card's numbers (lab plan item 5): when it reached its top speed, where a stop or turn began, the path.
	float TestT95 = -1.f, TestPath = 0.f, TestTop = 0.f;
	FVector TestPos = FVector::ZeroVector;
	/** A number set beside the base animal's (a splice) or ranked among the six (one of them). */
	FString Versus(const FBFSpecies& S, TFunctionRef<float(const FBFSpecies&)> Of, TFunctionRef<FString(float)> Show) const;
	TSharedPtr<SWidget> ReportWidget;
	TSharedPtr<class SVerticalBox> ReportBox;

	UPROPERTY()
	TObjectPtr<ABFSpliceStage> Stage;   // the splicing editor's screen
	TSharedPtr<SWidget> PickerWidget;
	TSharedPtr<SWidget> HudWidget;
	TSharedPtr<STextBlock> StatusText;
	bool bPickerOpen = false;
	bool bShowBalance = true;   // B: centre of mass and support area (Prototype 2); off in the scripted runs but -bfstance
	int32 Current = 0;
	EBFSpawnArea Area = EBFSpawnArea::Flat;
	int32 Ramp = 0;
	int32 SweepRamp = 0;        // -bfslopes: the ramp it is on

	// -bfauto: scripted drive + screenshots
	bool bAuto = false;
	bool bWantClips = false;   // keep playing baked clips across respawns
	int32 AutoSpecies = 0;
	int32 AutoStep = 0;
	float AutoClock = 0.f;
	bool bAutoForward = false;
	bool bAutoRun = false;
	bool bShotTaken = false;
	// -bfsprint: holding Shift
	bool bAutoKeys = false;     // keys pressed through Slate, read by the same input code as a player's
	bool bAutoRamp = false;     // the speed builds as a player's does (the other checks go straight to the gait they want)
	int32 SprintMark = 0;       // how many quarters of the way from SprintFrom to SprintTo it has passed (m/s)
	float SprintFrom = 0.f, SprintTo = 0.f, SprintTop = 0.f;
	int32 SprintGlideRun = 0;   // a glider's two leaps off the cliff: without Shift, with it
	FString SprintTimes;
	// -bfmouth: the jaw over a stretch of walking or running: how far it went, how many times it opened, the strides at the start
	FVector2D MouthSpan = FVector2D(1.f, 0.f);
	float MouthPeak = 0.f, MouthDip = 1.f, MouthStrides0 = 0.f, MouthStopClock = 0.f;
	int32 MouthBreaths = 0;
	bool bMouthUp = true;
	FRotator FlightAim = FRotator::ZeroRotator;
	FVector AutoDir = FVector::ZeroVector;      // -bfturn: the way the animal is steered (world)
	FVector WatchStart = FVector::ZeroVector;   // -bfsplice: where a take-off attempt began,
	float WatchPeak = 0.f, WatchAir = 0.f;      // the most height it gained (cm) and its seconds off the ground
	FString AutoOut;
	TArray<FString> AutoOnly;   // -bfonly=trex,raptor: the -bfstance run checks just these species
};
