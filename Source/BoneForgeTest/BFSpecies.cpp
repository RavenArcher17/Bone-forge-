#include "BFSpecies.h"

#include "BFPhysics.h"
#include "BFRuntimeImport.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TArray<FString> BFSpeciesKeys(const FString& RigFolder)
{
	TArray<FString> Keys = { TEXT("trex"), TEXT("trike"), TEXT("raptor"), TEXT("ankylo"), TEXT("brachio"), TEXT("ptera") };
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(RigFolder, TEXT("*_rig.json")), true, false);
	Files.Sort();
	for (const FString& F : Files)
	{
		const FString Key = F.LeftChop(9);   // "_rig.json"
		if (!Keys.Contains(Key) && !Key.StartsWith(TEXT("_")))
		{
			Keys.Add(Key);
		}
	}
	return Keys;
}

static FVector2D ReadRange(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj.IsValid() && Obj->TryGetArrayField(Field, Arr) && Arr->Num() == 2)
	{
		return FVector2D((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber());
	}
	return FVector2D::ZeroVector;
}

static FBFGait ReadGait(const TSharedPtr<FJsonObject>& Gaits, const TCHAR* Field)
{
	FBFGait G;
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (Gaits.IsValid() && Gaits->TryGetObjectField(Field, Obj))
	{
		G.Name = (*Obj)->GetStringField(TEXT("name"));
		G.SpeedCm = (*Obj)->GetNumberField(TEXT("speed_m_s")) * 100.f;
		G.StrideCm = (*Obj)->GetNumberField(TEXT("stride_m")) * 100.f;
	}
	return G;
}

const FBFLimits& FBFSpecies::LimitsOf(FName Bone) const
{
	static const FBFLimits None;
	const FBFLimits* L = Limits.Find(Bone);
	return L ? *L : None;
}

float FBFSpecies::ClimbPaceAt(float SlopeDeg) const
{
	if (ClimbPace.Num() == 0 || SlopeDeg <= 0.f)
	{
		return 1.f;
	}
	const float At = SlopeDeg / ClimbStepDeg;
	const int32 I = FMath::Min(FMath::FloorToInt(At), ClimbPace.Num() - 1);
	const int32 J = FMath::Min(I + 1, ClimbPace.Num() - 1);
	return FMath::Lerp(ClimbPace[I], ClimbPace[J], FMath::Clamp(At - I, 0.f, 1.f));
}

bool FBFSpecies::Load(const FString& RigFolder, const FString& InKey, FString& Error)
{
	Key = InKey;
	const FString Path = FPaths::Combine(RigFolder, Key + TEXT("_rig.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Error = FString::Printf(TEXT("Rig JSON not found: %s (set RigFolder in Config/DefaultGame.ini)"), *Path);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Error = FString::Printf(TEXT("Could not parse %s"), *Path);
		return false;
	}

	Name = Root->HasTypedField<EJson::String>(TEXT("creature")) ? Root->GetStringField(TEXT("creature")) : Key;
	const TSharedPtr<FJsonObject>* Mass = nullptr;
	if (Root->TryGetObjectField(TEXT("mass_kg"), Mass))
	{
		MassKg = (*Mass)->GetNumberField(TEXT("total"));
	}
	const TSharedPtr<FJsonObject>* Tilt = nullptr;
	if (Root->TryGetObjectField(TEXT("body_tilt"), Tilt))
	{
		bQuadruped = (*Tilt)->GetStringField(TEXT("kind")) == TEXT("quadruped");
		TiltUpDeg = (*Tilt)->GetNumberField(TEXT("up_everyday"));
		TiltDownDeg = (*Tilt)->GetNumberField(TEXT("down_everyday"));
	}
	const TSharedPtr<FJsonObject>* Gaits = nullptr;
	if (Root->TryGetObjectField(TEXT("gaits"), Gaits))
	{
		Walk = ReadGait(*Gaits, TEXT("walk"));
		Fast = ReadGait(*Gaits, TEXT("fast"));
	}

	const TSharedPtr<FJsonObject>* Pose = nullptr;
	if (Root->TryGetObjectField(TEXT("flight_pose_cm"), Pose))
	{
		for (const auto& KV : (*Pose)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = KV.Value->AsArray();
			FlightPose.Add(FName(*KV.Key), FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()));
		}
	}

	const TSharedPtr<FJsonObject>* Check = nullptr;
	if (Root->TryGetObjectField(TEXT("flight_check"), Check))
	{
		const FString Verdict = (*Check)->GetStringField(TEXT("verdict"));
		Flight = Verdict == TEXT("flies") ? EBFFlight::Flies : Verdict == TEXT("glides") ? EBFFlight::Glides
			: Verdict == TEXT("grounded") ? EBFFlight::Grounded : EBFFlight::None;
		(*Check)->TryGetNumberField(TEXT("wing_loading_n_m2"), WingLoading);
		(*Check)->TryGetNumberField(TEXT("flight_muscle_share"), FlightMuscleShare);
		FlightSummary = (*Check)->GetStringField(TEXT("summary"));
	}
	const TArray<TSharedPtr<FJsonValue>>* HipOffset = nullptr;
	if (Root->TryGetArrayField(TEXT("membrane_hip_offset_cm"), HipOffset) && HipOffset->Num() == 3)
	{
		MembraneHipOffset = FVector((*HipOffset)[0]->AsNumber(), (*HipOffset)[1]->AsNumber(), (*HipOffset)[2]->AsNumber());
	}
	const TSharedPtr<FJsonObject>* LegCheck = nullptr;
	if (Root->TryGetObjectField(TEXT("leg_check"), LegCheck))
	{
		const FString Verdict = (*LegCheck)->GetStringField(TEXT("verdict"));
		Legs = Verdict == TEXT("buckles") ? EBFLegs::Buckles : Verdict == TEXT("stands") ? EBFLegs::Stands : EBFLegs::Walks;
		LegSummary = (*LegCheck)->GetStringField(TEXT("summary"));
		// Jumping: every pair that carries weight pushes (a biped's legs, all four of a quadruped's).
		const TSharedPtr<FJsonObject>* Pairs = nullptr;
		double MuscleN = 0.0, LoadN = 0.0;
		if ((*LegCheck)->TryGetObjectField(TEXT("pairs"), Pairs))
		{
			for (const auto& It : (*Pairs)->Values)
			{
				const TSharedPtr<FJsonObject> P = It.Value->AsObject();
				double M = 0.0, L = 0.0;
				if (P.IsValid() && P->TryGetNumberField(TEXT("muscle_n"), M) && P->TryGetNumberField(TEXT("load_n"), L) && L > 0.0)
				{
					MuscleN += M;
					LoadN += L;
				}
			}
		}
		JumpPush = LoadN > 0.0 ? float(BFJumpLever * MuscleN / LoadN) : 0.f;
	}
	const TSharedPtr<FJsonObject>* ClimbCheck = nullptr;
	if (Root->TryGetObjectField(TEXT("climb_check"), ClimbCheck))
	{
		double V = 0.0;
		if ((*ClimbCheck)->TryGetNumberField(TEXT("max_deg"), V)) { ClimbMaxDeg = V; }
		if ((*ClimbCheck)->TryGetNumberField(TEXT("speed_step_deg"), V) && V > 0.0) { ClimbStepDeg = V; }
		ClimbSummary = (*ClimbCheck)->GetStringField(TEXT("summary"));
		(*ClimbCheck)->TryGetStringField(TEXT("why"), ClimbWhy);
		const TArray<TSharedPtr<FJsonValue>>* Pace = nullptr;
		if ((*ClimbCheck)->TryGetArrayField(TEXT("speed_share"), Pace))
		{
			for (const TSharedPtr<FJsonValue>& P : *Pace)
			{
				ClimbPace.Add(P->AsNumber());
			}
		}
	}
	const TSharedPtr<FJsonObject>* SwimCheck = nullptr;
	if (Root->TryGetObjectField(TEXT("swim_check"), SwimCheck))
	{
		const FString Verdict = (*SwimCheck)->GetStringField(TEXT("verdict"));
		Swim = Verdict == TEXT("swims") ? EBFSwim::Swims : Verdict == TEXT("head_under") ? EBFSwim::HeadUnder
			: Verdict == TEXT("sinks") ? EBFSwim::Sinks : EBFSwim::None;
		SwimSummary = (*SwimCheck)->GetStringField(TEXT("summary"));
		double V = 0.0;
		if ((*SwimCheck)->TryGetNumberField(TEXT("pitch_deg"), V)) { SwimPitchDeg = V; }
		if ((*SwimCheck)->TryGetNumberField(TEXT("com_depth_cm"), V)) { SwimComDepthCm = V; }
		if ((*SwimCheck)->TryGetNumberField(TEXT("speed_ms"), V)) { SwimSpeedCm = FMath::Max(V * 100.0, 20.0); }
		if ((*SwimCheck)->TryGetNumberField(TEXT("neck_raise_deg"), V)) { SwimNeckRaiseDeg = V; }
		if ((*SwimCheck)->TryGetNumberField(TEXT("density_kg_m3"), V)) { SwimDensity = V; }
		(*SwimCheck)->TryGetBoolField(TEXT("tippy"), bSwimTippy);
	}
	const TSharedPtr<FJsonObject>* Splice = nullptr;
	const TSharedPtr<FJsonObject>* Shares = nullptr;
	if (Root->TryGetObjectField(TEXT("splice"), Splice))
	{
		(*Splice)->TryGetStringField(TEXT("base"), BaseKey);
	}
	if (Root->TryGetObjectField(TEXT("splice"), Splice) && (*Splice)->TryGetObjectField(TEXT("lineage_share"), Shares))
	{
		TArray<TPair<FString, double>> Parts;
		for (const auto& KV : (*Shares)->Values)
		{
			Parts.Emplace(KV.Key, KV.Value->AsNumber());
		}
		Parts.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value > B.Value; });
		for (const TPair<FString, double>& P : Parts)
		{
			const FString Pct = P.Value < 0.01 ? FString::Printf(TEXT("%.1f"), P.Value * 100.0) : FString::Printf(TEXT("%.0f"), P.Value * 100.0);
			Lineage += FString::Printf(TEXT("%s%s %s%%"), Lineage.IsEmpty() ? TEXT("") : TEXT(", "), *P.Key, *Pct);
		}
	}
	// Whose head: a splice's base unless the head was swapped (splice.py: section "Head", or a recipe's Head donor).
	HeadKey = Key;
	if (Splice)
	{
		(*Splice)->TryGetStringField(TEXT("base"), HeadKey);
		FString Section, Donor;
		if ((*Splice)->TryGetStringField(TEXT("section"), Section) && Section == TEXT("Head") && (*Splice)->TryGetStringField(TEXT("donor"), Donor))
		{
			HeadKey = Donor;
		}
		const TSharedPtr<FJsonObject>* Recipe = nullptr;
		const TSharedPtr<FJsonObject>* Sections = nullptr;
		const TSharedPtr<FJsonObject>* Head = nullptr;
		if ((*Splice)->TryGetObjectField(TEXT("recipe"), Recipe) && (*Recipe)->TryGetObjectField(TEXT("sections"), Sections)
			&& (*Sections)->TryGetObjectField(TEXT("Head"), Head))
		{
			(*Head)->TryGetStringField(TEXT("donor"), HeadKey);
		}
	}
	Feed = HeadKey == TEXT("trike") ? EBFFeed::Shear : HeadKey == TEXT("ankylo") ? EBFFeed::Snip : HeadKey == TEXT("brachio") ? EBFFeed::Browse : EBFFeed::None;
	// parts_builder.FACE nostril size over the skull's length (head joint to snout tip); the Pteranodon has none.
	static const TMap<FString, float> Nostrils = { { TEXT("trex"), 0.0246f }, { TEXT("trike"), 0.0411f }, { TEXT("raptor"), 0.032f },
		{ TEXT("ankylo"), 0.0388f }, { TEXT("brachio"), 0.0358f } };
	NostrilShare = Nostrils.Contains(HeadKey) ? Nostrils[HeadKey] : 0.f;

	TMap<FName, FVector> Pos;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("joints")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		const FName Bone(*J->GetStringField(TEXT("name")));
		const TArray<TSharedPtr<FJsonValue>>& P = J->GetArrayField(TEXT("position_cm"));
		Pos.Add(Bone, FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()));
		const TSharedPtr<FJsonObject>* Lim = nullptr;
		if (J->TryGetObjectField(TEXT("limits_deg"), Lim))
		{
			FBFLimits L;
			L.Bend = ReadRange(*Lim, TEXT("bend"));
			L.Side = ReadRange(*Lim, TEXT("side"));
			L.Twist = ReadRange(*Lim, TEXT("twist"));
			Limits.Add(Bone, L);
		}
		const FString S = Bone.ToString();
		if (S.StartsWith(TEXT("tail_")) && S != TEXT("tail_club"))
		{
			Tail.Add(Bone);
		}
		else if (S.StartsWith(TEXT("neck_")))
		{
			Neck.Add(Bone);
		}
	}

	// Footfall: bipeds L, R alternating; quadrupeds left hind, left front, right hind, right front, a quarter stride apart.
	auto AddLimb = [&](const TCHAR* Upper, const TCHAR* Mid, const TCHAR* Low, const TCHAR* Side, float Offset, bool bFront)
	{
		FBFLimb L;
		L.Upper = FName(*FString::Printf(TEXT("%s_%s"), Upper, Side));
		L.Mid = FName(*FString::Printf(TEXT("%s_%s"), Mid, Side));
		L.Low = FName(*FString::Printf(TEXT("%s_%s"), Low, Side));
		if (const FVector* P = Pos.Find(L.Upper))
		{
			L.LengthCm = FMath::Max(P->Z, 10.f);
		}
		L.PhaseOffset = Offset;
		L.bFront = bFront;
		Limbs.Add(L);
	};
	// A splice on front legs too long for its body (splice.py front_stance) stands on them alone, its hind feet hanging
	// off the ground: those legs aren't walked on, so they stay as they hang.
	double HangCm = 0.0;
	bHindHang = Root->TryGetNumberField(TEXT("hind_hang_cm"), HangCm) && HangCm > 0.0;
	if (!bHindHang)
	{
		AddLimb(TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("l"), 0.f, false);
		AddLimb(TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("r"), 0.5f, false);
	}
	if (bQuadruped)
	{
		AddLimb(TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"), TEXT("l"), 0.25f, true);
		AddLimb(TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"), TEXT("r"), 0.75f, true);
	}
	// A third pair of limbs that walks (the splicing editor). Counted with the front limbs: the hind legs alone set the
	// body's height on a slope.
	const TSharedPtr<FJsonObject>* Extra = nullptr;
	if (Root->TryGetObjectField(TEXT("extra_pair"), Extra) && (*Extra)->GetBoolField(TEXT("walks")))
	{
		const FString U = (*Extra)->GetStringField(TEXT("upper")), M = (*Extra)->GetStringField(TEXT("mid")), W = (*Extra)->GetStringField(TEXT("low"));
		const float Phase = (*Extra)->GetNumberField(TEXT("phase"));
		AddLimb(*U, *M, *W, TEXT("l"), Phase, true);
		AddLimb(*U, *M, *W, TEXT("r"), Phase + 0.5f, true);
	}
	Root->TryGetStringField(TEXT("wing_pair"), WingPair);

	// Jumping (JumpPush from the leg check, above): the hips drop a quarter of their height and push the body up from there.
	{
		float HipZ = 0.f;
		for (const FBFLimb& L : Limbs)
		{
			if (const FVector* P = Pos.Find(L.Upper))
			{
				HipZ = FMath::Max(HipZ, (float)P->Z);
			}
		}
		JumpCrouchCm = 0.25f * HipZ;
		JumpCm = JumpPush > 1.f ? FMath::Min(JumpCrouchCm * (JumpPush - 1.f), BFJumpMaxCm) : 0.f;
		if (JumpCm >= 2.f)
		{
			JumpSummary = FString::Printf(TEXT("Jumps %.0f cm: through its legs' levers its muscles push %.1f times its weight, from a %.0f cm crouch%s."),
				JumpCm, JumpPush, JumpCrouchCm, JumpCm >= BFJumpMaxCm ? TEXT(" (no animal jumps higher: a kilo of muscle can only do so much work)") : TEXT(""));
		}
		else
		{
			JumpSummary = FString::Printf(TEXT("Can't jump: through its legs' levers its muscles push %.2f times its weight, and it needs more than its weight to leave the ground."), JumpPush);
			JumpCm = 0.f;
		}
	}

	// Capsules: "thigh (l, r)" is given for the left side and mirrored to the right.
	const TArray<TSharedPtr<FJsonValue>>* Caps = nullptr;
	if (Root->TryGetArrayField(TEXT("collision_capsules"), Caps))
	{
		for (const TSharedPtr<FJsonValue>& V : *Caps)
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			FString Bone = C->GetStringField(TEXT("bone"));
			const FString From = C->GetStringField(TEXT("from_joint")), To = C->GetStringField(TEXT("to_joint"));
			TArray<FString> Sides = { TEXT("") };
			if (Bone.Contains(TEXT("(l, r)")))
			{
				Bone = Bone.Replace(TEXT(" (l, r)"), TEXT(""));
				Sides = { TEXT("l"), TEXT("r") };
			}
			for (const FString& Side : Sides)
			{
				auto Sided = [&](const FString& J) { return Side.IsEmpty() ? J : J.LeftChop(2) + TEXT("_") + Side; };
				const FVector* A = Pos.Find(FName(*Sided(From)));
				const FVector* B = Pos.Find(FName(*Sided(To)));
				if (!A || !B)
				{
					continue;
				}
				FBFCapsule K;
				K.Bone = FName(*(Side.IsEmpty() ? Bone : Bone + TEXT("_") + Side));
				K.A = *A;
				K.B = *B;
				K.Radius = C->GetNumberField(TEXT("radius_cm"));
				K.MassKg = C->GetNumberField(TEXT("mass_kg"));
				Capsules.Add(K);
			}
		}
	}

	const FString Cap = Key.Left(1).ToUpper() + Key.Mid(1);
	const FString MeshPath = FString::Printf(TEXT("/Game/BoneForge/%s/%s_skeletal/SkeletalMeshes/%s_skeletal.%s_skeletal"), *Cap, *Key, *Key, *Key);
	Mesh = PrebuiltMesh ? nullptr : LoadObject<USkeletalMesh>(nullptr, *MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh)
	{
		// Not imported in the editor (a creature made in the splicing editor), or just built by Walk it: built here, in
		// the running game, from its rig and body (BFRuntimeImport), and kept in memory only.
		FString Glb, Why;
		if ((Mesh = PrebuiltMesh) != nullptr ||
			(BFRuntimeImport::BuildGlb(RigFolder, Key, Glb, Why) && (Mesh = BFRuntimeImport::ImportGlb(Key, Glb, Why)) != nullptr))
		{
			const TSharedPtr<FJsonObject>* SpliceInfo = nullptr;
			FString Scheme = Key;            // a splice wears its base species' skin
			if (Root->TryGetObjectField(TEXT("splice"), SpliceInfo))
			{
				(*SpliceInfo)->TryGetStringField(TEXT("base"), Scheme);
			}
			BFRuntimeImport::ApplySkin(Mesh, Scheme);
			bRuntimeMesh = true;
		}
		else
		{
			Error = FString::Printf(TEXT("Skeletal Mesh not found at %s and not built in the game either: %s"), *MeshPath, *Why);
			return false;
		}
	}
	Bounds = Mesh->GetImportedBounds().GetBox();
	Physics = Mesh->GetPhysicsAsset();
	if (!Physics && Capsules.Num() > 0)
	{
		FString Report;
		Physics = UBFPhysics::Build(*this, nullptr, Report);
		Physics->AddToRoot();
	}
	// Baked clips (bone_forge_anims.py), if they were made.
	auto Anim = [&](const TCHAR* Clip) -> UObject*
	{
		const FString Name = Key + TEXT("_") + Clip;
		const FString Path = FString::Printf(TEXT("%s.%s"), *(BFAnimFolder(Key) / Name), *Name);
		return StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	};
	Locomotion = Cast<UBlendSpace>(Anim(TEXT("locomotion")));
	FlightBlend = Cast<UBlendSpace>(Anim(TEXT("flight")));
	BiteAnim = Cast<UAnimSequence>(Anim(TEXT("bite")));
	RoarAnim = Cast<UAnimSequence>(Anim(TEXT("roar")));
	return true;
}

FString BFAnimFolder(const FString& Key)
{
	const FString Cap = Key.Left(1).ToUpper() + Key.Mid(1);
	return FString::Printf(TEXT("/Game/BoneForge/%s/%s_skeletal/Anims"), *Cap, *Key);
}

void BFMeasureJoint(FName Bone, const FQuat& RefRot, const FQuat& Rot, float& Bend, float& Side)
{
	// Read from where the bone points, not as a Rotator: Pitch near 90 (a raptor knee bends 90) flips the Yaw.
	const FVector Dir = (RefRot.Inverse() * Rot).GetAxisX();
	float BendSign, SideSign;
	BFBoneSigns(Bone, BendSign, SideSign);
	Bend = BendSign * FMath::RadiansToDegrees(FMath::Atan2(Dir.Z, Dir.X));
	Side = SideSign * FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Y, -1.f, 1.f)));
}

void BFBoneSigns(FName Bone, float& BendSign, float& SideSign)
{
	const FString S = Bone.ToString();
	BendSign = (S.StartsWith(TEXT("tail_")) || S.StartsWith(TEXT("calf_")) || S.StartsWith(TEXT("hand_")) || S == TEXT("jaw")) ? -1.f : 1.f;
	SideSign = (S.StartsWith(TEXT("tail_")) || S.EndsWith(TEXT("_r"))) ? 1.f : -1.f;
}
