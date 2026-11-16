// The driveable dinosaur: a Character whose capsule fits the body, with a posable copy of the
// imported Skeletal Mesh on top. Legs, tail, neck and jaw are posed in code every frame from the
// rig JSON's gaits and limits (FBFPoser), and the body tilts to the ground. C switches to the baked clips
// (bone_forge_anims.py) played on the Character's own mesh, to check them against the live pose.
// Species with a flight pose (the Pteranodon) can take off: the wings spread into that pose and
// flap, and the wing membrane is rebuilt every frame between the arm, the wing finger and the leg.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BFLook.h"
#include "BFPoser.h"
#include "BFSpecies.h"
#include "BFDino.generated.h"

class UPoseableMeshComponent;
class USkinnedMeshComponent;
class UProceduralMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UMaterialInstanceDynamic;

/** What a standing animal does when left alone (roadmap 7): every few seconds, one of these. */
enum class EBFIdle : uint8
{
	None,
	Look,       // looks round to one side, then the other
	Sniff,      // lowers its head to the ground and sniffs
	Shift,      // shifts its weight from one side's legs to the other's
	Swish,      // sweeps its tail
	Yawn,       // opens its jaws wide, head raised
	Shake,      // shakes off water, head to tail (after a swim)
	Feed,       // the plant eaters: crops or strips a mouthful and chews (the Triceratops) or swallows it (FBFSpecies::Feed)
	Count
};

/** Prototype 2, mass and stance: where the posed body's weight sits over the ground its feet cover. */
struct FBFBalance
{
	bool bValid = false;
	float MassKg = 0.f;               // the collision capsules' masses (the rig JSON's)
	FVector CoM = FVector::ZeroVector;   // centre of mass, world
	float HeightCm = 0.f;             // centre of mass above the ground under it
	float MarginCm = 0.f;             // how far inside (+) or outside (-) the support area it falls, seen from above
	int32 FeetDown = 0;               // feet on the ground
	bool bMoving = false;
	bool bStable = false;             // inside the support area by more than a sliver (2% of the body length)
	float FootGapMinCm = 0.f, FootGapMaxCm = 0.f;   // the feet's contact points above (+) or into (-) the ground under them
};

UCLASS()
class ABFDino : public ACharacter
{
	GENERATED_BODY()

public:
	ABFDino();

	/** Sets the species before the first tick: mesh, capsule, speeds, camera distance. */
	void Setup(const FBFSpecies& InSpecies);
	/** Game pace (Raven, 2026-11-12): every animal walks, runs and swims this many times faster than its body's real
	 *  speeds (its steps quicken with it: the gait follows its speed over its stride, so the feet don't slide), stops and
	 *  turns as much quicker, and runs Endurance times as long before it's winded. 1.8 by default, set in the menu. */
	static float GamePace;
	/** Detail by distance (the population check): its pose (legs, tail, skin, balance) worked out every this many frames
	 *  (the island's animals far off or out of view; you and the lab's always 1). Off for all: bDetailByDistance. */
	int32 DetailEvery = 1;
	static bool bDetailByDistance;
	static float Endurance;
	/** Its speeds from its body's real ones at the game pace now (again whenever the pace is changed). */
	void ApplyPace();
	/** Its own speed against its kind's (life cycle step 7: inherited), on every gait. */
	float SpeedGene = 1.f;
	/** Its body's real speeds (cm/s), before the game pace. */
	float GetRealWalkCm() const { return RealWalkCm; }
	float GetRealFastCm() const { return RealFastCm; }

	/** The creature tests itself the first time it appears (ABFGameMode::SpawnDino), whatever made it: an unseen copy
	 *  stands on imaginary slopes, facing up them and then down, posed, leg-IK'd and balanced by this very code, and the
	 *  steepest it stands still on without tipping over is its limit. Uphill that caps the climb check, so it never walks
	 *  onto a slope it would fall over on when it stops. Fills S.StandUpDeg/StandDownDeg and the climb check's words. */
	static void MeasureStanding(UWorld* World, FBFSpecies& S);

	virtual void Tick(float DeltaSeconds) override;

	/** Shift held or not: held, its speed builds to its top speed over its SprintSeconds (bAtOnce: straight there or back,
	 *  for the scripted checks of a gait). */
	void SetRunning(bool bRun, bool bAtOnce = false);
	/** How far Shift has taken it from its usual pace to its top speed, 0..1. */
	float GetSprint() const { return SprintEase(); }
	float GetSprintSeconds() const { return SprintSeconds; }
	void Zoom(float Steps);
	/** The check for close looks at the feet (-bfankles): the view low and close on them. */
	void LookAtFeet();
	/** Aims the camera at the head instead of the hips (for close-ups): Along the skull from its joint (0) to the snout
	 *  tip (1), ArmScale times the usual distance. */
	void FocusHead(bool bOn, float Along = 0.5f, float ArmScale = 1.f);
	/** Close-ups of one bone (the -bfidle check: a wing's feathers, a hip's skin): the camera aims at it from ArmCm away
	 *  and follows it; NAME_None goes back to the usual view. */
	void FocusBone(FName Bone, float ArmCm);
	/** Holds the nostrils' flare at this many cm (the -bfidle check pictures them shut and flared); negative lets go. */
	void SetNostrilOverride(float Cm) { NostrilOverride = Cm; }
	void SetMouthOpen(bool bOpen) { bMouthOpen = bOpen; }
	/** A quick bite: the neck dips, the jaw snaps shut. */
	/** A bite (a lunge, the jaws snapping). At a kill (its mouth on a target) the jaws then stay clamped and it tears the
	 *  mouthful free: the head pulled back and shaken. */
	void Bite() { BiteClock = 0.f; TearClock = bMouthTarget ? -0.55f * BFBiteSeconds : -1000.f; }
	float GetTear() const { return TearNow; }
	float GetToss() const { return TossNow; }
	/** Drinking, not cropping, for the next moment (call it each frame it drinks): the Feed idle sips instead of biting
	 *  and chewing. */
	void SetSipping() { SipHoldS = 0.3f; }
	/** Its eyes on At for the next moment (call it each frame): the head and neck turned to it, the body after them if it's
	 *  further round than the neck turns; Raise 0..1 lifts the head (alarm: head high), -1..0 lowers it (horns to it). */
	void SetGaze(const FVector& At, float Raise) { GazeAt = At; GazeRaise = Raise; GazeHoldS = 0.3f; }
	float GetGaze() const { return GazeW; }
	/** Following its nose for the next moment (call it each frame): quick sniffs, the nose down to the ground (Down > 0,
	 *  a trail) or up into the wind (Down < 0). */
	void SetSniffing(float Down) { SniffHoldS = 0.3f; SniffDown = Down; }
	/** A call (a bellow, a roar): the jaws wide, the head up, for Secs. (Silent: there are no sounds yet.) */
	void Call(float Secs) { CallHoldS = Secs; bCalling = true; }
	/** Lying down (resting, asleep): the body lowered to the ground over a couple of seconds, the legs folding under
	 *  it (its leg IK keeps the feet on the ground), the neck down to rest, eyes shut once it's down if asleep. Getting
	 *  up again it can't walk off till it's mostly up. */
	void SetLying(bool bLie, bool bAsleep = true) { bWantLie = bLie; bSleep = bLie && bAsleep; }
	float GetLie() const { return LieBlend; }
	bool IsCalling() const { return CallHoldS > 0.f; }
	/** Hurt (its health low): it limps, favouring one leg (the side kept), slower. */
	void SetLimp(float Amount) { if (LimpAmt <= 0.f && Amount > 0.f) { LimpSide = FMath::RandBool() ? 1.f : -1.f; } LimpAmt = FMath::Clamp(Amount, 0.f, 1.f); }
	float GetLimp() const { return LimpAmt; }
	/** Eating, drinking, biting (2026-11-14): its mouth goes to Target while it stands. The neck bends down (to nearly
	 *  its whole range) or up, a little more each frame till the snout is at the target's height, and the head turns
	 *  toward it. Where it can't get lower it learns so (GetMouthLowCm). */
	void SetMouthTarget(const FVector& Target) { MouthTarget = Target; bMouthTarget = true; }
	void ClearMouthTarget() { bMouthTarget = false; }
	bool HasMouthTarget() const { return bMouthTarget; }
	/** How far its snout is from the target now (cm; -1 with none). */
	float MouthMissCm() const;
	/** The lowest its snout has got (cm over its feet) with its neck bent as far down as it goes: 0 till it has found out. */
	float GetMouthLowCm() const { return MouthLowCm; }
	float GetFeedReach() const { return FeedReach; }
	float GetFeedDip() const { return FeedDip; }
	FVector GetMouthTarget() const { return MouthTarget; }
	FVector SnoutNow() const;

	/** Baked clips instead of the live pose (only when the species has them). */
	void SetClips(bool bOn);
	bool IsClips() const { return bClips; }
	FString GetClipName() const;

	// Flight (only for species with a flight pose)
	bool CanFly() const { return Species.CanFly(); }
	bool IsFlying() const { return bFlying; }
	void TakeOff();
	/** Back on the ground from flight (the island's fliers when put away far off). */
	void Land();
	/** Landing on a perch (a nest tree's shelf branch, a cliff ledge): in from Out of it, slowing, wings beating to a
	 *  hover, down onto Stand (where its capsule's middle stands); on it, it can't walk off the edge. */
	void PerchOn(const FVector& Stand, const FVector& Out);
	bool IsPerching() const { return bPerching; }
	/** On a perch (landed on one, not yet off again). */
	bool IsOnPerch() const { return bOnPerch; }
	/** Stood on a perch from the start (a hatchling, a body brought back there). */
	void SetOnPerch(const FVector& Out);
	/** Perch landings that took too long and were finished by putting it there (the check counts them). */
	int32 PerchSnaps = 0, PerchLandings = 0;
	/** Space on the ground for an animal without wings (lab plan item 4): it crouches, pushes off and rises as high as its
	 *  legs can push it (FBFSpecies::JumpCm), tucks its legs, and lands into a crouch that soaks up the fall. Too heavy, it
	 *  crouches and heaves without leaving the ground. */
	void StartJump();
	/** Its colours and covering (lab plan item 3), on its skin, toes and eyes at once: a species' colour scheme turned
	 *  round the colour wheel and lightened or darkened, and a species' covering, its tiles sized to this body. */
	void ApplyLook(const FBFLook& InLook);
	const FBFLook& GetLook() const { return Look; }
	/** The species whose skin it wears by default (its own, or a splice's base). */
	FString OwnSkinKey() const { return Species.BaseKey.IsEmpty() ? Species.Key : Species.BaseKey; }
	bool IsJumping() const { return JumpStage != EJump::None; }
	bool IsJumpCrouching() const { return JumpStage == EJump::Crouch; }
	bool IsJumpAirborne() const { return JumpStage == EJump::Air; }
	bool IsJumpLanding() const { return JumpStage == EJump::Land; }
	/** The last jump: how high its body rose (cm), and whether it left the ground. */
	float GetJumpRiseCm() const { return JumpRiseCm; }
	bool GetJumpLeft() const { return bJumpLeft; }
	/** Thrust -1..1 (W/S), bFlap = extra climbing wingbeats, Aim = the direction to fly. */
	void SetFlightInput(float InThrust, bool bInFlap, const FRotator& InAim);

	/** In the pool: floating and paddling (the swim check says how), or wading and walking on the bottom. */
	bool IsSwimming() const { return bSwimming; }
	bool IsInWater() const { return bInWater; }
	/** The snout under the surface this frame. */
	bool IsHeadUnderWater() const { return bHeadUnder; }

	/** Goes limp: the physics asset's bodies take over from the posed Body (respawn to stand up again). */
	void StartRagdoll();
	bool IsRagdoll() const { return bRagdoll; }
	/** How the ragdoll's joints sit against limits_deg (Outside = axes more than 5 degrees past a limit). */
	FString RagdollReport(int32& Outside) const;

	/** The centre of mass and support area this frame (not while flying or limp). */
	const FBFBalance& GetBalance() const { return Balance; }
	/** Draws the centre of mass, its drop line and the support area in the world. */
	void SetBalanceShown(bool bShow) { bShowBalance = bShow; }
	/** The balance in one plain line for the HUD. */
	FString GetBalanceLine() const;
	/** What leg IK did this frame: how far the hips came down and each foot moved (for the -bfstance log). */
	FString GetLegIKNote() const;
	/** How the turn looks this frame: body yaw, how far the head leads, how hard the body is turning (-bfturn log). */
	FString GetTurnNote() const;
	/** The tail against the ground (-bftails, -bfslopes logs): how far its skin would have gone into the ground as posed,
	 *  how far above it its lowest point is now, and how far a tail joint bent up for it (and past its range). Worst: the
	 *  worst of those since ResetTailWorst. */
	FString GetTailNote(bool bWorst = false) const;
	void ResetTailWorst();

	/** Idle behaviours: off for the scripted checks, so their pictures and numbers don't change from run to run. */
	void SetIdleBehaviours(bool bOn) { bIdleOn = bOn; }
	/** Starts one now (the -bfidle check), turning the first way it turns (look, shift) to its left. */
	void StartIdle(EBFIdle Which);
	EBFIdle GetIdle() const { return IdleAct; }
	static const TCHAR* IdleName(EBFIdle Which);
	static float IdleSeconds(EBFIdle Which);
	/** The ribcage's breathing scale this frame, how winded it is (0..1), and breaths a minute at rest. */
	float GetBreathScale() const;
	float GetExertion() const { return Exertion; }
	float GetBreathsPerMinute() const { return BreathRate * 60.f; }
	float GetSurge() const { return Surge; }
	float GetSag() const { return Sag; }
	float GetFootLockGap() const { return FootLockGap; }
	float GetFootSlide() const { return FootSlide; }
	float GetFootReachMiss() const { return FootReachMiss; }
	/** A bone of the shown mesh, world space (the -bfidle check measures the head against the hips). */
	FVector GetBoneWorld(FName Bone) const;
	/** The mesh its bones are shown on now: the ragdoll's once it's down, the posed body otherwise. */
	const USkinnedMeshComponent* PoseMesh() const;
	/** Blood on its skin (M_BoneForgeSkin's Blood0..3): a wound where it was bitten (the nearest bone to WorldNear, out
	 *  toward it), RadiusCm across on its skin; its muzzle reddened at a kill (grows by AddCm a mouthful); washed off in
	 *  water. Fresh blood is wet and bright and dries dark over minutes; it fades over an hour or so. */
	void AddWound(const FVector& WorldNear, float RadiusCm);
	void BloodyMuzzle(float AddCm);
	float GetWet() const { return WetNow; }
	float BloodCm(int32 Spot) const { return Spot >= 0 && Spot < 4 ? BloodSpot[Spot].W : 0.f; }
	/** Only plant eaters feed (their head's species). */
	bool CanFeed() const { return Species.Feed != EBFFeed::None; }
	/** The jaw this frame (0 shut .. 1 open), the nostrils' flare (cm their rims grow), the skull's nostril size (cm). */
	float GetMouthNow() const { return MouthNow; }
	float GetNostrilFlare() const { return NostrilFlareCm; }
	float GetNostrilCm() const { return NostrilCm; }
	/** Feathered wings (the mesh has feather coordinates, bone_forge_gltf.feather_uv): how long a feather is, and how
	 *  far the tips are pushed this frame by the wing's sway, ruffling and flutter (cm). */
	bool HasFeathers() const { return bFeathers; }
	float GetFeatherCm() const { return FeatherCm; }
	float GetFeatherTip() const { return FeatherTipCm; }
	/** Skin sliding: the most a walking limb is swung from rest this frame (degrees) and how far the skin's pattern
	 *  slides at the joint's edge for it (cm). */
	float GetSlideDeg() const { return SlideDeg; }
	float GetSlideCm() const { return SlideCm; }
	/** 0 turns the skin sliding off (the -bfidle check pictures the same stride with and without it). */
	void SetSlideScale(float Scale) { SlideScale = Scale; }
	void SetFoldScale(float Scale) { FoldScale = Scale; bFoldSnap = true; }
	float GetFoldMax() const { return FoldMax; }
	FName GetFoldMaxBone() const { return FoldMaxBone; }
	FString GetFoldNote() const { return FoldNote; }
	/** Stride cycles so far (the fraction is where it is in its stride). */
	float GetPhase() const { return Phase; }

	const FBFSpecies& GetSpecies() const { return Species; }
	float GetGroundSlopeDeg() const { return GroundSlopeDeg; }
	float GetSpeedMs() const;
	float GetAltitudeM() const;
	FString GetGaitName() const;
	float BodyLengthCm() const { return Species.Bounds.GetSize().X; }

private:
	void PoseBones(float DeltaSeconds);
	/** Breathing, the sprung surge and the idle behaviours, into the pose's inputs (Mouth: a yawn's jaw). */
	void TickLife(float DeltaSeconds, FBFPoseInput& In, float& IdleMouth);
	void AlignToGround(float DeltaSeconds);
	void TickFlight(float DeltaSeconds);
	void TickSwim(float DeltaSeconds);
	/** With the camera under the pool's surface, the view turns underwater: tinted, darker at the edges, murky. */
	void TickUnderwaterView();
	int8 CameraUnder = -1;        // -1 until the first check, so a new animal always sets the view
	void UpdateMembrane();
	void TickClips(float DeltaSeconds);
	/** The mesh that is showing: the posed Body, or the Character's mesh playing clips. */
	USkinnedMeshComponent* Shown() const;
	/** Hides the imported (folded) membrane on a mesh; the live one replaces it. */
	void HideImportedMembrane(USkinnedMeshComponent* M) const;
	/** Blinks every few seconds: the mesh carries two more eyelid sets (material slots *_LidHalf, *_LidClosed),
	 *  shown in turn (half, closed, half) over a fifth of a second. */
	void TickBlink(float DeltaSeconds);
	/** Shows the eyelid set for a blink stage (0 open, 1 half, 2 closed) on a mesh. */
	void ShowLids(USkinnedMeshComponent* M, int32 Stage) const;
	/** A point along the skull (FocusAlong of the way from the head bone to the snout tip) in capsule space, and the
	 *  skull's length. */
	bool HeadMidLocal(FVector& Mid, float& Length) const;
	/** Works out Balance from the shown mesh, and draws it when shown. */
	void TickBalance(float DeltaSeconds);
	/** A downward trace for the ground under a point: the world's, or a self-test's imaginary slope. */
	bool TraceGround(const FVector& Start, const FVector& End, FHitResult& Hit, const FCollisionQueryParams& Params) const;
	/** The self-test (MeasureStanding): this copy is never seen, never ticks, and stands on an imaginary slope through
	 *  ProbeOrigin rising ProbeTan per cm along ProbeFwd, with no world under it. */
	bool bProbe = false;
	float ProbeTan = 0.f;
	FVector ProbeOrigin = FVector::ZeroVector, ProbeFwd = FVector::ForwardVector;
	/** Stands still on an imaginary slope (degrees, + rising ahead of it) for a few seconds: false if it tips over. */
	bool ProbeStands(float SlopeDeg, float& MarginCm);
	/** Leg IK: moves each foot from the flat ground the pose assumes onto the real ground under it (traced), with
	 *  two-bone IK on hip (or shoulder), knee and ankle, and lays a planted foot's sole on that ground. */
	void SolveLegIK(TArray<FTransform>& Local, float DeltaSeconds);
	/** The tail meets the ground as terrain: where the pose puts its skin into the ground (a body tilted up a ramp with
	 *  its hips still on the flat, or facing down one), it bends up at the joints there and lies along the ground. */
	void SolveTailGround(TArray<FTransform>& Local);
	/** A low point of the tail's skin at rest, in its bone's space, and the tail joint that moves it (TailChain index). */
	struct FTailKeel
	{
		int32 Bone = INDEX_NONE;
		int32 Joint = 0;
		FVector Local = FVector::ZeroVector;
	};
	TArray<int32> TailChain;      // the tail's bones, base to tip
	TArray<int32> TailBelow;      // those and every bone hanging off them (a club), parents first
	TArray<FTailKeel> TailKeel;   // the lowest points of its skin, a few along each bone
	float TailReachCm = 100.f;    // how far above a skin point the ground is looked for
	// This frame: how far the skin would have gone into the ground as posed, how far its lowest point is above the ground
	// now (cm; BIG_NUMBER: no ground under it), and the most any one tail joint bent up for it and past its range (deg).
	float TailDip = 0.f, TailGap = BIG_NUMBER, TailBend = 0.f, TailForced = 0.f;
	float TailDipWorst = 0.f, TailGapWorst = BIG_NUMBER, TailBendWorst = 0.f, TailForcedWorst = 0.f;

	/** A walking limb's bones (indices) and its foot bone's own up direction at rest. */
	struct FLegBones
	{
		int32 Upper = INDEX_NONE, Mid = INDEX_NONE, Low = INDEX_NONE;
		FVector FootUp = FVector::UpVector;
		int32 Contact = INDEX_NONE;   // the foot's lowest joint at rest, where it meets the ground
		float Clearance = 0.f;        // that joint's height above the ground at rest
		/** Standing on two joints, one below the other (a T. rex foot's ball, then its toe tips): planted, the first sets
		 *  the foot's height and the toes bend at it onto the ground under them (a foot across a crease). */
		int32 Pivot = INDEX_NONE, Toe = INDEX_NONE;
		float PivotClearance = 0.f, ToeClearance = 0.f;
	};
	TArray<FLegBones> LegBones;
	float HipDrop = 0.f;          // cm the body is lowered so a planted foot on lower ground than the pose assumes reaches it
	// Jumping: crouch (the hips drop, the legs' IK keeps the feet down), push (launched up), air (legs tucked, no IK),
	// land (a crouch that soaks up the fall, deeper the faster it came down), then back up.
	enum class EJump : uint8 { None, Crouch, Air, Land };
	EJump JumpStage = EJump::None;
	float JumpClock = 0.f;
	float CrouchCm = 0.f;         // how far the jump lowers the hips now (on top of HipDrop's own)
	float TuckU = 0.f;            // 0..1 legs drawn up in the air
	float JumpStartZ = 0.f, JumpRiseCm = 0.f, LandCrouchCm = 0.f, FallSpeed = 0.f;
	bool bJumpLeft = false;
	void TickJump(float DeltaSeconds);
	TArray<float> LegShift;       // cm each foot was moved up (+) or down (-) this frame
	TArray<float> FootGap;        // cm each foot's lowest joint sits above the ground, as the balance check sees it
	float LegLeanLo = -40.f, LegLeanHi = 40.f;   // the upright swing kept inside 90% of every hip's and shoulder's range
	float LegLeanWideLo = -40.f, LegLeanWideHi = 40.f;   // ... of any one's: FBFPoser holds each leg to its own

	/** A collision capsule's mass at its middle, carried by its bone (Local: in the bone's space). */
	struct FMassPoint
	{
		int32 Bone = INDEX_NONE;
		FVector Local = FVector::ZeroVector;
		float Kg = 0.f;
	};
	TArray<FMassPoint> MassPoints;
	TArray<TArray<int32>> FootBones;   // per walking limb: its lowest posed bone and every bone below it
	FBFBalance Balance;
	bool bShowBalance = false;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPoseableMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Membrane;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Boom;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	FBFSpecies Species;
	FBFPoser Poser;
	float Phase = 0.f;            // stride cycles
	float MoveBlend = 0.f;        // 0 standing .. 1 at walk speed or faster
	float BodyPitch = 0.f, BodyRoll = 0.f;
	float GroundSlopeDeg = 0.f;
	/** The share of its speed it keeps on the slope it's walking up (the climb check); 1 on the flat or downhill. */
	float SlopePace = 1.f;
	/** Its walking (or running) speed now: wading halves it, an uphill slope slows it. */
	float GroundSpeedNow() const;
	void TickSprint(float DeltaSeconds);
	/** The time held (0..1) as speed: it picks up quickest at first and tapers off near the top, as a sprinter does. */
	float SprintEase() const { return SprintBuild * (1.5f - 0.5f * SprintBuild); }
	/** The same, for a flier's wingbeats (a glider has none to spare). */
	float FlapHard() const { return Species.Flight == EBFFlight::Flies ? SprintEase() : 0.f; }
	float SprintSeconds = 3.f;    // holding Shift, how long it takes to reach its top speed
	float SprintBuild = 0.f;      // 0..1: how much of that time it has held it
	float GroundRelZ = 0.f;       // the Body's height in the capsule: its origin (the ground under the hips) on the ground
	float LegLean = 0.f;          // a standing biped's legs held upright against the body's pitch (FBFPoseInput)
	float BallAheadCm = 0.f;      // how far the hind feet's balls (where they meet the ground) sit ahead of the hips
	float Upright = 0.f;          // 0..1: how much a standing biped holds its legs upright and balances (AlignToGround)
	float StepBack = 0.f;         // degrees a standing biped swings its legs back (- forward) to get its feet under its weight
	float HindLegCm = 100.f;      // hip height of the hind legs
	// Standing on both pairs: hips to shoulders at rest (mesh space), for the body's pitch on uneven ground
	bool bQuadPitch = false;
	float HipFwdCm = 0.f, QuadSpanCm = 100.f, QuadRestPitch = 0.f, QuadRiseCm = 0.f;

	// Turning (PoseBones): the head and neck lead, the body eases in after them, the tail swings out behind.
	float BaseTurnRate = 90.f;    // deg/s at full turn: heavier animals turn slower
	float TurnEase = 0.f;         // 0..1: how far into its full turn rate the body has eased
	float LookYaw = 0.f;          // degrees the head and neck lead the turn (+ to the left)
	float TurnRate = 0.f;         // -1..1: how hard the body is turning (+ to its left), 1 = BaseTurnRate
	float LastBodyYaw = 0.f;
	float LookCap = 30.f;         // how far the neck and head can turn together (90% of their side ranges)
	float FootReach = 50.f;       // cm from the hips to the farthest foot, seen from above: how fast feet move turning on the spot
	float ZoomScale = 1.f;
	float BaseArm = 600.f;
	bool bRunning = false;
	bool bHeadFocus = false;

	bool bRagdoll = false;
	bool bMouthOpen = false;
	float Mouth = 0.f;            // 0 closed .. 1 open
	float BiteClock = -1.f;       // seconds into a bite, -1 when not biting
	float TearClock = -1000.f;    // seconds into tearing a mouthful free (negative: the bite still closing; -1000 none)
	float TearNow = 0.f;
	float TossNow = 0.f;          // 0..1 the gulp's head-toss now
	float SipHoldS = 0.f;         // > 0: drinking (SetSipping)
	FVector GazeAt = FVector::ZeroVector;
	float GazeRaise = 0.f, GazeHoldS = 0.f, GazeW = 0.f;     // SetGaze, and how far it's turned to it now (0..1)
	float CallHoldS = 0.f;
	float SniffHoldS = 0.f, SniffDown = 0.f, SniffW = 0.f;
	bool bWantLie = false, bSleep = false;
	float LieBlend = 0.f;         // 0 standing .. 1 lying down
	bool bCalling = false;
	float LimpAmt = 0.f;          // 0 sound .. 1 badly hurt: it favours one hind leg (SetLimp)
	float LimpSide = 1.f;
	FVector4 BloodSpot[4] = { FVector4(0, 0, 0, 0), FVector4(0, 0, 0, 0), FVector4(0, 0, 0, 0), FVector4(0, 0, 0, 0) };   // mesh space, w radius cm (0 muzzle, 1-3 wounds)
	float BloodAge[4] = { 0.f, 0.f, 0.f, 0.f };   // seconds since each was last added to
	int32 NextWound = 1;
	float WetNow = 0.f, WetShown = 0.f;     // 0 dry .. 1 just out of the water (its skin darker and glossy)
	float BloodPush = 0.f;        // seconds till the spots are next sent to the skin
	FVector RefBoneMesh(FName Bone) const;
	void TickBlood(float DeltaSeconds);
	void PushBlood();          // 0..1 how hard it is pulling now (for the checks)
	bool bMouthTarget = false;
	FVector MouthTarget = FVector::ZeroVector;
	float FeedReach = 0.f;        // -1 neck raised .. 0 level .. 1.9 lowered nearly to its limit (held while it reaches)
	float MouthLowCm = 0.f;
	float LowHeldS = 0.f;         // how long its neck has been down as far as it goes, the target still lower
	float FeedDip = 0.f;          // degrees its front is pitched down to reach lower than its neck alone goes (a quadruped
	                              // drinking: the forelegs bend, as a giraffe's splay), 0 .. FeedDipMax()
	float FeedDipMax() const;
	float RoarClock = -1.f;       // seconds into a baked roar
	bool bClips = false;
	bool bRoarHeld = false;
	UPROPERTY()
	TObjectPtr<UObject> Playing;  // the clip or blend space the mesh is playing

	// Flight
	bool bFlying = false;
	float FlyBlend = 0.f;         // 0 standing pose .. 1 flight pose
	float FlySpeed = 0.f;         // cm/s along the flight path
	float FlapPhase = 0.f;
	float GlideU = 0.f;
	float FlapAmount = 0.f;       // 0 gliding .. 1 full wingbeats
	float Thrust = 0.f;
	bool bFlap = false;
	FRotator Aim = FRotator::ZeroRotator;
	float FlightPitch = 0.f, Bank = 0.f, LastYaw = 0.f;
	float FlightTime = 0.f;
	bool bPerching = false, bOnPerch = false;
	FVector PerchStand = FVector::ZeroVector, PerchOut = FVector(1.f, 0.f, 0.f);
	int32 PerchStage = 0;
	float PerchClock = 0.f;
	void TickPerch(float DeltaSeconds);
	float JumpZ = 0.f;            // cm/s up from a glider's leap or a failed take-off's hop, slowing under gravity
	int32 DetailFrame = 0;
	float DetailDt = 0.f;
	float AliveTime = 0.f;        // seconds since spawning (legs that buckle give way a moment after)
	float TipClock = 0.f;         // seconds it has stood with its weight outside its feet, past what stepping reaches
	float PelvisZ = 50.f;
	float ShoulderZ = 100.f, WingLenCm = 300.f;   // the flight pose's shoulder height and shoulder-to-wing-tip length
	FQuat PelvisRest = FQuat::Identity;   // the pelvis bone's rest rotation in the mesh (carries MembraneHipOffset)
	bool bMembraneBuilt = false;

	// Swimming
	bool bInWater = false, bSwimming = false, bHeadUnder = false;
	float SwimBlend = 0.f;        // 0 on its feet .. 1 floating
	FVector RestCoM = FVector::ZeroVector;   // the centre of mass at rest, in the capsule's space
	float SwimClock = 0.f;

	// Roadmap 7: breathing, secondary motion, idle behaviours.
	float BreathPhase = 0.f;      // breath cycles
	float BreathRate = 0.2f;      // breaths a second at rest, from body mass (Stahl 1967: 53.5 M^-0.26 a minute)
	float MouthMoving = 0.f, MouthEffort = 0.f;   // for the mouth's breathing, eased: how much it is on the move, how near its top speed (0..1)
	float RealWalkCm = 0.f, RealFastCm = 0.f, RealSwimCm = 0.f, RealBrake = 0.f, RealTurn = 0.f;
	float Exertion = 0.f;         // 0 rested .. 1 winded: running and swimming wind it, standing it recovers
	float Surge = 0.f, SurgeVel = 0.f, LastFwdSpeed = 0.f;   // speeding up (+) or slowing, as a damped spring
	bool bIdleOn = true;
	EBFIdle IdleAct = EBFIdle::None, LastIdle = EBFIdle::None;
	float IdleClock = 0.f;        // seconds into the behaviour
	float StillTime = 0.f;        // seconds standing still
	float IdleWait = 4.f;         // seconds of standing before the next behaviour
	float IdleSide = 1.f;         // which way it turns first: +1 its left
	bool bWet = false;            // out of the water and not yet shaken off
	float DryClock = 0.f;
	bool bWasInWater = false;
	FRandomStream IdleRand;
	/** A planted foot stays where it was put down (world) until it lifts, so the body can sway, shake or walk over it
	 *  without the foot sliding; when it lifts, the gap to where the pose has it eases away. */
	TArray<FVector> FootAnchor;
	TArray<bool> FootLocked;
	TArray<FVector> FootRelease;
	float FootLockGap = 0.f;      // the most a locked foot is away from where the pose has it, this frame (cm)
	bool bWasStanding = false;
	float IdleHold = 0.f;         // 0..1: standing feet held while an idle behaviour moves the body over them
	float FootReachMiss = 0.f;    // the most a held foot is beyond its leg's reach this frame (cm): the leg can't hold it there
	float LegGaitT = 0.f;
	float FootSlide = 0.f;        // the most a planted foot would slide (fore, aft and sideways) if it weren't held, this frame
	// The belly's jiggle: a damped spring the body's bounce (and landing) drives, shown by the skin material's
	// "Jiggle" (cm down, on the belly between hips and shoulders: bone_forge_materials.py).
	float Sag = 0.f, SagVel = 0.f, SagHz = 2.f, SagMax = 5.f;
	bool bWasOnGround = true;
	float LastFallSpeed = 0.f;
	float LastFlapU = 0.f;        // the wingbeat's fraction last frame (a failed take-off heaves on each downstroke)
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SkinMids;
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ScuteMids;   // the toes' and fingers' (also in SkinMids): keratin-coloured
	void MakeSkinMids();
	FBFLook Look;
	/** The rest pose's component-space transform of a bone (the mesh's own space). */
	FTransform RefComponent(int32 Bone) const;

	// Details on the skin (roadmap 7, second part): nostrils, feathers, the skin sliding over the hips and shoulders.
	// Set on the skin and eye materials' dynamic instances each frame (bone_forge_materials.py).
	void TickSkinDetail(float DeltaSeconds, const FBFPoseInput& In);
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> EyeMids;
	float MouthNow = 0.f;
	float NostrilCm = 0.f, NostrilFlareCm = 0.f, NostrilOverride = -1.f;
	float FocusAlong = 0.5f;      // where along the skull the head close-up aims
	FName FocusBoneName = NAME_None;
	bool bFeathers = false;
	float FeatherCm = 0.f;        // a feather's length (a third of the forearm and hand)
	float FeatherU = 0.f;         // flutter cycles
	float FeatherTipCm = 0.f;
	FVector FeatherSway[2], FeatherSwayVel[2], HandVel[2], HandPos[2];
	FVector FeatherN[2];          // each wing's feather plane, outward, at rest (mesh space)
	bool bHandValid = false;
	/** A walking limb's skin slide: its joint and rest direction (mesh space), its length, the bones it's measured by. */
	struct FSlideLimb
	{
		int32 Upper = INDEX_NONE, Mid = INDEX_NONE, Parent = INDEX_NONE;
		FVector Joint = FVector::ZeroVector, Dir = FVector::ForwardVector;
		float LenCm = 0.f;
		FQuat ParentRef = FQuat::Identity;
	};
	TArray<FSlideLimb> SlideLimbs;
	float SlideDeg = 0.f, SlideCm = 0.f, SlideScale = 1.f;
	/** A walking limb's knee or elbow, for skin folds (lab plan item 8): its bones, where it is and which way the lower
	 *  bone points at rest (mesh space), its rest angle (radians between the upper and lower bones) and its fold zone. */
	struct FFoldJoint
	{
		int32 Upper = INDEX_NONE, Mid = INDEX_NONE, Low = INDEX_NONE;
		FVector Joint = FVector::ZeroVector, LowDir = FVector::ForwardVector;
		float RestAngle = 0.f, ZoneCm = 0.f;
		float Shown = 0.f;        // the fold shown, eased toward the bend (in and out over about a tenth of a second)
		FVector Closes = FVector::ZeroVector;   // the side it last closed on
	};
	TArray<FFoldJoint> FoldJoints;
	float FoldScale = 1.f;        // -bffolds: 0 for its pictures without them
	bool bFoldSnap = false;       // switched: shown as bent at once (no easing; a paused check switches them)
	float FoldMax = 0.f;          // the most any joint is folded this frame (0..1), for the check
	FName FoldMaxBone;            // and which joint that is
	FString FoldNote;             // the values sent for it, for the check

	// Blink
	float BlinkClock = 0.f;       // seconds since the last blink
	float NextBlink = 3.f;        // seconds between blinks, drawn afresh after each
	int32 BlinkStage = -1;        // eyelid set showing (-1 none yet)
	TWeakObjectPtr<USkinnedMeshComponent> BlinkMesh;   // the mesh the stage was shown on
};
