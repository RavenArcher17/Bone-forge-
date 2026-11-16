// The Bone Forge procedural pose, as a pure function of a few inputs: stride phase, gait, idle sway,
// mouth, bite, flight blend and wingbeat. The testing ground calls it every frame (ABFDino), and the
// clip baker (UBFAnimBaker) samples it over one cycle to write real AnimSequences, so both agree.
#pragma once

#include "CoreMinimal.h"
#include "BFSpecies.h"

struct FBFPoseInput
{
	float Phase = 0.f;        // stride cycles (only the fraction matters)
	float MoveBlend = 0.f;    // 0 standing .. 1 walking
	float GaitT = 0.f;        // 0 walk stride .. 1 fast stride
	float IdleU = 0.f;        // idle sway cycles (one cycle = BFIdleSeconds)
	float Mouth = 0.f;        // 0 closed .. 1 open (roar: head raised)
	float Jaw = 0.f;          // 0 closed .. 1 open, the jaw alone: parted to breathe, the head stays where it is
	float Bite = 0.f;         // 0 .. 1 lunge: neck and head dip forward
	float FlyBlend = 0.f;     // 0 standing pose .. 1 flight pose
	float FlapPhase = 0.f;    // wingbeat cycles
	float FlapAmount = 0.f;   // 0 gliding .. 1 full wingbeats
	float GlideU = 0.f;       // glide wobble cycles (one per wingbeat)
	float FlapDownMax = 90.f; // degrees the downstroke may go below level before the wing tips reach the ground
	float LegLean = 0.f;      // degrees the body is pitched nose-up on a slope while the legs stay upright under it (each as far as its own hip or shoulder goes)
	float LookYaw = 0.f;      // degrees the head and neck lead a turn (+ to the animal's left), shared down the neck
	float Turn = 0.f;         // -1..1: how hard the body is turning (+ to its left; 1 = its full turn rate)
	float NeckRaise = 0.f;    // degrees the neck is raised nose-up (swimming), shared down it by its upward range

	// Roadmap 7, realistic animation. All default to nothing, so the clip baker's cycles are only changed by the parts
	// that follow the stride (the tail's bounce, the head held level).
	float Breath = 0.f;       // breath cycles (only the fraction matters)
	float BreathDepth = 0.f;  // 0 none, 1 resting, up to 2.5 winded: the ribcage swells and settles
	float NeckDown = 0.f;     // 0..1: the head lowered toward the ground (sniffing), half the neck's downward range at 1
	float Sniff = 0.f;        // 0..1: quick sniffing nods of the head
	float SniffU = 0.f;       // sniff cycles
	float Shift = 0.f;        // -1..1: the weight shifted onto the left (+) or right legs, the hips swaying over
	float Swish = 0.f;        // 0..1: a big sweep of the tail
	float SwishU = 0.f;       // tail sweep cycles
	float Shake = 0.f;        // 0..1: shaking off water, a wave from the head down the body to the tail
	float ShakeU = 0.f;       // shake cycles
	float Surge = 0.f;        // -1..1: speeding up (+) or slowing down (-), sprung: the neck and tail lag and overshoot
	float WingsOnly = 0.f;    // 0..1: only the wings take the flight pose (a failed take-off: it stays on its legs)
	float Lumber = 0.f;       // 0..1: a heavy walker's hips sway over each stance foot (live only: the baked clips have no leg IK to keep the feet put)
	float Tuck = 0.f;         // 0..1: the walking legs drawn up under the body, as in mid-swing (in the air, jumping)
};

/** Breaths a second at rest, the design doc's bird rule (Movement tab: dinosaurs breathed through air sacs, as birds
 *  do): 17.2 M^-0.31 a minute, about 6.5 for the raptor, 0.9 for the T. rex, 0.6 for the Brachiosaurus. */
inline float BFBreathsPerSecond(float MassKg) { return FMath::Clamp(17.2f * FMath::Pow(FMath::Max(MassKg, 1.f), -0.31f), 0.5f, 30.f) / 60.f; }

/** How much the breathing ribcage is scaled at this point in a breath. */
inline float BFBreathScale(float Breath, float Depth) { return 1.f + 0.012f * Depth * FMath::Sin(2.f * PI * Breath); }

/** How far the mouth is open breathing on the move, at this point in a breath (widest as the chest fills). Effort: 0 at
 *  its walk .. 1 at its top speed; Winded 0..1. Walking the jaw parts a little and shuts between breaths; at a run it
 *  hangs open and pumps, never quite shutting; winded, wider. A share of the jaw's own travel, so each head keeps its gape.
 *  Pump 1 .. 0: how much it opens and closes with each breath; less held near its middle opening (fast breathing). */
inline float BFBreathMouth(float Breath, float Effort, float Winded, float Pump = 1.f)
{
	const float Wave = 0.5f + 0.5f * FMath::Cos(2.f * PI * Breath);
	const float Shape = FMath::Lerp(FMath::SmoothStep(0.25f, 1.f, Wave), 0.35f + 0.65f * Wave, Effort);
	return (0.14f + 0.16f * Effort + 0.12f * Winded) * FMath::Lerp(FMath::Lerp(0.35f, 0.675f, Effort), Shape, Pump);
}

/** Seconds per idle sway cycle. */
constexpr float BFIdleSeconds = 10.f;
constexpr float BFBiteSeconds = 0.9f;
constexpr float BFRoarSeconds = 1.8f;

/** Wingbeats a second, gliding (0) to full flapping (1). */
inline float BFFlapRate(float FlapAmount) { return FMath::Lerp(1.2f, 2.6f, FlapAmount); }

/** A bite at u = 0..1 through it: the neck dips and comes back, the jaw opens on the way in and snaps shut at the bottom. */
inline void BFBiteCurves(float u, float& Bite, float& Mouth)
{
	Bite = 0.5f - 0.5f * FMath::Cos(2.f * PI * u);
	Mouth = u < 0.33f ? FMath::SmoothStep(0.f, 0.33f, u) : 1.f - FMath::SmoothStep(0.4f, 0.5f, u);
}

/** A roar at u = 0..1 through it: the jaw opens, holds, closes. */
inline float BFRoarMouth(float u) { return FMath::SmoothStep(0.f, 0.2f, u) * (1.f - FMath::SmoothStep(0.75f, 1.f, u)); }

class FBFPoser
{
public:
	/** Reads the reference pose and, for species that fly, works out the flight pose. */
	void Init(const FBFSpecies& InSpecies);

	/** Local (bone-space) transforms for every bone of the species' skeleton. */
	void Pose(const FBFPoseInput& In, TArray<FTransform>& Local) const;

	/** Stride length (cm) for a gait blend; the stride phase advances by speed / stride. */
	float StrideCm(float GaitT) const;

	const TArray<FTransform>& GetRefPose() const { return RefPose; }

private:
	void BuildFlightPose();
	/** Rot (a bone's local rotation) turned back inside the bone's bend and side limits; twist is dropped. */
	FQuat ClampToLimits(FName Bone, const FQuat& RefRot, const FQuat& Rot) const;
	/** Rotates a bone from its current local pose by rig angles (bend, side), mapped to Pitch/Yaw with the Bone Forge signs. */
	void AddBoneAngles(TArray<FTransform>& Local, FName Bone, float BendDeg, float SideDeg) const;
	/** Turns a bone about its own length (its local X). */
	void AddBoneRoll(TArray<FTransform>& Local, FName Bone, float RollDeg) const;

	const FBFSpecies* Species = nullptr;
	TArray<FTransform> RefPose;       // reference local transforms, by bone index
	TMap<int32, FQuat> FlightLocal;   // local rotation of each bone in the flight pose
	TSet<int32> WingBones;            // the wing pair's bones (upper arms and everything below them)
	float NeckLenCm = 0.f;            // neck base to head joint at rest
	float LumberCm = 0.f;             // how far a heavy walker's hips sway toward the leg it stands on (0 for light ones)
	/** Arms that don't walk (a biped's, an extra carried pair): upper and lower bone names, to swing with the stride. */
	TArray<TPair<FName, FName>> CarriedArms;
	int32 BreathBone = INDEX_NONE;    // the ribcage's first bone (spine_02): it and the trunk bones after it swell
	TArray<int32> BreathKeep;         // the bones hanging off the ribcage (neck, arms), held at their own size
};
