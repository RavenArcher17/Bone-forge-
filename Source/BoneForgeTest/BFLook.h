// A creature's look (lab plan item 3): its colours and its covering (scales, feathers, fuzz...), chosen in the game (L)
// and kept per creature in bone_forge/my_creatures.json. Fossil evidence is the default (each species' own); anything
// else is the player's choice. Applied live to the skin material's parameters (ABFDino::ApplyLook): nothing is rebuilt.
#pragma once

#include "CoreMinimal.h"

struct FBFLook
{
	FString Scheme;          // whose colours: trex, trike, raptor, ankylo, brachio, ptera; empty: its own
	float HueDeg = 0.f;      // turned round the colour wheel
	float Shade = 0.f;       // -0.5 darker .. +0.5 lighter
	FString Covering;        // whose covering (their tiles): a species key; empty: its own
	bool IsOwn() const { return Scheme.IsEmpty() && FMath::IsNearlyZero(HueDeg) && FMath::IsNearlyZero(Shade) && Covering.IsEmpty(); }
};

/** The six species' skins, kept in step with bone_forge/unreal/bone_forge_skin.py SKIN (colours from
 *  body_shapes/colour_schemes.png, tiles from the design doc's Skin Textures tab). */
struct FBFSkinScheme
{
	const TCHAR* Key;
	const TCHAR* Short;
	const TCHAR* Back;
	const TCHAR* Belly;
	const TCHAR* Pattern;
	const TCHAR* Accent;
	const TCHAR* Keratin;
	const TCHAR* Iris;
	const TCHAR* Tiles[4];   // back, belly, face, feet
	float TileCm;            // how much body one tile covers on this species
	float BumpCm;            // how high its scales stand
	float BodyCm;            // the species' length (a covering on a bigger or smaller body scales by the square root)
	const TCHAR* Covering;   // the covering in words
	const TCHAR* Fossils;    // what the fossils say about it
};

const FBFSkinScheme* BFSkinScheme(const FString& Key);
TConstArrayView<FBFSkinScheme> BFSkinSchemes();
/** A "#rrggbb" colour as the material takes it (gamma 2.2 to linear, as bone_forge_materials.py does). */
FLinearColor BFHexLinear(const TCHAR* Hex);
/** The toes' colour: the keratin a little way towards the belly (bone_forge_skin.scute_rgb), linear. */
FLinearColor BFScuteLinear(const FBFSkinScheme& S);
