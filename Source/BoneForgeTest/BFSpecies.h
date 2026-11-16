// One dinosaur as the testing ground needs it: read from its Bone Forge rig JSON (gaits, limits, mass)
// plus the Skeletal Mesh that bone_forge_unreal.py imported.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/** A rig joint's motion limits in degrees, [min, max] per axis (0, 0 when the JSON has null). */
struct FBFLimits
{
	FVector2D Bend = FVector2D::ZeroVector;
	FVector2D Side = FVector2D::ZeroVector;
	FVector2D Twist = FVector2D::ZeroVector;
};

/** One collision capsule from the rig JSON: a bone's body runs joint A to joint B (component space, cm). */
struct FBFCapsule
{
	FName Bone;
	FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
	float Radius = 10.f;
	float MassKg = 1.f;
};

class UPhysicsAsset;
class UBlendSpace;

/** What the leg check (bone_forge/splice/splice.py) says the walking legs' muscle can do with the body's weight. */
enum class EBFLegs : uint8
{
	Walks,      // at least twice the muscle its share of the weight needs
	Stands,     // enough to stand, too little to walk
	Buckles     // less than its share: the legs give way
};

/** What the flight check (bone_forge/splice/splice.py; Weight and Physics tab, "Can it fly?") says the wings can do. */
/** The swim check (splice.py): floats and breathes, floats with its nostrils under, or sinks. */
enum class EBFSwim : uint8
{
	None,
	Swims,
	HeadUnder,
	Sinks
};

enum class EBFFlight : uint8
{
	None,       // no wings
	Flies,      // wing loading under the limit and enough flight muscle
	Glides,     // wing loading under the limit, too little flight muscle to flap upward
	Grounded    // wing loading over the limit: the wings can't lift it
};

/** How a head feeds (the design doc's Movement tab, chewing): the plant eaters only. */
enum class EBFFeed : uint8
{
	None,       // the T. rex, Velociraptor (meat: bolted, not chewed) and Pteranodon (fish, swallowed whole)
	Shear,      // the Triceratops: crops low plants and chews them, its tooth rows shearing past each other
	Snip,       // the Ankylosaurus: crops low plants and swallows them with little chewing
	Browse      // the Brachiosaurus: reaches up, strips leaves and swallows them whole
};
class UAnimSequence;

/** One walking leg: hip (or shoulder), knee (or elbow), and where it sits in the footfall cycle. */
struct FBFLimb
{
	FName Upper, Mid, Low;
	float PhaseOffset = 0.f;   // 0..1 of a stride
	float LengthCm = 100.f;    // hip joint height above the ground
	bool bFront = false;
};

struct FBFGait
{
	FString Name;
	float SpeedCm = 100.f;     // cm/s
	float StrideCm = 100.f;    // one full stride (both feet), cm
};

/** Jumping: the share of the legs' muscle pull that reaches the ground through their levers, and the highest any animal
 *  jumps (cm). */
constexpr float BFJumpLever = 0.3f;
constexpr float BFJumpMaxCm = 150.f;

struct FBFSpecies
{
	FString Key;               // trex, trike, raptor, ankylo, brachio, ptera
	FString Name;              // display name from the JSON
	bool bQuadruped = false;
	/** A splice on front legs too long for its body (splice.py front_stance): it stands on them alone, its hind legs
	 *  hanging, and steps them under its weight the way a biped steps its legs. */
	bool bHindHang = false;
	float MassKg = 0.f;
	FBFGait Walk, Fast;
	float TiltUpDeg = 10.f, TiltDownDeg = 10.f;   // everyday nose-up / nose-down body tilt
	TArray<FBFLimb> Limbs;
	TArray<FName> Tail, Neck;
	TMap<FName, FBFLimits> Limits;
	TMap<FName, FVector> FlightPose;   // joint positions with the wings spread (Pteranodon), rig axes, cm
	/** Has wings and a flight pose, so it can try to take off; Flight says how that goes. */
	bool CanFly() const { return FlightPose.Num() > 0; }
	EBFFlight Flight = EBFFlight::None;
	float WingLoading = 0.f;           // N/m²: weight over wing area
	float FlightMuscleShare = 0.f;     // flight muscle over body weight
	FString FlightSummary;             // the flight check in plain words
	/** Where the wing membrane's back edge leaves the body: this far from the left hip joint (rig axes, cm; mirrored
	 *  on the right), out on the skin. Splices set it (splice.py); zero puts it on the hip joint (the Pteranodon). */
	FVector MembraneHipOffset = FVector::ZeroVector;
	/** The wing bones' extra name when the wings are an added pair ("_2": upperarm_2_l), else empty. */
	FString WingPair;

	EBFLegs Legs = EBFLegs::Walks;
	FString LegSummary;                // the leg check in plain words

	/** Jumping (lab plan item 4), from the leg check: the walking legs' muscle pushes the body up as they straighten from
	 *  a crouch. Through the legs' levers about BFJumpLever of the muscle's pull reaches the ground, so JumpPush is that
	 *  push over the body's weight; past 1 it leaves the ground, and rises JumpCrouchCm x (JumpPush - 1), at most
	 *  BFJumpMaxCm (the work a kilo of muscle can do: animals of any size jump about as high, Borelli's law). */
	float JumpPush = 0.f;
	float JumpCrouchCm = 0.f;          // how far the hips drop to push off: a quarter of the hips' height
	float JumpCm = 0.f;                // how high it rises; 0 can't jump
	FString JumpSummary;               // in plain words

	/** The climb check (splice.py climb_check): the steepest slope it walks up, limited by its feet's grip or its
	 *  legs' strength, and the share of its speed it keeps going uphill, every ClimbStepDeg degrees from flat. */
	float ClimbMaxDeg = 50.f;
	TArray<float> ClimbPace;
	float ClimbStepDeg = 5.f;
	FString ClimbSummary;              // the climb check in plain words
	FString ClimbWhy;                  // what stops it on steeper slopes: "its hoof-like toes slip"
	float ClimbPaceAt(float SlopeDeg) const;
	/** Its own test, the first time it appears (ABFDino::MeasureStanding): the steepest slopes it stands still on
	 *  without tipping over, facing up them and facing down (-1: not tested, or it can't walk). Uphill caps ClimbMaxDeg. */
	bool bStandTested = false;
	float StandUpDeg = -1.f, StandDownDeg = -1.f;

	EBFSwim Swim = EBFSwim::None;
	FString SwimSummary;               // the swim check in plain words
	float SwimPitchDeg = 0.f;          // floats this far nose-up (+) or down
	float SwimComDepthCm = 0.f;        // its centre of mass this far under the surface
	float SwimSpeedCm = 50.f;          // paddling speed
	float SwimNeckRaiseDeg = 0.f;      // how far it raises its neck to keep its nostrils clear
	bool bSwimTippy = false;           // top-heavy: it has to paddle to stay upright
	float SwimDensity = 0.f;           // kg per cubic metre, the whole body

	/** A splice's species by mass, "Velociraptor 71%, Pteranodon 29%"; empty for the six starter species. */
	FString Lineage;
	FString BaseKey;                   // a splice's base species (the report card compares it with that one)
	bool IsSplice() const { return !Lineage.IsEmpty(); }
	/** Whose head it wears (a species key: its own for the six, the head's donor in a splice) and how it feeds. */
	FString HeadKey;
	EBFFeed Feed = EBFFeed::None;
	/** Its nostrils' size as a share of its skull's length (the head's species, body_tools/parts_builder.py FACE). */
	float NostrilShare = 0.f;

	TArray<FBFCapsule> Capsules;

	USkeletalMesh* Mesh = nullptr;
	/** Load: this mesh, just built in the game (Walk it: the splicing editor builds and imports it in the background),
	 *  instead of any loaded under its name. */
	USkeletalMesh* PrebuiltMesh = nullptr;
	/** Its mesh was built in the running game (BFRuntimeImport), not imported in the editor. */
	bool bRuntimeMesh = false;
	/** The mesh's saved physics asset (bone_forge_physics.py), or one built from the capsules at load. */
	UPhysicsAsset* Physics = nullptr;
	/** Baked clips (bone_forge_anims.py), null until they are made: speed blend idle/walk/fast, glide/flap blend, one-shots. */
	UBlendSpace* Locomotion = nullptr;
	UBlendSpace* FlightBlend = nullptr;
	UAnimSequence* BiteAnim = nullptr;
	UAnimSequence* RoarAnim = nullptr;
	bool HasClips() const { return Locomotion != nullptr; }
	FBox Bounds = FBox(ForceInit);   // mesh bounds in rig axes: X forward, Y right, Z up, origin on the ground under the hips

	/** Loads <RigFolder>/<Key>_rig.json and the mesh at /Game/BoneForge/<Key>. False (and Error set) when either is missing. */
	bool Load(const FString& RigFolder, const FString& InKey, FString& Error);

	const FBFLimits& LimitsOf(FName Bone) const;
};

/** Bone Forge axis signs (unreal/README.md): rig bend = BendSign * Pitch, rig side = SideSign * Yaw. */
void BFBoneSigns(FName Bone, float& BendSign, float& SideSign);

/** A joint's local rotation measured like the rig: bend and side (degrees) of where the bone points, from its reference. */
void BFMeasureJoint(FName Bone, const FQuat& RefRot, const FQuat& Rot, float& Bend, float& Side);

/** /Game/BoneForge/<Species>/<key>_skeletal/Anims, where the baked clips go. */
FString BFAnimFolder(const FString& Key);

/** The six species, then every splice in the rig folder (splice/splice.py writes them), in the order the picker
 *  shows them. */
TArray<FString> BFSpeciesKeys(const FString& RigFolder);
