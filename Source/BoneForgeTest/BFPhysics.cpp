#include "BFPhysics.h"

#include "BFSpecies.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/Package.h"

/** Shortest distance between segments P1-Q1 and P2-Q2. */
static float SegmentDistance(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2)
{
	FVector A, B;
	FMath::SegmentDistToSegmentSafe(P1, Q1, P2, Q2, A, B);
	return (A - B).Size();
}

static TArray<FTransform> ComponentSpaceRef(const FReferenceSkeleton& RS)
{
	const TArray<FTransform>& Local = RS.GetRefBonePose();
	TArray<FTransform> CS;
	CS.SetNum(Local.Num());
	for (int32 i = 0; i < Local.Num(); ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		CS[i] = P == INDEX_NONE ? Local[i] : Local[i] * CS[P];
	}
	return CS;
}

UPhysicsAsset* UBFPhysics::Build(const FBFSpecies& S, UPhysicsAsset* PA, FString& Report)
{
	if (!PA)
	{
		PA = NewObject<UPhysicsAsset>(GetTransientPackage());
	}
	PA->SkeletalBodySetups.Empty();
	PA->ConstraintSetup.Empty();
	PA->CollisionDisableTable.Empty();

	const FReferenceSkeleton& RS = S.Mesh->GetRefSkeleton();
	const TArray<FTransform> CS = ComponentSpaceRef(RS);

	// Bodies: every capsule of a bone goes into that bone's body (the pelvis has two).
	TMap<FName, int32> BodyOf;
	TArray<TArray<const FBFCapsule*>> Shapes;
	TArray<float> Mass;
	for (const FBFCapsule& C : S.Capsules)
	{
		const int32 Bone = RS.FindBoneIndex(C.Bone);
		if (Bone == INDEX_NONE)
		{
			continue;
		}
		int32 Body = BodyOf.FindRef(C.Bone, INDEX_NONE);
		if (Body == INDEX_NONE)
		{
			USkeletalBodySetup* BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transactional);
			BS->BoneName = C.Bone;
			BS->PhysicsType = PhysType_Default;
			BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			BS->CollisionReponse = EBodyCollisionResponse::BodyCollision_Enabled;
			Body = PA->SkeletalBodySetups.Add(BS);
			BodyOf.Add(C.Bone, Body);
			Shapes.AddDefaulted();
			Mass.Add(0.f);
		}
		USkeletalBodySetup* BS = PA->SkeletalBodySetups[Body];
		const FVector A = CS[Bone].InverseTransformPosition(C.A);
		const FVector B = CS[Bone].InverseTransformPosition(C.B);
		FKSphylElem E;
		E.Center = (A + B) * 0.5f;
		E.Rotation = FRotationMatrix::MakeFromZ((B - A).GetSafeNormal()).Rotator();
		E.Radius = C.Radius;
		E.Length = FMath::Max((B - A).Size(), 1.f);   // cylinder part; the round ends reach a radius past each joint
		BS->AggGeom.SphylElems.Add(E);
		Shapes[Body].Add(&C);
		Mass[Body] += C.MassKg;
	}
	float Total = 0.f;
	for (int32 b = 0; b < PA->SkeletalBodySetups.Num(); ++b)
	{
		FBodyInstance& BI = PA->SkeletalBodySetups[b]->DefaultInstance;
		BI.SetMassOverride(FMath::Max(Mass[b], 0.01f), true);
		BI.LinearDamping = 0.2f;
		BI.AngularDamping = 2.f;
		// Light limbs on heavy bodies (a 0.3 kg raptor arm on a 3 kg chest) bend past their limits on a hard landing
		// with the default iterations.
		BI.SetPositionSolverIterationCount(32);
		BI.SetVelocitySolverIterationCount(4);
		BI.SetProjectionSolverIterationCount(4);
		Total += Mass[b];
	}

	// Constraints: each body to the nearest ancestor that has one, at the bone's joint, limited to limits_deg.
	// Bone axes: X along the bone (twist), Y to the right (Pitch = Swing2), Z up (Yaw = Swing1).
	int32 Locked = 0;
	for (const TPair<FName, int32>& KV : BodyOf)
	{
		const int32 Bone = RS.FindBoneIndex(KV.Key);
		int32 Parent = RS.GetParentIndex(Bone);
		while (Parent != INDEX_NONE && !BodyOf.Contains(RS.GetBoneName(Parent)))
		{
			Parent = RS.GetParentIndex(Parent);
		}
		if (Parent == INDEX_NONE)
		{
			continue;
		}
		const FBFLimits& L = S.LimitsOf(KV.Key);
		float BendSign, SideSign;
		BFBoneSigns(KV.Key, BendSign, SideSign);

		UPhysicsConstraintTemplate* CT = NewObject<UPhysicsConstraintTemplate>(PA, NAME_None, RF_Transactional);
		FConstraintInstance& CI = CT->DefaultInstance;
		CI.JointName = KV.Key;
		CI.ConstraintBone1 = KV.Key;
		CI.ConstraintBone2 = RS.GetBoneName(Parent);
		CI.SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
		// The limits are symmetric in Unreal, so the parent's frame is turned to the middle of each range: the
		// child at rest in that frame is the bone turned by the middle angles (AngularRotationOffset only does
		// this when edited in the editor; the physics ignores it).
		const FQuat Middle(FRotator(BendSign * (L.Bend.X + L.Bend.Y) * 0.5f, SideSign * (L.Side.X + L.Side.Y) * 0.5f,
			(L.Twist.X + L.Twist.Y) * 0.5f));
		const FTransform Rest = CS[Bone].GetRelativeTransform(CS[Parent]);
		CI.SetRefFrame(EConstraintFrame::Frame2, FTransform(Rest.GetRotation() * Middle, Rest.GetLocation()));
		CI.SetLinearLimits(LCM_Locked, LCM_Locked, LCM_Locked, 0.f);
		auto Range = [&](const FVector2D& R, auto SetLimit)
		{
			const float Half = (R.Y - R.X) * 0.5f;
			if (Half < 0.5f) { ++Locked; }
			SetLimit(Half < 0.5f ? ACM_Locked : ACM_Limited, FMath::Max(Half, 0.f));
		};
		Range(L.Side, [&](EAngularConstraintMotion M, float A) { CI.SetAngularSwing1Limit(M, A); });
		Range(L.Bend, [&](EAngularConstraintMotion M, float A) { CI.SetAngularSwing2Limit(M, A); });
		Range(L.Twist, [&](EAngularConstraintMotion M, float A) { CI.SetAngularTwistLimit(M, A); });
		CI.SetDisableCollision(true);
		// The non-linear joint solver, "slower and more convergent" than Chaos's default linear one: a heavy body
		// pressing a light limb into the ground pushed it 6-8° past its stop with the linear one (the raptor's arm).
		CI.ProfileInstance.bUseLinearJointSolver = false;
		CT->SetDefaultProfile(CI);
		PA->ConstraintSetup.Add(CT);
	}

	PA->UpdateBodySetupIndexMap();
	PA->UpdateBoundsBodiesArray();

	// Bodies that already overlap standing still would push each other apart: no collision between them.
	int32 Disabled = 0;
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		for (int32 j = i + 1; j < Shapes.Num(); ++j)
		{
			bool bOverlap = false;
			for (const FBFCapsule* A : Shapes[i])
			{
				for (const FBFCapsule* B : Shapes[j])
				{
					bOverlap |= SegmentDistance(A->A, A->B, B->A, B->B) < A->Radius + B->Radius;
				}
			}
			if (bOverlap)
			{
				PA->DisableCollision(i, j);
				++Disabled;
			}
		}
	}
#if WITH_EDITORONLY_DATA
	PA->PreviewSkeletalMesh = S.Mesh;
#endif
	Report = FString::Printf(TEXT("%s: %d bodies, %d constraints (%d locked axes), %d overlapping pairs not colliding, %.1f kg"),
		*S.Key, PA->SkeletalBodySetups.Num(), PA->ConstraintSetup.Num(), Locked, Disabled, Total);
	return PA;
}

FString UBFPhysics::CreatePhysicsAsset(const FString& RigFolder, const FString& Key)
{
	FBFSpecies S;
	FString Error;
	if (!S.Load(RigFolder, Key, Error))
	{
		return TEXT("ERROR: ") + Error;
	}
	const FString Cap = Key.Left(1).ToUpper() + Key.Mid(1);
	const FString Name = Key + TEXT("_physics");
	const FString PackageName = FString::Printf(TEXT("/Game/BoneForge/%s/%s_skeletal/%s"), *Cap, *Key, *Name);
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UPhysicsAsset* PA = FindObject<UPhysicsAsset>(Package, *Name);
	if (!PA)
	{
		PA = NewObject<UPhysicsAsset>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
	}
	FString Report;
	Build(S, PA, Report);
	S.Mesh->SetPhysicsAsset(PA);
	S.Mesh->MarkPackageDirty();
	Package->MarkPackageDirty();
	return Report + TEXT(" -> ") + PA->GetPathName();
}
