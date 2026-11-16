#include "BFPoser.h"

#include "Engine/SkeletalMesh.h"

void FBFPoser::Init(const FBFSpecies& InSpecies)
{
	Species = &InSpecies;
	RefPose = Species->Mesh->GetRefSkeleton().GetRefBonePose();
	FlightLocal.Reset();
	WingBones.Reset();
	if (Species->CanFly())
	{
		BuildFlightPose();
		const FReferenceSkeleton& WS = Species->Mesh->GetRefSkeleton();
		for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
		{
			const int32 Top = WS.FindBoneIndex(FName(*FString::Printf(TEXT("upperarm%s_%s"), *Species->WingPair, Side)));
			for (int32 i = 0; Top != INDEX_NONE && i < WS.GetNum(); ++i)
			{
				for (int32 p = i; p != INDEX_NONE; p = WS.GetParentIndex(p))
				{
					if (p == Top) { WingBones.Add(i); break; }
				}
			}
		}
	}

	const FReferenceSkeleton& RS = Species->Mesh->GetRefSkeleton();
	TArray<FTransform> CS;
	CS.SetNum(RefPose.Num());
	for (int32 i = 0; i < RefPose.Num(); ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		CS[i] = P == INDEX_NONE ? RefPose[i] : RefPose[i] * CS[P];
	}
	const int32 NeckBase = Species->Neck.Num() ? RS.FindBoneIndex(Species->Neck[0]) : INDEX_NONE;
	const int32 Head = RS.FindBoneIndex(TEXT("head"));
	NeckLenCm = NeckBase != INDEX_NONE && Head != INDEX_NONE ? (CS[Head].GetLocation() - CS[NeckBase].GetLocation()).Size() : 0.f;
	// Breathing: the ribcage (spine_02, spine_03, chest) swells as one; what hangs off it keeps its own size.
	TArray<int32> Ribs;
	for (const TCHAR* B : { TEXT("spine_02"), TEXT("spine_03"), TEXT("chest") })
	{
		const int32 I = RS.FindBoneIndex(B);
		if (I != INDEX_NONE)
		{
			Ribs.Add(I);
		}
	}
	BreathBone = Ribs.Num() ? Ribs[0] : INDEX_NONE;
	// Heavy animals lumber: their hips sway out over each stance leg, more the heavier they are (an elephant's sway is
	// a few centimetres); light ones run too fast to sway. 1% of hip height per tenfold mass over 50 kg, at most 2%.
	LumberCm = Species->Limbs.Num() ? FMath::Clamp(0.01f * FMath::LogX(10.f, FMath::Max(Species->MassKg, 1.f) / 50.f), 0.f, 0.02f)
		* Species->Limbs[0].LengthCm : 0.f;
	// Two-legged ones follow the design doc (Movement tab, weight shifts onto the planted foot): the hips sway toward
	// the standing leg by about a sixth of the hip width, side to side (so half that each way).
	const int32 HipL = RS.FindBoneIndex(TEXT("thigh_l")), HipR = RS.FindBoneIndex(TEXT("thigh_r"));
	if (!Species->bQuadruped && HipL != INDEX_NONE && HipR != INDEX_NONE)
	{
		LumberCm = FVector::Dist(CS[HipL].GetLocation(), CS[HipR].GetLocation()) / 12.f;
	}
	CarriedArms.Reset();
	for (const TCHAR* Pair : { TEXT(""), TEXT("_2") })
	{
		for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
		{
			const FName Upper(*FString::Printf(TEXT("upperarm%s_%s"), Pair, Side));
			const FName Lower(*FString::Printf(TEXT("lowerarm%s_%s"), Pair, Side));
			const bool bWalks = Species->Limbs.ContainsByPredicate([&](const FBFLimb& L) { return L.Upper == Upper; });
			if (!bWalks && !Species->FlightPose.Contains(Upper) && RS.FindBoneIndex(Upper) != INDEX_NONE)
			{
				CarriedArms.Add(TPair<FName, FName>(Upper, Lower));
			}
		}
	}
	BreathKeep.Reset();
	for (int32 i = 0; i < RS.GetNum(); ++i)
	{
		if (Ribs.Contains(RS.GetParentIndex(i)) && !Ribs.Contains(i))
		{
			BreathKeep.Add(i);
		}
	}
}

float FBFPoser::StrideCm(float GaitT) const
{
	return FMath::Max(FMath::Lerp(Species->Walk.StrideCm, Species->Fast.StrideCm, GaitT), 1.f);
}

void FBFPoser::AddBoneAngles(TArray<FTransform>& Local, FName Bone, float BendDeg, float SideDeg) const
{
	const int32 Index = Species->Mesh->GetRefSkeleton().FindBoneIndex(Bone);
	if (!Local.IsValidIndex(Index))
	{
		return;
	}
	// Bone Forge axes (unreal/README.md): local X points at the child joint, Y toward the creature's right.
	// bend = Pitch (negative for tail, knee, wrist, jaw); side = Yaw (neck/spine/head -, tail +, limbs - on _l, + on _r).
	float PitchSign, YawSign;
	BFBoneSigns(Bone, PitchSign, YawSign);
	const FQuat Delta(FRotator(PitchSign * BendDeg, YawSign * SideDeg, 0.f));
	FTransform& T = Local[Index];
	T.SetRotation(T.GetRotation() * Delta);
}

void FBFPoser::AddBoneRoll(TArray<FTransform>& Local, FName Bone, float RollDeg) const
{
	const int32 Index = Species->Mesh->GetRefSkeleton().FindBoneIndex(Bone);
	if (Local.IsValidIndex(Index) && RollDeg != 0.f)
	{
		Local[Index].SetRotation(Local[Index].GetRotation() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(RollDeg)));
	}
}

FQuat FBFPoser::ClampToLimits(FName Bone, const FQuat& RefRot, const FQuat& Rot) const
{
	const FBFLimits* L = Species->Limits.Find(Bone);
	if (!L)
	{
		return Rot;
	}
	// Same reading as BFMeasureJoint: bend and side from where the bone points, twist dropped.
	float Bend, Side;
	BFMeasureJoint(Bone, RefRot, Rot, Bend, Side);
	auto Clamp = [](float A, const FVector2D& R) -> float
	{
		if (A >= R.X && A <= R.Y)
		{
			return A;
		}
		// Past the range: stop at whichever end is nearer the long way round too (171 is 9 from 180 = -180).
		const float ToMin = FMath::Abs(FMath::FindDeltaAngleDegrees(A, R.X));
		const float ToMax = FMath::Abs(FMath::FindDeltaAngleDegrees(A, R.Y));
		return ToMin < ToMax ? R.X : R.Y;
	};
	Bend = Clamp(Bend, L->Bend);
	Side = Clamp(Side, L->Side);
	float BendSign, SideSign;
	BFBoneSigns(Bone, BendSign, SideSign);
	const float B = FMath::DegreesToRadians(BendSign * Bend);
	const float Sd = FMath::DegreesToRadians(SideSign * Side);
	const FVector Dir(FMath::Cos(Sd) * FMath::Cos(B), FMath::Sin(Sd), FMath::Cos(Sd) * FMath::Sin(B));
	return (RefRot * FQuat::FindBetweenNormals(FVector::ForwardVector, Dir)).GetNormalized();
}

void FBFPoser::BuildFlightPose()
{
	// Local bone rotations that put the joints where the rig's flight_pose_cm has them: walk the
	// skeleton from the root, turning each bone (and everything below it) so it points at its child.
	const FReferenceSkeleton& RS = Species->Mesh->GetRefSkeleton();
	const int32 N = RS.GetNum();
	TArray<FTransform> CS;
	CS.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		CS[i] = P == INDEX_NONE ? RefPose[i] : RefPose[i] * CS[P];
	}
	TMap<FName, FVector> Target = Species->FlightPose;
	// The JSON's flight head points the beak at the ground; keep it level in flight instead, and let the
	// jaw and crest follow the head.
	Target.Remove(TEXT("jaw"));
	Target.Remove(TEXT("jaw_end"));
	Target.Remove(TEXT("crest_end"));
	const int32 HeadI = RS.FindBoneIndex(TEXT("head"));
	const int32 HeadEndI = RS.FindBoneIndex(TEXT("head_end"));
	if (const FVector* H = Target.Find(TEXT("head")); H && HeadI != INDEX_NONE && HeadEndI != INDEX_NONE)
	{
		const float Len = (CS[HeadEndI].GetLocation() - CS[HeadI].GetLocation()).Size();
		Target.Add(TEXT("head_end"), *H + FVector(1.f, 0.f, -0.12f).GetSafeNormal() * Len);
	}

	TArray<FQuat> Delta;
	Delta.Init(FQuat::Identity, N);
	TArray<FQuat> TargetCS;
	TargetCS.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 P = RS.GetParentIndex(i);
		FQuat Inherited = P == INDEX_NONE ? FQuat::Identity : Delta[P];
		const FName Name = RS.GetBoneName(i);
		const FVector* From = Target.Find(Name);
		int32 Child = INDEX_NONE;
		float Best = 0.f;
		for (int32 c = i + 1; From && c < N; ++c)   // the child in the pose that's farthest away is the bone's direction
		{
			if (RS.GetParentIndex(c) == i && Target.Contains(RS.GetBoneName(c)))
			{
				const float D = (CS[c].GetLocation() - CS[i].GetLocation()).Size();
				if (D > Best)
				{
					Best = D;
					Child = c;
				}
			}
		}
		Delta[i] = Inherited;
		if (Child != INDEX_NONE && Best > 0.5f)
		{
			const FVector D0 = Inherited.RotateVector(CS[Child].GetLocation() - CS[i].GetLocation());
			const FVector D1 = Target[RS.GetBoneName(Child)] - *From;
			if (!D1.IsNearlyZero())
			{
				Delta[i] = FQuat::FindBetweenVectors(D0, D1) * Inherited;
			}
		}
		TargetCS[i] = Delta[i] * CS[i].GetRotation();
		if (P != INDEX_NONE)
		{
			// Keep the joint inside its limits: hinges (side 0..0) only fold, so the wing unfolds the way the
			// bones can, and anything the target asks past a limit stops at the limit.
			const FQuat Local = ClampToLimits(RS.GetBoneName(i), RefPose[i].GetRotation(), (TargetCS[P].Inverse() * TargetCS[i]).GetNormalized());
			TargetCS[i] = TargetCS[P] * Local;
			Delta[i] = TargetCS[i] * CS[i].GetRotation().Inverse();
			FlightLocal.Add(i, Local);
		}
	}
}

void FBFPoser::Pose(const FBFPoseInput& In, TArray<FTransform>& Local) const
{
	Local = RefPose;
	const FBFSpecies& S = *Species;
	const FReferenceSkeleton& RS = S.Mesh->GetRefSkeleton();

	// Flight pose, blended in on take-off, then the wingbeat: both upper arms swing about the body's long axis.
	if (In.FlyBlend > 0.001f)
	{
		for (const TPair<int32, FQuat>& KV : FlightLocal)
		{
			const float B = WingBones.Contains(KV.Key) ? In.FlyBlend : In.FlyBlend * (1.f - In.WingsOnly);
			if (B > 0.001f)
			{
				FTransform& T = Local[KV.Key];
				T.SetRotation(FQuat::Slerp(T.GetRotation(), KV.Value, B).GetNormalized());
			}
		}
		// Near the ground the downstroke stops where the wing tips meet it (a failed take-off beats against the ground).
		const float Stroke = FMath::Max(In.FlyBlend * FMath::Lerp(4.f, 38.f, In.FlapAmount) * FMath::Sin(2.f * PI * In.FlapPhase), -In.FlapDownMax);
		const float Glide = In.FlyBlend * 3.f * FMath::Sin(2.f * PI * In.GlideU);
		const int32 Chest = RS.FindBoneIndex(TEXT("chest"));
		if (Chest != INDEX_NONE)
		{
			// The chest's rotation in component space with the current pose (its parents are the spine).
			FQuat ChestCS = FQuat::Identity;
			TArray<int32> Chain;
			for (int32 i = Chest; i != INDEX_NONE; i = RS.GetParentIndex(i))
			{
				Chain.Insert(i, 0);
			}
			for (const int32 i : Chain)
			{
				ChestCS = ChestCS * Local[i].GetRotation();
			}
			for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
			{
				const int32 Arm = RS.FindBoneIndex(FName(*FString::Printf(TEXT("upperarm%s_%s"), *S.WingPair, Side)));
				if (Arm == INDEX_NONE)
				{
					continue;
				}
				const float Sign = Side[0] == TEXT('l') ? -1.f : 1.f;
				const FQuat Flap(FVector::ForwardVector, FMath::DegreesToRadians(Sign * (Stroke + Glide)));
				FTransform& T = Local[Arm];
				T.SetRotation((ChestCS.Inverse() * Flap * ChestCS * T.GetRotation()).GetNormalized());
			}
		}
	}

	const float Stride = StrideCm(In.GaitT);
	const float Ground = 1.f - In.FlyBlend * (1.f - In.WingsOnly);

	// Legs: the hip swings forward and back through the stride; the knee folds while the foot swings forward.
	// Amplitude comes from the stride length over the leg length, so a planted foot sweeps back as far as the body goes
	// forward; capped at 90% of the rig's limit (two thirds left the Triceratops' and Ankylosaurus' walking feet
	// sliding, as their legs couldn't swing far enough).
	for (const FBFLimb& L : S.Limbs)
	{
		const FVector2D HipLim = S.LimitsOf(L.Upper).Bend;
		const float HipCap = 0.9f * FMath::Min(HipLim.Y, -HipLim.X);
		const float Geo = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Stride * 0.25f / L.LengthCm, 0.f, 0.95f)));
		const float Amp = FMath::Min(Geo, FMath::Max(HipCap, 5.f)) * In.MoveBlend * Ground;
		const float A = 2.f * PI * (In.Phase + L.PhaseOffset);
		const float Lift = FMath::Max(0.f, FMath::Cos(A));
		const float KneeCap = 0.66f * S.LimitsOf(L.Mid).Bend.Y;
		// In the air (a jump) the legs draw up as in mid-swing, the hip forward, the knee folded, each in its own range.
		// Not walking wings (a pterosaur's front limbs): folding them pulls the membrane up into a sail.
		const float Tuck = S.CanFly() && L.bFront ? 0.f : In.Tuck;
		const float Hip = FMath::Lerp(Amp * FMath::Sin(A), 0.6f * FMath::Max(HipCap, 5.f), Tuck);
		const float Knee = FMath::Lerp(FMath::Min(KneeCap, Amp * 1.6f) * Lift, KneeCap, Tuck);
		AddBoneAngles(Local, L.Upper, Hip, 0.f);
		AddBoneAngles(Local, L.Mid, Knee, 0.f);
		// Keep the foot from pointing at the ground mid-swing, within the ankle's (or wrist's) range.
		const FVector2D LowLim = S.LimitsOf(L.Low).Bend;
		const float Foot = LowLim.X < LowLim.Y ? FMath::Clamp(-0.5f * Knee, LowLim.X, LowLim.Y) : -0.5f * Knee;
		AddBoneAngles(Local, L.Low, Foot, 0.f);
	}

	// Standing on a slope, the legs stay upright under the body while it pitches with the ground (ABFDino turns it
	// about the hips): each hip and shoulder swings its leg back by the body's pitch, as far as 90% of its own range
	// goes. Each its own: held to the stiffest joint's, a T. rex on Brachiosaurus arms kept its hind legs at the arms'
	// 21° and, front feet up on a slope, its hind toes ran out ahead of its weight and it fell over backwards; its hips
	// go 36°. ABFDino's leg IK then puts each foot on the ground and lays its sole along it.
	if (In.LegLean != 0.f)
	{
		for (const FBFLimb& L : S.Limbs)
		{
			const FVector2D Bend = S.LimitsOf(L.Upper).Bend;
			const float Lean = FMath::Clamp(In.LegLean, FMath::Max(-90.f, -0.9f * Bend.Y), FMath::Min(90.f, -0.9f * Bend.X));
			AddBoneAngles(Local, L.Upper, -Lean, 0.f);
		}
	}

	// Tail and neck: a travelling side-to-side wave at stride rate, plus a slow idle sway when standing.
	// Secondary motion: each footfall bounces the tail up and down, the wave reaching the tip a little later (more at a
	// run); the head going down (a bite, sniffing) lifts the tail to balance it; speeding up the neck lags back and the
	// tail dips, slowing down the head nods forward and the tail rises (In.Surge is sprung, so they overshoot and settle).
	const float Counter = FMath::Clamp(In.NeckDown * 0.5f + In.Bite * 0.35f, 0.f, 1.f);
	const float BobWave = (0.6f + 0.6f * In.GaitT) * In.MoveBlend;
	for (int32 i = 0; i < S.Tail.Num(); ++i)
	{
		const FVector2D Lim = S.LimitsOf(S.Tail[i]).Side;
		const float Cap = 0.66f * FMath::Min(Lim.Y, -Lim.X);
		const FVector2D BL = S.LimitsOf(S.Tail[i]).Bend;
		const float BendCap = 0.66f * FMath::Max(FMath::Min(BL.Y, -BL.X), 0.f);
		const float Walk = 0.35f * Cap * FMath::Sin(2.f * PI * (In.Phase - i * 0.07f)) * In.MoveBlend;
		const float Idle = 0.12f * Cap * FMath::Sin(2.f * PI * In.IdleU - i * 0.4f) * (1.f - In.MoveBlend);
		const float Swish = In.Swish * 0.6f * Cap * FMath::Sin(2.f * PI * In.SwishU - i * 0.35f);
		const float Shake = In.Shake * 0.5f * Cap * FMath::Sin(2.f * PI * In.ShakeU - 2.f - i * 0.5f);
		const float Bounce = 0.25f * BendCap * BobWave * FMath::Sin(4.f * PI * In.Phase - 1.f - i * 0.3f);
		const float Up = Counter * 0.6f * FMath::Max(BL.Y, 0.f) - In.Surge * 0.3f * BendCap;
		AddBoneAngles(Local, S.Tail[i], (Bounce + Up) * Ground, (Walk + Idle + Swish + Shake) * Ground);
	}
	// Walking, the neck takes up the body's bob so the head rides level, as birds' and horses' do: the neck's base
	// lifts as the hips dip and the head tips back down by as much.
	float Level = 0.f;
	if (NeckLenCm > 1.f && S.Limbs.Num() > 0 && S.Neck.Num() > 0)
	{
		const float Bob = 0.025f * S.Limbs[0].LengthCm * In.MoveBlend * Ground * (0.5f - 0.5f * FMath::Cos(4.f * PI * In.Phase));
		Level = FMath::Min(FMath::RadiansToDegrees(FMath::Atan(Bob / NeckLenCm)), 0.9f * FMath::Max(S.LimitsOf(S.Neck[0]).Bend.Y, 0.f));
	}
	// A small animal (a bird's way: the Velociraptor, the Pteranodon on foot) holds its head still in space while its
	// body sways under it: the neck swings the other way to its body's sway, so the head's aim stays put.
	const bool bSteadyHead = S.MassKg < 400.f;
	float NeckSwing = 0.f;
	for (int32 i = 0; i < S.Neck.Num(); ++i)
	{
		const FVector2D Lim = S.LimitsOf(S.Neck[i]).Side;
		const float Cap = 0.66f * FMath::Min(Lim.Y, -Lim.X);
		const FVector2D BL = S.LimitsOf(S.Neck[i]).Bend;
		const float BendCap = 0.66f * FMath::Max(FMath::Min(BL.Y, -BL.X), 0.f);
		const float Walk = (bSteadyHead ? 0.06f : -0.2f) * Cap * FMath::Sin(2.f * PI * (In.Phase + i * 0.05f)) * In.MoveBlend;
		NeckSwing += Walk;
		const float Idle = 0.1f * Cap * FMath::Sin(2.f * PI * In.IdleU + 1.f + i * 0.3f) * (1.f - In.MoveBlend);
		const float Shake = In.Shake * 0.5f * Cap * FMath::Sin(2.f * PI * In.ShakeU + 1.2f + i * 0.6f);
		// A bite dips the neck forward and down, a third of the way to its limit; sniffing lowers it half way.
		const float Dip = In.Bite * 0.35f * BL.X + In.NeckDown * 0.5f * BL.X;
		const float Lag = In.Surge * 0.3f * BendCap;
		AddBoneAngles(Local, S.Neck[i], (Dip + Lag + (i == 0 ? Level : 0.f)) * Ground, (Walk + Idle + Shake) * Ground);
	}
	{
		const FVector2D HB = S.LimitsOf(TEXT("head")).Bend;
		const FVector2D HS = S.LimitsOf(TEXT("head")).Side;
		const float Nod = In.Sniff * 0.12f * FMath::Max(HB.Y, 0.f) * FMath::Sin(2.f * PI * In.SniffU);
		const float HeadShake = In.Shake * 0.6f * 0.66f * FMath::Max(FMath::Min(HS.Y, -HS.X), 0.f) * FMath::Sin(2.f * PI * In.ShakeU + 2.4f);
		// (steady: the head turned back by what the neck swung, so it points where it pointed)
		const float Hold = bSteadyHead ? -NeckSwing : 0.f;
		AddBoneAngles(Local, TEXT("head"), (-Level + In.NeckDown * 0.3f * HB.X + Nod) * Ground, (HeadShake + Hold) * Ground);
		AddBoneRoll(Local, TEXT("head"), In.Shake * 18.f * FMath::Sin(2.f * PI * In.ShakeU + 2.4f) * Ground);
	}
	// Carried arms (a theropod's, the raptor's feathered wings) swing a little with each footfall, the forearm after the
	// upper arm, and lag back speeding up (forward slowing down).
	for (const TPair<FName, FName>& Arm : CarriedArms)
	{
		const FVector2D UB = S.LimitsOf(Arm.Key).Bend, LB = S.LimitsOf(Arm.Value).Bend;
		const float UCap = 0.66f * FMath::Max(FMath::Min(UB.Y, -UB.X), 0.f), LCap = 0.66f * FMath::Max(FMath::Min(LB.Y, -LB.X), 0.f);
		const float Swing = 0.2f * UCap * BobWave * FMath::Sin(4.f * PI * In.Phase - 1.3f) - 0.35f * In.Surge * UCap
			+ 0.06f * UCap * FMath::Sin(2.f * PI * In.IdleU + 2.f) * (1.f - In.MoveBlend);
		const float Elbow = 0.25f * LCap * BobWave * FMath::Sin(4.f * PI * In.Phase - 2.1f);
		AddBoneAngles(Local, Arm.Key, Swing * Ground, 0.f);
		AddBoneAngles(Local, Arm.Value, Elbow * Ground, 0.f);
	}
	// Lumbering: a heavy two-legged walker rolls a little toward the leg it stands on (its hips sway that way, below); a
	// four-legged one doesn't, as its front legs hang off the trunk the roll would swing.
	if (LumberCm > 0.f && S.Limbs.Num() > 0 && !S.bQuadruped)
	{
		const float Side = S.Limbs[0].Upper.ToString().EndsWith(TEXT("_r")) ? -1.f : 1.f;
		const float Roll = 1.5f * In.Lumber * (LumberCm / FMath::Max(0.02f * S.Limbs[0].LengthCm, 1.f)) * In.MoveBlend * (1.f - 0.5f * In.GaitT)
			* Side * FMath::Cos(2.f * PI * (In.Phase + S.Limbs[0].PhaseOffset));
		AddBoneRoll(Local, TEXT("spine_01"), Roll * Ground);
	}
	// Shaking off: the spine waves and twists against the neck, so the shake runs down the body.
	if (In.Shake > 0.001f || In.Shift != 0.f)
	{
		int32 k = 0;
		for (const TCHAR* Spine : { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03") })
		{
			const FVector2D L = S.LimitsOf(Spine).Side;
			const float R = FMath::Max(FMath::Min(L.Y, -L.X), 0.f);
			const float Wave = In.Shake * 0.5f * R * FMath::Sin(2.f * PI * In.ShakeU + PI - k * 0.4f);
			// Shifting its weight the shoulders lag a little behind the hips.
			AddBoneAngles(Local, Spine, 0.f, (Wave - 0.25f * In.Shift * R) * Ground);
			AddBoneRoll(Local, Spine, In.Shake * 7.f * FMath::Sin(2.f * PI * In.ShakeU + PI - k * 0.6f) * Ground);
			++k;
		}
	}

	// Swimming (ABFDino): the neck held up so the nostrils stay clear, each joint taking a share by its upward range.
	if (In.NeckRaise > 0.f)
	{
		TArray<FName> Lift = S.Neck;
		Lift.Add(TEXT("head"));
		float Total = 0.f;
		for (const FName B : Lift)
		{
			Total += FMath::Max(S.LimitsOf(B).Bend.Y, 0.f);
		}
		for (const FName B : Lift)
		{
			const float Up = FMath::Max(S.LimitsOf(B).Bend.Y, 0.f);
			AddBoneAngles(Local, B, Total > 0.f ? FMath::Min(In.NeckRaise * Up / Total, 0.9f * Up) : 0.f, 0.f);
		}
	}

	// Turning (ABFDino): the head and neck lead toward where the animal is heading, each joint taking a share by its
	// side range; the spine curves into the turn and the tail swings out behind it, so the body doesn't swivel stiff.
	if (In.LookYaw != 0.f || In.Turn != 0.f)
	{
		auto Range = [&S](FName Bone) { const FVector2D L = S.LimitsOf(Bone).Side; return FMath::Max(FMath::Min(L.Y, -L.X), 0.f); };
		TArray<FName> Lead = S.Neck;
		Lead.Add(TEXT("head"));
		float Total = 0.f;
		for (const FName B : Lead)
		{
			Total += Range(B);
		}
		for (const FName B : Lead)
		{
			const float Share = Total > 0.f ? Range(B) / Total : 0.f;
			AddBoneAngles(Local, B, 0.f, FMath::Clamp(In.LookYaw * Share, -0.9f * Range(B), 0.9f * Range(B)) * Ground);
		}
		const float Turn = FMath::Clamp(In.Turn, -1.f, 1.f) * Ground;
		for (const TCHAR* Spine : { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03") })
		{
			AddBoneAngles(Local, Spine, 0.f, 0.6f * Turn * Range(Spine));
		}
		for (const FName B : S.Tail)
		{
			AddBoneAngles(Local, B, 0.f, 0.5f * Turn * Range(B));
		}
	}

	// Jaw: opens to most of its limit. Roaring raises the head as it opens; a bite tucks it down instead; breathing moves
	// the jaw alone.
	const float JawOpen = FMath::Max(In.Mouth, In.Jaw);
	if (JawOpen > 0.001f)
	{
		AddBoneAngles(Local, TEXT("jaw"), JawOpen * 0.8f * S.LimitsOf(TEXT("jaw")).Bend.Y, 0.f);
	}
	if (In.Mouth > 0.001f)
	{
		AddBoneAngles(Local, TEXT("head"), In.Mouth * (1.f - In.Bite) * 0.3f * S.LimitsOf(TEXT("head")).Bend.Y, 0.f);
	}
	if (In.Bite > 0.001f)
	{
		AddBoneAngles(Local, TEXT("head"), In.Bite * 0.4f * S.LimitsOf(TEXT("head")).Bend.X * Ground, 0.f);
	}

	// Body bob: the hips dip twice a stride.
	const int32 Pelvis = RS.FindBoneIndex(TEXT("pelvis"));
	if (Local.IsValidIndex(Pelvis) && S.Limbs.Num() > 0)
	{
		const float Bob = 0.025f * S.Limbs[0].LengthCm * In.MoveBlend * Ground * (0.5f - 0.5f * FMath::Cos(4.f * PI * In.Phase));
		Local[Pelvis].AddToTranslation(FVector(0.f, 0.f, -Bob));
		// Shifting its weight the hips sway over one side's legs (the planted feet stay put: ABFDino's leg IK).
		Local[Pelvis].AddToTranslation(FVector(0.f, -In.Shift * 0.05f * S.Limbs[0].LengthCm * Ground, 0.f));
		// Lumbering: over the first limb's foot while it stands (half its stride after it lifts), the other way after.
		const float Side = S.Limbs[0].Upper.ToString().EndsWith(TEXT("_r")) ? -1.f : 1.f;
		const float Sway = In.Lumber * LumberCm * In.MoveBlend * (1.f - 0.5f * In.GaitT) * Side * FMath::Cos(2.f * PI * (In.Phase + S.Limbs[0].PhaseOffset));
		Local[Pelvis].AddToTranslation(FVector(0.f, Sway * Ground, 0.f));
	}
	// Lowering its head to the ground, a two-legged animal tips forward at the hips, its tail swinging up behind as the
	// balance (a short theropod neck can't reach the ground by itself); a four-legged one bows a little at the shoulders.
	if (In.NeckDown > 0.001f)
	{
		AddBoneAngles(Local, TEXT("pelvis"), -In.NeckDown * (S.bQuadruped ? 4.f : 22.f) * Ground, 0.f);
	}

	// Breathing: the ribcage swells and settles; the neck and arms hanging off it keep their own size.
	if (In.BreathDepth > 0.f && Local.IsValidIndex(BreathBone))
	{
		const float Scale = BFBreathScale(In.Breath, In.BreathDepth);
		Local[BreathBone].SetScale3D(Local[BreathBone].GetScale3D() * Scale);
		for (const int32 K : BreathKeep)
		{
			Local[K].SetScale3D(Local[K].GetScale3D() / Scale);
		}
	}
}
