#include "BFRuntimeImport.h"

#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "UObject/Package.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"

static FString Cap(const FString& Key)
{
	return Key.Left(1).ToUpper() + Key.Mid(1);
}

bool BFRuntimeImport::BuildGlb(const FString& RigFolder, const FString& Key, FString& GlbPath, FString& Error)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(RigFolder, TEXT("..")));
	const FString Script = FPaths::Combine(Root, TEXT("unreal"), TEXT("build_glb.py"));
	// The .glb from last time, if nothing it was made from has changed since (the game starts several seconds sooner for
	// each creature made in the editor): its rig, body and parts, and the scripts that write it. Walk it always rebuilds.
	{
		IFileManager& FM = IFileManager::Get();
		const FString Glb = FPaths::Combine(Root, TEXT("unreal"), TEXT("out"), Key + TEXT("_skeletal.glb"));
		const FDateTime Made = FM.GetTimeStamp(*Glb);
		if (Made != FDateTime::MinValue())
		{
			const FString Inputs[] =
			{
				FPaths::Combine(RigFolder, Key + TEXT("_rig.json")),
				FPaths::Combine(Root, TEXT("body_shapes"), Key + TEXT("_body.obj")),
				FPaths::Combine(Root, TEXT("body_shapes"), Key + TEXT("_body_chains.json")),
				FPaths::Combine(Root, TEXT("body_shapes"), Key + TEXT("_features.obj")),
				Script,
				FPaths::Combine(Root, TEXT("unreal"), TEXT("bone_forge_gltf.py")),
				FPaths::Combine(Root, TEXT("unreal"), TEXT("bone_forge_skin.py")),
			};
			bool bFresh = true;
			for (const FString& In : Inputs)
			{
				const FDateTime T = FM.GetTimeStamp(*In);
				bFresh &= T == FDateTime::MinValue() || T <= Made;
			}
			if (bFresh)
			{
				GlbPath = Glb;
				return true;
			}
		}
	}
	int32 Code = -1;
	FString Out, Err;
	FPlatformProcess::ExecProcess(TEXT("python"), *FString::Printf(TEXT("\"%s\" %s"), *Script, *Key), &Code, &Out, &Err);
	Out.TrimStartAndEndInline();
	if (Code != 0 || !Out.StartsWith(TEXT("ok ")))
	{
		Error = FString::Printf(TEXT("build_glb.py %s: %s %s"), *Key, *Out, *Err.Right(400));
		return false;
	}
	GlbPath = FPaths::Combine(Root, TEXT("unreal"), TEXT("out"), Key + TEXT("_skeletal.glb"));
	return true;
}

// The import's setup, shared by the waiting and the polled import: where it goes and the pipeline.
static bool Prepare(const FString& Key, const FString& GlbPath, FString& Dest, UInterchangeSourceData*& Source, FImportAssetParameters& Params, FString& Error)
{
	if (!FPaths::FileExists(GlbPath))
	{
		Error = FString::Printf(TEXT("no .glb at %s"), *GlbPath);
		return false;
	}
	// Where the editor's import puts it (bone_forge_unreal.py): FBFSpecies::Load looks there.
	Dest = FString::Printf(TEXT("/Game/BoneForge/%s/%s_skeletal"), *Cap(Key), *Key);
	Source = UInterchangeManager::CreateSourceData(GlbPath);
	// The mesh and its skeleton only: the .glb's materials would be Material Instance Constants, which only the editor
	// may create (it asserted in the game), and the game puts the species' skin on by slot name itself (ApplySkin);
	// the ragdoll it builds from the rig's capsules (UBFPhysics), the motion is procedural. In type subfolders, as the
	// editor import is: <key>_skeletal/SkeletalMeshes/<key>_skeletal.
	UInterchangeGenericAssetsPipeline* Pipe = NewObject<UInterchangeGenericAssetsPipeline>(GetTransientPackage(), NAME_None, RF_Transient);
	Pipe->bAssetTypeSubFolders = true;
	Pipe->MaterialPipeline->bImportMaterials = false;
	Pipe->MaterialPipeline->TexturePipeline->bImportTextures = false;
	Pipe->MeshPipeline->bImportStaticMeshes = false;
	Pipe->MeshPipeline->bImportSkeletalMeshes = true;
	Pipe->MeshPipeline->bCreatePhysicsAsset = false;
	Pipe->AnimationPipeline->bImportAnimations = false;
	Params.bIsAutomated = true;
	Params.bReplaceExisting = true;
	Params.OverridePipelines.Add(FSoftObjectPath(Pipe));
	return true;
}

USkeletalMesh* BFRuntimeImport::ImportGlb(const FString& Key, const FString& GlbPath, FString& Error)
{
	FString Dest;
	UInterchangeSourceData* Source = nullptr;
	FImportAssetParameters Params;
	if (!Prepare(Key, GlbPath, Dest, Source, Params, Error))
	{
		return nullptr;
	}
	TArray<UObject*> Made;
	const bool bOk = UInterchangeManager::GetInterchangeManager().ImportAsset(Dest, Source, Params, Made);
	FString Got;
	for (UObject* O : Made)
	{
		if (O)
		{
			Got += FString::Printf(TEXT(" %s(%s)"), *O->GetName(), *O->GetClass()->GetName());
		}
		if (USkeletalMesh* M = Cast<USkeletalMesh>(O))
		{
			return M;
		}
	}
	Error = FString::Printf(TEXT("Interchange import of %s %s, no Skeletal Mesh:%s"), *GlbPath, bOk ? TEXT("ran") : TEXT("failed"), *Got);
	return nullptr;
}

BFRuntimeImport::FImportHandle BFRuntimeImport::StartImport(const FString& Key, const FString& GlbPath, FString& Error)
{
	FString Dest;
	UInterchangeSourceData* Source = nullptr;
	FImportAssetParameters Params;
	if (!Prepare(Key, GlbPath, Dest, Source, Params, Error))
	{
		return nullptr;
	}
	return UInterchangeManager::GetInterchangeManager().ImportAssetWithResult(Dest, Source, Params);
}

bool BFRuntimeImport::Finished(const FImportHandle& Handle, USkeletalMesh*& Mesh, FString& Error)
{
	Mesh = nullptr;
	if (!Handle.IsValid())
	{
		Error = TEXT("no import running");
		return true;
	}
	if (Handle->GetStatus() != UE::Interchange::FImportResult::EStatus::Done)
	{
		return false;
	}
	Mesh = Cast<USkeletalMesh>(Handle->GetFirstAssetOfClass(USkeletalMesh::StaticClass()));
	if (!Mesh)
	{
		Error = FString::Printf(TEXT("the import made no Skeletal Mesh (%d objects)"), Handle->GetImportedObjects().Num());
	}
	return true;
}

void BFRuntimeImport::ApplySkin(USkeletalMesh* Mesh, const FString& Scheme)
{
	if (!Mesh)
	{
		return;
	}
	// Each slot (M_<Key>_<Part>: Skin, LidHalf, LidClosed, Eye, Scute, Mouth, Teeth, Keratin...) takes what the species'
	// own mesh wears on that part: its skin, eyes, toes, mouth, teeth and horn. A part its species hasn't got (a
	// Triceratops horn on a T. rex) from the first of the six that has one.
	TArray<FString> From = { Scheme, TEXT("trex"), TEXT("trike"), TEXT("raptor"), TEXT("ankylo"), TEXT("brachio"), TEXT("ptera") };
	TArray<USkeletalMesh*> Meshes;
	for (const FString& K : From)
	{
		const FString Path = FString::Printf(TEXT("/Game/BoneForge/%s/%s_skeletal/SkeletalMeshes/%s_skeletal.%s_skeletal"), *Cap(K), *K, *K, *K);
		if (USkeletalMesh* M = K == Scheme || K != From[0] ? LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr)
		{
			if (M != Mesh)
			{
				Meshes.Add(M);
			}
		}
	}
	for (FSkeletalMaterial& Slot : Mesh->GetMaterials())
	{
		FString Name = Slot.MaterialSlotName.ToString(), Part;
		int32 Cut = INDEX_NONE;
		if (!Name.FindLastChar(TEXT('_'), Cut))
		{
			continue;
		}
		Part = Name.Mid(Cut);                 // "_Skin"
		for (const USkeletalMesh* Source : Meshes)
		{
			const FSkeletalMaterial* Match = Source->GetMaterials().FindByPredicate([&](const FSkeletalMaterial& S)
			{
				return S.MaterialInterface && S.MaterialSlotName.ToString().EndsWith(Part);
			});
			if (Match)
			{
				Slot.MaterialInterface = Match->MaterialInterface;
				break;
			}
		}
	}
}
