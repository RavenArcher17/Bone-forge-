#include "BFPlants.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const FBFPlantKind KindTable[] =
	{
		// name                                        food kg  low cm  high cm  reach cm  regrow  cull cm  sink  shadow
		{ TEXT("monkey-puzzle conifer (Araucaria)"),    60.f,  1400.f, 3300.f,  650.f, 0.02f, 400000.f, 30.f, true },
		{ TEXT("redwood (Sequoia)"),                    70.f,   900.f, 4200.f,  560.f, 0.02f, 400000.f, 30.f, true },
		{ TEXT("bald cypress (Taxodium)"),              40.f,   700.f, 2500.f,  480.f, 0.03f, 400000.f, 30.f, true },
		{ TEXT("ginkgo"),                               35.f,   500.f, 2000.f,  520.f, 0.03f, 400000.f, 20.f, true },
		{ TEXT("tree fern (Cyathea)"),                  14.f,   180.f,  750.f,  320.f, 0.05f,  70000.f, 10.f, true },
		{ TEXT("cycad"),                                12.f,    30.f,  380.f,  170.f, 0.04f,  70000.f,  8.f, true },
		{ TEXT("bennettite (Williamsonia)"),             9.f,    20.f,  230.f,  130.f, 0.05f,  50000.f,  5.f, true },
		{ TEXT("early fan palm (Sabalites)"),           10.f,   300.f,  900.f,  230.f, 0.03f, 100000.f, 10.f, true },
		{ TEXT("flowering shrub (magnolia kin)"),        6.f,    20.f,  300.f,  140.f, 0.08f,  45000.f,  5.f, true },
		{ TEXT("ferns"),                                 1.5f,    0.f,  140.f,   90.f, 0.12f,  30000.f,  4.f, true },
		{ TEXT("horsetails (Equisetum)"),                2.5f,    0.f,  240.f,   60.f, 0.15f,  25000.f,  4.f, false },
	};
	static_assert(UE_ARRAY_COUNT(KindTable) == BFPlants::Kinds, "one row a plant");

	// To a hectare by biome (EBFBiome: sea, beach, plains, forest, swamp, rock, lake, river).
	const float DensityTable[BFPlants::Kinds][8] =
	{
		{ 0.f, 0.f,    1.5f,  20.f,   0.f,  5.f, 0.f, 0.f },     // Araucaria
		{ 0.f, 0.f,    0.f,   40.f,   0.f,  6.f, 0.f, 0.f },     // Redwood
		{ 0.f, 0.f,    0.f,    0.f,  50.f,  0.f, 0.f, 0.f },     // Cypress
		{ 0.f, 0.f,    1.f,    8.f,   3.f,  0.f, 0.f, 0.f },     // Ginkgo
		{ 0.f, 0.f,    0.f,   45.f,  35.f,  0.f, 0.f, 0.f },     // Tree fern
		{ 0.f, 6.f,   16.f,   12.f,   0.f,  5.f, 0.f, 0.f },     // Cycad
		{ 0.f, 2.f,   10.f,    4.f,   0.f,  2.f, 0.f, 0.f },     // Bennettite
		{ 0.f, 22.f,   0.f,    0.f,   4.f,  0.f, 0.f, 0.f },     // Palm
		{ 0.f, 3.f,   14.f,   20.f,   6.f,  3.f, 0.f, 0.f },     // Shrub
		{ 0.f, 0.f,  650.f,  600.f, 300.f, 60.f, 0.f, 0.f },     // Fern
		{ 0.f, 0.f,    0.f,    0.f, 500.f,  0.f, 0.f, 0.f },     // Horsetail
	};
	const float NearFresh[BFPlants::Kinds] = { 0.f, 0.f, 8.f, 5.f, 15.f, 0.f, 0.f, 0.f, 4.f, 60.f, 420.f };
	const float SlopeMax[BFPlants::Kinds] = { 32.f, 32.f, 15.f, 30.f, 30.f, 38.f, 32.f, 20.f, 36.f, 40.f, 20.f };
	const float HeightMax[BFPlants::Kinds] = { 175.f, 165.f, 40.f, 120.f, 110.f, 190.f, 150.f, 20.f, 150.f, 185.f, 80.f };

	// ------------------------------------------------------------------------------------------ building geometry

	struct FGeo
	{
		TArray<FVector3f> P, N;
		TArray<FColor> C;            // colour (sRGB), A how far it sways
		TArray<float> Leaf;          // 1 on leaves (light through them; shrink when eaten)
		TArray<FVector3f> Pull;      // eaten: where a leaf point shrinks to, minus where it is
		TArray<int32> T;
		float TrunkR = 0.f, TrunkH = 0.f;   // a trunk that blocks (0: none)

		int32 V(const FVector3f& Pos, const FVector3f& Nrm, FColor Col, float Sway, float IsLeaf, const FVector3f& Anchor)
		{
			P.Add(Pos);
			N.Add(Nrm.GetSafeNormal(1e-6f, FVector3f::UpVector));
			Col.A = uint8(FMath::Clamp(Sway, 0.f, 1.f) * 255.f);
			C.Add(Col);
			Leaf.Add(IsLeaf);
			Pull.Add(IsLeaf > 0.f ? Anchor - Pos : FVector3f::ZeroVector);
			return P.Num() - 1;
		}
		// Unreal's front faces wind the other way round from the right-hand rule: turn each to face its points' normals.
		void Tri(int32 A, int32 B, int32 D)
		{
			const FVector3f Face = FVector3f::CrossProduct(P[B] - P[A], P[D] - P[A]);
			if (FVector3f::DotProduct(Face, N[A] + N[B] + N[D]) > 0.f)
			{
				Swap(B, D);
			}
			T.Append({ A, B, D });
		}
		void Quad(int32 A, int32 B, int32 D, int32 E) { Tri(A, B, D); Tri(B, E, D); }
	};

	FColor Jit(FRandomStream& R, FColor C, float Amt)
	{
		const float L = 1.f + Amt * R.FRandRange(-1.f, 1.f);
		auto Ch = [&](uint8 V) { return uint8(FMath::Clamp(V * L * (1.f + 0.3f * Amt * R.FRandRange(-1.f, 1.f)), 0.f, 255.f)); };
		return FColor(Ch(C.R), Ch(C.G), Ch(C.B), 255);
	}

	FVector3f Perp(const FVector3f& T)
	{
		const FVector3f A = FMath::Abs(T.Z) < 0.9f ? FVector3f(0.f, 0.f, 1.f) : FVector3f(1.f, 0.f, 0.f);
		return FVector3f::CrossProduct(T, A).GetSafeNormal();
	}

	const FVector3f Up(0.f, 0.f, 1.f);

	/** A tube along Path (radius Rad at each point, Sides round it), parallel-transported so it doesn't twist. */
	void Tube(FGeo& G, const TArray<FVector3f>& Path, const TArray<float>& Rad, int32 Sides, TFunctionRef<FColor(int32, int32)> Col,
		TFunctionRef<float(int32)> Sway, float IsLeaf, const FVector3f& Anchor)
	{
		const int32 Np = Path.Num();
		FVector3f Nn = Perp((Path[1] - Path[0]).GetSafeNormal());
		const int32 Start = G.P.Num();
		for (int32 i = 0; i < Np; ++i)
		{
			const FVector3f Tg = (Path[FMath::Min(i + 1, Np - 1)] - Path[FMath::Max(i - 1, 0)]).GetSafeNormal();
			Nn = (Nn - Tg * FVector3f::DotProduct(Nn, Tg)).GetSafeNormal();
			const FVector3f B = FVector3f::CrossProduct(Tg, Nn);
			for (int32 s = 0; s < Sides; ++s)
			{
				const float A = 2.f * PI * s / Sides;
				const FVector3f D = Nn * FMath::Cos(A) + B * FMath::Sin(A);
				G.V(Path[i] + D * Rad[i], D, Col(i, s), Sway(i), IsLeaf, Anchor);
			}
		}
		for (int32 i = 0; i < Np - 1; ++i)
		{
			for (int32 s = 0; s < Sides; ++s)
			{
				const int32 A = Start + i * Sides + s, B = Start + i * Sides + (s + 1) % Sides;
				G.Quad(A, B, A + Sides, B + Sides);
			}
		}
	}

	/** A simple tube: a straight-ish path from points, radius from R0 to R1, one colour, swaying from S0 to S1. */
	void Limb(FGeo& G, FRandomStream& R, const TArray<FVector3f>& Path, float R0, float R1, int32 Sides, FColor Col, float Jitter, float S0, float S1,
		float IsLeaf, const FVector3f& Anchor)
	{
		TArray<float> Rad;
		TArray<FColor> Ring;
		for (int32 i = 0; i < Path.Num(); ++i)
		{
			Rad.Add(FMath::Lerp(R0, R1, float(i) / (Path.Num() - 1)));
		}
		TArray<FColor> Cols;
		for (int32 i = 0; i < Path.Num() * Sides; ++i)
		{
			Cols.Add(Jit(R, Col, Jitter));
		}
		const int32 N = Path.Num();
		Tube(G, Path, Rad, Sides, [&](int32 i, int32 s) { return Cols[i * Sides + s]; },
			[&](int32 i) { return FMath::Lerp(S0, S1, float(i) / (N - 1)); }, IsLeaf, Anchor);
	}

	/** A leaflet: a slim diamond from Base along Dir (its face to Nrm), its tip drooping. */
	void Leaflet(FGeo& G, const FVector3f& Base, const FVector3f& Dir, const FVector3f& Nrm, float Len, float W, float Droop, FColor Col,
		float S0, float S1, const FVector3f& Anchor)
	{
		const FVector3f Side = FVector3f::CrossProduct(Nrm, Dir).GetSafeNormal();
		const FVector3f Mid = Base + Dir * (0.42f * Len) - Nrm * (Droop * 0.2f * Len);
		const FVector3f Tip = Base + Dir * Len - Nrm * (Droop * Len);
		const int32 A = G.V(Base, Nrm, Col, S0, 1.f, Anchor);
		const int32 B = G.V(Mid + Side * (0.5f * W) + Nrm * (0.12f * W), Nrm, Col, FMath::Lerp(S0, S1, 0.45f), 1.f, Anchor);
		const int32 C = G.V(Mid - Side * (0.5f * W) + Nrm * (0.12f * W), Nrm, Col, FMath::Lerp(S0, S1, 0.45f), 1.f, Anchor);
		const int32 D = G.V(Tip, Nrm, Col, S1, 1.f, Anchor);
		G.Tri(A, B, C);
		G.Tri(B, D, C);
	}

	/** A long strip leaf with a toothed edge (a fern's pinna): Segs pieces along Dir. */
	void Pinna(FGeo& G, const FVector3f& Base, const FVector3f& Dir, const FVector3f& Nrm, float Len, float W, float Droop, int32 Segs, FColor Col,
		float S0, float S1, const FVector3f& Anchor)
	{
		const FVector3f Side = FVector3f::CrossProduct(Nrm, Dir).GetSafeNormal();
		int32 PrevL = INDEX_NONE, PrevR = INDEX_NONE;
		for (int32 i = 0; i <= Segs; ++i)
		{
			const float t = float(i) / Segs;
			const FVector3f P = Base + Dir * (Len * t) - Nrm * (Droop * Len * t * t);
			const float Tooth = (i % 2 == 1) ? 1.f : 0.62f;
			const float HalfW = 0.5f * W * Tooth * FMath::Sin(PI * FMath::Min(0.2f + 0.85f * t, 1.f));
			const float S = FMath::Lerp(S0, S1, t);
			const int32 L = G.V(P + Side * HalfW, Nrm, Col, S, 1.f, Anchor);
			const int32 Rr = G.V(P - Side * HalfW, Nrm, Col, S, 1.f, Anchor);
			if (PrevL != INDEX_NONE)
			{
				G.Quad(PrevL, PrevR, L, Rr);
			}
			PrevL = L;
			PrevR = Rr;
		}
	}

	/** A pinnate frond: a rachis rising from Base toward Out and arching over, its leaflets in pairs along it. */
	struct FFrond
	{
		float Len = 100.f, Rise = 1.f, Droop = 1.f;
		int32 Pairs = 12;
		float LeafLen = 20.f, LeafW = 3.f, LeafAngle = 60.f, LeafDroop = 0.1f;
		int32 PinnaSegs = 0;            // >0: toothed strips (a fern's pinnae), else diamonds
		float RachisR = 1.f;
		int32 RachisSides = 3;
		FColor Col = FColor(60, 100, 40), RachisCol = FColor(80, 80, 50);
		float Sway0 = 0.f, Sway1 = 0.3f;
		float Paddle = 0.f;             // >0: the whole frond one strip this wide (far away)
	};

	void Frond(FGeo& G, FRandomStream& R, const FVector3f& Base, const FVector3f& Out, const FFrond& F, float Jitter = 0.08f)
	{
		auto At = [&](float t) { return Base + Out * (F.Len * t) + Up * (F.Len * (F.Rise * t - F.Droop * t * t)); };
		auto Along = [&](float t) { return (Out + Up * (F.Rise - 2.f * F.Droop * t)).GetSafeNormal(); };
		const FVector3f Side = FVector3f::CrossProduct(Up, Out).GetSafeNormal();
		const FColor Col = Jit(R, F.Col, Jitter);
		if (F.Paddle > 0.f)
		{
			int32 PL = INDEX_NONE, PR = INDEX_NONE;
			for (int32 i = 0; i <= 4; ++i)
			{
				const float t = i / 4.f;
				FVector3f Nrm = FVector3f::CrossProduct(Along(t), Side);
				Nrm = Nrm.Z < 0.f ? -Nrm : Nrm;
				const float W = 0.5f * F.Paddle * FMath::Sin(PI * FMath::Min(0.15f + 0.85f * t, 1.f));
				const float S = FMath::Lerp(F.Sway0, F.Sway1, t);
				const int32 L = G.V(At(t) + Side * W, Nrm, Col, S, 1.f, Base);
				const int32 Rr = G.V(At(t) - Side * W, Nrm, Col, S, 1.f, Base);
				if (PL != INDEX_NONE)
				{
					G.Quad(PL, PR, L, Rr);
				}
				PL = L;
				PR = Rr;
			}
			return;
		}
		if (F.RachisSides > 0)
		{
			TArray<FVector3f> Path;
			for (int32 i = 0; i <= 5; ++i)
			{
				Path.Add(At(i / 5.f));
			}
			Limb(G, R, Path, F.RachisR, 0.3f * F.RachisR, F.RachisSides, F.RachisCol, 0.05f, F.Sway0, F.Sway1, 1.f, Base);
		}
		const float A = FMath::DegreesToRadians(F.LeafAngle);
		for (int32 i = 0; i < F.Pairs; ++i)
		{
			const float t = 0.1f + 0.88f * (i + 0.5f) / F.Pairs;
			const FVector3f P = At(t), Tg = Along(t);
			FVector3f Nrm = FVector3f::CrossProduct(Tg, Side).GetSafeNormal();
			Nrm = Nrm.Z < 0.f ? -Nrm : Nrm;
			// Longest a little below the middle, short at the base and the tip.
			const float Len = F.LeafLen * (0.3f + 0.7f * FMath::Sin(PI * FMath::Min(0.12f + 0.95f * t, 1.f)));
			const float S = FMath::Lerp(F.Sway0, F.Sway1, t);
			for (const float Sgn : { -1.f, 1.f })
			{
				const FVector3f Dir = (Tg * FMath::Cos(A) + Side * (Sgn * FMath::Sin(A)) + Nrm * 0.15f).GetSafeNormal();
				if (F.PinnaSegs > 0)
				{
					Pinna(G, P, Dir, Nrm, Len, F.LeafW, F.LeafDroop, F.PinnaSegs, Col, S, S + 0.05f, Base);
				}
				else
				{
					Leaflet(G, P, Dir, Nrm, Len, F.LeafW, F.LeafDroop, Col, S, S + 0.05f, Base);
				}
			}
		}
	}

	/** A rounded clump of leaves (an icosphere, lumpy): Detail 0 has 20 faces, 1 has 80. */
	void Blob(FGeo& G, FRandomStream& R, const FVector3f& Centre, float Radius, float Squash, int32 Detail, FColor Col, float Jitter, float Sway,
		float IsLeaf, const FVector3f& Anchor)
	{
		static TArray<FVector3f> Ico[2];
		static TArray<FIntVector> IcoT[2];
		if (Ico[0].Num() == 0)
		{
			const float Tt = (1.f + FMath::Sqrt(5.f)) / 2.f;
			Ico[0] = { {-1, Tt, 0}, {1, Tt, 0}, {-1, -Tt, 0}, {1, -Tt, 0}, {0, -1, Tt}, {0, 1, Tt}, {0, -1, -Tt}, {0, 1, -Tt},
				{Tt, 0, -1}, {Tt, 0, 1}, {-Tt, 0, -1}, {-Tt, 0, 1} };
			for (FVector3f& V : Ico[0])
			{
				V.Normalize();
			}
			IcoT[0] = { {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
				{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1} };
			Ico[1] = Ico[0];
			TMap<uint64, int32> Mids;
			auto Mid = [&](int32 A, int32 B)
			{
				const uint64 K = (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B));
				if (const int32* F = Mids.Find(K))
				{
					return *F;
				}
				return Mids.Add(K, Ico[1].Add(((Ico[1][A] + Ico[1][B]) * 0.5f).GetSafeNormal()));
			};
			for (const FIntVector& F : IcoT[0])
			{
				const int32 Ab = Mid(F.X, F.Y), Bc = Mid(F.Y, F.Z), Ca = Mid(F.Z, F.X);
				IcoT[1].Append({ FIntVector(F.X, Ab, Ca), FIntVector(F.Y, Bc, Ab), FIntVector(F.Z, Ca, Bc), FIntVector(Ab, Bc, Ca) });
			}
		}
		const int32 Dt = FMath::Clamp(Detail, 0, 1);
		const int32 Start = G.P.Num();
		for (const FVector3f& U : Ico[Dt])
		{
			const float Lump = 1.f + 0.22f * R.FRandRange(-1.f, 1.f);
			const FVector3f D(U.X, U.Y, U.Z * Squash);
			const float Shade = 0.82f + 0.25f * (0.5f + 0.5f * U.Z);    // darker underneath
			FColor Cc = Jit(R, Col, Jitter);
			Cc = FColor(uint8(FMath::Min(Cc.R * Shade, 255.f)), uint8(FMath::Min(Cc.G * Shade, 255.f)), uint8(FMath::Min(Cc.B * Shade, 255.f)));
			G.V(Centre + D * (Radius * Lump), U, Cc, Sway, IsLeaf, Anchor);
		}
		for (const FIntVector& F : IcoT[Dt])
		{
			G.Tri(Start + F.X, Start + F.Y, Start + F.Z);
		}
	}

	/** A conifer's tier of branches: a jagged skirt from round the trunk (Top) out and down to Radius, drooping Droop. */
	void Skirt(FGeo& G, FRandomStream& R, const FVector3f& Top, float Radius, float Droop, int32 Points, FColor Col, float Sway, const FVector3f& Anchor)
	{
		const float Yaw = R.FRandRange(0.f, 2.f * PI);
		const FVector3f Tilt(R.FRandRange(-0.08f, 0.08f), R.FRandRange(-0.08f, 0.08f), 0.f);
		const int32 C = G.V(Top + Up * (0.15f * Droop), Up, Jit(R, Col, 0.06f), Sway * 0.5f, 1.f, Anchor);
		TArray<int32> Inner, Outer;
		for (int32 i = 0; i < Points; ++i)
		{
			const float A = Yaw + 2.f * PI * i / Points;
			const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0.f);
			const float Reach = Radius * ((i % 2 == 0) ? 1.f : 0.68f) * R.FRandRange(0.85f, 1.1f);
			const FVector3f Nrm = (D * FMath::Max(Droop, 0.2f * Reach) + Up * Reach).GetSafeNormal();
			Inner.Add(G.V(Top + D * (0.42f * Reach) - Up * (0.3f * Droop), Nrm, Jit(R, Col, 0.08f), Sway * 0.75f, 1.f, Anchor));
			FColor TipCol = Jit(R, Col, 0.08f);
			TipCol = FColor(uint8(FMath::Min(TipCol.R * 1.12f, 255.f)), uint8(FMath::Min(TipCol.G * 1.12f, 255.f)), uint8(FMath::Min(TipCol.B * 1.05f, 255.f)));
			Outer.Add(G.V(Top + D * Reach - Up * (Droop + Reach * FVector3f::DotProduct(D, Tilt) + R.FRandRange(-0.06f, 0.06f) * Reach), Nrm, TipCol, Sway, 1.f, Anchor));
		}
		for (int32 i = 0; i < Points; ++i)
		{
			const int32 j = (i + 1) % Points;
			G.Tri(C, Inner[i], Inner[j]);
			G.Quad(Inner[i], Inner[j], Outer[i], Outer[j]);
		}
	}

	// ------------------------------------------------------------------------------------------ the plants

	FVector3f YawDir(float A) { return FVector3f(FMath::Cos(A), FMath::Sin(A), 0.f); }

	/** A trunk from the ground up: Rings points, radius from R0 to R1 (flared at the foot), leaning; coloured by Col. */
	TArray<FVector3f> Trunk(FGeo& G, FRandomStream& R, float H, float R0, float R1, float Flare, int32 Rings, int32 Sides, const FVector3f& Lean,
		TFunctionRef<FColor(int32, int32)> Col, float SwayTop)
	{
		TArray<FVector3f> Path;
		TArray<float> Rad;
		for (int32 i = 0; i < Rings; ++i)
		{
			const float t = float(i) / (Rings - 1);
			Path.Add(FVector3f(0.f, 0.f, H * t) + Lean * (H * t * t));
			Rad.Add(FMath::Lerp(R0, R1, t) * (1.f + Flare * FMath::Exp(-t * 14.f)));
		}
		Tube(G, Path, Rad, Sides, Col, [&](int32 i) { const float t = float(i) / (Rings - 1); return SwayTop * t * t; }, 0.f, FVector3f::ZeroVector);
		G.TrunkR = FMath::Max(G.TrunkR, R0 * 0.85f);
		G.TrunkH = FMath::Max(G.TrunkH, H);
		return Path;
	}

	FVector3f PathAt(const TArray<FVector3f>& Path, float Z)
	{
		for (int32 i = 1; i < Path.Num(); ++i)
		{
			if (Path[i].Z >= Z)
			{
				const float t = (Z - Path[i - 1].Z) / FMath::Max(Path[i].Z - Path[i - 1].Z, 1e-3f);
				return FMath::Lerp(Path[i - 1], Path[i], t);
			}
		}
		return Path.Last();
	}

	void Araucaria(FGeo& G, FRandomStream& R, int32 L)
	{
		const float H = R.FRandRange(2300.f, 3300.f);
		const float From = R.FRandRange(0.55f, 0.66f) * H;
		const FVector3f Lean(R.FRandRange(-0.03f, 0.03f) / H, R.FRandRange(-0.03f, 0.03f) / H, 0.f);
		const FColor Bark(132, 112, 94), Needles(50, 82, 38);
		TArray<FColor> BarkCols;
		for (int32 i = 0; i < 200; ++i)
		{
			BarkCols.Add(Jit(R, Bark, 0.12f));
		}
		const TArray<FVector3f> Stem = Trunk(G, R, H, 0.019f * H, 0.004f * H, 0.45f, L == 0 ? 9 : 5, L == 0 ? 10 : L == 1 ? 7 : 5, Lean,
			[&](int32 i, int32 s) { return BarkCols[(i * 13 + s * 7) % 200]; }, 0.35f);
		if (L == 2)
		{
			Blob(G, R, PathAt(Stem, 0.5f * (From + H)) + Up * 0.08f * H, 0.3f * H, 0.72f, 0, Needles, 0.08f, 0.35f, 1.f, PathAt(Stem, From));
			return;
		}
		const float Step = L == 0 ? 90.f : 170.f;
		int32 Whorl = 0;
		for (float Z = From; Z < 0.97f * H; Z += Step, ++Whorl)
		{
			const float t = (Z - From) / (H - From);
			const float Len = 0.3f * H * FMath::Pow(1.f - t, 0.55f) + 60.f;
			const FVector3f Root = PathAt(Stem, Z);
			const float Sway = 0.35f * (Z / H) * (Z / H);
			for (int32 b = 0; b < 5; ++b)
			{
				const float A = Whorl * 0.7f + 2.f * PI * b / 5.f + R.FRandRange(-0.2f, 0.2f);
				const FVector3f D = YawDir(A);
				TArray<FVector3f> Path;
				const int32 Pts = L == 0 ? 6 : 4;
				for (int32 i = 0; i < Pts; ++i)
				{
					const float s = float(i) / (Pts - 1);
					Path.Add(Root + D * (Len * s) + Up * (Len * (-0.1f * s + 0.3f * s * s)));
				}
				Limb(G, R, Path, 24.f, 12.f, L == 0 ? 7 : 5, Needles, 0.1f, Sway, Sway + 0.3f, 1.f, Root);
				if (L == 0)
				{
					for (int32 k = 0; k < 5; ++k)
					{
						const float s = 0.35f + 0.14f * k;
						const FVector3f From2 = Root + D * (Len * s) + Up * (Len * (-0.1f * s + 0.3f * s * s));
						const FVector3f D2 = (YawDir(A + (k % 2 == 0 ? 0.9f : -0.9f)) + Up * 0.25f).GetSafeNormal();
						Limb(G, R, { From2, From2 + D2 * (0.16f * Len), From2 + D2 * (0.32f * Len) }, 13.f, 7.f, 5, Needles, 0.1f, Sway + 0.15f, Sway + 0.35f, 1.f, Root);
					}
				}
			}
		}
		Blob(G, R, Stem.Last() + Up * 30.f, 70.f, 0.8f, L == 0 ? 1 : 0, Needles, 0.08f, 0.4f, 1.f, Stem.Last());
	}

	/** Redwood (tall and conical) and bald cypress (shorter, buttressed, a broad feathery crown, knees round it). */
	void Conifer(FGeo& G, FRandomStream& R, int32 L, bool bCypress)
	{
		const float H = bCypress ? R.FRandRange(1700.f, 2500.f) : R.FRandRange(2800.f, 4200.f);
		const float From = (bCypress ? R.FRandRange(0.38f, 0.48f) : R.FRandRange(0.24f, 0.34f)) * H;
		const float MaxR = (bCypress ? 0.2f : 0.15f) * H;
		const FVector3f Lean(R.FRandRange(-0.02f, 0.02f) / H, R.FRandRange(-0.02f, 0.02f) / H, 0.f);
		const FColor Bark = bCypress ? FColor(112, 96, 82) : FColor(156, 92, 62);
		const FColor Leaves = bCypress ? FColor(78, 100, 50) : FColor(36, 58, 38);
		TArray<FColor> BarkCols;
		for (int32 i = 0; i < 200; ++i)
		{
			BarkCols.Add(Jit(R, Bark, 0.1f));
		}
		const TArray<FVector3f> Stem = Trunk(G, R, H, (bCypress ? 0.026f : 0.022f) * H, 0.003f * H, bCypress ? 1.3f : 0.5f, L == 0 ? 9 : 5,
			L == 0 ? (bCypress ? 12 : 10) : L == 1 ? 7 : 5, Lean, [&](int32 i, int32 s) { return BarkCols[(i * 11 + s * 3) % 200]; }, 0.3f);
		if (bCypress && L == 0)
		{
			for (int32 k = 0; k < 6; ++k)        // knees: woody cones standing up out of the swamp round the tree
			{
				const FVector3f At = YawDir(R.FRandRange(0.f, 2.f * PI)) * R.FRandRange(120.f, 280.f) - Up * 10.f;
				const float Kh = R.FRandRange(30.f, 75.f);
				Limb(G, R, { At, At + Up * (0.5f * Kh), At + Up * Kh }, R.FRandRange(9.f, 15.f), 2.f, 6, Bark, 0.1f, 0.f, 0.f, 0.f, At);
			}
		}
		if (L == 2)
		{
			Skirt(G, R, Stem.Last() + Up * 40.f, MaxR, H - From, 8, Leaves, 0.3f, PathAt(Stem, From));
			return;
		}
		const int32 Tiers = (bCypress ? 12 : 16) / (L == 0 ? 1 : 2);
		for (int32 i = 0; i < Tiers; ++i)
		{
			const float t = float(i) / (Tiers - 1);
			const float Z = From + (H - From) * FMath::Pow(t, 0.95f);
			const float Rr = MaxR * (bCypress ? FMath::Pow(1.f - t, 0.5f) : FMath::Pow(1.f - t, 0.85f)) + 40.f;
			const float Sway = 0.35f * (Z / H) * (Z / H);
			Skirt(G, R, PathAt(Stem, Z), Rr, Rr * (bCypress ? 0.22f : 0.34f), L == 0 ? 13 : 9, Leaves, Sway + 0.1f, PathAt(Stem, Z));
		}
	}

	void Ginkgo(FGeo& G, FRandomStream& R, int32 L)
	{
		const float H = R.FRandRange(1300.f, 2000.f);
		const FColor Bark(118, 108, 96), Leaves(112, 138, 52);
		const TArray<FVector3f> Stem = Trunk(G, R, 0.42f * H, 0.022f * H, 0.012f * H, 0.3f, L == 0 ? 5 : 3, L == 0 ? 8 : 5, FVector3f::ZeroVector,
			[&](int32 i, int32 s) { return Bark; }, 0.1f);
		if (L == 2)
		{
			Blob(G, R, FVector3f(0.f, 0.f, 0.66f * H), 0.3f * H, 0.8f, 0, Leaves, 0.1f, 0.3f, 1.f, FVector3f(0.f, 0.f, 0.45f * H));
			return;
		}
		const int32 Limbs = 4;
		for (int32 k = 0; k < Limbs; ++k)
		{
			const float Yaw = 2.f * PI * k / Limbs + R.FRandRange(-0.4f, 0.4f);
			const float Elev = FMath::DegreesToRadians(R.FRandRange(35.f, 58.f));
			const FVector3f D = YawDir(Yaw) * FMath::Cos(Elev) + Up * FMath::Sin(Elev);
			const FVector3f From = FVector3f(0.f, 0.f, R.FRandRange(0.3f, 0.42f) * H);
			const float Len = R.FRandRange(0.38f, 0.5f) * H;
			const FVector3f To = From + D * Len;
			Limb(G, R, { From, From + D * (0.5f * Len), To }, 0.011f * H, 0.004f * H, L == 0 ? 6 : 4, Bark, 0.08f, 0.05f, 0.25f, 0.f, From);
			Blob(G, R, To, R.FRandRange(0.1f, 0.13f) * H, 0.75f, L == 0 ? 1 : 0, Leaves, 0.12f, 0.35f, 1.f, From);
			Blob(G, R, From + D * (0.55f * Len) + YawDir(Yaw + 1.2f) * 0.05f * H, R.FRandRange(0.07f, 0.09f) * H, 0.75f, L == 0 ? 1 : 0, Leaves, 0.12f, 0.3f, 1.f, From);
			if (L == 0)
			{
				for (const float Turn : { -0.6f, 0.6f })
				{
					const FVector3f B0 = From + D * (0.6f * Len);
					const FVector3f D2 = (YawDir(Yaw + Turn) + Up * 0.8f).GetSafeNormal();
					const FVector3f B1 = B0 + D2 * (0.25f * H);
					Limb(G, R, { B0, B1 }, 0.005f * H, 0.002f * H, 4, Bark, 0.08f, 0.2f, 0.3f, 0.f, From);
					Blob(G, R, B1, R.FRandRange(0.07f, 0.1f) * H, 0.75f, 1, Leaves, 0.12f, 0.35f, 1.f, From);
				}
			}
		}
	}

	void TreeFern(FGeo& G, FRandomStream& R, int32 L)
	{
		const float H = R.FRandRange(280.f, 620.f);
		const FColor Fibre(104, 80, 56);
		TArray<FColor> Cols;
		for (int32 i = 0; i < 100; ++i)
		{
			Cols.Add(Jit(R, Fibre, 0.22f));
		}
		const FVector3f Lean(R.FRandRange(-0.08f, 0.08f) / H, R.FRandRange(-0.08f, 0.08f) / H, 0.f);
		const TArray<FVector3f> Stem = Trunk(G, R, H, 15.f, 11.f, 0.35f, L == 0 ? 7 : 4, L == 0 ? 8 : 5, Lean,
			[&](int32 i, int32 s) { return Cols[(i * 7 + s * 3) % 100]; }, 0.25f);
		const FVector3f Top = Stem.Last();
		const int32 Fronds = L == 0 ? 12 : L == 1 ? 8 : 7;
		for (int32 k = 0; k < Fronds; ++k)
		{
			FFrond F;
			F.Len = R.FRandRange(240.f, 330.f);
			F.Rise = R.FRandRange(0.55f, 0.8f);
			F.Droop = R.FRandRange(0.9f, 1.2f);
			F.Pairs = L == 0 ? 18 : 9;
			F.LeafLen = L == 0 ? 52.f : 48.f;
			F.LeafW = L == 0 ? 9.f : 14.f;
			F.LeafAngle = 75.f;
			F.LeafDroop = 0.15f;
			F.PinnaSegs = L == 0 ? 4 : 0;
			F.RachisR = 2.2f;
			F.RachisSides = L == 0 ? 4 : 3;
			F.Col = FColor(68, 112, 40);
			F.RachisCol = FColor(88, 82, 46);
			F.Sway0 = 0.15f;
			F.Sway1 = 0.55f;
			F.Paddle = L == 2 ? 70.f : 0.f;
			Frond(G, R, Top, YawDir(k * 2.39996f + R.FRandRange(-0.2f, 0.2f)), F);
		}
		if (L == 0)
		{
			for (int32 c = 0; c < 3; ++c)        // croziers: young fronds still coiled at the crown
			{
				const FVector3f D = YawDir(c * 2.1f);
				TArray<FVector3f> Path;
				for (int32 i = 0; i <= 10; ++i)
				{
					const float A = i * 0.55f;
					const float Rad = 12.f * (1.f - 0.07f * i);
					Path.Add(Top + D * (6.f + Rad * FMath::Sin(A)) + Up * (14.f + Rad * (1.f - FMath::Cos(A)) + 8.f * (1.f - i / 10.f)));
				}
				Limb(G, R, Path, 2.6f, 1.6f, 4, FColor(110, 120, 52), 0.05f, 0.15f, 0.2f, 1.f, Top);
			}
		}
	}

	void CycadLike(FGeo& G, FRandomStream& R, int32 L, bool bBennettite)
	{
		const float H = bBennettite ? R.FRandRange(60.f, 110.f) : R.FRandRange(50.f, 220.f);
		const float R0 = bBennettite ? R.FRandRange(26.f, 34.f) : FMath::Lerp(30.f, 20.f, (H - 50.f) / 170.f);
		const FColor Light = bBennettite ? FColor(160, 138, 98) : FColor(150, 130, 94), Dark = bBennettite ? FColor(118, 100, 70) : FColor(108, 92, 64);
		const int32 Sides = L == 0 ? 12 : L == 1 ? 8 : 6;
		const int32 Rings = L == 0 ? 8 : 4;
		TArray<FVector3f> Path;
		TArray<float> Rad;
		const FVector3f Lean(R.FRandRange(-0.1f, 0.1f), R.FRandRange(-0.1f, 0.1f), 0.f);
		for (int32 i = 0; i < Rings; ++i)
		{
			const float t = float(i) / (Rings - 1);
			Path.Add(FVector3f(0.f, 0.f, H * t) + Lean * (H * t * t));
			Rad.Add(bBennettite ? R0 * (0.75f + 0.3f * FMath::Sin(PI * FMath::Min(t * 1.1f, 1.f))) * (t > 0.95f ? 0.7f : 1.f) : R0 * (1.f - 0.15f * t));
		}
		// The leaf bases' diamond pattern: alternating light and dark round and up the trunk.
		Tube(G, Path, Rad, Sides, [&](int32 i, int32 s) { return ((i + s) % 2 == 0) ? Light : Dark; }, [&](int32) { return 0.f; }, 0.f, FVector3f::ZeroVector);
		G.TrunkR = bBennettite ? 0.f : R0 * 0.85f;
		G.TrunkH = bBennettite ? 0.f : H;
		const FVector3f Top = Path.Last();
		const int32 Fronds = bBennettite ? (L == 0 ? 18 : L == 1 ? 11 : 8) : (L == 0 ? 22 : L == 1 ? 13 : 9);
		for (int32 k = 0; k < Fronds; ++k)
		{
			FFrond F;
			F.Len = bBennettite ? R.FRandRange(110.f, 160.f) : R.FRandRange(110.f, 170.f);
			const bool bInner = k < Fronds / 3;
			F.Rise = bInner ? R.FRandRange(1.4f, 2.f) : R.FRandRange(0.7f, 1.2f);
			F.Droop = bInner ? 0.6f : R.FRandRange(0.8f, 1.1f);
			F.Pairs = L == 0 ? (bBennettite ? 18 : 26) : 12;
			F.LeafLen = bBennettite ? 17.f : 20.f;
			F.LeafW = L == 0 ? (bBennettite ? 2.2f : 2.4f) : 4.5f;
			F.LeafAngle = 62.f;
			F.LeafDroop = 0.05f;
			F.RachisR = 1.4f;
			F.RachisSides = L == 0 ? 3 : 0;
			F.Col = bBennettite ? FColor(58, 88, 40) : FColor(46, 80, 34);
			F.RachisCol = FColor(70, 86, 44);
			F.Sway0 = 0.05f;
			F.Sway1 = 0.22f;
			F.Paddle = L == 2 ? (bBennettite ? 14.f : 18.f) : 0.f;
			Frond(G, R, Top - Up * 4.f, YawDir(k * 2.39996f + R.FRandRange(-0.15f, 0.15f)), F, 0.06f);
		}
		if (L < 2 && !bBennettite)
		{
			Blob(G, R, Top + Up * 12.f, 16.f, 1.1f, L == 0 ? 1 : 0, FColor(160, 128, 62), 0.08f, 0.f, 0.f, Top);     // its cone
		}
		if (L < 2 && bBennettite)
		{
			for (int32 k = 0; k < 6; ++k)     // flower-like cones (Williamsonia) tucked among the leaf bases
			{
				const float A = k * 1.05f + R.FRandRange(-0.2f, 0.2f);
				const FVector3f At = Top - Up * R.FRandRange(10.f, 30.f) + YawDir(A) * (0.8f * Rad.Last());
				Blob(G, R, At, R.FRandRange(7.f, 10.f), 1.2f, 0, (R.FRand() < 0.5f) ? FColor(214, 170, 80) : FColor(180, 84, 64), 0.08f, 0.f, 0.f, At);
			}
		}
	}

	void Palm(FGeo& G, FRandomStream& R, int32 L)
	{
		const float H = R.FRandRange(380.f, 750.f);
		const FVector3f Curve(R.FRandRange(-0.25f, 0.25f) / H, R.FRandRange(-0.25f, 0.25f) / H, 0.f);
		const TArray<FVector3f> Stem = Trunk(G, R, H, 17.f, 14.f, 0.4f, L == 0 ? 12 : 5, L == 0 ? 8 : 5, Curve,
			[&](int32 i, int32 s) { return (i % 2 == 0) ? FColor(150, 132, 104) : FColor(118, 102, 78); }, 0.35f);
		const FVector3f Top = Stem.Last();
		const int32 Leaves = L == 0 ? 12 : L == 1 ? 8 : 6;
		const int32 Ribs = L == 0 ? 16 : L == 1 ? 8 : 4;
		const FColor Green(70, 104, 46);
		for (int32 k = 0; k < Leaves; ++k)
		{
			const float Yaw = k * 2.39996f;
			const float Elev = FMath::DegreesToRadians(FMath::Lerp(55.f, -15.f, float(k) / Leaves) + R.FRandRange(-8.f, 8.f));
			const FVector3f D = YawDir(Yaw) * FMath::Cos(Elev) + Up * FMath::Sin(Elev);
			const float Stalk = R.FRandRange(110.f, 160.f);
			const FVector3f End = Top + D * Stalk;
			Limb(G, R, { Top, Top + D * (0.5f * Stalk), End }, 3.5f, 2.5f, L == 0 ? 4 : 3, FColor(88, 104, 58), 0.05f, 0.2f, 0.35f, 1.f, Top);
			// The fan: ribs spread over 200 deg in the plane across the stalk, pleated up and down, the tips drooping.
			const FVector3f Side = FVector3f::CrossProduct(Up, D).GetSafeNormal();
			const FVector3f FanUp = FVector3f::CrossProduct(D, Side).GetSafeNormal();
			const float Fan = R.FRandRange(110.f, 140.f);
			const FColor Col = Jit(R, Green, 0.08f);
			TArray<int32> Prev;
			for (int32 r = 0; r <= Ribs; ++r)
			{
				const float A = FMath::DegreesToRadians(-100.f + 200.f * r / Ribs);
				const FVector3f Dir = (D * FMath::Cos(A) + Side * FMath::Sin(A)).GetSafeNormal();
				const float Pleat = (r % 2 == 0 ? 1.f : -1.f) * 6.f;
				TArray<int32> Cur;
				for (const float Rr : { 0.12f, 0.6f, 1.f })
				{
					const FVector3f P = End + Dir * (Fan * Rr) + FanUp * (Pleat * Rr) - Up * (0.3f * Fan * Rr * Rr);
					Cur.Add(G.V(P, FanUp, Col, 0.35f + 0.3f * Rr, 1.f, End));
				}
				if (Prev.Num() > 0)
				{
					G.Quad(Prev[0], Cur[0], Prev[1], Cur[1]);
					G.Quad(Prev[1], Cur[1], Prev[2], Cur[2]);
				}
				Prev = Cur;
			}
		}
	}

	void Shrub(FGeo& G, FRandomStream& R, int32 L)
	{
		const float H = R.FRandRange(120.f, 260.f);
		const FColor Stem(92, 74, 56), Leaf(56, 92, 38);
		const bool bPink = (R.FRand() < 0.5f);
		if (L == 2)
		{
			Blob(G, R, FVector3f(0.f, 0.f, 0.55f * H), 0.42f * H, 0.8f, 0, Leaf, 0.1f, 0.2f, 1.f, FVector3f::ZeroVector);
			return;
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const float Yaw = 2.f * PI * k / 4.f + R.FRandRange(-0.4f, 0.4f);
			const float Elev = FMath::DegreesToRadians(R.FRandRange(55.f, 78.f));
			const FVector3f D = YawDir(Yaw) * FMath::Cos(Elev) + Up * FMath::Sin(Elev);
			const float Len = R.FRandRange(0.75f, 0.95f) * H;
			const FVector3f Base = YawDir(Yaw) * 6.f;
			const FVector3f Mid = Base + D * (0.55f * Len), Tip = Base + D * Len + YawDir(Yaw) * (0.1f * Len);
			Limb(G, R, { Base, Mid, Tip }, 3.5f, 1.5f, L == 0 ? 5 : 4, Stem, 0.08f, 0.f, 0.25f, 0.f, Base);
			for (const FVector3f& At : { Mid, Tip })
			{
				if (L == 1)
				{
					Blob(G, R, At, 0.22f * H, 0.8f, 0, Leaf, 0.1f, 0.25f, 1.f, Base);
					continue;
				}
				for (int32 l = 0; l < 12; ++l)
				{
					const float A = R.FRandRange(0.f, 2.f * PI);
					const FVector3f Ld = (YawDir(A) + Up * R.FRandRange(-0.2f, 0.7f)).GetSafeNormal();
					const FVector3f Nrm = (Up - Ld * FVector3f::DotProduct(Up, Ld)).GetSafeNormal();
					Leaflet(G, At + Ld * 3.f, Ld, Nrm, R.FRandRange(15.f, 21.f), R.FRandRange(6.f, 8.f), 0.25f, Jit(R, Leaf, 0.1f), 0.2f, 0.3f, Base);
				}
				if (At == Tip)    // a flower: six pale petals in a cup round a yellow middle
				{
					const FColor Petal = bPink ? FColor(226, 176, 186) : FColor(238, 226, 200);
					for (int32 p = 0; p < 6; ++p)
					{
						const FVector3f Pd = (YawDir(p * PI / 3.f) + Up * 1.2f).GetSafeNormal();
						const FVector3f Nrm = (Up - Pd * FVector3f::DotProduct(Up, Pd)).GetSafeNormal();
						Leaflet(G, At + Up * 4.f, Pd, Nrm, 9.f, 5.f, -0.1f, Petal, 0.25f, 0.3f, Base);
					}
					Blob(G, R, At + Up * 6.f, 2.5f, 1.f, 0, FColor(230, 200, 90), 0.05f, 0.25f, 1.f, Base);
				}
			}
		}
	}

	void Fern(FGeo& G, FRandomStream& R, int32 L)
	{
		const float Size = R.FRandRange(80.f, 150.f);
		const int32 Fronds = L == 0 ? 13 : L == 1 ? 8 : 6;
		for (int32 k = 0; k < Fronds; ++k)
		{
			const bool bDead = L < 2 && R.FRandRange(0.f, 1.f) < 0.12f;      // the odd old brown frond, lying lower
			FFrond F;
			F.Len = Size * R.FRandRange(0.8f, 1.15f);
			F.Rise = bDead ? 0.5f : R.FRandRange(1.2f, 1.8f);
			F.Droop = bDead ? 0.7f : R.FRandRange(1.0f, 1.5f);
			F.Pairs = L == 0 ? 13 : 7;
			F.LeafLen = 0.15f * Size;
			F.LeafW = L == 0 ? 5.f : 7.f;
			F.LeafAngle = 70.f;
			F.LeafDroop = 0.12f;
			F.PinnaSegs = L == 0 ? 3 : 0;
			F.RachisSides = 0;
			F.Col = bDead ? FColor(118, 92, 52) : FColor(64, 108, 40);
			F.Sway0 = 0.f;
			F.Sway1 = 0.25f;
			F.Paddle = L == 2 ? 0.25f * Size : 0.f;
			Frond(G, R, FVector3f(0.f, 0.f, 2.f), YawDir(k * 2.39996f + R.FRandRange(-0.25f, 0.25f)), F, 0.12f);
		}
	}

	void Horsetail(FGeo& G, FRandomStream& R, int32 L)
	{
		const int32 Stems = L == 0 ? 14 : L == 1 ? 8 : 6;
		const float Tall = R.FRandRange(90.f, 230.f);
		const FColor Stem(98, 124, 60), Node(46, 52, 34), Twig(90, 118, 56);
		for (int32 k = 0; k < Stems; ++k)
		{
			const FVector3f Base = YawDir(R.FRandRange(0.f, 2.f * PI)) * R.FRandRange(0.f, 45.f);
			const float H = Tall * R.FRandRange(0.6f, 1.1f);
			const float Rad = R.FRandRange(1.6f, 3.2f);
			const FVector3f Lean(R.FRandRange(-0.12f, 0.12f), R.FRandRange(-0.12f, 0.12f), 1.f);
			const int32 Segs = L == 0 ? FMath::Max(3, int32(H / 16.f)) : L == 1 ? 3 : 1;
			TArray<FVector3f> Path;
			TArray<float> Rads;
			for (int32 i = 0; i <= Segs; ++i)
			{
				Path.Add(Base + Lean * (H * i / Segs));
				Rads.Add(Rad * (1.f - 0.5f * i / Segs));
			}
			Tube(G, Path, Rads, L == 0 ? 5 : L == 1 ? 4 : 3, [&](int32 i, int32 s) { return (L == 0 && i > 0 && i < Segs) ? Node : Stem; },
				[&](int32 i) { const float t = float(i) / Segs; return 0.5f * t * t; }, 1.f, Base);
			if (L == 0 && R.FRandRange(0.f, 1.f) < 0.7f)
			{
				for (int32 i = 1; i < Segs; ++i)        // whorls of thin branches at the joints, up the middle of the stem
				{
					const float t = float(i) / Segs;
					if (t < 0.25f || t > 0.8f)
					{
						continue;
					}
					for (int32 b = 0; b < 7; ++b)
					{
						const FVector3f Bd = (YawDir(2.f * PI * b / 7.f + i) + Up * 0.7f).GetSafeNormal();
						const FVector3f Nrm = (Up - Bd * FVector3f::DotProduct(Up, Bd)).GetSafeNormal();
						Leaflet(G, Path[i], Bd, Nrm, R.FRandRange(14.f, 24.f) * (1.1f - t), 1.2f, 0.2f, Twig, 0.5f * t * t, 0.5f * t * t + 0.1f, Base);
					}
				}
			}
			else if (L == 0)
			{
				Blob(G, R, Path.Last() + Up * 3.f, 2.4f, 2.2f, 0, FColor(126, 98, 60), 0.05f, 0.5f, 1.f, Base);     // a cone at the tip
			}
		}
	}

	void Build(FGeo& G, EBFPlant P, int32 Variant, int32 Lod)
	{
		FRandomStream R(int32(P) * 7919 + Variant * 104729 + 11);
		switch (P)
		{
		case EBFPlant::Araucaria: Araucaria(G, R, Lod); break;
		case EBFPlant::Redwood: Conifer(G, R, Lod, false); break;
		case EBFPlant::Cypress: Conifer(G, R, Lod, true); break;
		case EBFPlant::Ginkgo: Ginkgo(G, R, Lod); break;
		case EBFPlant::TreeFern: TreeFern(G, R, Lod); break;
		case EBFPlant::Cycad: CycadLike(G, R, Lod, false); break;
		case EBFPlant::Bennettite: CycadLike(G, R, Lod, true); break;
		case EBFPlant::Palm: Palm(G, R, Lod); break;
		case EBFPlant::Shrub: Shrub(G, R, Lod); break;
		case EBFPlant::Fern: Fern(G, R, Lod); break;
		case EBFPlant::Horsetail: Horsetail(G, R, Lod); break;
		default: break;
		}
	}

	void ToDescription(const FGeo& G, FMeshDescription& MD)
	{
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> Col = Attr.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UV = Attr.GetVertexInstanceUVs();
		UV.SetNumChannels(3);
		const FPolygonGroupID Group = MD.CreatePolygonGroup();
		Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName(TEXT("Plant"));
		MD.ReserveNewVertices(G.P.Num());
		MD.ReserveNewVertexInstances(G.P.Num());
		MD.ReserveNewTriangles(G.T.Num() / 3);
		MD.ReserveNewPolygons(G.T.Num() / 3);
		TArray<FVertexInstanceID> Ids;
		Ids.Reserve(G.P.Num());
		for (int32 i = 0; i < G.P.Num(); ++i)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = G.P[i];
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = G.N[i];
			Tan[I] = Perp(G.N[i]);
			Sign[I] = 1.f;
			Col[I] = FVector4f(FLinearColor(G.C[i]));
			UV.Set(I, 0, FVector2f(G.Leaf[i], 0.f));
			UV.Set(I, 1, FVector2f(G.Pull[i].X, G.Pull[i].Y));
			UV.Set(I, 2, FVector2f(G.Pull[i].Z, 0.f));
			Ids.Add(I);
		}
		for (int32 t = 0; t + 2 < G.T.Num(); t += 3)
		{
			MD.CreateTriangle(Group, { Ids[G.T[t]], Ids[G.T[t + 1]], Ids[G.T[t + 2]] });
		}
	}

	TStrongObjectPtr<UStaticMesh> Cache[BFPlants::Kinds][BFPlants::Variants];
	int32 Tris[BFPlants::Kinds][BFPlants::Variants] = {};
}

const FBFPlantKind& BFPlants::Kind(EBFPlant P)
{
	return KindTable[FMath::Clamp(int32(P), 0, Kinds - 1)];
}

float BFPlants::Density(EBFPlant P, uint8 Biome)
{
	return Biome < 8 ? DensityTable[int32(P)][Biome] : 0.f;
}

float BFPlants::NearFreshWater(EBFPlant P)
{
	return NearFresh[int32(P)];
}

float BFPlants::MaxSlope(EBFPlant P)
{
	return SlopeMax[int32(P)];
}

float BFPlants::MaxHeightM(EBFPlant P)
{
	return HeightMax[int32(P)];
}

int32 BFPlants::Triangles(EBFPlant P, int32 Variant)
{
	return Tris[int32(P)][Variant];
}

UStaticMesh* BFPlants::Mesh(EBFPlant P, int32 Variant, UMaterialInterface* Mat)
{
	TStrongObjectPtr<UStaticMesh>& Slot = Cache[int32(P)][Variant];
	if (Slot.IsValid())
	{
		return Slot.Get();
	}
	FMeshDescription Lods[3];
	FGeo Fine;
	for (int32 L = 0; L < 3; ++L)
	{
		FGeo G;
		Build(G, P, Variant, L);
		ToDescription(G, Lods[L]);
		if (L == 0)
		{
			Fine = MoveTemp(G);
		}
	}
	Tris[int32(P)][Variant] = Fine.T.Num() / 3;
	UStaticMesh* SM = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	SM->NeverStream = true;
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName(TEXT("Plant")), FName(TEXT("Plant"))));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = false;
	Params.bAllowCpuAccess = false;
	SM->BuildFromMeshDescriptions({ &Lods[0], &Lods[1], &Lods[2] }, Params);
	// Levels of detail by how much of the screen it fills: the trees keep their shape further off.
	const bool bBig = Kind(P).CullCm > 200000.f;
	if (FStaticMeshRenderData* RD = SM->GetRenderData())
	{
		const float Sizes[3] = { 1.f, bBig ? 0.3f : 0.1f, bBig ? 0.07f : 0.03f };
		for (int32 L = 0; L < 3 && L < RD->LODResources.Num(); ++L)
		{
			RD->ScreenSize[L].Default = Sizes[L];
		}
	}
	if (Fine.TrunkR > 0.f && Fine.TrunkH > 0.f)
	{
		// The trunk blocks: a capsule up it (creatures walk round trees, not through them).
		SM->CreateBodySetup();
		UBodySetup* Body = SM->GetBodySetup();
		Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		const float Len = FMath::Max(Fine.TrunkH - 2.f * Fine.TrunkR, 1.f);
		FKSphylElem Cap(Fine.TrunkR, Len);
		Cap.Center = FVector(0.0, 0.0, 0.5 * Len + Fine.TrunkR);
		Body->AggGeom.SphylElems.Add(Cap);
		Body->CreatePhysicsMeshes();
	}
	Slot.Reset(SM);
	return SM;
}
