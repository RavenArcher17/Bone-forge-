#include "BFLook.h"

namespace
{
	const FBFSkinScheme Schemes[] =
	{
		{ TEXT("trex"), TEXT("T. rex"), TEXT("#5b5137"), TEXT("#cdbf9b"), TEXT("#3c3625"), TEXT("#8a5a35"), TEXT("#3a3226"), TEXT("#c99a2e"),
			{ TEXT("trex_pebble"), TEXT("trex_pebble"), TEXT("trike_frill_polygons"), TEXT("theropod_foot") }, 40.f, 1.2f, 1210.f,
			TEXT("pebbly scales"), TEXT("skin impressions show small pebbly scales on a T. rex's neck, hips and tail") },
		{ TEXT("trike"), TEXT("Trike"), TEXT("#6e6250"), TEXT("#c9bba0"), TEXT("#544a3c"), TEXT("#a2562f"), TEXT("#e3d8bd"), TEXT("#6b4a2a"),
			{ TEXT("trike_cone_scales"), TEXT("trike_cone_scales"), TEXT("trike_frill_polygons"), TEXT("trike_frill_polygons") }, 45.f, 1.4f, 790.f,
			TEXT("big cone scales"), TEXT("Triceratops skin impressions show big rounded scales, some raised into cones") },
		{ TEXT("raptor"), TEXT("Raptor"), TEXT("#3a2f28"), TEXT("#b9a58a"), TEXT("#e8dfcf"), TEXT("#3a2f28"), TEXT("#2c2620"), TEXT("#d8a12a"),
			{ TEXT("raptor_feathers"), TEXT("raptor_feathers"), TEXT("trike_frill_polygons"), TEXT("theropod_foot") }, 9.f, 0.15f, 200.f,
			TEXT("feathers"), TEXT("quill knobs on a Velociraptor's forearm show it had feathers") },
		{ TEXT("ankylo"), TEXT("Ankylo"), TEXT("#7a4a32"), TEXT("#c7a98a"), TEXT("#4f3324"), TEXT("#4f3324"), TEXT("#3b2a20"), TEXT("#5a3d24"),
			{ TEXT("ankylo_armor"), TEXT("trex_pebble"), TEXT("ankylo_armor"), TEXT("trex_pebble") }, 35.f, 1.6f, 660.f,
			TEXT("armour plates"), TEXT("ankylosaurs were covered in bony plates (osteoderms) set in scaly skin") },
		{ TEXT("brachio"), TEXT("Brachio"), TEXT("#6d7363"), TEXT("#bfc0ae"), TEXT("#575c4f"), TEXT("#575c4f"), TEXT("#3d3a33"), TEXT("#4b3a26"),
			{ TEXT("sauropod_mixed"), TEXT("sauropod_mixed"), TEXT("trike_frill_polygons"), TEXT("trex_pebble") }, 70.f, 1.8f, 1770.f,
			TEXT("sauropod skin"), TEXT("sauropod skin impressions show small rounded scales in rosettes") },
		{ TEXT("ptera"), TEXT("Ptera"), TEXT("#4a4540"), TEXT("#e2ded6"), TEXT("#4a4540"), TEXT("#b5452a"), TEXT("#d8c79a"), TEXT("#8a3a1c"),
			{ TEXT("ptera_fuzz"), TEXT("ptera_fuzz"), TEXT("ptera_fuzz"), TEXT("theropod_foot") }, 12.f, 0.15f, 230.f,
			TEXT("fuzz"), TEXT("pterosaur fossils show a fuzz of fine filaments (pycnofibres)") },
	};

	FVector3f HexGamma(const TCHAR* Hex)
	{
		const FColor C = FColor::FromHex(Hex);
		return FVector3f(C.R / 255.f, C.G / 255.f, C.B / 255.f);
	}

	FLinearColor ToLinear(const FVector3f& G)
	{
		return FLinearColor(FMath::Pow(G.X, 2.2f), FMath::Pow(G.Y, 2.2f), FMath::Pow(G.Z, 2.2f), 1.f);
	}
}

const FBFSkinScheme* BFSkinScheme(const FString& Key)
{
	for (const FBFSkinScheme& S : Schemes)
	{
		if (Key == S.Key)
		{
			return &S;
		}
	}
	return nullptr;
}

TConstArrayView<FBFSkinScheme> BFSkinSchemes()
{
	return MakeArrayView(Schemes);
}

FLinearColor BFHexLinear(const TCHAR* Hex)
{
	return ToLinear(HexGamma(Hex));
}

FLinearColor BFScuteLinear(const FBFSkinScheme& S)
{
	const FVector3f K = HexGamma(S.Keratin), B = HexGamma(S.Belly);
	return ToLinear(K + 0.35f * (B - K));
}
