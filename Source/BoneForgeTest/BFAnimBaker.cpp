#include "BFAnimBaker.h"

#include "BFPoser.h"
#include "BFSpecies.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"

namespace
{
	/** One clip: its length and the pose inputs at u = 0..1 through it. */
	struct FBFClip
	{
		const TCHAR* Name;
		float Seconds;
		bool bLoop;
		TFunction<FBFPoseInput(float)> At;
	};

	template <typename T>
	T* FindOrCreate(const FString& Folder, const FString& Name)
	{
		UPackage* Package = CreatePackage(*(Folder / Name));
		Package->FullyLoad();
		T* Asset = FindObject<T>(Package, *Name);
		if (!Asset)
		{
			Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		}
		Package->MarkPackageDirty();
		return Asset;
	}

	UAnimSequence* Bake(const FBFSpecies& S, const FBFPoser& Poser, const FString& Folder, const FBFClip& Clip, FString& Report)
	{
		const FString Name = S.Key + TEXT("_") + Clip.Name;
		UAnimSequence* Seq = FindOrCreate<UAnimSequence>(Folder, Name);
		Seq->SetSkeleton(S.Mesh->GetSkeleton());
		Seq->SetPreviewMesh(S.Mesh, false);

		// 30 keys a second or so, with the frame rate nudged so the clip is exactly Clip.Seconds long.
		const int32 Frames = FMath::Max(FMath::RoundToInt(Clip.Seconds * 30.f), 8);
		const FFrameRate Rate(FMath::RoundToInt(Frames * 1000.f / Clip.Seconds), 1000);

		const FReferenceSkeleton& RS = S.Mesh->GetRefSkeleton();
		const TArray<FTransform>& Ref = Poser.GetRefPose();
		const int32 N = RS.GetNum();
		TArray<TArray<FVector>> Pos, Scale;
		TArray<TArray<FQuat>> Rot;
		Pos.SetNum(N);
		Rot.SetNum(N);
		Scale.SetNum(N);
		TArray<FTransform> Local;
		int32 PastLimit = 0;
		TArray<FString> PastBones;
		float WorstBy = 0.f;
		FString Worst;
		TMap<FName, FVector4> Range;   // per bone over the clip: bend min, bend max, side min, side max
		TSet<FName> Past;
		for (int32 k = 0; k <= Frames; ++k)
		{
			Poser.Pose(Clip.At(k / float(Frames)), Local);
			for (int32 b = 0; b < N; ++b)
			{
				FQuat Q = Local[b].GetRotation().GetNormalized();
				if (k > 0 && (Q | Rot[b].Last()) < 0.f)   // keep neighbouring keys on the same side so they interpolate the short way
				{
					Q = -Q;
				}
				Pos[b].Add(Local[b].GetLocation());
				Rot[b].Add(Q);
				Scale[b].Add(Local[b].GetScale3D());

				const FName Bone = RS.GetBoneName(b);
				if (const FBFLimits* L = S.Limits.Find(Bone); L && RS.GetParentIndex(b) != INDEX_NONE)
				{
					float Bend, Side;
					BFMeasureJoint(Bone, Ref[b].GetRotation(), Q, Bend, Side);
					FVector4& R4 = Range.FindOrAdd(Bone, FVector4(Bend, Bend, Side, Side));
					R4 = FVector4(FMath::Min(R4.X, Bend), FMath::Max(R4.Y, Bend), FMath::Min(R4.Z, Side), FMath::Max(R4.W, Side));
					auto Check = [&](float Angle, const FVector2D& R, const TCHAR* Axis)
					{
						const float By = FMath::Max(R.X - Angle, Angle - R.Y);
						if (By > 0.5f)
						{
							++PastLimit;
							Past.Add(Bone);
							if (k == 0) PastBones.AddUnique(FString::Printf(TEXT("%s %s %.0f"), *Bone.ToString(), Axis, Angle));
						}
						if (By > 0.5f && By > WorstBy)
						{
							WorstBy = By;
							Worst = FString::Printf(TEXT("%s %s %.0f (limit %.0f..%.0f)"), *Bone.ToString(), Axis, Angle, R.X, R.Y);
						}
					};
					Check(Bend, L->Bend, TEXT("bend"));
					Check(Side, L->Side, TEXT("side"));
				}
			}
		}
		// A loop's last key must be its first again.
		float Seam = 0.f;
		for (int32 b = 0; Clip.bLoop && b < N; ++b)
		{
			Seam = FMath::Max(Seam, FMath::RadiansToDegrees(Rot[b][0].AngularDistance(Rot[b].Last())));
		}

		IAnimationDataController& C = Seq->GetController();
		C.OpenBracket(FText::FromString(TEXT("Bone Forge bake")), false);
		C.InitializeModel();
		C.RemoveAllBoneTracks(false);
		C.SetFrameRate(Rate, false);
		C.SetNumberOfFrames(FFrameNumber(Frames), false);
		for (int32 b = 0; b < N; ++b)
		{
			const FName Bone = RS.GetBoneName(b);
			C.AddBoneCurve(Bone, false);
			C.SetBoneTrackKeys(Bone, Pos[b], Rot[b], Scale[b], false);
		}
		C.NotifyPopulated();
		C.CloseBracket(false);
		Seq->bLoop = Clip.bLoop;
		Seq->MarkPackageDirty();

		Report += FString::Printf(TEXT("  %s: %.2f s, %d frames, %s, %d joint axes past limits_deg%s%s%s\n"), *Name, Clip.Seconds, Frames,
			Clip.bLoop ? TEXT("loop") : TEXT("one-shot"), PastLimit, WorstBy > 0.f ? TEXT(" (furthest: ") : TEXT(""), *Worst, WorstBy > 0.f ? TEXT(")") : TEXT(""));
		if (PastBones.Num() > 0)
		{
			Report += TEXT("    first frame past limits: ") + FString::Join(TArray<FString>(PastBones.GetData(), FMath::Min(PastBones.Num(), 60)), TEXT(", ")) + TEXT("\n");
		}
		for (const FName& Bone : Past)
		{
			const FVector4& R4 = Range[Bone];
			const FBFLimits& L = S.Limits[Bone];
			Report += FString::Printf(TEXT("    %s moves bend %.0f..%.0f (limit %.0f..%.0f), side %.0f..%.0f (limit %.0f..%.0f)\n"), *Bone.ToString(),
				R4.X, R4.Y, L.Bend.X, L.Bend.Y, R4.Z, R4.W, L.Side.X, L.Side.Y);
		}
		if (Clip.bLoop)
		{
			Report += FString::Printf(TEXT("    loop seam %.3f deg\n"), Seam);
		}
		return Seq;
	}

	/** A one-axis blend space with Samples at their axis values. */
	void BlendSpace(const FBFSpecies& S, const FString& Folder, const TCHAR* Name, const TCHAR* Axis, float Max,
		const TArray<TPair<UAnimSequence*, float>>& Samples, FString& Report)
	{
		const FString Full = S.Key + TEXT("_") + Name;
		UBlendSpace1D* BS = FindOrCreate<UBlendSpace1D>(Folder, Full);
		BS->SetSkeleton(S.Mesh->GetSkeleton());
		BS->SetPreviewMesh(S.Mesh, false);
		// BlendParameters is protected; set it the way the editor's details panel does, through its property.
		const FStructProperty* Prop = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
		FBlendParameter& P = Prop->ContainerPtrToValuePtr<FBlendParameter>(BS, 0)[0];
		P.DisplayName = Axis;
		P.Min = 0.f;
		P.Max = Max;
		P.GridNum = 4;
		P.bSnapToGrid = false;
		P.bWrapInput = false;
		while (BS->GetNumberOfBlendSamples() > 0)
		{
			BS->DeleteSample(0);
		}
		for (const TPair<UAnimSequence*, float>& KV : Samples)
		{
			BS->AddSample(KV.Key, FVector(KV.Value, 0.f, 0.f));
		}
		BS->ValidateSampleData();
		BS->ResampleData();
		BS->MarkPackageDirty();
		Report += FString::Printf(TEXT("  %s: %s 0..%.0f, %d samples\n"), *Full, Axis, Max, BS->GetNumberOfBlendSamples());
	}
}
#endif

FString UBFAnimBaker::BakeClips(const FString& RigFolder, const FString& Key)
{
#if WITH_EDITOR
	FBFSpecies S;
	FString Error;
	if (!S.Load(RigFolder, Key, Error))
	{
		return TEXT("ERROR: ") + Error;
	}
	FBFPoser Poser;
	Poser.Init(S);
	const FString Folder = BFAnimFolder(Key);
	FString Report = FString::Printf(TEXT("%s -> %s\n"), *Key, *Folder);

	// The idle clip breathes too, in whole breaths so it loops (none for the big ones, who take a breath every minute or so,
	// longer than the clip).
	const float Breaths = FMath::RoundToFloat(BFBreathsPerSecond(S.MassKg) * BFIdleSeconds);
	FBFClip Clips[] = {
		{ TEXT("idle"), BFIdleSeconds, true, [Breaths](float u) { FBFPoseInput In; In.IdleU = u; In.Breath = u * Breaths; In.BreathDepth = Breaths > 0.f ? 1.f : 0.f; return In; } },
		{ TEXT("walk"), Poser.StrideCm(0.f) / S.Walk.SpeedCm, true, [](float u) { FBFPoseInput In; In.Phase = u; In.MoveBlend = 1.f; In.Jaw = BFBreathMouth(u, 0.f, 0.f); return In; } },
		{ TEXT("run"), Poser.StrideCm(1.f) / S.Fast.SpeedCm, true, [](float u) { FBFPoseInput In; In.Phase = u; In.MoveBlend = 1.f; In.GaitT = 1.f; In.Jaw = BFBreathMouth(u, 1.f, 0.f); return In; } },
		{ TEXT("bite"), BFBiteSeconds, false, [](float u) { FBFPoseInput In; BFBiteCurves(u, In.Bite, In.Mouth); return In; } },
		{ TEXT("roar"), BFRoarSeconds, false, [](float u) { FBFPoseInput In; In.Mouth = BFRoarMouth(u); return In; } },
	};
	TMap<FString, UAnimSequence*> Made;
	for (const FBFClip& C : Clips)
	{
		Made.Add(C.Name, Bake(S, Poser, Folder, C, Report));
	}
	BlendSpace(S, Folder, TEXT("locomotion"), TEXT("Speed"), S.Fast.SpeedCm,
		{ { Made[TEXT("idle")], 0.f }, { Made[TEXT("walk")], S.Walk.SpeedCm }, { Made[TEXT("run")], S.Fast.SpeedCm } }, Report);
	Report = Report.Replace(TEXT("_run:"), *FString::Printf(TEXT("_run (%s):"), *S.Fast.Name));

	if (S.CanFly())
	{
		// One wingbeat per clip in both, so the blend keeps the strokes in step.
		const FBFClip Glide = { TEXT("glide"), 1.f / BFFlapRate(0.f), true,
			[](float u) { FBFPoseInput In; In.FlyBlend = 1.f; In.FlapPhase = u; In.GlideU = u; return In; } };
		const FBFClip Flap = { TEXT("flap"), 1.f / BFFlapRate(1.f), true,
			[](float u) { FBFPoseInput In; In.FlyBlend = 1.f; In.FlapAmount = 1.f; In.FlapPhase = u; In.GlideU = u; return In; } };
		UAnimSequence* G = Bake(S, Poser, Folder, Glide, Report);
		UAnimSequence* F = Bake(S, Poser, Folder, Flap, Report);
		BlendSpace(S, Folder, TEXT("flight"), TEXT("Flap"), 1.f, { { G, 0.f }, { F, 1.f } }, Report);
	}
	return Report.TrimEnd();
#else
	return TEXT("ERROR: the clip baker only runs in the editor");
#endif
}
