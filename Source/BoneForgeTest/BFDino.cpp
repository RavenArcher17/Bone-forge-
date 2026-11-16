#include "BFDino.h"
#include "BFEffects.h"
#include "BFGameMode.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Texture.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#if WITH_EDITORONLY_DATA
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

// Standing still with its centre of mass further outside its feet than this share of its length (and past what stepping
// reaches), an animal tips over. The splicing editor's readout (compose.py TIP_SHARE) uses the same.
static constexpr float TIP_SHARE = 0.02f;

ABFDino::ABFDino()
{
	PrimaryActorTick.bCanEverTick = true;

	// The Character's own mesh stays empty: the posable Body below is what you see.
	GetMesh()->SetVisibility(false);

	Body = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Membrane = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Membrane"));
	Membrane->SetupAttachment(GetCapsuleComponent());
	Membrane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Membrane->SetCastShadow(true);

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(GetCapsuleComponent());
	Boom->bUsePawnControlRotation = true;
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = 8.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Boom, USpringArmComponent::SocketName);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

bool ABFDino::bDetailByDistance = true;
float ABFDino::GamePace = 1.8f;
float ABFDino::Endurance = 3.f;

void ABFDino::ApplyPace()
{
	Species.Walk.SpeedCm = RealWalkCm * GamePace * SpeedGene;
	Species.Fast.SpeedCm = RealFastCm * GamePace * SpeedGene;
	Species.SwimSpeedCm = RealSwimCm * GamePace * SpeedGene;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = Species.Legs != EBFLegs::Walks ? 0.f : Species.Walk.SpeedCm;
	Move->MaxAcceleration = Species.Fast.SpeedCm * 1.5f;
	Move->BrakingDecelerationWalking = RealBrake * GamePace;
	BaseTurnRate = RealTurn * FMath::Sqrt(GamePace);
}

void ABFDino::Setup(const FBFSpecies& InSpecies)
{
	Species = InSpecies;
	RealWalkCm = Species.Walk.SpeedCm;
	RealFastCm = Species.Fast.SpeedCm;
	RealSwimCm = Species.SwimSpeedCm;
	Species.Walk.SpeedCm *= GamePace;
	Species.Fast.SpeedCm *= GamePace;
	Species.SwimSpeedCm *= GamePace;
	const FVector Size = Species.Bounds.GetSize();
	const float Radius = FMath::Clamp(Size.Y * 0.5f, 12.f, 400.f);
	const float HalfHeight = FMath::Max(Size.Z * 0.5f, Radius);
	GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);

	Body->SetSkinnedAssetAndUpdate(Species.Mesh);
	GroundRelZ = -HalfHeight - Species.Bounds.Min.Z;
	Body->SetRelativeLocation(FVector(0.f, 0.f, GroundRelZ));
	Poser.Init(Species);
	const TArray<FTransform>& RefPose = Poser.GetRefPose();

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = Species.Legs != EBFLegs::Walks ? 0.f : Species.Walk.SpeedCm;
	Move->MaxAcceleration = Species.Fast.SpeedCm * 1.5f;
	// Stopping takes room, the more the heavier it is: about 16 m/s² for the raptor, 4.6 for the T. rex, 3.4 for the
	// Brachiosaurus (from a sprint the T. rex takes 1.6 s and 6 m, about half its length).
	Move->bUseSeparateBrakingFriction = true;
	Move->BrakingFriction = 0.f;
	RealBrake = 3000.f * FMath::Pow(FMath::Max(Species.MassKg, 1.f), -0.2f);
	Move->BrakingDecelerationWalking = RealBrake * GamePace;
	// Holding Shift builds its speed up over this long (TickSprint), the bigger the longer: 1.9 s for the raptor, 4.9
	// for the T. rex, 6 for the Brachiosaurus.
	SprintSeconds = FMath::Clamp(1.2f * FMath::Pow(FMath::Max(Species.MassKg, 1.f), 0.15f), 1.5f, 6.f);
	SprintBuild = 0.f;
	// Heavier animals turn slower: about 90 deg/s for the T. rex, 400 for the raptor (PoseBones eases into it).
	RealTurn = FMath::Clamp(900.f / FMath::Pow(FMath::Max(Species.MassKg, 1.f), 0.25f), 40.f, 540.f);
	BaseTurnRate = RealTurn * FMath::Sqrt(GamePace);
	// Breathing slows with size: about 24 breaths a minute for the raptor, 5 for the T. rex, 3 for the Brachiosaurus.
	BreathRate = BFBreathsPerSecond(Species.MassKg);
	BreathPhase = 0.f;
	MouthMoving = MouthEffort = 0.f;
	Exertion = 0.f;
	IdleRand.Initialize(GetTypeHash(Species.Key) ^ GetUniqueID());
	IdleWait = IdleRand.FRandRange(3.f, 7.f);
	IdleAct = EBFIdle::None;
	FootLocked.Reset();
	// The belly's spring: quicker in small animals (about 4 Hz for the raptor, 2.3 for the T. rex, 1.9 for the
	// Brachiosaurus); its swing at most 3% of the hips' height.
	SagHz = FMath::Clamp(3.f * FMath::Pow(FMath::Max(Species.MassKg, 1.f) / 1000.f, -0.1f), 1.5f, 5.f);
	SagMax = 0.03f * (Species.Limbs.Num() ? Species.Limbs[0].LengthCm : 100.f);
	Sag = SagVel = 0.f;
	MakeSkinMids();
	Move->RotationRate = FRotator(0.f, BaseTurnRate, 0.f);
	LastBodyYaw = GetActorRotation().Yaw;
	LookCap = 0.f;
	TArray<FName> Lead = Species.Neck;
	Lead.Add(TEXT("head"));
	for (const FName B : Lead)
	{
		const FVector2D L = Species.LimitsOf(B).Side;
		LookCap += 0.9f * FMath::Max(FMath::Min(L.Y, -L.X), 0.f);
	}
	// The climb check: steeper than this its feet slip or its legs can't push it up, and it slides back.
	Move->SetWalkableFloorAngle(FMath::Clamp(Species.ClimbMaxDeg, 5.f, 60.f));
	SlopePace = 1.f;
	Move->MaxStepHeight = FMath::Clamp(Size.Z * 0.15f, 15.f, 120.f);
	Move->MaxFlySpeed = 3000.f;
	Move->BrakingDecelerationFlying = 0.f;

	// By its length, or its height when that's greater (a Velociraptor on Brachiosaurus arms stands 5 m tall on a 3.6 m
	// length, and the camera sat in its arms)
	BaseArm = FMath::Max(Size.X, Size.Z) * 1.1f + 300.f;
	Boom->TargetArmLength = BaseArm * ZoomScale;
	Boom->TargetOffset = FVector(0.f, 0.f, Size.Z * 0.25f);
	Boom->ProbeSize = 20.f;

	const int32 Pelvis = Species.Mesh->GetRefSkeleton().FindBoneIndex(TEXT("pelvis"));
	if (RefPose.IsValidIndex(Pelvis))
	{
		PelvisZ = RefPose[Pelvis].GetLocation().Z + (RefPose.IsValidIndex(0) ? RefPose[0].GetLocation().Z : 0.f);
	}
	if (Species.CanFly())
	{
		// The imported membrane is drawn folded for the standing pose; the live one replaces it.
		HideImportedMembrane(Body);
		const FVector* Shoulder = Species.FlightPose.Find(FName(*(TEXT("upperarm") + Species.WingPair + TEXT("_l"))));
		const FVector* Tip = Species.FlightPose.Find(FName(*(TEXT("wing_end") + Species.WingPair + TEXT("_l"))));
		if (Shoulder && Tip)
		{
			ShoulderZ = Shoulder->Z;
			WingLenCm = FMath::Max((*Tip - *Shoulder).Size(), 1.f);
		}
	}

	// Balance (Prototype 2): each collision capsule's mass sits at its middle and moves with its bone, so the centre of
	// mass follows the pose. The capsules are in component space at rest; keep each middle in its bone's space.
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	TArray<FTransform> CS = RefPose;
	for (int32 i = 0; i < CS.Num(); ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		if (P != INDEX_NONE)
		{
			CS[i] = RefPose[i] * CS[P];
		}
	}
	MassPoints.Reset();
	float CapsuleKg = 0.f;
	for (const FBFCapsule& C : Species.Capsules)
	{
		const int32 Bone = RS.FindBoneIndex(C.Bone);
		if (Bone != INDEX_NONE && CS.IsValidIndex(Bone))
		{
			MassPoints.Add({Bone, CS[Bone].InverseTransformPosition((C.A + C.B) * 0.5f), C.MassKg});
			CapsuleKg += C.MassKg;
		}
	}
	// Where it floats from (TickSwim, AlignToGround): the centre of mass at rest, in the capsule's space.
	RestCoM = FVector::ZeroVector;
	for (const FMassPoint& P : MassPoints)
	{
		RestCoM += CS[P.Bone].TransformPosition(P.Local) * P.Kg;
	}
	RestCoM = RestCoM / FMath::Max(CapsuleKg, 1e-3f) + FVector(0.f, 0.f, GroundRelZ);

	// Upright legs on a slope put the balls of the feet ahead of the hips on ground that is higher (uphill) or
	// lower (downhill) than under the hips; AlignToGround lifts the body by that.
	BallAheadCm = 0.f;
	int32 Balls = 0;
	const int32 PelvisBone = RS.FindBoneIndex(TEXT("pelvis"));
	for (const FBFLimb& L : Species.Limbs)
	{
		const int32 Ball = RS.FindBoneIndex(FName(*L.Low.ToString().Replace(TEXT("foot"), TEXT("ball"))));
		if (!L.bFront && Ball != INDEX_NONE && CS.IsValidIndex(Ball) && CS.IsValidIndex(PelvisBone))
		{
			BallAheadCm += CS[Ball].GetLocation().X - CS[PelvisBone].GetLocation().X;
			++Balls;
		}
	}
	BallAheadCm /= FMath::Max(Balls, 1);
	HindLegCm = Species.Limbs.Num() ? Species.Limbs[0].LengthCm : 100.f;     // (on front legs alone: theirs)
	for (const FBFLimb& L : Species.Limbs)
	{
		if (!L.bFront)
		{
			HindLegCm = L.LengthCm;
		}
	}
	StepBack = 0.f;
	FootBones.Reset();
	for (const FBFLimb& L : Species.Limbs)
	{
		TArray<int32>& Feet = FootBones.AddDefaulted_GetRef();
		const int32 Low = RS.FindBoneIndex(L.Low);
		for (int32 i = 0; Low != INDEX_NONE && i < RS.GetNum(); ++i)
		{
			for (int32 b = i; b != INDEX_NONE; b = RS.GetParentIndex(b))
			{
				if (b == Low)
				{
					Feet.Add(i);
					break;
				}
			}
		}
	}
	// Leg IK: each limb's three bones (they are parent and child in every rig), its foot's up at rest, and how far the
	// legs can swing back upright before a hip or shoulder runs out of range.
	LegBones.Reset();
	LegLeanLo = -90.f;
	LegLeanHi = 90.f;
	LegLeanWideLo = 0.f;
	LegLeanWideHi = 0.f;
	for (const FBFLimb& L : Species.Limbs)
	{
		FLegBones& Leg = LegBones.AddDefaulted_GetRef();
		Leg.Upper = RS.FindBoneIndex(L.Upper);
		Leg.Mid = RS.FindBoneIndex(L.Mid);
		Leg.Low = RS.FindBoneIndex(L.Low);
		if (Leg.Mid == INDEX_NONE || Leg.Low == INDEX_NONE || RS.GetParentIndex(Leg.Mid) != Leg.Upper || RS.GetParentIndex(Leg.Low) != Leg.Mid)
		{
			Leg.Upper = INDEX_NONE;   // not a simple chain: no IK on it
		}
		else if (CS.IsValidIndex(Leg.Low))
		{
			Leg.FootUp = CS[Leg.Low].GetRotation().Inverse().RotateVector(FVector::UpVector);
			Leg.Clearance = BIG_NUMBER;
			for (const int32 b : FootBones[LegBones.Num() - 1])
			{
				if (CS[b].GetLocation().Z < Leg.Clearance)
				{
					Leg.Clearance = CS[b].GetLocation().Z;
					Leg.Contact = b;
				}
			}
			// The joints it stands on (within a few cm of the lowest): the one nearest the ankle with another below it is
			// the pivot (the ball), the farthest below that the toe.
			TArray<int32> Sole;
			for (const int32 b : FootBones[LegBones.Num() - 1])
			{
				if (b != Leg.Low && CS[b].GetLocation().Z <= Leg.Clearance + FMath::Max(3.f, 0.05f * L.LengthCm))
				{
					Sole.Add(b);
				}
			}
			auto Below = [&RS](int32 Child, int32 Parent)
			{
				for (int32 p = RS.GetParentIndex(Child); p != INDEX_NONE; p = RS.GetParentIndex(p))
				{
					if (p == Parent)
					{
						return true;
					}
				}
				return false;
			};
			for (const int32 p : Sole)
			{
				bool bAbove = false, bUnder = false;
				for (const int32 t : Sole)
				{
					bAbove |= Below(t, p);
					bUnder |= Below(p, t);
				}
				if (bAbove && !bUnder)
				{
					Leg.Pivot = p;
					break;
				}
			}
			for (const int32 t : Sole)
			{
				if (Leg.Pivot != INDEX_NONE && Below(t, Leg.Pivot) && (Leg.Toe == INDEX_NONE
					|| FVector::Dist(CS[t].GetLocation(), CS[Leg.Pivot].GetLocation()) > FVector::Dist(CS[Leg.Toe].GetLocation(), CS[Leg.Pivot].GetLocation())))
				{
					Leg.Toe = t;
				}
			}
			if (Leg.Toe != INDEX_NONE)
			{
				Leg.PivotClearance = CS[Leg.Pivot].GetLocation().Z;
				Leg.ToeClearance = CS[Leg.Toe].GetLocation().Z;
			}
			else
			{
				Leg.Pivot = INDEX_NONE;
			}
		}
		const FVector2D& Bend = Species.LimitsOf(L.Upper).Bend;
		LegLeanHi = FMath::Min(LegLeanHi, -0.9f * Bend.X);
		LegLeanLo = FMath::Max(LegLeanLo, -0.9f * Bend.Y);
		LegLeanWideHi = FMath::Max(LegLeanWideHi, FMath::Min(90.f, -0.9f * Bend.X));
		LegLeanWideLo = FMath::Min(LegLeanWideLo, FMath::Max(-90.f, -0.9f * Bend.Y));
	}
	HipDrop = 0.f;
	if (const int32 PelvisI = RS.FindBoneIndex(TEXT("pelvis")); CS.IsValidIndex(PelvisI))
	{
		PelvisRest = CS[PelvisI].GetRotation();
	}
	// Hips to shoulders at rest (both pairs walking): AlignToGround pitches the body so each pair keeps its standing
	// height over the ground under its own feet.
	bQuadPitch = false;
	{
		FVector Hip = FVector::ZeroVector, Shoulder = FVector::ZeroVector;
		int32 NH = 0, NS = 0;
		for (const FBFLimb& L : Species.Limbs)
		{
			const FString U = L.Upper.ToString();
			const int32 B = RS.FindBoneIndex(L.Upper);
			if (!CS.IsValidIndex(B) || U.Contains(TEXT("_2_")))
			{
				continue;
			}
			if (U.StartsWith(TEXT("thigh_"))) { Hip += CS[B].GetLocation(); ++NH; }
			else if (U.StartsWith(TEXT("upperarm_"))) { Shoulder += CS[B].GetLocation(); ++NS; }
		}
		if (NH > 0 && NS > 0)
		{
			Hip /= NH;
			Shoulder /= NS;
			HipFwdCm = Hip.X;
			QuadSpanCm = FMath::Max(FVector2D(Shoulder.X - Hip.X, Shoulder.Z - Hip.Z).Size(), 1.f);
			QuadRestPitch = FMath::RadiansToDegrees(FMath::Atan2(Shoulder.Z - Hip.Z, Shoulder.X - Hip.X));
			QuadRiseCm = Shoulder.Z - Hip.Z;
			bQuadPitch = Shoulder.X - Hip.X > 1.f;
		}
	}
	FootReach = 50.f;     // the body turns about the hips (the mesh origin sits on the capsule's axis)
	for (const TArray<int32>& Foot : FootBones)
	{
		for (const int32 b : Foot)
		{
			FootReach = FMath::Max(FootReach, FVector2D(CS[b].GetLocation()).Size());
		}
	}

	// The tail on the ground (SolveTailGround): its bones base to tip, what hangs off them (a club), and the lowest points
	// of its own skin, the lowest vertex in each third of each bone (each vertex going with the bone that carries most of
	// it), which are kept above the ground. Without the mesh's vertices (a cooked game), the collision capsules' undersides.
	TailChain.Reset();
	TailBelow.Reset();
	TailKeel.Reset();
	{
		TArray<int32> Named;
		for (const FName Name : Species.Tail)
		{
			if (const int32 B = RS.FindBoneIndex(Name); B != INDEX_NONE)
			{
				Named.Add(B);
			}
		}
		Named.Sort();      // (a parent always comes before its children)
		for (const int32 B : Named)
		{
			if (TailChain.Num() == 0 || RS.GetParentIndex(B) == TailChain.Last())
			{
				TailChain.Add(B);
			}
		}
	}
	TArray<int32> JointOf;
	JointOf.Init(INDEX_NONE, RS.GetNum());
	TailReachCm = 50.f;
	for (int32 i = 0; TailChain.Num() > 0 && i < RS.GetNum(); ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		JointOf[i] = TailChain.IndexOfByKey(i);
		if (JointOf[i] == INDEX_NONE && P != INDEX_NONE)
		{
			JointOf[i] = JointOf[P];
		}
		if (JointOf[i] != INDEX_NONE)
		{
			TailBelow.Add(i);
			if (P != INDEX_NONE && i != TailChain[0])
			{
				TailReachCm += 0.6f * FVector::Dist(CS[i].GetLocation(), CS[P].GetLocation());
			}
		}
	}
	TMap<int32, TArray<FVector>> TailSkin;
#if WITH_EDITORONLY_DATA
	if (const FSkeletalMeshModel* Model = Species.Mesh->GetImportedModel(); Model && Model->LODModels.Num() > 0 && TailBelow.Num() > 0)
	{
		for (const FSkelMeshSection& Sec : Model->LODModels[0].Sections)
		{
			for (const FSoftSkinVertex& V : Sec.SoftVertices)
			{
				int32 Most = 0;
				for (int32 w = 1; w < MAX_TOTAL_INFLUENCES; ++w)
				{
					if (V.InfluenceWeights[w] > V.InfluenceWeights[Most])
					{
						Most = w;
					}
				}
				const int32 Bone = Sec.BoneMap.IsValidIndex(V.InfluenceBones[Most]) ? Sec.BoneMap[V.InfluenceBones[Most]] : INDEX_NONE;
				if (JointOf.IsValidIndex(Bone) && JointOf[Bone] != INDEX_NONE)
				{
					TailSkin.FindOrAdd(Bone).Add(FVector(V.Position));
				}
			}
		}
	}
#endif
	if (TailSkin.Num() == 0)
	{
		for (const FBFCapsule& C : Species.Capsules)
		{
			const int32 Bone = RS.FindBoneIndex(C.Bone);
			if (JointOf.IsValidIndex(Bone) && JointOf[Bone] != INDEX_NONE)
			{
				for (const float T : { 0.f, 0.5f, 1.f })
				{
					TailSkin.FindOrAdd(Bone).Add(FMath::Lerp(C.B, C.A, T) - FVector(0.f, 0.f, C.Radius));
				}
			}
		}
	}
	for (const TPair<int32, TArray<FVector>>& Skin : TailSkin)
	{
		const FTransform& Ref = CS[Skin.Key];
		float Lo = BIG_NUMBER, Hi = -BIG_NUMBER;
		for (const FVector& P : Skin.Value)
		{
			const float X = Ref.InverseTransformPosition(P).X;     // along the bone (its X points at its child)
			Lo = FMath::Min(Lo, X);
			Hi = FMath::Max(Hi, X);
		}
		FVector Low[3];
		float LowZ[3] = { BIG_NUMBER, BIG_NUMBER, BIG_NUMBER };
		for (const FVector& P : Skin.Value)
		{
			const int32 Third = FMath::Clamp(FMath::FloorToInt(3.f * (Ref.InverseTransformPosition(P).X - Lo) / FMath::Max(Hi - Lo, 1e-3f)), 0, 2);
			if (P.Z < LowZ[Third])
			{
				LowZ[Third] = P.Z;
				Low[Third] = P;
			}
		}
		for (int32 t = 0; t < 3; ++t)
		{
			if (LowZ[t] < BIG_NUMBER)
			{
				TailKeel.Add({ Skin.Key, JointOf[Skin.Key], Ref.InverseTransformPosition(Low[t]) });
			}
		}
	}
	ResetTailWorst();

	if (!bProbe)
	{
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] tail %s: %d joints, %d bones with skin, %d low points kept above the ground"), *Species.Key,
			TailChain.Num(), TailSkin.Num(), TailKeel.Num());
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] balance %s: %d mass points, %.1f kg from the capsules (catalog %.1f kg%s)"), *Species.Key,
			MassPoints.Num(), CapsuleKg, Species.MassKg, FMath::Abs(CapsuleKg - Species.MassKg) > 0.01f * Species.MassKg ? TEXT(", MISMATCH") : TEXT(""));
	}
}

// ---------------------------------------------------------------------------------------------- the self-test on slopes

bool ABFDino::TraceGround(const FVector& Start, const FVector& End, FHitResult& Hit, const FCollisionQueryParams& Params) const
{
	if (!bProbe)
	{
		return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	}
	// The imaginary slope, met as the world's ground is: only between the trace's ends (every ground trace is straight down).
	const float Z = ProbeOrigin.Z + ProbeTan * ((Start.X - ProbeOrigin.X) * ProbeFwd.X + (Start.Y - ProbeOrigin.Y) * ProbeFwd.Y);
	if (Z > FMath::Max(Start.Z, End.Z) || Z < FMath::Min(Start.Z, End.Z))
	{
		return false;
	}
	Hit = FHitResult();
	Hit.bBlockingHit = true;
	Hit.ImpactPoint = Hit.Location = FVector(Start.X, Start.Y, Z);
	Hit.ImpactNormal = Hit.Normal = FVector(-ProbeTan * ProbeFwd.X, -ProbeTan * ProbeFwd.Y, 1.f).GetSafeNormal();
	Hit.Distance = FMath::Abs(Start.Z - Z);
	return true;
}

bool ABFDino::ProbeStands(float SlopeDeg, float& MarginCm)
{
	ProbeFwd = GetActorForwardVector().GetSafeNormal2D();
	ProbeTan = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
	ProbeOrigin = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	bIdleOn = false;
	AliveTime = 10.f;
	TipClock = 0.f;
	// The frame the game runs, in its order: the body tilted and set on the ground, posed and its feet put down by leg
	// IK, then its balance (and a biped's stepping). The first step is a whole second, which settles everything that
	// eases (tilt, height, hip drop, stepping) at once, as if it had stopped there a while ago; then 2.5 s at 30 frames
	// a second, tipping by the game's own rule: a second with its weight outside its feet, stepping as far as it goes.
	bool bTipped = false;
	for (int32 i = 0; i <= 75 && !bTipped; ++i)
	{
		const float Dt = i == 0 ? 1.f : 1.f / 30.f;
		AlignToGround(Dt);
		PoseBones(Dt);
		Body->RefreshBoneTransforms();      // (the game's frame does this after the actor's tick)
		TickBalance(Dt);
		bTipped = TipClock > 1.f;
	}
	MarginCm = Balance.bValid ? Balance.MarginCm : 0.f;
	return !bTipped;
}

void ABFDino::MeasureStanding(UWorld* World, FBFSpecies& S)
{
	if (S.bStandTested || !World || !S.Mesh)
	{
		return;
	}
	S.bStandTested = true;
	if (S.Legs != EBFLegs::Walks || S.Limbs.Num() == 0)
	{
		return;      // (it can't walk anywhere to stop)
	}
	const double T0 = FPlatformTime::Seconds();
	int32 Tries = 0;
	float LastMargin = 0.f;
	auto Stands = [&](float Deg)
	{
		const FTransform At(FVector(0.f, 0.f, 500000.f));     // out of everything's way, though it touches nothing
		ABFDino* P = World->SpawnActorDeferred<ABFDino>(ABFDino::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!P)
		{
			return true;
		}
		P->bProbe = true;
		P->SetActorHiddenInGame(true);
		P->SetActorEnableCollision(false);
		P->SetActorTickEnabled(false);
		P->Setup(S);
		P->FinishSpawning(At);
		const bool bStands = P->ProbeStands(Deg, LastMargin);
		P->Destroy();
		++Tries;
		return bStands;
	};
	// The steepest it stands on one way: up in 5° steps to Top, then in 1° steps from the last it stood on to the first
	// it tipped on.
	auto Steepest = [&](float Sign, float Top)
	{
		float Stood = 0.f;
		for (float D = 5.f; Stood < Top; D += 5.f)
		{
			const float A = FMath::Min(D, Top);
			if (Stands(Sign * A))
			{
				Stood = A;
				continue;
			}
			for (float E = Stood + 1.f; E < A - 0.01f && Stands(Sign * E); E += 1.f)
			{
				Stood = E;
			}
			break;
		}
		return Stood;
	};
	const float Climb = FMath::Clamp(S.ClimbMaxDeg, 0.f, 60.f);
	S.StandUpDeg = Steepest(1.f, Climb);
	const float Top = FMath::Min(Climb, S.StandUpDeg);
	S.StandDownDeg = Steepest(-1.f, Top);    // (it can't walk onto anything steeper either way)

	// Its feet and legs would take it higher than it can stop: the climb check stops where its balance does.
	const float Was = S.ClimbMaxDeg;
	if (S.StandUpDeg < S.ClimbMaxDeg)
	{
		S.ClimbWhy = FString::Printf(TEXT("standing still on anything steeper it tips over backwards, as it found by trying "
			"when it first appeared (its feet and legs alone would take it up to %.0f)"), Was);
		S.ClimbMaxDeg = S.StandUpDeg;
		FString Slows;
		if (S.ClimbMaxDeg > 0.f)
		{
			const float AtTop = FMath::Clamp(S.ClimbPaceAt(S.ClimbMaxDeg), 0.f, 1.f);
			const int32 AtHalf = FMath::RoundToInt(S.ClimbMaxDeg * 0.5f / 5.f) * 5;
			Slows = AtTop >= 0.999f ? FString(TEXT(" It keeps full speed all the way up."))
				: FString::Printf(TEXT(" It slows as the slope steepens: %.0f%% of its walking speed at %d degrees, %.0f%% at its limit."),
					100.f * FMath::Clamp(S.ClimbPaceAt(AtHalf), 0.f, 1.f), AtHalf, 100.f * AtTop);
		}
		S.ClimbSummary = FString::Printf(TEXT("Climbs slopes up to %.0f degrees: %s.%s"), S.ClimbMaxDeg, *S.ClimbWhy, *Slows);
	}
	if (S.StandDownDeg < Top)
	{
		S.ClimbSummary += FString::Printf(TEXT(" Facing downhill it stands still on slopes up to %.0f degrees; steeper, it tips over forwards."), S.StandDownDeg);
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] stand test %s: stands still facing uphill on slopes up to %.0f degrees, downhill %.0f "
		"(of %.0f tried; %d tries, %.0f ms); climb limit %.0f -> %.0f"), *S.Key, S.StandUpDeg, S.StandDownDeg, Climb, Tries,
		1000.0 * (FPlatformTime::Seconds() - T0), Was, S.ClimbMaxDeg);
}

FString ABFDino::GetTurnNote() const
{
	return FString::Printf(TEXT("body yaw %.0f, head leads %.0f° %s (of %.0f), body turning at %.0f%% of its %.0f deg/s"),
		GetActorRotation().Yaw, FMath::Abs(LookYaw), LookYaw >= 0.f ? TEXT("left") : TEXT("right"), LookCap,
		100.f * FMath::Abs(TurnRate), BaseTurnRate);
}

float ABFDino::GetSpeedMs() const
{
	return (bFlying ? GetVelocity().Size() : GetVelocity().Size2D()) / 100.f;
}

float ABFDino::GetAltitudeM() const
{
	FHitResult Hit;
	const FVector Start = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BFAlt), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 100000.f), ECC_Visibility, Params))
	{
		return FMath::Max(0.f, (Hit.Distance - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) / 100.f);
	}
	return 0.f;
}

FString ABFDino::GetGaitName() const
{
	if (bFlying)
	{
		if (SprintEase() > 0.3f && Thrust > 0.f && Species.Flight != EBFFlight::Grounded)
		{
			return Species.Flight == EBFFlight::Glides ? TEXT("diving") : TEXT("flapping hard");
		}
		return FlapAmount > 0.35f ? TEXT("flapping") : TEXT("gliding");
	}
	if (bSwimming)
	{
		return TEXT("swimming");
	}
	const float Speed = GetVelocity().Size2D();
	if (Speed < 5.f)
	{
		return TEXT("standing");
	}
	return Speed > (Species.Walk.SpeedCm + Species.Fast.SpeedCm) * 0.5f ? Species.Fast.Name : Species.Walk.Name;
}

void ABFDino::SetRunning(bool bRun, bool bAtOnce)
{
	bRunning = bRun;
	if (bAtOnce)
	{
		SprintBuild = bRun ? 1.f : 0.f;
	}
	GetCharacterMovement()->MaxWalkSpeed = GroundSpeedNow();
}

void ABFDino::TickSprint(float DeltaSeconds)
{
	// Shift is an accelerator: held while it's going somewhere (on the ground or in the water any way, in the air
	// forward) its speed builds, the longer the faster, up to its top speed; let go, it eases back down in half the time.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const bool bPushing = bFlying ? Thrust > 0.f : Move->GetCurrentAcceleration().SizeSquared2D() > 1.f;
	const bool bBuild = bRunning && bPushing && !bRagdoll;
	SprintBuild = FMath::Clamp(SprintBuild + DeltaSeconds / (bBuild ? SprintSeconds : -0.5f * SprintSeconds), 0.f, 1.f);
	// Stopped or stopped dead against something, it has nothing built up to carry on with.
	const float Slow = 0.3f * (bSwimming ? Species.SwimSpeedCm : Species.Walk.SpeedCm);
	if (!bFlying && GetVelocity().Size2D() < Slow)
	{
		SprintBuild = 0.f;
	}
	// At speed its path swings only as fast as its body can turn: a wide arc, not a slide sideways. (Walking, it turns as
	// sharply as it always has; the push that gets it moving from a standstill would swing a fast animal's path round too.)
	const float AtSpeed = FMath::Clamp((GetVelocity().Size2D() - Species.Walk.SpeedCm) / FMath::Max(Species.Fast.SpeedCm - Species.Walk.SpeedCm, 1.f), 0.f, 1.f);
	Move->GroundFriction = FMath::Lerp(8.f, FMath::Clamp(FMath::DegreesToRadians(BaseTurnRate), 1.f, 8.f), SprintEase());
	Move->MaxAcceleration = FMath::Lerp(Species.Fast.SpeedCm * 1.5f, 2.f * (Species.Fast.SpeedCm - Species.Walk.SpeedCm) / SprintSeconds, AtSpeed);
}

float ABFDino::GroundSpeedNow() const
{
	// The leg check (splice.py): legs that can only stand can't walk. Shift takes it from its walk toward its fast gait
	// (TickSprint). Wading, the water drags at the legs: half speed. Uphill, the climb check: it slows as the slope
	// steepens, down to a crawl near its limit.
	const float Base = Species.Legs != EBFLegs::Walks ? 0.f : FMath::Lerp(Species.Walk.SpeedCm, Species.Fast.SpeedCm, SprintEase());
	// Hurt, it limps, slower; winded (after a chase), it can't keep up its pace.
	return Base * (1.f - FMath::SmoothStep(0.1f, 0.4f, LieBlend)) * (bInWater ? 0.5f : 1.f) * SlopePace * (1.f - 0.45f * LimpAmt) * (1.f - 0.3f * FMath::SmoothStep(0.8f, 1.f, Exertion));
}

void ABFDino::LookAtFeet()
{
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float Leg = 100.f;
	for (const FBFLimb& L : Species.Limbs)
	{
		Leg = FMath::Max(Leg, L.LengthCm);
	}
	Boom->TargetOffset = FVector(0.f, 0.f, 0.3f * Leg - Half);
	Boom->TargetArmLength = 1.1f * Leg;
	Boom->bEnableCameraLag = false;
	Boom->bDoCollisionTest = false;
}

void ABFDino::Zoom(float Steps)
{
	ZoomScale = FMath::Clamp(ZoomScale * FMath::Pow(0.88f, Steps), 0.25f, 3.f);
	Boom->TargetArmLength = BaseArm * ZoomScale;
}

bool ABFDino::HeadMidLocal(FVector& Mid, float& Length) const
{
	const USkinnedMeshComponent* M = Shown();      // the posed body or the one playing clips
	if (!M || M->GetBoneIndex(TEXT("head")) == INDEX_NONE || M->GetBoneIndex(TEXT("head_end")) == INDEX_NONE)
	{
		return false;
	}
	const FTransform& Root = GetCapsuleComponent()->GetComponentTransform();
	const FVector H = Root.InverseTransformPosition(M->GetBoneLocation(TEXT("head")));
	const FVector E = Root.InverseTransformPosition(M->GetBoneLocation(TEXT("head_end")));
	Mid = FMath::Lerp(H, E, FocusAlong);
	Length = FVector::Dist(H, E);
	return true;
}

void ABFDino::FocusBone(FName Bone, float ArmCm)
{
	FocusBoneName = Bone;
	if (Bone == NAME_None)
	{
		FocusHead(false);
		return;
	}
	bHeadFocus = false;
	Boom->TargetOffset = FVector::ZeroVector;
	Boom->TargetArmLength = ArmCm;
	Boom->bDoCollisionTest = false;
	Boom->bEnableCameraLag = false;
}

void ABFDino::FocusHead(bool bOn, float Along, float ArmScale)
{
	bHeadFocus = bOn;
	FocusAlong = Along;
	FocusBoneName = NAME_None;
	const FVector Size = Species.Bounds.GetSize();
	// Aim at the middle of the skull (head bone to snout tip), about a skull length away, so the face fills the
	// view (Tick keeps aiming there as the head moves); without those bones, at the front end of the bounds.
	FVector HeadMid(Species.Bounds.Max.X * 0.85f, 0.f, Size.Z * 0.15f);
	float HeadArm = FMath::Max(Size.Z, Size.Y) * 1.4f + 80.f;
	float Length = 0.f;
	if (HeadMidLocal(HeadMid, Length))
	{
		HeadArm = (Length * 1.1f + 30.f) * ArmScale;
	}
	Boom->SetRelativeLocation(bOn ? HeadMid : FVector::ZeroVector);
	Boom->TargetOffset = bOn ? FVector::ZeroVector : FVector(0.f, 0.f, Size.Z * 0.25f);
	Boom->TargetArmLength = bOn ? HeadArm : BaseArm * ZoomScale;
	Boom->bDoCollisionTest = !bOn;      // the pivot is inside the skull, which would pull the camera into it
}

void ABFDino::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickUnderwaterView();
	if (bRagdoll)
	{
		// The capsule stays where the animal went limp; the camera follows the falling body instead.
		Boom->SetWorldLocation(GetMesh()->GetBoneLocation(TEXT("pelvis")));
		return;
	}
	if (!Species.Mesh)
	{
		return;
	}
	AliveTime += DeltaSeconds;
	if (Species.Legs == EBFLegs::Buckles && AliveTime > 1.2f && !bInWater)
	{
		StartRagdoll();      // the leg check says its legs can't hold its weight up: they give way (in water it floats)
		return;
	}
	{
		const bool bCan = bWantLie && !bFlying && !bRagdoll && !bInWater && GetVelocity().Size2D() < 40.f;
		LieBlend = FMath::FInterpConstantTo(LieBlend, bCan ? 1.f : 0.f, DeltaSeconds, bCan ? 0.4f : 0.7f);
	}
	if (bCalling)
	{
		CallHoldS -= DeltaSeconds;
		bMouthOpen = CallHoldS > 0.f;          // (its own call: the mouth wide, the head raised, FBFPoser)
		bCalling = CallHoldS > 0.f;
	}
	Mouth = FMath::FInterpTo(Mouth, bMouthOpen ? 1.f : 0.f, DeltaSeconds, 7.f);
	TickSprint(DeltaSeconds);
	TickSwim(DeltaSeconds);
	TickJump(DeltaSeconds);
	AlignToGround(DeltaSeconds);
	if (Species.CanFly())
	{
		TickFlight(DeltaSeconds);
	}
	// Its movement far off: fewer steps a second (each covering the time since the last).
	{
		const int32 Every = bDetailByDistance ? DetailEvery : 1;
		const float Interval = Every >= 8 ? 1.f / 15.f : Every >= 4 ? 1.f / 30.f : 0.f;
		if (GetCharacterMovement()->GetComponentTickInterval() != Interval && !IsPlayerControlled())
		{
			GetCharacterMovement()->SetComponentTickInterval(Interval);
		}
	}
	// Its pose, every frame near you; far off or out of view every few (the time between made up in one step).
	DetailDt += DeltaSeconds;
	if (++DetailFrame >= (bDetailByDistance ? FMath::Max(DetailEvery, 1) : 1))
	{
		const float PoseDt = DetailDt;
		DetailFrame = 0;
		DetailDt = 0.f;
		PoseBones(PoseDt);
		if (Species.CanFly())
		{
			UpdateMembrane();
		}
		TickBlink(PoseDt);
		if (!bDetailByDistance || DetailEvery <= 1)
		{
			TickBalance(PoseDt);       // (judged on a pose a few frames old, a far body would tip over that wouldn't)
		}
		else
		{
			Balance = FBFBalance();
		}
	}
	FVector HeadMid;
	float Length;
	if (bHeadFocus && HeadMidLocal(HeadMid, Length))
	{
		Boom->SetRelativeLocation(HeadMid);      // the head sways as the animal idles (the Brachiosaurus most)
	}
	if (FocusBoneName != NAME_None && Shown() && Shown()->GetBoneIndex(FocusBoneName) != INDEX_NONE)
	{
		Boom->SetRelativeLocation(GetCapsuleComponent()->GetComponentTransform().InverseTransformPosition(Shown()->GetBoneLocation(FocusBoneName)));
	}
}

// ---------------------------------------------------------------------------------------------- blink

void ABFDino::TickBlink(float DeltaSeconds)
{
	BlinkClock += DeltaSeconds;
	const float T = BlinkClock - NextBlink;          // seconds into the blink
	int32 Stage = T < 0.f ? 0 : T < 0.05f ? 1 : T < 0.15f ? 2 : T < 0.22f ? 1 : 0;
	if (bSleep && LieBlend > 0.8f)
	{
		Stage = 2;          // asleep: eyes shut
	}
	else if (bSleep && LieBlend > 0.5f)
	{
		Stage = 1;          // drowsing off
	}
	if (T >= 0.22f)
	{
		BlinkClock = 0.f;
		NextBlink = FMath::FRandRange(2.5f, 6.f);
	}
	USkinnedMeshComponent* M = Shown();
	if (M && (Stage != BlinkStage || BlinkMesh.Get() != M))
	{
		ShowLids(M, Stage);
		BlinkStage = Stage;
		BlinkMesh = M;
	}
}

void ABFDino::ShowLids(USkinnedMeshComponent* M, int32 Stage) const
{
	if (const FSkeletalMeshRenderData* RD = Species.Mesh ? Species.Mesh->GetResourceForRendering() : nullptr)
	{
		const TArray<FSkeletalMaterial>& Mats = Species.Mesh->GetMaterials();
		for (int32 s = 0; RD->LODRenderData.Num() > 0 && s < RD->LODRenderData[0].RenderSections.Num(); ++s)
		{
			const int32 Mat = RD->LODRenderData[0].RenderSections[s].MaterialIndex;
			if (!Mats.IsValidIndex(Mat))
			{
				continue;
			}
			const FString Slot = Mats[Mat].MaterialSlotName.ToString();
			if (Slot.EndsWith(TEXT("LidHalf")))
			{
				M->ShowMaterialSection(Mat, s, Stage == 1, 0);
			}
			else if (Slot.EndsWith(TEXT("LidClosed")))
			{
				M->ShowMaterialSection(Mat, s, Stage == 2, 0);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------- ragdoll

void ABFDino::StartRagdoll()
{
	if (bRagdoll || !Species.Physics)
	{
		return;
	}
	bRagdoll = true;
	bFlying = false;
	Balance.bValid = false;      // limp: no stance to judge (TickBalance stops)
	const FVector Velocity = GetVelocity();
	// Each body starts where the shown mesh has that bone (mid-stride, wings spread...), so read them first.
	TMap<FName, FTransform> Start;
	USkinnedMeshComponent* Src = Shown();
	for (const USkeletalBodySetup* BS : Species.Physics->SkeletalBodySetups)
	{
		if (const int32 Bone = Src->GetBoneIndex(BS->BoneName); Bone != INDEX_NONE)
		{
			Start.Add(BS->BoneName, Src->GetBoneTransform(Bone));
		}
	}
	// The Character's own mesh takes over from the posable Body (or stops playing clips).
	USkeletalMeshComponent* M = GetMesh();
	if (bClips)
	{
		M->Stop();
		bClips = false;
	}
	M->SetSkeletalMeshAsset(Species.Mesh);
	M->SetPhysicsAsset(Species.Physics, true);
	M->SetRelativeTransform(Body->GetRelativeTransform());
	M->SetVisibility(true);
	Body->SetVisibility(false);
	Membrane->SetVisibility(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	M->SetCollisionProfileName(TEXT("Ragdoll"));
	M->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	M->SetAllBodiesSimulatePhysics(true);
	M->SetSimulatePhysics(true);
	// ... and keeps the animal's speed.
	for (const USkeletalBodySetup* BS : Species.Physics->SkeletalBodySetups)
	{
		const FTransform* At = Start.Find(BS->BoneName);
		if (FBodyInstance* BI = M->GetBodyInstance(BS->BoneName); BI && At)
		{
			BI->SetBodyTransform(*At, ETeleportType::TeleportPhysics);
			BI->SetLinearVelocity(Velocity, false);
		}
	}
	M->WakeAllRigidBodies();
	// Standing still, a small shove so it falls over rather than balancing on its legs.
	if (Velocity.Size() < 50.f)
	{
		M->AddImpulse(GetActorRightVector() * 120.f + FVector(0.f, 0.f, 60.f), NAME_None, true);
	}
}

// ---------------------------------------------------------------------------------------------- leg IK

/** Two-bone IK in component space: turns A (hip) and B (knee) so C (ankle) reaches Target, the knee bending the way it
 *  bends now; C keeps its rotation. Local holds the bones' local transforms (B's under A). */
static void BFTwoBone(TArray<FTransform>& CS, const TArray<FTransform>& Local, int32 iA, int32 iB, int32 iC, const FVector& Target)
{
	const FVector A = CS[iA].GetLocation();
	const FVector B = CS[iB].GetLocation();
	const FVector C = CS[iC].GetLocation();
	const float La = (B - A).Size(), Lb = (C - B).Size();
	if (La < 1e-3f || Lb < 1e-3f)
	{
		return;
	}
	const FVector Dir = (Target - A).GetSafeNormal();
	const float D = FMath::Clamp((Target - A).Size(), FMath::Abs(La - Lb) + 0.5f, (La + Lb) * 0.995f);   // never locked straight
	FVector Pole = (B - A) - Dir * FVector::DotProduct(B - A, Dir);
	if (!Pole.Normalize())
	{
		Pole = (C - B).GetSafeNormal();
	}
	const float CosA = FMath::Clamp((La * La + D * D - Lb * Lb) / (2.f * La * D), -1.f, 1.f);
	const FVector NewB = A + Dir * (La * CosA) + Pole * (La * FMath::Sqrt(1.f - CosA * CosA));
	const FVector NewC = A + Dir * D;
	const FQuat FootRot = CS[iC].GetRotation();
	CS[iA].SetRotation((FQuat::FindBetweenVectors(B - A, NewB - A) * CS[iA].GetRotation()).GetNormalized());
	CS[iB] = Local[iB] * CS[iA];
	const FVector CNow = (Local[iC] * CS[iB]).GetLocation();
	CS[iB].SetRotation((FQuat::FindBetweenVectors(CNow - CS[iB].GetLocation(), NewC - CS[iB].GetLocation()) * CS[iB].GetRotation()).GetNormalized());
	CS[iC] = FTransform(FootRot, NewC);
}

void ABFDino::SolveLegIK(TArray<FTransform>& Local, float DeltaSeconds)
{
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	if (bRagdoll || bFlying || FlyBlend > 0.01f || SwimBlend > 0.01f || JumpStage == EJump::Air || Local.Num() != RS.GetNum()
		|| LegBones.Num() != Species.Limbs.Num() || FootBones.Num() != Species.Limbs.Num())
	{
		HipDrop = FMath::FInterpTo(HipDrop, 0.f, DeltaSeconds, 4.f);
		FootLocked.Init(false, LegBones.Num());      // back on its feet it puts them down afresh
		FootRelease.Init(FVector::ZeroVector, LegBones.Num());
		return;
	}
	TArray<FTransform> CS;
	CS.SetNum(Local.Num());
	for (int32 i = 0; i < Local.Num(); ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		CS[i] = P == INDEX_NONE ? Local[i] : Local[i] * CS[P];
	}
	// The pose stands on flat ground through the Body's origin (the ground under the hips), square to the body.
	const FTransform& ToWorld = Body->GetComponentTransform();
	const FVector Up = ToWorld.GetUnitAxis(EAxis::Z);
	const FVector Origin = ToWorld.GetLocation();
	auto Assumed = [&](const FVector& W)
	{
		return Origin.Z - ((W.X - Origin.X) * Up.X + (W.Y - Origin.Y) * Up.Y) / FMath::Max(Up.Z, 0.2f);
	};
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BFLegIK), false, this);
	const float Reach = FMath::Max(Species.Bounds.GetSize().Z, 50.f);
	const bool bMoving = MoveBlend > 0.05f;

	struct FFix
	{
		int32 Leg;
		float Delta;      // how far the ankle has to move up (+) or down (-) to put the foot on the ground
		bool bPlanted;
	};
	TArray<FFix> Fixes;
	LegShift.Init(0.f, LegBones.Num());
	float Need = 0.f;     // how far down the lowest planted foot has to reach, before any hip drop
	auto Trace = [&](const FVector& W, FHitResult& Hit)
	{
		return TraceGround(W + FVector(0.f, 0.f, Reach), W - FVector(0.f, 0.f, Reach), Hit, Params);
	};
	for (int32 l = 0; l < LegBones.Num(); ++l)
	{
		const FLegBones& Leg = LegBones[l];
		FHitResult Hit;
		if (Leg.Upper == INDEX_NONE || Leg.Contact == INDEX_NONE || !Trace(ToWorld.TransformPosition(CS[Leg.Low].GetLocation()), Hit))
		{
			continue;
		}
		const bool bPlanted = !bMoving || FMath::Cos(2.f * PI * (Phase + Species.Limbs[l].PhaseOffset)) <= 0.05f;
		if (bPlanted)
		{
			// The sole on the ground: the foot turned about the ankle so its up at rest points along the ground's normal
			// (at most 35°), and the bones below it follow, before the foot's height is measured.
			const FVector N = ToWorld.InverseTransformVectorNoScale(Hit.ImpactNormal);
			const FQuat Turn = FQuat::FindBetweenNormals(CS[Leg.Low].GetRotation().RotateVector(Leg.FootUp), N);
			FVector Axis;
			float Angle;
			Turn.ToAxisAndAngle(Axis, Angle);
			CS[Leg.Low].SetRotation((FQuat(Axis, FMath::Min(Angle, FMath::DegreesToRadians(35.f))) * CS[Leg.Low].GetRotation()).GetNormalized());
			for (const int32 b : FootBones[l])
			{
				if (b != Leg.Low)
				{
					CS[b] = Local[b] * CS[RS.GetParentIndex(b)];
				}
			}
		}
		// The joint the foot stands on at rest (measuring whichever is lowest would pick a different one once the sole is
		// tilted, and drop the animal into a crouch to reach it). Planted on a ball and toes, the ball: the toes then
		// bend onto the ground (below), so a foot across a crease stands on its ball, not up on its toe tips.
		const bool bOnBall = bPlanted && Leg.Pivot != INDEX_NONE;
		const float Clear = bOnBall ? Leg.PivotClearance : Leg.Clearance;
		const FVector Contact = ToWorld.TransformPosition(CS[bOnBall ? Leg.Pivot : Leg.Contact].GetLocation());
		const float GroundZ = Trace(Contact, Hit) ? Hit.ImpactPoint.Z : Contact.Z - Clear;
		// A planted foot stands on the ground as it does at rest (its clearance square to the ground); a swinging one
		// keeps its height above the ground the pose walks on (the stride's plane).
		const float Delta = bPlanted ? GroundZ + Clear / FMath::Max(Hit.ImpactNormal.Z, 0.5f) - Contact.Z : GroundZ - Assumed(Contact);
		Fixes.Add({l, Delta, bPlanted});
		LegShift[l] = Delta;
		if (bPlanted)
		{
			Need = FMath::Min(Need, Delta + HipDrop);
		}
	}
	// A leg can't stretch past the pose, so when a planted foot's ground is lower than the pose's, the hips come down
	// (AlignToGround) and the other legs bend more.
	// A jump's crouch (and the crouch that soaks up its landing) lowers them further, quickly: the feet stay put.
	HipDrop = FMath::FInterpTo(HipDrop, FMath::Max(Need, -0.3f * HindLegCm) - CrouchCm, DeltaSeconds, JumpStage != EJump::None ? 30.f : 6.f);

	// A planted foot stays where it was put down (seen from above) until it lifts: standing, the body sways, shifts its
	// weight or shakes over it; walking, the body travels over it (and a heavy walker's hips sway out over it). When it
	// lifts, the gap to where the pose has it eases away over about a tenth of a second; a gap past a third of the
	// leg's length puts it down afresh.
	if (FootAnchor.Num() != LegBones.Num() || FootLocked.Num() != LegBones.Num())
	{
		FootAnchor.Init(FVector::ZeroVector, LegBones.Num());
		FootLocked.Init(false, LegBones.Num());
		FootRelease.Init(FVector::ZeroVector, LegBones.Num());
	}
	FootLockGap = 0.f;
	FootSlide = 0.f;
	FootReachMiss = 0.f;
	const float Ease = FMath::Exp(-DeltaSeconds / 0.1f);
	// Standing (it settles to a standstill) every foot holds where it is, whatever the body does over it; walking, a
	// planted foot holds only sideways (a heavy walker's sway), fore and aft it follows the stride as the pose has it.
	const float Stand = 1.f - FMath::Clamp(MoveBlend / 0.05f, 0.f, 1.f);
	if (Stand >= 1.f && !bWasStanding)
	{
		FootLocked.Init(false, LegBones.Num());      // come to a stop: the feet are put down where they are now
	}
	bWasStanding = Stand >= 1.f;
	const FVector Right = ToWorld.GetUnitAxis(EAxis::Y).GetSafeNormal2D();
	for (const FFix& F : Fixes)
	{
		const FLegBones& Leg = LegBones[F.Leg];
		FVector TargetW = ToWorld.TransformPosition(CS[Leg.Low].GetLocation()) + FVector(0.f, 0.f, F.Delta);
		const float LegCm = Species.Limbs.IsValidIndex(F.Leg) ? Species.Limbs[F.Leg].LengthCm : 100.f;
		if (F.bPlanted)
		{
			// Walking it holds sideways (a heavy walker's sway) and fore and aft too (the leg takes up the ankle's arc
			// about the toes), but not starting off or slowing (the pose's stride not yet full, it would fall short of
			// the body) or running (a run's stride is mostly in the air). Standing it holds only while an idle behaviour
			// moves the body over its feet; otherwise the feet go where the pose puts them (a biped steps them under its
			// weight as it settles).
			const float WalkFore = (1.f - FMath::Clamp(LegGaitT * 2.f, 0.f, 1.f)) * FMath::SmoothStep(0.85f, 1.f, MoveBlend);
			const float Fore = FMath::Lerp(WalkFore, IdleHold, Stand);
			const float Lat = FMath::Lerp(1.f, IdleHold, Stand);
			if (Fore <= 0.f && Lat <= 0.f)
			{
				FootAnchor[F.Leg] = TargetW;      // nothing held: the anchor goes with the foot
			}
			FVector Off = FootAnchor[F.Leg] - TargetW;
			Off.Z = 0.f;
			const FVector Side = Right * FVector::DotProduct(Off, Right);
			Off = Side * Lat + (Off - Side) * Fore;
			if (!FootLocked[F.Leg] || (Stand < 1.f && Off.Size() > 0.33f * LegCm))
			{
				FootAnchor[F.Leg] = TargetW;
				FootLocked[F.Leg] = true;
				Off = FVector::ZeroVector;
				// Put down: a print, dust or a splash (ABFEffects), walking or running.
				if (MoveBlend > 0.3f && !bFlying && !bRagdoll && GetCharacterMovement()->IsMovingOnGround())
				{
					if (ABFEffects* E = ABFEffects::Get(GetWorld()))
					{
						E->Footfall(TargetW, GetActorRotation().Yaw, 0.28f * LegCm, float(GetVelocity().Size2D()), Species.MassKg, bQuadPitch ? 0 : (OwnSkinKey() == TEXT("raptor") ? 2 : 3));
					}
				}
			}
			FootLockGap = FMath::Max(FootLockGap, Off.Size());
			FootSlide = FMath::Max(FootSlide, FVector::Dist2D(FootAnchor[F.Leg], TargetW));
			FootRelease[F.Leg] = Off;
			TargetW += Off;
		}
		else
		{
			FootLocked[F.Leg] = false;
			FootRelease[F.Leg] *= Ease;
			TargetW += FootRelease[F.Leg];
		}
		const FVector Target = ToWorld.InverseTransformPosition(TargetW);
		{
			const float LegReach = 0.995f * ((CS[Leg.Mid].GetLocation() - CS[Leg.Upper].GetLocation()).Size() + (CS[Leg.Low].GetLocation() - CS[Leg.Mid].GetLocation()).Size());
			FootReachMiss = FMath::Max(FootReachMiss, (Target - CS[Leg.Upper].GetLocation()).Size() - LegReach);
		}
		BFTwoBone(CS, Local, Leg.Upper, Leg.Mid, Leg.Low, Target);
		const int32 PA = RS.GetParentIndex(Leg.Upper);
		Local[Leg.Upper] = PA == INDEX_NONE ? CS[Leg.Upper] : CS[Leg.Upper].GetRelativeTransform(CS[PA]);
		Local[Leg.Mid] = CS[Leg.Mid].GetRelativeTransform(CS[Leg.Upper]);
		Local[Leg.Low] = CS[Leg.Low].GetRelativeTransform(CS[Leg.Mid]);

		// Planted on its ball, the toes bend at it (at most 40°) to meet the ground under their tips: up a ramp
		// past a crease, down over an edge, and on even ground hardly at all.
		if (F.bPlanted && Leg.Pivot != INDEX_NONE)
		{
			for (const int32 b : FootBones[F.Leg])
			{
				if (b != Leg.Low)
				{
					CS[b] = Local[b] * CS[RS.GetParentIndex(b)];
				}
			}
			const FVector P = CS[Leg.Pivot].GetLocation();
			const FVector T = CS[Leg.Toe].GetLocation();
			const FVector TW = ToWorld.TransformPosition(T);
			FHitResult ToeHit;
			if (Trace(TW, ToeHit))
			{
				const FVector Want = ToWorld.InverseTransformPosition(FVector(TW.X, TW.Y,
					ToeHit.ImpactPoint.Z + Leg.ToeClearance / FMath::Max(ToeHit.ImpactNormal.Z, 0.5f)));
				FVector Axis;
				float Angle;
				FQuat::FindBetweenVectors(T - P, Want - P).ToAxisAndAngle(Axis, Angle);
				const int32 PP = RS.GetParentIndex(Leg.Pivot);
				CS[Leg.Pivot].SetRotation((FQuat(Axis, FMath::Min(Angle, FMath::DegreesToRadians(40.f))) * CS[Leg.Pivot].GetRotation()).GetNormalized());
				Local[Leg.Pivot] = CS[Leg.Pivot].GetRelativeTransform(CS[PP]);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------- the tail on the ground

void ABFDino::SolveTailGround(TArray<FTransform>& Local)
{
	TailDip = TailBend = TailForced = 0.f;
	TailGap = BIG_NUMBER;
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	const TArray<FTransform>& Ref = Poser.GetRefPose();
	if (bRagdoll || bFlying || FlyBlend > 0.01f || SwimBlend > 0.01f || TailKeel.Num() == 0 || Local.Num() != RS.GetNum() || Ref.Num() != Local.Num())
	{
		return;
	}
	TArray<FTransform> CS;
	CS.SetNum(Local.Num());
	for (int32 i = 0; i < Local.Num(); ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		CS[i] = P == INDEX_NONE ? Local[i] : Local[i] * CS[P];
	}
	const FTransform& ToWorld = Body->GetComponentTransform();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BFTail), false, this);
	constexpr float Rest = 1.f;      // cm its skin lies above the ground
	auto SkinW = [&](const FTailKeel& K) { return ToWorld.TransformPosition(CS[K.Bone].TransformPosition(K.Local)); };
	// How far the lowest of its skin is above the ground (square to it; - into it), cm.
	auto Lowest = [&]()
	{
		float Low = BIG_NUMBER;
		for (const FTailKeel& K : TailKeel)
		{
			const FVector W = SkinW(K);
			FHitResult Hit;
			if (TraceGround(W + FVector(0.f, 0.f, TailReachCm), W - FVector(0.f, 0.f, TailReachCm), Hit, Params))
			{
				Low = FMath::Min(Low, FVector::DotProduct(W - Hit.ImpactPoint, Hit.ImpactNormal));
			}
		}
		return Low;
	};
	const float Before = Lowest();
	TailDip = Before < BIG_NUMBER ? FMath::Max(-Before, 0.f) : 0.f;
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("bfnotailfix"));     // (for pictures of it before)
	if (Before >= Rest || bOff)
	{
		TailGap = Before;
		TailGapWorst = FMath::Min(TailGapWorst, TailGap);
		TailDipWorst = FMath::Max(TailDipWorst, TailDip);
		return;
	}

	// The least turn about tail joint m (toward the ground's normal) that puts every skin point tail joint k moves at
	// least Rest above the ground, for the one that needs most, and its axis (world). A point the turn can't lift (the
	// ground up at the joint itself) is left: that's the body in the ground, not the tail.
	auto Need = [&](int32 m, int32 k, FVector& Axis)
	{
		float Most = 0.f;
		const FVector J = ToWorld.TransformPosition(CS[TailChain[m]].GetLocation());
		for (const FTailKeel& K : TailKeel)
		{
			if (K.Joint != k)
			{
				continue;
			}
			const FVector W = SkinW(K);
			FHitResult Hit;
			if (!TraceGround(W + FVector(0.f, 0.f, TailReachCm), W - FVector(0.f, 0.f, Rest + 1.f), Hit, Params))
			{
				continue;
			}
			// Turned by D about Side (unit, square to D): its height over the ground's plane is A cos D + B sin D.
			const FVector N = Hit.ImpactNormal;
			const FVector D = W - J;
			const FVector Side = FVector::CrossProduct(D, N);
			const float A = FVector::DotProduct(D, N), B = Side.Size(), R = D.Size();
			const float C = Rest - FVector::DotProduct(J - Hit.ImpactPoint, N);
			if (A >= C || B < 1e-3f || C >= 0.999f * R)
			{
				continue;
			}
			const float Deg = FMath::RadiansToDegrees(FMath::Atan2(B, A) - FMath::Acos(C / R));
			if (Deg > Most)
			{
				Most = Deg;
				Axis = Side / B;
			}
		}
		return Most;
	};
	TArray<float> Lift, Past;
	Lift.Init(0.f, TailChain.Num());
	Past.Init(0.f, TailChain.Num());
	auto Turn = [&](int32 m, float Deg, const FVector& AxisW, bool bPast)
	{
		const int32 B = TailChain[m];
		const FVector Axis = ToWorld.InverseTransformVectorNoScale(AxisW).GetSafeNormal();
		CS[B].SetRotation((FQuat(Axis, FMath::DegreesToRadians(Deg)) * CS[B].GetRotation()).GetNormalized());
		const int32 P = RS.GetParentIndex(B);
		Local[B] = P == INDEX_NONE ? CS[B] : CS[B].GetRelativeTransform(CS[P]);
		for (const int32 b : TailBelow)
		{
			const int32 Pb = RS.GetParentIndex(b);
			CS[b] = Pb == INDEX_NONE ? Local[b] : Local[b] * CS[Pb];
		}
		Lift[m] += Deg;
		Past[m] += bPast ? Deg : 0.f;
	};
	// How much further up a tail joint bends before its range (limits_deg) runs out.
	auto Room = [&](int32 m)
	{
		const int32 B = TailChain[m];
		const FName Name = RS.GetBoneName(B);
		float Bend, Side;
		BFMeasureJoint(Name, Ref[B].GetRotation(), Local[B].GetRotation(), Bend, Side);
		return FMath::Max(Species.LimitsOf(Name).Bend.Y - Bend, 0.f);
	};

	// Base to tip, each joint bends up just enough to lift what it carries off the ground, so the tail hangs straight
	// down to where it meets the ground and lies along it from there.
	for (int32 k = 0; k < TailChain.Num(); ++k)
	{
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			FVector Axis;
			const float Deg = Need(k, k, Axis);
			if (Deg < 0.05f)
			{
				break;
			}
			// Its own joint first, as far as its range goes; then the joints before it, nearest first, each as far as its
			// range goes: a stiff tail lifts from further up (a Velociraptor's, stiff past its base, from the base).
			const float Own = FMath::Min(Deg, Room(k));
			if (Own > 0.f)
			{
				Turn(k, Own, Axis, false);
			}
			for (int32 m = k - 1; m >= 0 && Own < Deg - 0.05f; --m)
			{
				FVector Up;
				const float More = Need(m, k, Up);
				if (More < 0.05f)
				{
					break;
				}
				const float Give = FMath::Min(More, Room(m));
				if (Give > 0.f)
				{
					Turn(m, Give, Up, false);
				}
			}
			// Every joint back to the base out of range and it still meets the ground: the ground bends it further
			// anyway, over this joint and the two before it, so the tail curves rather than kinks.
			for (int32 m = FMath::Max(k - 2, 0); m <= k; ++m)
			{
				FVector Up;
				const float More = Need(m, k, Up);
				if (More < 0.05f)
				{
					break;
				}
				Turn(m, More / (k - m + 1), Up, true);
			}
		}
	}
	TailGap = Lowest();
	for (int32 m = 0; m < TailChain.Num(); ++m)
	{
		TailBend = FMath::Max(TailBend, Lift[m]);
		TailForced = FMath::Max(TailForced, Past[m]);
	}
	TailDipWorst = FMath::Max(TailDipWorst, TailDip);
	TailGapWorst = FMath::Min(TailGapWorst, TailGap);
	TailBendWorst = FMath::Max(TailBendWorst, TailBend);
	TailForcedWorst = FMath::Max(TailForcedWorst, TailForced);
}

void ABFDino::ResetTailWorst()
{
	TailDipWorst = TailBendWorst = TailForcedWorst = 0.f;
	TailGapWorst = BIG_NUMBER;
}

FString ABFDino::GetTailNote(bool bWorst) const
{
	if (TailKeel.Num() == 0)
	{
		return TEXT("no tail");
	}
	const float Dip = bWorst ? TailDipWorst : TailDip, Gap = bWorst ? TailGapWorst : TailGap;
	const float Bend = bWorst ? TailBendWorst : TailBend, Forced = bWorst ? TailForcedWorst : TailForced;
	FString Note = Dip > 0.f ? FString::Printf(TEXT("tail as posed %.0f cm into the ground"), Dip) : FString(TEXT("tail as posed clear of the ground"));
	Note += Gap < 0.5f * BIG_NUMBER ? FString::Printf(TEXT(", its lowest skin now %.1f cm above it"), Gap) : FString(TEXT(", no ground under it"));
	if (Bend > 0.f)
	{
		Note += FString::Printf(TEXT(", bent up %.1f deg at one joint at most (%.1f of it past the joint's range)"), Bend, Forced);
	}
	return Note;
}

// ---------------------------------------------------------------------------------------------- balance

/** Convex hull of points seen from above, counter-clockwise (Andrew's monotone chain). */
static TArray<FVector2D> BFHull(TArray<FVector2D> P)
{
	P.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	if (P.Num() < 3)
	{
		return P;
	}
	auto Turn = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return FVector2D::CrossProduct(A - O, B - O); };
	TArray<FVector2D> H;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const int32 Start = H.Num();
		for (int32 k = 0; k < P.Num(); ++k)
		{
			const FVector2D& Q = P[Pass == 0 ? k : P.Num() - 1 - k];
			while (H.Num() >= Start + 2 && Turn(H[H.Num() - 2], H.Last(), Q) <= 0.f)
			{
				H.Pop();
			}
			H.Add(Q);
		}
		H.Pop();   // each half's last point starts the other half
	}
	return H;
}

/** How far a point is inside (+) or outside (-) a counter-clockwise hull; a hull of one or two points has no inside. */
static float BFSignedDistance(const TArray<FVector2D>& H, const FVector2D& Q)
{
	if (H.Num() == 0)
	{
		return -BIG_NUMBER;
	}
	float Nearest = (H.Num() == 1) ? FVector2D::Distance(H[0], Q) : BIG_NUMBER;
	bool bInside = H.Num() >= 3;
	for (int32 i = 0; H.Num() >= 2 && i < H.Num(); ++i)
	{
		const FVector2D& A = H[i];
		const FVector2D& B = H[(i + 1) % H.Num()];
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(Q - A, AB) / FMath::Max(AB.SizeSquared(), 1e-6f), 0.f, 1.f);
		Nearest = FMath::Min(Nearest, FVector2D::Distance(A + AB * T, Q));
		bInside &= FVector2D::CrossProduct(AB, Q - A) >= 0.f;
	}
	return bInside ? Nearest : -Nearest;
}

void ABFDino::TickBalance(float DeltaSeconds)
{
	Balance = FBFBalance();
	USkinnedMeshComponent* M = Shown();
	if (bRagdoll || bFlying || FlyBlend > 0.01f || SwimBlend > 0.01f || !M || MassPoints.Num() == 0 || FootBones.Num() != Species.Limbs.Num())
	{
		return;
	}
	FVector Sum = FVector::ZeroVector;
	float Kg = 0.f;
	for (const FMassPoint& P : MassPoints)
	{
		Sum += M->GetBoneTransform(P.Bone).TransformPosition(P.Local) * P.Kg;
		Kg += P.Kg;
	}
	if (Kg <= 0.f)
	{
		return;
	}
	Balance.MassKg = Kg;
	Balance.CoM = Sum / Kg;

	// The feet on the ground: standing, all of them; walking, each one through the back half of its stride
	// (FBFPoser lifts a foot while the cosine of its stride angle is above zero). A foot's contact is the part of
	// it near the ground: its lowest bones (a Pteranodon's folded wing finger rides above its hand).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BFBalance), false, this);
	auto Ground = [&](const FVector2D& At, float FallbackZ)
	{
		FHitResult Hit;
		const FVector Start(At.X, At.Y, Balance.CoM.Z + 100.f);
		const FVector End(At.X, At.Y, Balance.CoM.Z - Species.Bounds.GetSize().Z * 3.f - 500.f);
		return TraceGround(Start, End, Hit, Params) ? Hit.ImpactPoint.Z : FallbackZ;
	};
	Balance.bMoving = MoveBlend > 0.05f && !bClips;
	FootGap.Init(-1.f, Species.Limbs.Num());
	TArray<FVector2D> Contact;
	TArray<float> ContactGap;   // each contact point's height above the ground under it
	for (int32 l = 0; l < Species.Limbs.Num(); ++l)
	{
		const FBFLimb& L = Species.Limbs[l];
		if (Balance.bMoving && FMath::Cos(2.f * PI * (Phase + L.PhaseOffset)) > 0.05f)
		{
			continue;
		}
		// Each joint's height above the ground under it (on a slope the joint lowest in the world is the downhill one,
		// not the one nearest the ground).
		TArray<FVector> W;
		TArray<float> Gaps;
		float Gap = BIG_NUMBER;
		for (const int32 b : FootBones[l])
		{
			W.Add(M->GetBoneTransform(b).GetLocation());
			Gaps.Add(W.Last().Z - Ground(FVector2D(W.Last()), W.Last().Z));
			Gap = FMath::Min(Gap, Gaps.Last());
		}
		if (FootGap.IsValidIndex(l))
		{
			FootGap[l] = Gap;
		}
		// A foot held off the ground carries nothing: it only counts when a joint of it is near the ground under it.
		if (Gap > FMath::Max(10.f, 0.1f * L.LengthCm))
		{
			continue;
		}
		bool bAny = false;
		for (int32 k = 0; k < W.Num(); ++k)
		{
			if (Gaps[k] < Gap + 0.12f * L.LengthCm)
			{
				Contact.Add(FVector2D(W[k]));
				ContactGap.Add(Gaps[k]);
				bAny = true;
			}
		}
		Balance.FeetDown += bAny ? 1 : 0;
	}
	if (Contact.Num() == 0)
	{
		return;
	}
	const TArray<FVector2D> Hull = BFHull(Contact);
	Balance.MarginCm = BFSignedDistance(Hull, FVector2D(Balance.CoM));
	Balance.bStable = Balance.MarginCm > 0.02f * BodyLengthCm();

	// A standing biped steps its feet under its weight: it swings its legs back (or forward) until the centre of mass is
	// over the middle of its feet, as far as its hips allow (one on its front legs alone the same, by its shoulders).
	// When they don't allow enough, it tips over (below).
	const bool bSteps = !Species.bQuadruped || Species.bHindHang;
	if (Upright > 0.01f && bSteps && Hull.Num() > 0)
	{
		FVector2D Mid = FVector2D::ZeroVector;
		for (const FVector2D& P : Hull)
		{
			Mid += P;
		}
		Mid /= Hull.Num();
		const float Ahead = FVector2D::DotProduct(FVector2D(Balance.CoM) - Mid, FVector2D(GetActorForwardVector()).GetSafeNormal());
		const float Want = StepBack + FMath::RadiansToDegrees(FMath::Atan2(-Ahead, FMath::Max(HindLegCm, 1.f)));
		// The legs swing back by BodyPitch + StepBack in all (FBFPoseInput::LegLean), inside the hips' range.
		StepBack = FMath::Clamp(FMath::FInterpTo(StepBack, Want, DeltaSeconds, 3.f), LegLeanLo - BodyPitch, LegLeanHi - BodyPitch);
	}
	else
	{
		StepBack = 0.f;
	}

	const float UnderZ = Ground(FVector2D(Balance.CoM), Balance.CoM.Z - PelvisZ);
	Balance.HeightCm = Balance.CoM.Z - UnderZ;
	Balance.FootGapMinCm = FMath::Min(ContactGap);
	Balance.FootGapMaxCm = FMath::Max(ContactGap);
	Balance.bValid = true;

	// Standing still with its weight outside its feet, and stepping (if it steps) as far as it goes: it tips over, a
	// second after it stops or appears, as the splicing editor's balance readout says (compose.py readout).
	const bool bStepMaxed = !bSteps || StepBack <= LegLeanLo - BodyPitch + 0.5f || StepBack >= LegLeanHi - BodyPitch - 0.5f;
	const bool bOutside = Balance.MarginCm < -TIP_SHARE * BodyLengthCm();
	TipClock = !Balance.bMoving && Upright > 0.9f && !bInWater && bOutside && bStepMaxed ? TipClock + DeltaSeconds : 0.f;
	if (TipClock > 1.f && AliveTime > 1.5f && !bProbe)      // (a self-test's copy only reports it: ProbeStands)
	{
		// Along the body from the hips' ground point: where its weight and its feet are (to check the climb check's model)
		const FVector F = GetActorForwardVector().GetSafeNormal2D();
		const FVector O = GetActorLocation();
		float Lo = BIG_NUMBER, Hi = -BIG_NUMBER;
		for (const FVector2D& P : Hull)
		{
			const float X = FVector::DotProduct(FVector(P, 0.f) - FVector(O.X, O.Y, 0.f), F);
			Lo = FMath::Min(Lo, X);
			Hi = FMath::Max(Hi, X);
		}
		// Each foot's joints: along the body, and height above the ground under each (cm)
		FString Joints;
		for (int32 l = 0; l < Species.Limbs.Num() && FootBones.IsValidIndex(l); ++l)
		{
			Joints += FString::Printf(TEXT(" %s:"), *Species.Limbs[l].Low.ToString());
			for (const int32 b : FootBones[l])
			{
				const FVector W = M->GetBoneTransform(b).GetLocation();
				Joints += FString::Printf(TEXT(" %.0f/%.0f"), FVector::DotProduct(W - O, F), W.Z - Ground(FVector2D(W), W.Z));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] %s tips over: its centre of mass is %.0f cm outside its feet, past what %s "
			"(along it: weight %.0f, feet %.0f..%.0f cm; %d feet down; pitch %.1f, leg lean %.1f, step %.1f; joints along/up:%s)"),
			*Species.Key, -Balance.MarginCm, bSteps ? TEXT("stepping reaches") : TEXT("four feet can hold"),
			FVector::DotProduct(Balance.CoM - O, F), Lo, Hi, Balance.FeetDown, BodyPitch, LegLean, StepBack, *Joints);
		StartRagdoll();
		return;
	}
	if (!bShowBalance || bProbe)
	{
		return;
	}

	// Drawn over the body (the centre of mass is inside it): green inside the feet, amber only just, red outside.
	const FColor Col = Balance.bStable ? FColor(70, 210, 100) : Balance.MarginCm > 0.f ? FColor(245, 185, 40) : FColor(235, 70, 55);
	const float Size = BodyLengthCm();
	const float Thick = FMath::Max(1.5f, 0.004f * Size);
	UWorld* World = GetWorld();
	const FVector Under(Balance.CoM.X, Balance.CoM.Y, UnderZ + 1.f);
	DrawDebugSphere(World, Balance.CoM, FMath::Max(3.f, 0.025f * Size), 16, Col, false, -1.f, SDPG_Foreground, Thick);
	DrawDebugLine(World, Balance.CoM, Under, Col, false, -1.f, SDPG_Foreground, Thick * 0.6f);
	const float Cross = FMath::Max(4.f, 0.03f * Size);
	DrawDebugLine(World, Under - FVector(Cross, 0.f, 0.f), Under + FVector(Cross, 0.f, 0.f), Col, false, -1.f, SDPG_Foreground, Thick);
	DrawDebugLine(World, Under - FVector(0.f, Cross, 0.f), Under + FVector(0.f, Cross, 0.f), Col, false, -1.f, SDPG_Foreground, Thick);
	TArray<FVector> Outline;
	for (const FVector2D& P : Hull)
	{
		Outline.Add(FVector(P.X, P.Y, Ground(P, UnderZ) + 1.f));
	}
	for (int32 i = 0; i < Outline.Num(); ++i)
	{
		if (Outline.Num() > 1 && (i + 1 < Outline.Num() || Outline.Num() > 2))
		{
			DrawDebugLine(World, Outline[i], Outline[(i + 1) % Outline.Num()], FColor(80, 160, 255), false, -1.f, SDPG_Foreground, Thick);
		}
		DrawDebugPoint(World, Outline[i], FMath::Clamp(Thick * 1.5f, 4.f, 8.f), FColor(80, 160, 255), false, -1.f, SDPG_Foreground);
	}
}

FString ABFDino::GetLegIKNote() const
{
	FString Note = FString::Printf(TEXT("hips %+.0f cm, feet moved"), HipDrop);
	for (int32 l = 0; l < LegShift.Num() && l < Species.Limbs.Num(); ++l)
	{
		Note += FString::Printf(TEXT(" %s %+.0f (now %.0f up)"), *Species.Limbs[l].Low.ToString(), LegShift[l], FootGap.IsValidIndex(l) ? FootGap[l] : -1.f);
	}
	return Note;
}

FString ABFDino::GetBalanceLine() const
{
	if (!Balance.bValid)
	{
		return FString();
	}
	auto Length = [](float Cm)
	{
		Cm = FMath::Abs(Cm);
		return Cm < 100.f ? FString::Printf(TEXT("%.0f cm"), Cm) : FString::Printf(TEXT("%.2f m"), Cm / 100.f);
	};
	const FString Mass = Balance.MassKg >= 1000.f ? FString::Printf(TEXT("%.1f t"), Balance.MassKg / 1000.f) : FString::Printf(TEXT("%.0f kg"), Balance.MassKg);
	FString Where;
	if (Balance.MarginCm >= 0.f)
	{
		Where = FString::Printf(TEXT("%s inside the feet on the ground: %s"), *Length(Balance.MarginCm),
			Balance.bStable ? TEXT("stable") : TEXT("only just balanced"));
	}
	else
	{
		// (inside the tipping rule's slack it stays up, TickBalance)
		Where = FString::Printf(TEXT("%s outside the feet on the ground: %s"), *Length(Balance.MarginCm),
			Balance.bMoving ? TEXT("kept up by stepping") : -Balance.MarginCm <= TIP_SHARE * BodyLengthCm() ? TEXT("only just held") : TEXT("tips over"));
	}
	return FString::Printf(TEXT("mass %s  |  centre of mass %s up  |  %d %s down  |  %s"), *Mass, *Length(Balance.HeightCm),
		Balance.FeetDown, Balance.FeetDown == 1 ? TEXT("foot") : TEXT("feet"), *Where);
}

FString ABFDino::RagdollReport(int32& Outside) const
{
	// Every bone with a body, measured against its parent like the rig: bend = sign * Pitch, side = sign * Yaw
	// from the reference pose. Anything more than 5 degrees past limits_deg counts.
	Outside = 0;
	const USkeletalMeshComponent* M = GetMesh();
	if (!bRagdoll || !Species.Physics)
	{
		return TEXT("not ragdolling");
	}
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	FString Worst;
	float WorstBy = 0.f;
	int32 Checked = 0;
	for (const USkeletalBodySetup* BS : Species.Physics->SkeletalBodySetups)
	{
		const int32 Bone = RS.FindBoneIndex(BS->BoneName);
		const int32 Parent = RS.GetParentIndex(Bone);
		if (Bone == INDEX_NONE || Parent == INDEX_NONE || !Species.Limits.Contains(BS->BoneName))
		{
			continue;
		}
		const FTransform Local = M->GetBoneTransform(Bone).GetRelativeTransform(M->GetBoneTransform(Parent));
		float Bend, Side;
		BFMeasureJoint(BS->BoneName, Poser.GetRefPose()[Bone].GetRotation(), Local.GetRotation(), Bend, Side);
		const FBFLimits& L = Species.LimitsOf(BS->BoneName);
		auto Check = [&](float Angle, const FVector2D& R, const TCHAR* Axis)
		{
			const float By = FMath::Max(R.X - Angle, Angle - R.Y);
			if (By > 5.f)
			{
				++Outside;
			}
			if (By > WorstBy)
			{
				WorstBy = By;
				Worst = FString::Printf(TEXT("%s %s %.0f (limit %.0f..%.0f)"), *BS->BoneName.ToString(), Axis, Angle, R.X, R.Y);
			}
		};
		// Bend is read in the plane the bone points along, which collapses as the bone turns 90 deg to the side (a
		// raptor wrist folds up to 90): past 70 deg of side the bend reading is noise, so only the side counts there.
		if (FMath::Abs(Side) < 70.f)
		{
			Check(Bend, L.Bend, TEXT("bend"));
		}
		Check(Side, L.Side, TEXT("side"));
		++Checked;
	}
	const FVector Pelvis = M->GetBoneLocation(TEXT("pelvis"));
	return FString::Printf(TEXT("%d joints checked, %d axes more than 5 deg past their limits%s%s; pelvis %.0f cm above the spawn, moving %.0f cm/s"),
		Checked, Outside, WorstBy > 0.f ? TEXT("; furthest: ") : TEXT(""), *Worst,
		Pelvis.Z - GetActorLocation().Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), M->GetPhysicsLinearVelocity(TEXT("pelvis")).Size());
}

// ---------------------------------------------------------------------------------------------- swimming

void ABFDino::TickSwim(float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	float Surface = 0.f;
	const FVector Loc = GetActorLocation();
	bInWater = !bFlying && ABFGameMode::WaterAt(Loc, Surface);
	const float HH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	bool bFloats = false;
	float FloatZ = 0.f;
	if (bInWater && (Species.Swim == EBFSwim::Swims || Species.Swim == EBFSwim::HeadUnder))
	{
		// Floating, the centre of mass sits as deep as the swim check worked out; standing, the capsule sits on the
		// bottom. It floats off its feet where the water is deep enough to lift it there, and wades where it isn't.
		FloatZ = Surface - Species.SwimComDepthCm - RestCoM.Z;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BFSwim), false, this);
		const bool bFloor = GetWorld()->LineTraceSingleByChannel(Hit, Loc, Loc - FVector(0.f, 0.f, 100000.f), ECC_Visibility, Params);
		const float StandZ = bFloor ? Hit.ImpactPoint.Z + HH : -1e9f;
		bFloats = bSwimming ? StandZ < FloatZ + 10.f : StandZ < FloatZ - 10.f;
	}
	if (bFloats && !bSwimming)
	{
		bSwimming = true;
		Move->SetMovementMode(MOVE_Flying);
		Move->MaxFlySpeed = Species.SwimSpeedCm;
		Move->BrakingDecelerationFlying = Species.SwimSpeedCm * 2.f;
	}
	else if (!bFloats && bSwimming)
	{
		bSwimming = false;
		Move->SetMovementMode(MOVE_Walking);
		Move->BrakingDecelerationFlying = 0.f;
		Move->MaxFlySpeed = 3000.f;
	}
	SwimClock += DeltaSeconds;
	if (bSwimming)
	{
		// Holding Shift it strokes harder: 2.5 times the thrust, 1.6 times the speed against the water's drag.
		Move->MaxFlySpeed = Species.SwimSpeedCm * (1.f + 0.6f * SprintEase());
		// Held at its floating depth, bobbing a little with each stroke.
		const float Bob = 0.015f * HH * FMath::Sin(2.f * PI * SwimClock * 0.6f);
		Move->Velocity.Z = FMath::Clamp((FloatZ + Bob - Loc.Z) * 2.f, -250.f, 250.f);
	}
	else if (!bFlying && !bRagdoll)
	{
		// Wading (the water drags at the legs) and climbing (GroundSpeedNow).
		Move->MaxWalkSpeed = GroundSpeedNow();
	}
	SwimBlend = FMath::FInterpTo(SwimBlend, bSwimming ? 1.f : 0.f, DeltaSeconds, 2.5f);
	// A river's current carries it: all of it while it floats; wading, as much as the water has hold of its legs.
	if (bInWater && !bRagdoll)
	{
		const FVector Current = ABFGameMode::CurrentAt(Loc);
		if (!Current.IsNearlyZero(1.f))
		{
			const float Hold = bSwimming ? 1.f : 0.35f * FMath::Clamp((Surface - (Loc.Z - HH)) / FMath::Max(2.f * HH, 1.f), 0.f, 1.f);
			AddActorWorldOffset(Current * Hold * DeltaSeconds, true);
		}
	}
	bHeadUnder = false;
	if (bInWater)
	{
		const USkinnedMeshComponent* M = Shown();
		const FName Snout = M && M->GetBoneIndex(TEXT("head_end")) != INDEX_NONE ? FName(TEXT("head_end")) : FName(TEXT("head"));
		bHeadUnder = M && M->GetBoneIndex(Snout) != INDEX_NONE && M->GetBoneLocation(Snout).Z < Surface;
	}
}

void ABFDino::TickUnderwaterView()
{
	if (!IsLocallyControlled() || !Camera)
	{
		return;
	}
	float Surface = 0.f;
	const FVector Cam = Camera->GetComponentLocation();
	const int8 Under = ABFGameMode::WaterAt(Cam, Surface) && Cam.Z < Surface ? 1 : 0;
	if (Under == CameraUnder)
	{
		return;
	}
	CameraUnder = Under;
	// The camera's own look: a blue-green cast, a little less colour and contrast, darker toward the edges.
	FPostProcessSettings& P = Camera->PostProcessSettings;
	P.bOverride_SceneColorTint = true;
	P.SceneColorTint = FLinearColor(0.55f, 0.86f, 0.92f);
	P.bOverride_ColorSaturation = true;
	P.ColorSaturation = FVector4(0.8f, 0.8f, 0.8f, 1.f);
	P.bOverride_ColorContrast = true;
	P.ColorContrast = FVector4(0.9f, 0.9f, 0.9f, 1.f);
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = 0.75f;
	P.bOverride_AutoExposureBias = true;       // less light gets down through the water
	P.AutoExposureBias = -0.5f;
	Camera->PostProcessBlendWeight = Under ? 1.f : 0.f;
	// The water's murk: the world's fog (ABFGameMode).
	if (ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>())
	{
		GM->SetUnderwaterView(Under == 1);
	}
}

// ---------------------------------------------------------------------------------------------- flight

void ABFDino::TakeOff()
{
	if (!Species.CanFly() || bFlying || bRagdoll)     // legs that gave way can't take off
	{
		return;
	}
	bFlying = true;
	FlightTime = 0.f;
	JumpZ = 0.f;
	bPerching = false;
	if (bOnPerch)
	{
		// Off a perch: out, away from the trunk or the rock face, before anything else.
		bOnPerch = false;
		GetCharacterMovement()->bCanWalkOffLedges = true;
		SetActorRotation(FRotator(0.f, PerchOut.Rotation().Yaw, 0.f));
	}
	LastYaw = GetActorRotation().Yaw;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	switch (Species.Flight)
	{
	case EBFFlight::Grounded:
		// The wings can't lift it: a hop on its legs, then it flaps on the spot until it gives up (TickFlight).
		FlySpeed = 0.f;
		JumpZ = 150.f;
		LaunchCharacter(FVector(0.f, 0.f, JumpZ), false, false);
		return;
	case EBFFlight::Glides:
		// No wingbeat strong enough to climb: it leaps with the speed it has (a running jump, or off a height).
		FlySpeed = FMath::Max(GetVelocity().Size2D(), 100.f);
		JumpZ = GetCharacterMovement()->IsMovingOnGround() ? 350.f : 0.f;
		Aim = FRotator(-5.f, GetActorRotation().Yaw, 0.f);
		break;
	default:
		FlySpeed = 900.f;
		Aim = FRotator(20.f, GetActorRotation().Yaw, 0.f);
		break;
	}
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	GetCharacterMovement()->Velocity = Aim.Vector() * FlySpeed + FVector(0.f, 0.f, Species.Flight == EBFFlight::Glides ? JumpZ : 400.f);
}

void ABFDino::StartJump()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (JumpStage != EJump::None || bRagdoll || bFlying || bInWater || !Move->IsMovingOnGround() || Species.Legs != EBFLegs::Walks)
	{
		return;
	}
	JumpStage = EJump::Crouch;
	JumpClock = 0.f;
	bJumpLeft = false;
	JumpRiseCm = 0.f;
	JumpStartZ = GetActorLocation().Z;
}

void ABFDino::TickJump(float DeltaSeconds)
{
	if (JumpStage == EJump::None)
	{
		CrouchCm = FMath::FInterpTo(CrouchCm, 0.f, DeltaSeconds, 10.f);
		TuckU = FMath::FInterpTo(TuckU, 0.f, DeltaSeconds, 8.f);
		return;
	}
	if (bRagdoll || bFlying || bInWater)
	{
		JumpStage = EJump::None;
		return;
	}
	UCharacterMovementComponent* Move = GetCharacterMovement();
	JumpClock += DeltaSeconds;
	const float Depth = Species.JumpCrouchCm;
	const float CrouchT = FMath::Clamp(0.15f + 0.002f * Depth, 0.15f, 0.45f);   // a bigger animal takes longer to crouch
	const float Gravity = FMath::Max(-GetWorld()->GetGravityZ(), 1.f);
	switch (JumpStage)
	{
	case EJump::Crouch:
		// Down into the crouch, the leg IK keeping the feet where they are, then the push: straight up at the speed that
		// carries it JumpCm high, keeping whatever speed it had along the ground (a running jump goes on forward).
		CrouchCm = Depth * FMath::SmoothStep(0.f, 1.f, JumpClock / CrouchT);
		if (JumpClock >= CrouchT)
		{
			JumpClock = 0.f;
			if (Species.JumpCm > 0.f)
			{
				LaunchCharacter(FVector(0.f, 0.f, FMath::Sqrt(2.f * Gravity * Species.JumpCm)), false, true);
				JumpStage = EJump::Air;
				bJumpLeft = true;
				JumpStartZ = GetActorLocation().Z;
			}
			else
			{
				JumpStage = EJump::Land;      // too heavy: the heave doesn't lift it, and it straightens back up
				LandCrouchCm = Depth;
				JumpClock = 0.08f;
			}
		}
		break;
	case EJump::Air:
		// The legs straighten as it leaves the ground, then draw up under it.
		CrouchCm = FMath::FInterpTo(CrouchCm, 0.f, DeltaSeconds, 20.f);
		TuckU = FMath::FInterpTo(TuckU, 1.f, DeltaSeconds, 6.f);
		JumpRiseCm = FMath::Max(JumpRiseCm, GetActorLocation().Z - JumpStartZ);
		FallSpeed = FMath::Max(0.f, -GetVelocity().Z);
		// Down (only coming down: a long frame just after the push mustn't count as landing before it has risen).
		if (JumpClock > 0.1f && GetVelocity().Z <= 1.f && Move->IsMovingOnGround())
		{
			// Into a crouch as deep as the fall was fast (its own jump's speed: its full crouch), and back up.
			const float JumpSpeed = FMath::Sqrt(2.f * Gravity * FMath::Max(Species.JumpCm, 1.f));
			LandCrouchCm = FMath::Clamp(Depth * FallSpeed / JumpSpeed, 0.3f * Depth, Depth);
			JumpStage = EJump::Land;
			JumpClock = 0.f;
		}
		break;
	case EJump::Land:
	{
		TuckU = FMath::FInterpTo(TuckU, 0.f, DeltaSeconds, 14.f);
		const float Down = 0.08f, Up = 1.5f * CrouchT;
		CrouchCm = JumpClock < Down ? LandCrouchCm * FMath::SmoothStep(0.f, 1.f, JumpClock / Down)
			: LandCrouchCm * (1.f - FMath::SmoothStep(0.f, 1.f, (JumpClock - Down) / Up));
		if (JumpClock > Down + Up)
		{
			JumpStage = EJump::None;
		}
		break;
	}
	default:
		break;
	}
}

void ABFDino::PerchOn(const FVector& Stand, const FVector& Out)
{
	if (!bFlying)
	{
		return;
	}
	if (!bPerching || !PerchStand.Equals(Stand, 10.f))
	{
		bPerching = true;
		PerchStand = Stand;
		PerchOut = Out.GetSafeNormal2D().IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Out.GetSafeNormal2D();
		PerchStage = 0;
		PerchClock = 0.f;
	}
}

void ABFDino::SetOnPerch(const FVector& Out)
{
	bOnPerch = true;
	PerchOut = Out.GetSafeNormal2D().IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Out.GetSafeNormal2D();
	GetCharacterMovement()->bCanWalkOffLedges = false;
}

void ABFDino::TickPerch(float DeltaSeconds)
{
	// Two legs: to a point 18 m out from the perch and 5 m above it, fast; then in to it, slowing, the wings beating
	// harder as it slows (a pterosaur's landing: a stall onto its feet), the body level, facing in.
	PerchClock += DeltaSeconds;
	const FVector Me = GetActorLocation();
	const FVector Approach = PerchStand + PerchOut * 1800.f + FVector(0.f, 0.f, 500.f);
	if (PerchStage == 0 && FVector::Dist(Me, Approach) < 600.f)
	{
		PerchStage = 1;
	}
	const FVector Target = PerchStage == 0 ? Approach : PerchStand;
	const float Dist = FVector::Dist(Me, Target);
	const float Want = PerchStage == 0 ? FMath::Clamp(Dist * 0.8f, 700.f, 1500.f) : FMath::Clamp(Dist * 1.2f, 120.f, 700.f);
	FlySpeed = FMath::FInterpTo(FlySpeed, Want, DeltaSeconds, 2.5f);
	const FVector Dir = (Target - Me).GetSafeNormal();
	GetCharacterMovement()->Velocity = Dir * FlySpeed * GamePace;
	FlapAmount = FMath::FInterpTo(FlapAmount, PerchStage == 1 ? FMath::Lerp(1.f, 0.5f, FMath::Clamp(Dist / 1800.f, 0.f, 1.f)) : 0.5f, DeltaSeconds, 3.f);
	const FVector Flat = Dir.GetSafeNormal2D();
	const float Yaw = Flat.IsNearlyZero() ? GetActorRotation().Yaw : Flat.Rotation().Yaw;
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	LastYaw = Yaw;
	FlightPitch = FMath::FInterpTo(FlightPitch, PerchStage == 1 ? 0.f : FMath::RadiansToDegrees(FMath::Atan2(GetVelocity().Z, FMath::Max(GetVelocity().Size2D(), 1.f))), DeltaSeconds, 4.f);
	Bank = FMath::FInterpTo(Bank, 0.f, DeltaSeconds, 3.f);
	const bool bThere = PerchStage == 1 && Dist < 70.f;
	const bool bLate = PerchClock > 30.f;          // held up (a crown in the way): put on it
	if (bThere || bLate)
	{
		PerchSnaps += bLate && !bThere ? 1 : 0;
		PerchLandings += bThere ? 1 : 0;
		SetActorLocation(PerchStand, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		bPerching = false;
		Land();
		SetActorRotation(FRotator(0.f, (-PerchOut).Rotation().Yaw, 0.f));
		SetOnPerch(PerchOut);
	}
}

void ABFDino::Land()
{
	bFlying = false;
	bPerching = false;
	FlySpeed = 0.f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
}

void ABFDino::SetFlightInput(float InThrust, bool bInFlap, const FRotator& InAim)
{
	Thrust = FMath::Clamp(InThrust, -1.f, 1.f);
	bFlap = bInFlap;
	Aim = InAim;
}

void ABFDino::TickFlight(float DeltaSeconds)
{
	FlyBlend = FMath::FInterpTo(FlyBlend, bFlying ? 1.f : 0.f, DeltaSeconds, bFlying ? 4.f : 3.f);
	if (!bFlying)
	{
		FlapAmount = FMath::FInterpTo(FlapAmount, 0.f, DeltaSeconds, 4.f);
		return;
	}
	FlightTime += DeltaSeconds;

	if (Species.Flight == EBFFlight::Grounded)
	{
		// A failed take-off: it flaps as hard as it can, but the lift is a sliver of its weight, so it falls (off a
		// cliff, all the way down) and stays on the ground. It labours: each of the first downstrokes heaves the body a
		// little off its feet, then it tires, the beats slower and shallower, until after four seconds it gives up and
		// folds its wings.
		const float Tired = FMath::Clamp((FlightTime - 0.8f) / 3.f, 0.f, 1.f);
		FlapAmount = FMath::FInterpTo(FlapAmount, FMath::Lerp(1.f, 0.3f, Tired), DeltaSeconds, 3.f);
		const float U = FMath::Frac(FlapPhase);
		if (LastFlapU < 0.75f && U >= 0.75f && FlightTime < 2.5f && GetCharacterMovement()->IsMovingOnGround())
		{
			LaunchCharacter(FVector(0.f, 0.f, 90.f * (1.f - Tired)), false, true);
		}
		LastFlapU = U;
		if (FlightTime > 4.f && GetCharacterMovement()->IsMovingOnGround())
		{
			Land();
		}
		return;
	}

	if (bPerching)
	{
		TickPerch(DeltaSeconds);
		return;
	}

	// Arcade gliding flight: wingbeats push along the flight path, climbing trades speed for height
	// and diving gains it back, drag caps the speed, and below stall speed the animal sinks. The flight check
	// scales it: stall speed grows with the square root of wing loading (550 cm/s at the Pteranodon's 182 N/m²,
	// which this was tuned on), and a glider's few flight muscles make a wingbeat too weak to climb on.
	const float StallCm = Species.WingLoading > 0.f ? 550.f * FMath::Sqrt(Species.WingLoading / 182.4f) : 550.f;
	const float Power = Species.Flight == EBFFlight::Glides ? FMath::Square(FMath::Min(Species.FlightMuscleShare / 0.15f, 1.f)) : 1.f;
	// Holding Shift (TickSprint): a flier beats harder, its thrust growing with the flight muscle it has (20% of its
	// weight, the Pteranodon's, gives 2.6 times the thrust and so 1.6 times the speed against drag). A glider has none
	// to spare: it tips forward instead, trading height for speed.
	const float Hard = FlapHard();
	const float HardThrust = 1.f + Hard * 1.2f * FMath::Min(Species.FlightMuscleShare / 0.15f, 2.f);
	const float Dive = Species.Flight == EBFFlight::Glides ? 12.f * SprintEase() : 0.f;
	const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch) - Dive, -60.f, 45.f);
	const FRotator Dir(Pitch, Aim.Yaw, 0.f);
	const bool bStall = FlySpeed < StallCm;
	const float FlapTarget = FMath::Clamp(FMath::Max(Thrust, 0.f) + (bFlap ? 1.f : 0.f) + (FlightTime < 1.2f ? 1.f : 0.f) + (bStall ? 0.6f : 0.f), 0.08f, 1.f);
	// A glider's few flight muscles make shallow wingbeats (they look weak as well as push weakly).
	const float FlapCap = Species.Flight == EBFFlight::Glides ? 0.35f + 0.65f * Power : 1.f;
	FlapAmount = FMath::FInterpTo(FlapAmount, FMath::Min(FlapTarget, FlapCap), DeltaSeconds, 3.f);
	FlySpeed += (FlapAmount * 900.f * Power * HardThrust - (Thrust < 0.f ? 700.f : 0.f)) * DeltaSeconds;
	FlySpeed -= 980.f * FMath::Sin(FMath::DegreesToRadians(Pitch)) * DeltaSeconds;
	FlySpeed -= 0.00045f * FlySpeed * FlySpeed * DeltaSeconds;
	FlySpeed = FMath::Clamp(FlySpeed, 0.f, 2800.f);
	const float Sink = bStall ? (StallCm - FlySpeed) * 1.2f : 0.f;
	const float Lift = bFlap ? 250.f * Power : 0.f;
	JumpZ = FMath::Max(JumpZ - 980.f * DeltaSeconds, 0.f);
	// At the game pace, as walking is (its own flight's numbers stay its own).
	GetCharacterMovement()->Velocity = (Dir.Vector() * FlySpeed + FVector(0.f, 0.f, Lift - Sink + JumpZ)) * GamePace;

	// Face the flight path; bank into turns.
	const float YawRate = FRotator::NormalizeAxis(Dir.Yaw - LastYaw) / FMath::Max(DeltaSeconds, 1e-3f);
	LastYaw = Dir.Yaw;
	SetActorRotation(FRotator(0.f, Dir.Yaw, 0.f));
	FlightPitch = FMath::FInterpTo(FlightPitch, FMath::RadiansToDegrees(FMath::Atan2(GetVelocity().Z, FMath::Max(GetVelocity().Size2D(), 1.f))), DeltaSeconds, 4.f);
	Bank = FMath::FInterpTo(Bank, FMath::Clamp(YawRate * 0.35f, -45.f, 45.f), DeltaSeconds, 3.f);

	// Touch down when skimming the ground on the way down (a glider's short leap can end sooner).
	if (FlightTime > (Species.Flight == EBFFlight::Glides ? 0.4f : 1.5f) && GetVelocity().Z <= 0.f)
	{
		FHitResult Hit;
		const float HH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BFLand), false, this);
		const FVector Start = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, HH + 30.f), ECC_Visibility, Params))
		{
			Land();
		}
	}
}

void ABFDino::UpdateMembrane()
{
	// Each wing's membrane: from the leading edge (shoulder, elbow, wrist, the four wing-finger joints,
	// the tip) back to the trailing edge (hip, knee, ankle, then a curve out to the wing tip).
	if (!bClips)
	{
		Body->RefreshBoneTransforms();
	}
	USkinnedMeshComponent* Src = Shown();
	const FTransform ToLocal = Membrane->GetComponentTransform().Inverse();
	auto P = [&](const TCHAR* Bone, const TCHAR* Side) -> FVector
	{
		return ToLocal.TransformPosition(Src->GetBoneLocation(FName(*FString::Printf(TEXT("%s_%s"), Bone, Side)), EBoneSpaces::WorldSpace));
	};
	// The wing's own bones (the back edge runs to the legs, whatever pair the wings are).
	auto W = [&](const TCHAR* Bone, const TCHAR* Side) -> FVector
	{
		return P(*(FString(Bone) + Species.WingPair), Side);
	};
	auto Resample = [](const TArray<FVector>& Line, int32 Count)
	{
		TArray<float> Acc = { 0.f };
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Acc.Add(Acc.Last() + (Line[i] - Line[i - 1]).Size());
		}
		TArray<FVector> Out;
		for (int32 k = 0; k < Count; ++k)
		{
			const float D = Acc.Last() * k / (Count - 1);
			int32 i = 1;
			while (i < Line.Num() - 1 && Acc[i] < D)
			{
				++i;
			}
			const float Seg = FMath::Max(Acc[i] - Acc[i - 1], 1e-3f);
			Out.Add(FMath::Lerp(Line[i - 1], Line[i], FMath::Clamp((D - Acc[i - 1]) / Seg, 0.f, 1.f)));
		}
		return Out;
	};

	constexpr int32 NU = 28, NV = 7;
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
	{
		const TArray<FVector> Lead = Resample({ W(TEXT("upperarm"), Side), W(TEXT("lowerarm"), Side), W(TEXT("hand"), Side),
			W(TEXT("wingfinger_01"), Side), W(TEXT("wingfinger_02"), Side), W(TEXT("wingfinger_03"), Side),
			W(TEXT("wingfinger_04"), Side), W(TEXT("wing_end"), Side) }, NU);
		const FVector Tip = W(TEXT("wing_end"), Side), Ankle = P(TEXT("foot"), Side);
		// The back edge leaves the body on the skin over the hip (a splice's body is wider than the wing's donor),
		// carried round with the hips.
		FVector HipOff = Species.MembraneHipOffset;
		HipOff.Y *= Side[0] == TEXT('r') ? -1.f : 1.f;
		const FQuat Pelvis = Src->GetBoneQuaternion(TEXT("pelvis"), EBoneSpaces::WorldSpace);
		const FVector Hip = P(TEXT("thigh"), Side) + ToLocal.TransformVector(Pelvis * (PelvisRest.Inverse() * HipOff));
		TArray<FVector> TrailLine = { Hip, P(TEXT("calf"), Side), Ankle };
		const FVector Mid = Lead[NU * 2 / 3];
		for (int32 k = 1; k <= 8; ++k)   // the free trailing edge bows in toward the wing's leading edge
		{
			const float S = k / 8.f;
			const FVector OnLine = FMath::Lerp(Ankle, Tip, S);
			TrailLine.Add(OnLine + (Mid - FMath::Lerp(Ankle, Tip, 0.66f)) * 0.22f * FMath::Sin(PI * S));
		}
		const TArray<FVector> Trail = Resample(TrailLine, NU);

		for (const int32 Face : { 0, 1 })   // both faces, so it shows from above and below
		{
			const int32 Base = Verts.Num();
			for (int32 u = 0; u < NU; ++u)
			{
				for (int32 v = 0; v < NV; ++v)
				{
					Verts.Add(FMath::Lerp(Lead[u], Trail[u], v / float(NV - 1)));
				}
			}
			for (int32 u = 0; u < NU; ++u)
			{
				for (int32 v = 0; v < NV; ++v)
				{
					const FVector A = Verts[Base + FMath::Min(u + 1, NU - 1) * NV + v] - Verts[Base + FMath::Max(u - 1, 0) * NV + v];
					const FVector B = Verts[Base + u * NV + FMath::Min(v + 1, NV - 1)] - Verts[Base + u * NV + FMath::Max(v - 1, 0)];
					FVector N = FVector::CrossProduct(A, B).GetSafeNormal();
					Normals.Add(Face == 0 ? -N : N);   // each face's normal points out of the side it's wound for
				}
			}
			for (int32 u = 0; u + 1 < NU; ++u)
			{
				for (int32 v = 0; v + 1 < NV; ++v)
				{
					const int32 a = Base + u * NV + v, b = a + NV, c = b + 1, d = a + 1;
					if (Face == 0)
					{
						Tris.Append({ a, b, c, a, c, d });
					}
					else
					{
						Tris.Append({ a, c, b, a, d, c });
					}
				}
			}
		}
	}
	if (!bMembraneBuilt)
	{
		Membrane->CreateMeshSection(0, Verts, Tris, Normals, {}, {}, {}, false);
		// Two-sided (bone_forge_materials.py makes it), so the membrane shows from above and below; the basic
		// shape material is one-sided and was only a stand-in.
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeMembrane.M_BoneForgeMembrane"));
		if (!Base)
		{
			Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.11f, 0.09f, 0.075f));   // the dark grey-brown wings
		Membrane->SetMaterial(0, M);
		bMembraneBuilt = true;
	}
	else
	{
		Membrane->UpdateMeshSection(0, Verts, Normals, {}, {}, {});
	}
}

// ---------------------------------------------------------------------------------------------- posing

void ABFDino::PoseBones(float DeltaSeconds)
{
	if (Body->BoneSpaceTransforms.Num() != Poser.GetRefPose().Num())
	{
		return;
	}
	const float Speed = bFlying ? 0.f : GetVelocity().Size2D();
	const FBFGait& W = Species.Walk;
	const FBFGait& F = Species.Fast;
	FBFPoseInput In;
	In.GaitT = FMath::Clamp((Speed - W.SpeedCm) / FMath::Max(F.SpeedCm - W.SpeedCm, 1.f), 0.f, 1.f);

	// Turning: the head and neck swing toward where the animal is heading first; the body eases into the turn after
	// them (its yaw rate ramps up, and slows again as it lines up); the tail swings out behind (FBFPoser). Turning on
	// the spot carries the feet round the hips, so the legs step as if walking at that speed instead of sliding.
	const float Yaw = GetActorRotation().Yaw;
	const float YawRate = DeltaSeconds > 0.f ? FRotator::NormalizeAxis(Yaw - LastBodyYaw) / DeltaSeconds : 0.f;
	LastBodyYaw = Yaw;
	float Ahead = 0.f;   // degrees to the right of the body that the animal is heading
	const FVector Want = GetCharacterMovement()->GetCurrentAcceleration();
	if (!bFlying && !bRagdoll && Want.SizeSquared2D() > 1.f)
	{
		Ahead = FMath::FindDeltaAngleDegrees(Yaw, Want.Rotation().Yaw);
	}
	LookYaw = FMath::FInterpTo(LookYaw, FMath::Clamp(-Ahead, -LookCap, LookCap), DeltaSeconds, 5.f);
	TurnEase = FMath::FInterpTo(TurnEase, FMath::Clamp(FMath::Abs(Ahead) / 25.f, 0.15f, 1.f), DeltaSeconds, 2.5f);
	if (!bFlying)
	{
		GetCharacterMovement()->RotationRate.Yaw = BaseTurnRate * TurnEase;
	}
	TurnRate = FMath::FInterpTo(TurnRate, bFlying ? 0.f : FMath::Clamp(-YawRate / BaseTurnRate, -1.f, 1.f), DeltaSeconds, 4.f);
	const float StepSpeed = bFlying ? 0.f : FMath::Max(Speed, FMath::Abs(FMath::DegreesToRadians(YawRate)) * FootReach);

	if (GetCharacterMovement()->IsMovingOnGround())
	{
		Phase = FMath::Fmod(Phase + StepSpeed / Poser.StrideCm(In.GaitT) * DeltaSeconds, 1000.f);
	}
	else if (bSwimming)
	{
		// Paddling: the legs keep stroking even treading water, at least at half the stroke rate of its swimming speed.
		Phase = FMath::Fmod(Phase + FMath::Max(Speed, 0.5f * Species.SwimSpeedCm) / Poser.StrideCm(0.f) * DeltaSeconds, 1000.f);
	}
	MoveBlend = FMath::FInterpTo(MoveBlend, FMath::Max(FMath::Clamp(StepSpeed / FMath::Max(W.SpeedCm * 0.6f, 1.f), 0.f, 1.f), SwimBlend),
		DeltaSeconds, 6.f);
	if (FlyBlend > 0.001f)
	{
		const float Rate = BFFlapRate(FlapAmount) * (1.f + 0.35f * FlapHard());     // beating hard: quicker wingbeats
		FlapPhase = FMath::Fmod(FlapPhase + Rate * DeltaSeconds, 1000.f);
		GlideU = FMath::Fmod(GlideU + Rate * DeltaSeconds, 1000.f);
	}
	float BiteMouth = 0.f;
	if (BiteClock >= 0.f)
	{
		BiteClock += DeltaSeconds;
		BFBiteCurves(FMath::Min(BiteClock / BFBiteSeconds, 1.f), In.Bite, BiteMouth);
	}
	if (bClips)
	{
		TickClips(DeltaSeconds);
	}
	else
	{
		In.Phase = Phase;
		In.MoveBlend = MoveBlend;
		In.Tuck = TuckU;          // a jump: the legs drawn up in the air
		In.IdleU = GetWorld()->GetTimeSeconds() / BFIdleSeconds;
		In.Mouth = FMath::Max(Mouth, BiteMouth);
		In.FlyBlend = FlyBlend;
		In.FlapPhase = FlapPhase;
		In.FlapAmount = FlapAmount * (1.f + 0.2f * FlapHard());      // and deeper ones
		In.GlideU = GlideU;
		if (FlyBlend > 0.001f)
		{
			const float Up = ShoulderZ + 100.f * GetAltitudeM() - 10.f;   // shoulders above the ground, less a little
			In.FlapDownMax = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up / WingLenCm, 0.f, 1.f)));
		}
		In.LegLean = LegLean;
		In.LookYaw = LookYaw;
		In.Turn = TurnRate;
		In.NeckRaise = SwimBlend * Species.SwimNeckRaiseDeg;
		if (bMouthTarget && !bFlying && !bRagdoll && MoveBlend < 0.35f)
		{
			// Its snout to the target: down or up a little more each frame by how far off it is (a hand's breadth is near
			// enough); the head turned toward it as far as it looks round.
			const FVector Snout = SnoutNow();
			const float Off = float(Snout.Z - MouthTarget.Z);
			FeedReach = FMath::Clamp(FeedReach + FMath::Clamp(Off / 120.f, -1.f, 1.f) * 1.6f * DeltaSeconds * (FMath::Abs(Off) > 12.f ? 1.f : 0.f), -1.f, 1.9f);
			const float Feet = float(GetActorLocation().Z) - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			// As low as it goes (its neck down as far as it bends, settled there a second and a half, the target still
			// lower): the lowest it has got so, it remembers.
			// Its neck down as far as it bends and the target still lower: a four-legged one bends its forelegs, its front
			// pitched down a little more each frame (back up again once its mouth is lower than it needs).
			const float DipMax = FeedDipMax();
			if (FeedReach >= 1.85f && Off > 30.f)
			{
				FeedDip = FMath::Min(FeedDip + 5.f * DeltaSeconds, DipMax);
			}
			else if (Off < -20.f || FeedReach < 1.5f)
			{
				FeedDip = FMath::Max(FeedDip - 5.f * DeltaSeconds, 0.f);
			}
			LowHeldS = FeedReach >= 1.89f && Off > 30.f && FeedDip >= DipMax - 0.5f ? LowHeldS + DeltaSeconds : 0.f;
			if (LowHeldS > 1.5f)
			{
				const float H = float(Snout.Z) - Feet;
				MouthLowCm = MouthLowCm > 0.f ? FMath::Min(MouthLowCm, H) : FMath::Max(H, 1.f);
			}
			const float Hold = 1.f - MoveBlend / 0.35f;
			if (FeedReach > 0.f)
			{
				In.NeckDown = FMath::Max(In.NeckDown, FeedReach * Hold);
			}
			else
			{
				float Up = FMath::Max(Species.LimitsOf(TEXT("head")).Bend.Y, 0.f);
				for (const FName B : Species.Neck)
				{
					Up += FMath::Max(Species.LimitsOf(B).Bend.Y, 0.f);
				}
				In.NeckRaise = FMath::Max(In.NeckRaise, -FeedReach * 0.9f * Up * Hold);
			}
			const float ToYaw = FRotator::NormalizeAxis((MouthTarget - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw - GetActorRotation().Yaw);
			In.LookYaw += FMath::Clamp(ToYaw, -LookCap, LookCap) * Hold;
		}
		else if (!bMouthTarget)
		{
			FeedReach = FMath::FInterpTo(FeedReach, 0.f, DeltaSeconds, 2.f);
		}
		if (!bMouthTarget || MoveBlend >= 0.35f || bFlying || bRagdoll)
		{
			FeedDip = FMath::Max(FeedDip - 8.f * DeltaSeconds, 0.f);          // up off its bent forelegs
		}
		if (LieBlend > 0.01f)
		{
			// Lying, the head comes down to rest (a long neck laid along the ground), the tail laid out.
			const float L = FMath::SmoothStep(0.3f, 1.f, LieBlend);
			In.NeckDown = FMath::Max(In.NeckDown, (bSleep ? 0.9f : 0.4f) * L);
			In.BreathDepth *= bSleep ? 0.7f : 1.f;
		}
		SniffHoldS -= DeltaSeconds;
		SniffW = FMath::FInterpTo(SniffW, SniffHoldS > 0.f ? 1.f : 0.f, DeltaSeconds, 3.f);
		if (SniffW > 0.01f && !bMouthTarget && !bFlying && !bRagdoll)
		{
			In.Sniff = FMath::Max(In.Sniff, SniffW);
			In.SniffU = GetWorld()->GetTimeSeconds() * 4.f;
			if (SniffDown > 0.f)
			{
				In.NeckDown = FMath::Max(In.NeckDown, SniffDown * SniffW);
			}
			else
			{
				float Up = FMath::Max(Species.LimitsOf(TEXT("head")).Bend.Y, 0.f);
				for (const FName B : Species.Neck)
				{
					Up += FMath::Max(Species.LimitsOf(B).Bend.Y, 0.f);
				}
				In.NeckRaise = FMath::Max(In.NeckRaise, -0.4f * SniffDown * Up * SniffW);
			}
		}
		// Its eyes on something (SetGaze): the head and neck turned to it as far as they go, raised (alarm) or lowered
		// (horns to it); further round than that, the body turns too, standing.
		GazeHoldS -= DeltaSeconds;
		GazeW = FMath::FInterpTo(GazeW, GazeHoldS > 0.f && !bRagdoll && !bFlying ? 1.f : 0.f, DeltaSeconds, GazeHoldS > 0.f ? 6.f : 3.f);
		if (GazeW > 0.01f && !bMouthTarget)
		{
			const float ToYaw = FRotator::NormalizeAxis((GazeAt - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw - GetActorRotation().Yaw);
			In.LookYaw = FMath::Lerp(In.LookYaw, FMath::Clamp(ToYaw, -LookCap, LookCap), GazeW);
			if (FMath::Abs(ToYaw) > 0.8f * LookCap && MoveBlend < 0.3f && GazeHoldS > 0.f)
			{
				SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, GetActorRotation().Yaw + ToYaw, 0.f), DeltaSeconds, 1.2f));
			}
			if (GazeRaise > 0.f)
			{
				float Up = FMath::Max(Species.LimitsOf(TEXT("head")).Bend.Y, 0.f);
				for (const FName B : Species.Neck)
				{
					Up += FMath::Max(Species.LimitsOf(B).Bend.Y, 0.f);
				}
				In.NeckRaise = FMath::Max(In.NeckRaise, 0.5f * Up * GazeRaise * GazeW);
				In.NeckDown *= 1.f - GazeW;
			}
			else
			{
				In.NeckDown = FMath::Max(In.NeckDown, -0.7f * GazeRaise * GazeW);
			}
		}
		// Limping: the weight comes off the sore leg as it lands (the hips shift over the good one) and the head
		// bobs up as it takes the weight, down onto the good one; the stride hitches.
		if (LimpAmt > 0.01f && MoveBlend > 0.1f && !bFlying && !bRagdoll)
		{
			const float Sore = FMath::Max(FMath::Sin(2.f * PI * Phase), 0.f);          // the sore leg's stance half
			In.Shift = FMath::Clamp(In.Shift - LimpSide * 0.9f * LimpAmt * Sore * MoveBlend, -1.f, 1.f);
			In.NeckRaise += 6.f * LimpAmt * Sore * MoveBlend;
			In.NeckDown += 0.25f * LimpAmt * FMath::Max(-FMath::Sin(2.f * PI * Phase), 0.f) * MoveBlend;
		}
		// Winded and standing: head low, flanks heaving (the breath's own depth), mouth open.
		if (Exertion > 0.5f && MoveBlend < 0.3f && !bMouthTarget && !bFlying && !bRagdoll && !bSwimming)
		{
			const float Tired = FMath::SmoothStep(0.5f, 0.9f, Exertion) * (1.f - MoveBlend / 0.3f);
			In.NeckDown = FMath::Max(In.NeckDown, 0.45f * Tired);
			In.Jaw = FMath::Max(In.Jaw, 0.25f * Tired);
		}
		// Tearing a mouthful off a kill, once the bite has closed on it: the jaws stay shut, the head hauls back up and
		// away and shakes it (a small hunter whips its head side to side, a big one wrenches with its whole neck, slower),
		// then lets the neck back down to the kill for the next bite.
		TearNow = 0.f;
		TossNow = 0.f;
		if (TearClock > -999.f)
		{
			TearClock += DeltaSeconds;
			const bool bSmall = Species.MassKg < 1500.f;
			const float Len = 1.0f;          // the pull, then the toss
			if (TearClock >= Len || !bMouthTarget || MoveBlend > 0.35f)
			{
				TearClock = -1000.f;
			}
			else if (TearClock > 0.f)
			{
				const float U = TearClock / Len;
				const float PullEnd = bSmall ? 0.65f : 0.75f;
				const float Pull = FMath::Sin(PI * FMath::Pow(FMath::Min(U / PullEnd, 1.f), 0.7f));
				TearNow = Pull;
				// The gulp: the head flicked up, the jaws parting to let the meat slide back, shut again.
				const float Toss = FMath::Sin(PI * FMath::Clamp((U - PullEnd) / (1.f - PullEnd), 0.f, 1.f));
				if (Toss > 0.f)
				{
					float Up = FMath::Max(Species.LimitsOf(TEXT("head")).Bend.Y, 0.f);
					for (const FName B : Species.Neck)
					{
						Up += FMath::Max(Species.LimitsOf(B).Bend.Y, 0.f);
					}
					In.NeckDown = FMath::Max(In.NeckDown - 0.6f * Toss, 0.f);
					In.NeckRaise = FMath::Max(In.NeckRaise, (bSmall ? 0.45f : 0.25f) * Up * Toss);
					TossNow = Toss;
				}
				In.NeckDown = FMath::Max(In.NeckDown - (bSmall ? 0.5f : 0.35f) * Pull, 0.f);
				const float Hz = bSmall ? 4.f : 1.6f;
				In.LookYaw += (bSmall ? 0.35f : 0.2f) * LookCap * Pull * FMath::Sin(2.f * PI * Hz * TearClock);
				In.Turn = FMath::Clamp(In.Turn + (bSmall ? 0.f : 0.3f) * Pull * FMath::Sin(2.f * PI * Hz * TearClock), -1.f, 1.f);
				In.Bite = 0.f;
				BiteMouth = 0.f;          // the jaws clamped on the meat
				In.Mouth = Toss > 0.f ? 0.3f * Toss : FMath::Min(In.Mouth, 0.06f);
			}
		}
		LegGaitT = In.GaitT;
		float IdleMouth = 0.f;
		TickLife(DeltaSeconds, In, IdleMouth);
		In.Mouth = FMath::Max(In.Mouth, IdleMouth);
		MouthNow = FMath::Max(In.Mouth, In.Jaw);
		Poser.Pose(In, Body->BoneSpaceTransforms);
		SolveLegIK(Body->BoneSpaceTransforms, DeltaSeconds);
		SolveTailGround(Body->BoneSpaceTransforms);
		Body->MarkRefreshTransformDirty();
	}
	TickSkinDetail(DeltaSeconds, In);
	TickBlood(DeltaSeconds);
	if (bClips)
	{
		HipDrop = FMath::FInterpTo(HipDrop, 0.f, DeltaSeconds, 4.f);   // the baked clips get no leg IK
	}
	if (BiteClock > BFBiteSeconds)
	{
		BiteClock = -1.f;
	}
}

// ---------------------------------------------------------------------------------------------- life (roadmap 7)

const USkinnedMeshComponent* ABFDino::PoseMesh() const
{
	return bRagdoll ? static_cast<const USkinnedMeshComponent*>(GetMesh()) : static_cast<const USkinnedMeshComponent*>(Body.Get());
}

FVector ABFDino::RefBoneMesh(FName Bone) const
{
	const TArray<FTransform>& Ref = Poser.GetRefPose();
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	FTransform T = FTransform::Identity;
	for (int32 i = RS.FindBoneIndex(Bone); i != INDEX_NONE && Ref.IsValidIndex(i); i = RS.GetParentIndex(i))
	{
		T = T * Ref[i];
	}
	return T.GetLocation();
}

void ABFDino::AddWound(const FVector& WorldNear, float RadiusCm)
{
	if (!Species.Mesh || !Body)
	{
		return;
	}
	// The bone nearest the bite, and out from it toward where the teeth were (in its own standing pose), at most
	// as far out as its skin is round that bone.
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	int32 Best = INDEX_NONE;
	float BestD = 1e12f;
	for (int32 i = 0; i < RS.GetNum(); ++i)
	{
		const float D = float(FVector::DistSquared(Body->GetBoneLocation(RS.GetBoneName(i)), WorldNear));
		if (D < BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	if (Best == INDEX_NONE)
	{
		return;
	}
	const FName Bone = RS.GetBoneName(Best);
	const FVector Out = Body->GetComponentTransform().InverseTransformVector(WorldNear - Body->GetBoneLocation(Bone));
	const float Skin = 0.5f * float(Species.Bounds.GetSize().Y);
	const FVector At = RefBoneMesh(Bone) + Out.GetClampedToMaxSize(Skin);
	const float R = RadiusCm / FMath::Max(float(Body->GetComponentScale().X), 0.05f);
	// A new wound near an old one adds to it; otherwise the next of the three slots (the oldest goes).
	int32 Slot = INDEX_NONE;
	for (int32 k = 1; k < 4; ++k)
	{
		if (BloodSpot[k].W > 0.f && FVector::Dist(FVector(BloodSpot[k]), At) < 0.6f * (BloodSpot[k].W + R))
		{
			Slot = k;
		}
	}
	if (Slot == INDEX_NONE)
	{
		Slot = NextWound;
		NextWound = NextWound % 3 + 1;
		BloodSpot[Slot] = FVector4(At.X, At.Y, At.Z, R);
	}
	else
	{
		BloodSpot[Slot].W = FMath::Min(BloodSpot[Slot].W + 0.5f * R, 3.f * R);
	}
	BloodAge[Slot] = 0.f;
	PushBlood();
}

void ABFDino::BloodyMuzzle(float AddCm)
{
	if (!Species.Mesh || !Body)
	{
		return;
	}
	const FVector Tip = RefBoneMesh(TEXT("head_end"));
	const FVector Head = RefBoneMesh(TEXT("head"));
	const float Most = FMath::Max(0.7f * float(FVector::Dist(Tip, Head)), 10.f);
	const float R = AddCm / FMath::Max(float(Body->GetComponentScale().X), 0.05f);
	// The jaws and chin: a little back from the snout's tip, low.
	const FVector At = FMath::Lerp(Tip, Head, 0.3f);
	BloodSpot[0] = FVector4(At.X, At.Y, At.Z, FMath::Min(BloodSpot[0].W + R, Most));
	BloodAge[0] = 0.f;
	PushBlood();
}

void ABFDino::PushBlood()
{
	// Each spot's colour is the freshest's: bright and wet at first, dark and dull dried (minutes).
	float Newest = 1e9f;
	for (int32 k = 0; k < 4; ++k)
	{
		if (BloodSpot[k].W > 0.f)
		{
			Newest = FMath::Min(Newest, BloodAge[k]);
		}
	}
	const float Dry = FMath::SmoothStep(30.f, 400.f, Newest);
	const FLinearColor Col = FMath::Lerp(FLinearColor(0.11f, 0.006f, 0.005f), FLinearColor(0.035f, 0.01f, 0.007f), Dry);
	for (UMaterialInstanceDynamic* D : SkinMids)
	{
		if (!D)
		{
			continue;
		}
		for (int32 k = 0; k < 4; ++k)
		{
			D->SetVectorParameterValue(*FString::Printf(TEXT("Blood%d"), k), FLinearColor(BloodSpot[k].X, BloodSpot[k].Y, BloodSpot[k].Z, BloodSpot[k].W));
		}
		D->SetVectorParameterValue(TEXT("BloodColour"), Col);
		D->SetScalarParameterValue(TEXT("BloodWet"), 1.f - Dry);
	}
	BloodPush = 1.f;
}

void ABFDino::TickBlood(float DeltaSeconds)
{
	// Wet from the water: darker and glossy, drying over a minute and a half (a shake dries it faster).
	WetNow = bInWater ? 1.f : FMath::Max(WetNow - DeltaSeconds / (IdleAct == EBFIdle::Shake ? 8.f : 90.f), 0.f);
	if (FMath::Abs(WetNow - WetShown) > 0.03f || (WetNow == 0.f && WetShown != 0.f))
	{
		WetShown = WetNow;
		for (UMaterialInstanceDynamic* D : SkinMids)
		{
			if (D)
			{
				D->SetScalarParameterValue(TEXT("Wet"), WetNow);
			}
		}
	}
	bool bAny = false;
	for (int32 k = 0; k < 4; ++k)
	{
		if (BloodSpot[k].W <= 0.f)
		{
			continue;
		}
		bAny = true;
		BloodAge[k] += DeltaSeconds;
		// It fades: the muzzle after ten minutes (licked, rubbed off), wounds after half an hour (healing over);
		// in water it washes off fast.
		const float After = k == 0 ? 600.f : 1800.f;
		const float Rate = (bInWater ? 6.f : 0.f) + (BloodAge[k] > After ? 0.05f : 0.f);
		BloodSpot[k].W = FMath::Max(BloodSpot[k].W - Rate * DeltaSeconds, 0.f);
	}
	BloodPush -= DeltaSeconds;
	if (bAny && BloodPush <= 0.f)
	{
		PushBlood();
	}
}

float ABFDino::FeedDipMax() const
{
	// Four-legged only (a biped tips over): the shoulders lowered by up to 35% of a leg's height, as far as the shoulders
	// swing back to keep the forelegs upright.
	if (!bQuadPitch || bFlying || QuadSpanCm < 1.f)
	{
		return 0.f;
	}
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(0.35f * HindLegCm, QuadSpanCm));
	return FMath::Clamp(FMath::Min(Deg, -LegLeanLo - 2.f), 0.f, 25.f);
}

FVector ABFDino::SnoutNow() const
{
	FVector S = GetBoneWorld(TEXT("head_end"));
	if (S.Equals(GetActorLocation()))
	{
		S = GetBoneWorld(TEXT("head"));
	}
	return S;
}

float ABFDino::MouthMissCm() const
{
	return bMouthTarget ? float(FVector::Dist(SnoutNow(), MouthTarget)) : -1.f;
}

const TCHAR* ABFDino::IdleName(EBFIdle Which)
{
	switch (Which)
	{
	case EBFIdle::Look: return TEXT("look");
	case EBFIdle::Sniff: return TEXT("sniff");
	case EBFIdle::Shift: return TEXT("shift");
	case EBFIdle::Swish: return TEXT("swish");
	case EBFIdle::Yawn: return TEXT("yawn");
	case EBFIdle::Shake: return TEXT("shake");
	case EBFIdle::Feed: return TEXT("feed");
	default: return TEXT("none");
	}
}

float ABFDino::IdleSeconds(EBFIdle Which)
{
	switch (Which)
	{
	case EBFIdle::Look: return 4.5f;
	case EBFIdle::Sniff: return 4.f;
	case EBFIdle::Shift: return 5.f;
	case EBFIdle::Swish: return 2.5f;
	case EBFIdle::Yawn: return 3.f;
	case EBFIdle::Shake: return 2.2f;
	case EBFIdle::Feed: return 8.f;
	default: return 0.f;
	}
}

void ABFDino::StartIdle(EBFIdle Which)
{
	// Not on legs that can't stand, nor collapsed (it doesn't tick its life then, so a behaviour would never end).
	IdleAct = (Which == EBFIdle::Feed && !CanFeed()) || bRagdoll || Species.Legs != EBFLegs::Walks ? EBFIdle::None : Which;
	IdleClock = 0.f;
	IdleSide = 1.f;
}

void ABFDino::ApplyLook(const FBFLook& InLook)
{
	Look = InLook;
	const FBFSkinScheme* Own = BFSkinScheme(OwnSkinKey());
	const FBFSkinScheme* Col = InLook.Scheme.IsEmpty() ? Own : BFSkinScheme(InLook.Scheme);
	const FBFSkinScheme* Cov = InLook.Covering.IsEmpty() ? Own : BFSkinScheme(InLook.Covering);
	if (!Own || !Col || !Cov)
	{
		return;
	}
	// The colours: turned round the colour wheel, lighter or darker. The toes keep their keratin, the eyes their iris.
	auto Shift = [&](const TCHAR* Hex)
	{
		FLinearColor HSV = BFHexLinear(Hex).LinearRGBToHSV();
		HSV.R = FMath::Fmod(HSV.R + InLook.HueDeg + 720.f, 360.f);
		HSV.B = FMath::Clamp(HSV.B * (1.f + InLook.Shade), 0.f, 1.f);
		return HSV.HSVToLinearRGB();
	};
	const FLinearColor Back = Shift(Col->Back), Belly = Shift(Col->Belly), Pattern = Shift(Col->Pattern), Accent = Shift(Col->Accent);
	const FLinearColor Toe = BFScuteLinear(*Col);
	// The covering: its tiles, sized to this body (the square root of the lengths: a T. rex's feathers aren't six times a
	// raptor's), unless it is its own, which keeps the size its material was made with.
	const float Scale = Cov == Own ? 1.f : FMath::Sqrt(FMath::Max(Species.Bounds.GetSize().X, 20.f) / Cov->BodyCm);
	const float TileCm = Cov == Own ? Own->TileCm : Cov->TileCm * Scale;
	const float BumpCm = Cov == Own ? Own->BumpCm : Cov->BumpCm * Scale;
	static const TCHAR* Regions[4] = { TEXT("BackTile"), TEXT("BellyTile"), TEXT("FaceTile"), TEXT("FeetTile") };
	UTexture* Tiles[4] = {};
	for (int32 r = 0; r < 4; ++r)
	{
		const FString Path = FString::Printf(TEXT("/Game/BoneForge/Textures/T_%s_H.T_%s_H"), Cov->Tiles[r], Cov->Tiles[r]);
		Tiles[r] = LoadObject<UTexture>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	for (UMaterialInstanceDynamic* D : SkinMids)
	{
		if (!D)
		{
			continue;
		}
		const bool bToes = ScuteMids.Contains(D);
		D->SetVectorParameterValue(TEXT("BackColour"), bToes ? Toe : Back);
		D->SetVectorParameterValue(TEXT("BellyColour"), bToes ? Toe : Belly);
		D->SetVectorParameterValue(TEXT("PatternColour"), bToes ? Toe : Pattern);
		D->SetVectorParameterValue(TEXT("AccentColour"), bToes ? Toe : Accent);
		for (int32 r = 0; r < 4; ++r)
		{
			if (Tiles[r])
			{
				D->SetTextureParameterValue(Regions[r], Tiles[r]);
			}
		}
		D->SetScalarParameterValue(TEXT("TileCm"), TileCm);
		D->SetScalarParameterValue(TEXT("BumpCm"), BumpCm);
	}
	for (UMaterialInstanceDynamic* D : EyeMids)
	{
		if (D)
		{
			D->SetVectorParameterValue(TEXT("Iris"), BFHexLinear(Col->Iris));
		}
	}
	UTexture* Shown = nullptr;
	if (SkinMids.Num() > 0 && SkinMids[0])
	{
		SkinMids[0]->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BackTile")), Shown);
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] look on %s: %s colours, %s covering (back tile now %s, %d of 4 tiles loaded, %.1f cm a tile), %d skin and %d toe materials"),
		*Species.Key, Col->Key, Cov->Key, Shown ? *Shown->GetName() : TEXT("none"), (Tiles[0] != nullptr) + (Tiles[1] != nullptr) + (Tiles[2] != nullptr) + (Tiles[3] != nullptr),
		TileCm, SkinMids.Num() - ScuteMids.Num(), ScuteMids.Num());
}

void ABFDino::MakeSkinMids()
{
	// Every skin slot (the skin master's instances) on both meshes, so the live pose and the baked clips both jiggle;
	// the belly is the stretch between the hips and the shoulders (or chest), in the mesh's own space.
	SkinMids.Reset();
	ScuteMids.Reset();
	const TArray<FTransform>& Ref = Poser.GetRefPose();
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	auto MeshX = [&](const TCHAR* Bone) -> float
	{
		FTransform T = FTransform::Identity;
		for (int32 i = RS.FindBoneIndex(Bone); i != INDEX_NONE && Ref.IsValidIndex(i); i = RS.GetParentIndex(i))
		{
			T = T * Ref[i];
		}
		return T.GetLocation().X;
	};
	const float X0 = MeshX(TEXT("thigh_l"));
	float X1 = RS.FindBoneIndex(TEXT("upperarm_l")) != INDEX_NONE ? MeshX(TEXT("upperarm_l")) : MeshX(TEXT("chest"));
	if (X1 - X0 < 20.f)
	{
		X1 = X0 + 0.3f * Species.Bounds.GetSize().X;
	}
	EyeMids.Reset();
	for (UMeshComponent* M : { static_cast<UMeshComponent*>(Body), static_cast<UMeshComponent*>(GetMesh()) })
	{
		for (int32 i = 0; M && i < M->GetNumMaterials(); ++i)
		{
			UMaterialInterface* Mat = M->GetMaterial(i);
			UMaterial* Base = Mat ? Mat->GetBaseMaterial() : nullptr;
			if (Base && Base->GetName() == TEXT("M_BoneForgeEye"))
			{
				if (UMaterialInstanceDynamic* D = M->CreateDynamicMaterialInstance(i, Mat))
				{
					EyeMids.Add(D);      // the eyes and nostrils: the nostrils flare
				}
				continue;
			}
			if (!Base || Base->GetName() != TEXT("M_BoneForgeSkin"))
			{
				continue;
			}
			UMaterialInstanceDynamic* D = M->CreateDynamicMaterialInstance(i, Mat);
			if (D)
			{
				D->SetScalarParameterValue(TEXT("BellyX0"), X0);
				D->SetScalarParameterValue(TEXT("BellyX1"), X1);
				D->SetScalarParameterValue(TEXT("Jiggle"), 0.f);
				SkinMids.Add(D);
				const TArray<FName> Slots = M->GetMaterialSlotNames();
				if ((Slots.IsValidIndex(i) && Slots[i].ToString().EndsWith(TEXT("_Scute"))) || Mat->GetName().EndsWith(TEXT("_Scute")))
				{
					ScuteMids.Add(D);      // toes and fingers: the skin's scales in the keratin's colour
				}
			}
		}
	}

	// Skin sliding: each walking limb's joint, the way it points at rest and its length (mesh space).
	SlideLimbs.Reset();
	for (const FBFLimb& L : Species.Limbs)
	{
		FSlideLimb S;
		S.Upper = RS.FindBoneIndex(L.Upper);
		S.Mid = RS.FindBoneIndex(L.Mid);
		if (S.Upper == INDEX_NONE || S.Mid == INDEX_NONE || SlideLimbs.Num() >= 6)
		{
			continue;
		}
		S.Parent = RS.GetParentIndex(S.Upper);
		const FVector U = RefComponent(S.Upper).GetLocation(), K = RefComponent(S.Mid).GetLocation();
		S.Joint = U;
		S.LenCm = FVector::Dist(U, K);
		S.Dir = (K - U).GetSafeNormal();
		S.ParentRef = S.Parent != INDEX_NONE ? RefComponent(S.Parent).GetRotation() : FQuat::Identity;
		if (S.LenCm > 1.f)
		{
			SlideLimbs.Add(S);
		}
	}
	// Skin folds: each walking limb's knee or elbow (at most six), the lower bone's rest direction and the zone either
	// side of the joint the folds can reach (just under half the lower bone).
	{
		FLinearColor Probe;
		const bool bHas = SkinMids.Num() > 0 && SkinMids[0] && SkinMids[0]->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FoldA0")), Probe);
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] skin %s: fold settings on its material: %s (%d skin materials)"), *Species.Key, bHas ? TEXT("yes") : TEXT("NO"), SkinMids.Num());
	}
	FoldJoints.Reset();
	for (const FBFLimb& L : Species.Limbs)
	{
		FFoldJoint F;
		F.Upper = RS.FindBoneIndex(L.Upper);
		F.Mid = RS.FindBoneIndex(L.Mid);
		F.Low = RS.FindBoneIndex(L.Low);
		if (F.Upper == INDEX_NONE || F.Mid == INDEX_NONE || F.Low == INDEX_NONE || FoldJoints.Num() >= 6)
		{
			continue;
		}
		const FVector U = RefComponent(F.Upper).GetLocation(), K = RefComponent(F.Mid).GetLocation(), A = RefComponent(F.Low).GetLocation();
		F.Joint = K;
		F.LowDir = (A - K).GetSafeNormal();
		F.RestAngle = FMath::Acos(FMath::Clamp(FVector::DotProduct((K - U).GetSafeNormal(), F.LowDir), -1.f, 1.f));
		F.ZoneCm = 0.45f * FVector::Dist(K, A);
		if (F.ZoneCm > 1.f)
		{
			FoldJoints.Add(F);
		}
	}

	// Nostrils: their size from the skull's length (the head's species' share).
	const int32 HeadBone = RS.FindBoneIndex(TEXT("head")), SnoutBone = RS.FindBoneIndex(TEXT("head_end"));
	NostrilCm = HeadBone != INDEX_NONE && SnoutBone != INDEX_NONE
		? Species.NostrilShare * FVector::Dist(RefComponent(HeadBone).GetLocation(), RefComponent(SnoutBone).GetLocation()) : 0.f;

	// Feathered wings: the mesh carries feather coordinates (bone_forge_gltf.feather_uv) on some vertex.
	bFeathers = false;
#if WITH_EDITORONLY_DATA
	if (const FSkeletalMeshModel* Model = Species.Mesh->GetImportedModel(); Model && Model->LODModels.Num() > 0)
	{
		for (const FSkelMeshSection& Sec : Model->LODModels[0].Sections)
		{
			for (const FSoftSkinVertex& V : Sec.SoftVertices)
			{
				if (V.UVs[0].X > 0.01f)
				{
					bFeathers = true;
					break;
				}
			}
			if (bFeathers)
			{
				break;
			}
		}
	}
#endif
	FeatherCm = 0.f;
	for (int32 k = 0; k < 2; ++k)
	{
		const TCHAR* Side = k == 0 ? TEXT("l") : TEXT("r");
		const float Sgn = k == 0 ? -1.f : 1.f;
		const int32 LA = RS.FindBoneIndex(FName(FString::Printf(TEXT("lowerarm_%s"), Side)));
		const int32 HA = RS.FindBoneIndex(FName(FString::Printf(TEXT("hand_%s"), Side)));
		const int32 FE = RS.FindBoneIndex(FName(FString::Printf(TEXT("finger_end_%s"), Side)));
		FeatherN[k] = FVector(0.f, Sgn, 0.f);
		FeatherSway[k] = FeatherSwayVel[k] = HandVel[k] = HandPos[k] = FVector::ZeroVector;
		if (LA == INDEX_NONE || HA == INDEX_NONE || FE == INDEX_NONE)
		{
			continue;
		}
		const FVector A = RefComponent(LA).GetLocation(), H = RefComponent(HA).GetLocation(), F = RefComponent(FE).GetLocation();
		// The feathers are about a third of the forearm and hand long (parts_builder.wing_feathers: 6 to 11 cm on the
		// raptor's 25), lying in the plane of the arm and the way they trail, back and down.
		FeatherCm = FMath::Max(FeatherCm, 0.36f * (FVector::Dist(A, H) + FVector::Dist(H, F)));
		FVector N = FVector::CrossProduct(F - A, FVector(-1.f, 0.3f * Sgn, -0.5f).GetSafeNormal()).GetSafeNormal();
		FeatherN[k] = N.Y * Sgn < 0.f ? -N : N;      // outward
	}
	bHandValid = false;
}

FTransform ABFDino::RefComponent(int32 Bone) const
{
	const TArray<FTransform>& Ref = Poser.GetRefPose();
	const FReferenceSkeleton& RS = Species.Mesh->GetRefSkeleton();
	FTransform T = FTransform::Identity;
	for (int32 i = Bone; i != INDEX_NONE && Ref.IsValidIndex(i); i = RS.GetParentIndex(i))
	{
		T = T * Ref[i];
	}
	return T;
}

void ABFDino::TickSkinDetail(float DeltaSeconds, const FBFPoseInput& In)
{
	USkinnedMeshComponent* M = Shown();
	if (!M || DeltaSeconds <= 0.f)
	{
		return;
	}
	const TArray<FTransform>& CS = M->GetComponentSpaceTransforms();
	const FTransform& C2W = M->GetComponentTransform();
	// How a bone has turned from rest, as a rotation of the mesh's own axes.
	auto Turned = [&](int32 Bone) { return CS.IsValidIndex(Bone) ? CS[Bone].GetRotation() * RefComponent(Bone).GetRotation().Inverse() : FQuat::Identity; };

	// Nostrils (the design doc: flare slightly on each breath in, more when sniffing): their rims grow by a share of
	// their size, a tenth breathing in at rest, a quarter winded, a third sniffing.
	NostrilFlareCm = 0.f;
	if (NostrilCm > 0.f && EyeMids.Num() > 0)
	{
		const float BreathIn = FMath::Max(0.f, FMath::Cos(2.f * PI * In.Breath)) * FMath::Min(In.BreathDepth, 2.5f) / 2.5f;
		const float Sniffing = In.Sniff * (0.5f + 0.5f * FMath::Sin(2.f * PI * In.SniffU));
		NostrilFlareCm = NostrilOverride >= 0.f ? NostrilOverride : FMath::Max(0.25f * BreathIn, 0.35f * Sniffing) * 0.5f * NostrilCm;
		const int32 Head = M->GetBoneIndex(TEXT("head"));
		const FVector Right = Head != INDEX_NONE ? C2W.TransformVectorNoScale(Turned(Head).RotateVector(FVector::RightVector)) : GetActorRightVector();
		for (UMaterialInstanceDynamic* D : EyeMids)
		{
			if (D)
			{
				D->SetScalarParameterValue(TEXT("NostrilFlare"), NostrilFlareCm);
				D->SetVectorParameterValue(TEXT("HeadRight"), FLinearColor(Right));
			}
		}
	}

	// Feathers: the tips lag the hand as it moves (a stiff damped spring), ruffle up and out shaking off water,
	// and flutter in the airflow, faster and harder the faster it runs.
	FeatherTipCm = 0.f;
	if (bFeathers && FeatherCm > 0.f)
	{
		const float Speed = GetVelocity().Size();
		const float Run = FMath::Clamp(Speed / 800.f, 0.f, 1.5f);
		FeatherU = FMath::Fmod(FeatherU + (In.Shake > 0.01f ? 9.f : 6.f + 1.5f * Speed / 100.f) * DeltaSeconds, 1000.f);
		const float Flutter = FeatherCm * (0.05f * Run * Run + 0.12f * In.Shake);
		const float Lift = FeatherCm * 0.3f * In.Shake;
		FVector NW[2];
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 Hand = M->GetBoneIndex(FName(k == 0 ? TEXT("hand_l") : TEXT("hand_r")));
			NW[k] = C2W.TransformVectorNoScale(Turned(Hand).RotateVector(FeatherN[k]));
			const FVector Pos = Hand != INDEX_NONE && CS.IsValidIndex(Hand) ? C2W.TransformPosition(CS[Hand].GetLocation()) : GetActorLocation();
			if (!bHandValid || FVector::Dist(Pos, HandPos[k]) > 50.f)      // the first frame, or moved (teleported)
			{
				HandPos[k] = Pos;
				HandVel[k] = FeatherSway[k] = FeatherSwayVel[k] = FVector::ZeroVector;
				continue;
			}
			const FVector Vel = (Pos - HandPos[k]) / DeltaSeconds;
			const FVector Acc = ((Vel - HandVel[k]) / DeltaSeconds).GetClampedToMaxSize(6000.f);
			HandPos[k] = Pos;
			HandVel[k] = Vel;
			// Stiff: about 12 Hz, well above the stride's bounce (at 5 Hz the raptor's running bounce rang the tips to
			// a third of their length).
			const float W = 2.f * PI * 12.f, Z = 0.4f;
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaSeconds * 240.f));
			const float H = DeltaSeconds / Steps;
			for (int32 s = 0; s < Steps; ++s)
			{
				FeatherSwayVel[k] += (-Acc - W * W * FeatherSway[k] - 2.f * Z * W * FeatherSwayVel[k]) * H;
				FeatherSway[k] += FeatherSwayVel[k] * H;
			}
			FeatherSway[k] = FeatherSway[k].GetClampedToMaxSize(0.3f * FeatherCm);
			FeatherTipCm = FMath::Max(FeatherTipCm, FeatherSway[k].Size() + Lift + Flutter);
		}
		bHandValid = true;
		for (UMaterialInstanceDynamic* D : SkinMids)
		{
			if (D)
			{
				D->SetVectorParameterValue(TEXT("FeatherSwayL"), FLinearColor(FeatherSway[0]));
				D->SetVectorParameterValue(TEXT("FeatherSwayR"), FLinearColor(FeatherSway[1]));
				D->SetVectorParameterValue(TEXT("FeatherNL"), FLinearColor(NW[0]));
				D->SetVectorParameterValue(TEXT("FeatherNR"), FLinearColor(NW[1]));
				D->SetScalarParameterValue(TEXT("FeatherLift"), Lift);
				D->SetScalarParameterValue(TEXT("FeatherFlutter"), Flutter);
				D->SetScalarParameterValue(TEXT("FeatherU"), FeatherU);
			}
		}
	}

	// Skin sliding over the hips and shoulders: each walking limb's swing from rest, as the body sees it (in its
	// parent's frame), about the mesh's own Y; the skin material turns its pattern near the joint by a share of it.
	SlideDeg = SlideCm = 0.f;
	FLinearColor SA[6], SB[6];
	for (int32 i = 0; i < 6; ++i)
	{
		SA[i] = SB[i] = FLinearColor(0.f, 0.f, 0.f, 0.f);
		if (!SlideLimbs.IsValidIndex(i) || !CS.IsValidIndex(SlideLimbs[i].Upper) || !CS.IsValidIndex(SlideLimbs[i].Mid))
		{
			continue;
		}
		const FSlideLimb& S = SlideLimbs[i];
		const FQuat ParentNow = CS.IsValidIndex(S.Parent) ? CS[S.Parent].GetRotation() : FQuat::Identity;
		const FVector V = (S.ParentRef * ParentNow.Inverse()).RotateVector(CS[S.Mid].GetLocation() - CS[S.Upper].GetLocation());
		const float Theta = FMath::UnwindRadians(FMath::Atan2(S.Dir.Z, S.Dir.X) - FMath::Atan2(V.Z, V.X));
		SA[i] = FLinearColor(S.Joint.X, S.Joint.Y, S.Joint.Z, Theta * SlideScale);
		SB[i] = FLinearColor(S.Dir.X, S.Dir.Y, S.Dir.Z, S.LenCm);
		SlideDeg = FMath::Max(SlideDeg, FMath::Abs(FMath::RadiansToDegrees(Theta)));
		// The pattern at a fifth of the limb's length from the joint, on the limb, where the slide is about its most.
		SlideCm = FMath::Max(SlideCm, FMath::Abs(Theta) * 0.36f * 0.2f * S.LenCm);
	}
	static const FName NameA[6] = { TEXT("SlideA0"), TEXT("SlideA1"), TEXT("SlideA2"), TEXT("SlideA3"), TEXT("SlideA4"), TEXT("SlideA5") };
	static const FName NameB[6] = { TEXT("SlideB0"), TEXT("SlideB1"), TEXT("SlideB2"), TEXT("SlideB3"), TEXT("SlideB4"), TEXT("SlideB5") };

	// Skin folds (lab plan item 8): how far each knee or elbow is bent past its rest (the angle between its bones, 1 at
	// 60 degrees more), and the side it closes on: the way the lower bone has turned, seen from the upper bone's rest
	// frame (where the material's rest positions are).
	FoldMax = 0.f;
	FLinearColor FA[6], FB[6], FC[6];
	for (int32 i = 0; i < 6; ++i)
	{
		FA[i] = FB[i] = FC[i] = FLinearColor(0.f, 0.f, 0.f, 0.f);
		if (!FoldJoints.IsValidIndex(i))
		{
			continue;
		}
		FFoldJoint& F = FoldJoints[i];
		if (!CS.IsValidIndex(F.Upper) || !CS.IsValidIndex(F.Mid) || !CS.IsValidIndex(F.Low))
		{
			continue;
		}
		const FVector Up = (CS[F.Mid].GetLocation() - CS[F.Upper].GetLocation()).GetSafeNormal();
		const FVector Low = (CS[F.Low].GetLocation() - CS[F.Mid].GetLocation()).GetSafeNormal();
		const float Extra = FMath::Acos(FMath::Clamp(FVector::DotProduct(Up, Low), -1.f, 1.f)) - F.RestAngle;
		const float Raw = FMath::Clamp(Extra / FMath::DegreesToRadians(60.f), 0.f, 1.f);
		if (Raw > FoldMax)                        // (as bent, folds shown or not)
		{
			FoldMax = Raw;
			FoldMaxBone = Species.Mesh->GetRefSkeleton().GetBoneName(F.Mid);
		}
		// Eased in and out (each stride's folds come and go rather than pop), the side kept from when it was last bent.
		F.Shown = FoldScale <= 0.f ? 0.f : bFoldSnap ? Raw : FMath::FInterpTo(F.Shown, Raw, DeltaSeconds, 12.f);
		const float Bend = F.Shown * FoldScale;
		const FVector LowAtRest = (RefComponent(F.Upper).GetRotation() * CS[F.Upper].GetRotation().Inverse()).RotateVector(Low);
		const FVector Bent = (LowAtRest - F.LowDir * FVector::DotProduct(LowAtRest, F.LowDir)).GetSafeNormal();
		if (Raw > 0.05f && !Bent.IsNearlyZero())
		{
			F.Closes = Bent;
		}
		const FVector Closes = F.Closes;
		if (Bend <= 0.001f || Closes.IsNearlyZero())
		{
			continue;
		}
		FA[i] = FLinearColor(F.Joint.X, F.Joint.Y, F.Joint.Z, Bend);
		FB[i] = FLinearColor(Closes.X, Closes.Y, Closes.Z, F.ZoneCm);
		FC[i] = FLinearColor(F.LowDir.X, F.LowDir.Y, F.LowDir.Z, 0.f);
		if (Raw >= FoldMax)
		{
			FoldNote = FString::Printf(TEXT("joint %s at (%.0f, %.0f, %.0f), bend %.2f, closes toward (%.2f, %.2f, %.2f), lower bone (%.2f, %.2f, %.2f), zone %.0f cm"),
				*FoldMaxBone.ToString(), F.Joint.X, F.Joint.Y, F.Joint.Z, Bend, Closes.X, Closes.Y, Closes.Z, F.LowDir.X, F.LowDir.Y, F.LowDir.Z, F.ZoneCm);
		}
	}
	static const bool bFoldDebug = FParse::Param(FCommandLine::Get(), TEXT("bffolddebug"));
	if (bFoldDebug && FoldJoints.Num() > 0)
	{
		// One fold zone over the whole body, fully bent, closing upward: ridges everywhere if the material has them.
		const FFoldJoint& F = FoldJoints[0];
		FA[0] = FLinearColor(F.Joint.X, F.Joint.Y, F.Joint.Z, 1.f);
		FB[0] = FLinearColor(0.f, 0.f, 1.f, 3000.f);
		FC[0] = FLinearColor(1.f, 0.f, 0.f, 0.f);
	}
	bFoldSnap = false;
	static const FName FoldA[6] = { TEXT("FoldA0"), TEXT("FoldA1"), TEXT("FoldA2"), TEXT("FoldA3"), TEXT("FoldA4"), TEXT("FoldA5") };
	static const FName FoldB[6] = { TEXT("FoldB0"), TEXT("FoldB1"), TEXT("FoldB2"), TEXT("FoldB3"), TEXT("FoldB4"), TEXT("FoldB5") };
	static const FName FoldC[6] = { TEXT("FoldC0"), TEXT("FoldC1"), TEXT("FoldC2"), TEXT("FoldC3"), TEXT("FoldC4"), TEXT("FoldC5") };
	for (UMaterialInstanceDynamic* D : SkinMids)
	{
		if (D)
		{
			for (int32 i = 0; i < 6; ++i)
			{
				D->SetVectorParameterValue(NameA[i], SA[i]);
				D->SetVectorParameterValue(NameB[i], SB[i]);
				D->SetVectorParameterValue(FoldA[i], FA[i]);
				D->SetVectorParameterValue(FoldB[i], FB[i]);
				D->SetVectorParameterValue(FoldC[i], FC[i]);
			}
		}
	}
}

float ABFDino::GetBreathScale() const
{
	return BFBreathScale(BreathPhase, 1.f + 1.5f * Exertion);
}

FVector ABFDino::GetBoneWorld(FName Bone) const
{
	const USkinnedMeshComponent* M = Shown();
	return M && M->GetBoneIndex(Bone) != INDEX_NONE ? M->GetBoneLocation(Bone) : GetActorLocation();
}

void ABFDino::TickLife(float DeltaSeconds, FBFPoseInput& In, float& IdleMouth)
{
	const float Speed = GetVelocity().Size2D();
	const bool bGround = GetCharacterMovement()->IsMovingOnGround();

	// Breathing: running (faster than its walk) and swimming wind it over about 15 s; resting it gets its breath back
	// over about 25. Winded, it breathes up to 2.5 times as fast and as deep.
	const bool bHard = (bGround && Speed > Species.Walk.SpeedCm * 1.2f) || (bSwimming && Speed > 0.3f * Species.SwimSpeedCm) || bFlying;
	Exertion = FMath::Clamp(Exertion + DeltaSeconds * (bHard ? 1.f / (15.f * Endurance) : -1.f / 25.f), 0.f, 1.f);
	// On its feet and moving, the breath keeps time with the legs: one every two strides at its walk, one a stride at its
	// top speed. Standing, the resting rate above.
	const float MoveU = bGround ? FMath::Clamp(Speed / FMath::Max(0.6f * Species.Walk.SpeedCm, 1.f), 0.f, 1.f) : 0.f;
	const float RunU = FMath::Clamp((Speed - Species.Walk.SpeedCm) / FMath::Max(Species.Fast.SpeedCm - Species.Walk.SpeedCm, 1.f), 0.f, 1.f);
	const float LegsCm = Poser.StrideCm(In.GaitT);
	const float RestHz = BreathRate * (1.f + 1.5f * Exertion);
	const float LegsHz = LegsCm > 1.f ? FMath::Lerp(0.5f, 1.f, RunU) * Speed / LegsCm : 0.f;
	const float BreathHz = FMath::Lerp(RestHz, FMath::Max(RestHz, LegsHz), MoveU);
	BreathPhase = FMath::Fmod(BreathPhase + BreathHz * DeltaSeconds, 1000.f);
	In.Breath = BreathPhase;
	In.BreathDepth = 1.f + 1.5f * Exertion;

	// The mouth breathes with it (BFBreathMouth), eased so a change of pace doesn't jerk the jaw; a bite clears it out of
	// the way fast, so the jaw can snap shut. Open at speed, each footfall jolts the slack jaw a little further down.
	const bool bBiting = BiteClock >= 0.f;
	MouthMoving = FMath::FInterpTo(MouthMoving, bBiting ? 0.f : MoveU, DeltaSeconds, bBiting ? 14.f : 3.f);
	MouthEffort = FMath::FInterpTo(MouthEffort, RunU, DeltaSeconds, 3.f);
	// Breathing fast (a small animal at a sprint: the raptor's 3.4 breaths a second) the jaw hangs open and barely pumps:
	// fully shutting and opening that often read as chattering. From 1.5 breaths a second to 3.5, the pump falls to a quarter.
	const float Pump = 1.f - 0.75f * FMath::SmoothStep(1.5f, 3.5f, BreathHz);
	In.Jaw = MouthMoving * BFBreathMouth(BreathPhase, MouthEffort, Exertion, Pump) * (1.f + 0.15f * MouthEffort * (0.5f - 0.5f * FMath::Cos(4.f * PI * Phase)));

	// Speeding up and slowing down: the forward acceleration (a share of its top speed per second) drives a spring
	// that overshoots a little and settles, so the neck and tail lag, then swing back.
	const float Fwd = bGround ? FVector::DotProduct(GetVelocity(), GetActorForwardVector()) : 0.f;
	const float Accel = DeltaSeconds > 0.f ? (Fwd - LastFwdSpeed) / DeltaSeconds : 0.f;
	LastFwdSpeed = Fwd;
	const float Want = FMath::Clamp(0.8f * Accel / FMath::Max(Species.Fast.SpeedCm, 100.f), -1.f, 1.f);
	const float W = 7.f, Zeta = 0.4f;
	const float Dt = FMath::Min(DeltaSeconds, 1.f / 30.f);
	SurgeVel += (W * W * (Want - Surge) - 2.f * Zeta * W * SurgeVel) * Dt;
	Surge = FMath::Clamp(Surge + SurgeVel * Dt, -1.f, 1.f);
	In.Surge = bGround ? Surge : 0.f;
	In.Lumber = 1.f;
	IdleHold = FMath::FInterpConstantTo(IdleHold, IdleAct != EBFIdle::None ? 1.f : 0.f, DeltaSeconds, 8.f);
	In.WingsOnly = Species.Flight == EBFFlight::Grounded ? 1.f : 0.f;

	// The belly: the body's bounce (the hips dip twice a stride, FBFPoser) accelerates it up and down and the belly
	// lags, sags and swings back; landing from a fall or a hop drops it. Sag'' = -a_body - w^2 Sag - 2 z w Sag'.
	{
		const float Stride = Poser.StrideCm(In.GaitT);
		const float StrideHz = Stride > 1.f ? FMath::Max(Speed, bSwimming ? 0.5f * Species.SwimSpeedCm : 0.f) / Stride : 0.f;
		const float BobAmp = 0.5f * 0.025f * (Species.Limbs.Num() ? Species.Limbs[0].LengthCm : 0.f) * MoveBlend * (1.f - FlyBlend);
		const float WB = 4.f * PI * StrideHz;
		// The hips' height is -Bob = -BobAmp (1 - cos(4 pi phase)); its acceleration:
		const float BodyAccel = -BobAmp * WB * WB * FMath::Cos(4.f * PI * Phase);
		if (bGround && !bWasOnGround)
		{
			SagVel += 0.3f * LastFallSpeed;      // landing
		}
		bWasOnGround = bGround;
		LastFallSpeed = FMath::Max(-GetVelocity().Z, 0.f);
		const float WS = 2.f * PI * SagHz, ZS = 0.25f;
		const float Steps = FMath::Max(1.f, FMath::CeilToFloat(DeltaSeconds / (1.f / 120.f)));
		const float H = DeltaSeconds / Steps;
		for (int32 s = 0; s < (int32)Steps; ++s)
		{
			SagVel += (BodyAccel - WS * WS * Sag - 2.f * ZS * WS * SagVel) * H;
			Sag += SagVel * H;
		}
		Sag = FMath::Clamp(Sag, -SagMax, SagMax);
		for (UMaterialInstanceDynamic* M : SkinMids)
		{
			if (M)
			{
				M->SetScalarParameterValue(TEXT("Jiggle"), Sag);
			}
		}
	}

	// Out of the water it wants to shake off.
	if (bWasInWater && !bInWater)
	{
		bWet = true;
		DryClock = 0.f;
	}
	bWasInWater = bInWater;
	DryClock += DeltaSeconds;
	if (DryClock > 30.f)
	{
		bWet = false;      // dried off without shaking
	}

	// Idle behaviours: only standing still on its own feet, not biting or roaring.
	const bool bStill = bGround && !bFlying && !bRagdoll && !bInWater && !bClips && MoveBlend < 0.05f && BiteClock < 0.f && Mouth < 0.05f
		&& Species.Legs == EBFLegs::Walks;
	StillTime = bStill ? StillTime + DeltaSeconds : 0.f;
	if (!bStill)
	{
		IdleAct = EBFIdle::None;
	}
	else if (IdleAct == EBFIdle::None && bIdleOn && StillTime > IdleWait)
	{
		if (bWet)
		{
			IdleAct = EBFIdle::Shake;
		}
		else
		{
			// Looking round most often, the plant eaters feeding about as often; never the same one twice running.
			const float Weights[(int32)EBFIdle::Count] = { 0.f, 0.3f, 0.2f, 0.2f, 0.15f, 0.15f, 0.f, CanFeed() ? 0.25f : 0.f };
			float Total = 0.f;
			for (const float Weight : Weights)
			{
				Total += Weight;
			}
			do
			{
				float R = IdleRand.FRand() * Total;
				int32 Pick = 1;
				while (Pick < (int32)EBFIdle::Count - 1 && R > Weights[Pick]) { R -= Weights[Pick]; ++Pick; }
				IdleAct = (EBFIdle)Pick;
			} while (IdleAct == LastIdle);
		}
		IdleClock = 0.f;
		IdleSide = IdleRand.FRand() < 0.5f ? -1.f : 1.f;
	}
	// Winded and standing, it cools off with its mouth a little open, the throat fluttering (the design doc: as birds do).
	if (bStill && Exertion > 0.2f)
	{
		In.Jaw = FMath::Max(In.Jaw, FMath::Min(StillTime / 0.5f, 1.f) * FMath::Min(Exertion, 1.f) * 0.16f * (0.75f + 0.25f * FMath::Sin(2.f * PI * 6.f * GetWorld()->GetTimeSeconds())));
	}
	if (IdleAct == EBFIdle::None)
	{
		return;
	}
	IdleClock += DeltaSeconds;
	const float Len = IdleSeconds(IdleAct);
	const float U = FMath::Clamp(IdleClock / Len, 0.f, 1.f);
	const float Env = FMath::SmoothStep(0.f, 0.15f, U) * (1.f - FMath::SmoothStep(0.8f, 1.f, U));
	switch (IdleAct)
	{
	case EBFIdle::Look:        // one way, then the other, most of the way its head and neck can turn; the body curves after it
	{
		// The big ones sweep slowly; the small ones (the design doc: Velociraptor, Pteranodon) snap their heads round
		// and hold, as birds do: one side, the other, back.
		float Way = IdleSide * FMath::Sin(2.f * PI * U) * FMath::SmoothStep(0.f, 0.1f, U);
		if (Species.MassKg < 200.f)
		{
			const auto Snap = [U](float At) { return FMath::SmoothStep(At, At + 0.04f, U); };
			Way = IdleSide * (0.9f * Snap(0.1f) - 1.6f * Snap(0.42f) + 0.7f * Snap(0.74f));
		}
		In.LookYaw += 0.75f * LookCap * Way;
		In.Turn = FMath::Clamp(In.Turn + 0.5f * Way, -1.f, 1.f);
		break;
	}
	case EBFIdle::Sniff:       // the head goes down, sniffs, comes back up; the tail rises to balance it (FBFPoser)
		In.NeckDown = Env;
		In.Sniff = FMath::SmoothStep(0.25f, 0.35f, U) * (1.f - FMath::SmoothStep(0.65f, 0.75f, U));
		In.SniffU = IdleClock * 4.f;
		break;
	case EBFIdle::Shift:       // onto one side's legs, then the other's
		In.Shift = IdleSide * FMath::Sin(2.f * PI * U);
		break;
	case EBFIdle::Swish:
		In.Swish = Env;
		In.SwishU = IdleClock * 0.8f;
		break;
	case EBFIdle::Yawn:        // opens slowly, holds, shuts
		IdleMouth = 0.85f * FMath::SmoothStep(0.f, 0.35f, U) * (1.f - FMath::SmoothStep(0.7f, 0.95f, U));
		break;
	case EBFIdle::Shake:       // quick to start, dying away
		In.Shake = FMath::SmoothStep(0.f, 0.08f, U) * (1.f - FMath::SmoothStep(0.55f, 1.f, U));
		In.ShakeU = IdleClock * 3.5f;
		break;
	case EBFIdle::Feed:        // the design doc's Movement tab, chewing (FBFSpecies::Feed says how this head eats)
	{
		const auto Bump = [U](float A, float B, float C, float D) { return FMath::SmoothStep(A, B, U) * (1.f - FMath::SmoothStep(C, D, U)); };
		// Cropping: the jaw opens on the plants, snaps shut, and the head tugs the mouthful free.
		const float Crop = 0.5f * Bump(0.2f, 0.27f, 0.29f, 0.31f);
		const float Tug = Bump(0.31f, 0.34f, 0.36f, 0.42f);
		const float Swallow = Bump(0.86f, 0.89f, 0.9f, 0.95f);
		const float T = IdleClock - 0.42f * Len;       // seconds into chewing
		float Chew = 0.f;
		if (SipHoldS > 0.f)
		{
			// Drinking: the lips at the water, sipping (the jaw working a little, the head dipping with each draw), and
			// now and then the head lifted to swallow, the jaw shut.
			SipHoldS -= GetWorld()->GetDeltaSeconds();
			const float Sip = 0.5f - 0.5f * FMath::Cos(2.f * PI * 0.9f * IdleClock);
			const float Gulp = Bump(0.45f, 0.5f, 0.56f, 0.62f) + Swallow;
			IdleMouth = (0.05f + 0.07f * Sip) * (1.f - FMath::Min(Gulp, 1.f));
			In.NeckDown = FMath::Max(In.NeckDown - 0.05f * (1.f - Sip) - 0.3f * FMath::Min(Gulp, 1.f), 0.f);
			break;
		}
		if (Species.Feed == EBFFeed::Shear)
		{
			// The Triceratops chews, about a stroke a second, its tooth rows shearing past each other as the jaw closes
			// (up and down: its jaw has no side-to-side range in the rig, and ceratopsian teeth sliced vertically).
			Chew = U > 0.42f && U < 0.86f ? 0.22f * (0.5f - 0.5f * FMath::Cos(2.f * PI * 1.1f * T)) : 0.f;
		}
		else
		{
			// The Ankylosaurus and Brachiosaurus snip and swallow: a couple of small movements of the jaw, then a gulp.
			Chew = U > 0.42f && U < 0.55f ? 0.08f * (0.5f - 0.5f * FMath::Cos(2.f * PI * 1.6f * T)) : 0.f;
		}
		IdleMouth = FMath::Max(Crop, Chew);
		if (bMouthTarget)
		{
			// (Where its mouth goes is the target's: TickPose reaches for it. Only the tug and the swallow here.)
			In.NeckDown = FMath::Max(In.NeckDown - 0.15f * Tug - 0.1f * Swallow, 0.f);
			if (Species.Feed == EBFFeed::Browse && FeedReach < 0.5f)
			{
				// Up in the leaves: it grips a spray and strips it, the head drawn along the branch to one side and
				// down, the branch pulled after it.
				const float Strip = Bump(0.3f, 0.36f, 0.4f, 0.5f);
				In.LookYaw += IdleSide * 0.3f * LookCap * Strip;
				In.NeckDown += 0.18f * Strip;
				IdleMouth = FMath::Max(IdleMouth, 0.f);
			}
		}
		else if (Species.Feed == EBFFeed::Browse)
		{
			// Up into the trees: the neck raised to about half its upward range, stripping the leaves pulls the head
			// back down a little, and it swallows with its head up.
			float Up = FMath::Max(Species.LimitsOf(TEXT("head")).Bend.Y, 0.f);
			for (const FName B : Species.Neck)
			{
				Up += FMath::Max(Species.LimitsOf(B).Bend.Y, 0.f);
			}
			In.NeckRaise = FMath::Max(In.NeckRaise, 0.45f * Up * (Env - 0.12f * Tug + 0.05f * Swallow));
		}
		else
		{
			// Down to the plants (a sniff at them first), the head up a little while it chews and swallows.
			In.NeckDown = FMath::Max(Env * (1.f - 0.3f * FMath::SmoothStep(0.38f, 0.46f, U)) - 0.15f * Tug - 0.1f * Swallow, 0.f);
			In.Sniff = Bump(0.1f, 0.13f, 0.18f, 0.2f);
			In.SniffU = IdleClock * 4.f;
		}
		break;
	}
	default:
		break;
	}
	if (IdleClock >= Len)
	{
		if (IdleAct == EBFIdle::Shake)
		{
			bWet = false;
		}
		LastIdle = IdleAct;
		IdleAct = EBFIdle::None;
		StillTime = 0.f;
		IdleWait = IdleRand.FRandRange(4.f, 9.f);
	}
}

// ---------------------------------------------------------------------------------------------- baked clips

USkinnedMeshComponent* ABFDino::Shown() const
{
	return bClips ? static_cast<USkinnedMeshComponent*>(GetMesh()) : static_cast<USkinnedMeshComponent*>(Body);
}

void ABFDino::HideImportedMembrane(USkinnedMeshComponent* M) const
{
	if (const FSkeletalMeshRenderData* RD = Species.Mesh->GetResourceForRendering())
	{
		const TArray<FSkeletalMaterial>& Mats = Species.Mesh->GetMaterials();
		for (int32 s = 0; RD->LODRenderData.Num() > 0 && s < RD->LODRenderData[0].RenderSections.Num(); ++s)
		{
			const int32 Mat = RD->LODRenderData[0].RenderSections[s].MaterialIndex;
			if (Mats.IsValidIndex(Mat) && Mats[Mat].MaterialSlotName.ToString().Contains(TEXT("Membrane")))
			{
				M->ShowMaterialSection(Mat, s, false, 0);
			}
		}
	}
}

void ABFDino::SetClips(bool bOn)
{
	if (bRagdoll || bOn == bClips || (bOn && !Species.HasClips()))
	{
		return;
	}
	bClips = bOn;
	USkeletalMeshComponent* M = GetMesh();
	if (bClips)
	{
		M->SetSkeletalMeshAsset(Species.Mesh);
		M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		M->SetRelativeTransform(Body->GetRelativeTransform());
		if (Species.CanFly())
		{
			HideImportedMembrane(M);
		}
		Playing = nullptr;
		TickClips(0.f);
	}
	else
	{
		M->Stop();
	}
	M->SetVisibility(bClips);
	Body->SetVisibility(!bClips);
}

void ABFDino::TickClips(float DeltaSeconds)
{
	USkeletalMeshComponent* M = GetMesh();
	M->SetRelativeTransform(Body->GetRelativeTransform());   // AlignToGround tilts and lifts the Body; the mesh follows it

	// Q starts a roar here (the live pose holds the mouth open instead).
	if (bMouthOpen && !bRoarHeld && RoarClock < 0.f)
	{
		RoarClock = 0.f;
	}
	bRoarHeld = bMouthOpen;
	if (RoarClock >= 0.f)
	{
		RoarClock += DeltaSeconds;
		if (RoarClock > BFRoarSeconds)
		{
			RoarClock = -1.f;
		}
	}

	UObject* Want = Species.Locomotion;
	bool bLoop = true;
	float X = GetVelocity().Size2D();
	if (BiteClock >= 0.f && BiteClock <= BFBiteSeconds && Species.BiteAnim)
	{
		Want = Species.BiteAnim;
		bLoop = false;
	}
	else if (RoarClock >= 0.f && Species.RoarAnim)
	{
		Want = Species.RoarAnim;
		bLoop = false;
	}
	else if (FlyBlend > 0.5f && Species.FlightBlend)
	{
		Want = Species.FlightBlend;
		X = FlapAmount;
	}
	if (Want != Playing)
	{
		M->PlayAnimation(Cast<UAnimationAsset>(Want), bLoop);
		Playing = Want;
	}
	if (UAnimSingleNodeInstance* I = M->GetSingleNodeInstance(); I && Cast<UBlendSpace>(Want))
	{
		I->SetBlendSpacePosition(FVector(X, 0.f, 0.f));
	}
}

FString ABFDino::GetClipName() const
{
	return bClips && Playing ? Playing->GetName() : TEXT("live pose");
}

void ABFDino::AlignToGround(float DeltaSeconds)
{
	const UCapsuleComponent* Cap = GetCapsuleComponent();
	const float HalfHeight = Cap->GetScaledCapsuleHalfHeight();
	const FVector Size = Species.Bounds.GetSize();
	const FVector Loc = GetActorLocation();
	const FVector Fwd = GetActorForwardVector();
	const FVector Right = GetActorRightVector();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BFGround), false, this);
	auto GroundZ = [&](const FVector& At, float& OutZ, FVector* OutNormal = nullptr)
	{
		FHitResult Hit;
		const FVector Start(At.X, At.Y, Loc.Z + HalfHeight * 2.f);
		const FVector End(At.X, At.Y, Loc.Z - HalfHeight * 3.f);
		if (TraceGround(Start, End, Hit, Params))
		{
			OutZ = Hit.ImpactPoint.Z;
			if (OutNormal)
			{
				*OutNormal = Hit.ImpactNormal;
			}
			return true;
		}
		return false;
	};

	const float Reach = FMath::Max(Size.X * 0.3f, 20.f);
	const float Half = FMath::Max(Size.Y * 0.5f, 10.f);
	float ZF, ZB, ZL, ZR, ZC;
	FVector Normal = FVector::UpVector;
	float TargetPitch = 0.f, TargetRoll = 0.f;
	if (GroundZ(Loc + Fwd * Reach, ZF) && GroundZ(Loc - Fwd * Reach, ZB))
	{
		TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(ZF - ZB, 2.f * Reach));
	}
	// On both pairs, the legs held upright under hips and shoulders: pitched so each pair keeps its standing height
	// over the ground under its own feet. Pitched by the slope itself, a body that stands tilted (a T. rex on
	// Brachiosaurus arms, 31° head-up) raised its shoulders more than the ground rose under its front feet, and they
	// lost it. For a level back the two are the same.
	float ZH, ZS;
	const float Along = QuadSpanCm * FMath::Cos(FMath::DegreesToRadians(QuadRestPitch + BodyPitch));
	if (bQuadPitch && !bFlying && GroundZ(Loc + Fwd * HipFwdCm, ZH) && GroundZ(Loc + Fwd * (HipFwdCm + FMath::Max(Along, 1.f)), ZS))
	{
		TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(ZS - ZH + QuadRiseCm, FMath::Max(Along, 1.f))) - QuadRestPitch;
	}
	if (GroundZ(Loc - Right * Half, ZL) && GroundZ(Loc + Right * Half, ZR))
	{
		TargetRoll = -FMath::RadiansToDegrees(FMath::Atan2(ZR - ZL, 2.f * Half));
	}
	TargetPitch -= FeedDip;          // reaching lower than its neck goes: its front down on bent forelegs
	BodyPitch = FMath::FInterpTo(BodyPitch, FMath::Clamp(TargetPitch, -40.f, 40.f), DeltaSeconds, 5.f);
	BodyRoll = FMath::FInterpTo(BodyRoll, FMath::Clamp(TargetRoll, -20.f, 20.f), DeltaSeconds, 5.f);

	// The capsule's round bottom rides above a slope; put the hips' ground point back on the surface.
	float BaseZ = -HalfHeight - Species.Bounds.Min.Z;
	if (!bFlying && GroundZ(Loc, ZC, &Normal))
	{
		BaseZ = FMath::Clamp(ZC - Loc.Z, -HalfHeight * 1.5f, -HalfHeight * 0.5f) - Species.Bounds.Min.Z;
		GroundSlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.f, 1.f)));
	}
	// Facing uphill (the slope's normal leans back toward it), it keeps the climb check's share of its speed.
	const FVector Downhill = FVector(Normal.X, Normal.Y, 0.f).GetSafeNormal();
	const float Uphill = -FVector::DotProduct(Downhill, Fwd.GetSafeNormal2D());
	// Downhill on a steep slope it goes carefully (a heavy animal most): shorter, slower steps, down to 55% of its pace.
	const float Down = !bFlying && Uphill < 0.f ? -Uphill * FMath::SmoothStep(14.f, 32.f, GroundSlopeDeg) : 0.f;
	const float Careful = 1.f - Down * FMath::Lerp(0.25f, 0.45f, FMath::Clamp(Species.MassKg / 8000.f, 0.f, 1.f));
	const float PaceNow = !bFlying && Uphill > 0.f ? FMath::Lerp(1.f, Species.ClimbPaceAt(GroundSlopeDeg), Uphill) : Careful;
	SlopePace = FMath::FInterpTo(SlopePace, PaceNow, DeltaSeconds, 3.f);
	GroundRelZ = FMath::FInterpTo(GroundRelZ, BaseZ, DeltaSeconds, 10.f);
	FVector Rel(0.f, 0.f, GroundRelZ);
	FRotator Rot(BodyPitch, 0.f, BodyRoll);

	// Standing on a slope, an animal keeps its legs upright under its weight (Prototype 2: tilted whole with the
	// ground, one facing uphill had its weight behind its feet and would have tipped over backwards). The body pitches
	// about the hips instead of the ground under them, and FBFPoser swings the legs back by that pitch, as far as the
	// hips and shoulders allow; leg IK (SolveLegIK) puts the feet on the ground. Walking keeps the stride along the
	// slope, with the IK fixing each foot, and the baked clips play as they were baked.
	Upright = bClips ? 0.f : (1.f - MoveBlend) * (1.f - FlyBlend);
	LegLean = FMath::Clamp((BodyPitch + StepBack) * Upright, LegLeanWideLo, LegLeanWideHi);   // (each leg as far as its own joint goes: FBFPoser)
	const FVector Hips(0.f, 0.f, PelvisZ);
	Rel += Upright * (Hips - FRotator(BodyPitch, 0.f, 0.f).RotateVector(Hips));
	const float BallAhead = BallAheadCm - HindLegCm * FMath::Sin(FMath::DegreesToRadians(StepBack));
	Rel.Z += Upright * BallAhead * FMath::Tan(FMath::DegreesToRadians(BodyPitch)) + HipDrop;
	// Lying: lowered by most of its leg's height (to its belly on the ground), eased (in, then settling).
	Rel.Z -= FMath::SmoothStep(0.f, 1.f, LieBlend) * 0.72f * HindLegCm;

	// Floating, the body turns about its centre of mass to the swim check's attitude (a tippy one rocks as it paddles).
	if (SwimBlend > 0.001f)
	{
		const float Rock = Species.bSwimTippy ? 6.f * FMath::Sin(2.f * PI * SwimClock * 0.35f) : 1.5f * FMath::Sin(2.f * PI * SwimClock * 0.35f);
		const FRotator SwimRot(Species.SwimPitchDeg, 0.f, Rock);
		const FVector Rest(0.f, 0.f, -HalfHeight - Species.Bounds.Min.Z);
		const FVector SwimLoc = RestCoM - SwimRot.RotateVector(RestCoM - Rest);
		Rot = FMath::Lerp(Rot, SwimRot, SwimBlend);
		Rel = FMath::Lerp(Rel, SwimLoc, SwimBlend);
	}

	// In flight the body turns about the hips, which sit at the capsule's centre: nose along the
	// flight path, banked into turns.
	if (FlyBlend > 0.001f)
	{
		const FRotator FlyRot(FlightPitch, 0.f, Bank);
		const FVector FlyLoc = -FlyRot.RotateVector(FVector(0.f, 0.f, PelvisZ));
		Rot = FMath::Lerp(Rot, FlyRot, FlyBlend);
		Rel = FMath::Lerp(Rel, FlyLoc, FlyBlend);
	}
	Body->SetRelativeRotation(Rot);
	Body->SetRelativeLocation(Rel);
}
