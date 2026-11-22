#include "BFWorld.h"
#include "BFEffects.h"

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "BFSpecies.h"
#include "BFGameMode.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

ABFWorld* ABFWorld::Active = nullptr;

FString ABFWorld::GetStage() const
{
	static const TCHAR* Words[] = { TEXT("Laying out the land"), TEXT("Rain wearing it down"), TEXT("Water finding its way down"),
		TEXT("Carving rivers and filling lakes"), TEXT("Plains, forest and swamp"), TEXT("Building the ground"), TEXT("Growing the plants"), TEXT("Ready") };
	return Words[FMath::Clamp(StageNo.load(), 0, 7)];
}

namespace
{
	// The ground's colours by biome (linear-ish sRGB bytes; the terrain material shows vertex colours).
	const FColor BiomeColour[] =
	{
		FColor(52, 64, 66),      // Sea: its floor, seen through the water
		FColor(204, 189, 143),   // Beach
		FColor(72, 98, 44),      // Plains
		FColor(52, 64, 36),      // Forest
		FColor(64, 74, 40),      // Swamp
		FColor(114, 113, 110),   // Rock (2026-11-17: cooler grey; 124,118,108 read as cream-tan in the sun from above)
		FColor(84, 74, 54),      // Lake bed
		FColor(84, 74, 54),      // River bed
	};
	const TCHAR* BiomeName[] = { TEXT("sea"), TEXT("beach"), TEXT("plains"), TEXT("forest"), TEXT("swamp"), TEXT("rock"), TEXT("lake"), TEXT("river") };
}

ABFWorld::ABFWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ABFWorld::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Work.IsValid())
	{
		Work.Wait();
	}
	if (Active == this)
	{
		Active = nullptr;
	}
	Super::EndPlay(Reason);
}

void ABFWorld::Generate(int32 InSeed)
{
	Seed = InSeed;
	// Ten kilometres east of the lab's field, its sea level the lab's ground level.
	Origin = FVector(1000000.0 - 0.5 * SizeCm(), -0.5 * SizeCm(), 0.0);
	bReady = false;
	bLandDone = false;
	NextChunk = 0;
	Caves.Reset();
	Places.Reset();
	GroundTile.Reset();
	RockSets.Reset();
	RockCells.Reset();
	RockSummary.Reset();
	Progress = 0.f;
	StageNo = 0;
	StartedAt = FPlatformTime::Seconds();
	Work = Async(EAsyncExecution::Thread, [this]() { MakeLand(); bLandDone = true; });
}

float ABFWorld::HeightM(float GX, float GY) const
{
	GX = FMath::Clamp(GX, 0.f, float(N - 1) - 0.001f);
	GY = FMath::Clamp(GY, 0.f, float(N - 1) - 0.001f);
	const int32 X = FMath::FloorToInt(GX), Y = FMath::FloorToInt(GY);
	const float FX = GX - X, FY = GY - Y;
	return FMath::Lerp(FMath::Lerp(H[Idx(X, Y)], H[Idx(X + 1, Y)], FX), FMath::Lerp(H[Idx(X, Y + 1)], H[Idx(X + 1, Y + 1)], FX), FY);
}

void ABFWorld::MakeLand()
{
	const double T0 = FPlatformTime::Seconds();
	FRandomStream R(Seed);
	FVector2D Off[8];
	for (FVector2D& O : Off)
	{
		O = FVector2D(R.FRandRange(-20000.f, 20000.f), R.FRandRange(-20000.f, 20000.f));
	}
	auto Noise = [&](int32 K, float X, float Y) { return FMath::PerlinNoise2D(FVector2D(X, Y) + Off[K]); };
	auto Fbm = [&](int32 K, float X, float Y, int32 Octaves)
	{
		float Sum = 0.f, Amp = 1.f, Freq = 1.f, Norm = 0.f;
		for (int32 o = 0; o < Octaves; ++o)
		{
			Sum += Amp * Noise(K, X * Freq, Y * Freq);
			Norm += Amp;
			Amp *= 0.5f;
			Freq *= 2.f;
		}
		return Sum / Norm;
	};
	const float CellM = CellCm / 100.f;
	const float SizeM = (N - 1) * CellM;

	// The land: a round island pushed in and out by noise; broad rises and basins, rolling hills, and a rocky spine where a
	// slow noise says mountains (ridged noise: sharp crests, wide valleys). Metres above the sea.
	// (2026-11-16, Raven: "the map is mostly ocean around the small island ... a bigger island or some smaller masses out
	// in the sea ... other than just shallow flat ocean". The island reaches further out (its coast at 0.7-0.97 of the way
	// to the edge, was 0.62-0.92); out in the sea round it, 3-6 islets (90-280 m across, a rocky knoll or a low cay) and
	// a few sea stacks; the sea floor has banks and shoals (some 2-6 m under the water, showing pale) and deepens toward
	// the edge.) -bfoldsea: as before.
	const bool bOldSea = FParse::Param(FCommandLine::Get(), TEXT("bfoldsea"));
	struct FIslet { FVector2D At; float R; float Peak; };
	TArray<FIslet> Islets;
	FVector2D SeaOff[3];
	{
		FRandomStream Ri(Seed + 4242);
		for (FVector2D& O : SeaOff)
		{
			O = FVector2D(Ri.FRandRange(-20000.f, 20000.f), Ri.FRandRange(-20000.f, 20000.f));
		}
		const int32 Want = bOldSea ? 0 : Ri.RandRange(5, 9), Stacks = bOldSea ? 0 : Ri.RandRange(4, 8);
		for (int32 Tries = 0; Tries < 400 && Islets.Num() < Want + Stacks; ++Tries)
		{
			const bool bStack = Islets.Num() >= Want;
			const FVector2D Uv(Ri.FRandRange(-0.86f, 0.86f), Ri.FRandRange(-0.86f, 0.86f));
			if (Uv.Size() < (bStack ? 0.98f : 1.04f))
			{
				continue;          // (out past the island's coast)
			}
			const FIslet It{ FVector2D((Uv.X + 1.f) * 0.5f * SizeM, (Uv.Y + 1.f) * 0.5f * SizeM),
				bStack ? Ri.FRandRange(10.f, 22.f) : Ri.FRandRange(45.f, Islets.Num() < 2 ? 220.f : 140.f), bStack ? Ri.FRandRange(12.f, 30.f) : Ri.FRandRange(4.f, 42.f) };
			if (Islets.ContainsByPredicate([&](const FIslet& O) { return FVector2D::Distance(O.At, It.At) < O.R + It.R + 150.f; }))
			{
				continue;
			}
			Islets.Add(It);
		}
	}
	const float CoastIn = bOldSea ? 0.62f : 0.7f, CoastOut = bOldSea ? 0.92f : 0.97f;
	H.SetNumUninitialized(N * N);
	ParallelFor(N, [&](int32 Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const float MX = X * CellM, MY = Y * CellM;
			const float U = 2.f * MX / SizeM - 1.f, V = 2.f * MY / SizeM - 1.f;
			const float D = FMath::Sqrt(U * U + V * V) + 0.22f * Fbm(0, MX / 900.f, MY / 900.f, 3);
			const float Land = 1.f - FMath::SmoothStep(CoastIn, CoastOut, D);
			const float Rises = Fbm(1, MX / 1600.f, MY / 1600.f, 4);
			const float Hills = Fbm(2, MX / 450.f, MY / 450.f, 5);
			float Ridge = 1.f - FMath::Abs(Fbm(3, MX / 800.f, MY / 800.f, 4));
			Ridge = Ridge * Ridge * Ridge;
			const float Mountains = FMath::SmoothStep(0.f, 0.3f, Fbm(4, MX / 1400.f, MY / 1400.f, 2));
			// (2026-11-16, terrain polish: the mountains were smooth domes. Gullies and spurs down their flanks, 40-80 m
			// apart and up to ~9 m deep, more the higher the mountain; the rain then works them deeper.)
			float Gully = 0.f;
			if (!bOldSea)
			{
				const float Gn = FMath::PerlinNoise2D(FVector2D(MX / 70.f, MY / 70.f) + SeaOff[2] * 1.3f) + 0.5f * FMath::PerlinNoise2D(FVector2D(MX / 33.f, MY / 33.f) + SeaOff[1] * 0.7f);
				Gully = (FMath::Square(1.f - FMath::Min(FMath::Abs(Gn), 1.f)) - 0.45f) * -9.f * FMath::Clamp(Ridge * Mountains * 2.f, 0.f, 1.f);
			}
			const float Up = 20.f + 34.f * Rises + 18.f * Hills + 200.f * Ridge * Mountains + Gully;
			float Floor = -28.f + 4.f * Hills;
			if (!bOldSea)
			{
				// banks and shoals, deeper toward the edge
				const float Bank = FMath::PerlinNoise2D(FVector2D(MX / 650.f, MY / 650.f) + SeaOff[0]) + 0.5f * FMath::PerlinNoise2D(FVector2D(MX / 260.f, MY / 260.f) + SeaOff[1]);
				Floor = -30.f + 4.f * Hills + 28.f * FMath::SmoothStep(0.0f, 0.6f, Bank) - 14.f * FMath::SmoothStep(1.05f, 1.4f, FMath::Sqrt(U * U + V * V));
			}
			// (2026-11-17, a closer look: the open sea's floor beyond the map lies flat 32 m down; the map's own floor met it at
			// whatever depth its banks left, and the water's colour changed along the map's edge, a square round the island
			// from above. The last 12% of the way out it eases to 32 m; -bfoldseaedge: as before.)
			if (!bOldSea && !FParse::Param(FCommandLine::Get(), TEXT("bfoldseaedge")))
			{
				Floor = FMath::Lerp(Floor, -32.f, FMath::SmoothStep(0.88f, 0.995f, FMath::Max(FMath::Abs(U), FMath::Abs(V))));
			}
			float Hh = FMath::Lerp(Floor, FMath::Max(Up, 3.f), Land);
			for (const FIslet& It : Islets)
			{
				// an islet: a knoll, ragged at its shore, rising to its peak (a stack: sheer, flat-topped)
				const float Dm = float(FVector2D::Distance(FVector2D(MX, MY), It.At));
				if (Dm > It.R * 2.2f)
				{
					continue;
				}
				const float Ragged = 1.f + 0.35f * FMath::PerlinNoise2D(FVector2D(MX / 60.f, MY / 60.f) + SeaOff[2]);
				const float T = Dm / (It.R * Ragged);
				const bool bStack = It.R < 30.f;
				const float Top = bStack ? It.Peak * (1.f - FMath::SmoothStep(0.75f, 1.f, T))
					: 2.f + It.Peak * FMath::Square(FMath::Max(1.f - T, 0.f)) + 3.f * Hills * FMath::Max(1.f - T, 0.f);
				const float Shelf = FMath::Lerp(-3.f, Floor, FMath::SmoothStep(1.f, 2.2f, T));          // (shallows round it)
				Hh = FMath::Max(Hh, T < 1.f ? FMath::Max(Top, 1.5f * (1.f - T)) : Shelf);
			}
			H[Idx(X, Y)] = Hh;
		}
	});
	Progress = 0.1f;

	// Rain: droplets run downhill picking up soil where they speed up and dropping it where they slow (Sebastian
	// Lague's droplet erosion). Valleys deepen, slopes gather fans of sediment: the land looks worn, not random.
	StageNo = 1;
	{
		const int32 Drops = 260000, Life = 32, Radius = 3;
		const float Inertia = 0.05f, CapacityK = 4.f, MinCapacity = 0.01f, ErodeK = 0.3f, DepositK = 0.3f, Evaporate = 0.01f, Gravity = 4.f;
		TArray<FIntPoint> Brush;
		TArray<float> BrushW;
		float WSum = 0.f;
		for (int32 dy = -Radius; dy <= Radius; ++dy)
		{
			for (int32 dx = -Radius; dx <= Radius; ++dx)
			{
				const float Dist = FMath::Sqrt(float(dx * dx + dy * dy));
				if (Dist <= Radius)
				{
					Brush.Add(FIntPoint(dx, dy));
					BrushW.Add(1.f - Dist / Radius);
					WSum += 1.f - Dist / Radius;
				}
			}
		}
		for (float& Wt : BrushW)
		{
			Wt /= WSum;
		}
		auto HeightGrad = [&](float PX, float PY, float& GX, float& GY)
		{
			const int32 X = int32(PX), Y = int32(PY);
			const float FX = PX - X, FY = PY - Y;
			const float A = H[Idx(X, Y)], B = H[Idx(X + 1, Y)], C = H[Idx(X, Y + 1)], D = H[Idx(X + 1, Y + 1)];
			GX = (B - A) * (1.f - FY) + (D - C) * FY;
			GY = (C - A) * (1.f - FX) + (D - B) * FX;
			return A * (1.f - FX) * (1.f - FY) + B * FX * (1.f - FY) + C * (1.f - FX) * FY + D * FX * FY;
		};
		for (int32 i = 0; i < Drops; ++i)
		{
			float PX = R.FRandRange(Radius + 1.f, N - Radius - 2.f), PY = R.FRandRange(Radius + 1.f, N - Radius - 2.f);
			float DX = 0.f, DY = 0.f, Speed = 1.f, Water = 1.f, Sediment = 0.f;
			for (int32 Step = 0; Step < Life; ++Step)
			{
				const int32 NX = int32(PX), NY = int32(PY);
				const float CX = PX - NX, CY = PY - NY;
				float GX, GY;
				const float H0 = HeightGrad(PX, PY, GX, GY);
				if (H0 < 0.f)
				{
					break;                      // into the sea: the sea floor stays as it is
				}
				DX = DX * Inertia - GX * (1.f - Inertia);
				DY = DY * Inertia - GY * (1.f - Inertia);
				const float Len = FMath::Sqrt(DX * DX + DY * DY);
				if (Len < 1e-6f)
				{
					break;
				}
				DX /= Len;
				DY /= Len;
				PX += DX;
				PY += DY;
				if (PX < Radius + 1 || PY < Radius + 1 || PX >= N - Radius - 2 || PY >= N - Radius - 2)
				{
					break;
				}
				float GX2, GY2;
				const float DH = HeightGrad(PX, PY, GX2, GY2) - H0;
				const float Capacity = FMath::Max(-DH * Speed * Water * CapacityK, MinCapacity);
				if (Sediment > Capacity || DH > 0.f)
				{
					const float Drop = DH > 0.f ? FMath::Min(DH, Sediment) : (Sediment - Capacity) * DepositK;
					Sediment -= Drop;
					H[Idx(NX, NY)] += Drop * (1.f - CX) * (1.f - CY);
					H[Idx(NX + 1, NY)] += Drop * CX * (1.f - CY);
					H[Idx(NX, NY + 1)] += Drop * (1.f - CX) * CY;
					H[Idx(NX + 1, NY + 1)] += Drop * CX * CY;
				}
				else
				{
					const float Erode = FMath::Min((Capacity - Sediment) * ErodeK, -DH);
					for (int32 k = 0; k < Brush.Num(); ++k)
					{
						float& Cell = H[Idx(NX + Brush[k].X, NY + Brush[k].Y)];
						const float Take = FMath::Min(FMath::Max(Cell, 0.f), Erode * BrushW[k]);
						Cell -= Take;
						Sediment += Take;
					}
				}
				Speed = FMath::Sqrt(FMath::Max(Speed * Speed + DH * Gravity, 0.f));
				Water *= 1.f - Evaporate;
			}
			if ((i & 16383) == 0)
			{
				Progress = 0.1f + 0.4f * float(i) / Drops;
			}
		}
	}

	// Ravines (Raven, 2026-11-15: "a ravine with a slope to the bottom and large chunks carved out of it, and maybe
	// waterfalls"; not every island has everything). Each island rolls none (4 in 10), one (4.5 in 10) or two. A ravine
	// follows the land down from high ground for 150-350 m: its floor climbs gently (3 deg) from where it opens at the
	// low end, so the ground rises over it faster and it deepens toward its head, up to 40 m; a floor 6-14 m across,
	// walls at about 65 deg, ragged; a head wall at the top, where whatever water runs down from above falls in. Cut
	// before the water finds its way, so streams run down it and fall over its head. (Rock shapes dress it later.)
	Ravines.Reset();
	Falls.Reset();
	FallRuns.Reset();
	FallReach.Reset();
	Gorge.Init(0, N * N);
	{
		FRandomStream RR(Seed + 4242);
		const float Pick = RR.FRand();
		const int32 WantRav = Pick < 0.6f ? 1 : 2;          // (2026-11-17: every island has one; 40% had none and Raven had never seen one)
		const float Steep = 1.75f;          // metres up the wall per metre across (~60 deg; 65 stood as flat plates)
		// (Its head where a stream already runs, so the stream goes over it: a waterfall. Where the rain gathers, the
		// land's own way down: each point's catchment, cells of rain, as the water will find it.)
		TArray<int32> Heads;
		TArray<float> Pre;
		TArray<int32> Par;
		if (WantRav > 0)
		{
			// (filled from the sea in, lowest first, as the water will be: each point drains to the one it was reached
			// from, so the small hollows rain left don't stop the count)
			TArray<float> F2 = H;
			TArray<int32> Popped;
			Par.Init(INDEX_NONE, N * N);
			TArray<bool> Seen;
			Seen.Init(false, N * N);
			struct FQ2 { float Z; int32 I; };
			TArray<FQ2> Heap;
			auto Less2 = [](const FQ2& A, const FQ2& B) { return A.Z < B.Z; };
			for (int32 i = 0; i < N; ++i)
			{
				for (const int32 C : { Idx(i, 0), Idx(i, N - 1), Idx(0, i), Idx(N - 1, i) })
				{
					if (!Seen[C])
					{
						Seen[C] = true;
						Heap.HeapPush({ F2[C], C }, Less2);
					}
				}
			}
			Popped.Reserve(N * N);
			while (Heap.Num() > 0)
			{
				FQ2 Q;
				Heap.HeapPop(Q, Less2, EAllowShrinking::No);
				Popped.Add(Q.I);
				const int32 X = Q.I % N, Y = Q.I / N;
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						const int32 NX = X + dx, NY = Y + dy;
						if ((dx || dy) && NX >= 0 && NY >= 0 && NX < N && NY < N && !Seen[Idx(NX, NY)])
						{
							const int32 NC = Idx(NX, NY);
							Seen[NC] = true;
							F2[NC] = FMath::Max(H[NC], F2[Q.I]);
							Par[NC] = Q.I;
							Heap.HeapPush({ F2[NC], NC }, Less2);
						}
					}
				}
			}
			Pre.Init(1.f, N * N);
			for (int32 k = Popped.Num() - 1; k >= 0; --k)
			{
				const int32 C = Popped[k];
				if (Par[C] != INDEX_NONE)
				{
					Pre[Par[C]] += Pre[C];
				}
			}
			// (2026-11-17: looser where an island has few: smaller streams and lower ground)
			for (int32 Pass = 0; Pass < 2 && Heads.Num() < 40; ++Pass)
			{
				Heads.Reset();
				for (int32 C = 0; C < N * N; ++C)
				{
					const int32 X = C % N, Y = C / N;
					if (Pre[C] >= (Pass ? 2500.f : 6500.f) && Pre[C] <= 60000.f && H[C] >= (Pass ? 15.f : 25.f) && H[C] <= 170.f && X > N / 8 && Y > N / 8 && X < N * 7 / 8 && Y < N * 7 / 8)
					{
						Heads.Add(C);
					}
				}
			}
		}
		int32 RavShort = 0, RavShallow = 0;
		for (int32 Try = 0; Try < 600 && Ravines.Num() < WantRav && Heads.Num() > 0; ++Try)
		{
			const int32 HC = Heads[RR.RandRange(0, Heads.Num() - 1)];
			const int32 SX = HC % N, SY = HC / N;
			// down the land, the steepest way (a little wander), 150-350 m
			const float WantM = RR.FRandRange(150.f, 350.f);
			TArray<FIntPoint> Run = { FIntPoint(SX, SY) };
			float Len = 0.f;
			while (Len < WantM)
			{
				const FIntPoint P = Run.Last();
				// (2026-11-17: the way the water drains, through the hollows: the steepest way down stopped in the first
				// hollow, and 562 of 600 tries on island 22436 came out too short)
				const int32 Dn = Par[Idx(P.X, P.Y)];
				if (Dn != INDEX_NONE)
				{
					const FIntPoint B2(Dn % N, Dn / N);
					if (H[Dn] < 3.f || B2.X <= 2 || B2.Y <= 2 || B2.X >= N - 3 || B2.Y >= N - 3)
					{
						break;
					}
					Len += CellM * ((B2.X != P.X && B2.Y != P.Y) ? 1.4142f : 1.f);
					Run.Add(B2);
					continue;
				}
				FIntPoint Best = P;
				float BestZ = H[Idx(P.X, P.Y)];
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						const int32 NX = P.X + dx, NY = P.Y + dy;
						if ((dx || dy) && NX > 2 && NY > 2 && NX < N - 3 && NY < N - 3)
						{
							const float Z = H[Idx(NX, NY)] + RR.FRandRange(0.f, 0.1f);
							if (Z < BestZ)
							{
								BestZ = Z;
								Best = FIntPoint(NX, NY);
							}
						}
					}
				}
				if (Best == P || H[Idx(Best.X, Best.Y)] < 3.f)
				{
					break;
				}
				Len += CellM * ((Best.X != P.X && Best.Y != P.Y) ? 1.4142f : 1.f);
				Run.Add(Best);
			}
			if (Len < 140.f)
			{
				++RavShort;
				continue;
			}
			// a smoothed line through it (every third point), from the head (0) to the low end
			FBFRavine Rv;
			TArray<FVector2D> G;
			for (int32 i = 0; i < Run.Num(); i += 3)
			{
				G.Add(FVector2D(Run[i]));
			}
			if (G.Last() != FVector2D(Run.Last()))
			{
				G.Add(FVector2D(Run.Last()));
			}
			if (Ravines.ContainsByPredicate([&](const FBFRavine& O)
			{
				return O.Path.ContainsByPredicate([&](const FVector& Q) { return G.ContainsByPredicate([&](const FVector2D& P) { return FVector2D::Distance(P, FVector2D(Q)) < 40.f; }); });
			}))
			{
				continue;
			}
			// the floor, up from the low end; no deeper than 40 m
			TArray<float> FloorM, Half;
			FloorM.SetNum(G.Num());
			Half.SetNum(G.Num());
			FloorM.Last() = H[Idx(int32(G.Last().X), int32(G.Last().Y))] - 0.5f;
			for (int32 i = G.Num() - 2; i >= 0; --i)
			{
				const float Step = FVector2D::Distance(G[i], G[i + 1]) * CellM;
				const float Ground = H[Idx(int32(G[i].X), int32(G[i].Y))];
				FloorM[i] = FMath::Max(FMath::Min(FloorM[i + 1] + 0.015f * Step, Ground - 0.5f), Ground - 40.f);          // (its floor climbing ~1 deg: a stream's valley falls faster, so it deepens upstream)
			}
			float Deepest = 0.f;
			for (int32 i = 0; i < G.Num(); ++i)
			{
				Deepest = FMath::Max(Deepest, H[Idx(int32(G[i].X), int32(G[i].Y))] - FloorM[i]);
				Half[i] = RR.FRandRange(3.f, 7.f);
			}
			if (Deepest < 10.f)
			{
				++RavShallow;
				continue;
			}
			// cut it
			FBox2D Box(ForceInit);
			for (const FVector2D& P : G)
			{
				Box += P;
			}
			const int32 Pad = int32(45.f / (Steep * CellM)) + 6;
			for (int32 Y = FMath::Max(int32(Box.Min.Y) - Pad, 2); Y <= FMath::Min(int32(Box.Max.Y) + Pad, N - 3); ++Y)
			{
				for (int32 X = FMath::Max(int32(Box.Min.X) - Pad, 2); X <= FMath::Min(int32(Box.Max.X) + Pad, N - 3); ++X)
				{
					const FVector2D Q(X, Y);
					float Best = 1e9f, Fl = 0.f, Hw = 0.f;
					for (int32 i = 0; i + 1 < G.Num(); ++i)
					{
						const FVector2D AB = G[i + 1] - G[i];
						const float T = FMath::Clamp(float(FVector2D::DotProduct(Q - G[i], AB) / FMath::Max(AB.SizeSquared(), 0.001)), 0.f, 1.f);
						const float Dd = float(FVector2D::Distance(Q, G[i] + AB * T)) * CellM;
						if (Dd < Best)
						{
							Best = Dd;
							Fl = FMath::Lerp(FloorM[i], FloorM[i + 1], T);
							Hw = FMath::Lerp(Half[i], Half[i + 1], T);
						}
					}
					const float Jag = 2.5f * FMath::PerlinNoise2D(Q * 0.3f + FVector2D(Seed % 53, 7.f)) + 0.6f * FMath::PerlinNoise2D(Q * 0.7f + FVector2D(3.f, Seed % 31));          // (finer than this stood up as lone spires on the 4 m grid)
					// (2026-11-15: the steepness varying along and round it, 45-70 deg, so a wall isn't one even slope)
					const float StHere = Steep * (1.f + 0.32f * FMath::PerlinNoise2D(Q * 0.06f + FVector2D(Seed % 17, 21.f)));
					const float Wall = Fl + FMath::Max(0.f, Best - Hw + Jag) * StHere;
					const int32 C = Idx(X, Y);
					if (Wall < H[C] - 0.05f)
					{
						H[C] = Wall;
						Gorge[C] = 1;
					}
				}
			}
			for (int32 i = 0; i < G.Num(); ++i)
			{
				Rv.Path.Add(Origin + FVector(G[i].X * CellCm, G[i].Y * CellCm, FloorM[i] * 100.f));
				Rv.HalfCm.Add(Half[i] * 100.f);
			}
			Rv.DeepM = Deepest;
			Ravines.Add(Rv);
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: ravines: wanted %d, made %d; %d stream heads to start from; %d too short, %d too shallow"), WantRav, Ravines.Num(), Heads.Num(), RavShort, RavShallow);
	}

	// Water finding its way down: every hollow filled to where it spills (priority flood from the sea), so lakes stand
	// in hollows and every cell drains to the sea; then each cell's catchment (its share of the rain), downhill.
	StageNo = 2;
	Progress = 0.5f;
	TArray<float> F = H;
	{
		TArray<bool> Done;
		Done.Init(false, N * N);
		struct FQ { float Z; int32 I; };
		TArray<FQ> Heap;
		auto Less = [](const FQ& A, const FQ& B) { return A.Z < B.Z; };
		for (int32 i = 0; i < N; ++i)
		{
			for (const int32 C : { Idx(i, 0), Idx(i, N - 1), Idx(0, i), Idx(N - 1, i) })
			{
				if (!Done[C])
				{
					Done[C] = true;
					F[C] = FMath::Max(H[C], 0.f);
					Heap.HeapPush({ F[C], C }, Less);
				}
			}
		}
		while (Heap.Num() > 0)
		{
			FQ Top;
			Heap.HeapPop(Top, Less, EAllowShrinking::No);
			const int32 X = Top.I % N, Y = Top.I / N;
			for (int32 dy = -1; dy <= 1; ++dy)
			{
				for (int32 dx = -1; dx <= 1; ++dx)
				{
					const int32 NX = X + dx, NY = Y + dy;
					if ((dx | dy) == 0 || NX < 0 || NY < 0 || NX >= N || NY >= N || Done[Idx(NX, NY)])
					{
						continue;
					}
					const int32 C = Idx(NX, NY);
					Done[C] = true;
					F[C] = FMath::Max(H[C], Top.Z + 1e-4f);
					Heap.HeapPush({ F[C], C }, Less);
				}
			}
		}
	}
	Progress = 0.6f;
	TArray<int32> Order;
	Order.SetNumUninitialized(N * N);
	for (int32 i = 0; i < N * N; ++i)
	{
		Order[i] = i;
	}
	Order.Sort([&](int32 A, int32 B) { return F[A] > F[B]; });
	TArray<float> Acc;
	Acc.Init(1.f, N * N);
	TArray<int32> Down;                 // where each point's water goes next (the current's way)
	Down.Init(INDEX_NONE, N * N);
	for (const int32 C : Order)
	{
		const int32 X = C % N, Y = C / N;
		int32 Best = INDEX_NONE;
		float BestDrop = 0.f;
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				const int32 NX = X + dx, NY = Y + dy;
				if ((dx | dy) == 0 || NX < 0 || NY < 0 || NX >= N || NY >= N)
				{
					continue;
				}
				const float Drop = (F[C] - F[Idx(NX, NY)]) / ((dx != 0 && dy != 0) ? 1.4142f : 1.f);
				if (Drop > BestDrop)
				{
					BestDrop = Drop;
					Best = Idx(NX, NY);
				}
			}
		}
		if (Best != INDEX_NONE)
		{
			Acc[Best] += Acc[C];
		}
		Down[C] = Best;
	}
	Progress = 0.7f;

	// The water: the sea at its level; lakes at their spill level where a hollow is filled; rivers where enough rain
	// gathers, their channels carved deeper and wider the more it is (a stream to a river some metres deep).
	StageNo = 3;
	W.Init(-1000.f, N * N);
	TArray<uint8> Kind;                 // 0 land, 1 sea, 2 lake, 3 river
	Kind.Init(0, N * N);
	const float Threshold = 6000.f;     // cells of rain (16 m2 each) that make a stream
	for (int32 C = 0; C < N * N; ++C)
	{
		if (F[C] < 0.05f && H[C] < 0.f)
		{
			W[C] = 0.f;
			Kind[C] = 1;
		}
		else if (F[C] - H[C] > 0.4f)
		{
			W[C] = F[C];
			Kind[C] = 2;
		}
	}
	// Each channel point's water level: below the filled surface, 20 cm below the lowest dry ground just outside its
	// channel (a river sits in its banks, never above the land beside it), and no higher than the point upstream of it
	// (in order from the highest down, each passes its level on to the point it drains to). Its bed is cut under that.
	auto IsChannel = [&](int32 C) { return Acc[C] >= Threshold && Kind[C] == 0; };
	const float FallM = FParse::Param(FCommandLine::Get(), TEXT("bfnofalls")) ? 1e9f : 3.f;          // a drop this big at once is a waterfall
	auto ReachOf = [&](int32 C) { return FMath::Clamp(int32(0.5f + 0.6f * FMath::Log2(Acc[C] / Threshold)), 0, 1); };
	TArray<float> Lvl;
	Lvl.Init(1e9f, N * N);
	for (const int32 C : Order)
	{
		if (!IsChannel(C))
		{
			continue;
		}
		const int32 X = C % N, Y = C / N, Rc = ReachOf(C);
		float Bank = 1e9f;
		for (int32 dy = -Rc - 3; dy <= Rc + 3; ++dy)
		{
			for (int32 dx = -Rc - 3; dx <= Rc + 3; ++dx)
			{
				const float Dist = FMath::Sqrt(float(dx * dx + dy * dy));
				const int32 NX = X + dx, NY = Y + dy;
				if (Dist < Rc + 1.5f || Dist >= Rc + 3.f || NX < 0 || NY < 0 || NX >= N || NY >= N)
				{
					continue;
				}
				const int32 NC = Idx(NX, NY);
				// (a ravine beside a river's lip isn't its bank: the river goes over into it, not down to its floor)
				if (Kind[NC] == 0 && Acc[NC] < Threshold && !(Gorge[NC] && H[NC] < H[C] - 1.f))
				{
					Bank = FMath::Min(Bank, H[NC]);
				}
			}
		}
		Lvl[C] = FMath::Min(Lvl[C], FMath::Min(F[C] - 0.15f, Bank - 0.2f));
		if (Down[C] != INDEX_NONE)
		{
			Lvl[Down[C]] = FMath::Min(Lvl[Down[C]], Lvl[C]);
		}
	}
	// Runs, not stairs (Raven, 2026-11-14: "polish it up"): where the land drops in a step the level fell metres in one
	// point and the water stood in flat stairs. No point's level stands more than MaxRun (m per m) above a point
	// downstream of it, so above a step the river cuts down into a chute running into it. (Only ever lower: still in its
	// banks, still falling downstream.) -bfnoruns: as it was, for the checks' before.
	RunsLowered = 0;
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfnoruns")))
	{
		const float MaxRun = 0.18f;
		TArray<float> Ramped = Lvl;
		for (int32 C = 0; C < N * N; ++C)
		{
			if (!IsChannel(C) || Lvl[C] > 1e8f)
			{
				continue;
			}
			float Along = 0.f;
			int32 At = C;
			for (int32 k = 0; k < 24; ++k)
			{
				const int32 Nx = Down[At];
				if (Nx == INDEX_NONE)
				{
					break;
				}
				const bool bDiag = (Nx % N != At % N) && (Nx / N != At / N);
				Along += CellM * (bDiag ? 1.4142f : 1.f);
				// (a waterfall: a drop of 3 m or more at once is left as it is, the river going over the edge)
				if (IsChannel(Nx) && Lvl[Nx] < 1e8f && Lvl[At] < 1e8f && Lvl[At] - Lvl[Nx] >= FallM)
				{
					break;
				}
				At = Nx;
				if (!IsChannel(At) || Lvl[At] > 1e8f)
				{
					break;          // into the sea or a lake: its level is theirs from here
				}
				Ramped[C] = FMath::Min(Ramped[C], Lvl[At] + MaxRun * Along);
			}
		}
		for (int32 C = 0; C < N * N; ++C)
		{
			RunsLowered += (Ramped[C] < Lvl[C] - 0.05f) ? 1 : 0;
		}
		Lvl = MoveTemp(Ramped);
	}
	TArray<float> Carved = H;
	for (int32 C = 0; C < N * N; ++C)
	{
		if (!IsChannel(C))
		{
			continue;
		}
		const float A = Acc[C] / Threshold;
		const float Depth = FMath::Clamp(0.8f + 0.7f * FMath::Log2(A), 0.8f, 4.f);
		const int32 Reach = ReachOf(C);          // up to ~20 m wide: an island's rivers
		const float Level = Lvl[C];
		const int32 X = C % N, Y = C / N;
		for (int32 dy = -Reach - 1; dy <= Reach + 1; ++dy)
		{
			for (int32 dx = -Reach - 1; dx <= Reach + 1; ++dx)
			{
				const int32 NX = X + dx, NY = Y + dy;
				if (NX < 0 || NY < 0 || NX >= N || NY >= N)
				{
					continue;
				}
				const float T = FMath::Sqrt(float(dx * dx + dy * dy)) / (Reach + 1.5f);
				if (T >= 1.f)
				{
					continue;
				}
				const int32 NC = Idx(NX, NY);
				const float Bed = Level - Depth * (1.f - T * T) + 0.1f;
				Carved[NC] = FMath::Min(Carved[NC], Bed);
				if (Carved[NC] < Level && Kind[NC] != 1)
				{
					W[NC] = FMath::Max(W[NC], Level);
					if (Kind[NC] == 0)
					{
						Kind[NC] = 3;
					}
				}
			}
		}
	}
	H = MoveTemp(Carved);
	// (after the cut a channel point is marked a river (Kind 3), no longer land: so a river point, either way)
	auto IsRiverPt = [&](int32 C) { return Acc[C] >= Threshold && (Kind[C] == 0 || Kind[C] == 3); };
	// Waterfalls: each run of points along a river where its level drops 3 m or more at a step, top to bottom; its width
	// the river's. Over 6 m (or the biggest) are named places.
	{
		TArray<uint8> InFall;
		InFall.Init(0, N * N);
		for (const int32 C0 : Order)
		{
			if (!IsRiverPt(C0) || InFall[C0] || Lvl[C0] > 1e8f)
			{
				continue;
			}
			int32 C = C0, Bottom = INDEX_NONE;
			TArray<int32> Run = { C0 };
			while (Down[C] != INDEX_NONE && IsRiverPt(Down[C]) && Lvl[Down[C]] < 1e8f && Lvl[C] - Lvl[Down[C]] >= FallM && !InFall[C])
			{
				InFall[C] = 1;
				Bottom = Down[C];
				C = Down[C];
				Run.Add(C);
			}
			if (Bottom == INDEX_NONE || Lvl[C0] - Lvl[Bottom] < FallM)
			{
				continue;
			}
			InFall[Bottom] = 1;
			FBFFall Fa;
						Fa.Top = Origin + FVector((C0 % N) * CellCm, (C0 / N) * CellCm, Lvl[C0] * 100.f);
			Fa.Bottom = Origin + FVector((Bottom % N) * CellCm, (Bottom / N) * CellCm, Lvl[Bottom] * 100.f);
			Fa.Dir = (Fa.Bottom - Fa.Top).GetSafeNormal2D();
			Fa.WidthCm = (2 * ReachOf(C0) + 1) * CellCm * 0.85f;
			Fa.DropM = Lvl[C0] - Lvl[Bottom];
			// (not where a fall's top is in another's: one wide river's points)
			if (!Falls.ContainsByPredicate([&](const FBFFall& O) { return FVector::Dist2D(O.Top, Fa.Top) < Fa.WidthCm + 400.f; }))
			{
				Falls.Add(Fa);
				FallRuns.Add(Run);
				FallReach.Add(ReachOf(C0));
			}
		}
		// Falls one straight after another (2026-11-16, island 906: a small fall from a lake into its own carved pool, the
		// big one leaving that pool: two pools stacked made a box at the top) are one fall: from the first's top to the
		// last's foot. -bfoldlip: kept apart.
		if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldlip")))
		{
			bool bMerged = true;
			while (bMerged)
			{
				bMerged = false;
				for (int32 a = 0; a < Falls.Num() && !bMerged; ++a)
				{
					for (int32 b = 0; b < Falls.Num() && !bMerged; ++b)
					{
						if (a != b && FVector::Dist2D(Falls[a].Bottom, Falls[b].Top) < 3.5f * CellCm && Falls[b].Top.Z <= Falls[a].Bottom.Z + 150.f)
						{
							FBFFall& A = Falls[a];
							A.Bottom = Falls[b].Bottom;
							A.Dir = (A.Bottom - A.Top).GetSafeNormal2D();
							A.DropM = float(A.Top.Z - A.Bottom.Z) / 100.f;
							FallRuns[a].Append(FallRuns[b]);
							FallReach[a] = FMath::Max(FallReach[a], FallReach[b]);
							Falls.RemoveAt(b);
							FallRuns.RemoveAt(b);
							FallReach.RemoveAt(b);
							bMerged = true;
						}
					}
				}
			}
		}
		// Each fall's shape (Raven, 2026-11-15: "the waterfalls and rivers and the terrain around them to look more
		// natural"). A real fall drops from a hard lip over a face undercut beneath it, into a plunge pool the water
		// scours out, in an amphitheatre of steep rock (the falls retreating upstream leave a gorge below). On the 4 m grid:
		// below the lip, all the drop at once (the points it fell down in steps carved to the pool: the face one point
		// across, near sheer); a round pool at its foot (radius a fifth of the drop, 6-16 m; 1.5-6 m deep), narrowing
		// down to the river's channel; its sides rising steeply (~68 deg) from the water. Falls under 5 m stay as steps.
		if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldfalls")))
		{
			for (int32 f = 0; f < Falls.Num(); ++f)
			{
				FBFFall& Fa = Falls[f];
				static const bool bStraightRim = FParse::Param(FCommandLine::Get(), TEXT("bfstraightrim"));
				const TArray<int32>& Run = FallRuns[f];
				if (Fa.DropM < 5.f || Run.Num() < 2)
				{
					continue;
				}
				const int32 TopC = Run[0], P1 = Run[1];
				const float PoolLvl = Lvl[Run.Last()];
				const float PoolR = FMath::Clamp(0.2f * Fa.DropM / CellM, 1.5f, 4.f);           // cells
				const float Deep = FMath::Clamp(0.12f * Fa.DropM, 1.5f, 6.f);
				const float Reach = FallReach[f] + 0.8f;
				const FVector2D T(TopC % N, TopC / N), P(P1 % N, P1 / N);
				const FVector2D Dir = (P - T).GetSafeNormal();
				TArray<FVector2D> Chain;
				for (int32 k = 1; k < Run.Num(); ++k)
				{
					Chain.Add(FVector2D(Run[k] % N, Run[k] / N));
				}
				const int32 Pad = int32(PoolR) + 6;
				FBox2D Box(ForceInit);
				for (const FVector2D& Q : Chain)
				{
					Box += Q;
				}
				for (int32 Y = FMath::Max(int32(Box.Min.Y) - Pad, 1); Y <= FMath::Min(int32(Box.Max.Y) + Pad, N - 2); ++Y)
				{
					for (int32 X = FMath::Max(int32(Box.Min.X) - Pad, 1); X <= FMath::Min(int32(Box.Max.X) + Pad, N - 2); ++X)
					{
						const FVector2D Q(X, Y);
						if (FVector2D::DotProduct(Q - T, Dir) < 0.6f)
						{
							continue;           // the lip and above: as it is
						}
						float D = 1e9f;
						for (int32 k = 0; k < Chain.Num(); ++k)
						{
							const FVector2D A = Chain[k], B = Chain[FMath::Min(k + 1, Chain.Num() - 1)];
							const FVector2D AB = B - A;
							const float t = AB.IsNearlyZero() ? 0.f : FMath::Clamp(float(FVector2D::DotProduct(Q - A, AB) / AB.SizeSquared()), 0.f, 1.f);
							D = FMath::Min(D, float(FVector2D::Distance(Q, A + AB * t)));
						}
						const float Dp = float(FVector2D::Distance(Q, P));
						const float Rw = FMath::Lerp(PoolR, Reach, FMath::Clamp((Dp - PoolR) / 3.f, 0.f, 1.f));
						const int32 Cc = Idx(X, Y);
						if (D <= Rw)
						{
							const float Bed = PoolLvl - 0.6f - Deep * FMath::SmoothStep(PoolR, 0.f, Dp);
							H[Cc] = FMath::Min(H[Cc], Bed);
							W[Cc] = FMath::Max(W[Cc] > H[Cc] ? W[Cc] : -1000.f, PoolLvl);
							if (Kind[Cc] == 0)
							{
								Kind[Cc] = 3;
							}
						}
						else if ((Kind[Cc] == 0 || Kind[Cc] == 3) && (bStraightRim || float(FVector2D::DotProduct(Q - T, Dir)) >= 0.6f + 0.3f * FMath::Square(FMath::Max(FMath::Abs(float(FVector2D::CrossProduct(Dir, Q - T))) - 0.5f * Fa.WidthCm / CellCm - 0.5f, 0.f)) + 0.6f * FMath::PerlinNoise2D(Q * 0.3f + FVector2D(5.f, 1.f))))
						// (2026-11-17: the walls' upper edge a horseshoe round the pool, its arms reaching downstream (0.3 points on per
						// point aside, squared, from half a point past the channel). Cut from a straight line
						// across the river, the rim ran ruler-straight 40 m either side, island 906's 35 m fall and island 13's 41 m one
						// from above. -bfstraightrim: as before.)
						{
							// (2026-11-16, Raven, island 906: spikes 15-30 m round a big fall's pool. They were river points of the
							// old cascade, beside the run but outside the pool: only land was brought down to the walls, so they
							// stood at their old heights. Now they come down too, and dry: the water goes down the face.)
							if (Kind[Cc] == 3)
							{
								Kind[Cc] = 0;
							}
							W[Cc] = -1000.f;          // (dry: water left up a lowered wall stood as sheets up the gorge's sides)
							const float Wall = PoolLvl + 0.5f + FMath::Max(D - Rw + 0.6f * FMath::PerlinNoise2D(Q * 0.33f + FVector2D(9.f, 4.f)), 0.f) * CellM * 1.8f + 2.5f * FMath::PerlinNoise2D(Q * 0.45f);          // (~60 deg, ragged: steeper stood as flat plates on the 4 m grid)
							if (Wall < H[Cc])
							{
								H[Cc] = Wall;
							}
						}
						Gorge[Cc] = 1;
					}
				}
				// The lip (2026-11-16, Raven: the start of a fall "looks horrible": a raised bar of bank stood across the river
				// at the edge, so the water seemed dammed and the curtain began below it). The river's channel carried right to
				// the edge at its own level, and the banks beside it rising from it gently (~24 deg, ragged), so the water
				// runs over the lip in a notch. Over the last 3 points before the edge, fading in from upstream.
				// (tried 2026-11-16: at a lake's outlet the lip stepped down from the lake in a box; only with -bflipnotch)
				if (FParse::Param(FCommandLine::Get(), TEXT("bflipnotch")))
				{
					// (the water's own level there: at a lake's outlet the lake stands over the run's level, and cut to the run's
					// level the lip stepped down from the lake in a box)
					const float TopLvl = FMath::Max(Lvl[TopC], W[TopC] > H[TopC] ? W[TopC] : -1000.f);
					Fa.Top.Z = TopLvl * 100.f;
					const float HalfW = FMath::Max(0.5f * Fa.WidthCm / CellCm, 0.6f);
					for (int32 Y = FMath::Max(int32(T.Y) - 9, 1); Y <= FMath::Min(int32(T.Y) + 9, N - 2); ++Y)
					{
						for (int32 X = FMath::Max(int32(T.X) - 9, 1); X <= FMath::Min(int32(T.X) + 9, N - 2); ++X)
						{
							const FVector2D Q(X, Y);
							const float Along = float(FVector2D::DotProduct(Q - T, Dir)), Lat = FMath::Abs(float(FVector2D::CrossProduct(Dir, Q - T)));
							const int32 Cc = Idx(X, Y);
							if (Along < -3.f || Along >= 0.6f || Kind[Cc] == 1 || Kind[Cc] == 2)
							{
								continue;
							}
							const float Fade = FMath::SmoothStep(-3.f, -1.f, Along);
							const float Edge = Lat - HalfW;
							if (Edge <= 0.f)
							{
								H[Cc] = FMath::Min(H[Cc], TopLvl - 0.5f);
								W[Cc] = FMath::Max(W[Cc], TopLvl);
								if (Kind[Cc] == 0)
								{
									Kind[Cc] = 3;
								}
							}
							else
							{
								const float Bank = TopLvl + 0.15f + Edge * CellM * 0.45f + 0.5f * FMath::PerlinNoise2D(Q * 0.4f + FVector2D(3.f, 7.f));
								if (Bank < H[Cc])
								{
									H[Cc] = FMath::Lerp(H[Cc], Bank, Fade);
								}
							}
						}
					}
				}
				// The lip fed at the water's level above it (2026-11-16, Raven: "work more on waterfalls"; island 906: a lake
				// stood 4.5 m over the fall's first point, held back by a bar of bank, and a box of water sat at the edge below
				// it). The highest water within 3 points upstream of the lip: if it stands over the lip's level, the water
				// runs out to the edge at that level (the channel's ground under it, the banks beside just holding it), so
				// the fall leaves the lake's own surface. (Off: the passes after this set the river's levels again; the lip is now
				// done last of all in MakeLand. Here, -bfinlooplip.)
				if (FParse::Param(FCommandLine::Get(), TEXT("bfinlooplip")))
				{
					const float HalfW = FMath::Max(0.5f * Fa.WidthCm / CellCm, 0.6f);
					float UpLvl = Lvl[TopC];
					for (int32 Y = FMath::Max(int32(T.Y) - 7, 1); Y <= FMath::Min(int32(T.Y) + 7, N - 2); ++Y)
					{
						for (int32 X = FMath::Max(int32(T.X) - 7, 1); X <= FMath::Min(int32(T.X) + 7, N - 2); ++X)
						{
							const FVector2D Q(X, Y);
							const int32 Cc = Idx(X, Y);
							if (FVector2D::DotProduct(Q - T, Dir) <= 0.3f && FVector2D::Distance(Q, T) <= 6.5f && W[Cc] > H[Cc] + 0.05f && Kind[Cc] != 1)
							{
								UpLvl = FMath::Max(UpLvl, W[Cc]);
							}
						}
					}
					if (UpLvl > Lvl[TopC] + 0.4f)
					{
						for (int32 Y = FMath::Max(int32(T.Y) - 8, 1); Y <= FMath::Min(int32(T.Y) + 8, N - 2); ++Y)
						{
							for (int32 X = FMath::Max(int32(T.X) - 8, 1); X <= FMath::Min(int32(T.X) + 8, N - 2); ++X)
							{
								const FVector2D Q(X, Y);
								const float Along = float(FVector2D::DotProduct(Q - T, Dir)), Lat = FMath::Abs(float(FVector2D::CrossProduct(Dir, Q - T)));
								const int32 Cc = Idx(X, Y);
								if (Along > 0.5f || Along < -6.5f || Kind[Cc] == 1 || Kind[Cc] == 2)
								{
									continue;
								}
								if (Lat <= HalfW + 0.25f)
								{
									// the channel: water at the lake's level, its bed under it
									W[Cc] = FMath::Max(W[Cc], UpLvl);
									H[Cc] = FMath::Min(H[Cc], UpLvl - 0.6f);
									if (Kind[Cc] == 0)
									{
										Kind[Cc] = 3;
									}
								}
								else if (Lat <= HalfW + 2.5f && Kind[Cc] == 0)
								{
									// the banks just holding it, a little over it
									H[Cc] = FMath::Max(H[Cc], UpLvl + 0.3f + 0.4f * (Lat - HalfW - 0.25f));
								}
							}
						}
						Fa.Top.Z = UpLvl * 100.f;
					}
				}
				Fa.Bottom = Origin + FVector(P.X * CellCm, P.Y * CellCm, PoolLvl * 100.f);
				Fa.Dir = FVector(Dir.X, Dir.Y, 0.f);
				Fa.DropM = float(Fa.Top.Z) / 100.f - PoolLvl;
				Fa.PoolCm = PoolR * CellCm;
			}
		}
		// Lone spires in a gorge (2026-11-15: thin points 10 m and more stood up beside falls, where two cuts met and left one
		// point between them): no point by a gorge more than 1.5 m over the third highest round it. A wall keeps its height (it
		// has high ground beside it); a point standing alone comes down.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 Y = 1; Y < N - 1; ++Y)
			{
				for (int32 X = 1; X < N - 1; ++X)
				{
					const int32 C = Idx(X, Y);
					// (the spire itself was never cut, so isn't gorge: any point beside the gorge)
					if (!Gorge[C] && !Gorge[C - 1] && !Gorge[C + 1] && !Gorge[C - N] && !Gorge[C + N])
					{
						continue;
					}
					float Hi1 = -1e9f, Hi2 = -1e9f, Hi3 = -1e9f;          // (the third highest: a spire two points thick has one high neighbour)
					for (int32 dy = -1; dy <= 1; ++dy)
					{
						for (int32 dx = -1; dx <= 1; ++dx)
						{
							if (dx || dy)
							{
								const float Hn = H[Idx(X + dx, Y + dy)];
								if (Hn > Hi1) { Hi3 = Hi2; Hi2 = Hi1; Hi1 = Hn; }
								else if (Hn > Hi2) { Hi3 = Hi2; Hi2 = Hn; }
								else if (Hn > Hi3) { Hi3 = Hn; }
							}
						}
					}
					if (H[C] > Hi3 + 1.5f && Kind[C] == 0)
					{
						H[C] = Hi3 + 1.5f;
					}
				}
			}
		}
	}
	{
		// (what the rivers do: their biggest single steps, and at each ravine's head)
		float MaxStep = 0.f;
		int32 Steps2 = 0, Steps3 = 0;
		for (int32 C = 0; C < N * N; ++C)
		{
			if (IsRiverPt(C) && Lvl[C] < 1e8f && Down[C] != INDEX_NONE && IsRiverPt(Down[C]) && Lvl[Down[C]] < 1e8f)
			{
				const float St = Lvl[C] - Lvl[Down[C]];
				MaxStep = FMath::Max(MaxStep, St);
				Steps2 += St >= 2.f ? 1 : 0;
				Steps3 += St >= 3.f ? 1 : 0;
			}
		}
		FString Heads;
		for (const FBFRavine& Rv : Ravines)
		{
			const int32 HX = FMath::RoundToInt((Rv.Path[0].X - Origin.X) / CellCm), HY = FMath::RoundToInt((Rv.Path[0].Y - Origin.Y) / CellCm);
			for (int32 dy = -3; dy <= 3; dy += 3)
			{
				for (int32 dx = -3; dx <= 3; dx += 3)
				{
					const int32 C = Idx(HX + dx, HY + dy);
					Heads += FString::Printf(TEXT(" [%d,%d acc %.0f%s lvl %.1f h %.1f down %s]"), dx, dy, Acc[C], IsRiverPt(C) ? TEXT(" ch") : TEXT(""), Lvl[C] < 1e8f ? Lvl[C] : -1.f, H[C],
						Down[C] != INDEX_NONE ? *FString::Printf(TEXT("%.1f"), H[Down[C]]) : TEXT("-"));
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: river steps: biggest %.1f m, %d over 2 m, %d over 3 m; ravine heads:%s"), MaxStep, Steps2, Steps3, *Heads);
	}
	// Water spreads onto ground lower than itself next to it (a river through a flat or a hollow beside it), so its
	// shore is where the land rises out of it, not where the carving happened to stop.
	{
		TArray<int32> Queue, Steps;
		Steps.Init(0, N * N);
		for (int32 C = 0; C < N * N; ++C)
		{
			if (Kind[C] == 2 || Kind[C] == 3)
			{
				Queue.Add(C);
			}
		}
		for (int32 q = 0; q < Queue.Num(); ++q)
		{
			const int32 C = Queue[q];
			if (Steps[C] >= (Kind[C] == 3 ? 1 : 16))     // a river one step (on a slope it would run off as a sheet)
			{
				continue;
			}
			const int32 X = C % N, Y = C / N;
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 NX = X + D.X, NY = Y + D.Y;
				if (NX < 0 || NY < 0 || NX >= N || NY >= N)
				{
					continue;
				}
				const int32 NC = Idx(NX, NY);
				if (Kind[NC] == 0 && H[NC] < W[C] - 0.02f)
				{
					W[NC] = W[C];
					Kind[NC] = Kind[C];
					Steps[NC] = Steps[C] + 1;
					Queue.Add(NC);
				}
			}
		}
	}
	// Water that would spill (Raven, 2026-11-14: on hillsides it stood up out of the rivers). The pits on a slope were
	// filled as ponds before the rivers were cut, and a river cut through a pond's rim left the pond at its old level,
	// metres above the river beside it. So: no water stands above dry ground next to it (it runs over), and a pond's
	// water above water next to it runs down to it; again till nothing changes, the pond draining across itself.
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfnospill")))          // (-bfnospill: as it was, for the checks' before)
	{
		auto Wet = [&](int32 C) { return Kind[C] != 1 && W[C] > H[C] + 0.02f; };
		TArray<int32> Queue;
		TArray<uint8> In;
		In.Init(0, N * N);
		for (int32 C = 0; C < N * N; ++C)
		{
			if (Wet(C))
			{
				Queue.Add(C);
				In[C] = 1;
			}
		}
		int32 Lowered = 0;
		for (int32 q = 0; q < Queue.Num(); ++q)
		{
			const int32 C = Queue[q];
			In[C] = 0;
			if (!Wet(C))
			{
				continue;
			}
			const int32 X = C % N, Y = C / N;
			float Cap = W[C];
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 NX = X + D.X, NY = Y + D.Y;
				if (NX < 0 || NY < 0 || NX >= N || NY >= N)
				{
					continue;
				}
				const int32 NC = Idx(NX, NY);
				if (Kind[NC] == 1)
				{
					continue;          // the sea takes what reaches it
				}
				if (!Wet(NC))
				{
					Cap = FMath::Min(Cap, FMath::Max(H[NC], H[C]) + 0.02f);      // over the bank: down to the bank (or its own bed)
				}
				else
				{
					// Into lower water beside it: its surface no higher over that water's than its bed is over that
					// water's bed, and 30 cm (a river's surface follows its bed down a slope; a pond's, or water copied
					// over from upstream, runs down to it).
					Cap = FMath::Min(Cap, W[NC] + FMath::Max(0.05f, H[C] - H[NC] + 0.3f));
				}
			}
			if (Cap < W[C] - 0.01f)
			{
				W[C] = Cap;
				++Lowered;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					if (NX >= 0 && NY >= 0 && NX < N && NY < N && !In[Idx(NX, NY)] && Wet(Idx(NX, NY)))
					{
						In[Idx(NX, NY)] = 1;
						Queue.Add(Idx(NX, NY));
					}
				}
			}
			if (Queue.Num() > 40 * N * N)
			{
				break;          // (never: a guard)
			}
		}
		for (int32 C = 0; C < N * N; ++C)
		{
			if (Kind[C] != 1 && Kind[C] != 0 && !(W[C] > H[C] + 0.02f))
			{
				Kind[C] = 0;          // drained dry
				W[C] = -1000.f;
			}
		}
		SpillLowered = Lowered;
	}
	Progress = 0.8f;

	// Biomes from height, slope and wetness: the beach by the sea, rock where it's steep or high, swamp on flat ground by
	// fresh water, forest where a slow noise (wetter near water) says trees, plains everywhere else.
	StageNo = 4;
	TArray<int32> ToWater, ToSea;
	ToWater.Init(MAX_int32, N * N);
	ToSea.Init(MAX_int32, N * N);
	{
		TArray<int32> Queue;
		for (const bool bSea : { false, true })
		{
			TArray<int32>& Dist = bSea ? ToSea : ToWater;
			Queue.Reset();
			for (int32 C = 0; C < N * N; ++C)
			{
				if (bSea ? Kind[C] == 1 : (Kind[C] == 2 || Kind[C] == 3))
				{
					Dist[C] = 0;
					Queue.Add(C);
				}
			}
			for (int32 q = 0; q < Queue.Num(); ++q)
			{
				const int32 C = Queue[q];
				if (Dist[C] >= 80)
				{
					continue;
				}
				const int32 X = C % N, Y = C / N;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					if (NX >= 0 && NY >= 0 && NX < N && NY < N && Dist[Idx(NX, NY)] == MAX_int32)
					{
						Dist[Idx(NX, NY)] = Dist[C] + 1;
						Queue.Add(Idx(NX, NY));
					}
				}
			}
		}
	}
	// An estuary where a river reaches the sea: over its last 48 m its surface comes down to the sea's level (easing in),
	// its bed cut down with it to keep its depth, so the fresh water runs into the sea through a channel in the beach
	// instead of lying on it and stepping down. Its last few metres (within 5 cm of the sea) are the sea's own surface.
	// A lake just behind the beach (within 48 m of the sea) and less than 1.5 m above it is a lagoon: the whole of it comes
	// down to the sea's level, its bed lowered as much so it keeps its depth (lowering only its seaward edge tilted it).
	{
		TArray<int32> Seen, Stack, Cells;
		Seen.Init(0, N * N);
		for (int32 S = 0; S < N * N; ++S)
		{
			if (Kind[S] != 2 || Seen[S])
			{
				continue;
			}
			Cells.Reset();
			Stack.Reset();
			Stack.Add(S);
			Seen[S] = 1;
			int32 Nearest = MAX_int32;
			float Level = 0.f;
			while (Stack.Num() > 0)
			{
				const int32 C = Stack.Pop(EAllowShrinking::No);
				Cells.Add(C);
				Nearest = FMath::Min(Nearest, ToSea[C]);
				Level = FMath::Max(Level, W[C]);
				const int32 X = C % N, Y = C / N;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					if (NX >= 0 && NY >= 0 && NX < N && NY < N && Kind[Idx(NX, NY)] == 2 && !Seen[Idx(NX, NY)])
					{
						Seen[Idx(NX, NY)] = 1;
						Stack.Add(Idx(NX, NY));
					}
				}
			}
			if (Nearest <= 12 && Level <= 1.5f)
			{
				for (const int32 C : Cells)
				{
					const float Drop = W[C] - 0.04f;
					W[C] = 0.04f;
					H[C] -= Drop;
				}
			}
		}
	}
	for (int32 C = 0; C < N * N; ++C)
	{
		if (Kind[C] != 3 || ToSea[C] > 12)
		{
			continue;
		}
		const float T = FMath::Clamp(float(ToSea[C] - 1) / 11.f, 0.f, 1.f);
		const float Depth = FMath::Max(W[C] - H[C], 0.4f);
		W[C] = FMath::Lerp(0.04f, W[C], T * T);
		H[C] = FMath::Min(H[C], W[C] - Depth);
	}
	// Lakes of their own depths (Raven, 2026-11-15: all the lakes were the same depth; rain-washed sediment had filled
	// every hollow flat). Each lake, by its size and height and its own roll: a pond (under ~1,000 m2) 0.6-1.3 m deep, a
	// lowland lake 2-5.5 m, a mountain tarn (over 60 m up) 6-12 m; deepest away from its shore, shelving at its edges
	// over a few metres (where animals drink), its bed lumpy.
	LakeText.Reset();
	{
		TArray<int32> Seen, Stack, Cells, Dist;
		Seen.Init(0, N * N);
		Dist.Init(-1, N * N);
		int32 Ponds = 0, Lakes = 0, Tarns = 0;
		for (int32 S = 0; S < N * N; ++S)
		{
			if (Kind[S] != 2 || Seen[S] || W[S] <= 0.05f)
			{
				continue;
			}
			Cells.Reset();
			Stack.Reset();
			Stack.Add(S);
			Seen[S] = 1;
			float Level = 0.f;
			while (Stack.Num() > 0)
			{
				const int32 C = Stack.Pop(EAllowShrinking::No);
				Cells.Add(C);
				Level = FMath::Max(Level, W[C]);
				const int32 X = C % N, Y = C / N;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					if (NX >= 0 && NY >= 0 && NX < N && NY < N && Kind[Idx(NX, NY)] == 2 && !Seen[Idx(NX, NY)])
					{
						Seen[Idx(NX, NY)] = 1;
						Stack.Add(Idx(NX, NY));
					}
				}
			}
			// how far each of its cells is from its shore (cells)
			TArray<int32> Queue;
			for (const int32 C : Cells)
			{
				const int32 X = C % N, Y = C / N;
				bool bEdge = false;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					bEdge |= NX < 0 || NY < 0 || NX >= N || NY >= N || Kind[Idx(NX, NY)] != 2;
				}
				Dist[C] = bEdge ? 1 : -1;
				if (bEdge)
				{
					Queue.Add(C);
				}
			}
			int32 Far = 1;
			for (int32 q = 0; q < Queue.Num(); ++q)
			{
				const int32 C = Queue[q];
				const int32 X = C % N, Y = C / N;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					if (NX >= 0 && NY >= 0 && NX < N && NY < N && Kind[Idx(NX, NY)] == 2 && Dist[Idx(NX, NY)] < 0)
					{
						Dist[Idx(NX, NY)] = Dist[C] + 1;
						Far = FMath::Max(Far, Dist[C] + 1);
						Queue.Add(Idx(NX, NY));
					}
				}
			}
			FRandomStream LR(Seed * 7 + S);
			float Deep;
			if (Cells.Num() < 60)
			{
				Deep = LR.FRandRange(0.6f, 1.3f);
				++Ponds;
			}
			else if (Level > 60.f)
			{
				Deep = LR.FRandRange(6.f, 12.f);
				++Tarns;
			}
			else
			{
				Deep = LR.FRandRange(2.f, 5.5f);
				++Lakes;
			}
			const float Shelf = FMath::Clamp(0.6f * Far, 1.5f, 10.f);
			for (const int32 C : Cells)
			{
				const float Lumps = 0.85f + 0.3f * FMath::PerlinNoise2D(FVector2D(C % N, C / N) * 0.11f + FVector2D(Seed % 97, 3.f));
				const float Sink = FMath::Max(0.3f, Deep * Lumps * FMath::SmoothStep(0.f, Shelf, float(Dist[C]) - 0.3f));
				H[C] = W[C] - Sink;
			}
		}
		LakeText = FString::Printf(TEXT("%d ponds, %d lakes, %d mountain tarns"), Ponds, Lakes, Tarns);
	}
	// River banks (Raven, 2026-11-15: rivers cut big unnatural channels). A channel was cut straight down into the 4 m
	// grid, its banks cliffs 3-4 m high (more where its water was lowered to run down a slope). Within 28 m of a river the
	// ground rises from the water no steeper than ~32 deg (2.5 m a cell): the banks lie back, a big river's valley widens.
	BanksLowered = 0;
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldbanks")))          // (-bfoldbanks: as it was, for the checks' before)
	{
		const float Rise = 2.5f;
		TArray<int32> Band, Steps;
		Steps.Init(-1, N * N);
		for (int32 C = 0; C < N * N; ++C)
		{
			if (Kind[C] == 3)
			{
				Steps[C] = 0;
				Band.Add(C);
			}
		}
		for (int32 q = 0; q < Band.Num(); ++q)
		{
			const int32 C = Band[q];
			if (Steps[C] >= 7)
			{
				continue;
			}
			const int32 X = C % N, Y = C / N;
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 NX = X + D.X, NY = Y + D.Y;
				if (NX > 0 && NY > 0 && NX < N - 1 && NY < N - 1 && Steps[Idx(NX, NY)] < 0 && Kind[Idx(NX, NY)] == 0 && !Gorge[Idx(NX, NY)])
				{
					Steps[Idx(NX, NY)] = Steps[C] + 1;
					Band.Add(Idx(NX, NY));
				}
			}
		}
		// (2026-11-17, island 20261112 from above: by a fall's lip the river below the fall laid the plateau beside the river
		// above it back at 32 deg, two gullies 35 m deep either side, and the river above stood on a flat causeway between
		// them. Not within 30 m of a fall's lip: the falls' own shaping has that ground.)
		TArray<int32> Dry = Band.FilterByPredicate([&](int32 C)
		{
			if (Kind[C] != 0)
			{
				return false;
			}
			const FVector P = Origin + FVector((C % N) * CellCm, (C / N) * CellCm, 0.f);
			for (const FBFFall& Fa : Falls)
			{
				if (Fa.PoolCm > 0.f && FVector::Dist2D(P, Fa.Top) < Fa.WidthCm + 3000.f)
				{
					return false;
				}
			}
			return true;
		});
		const TArray<float> Before = H;
		auto Base = [&](int32 C) { return Kind[C] == 2 || Kind[C] == 3 ? W[C] + 0.3f : H[C]; };
		for (int32 Pass = 0; Pass < 10; ++Pass)
		{
			for (int32 k = 0; k < Dry.Num(); ++k)
			{
				const int32 C = Dry[Pass % 2 == 0 ? k : Dry.Num() - 1 - k];
				const int32 X = C % N, Y = C / N;
				float Lim = H[C], Floor = -1e9f;
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						if (dx == 0 && dy == 0)
						{
							continue;
						}
						const int32 NC = Idx(X + dx, Y + dy);
						Lim = FMath::Min(Lim, Base(NC) + Rise * ((dx != 0 && dy != 0) ? 1.414f : 1.f));
						if (Kind[NC] == 2 || Kind[NC] == 3)
						{
							Floor = FMath::Max(Floor, W[NC] + 0.3f);
						}
					}
				}
				H[C] = FMath::Max(Lim, FMath::Min(Floor, H[C]));
			}
		}
		for (const int32 C : Dry)
		{
			BanksLowered += H[C] < Before[C] - 0.05f ? 1 : 0;
		}
	}
	// Blades and spires by the gorges and falls (2026-11-16: thin points and blades 10 m and more still stood beside island
	// 13's big fall; clamping a point to its neighbours missed blades two points thick, and the banks pass after it could
	// leave new ones). Last of all, on the ground by a gorge or a fall: an opening (the lowest round each point, then the
	// highest of those round it) is the ground with everything under 3 points (12 m) wide taken away; no point stands more
	// than 1.5 m over it. A gorge wall is the edge of wide high ground and keeps its height; a blade comes down.
	SpiresCut = 0;
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldspires")))
	{
		TArray<uint8> Near;
		Near.Init(0, N * N);
		for (int32 Y = 3; Y < N - 3; ++Y)
		{
			for (int32 X = 3; X < N - 3; ++X)
			{
				bool bNear = false;
				for (int32 dy = -3; dy <= 3 && !bNear; ++dy)
				{
					for (int32 dx = -3; dx <= 3 && !bNear; ++dx)
					{
						bNear = Gorge[Idx(X + dx, Y + dy)] != 0;
					}
				}
				const FVector P = Origin + FVector(X * CellCm, Y * CellCm, 0.f);
				for (const FBFFall& Fa : Falls)
				{
					bNear = bNear || FVector::Dist2D(P, Fa.Bottom) < Fa.PoolCm + 6000.f || FVector::Dist2D(P, Fa.Top) < 4000.f;
				}
				// (2026-11-16, Raven, island 906: spikes 15 m and more on the wet silt beside a river, down from a fall but
				// outside the reach above. Now beside any water too: within 2 points of a wet one)
				for (int32 dy = -2; dy <= 2 && !bNear; ++dy)
				{
					for (int32 dx = -2; dx <= 2 && !bNear; ++dx)
					{
						const int32 Cw = Idx(X + dx, Y + dy);
						bNear = W[Cw] > H[Cw] + 0.05f;
					}
				}
				Near[Idx(X, Y)] = bNear ? 1 : 0;
			}
		}
		TArray<float> Lo = H, Open = H;
		for (int32 Y = 1; Y < N - 1; ++Y)
		{
			for (int32 X = 1; X < N - 1; ++X)
			{
				float M = H[Idx(X, Y)];
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						M = FMath::Min(M, H[Idx(X + dx, Y + dy)]);
					}
				}
				Lo[Idx(X, Y)] = M;
			}
		}
		for (int32 Y = 2; Y < N - 2; ++Y)
		{
			for (int32 X = 2; X < N - 2; ++X)
			{
				const int32 C = Idx(X, Y);
				if (!Near[C] || Kind[C] != 0)
				{
					continue;
				}
				float M = -1e9f;
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						M = FMath::Max(M, Lo[Idx(X + dx, Y + dy)]);
					}
				}
				if (H[C] > M + 1.5f)
				{
					H[C] = M + 1.5f;
					++SpiresCut;
				}
			}
		}
	}
		// A river stepping diagonally from point to point (2026-11-17, top-down pictures of island 906's gorge): its water
	// squares met only at their corners and drew as a chain of diamonds, white and green by turns. The two points
	// beside each diagonal step joined to it: their water the mean of the step's two, their ground under it (at most a
	// metre dug; higher ground is left as a bank). Not by a fall's lip, nor where the step drops more than 4.5 m.
	// -bfnodiagfill: as before.
	int32 DiagFilled = 0;
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfnodiagfill")))
	{
		TArray<int32> FillC;
		TArray<float> FillW;
		auto Wet3 = [&](int32 C) { return Kind[C] == 3 && W[C] > H[C] + 0.02f; };
		for (int32 Y = 2; Y < N - 2; ++Y)
		{
			for (int32 X = 2; X < N - 2; ++X)
			{
				const int32 C = Idx(X, Y);
				if (!Wet3(C))
				{
					continue;
				}
				for (const int32 Sx : { -1, 1 })
				{
					const int32 D = Idx(X + Sx, Y + 1);
					if (!Wet3(D) || FMath::Abs(W[C] - W[D]) > 4.5f)
					{
						continue;
					}
					const int32 Ea = Idx(X + Sx, Y), Eb = Idx(X, Y + 1);
					if (Wet3(Ea) || Wet3(Eb))
					{
						continue;          // (already joined along an edge)
					}
					const float Lv = 0.5f * (W[C] + W[D]);
					bool bByFall = false;
					for (const FBFFall& Fa : Falls)
					{
						bByFall = bByFall || FVector::Dist2D(Origin + FVector(X * CellCm, Y * CellCm, 0.f), Fa.Top) < Fa.WidthCm + 500.f;
					}
					if (bByFall)
					{
						continue;
					}
					for (const int32 E : { Ea, Eb })
					{
						if (Kind[E] != 1 && Kind[E] != 2 && H[E] < Lv + 1.f)
						{
							FillC.Add(E);
							FillW.Add(Lv);
						}
					}
				}
			}
		}
		for (int32 i = 0; i < FillC.Num(); ++i)
		{
			const int32 E = FillC[i];
			W[E] = FMath::Max(W[E] > H[E] ? W[E] : -1000.f, FillW[i]);
			H[E] = FMath::Min(H[E], W[E] - 0.3f);
			Kind[E] = 3;
			++DiagFilled;
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: %d points filled beside diagonal river steps"), DiagFilled);
	}
	// Perched water (2026-11-17, island 906 from above: white and green diamonds by a plunge pool). River points left from
	// the old cascade beside a pool kept their water 6-9 m over the pool's, each drawn as a lone diamond: a point whose
	// water stands 3 m or more over a deep neighbour's (a pool's) is dried; then any river point with no wet point round it.
	// -bfnoperched: as before.
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfnoperched")))
	{
		auto WetAt = [&](int32 C) { return (Kind[C] == 3 || Kind[C] == 2) && W[C] > H[C] + 0.02f; };
		int32 Perched = 0, Lone = 0;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			TArray<int32> DryC;
			for (int32 Y = 2; Y < N - 2; ++Y)
			{
				for (int32 X = 2; X < N - 2; ++X)
				{
					const int32 C = Idx(X, Y);
					if (Kind[C] != 3 || !WetAt(C))
					{
						continue;
					}
					int32 Wet = 0;
					bool bOver = false;
					for (int32 dy = -1; dy <= 1; ++dy)
					{
						for (int32 dx = -1; dx <= 1; ++dx)
						{
							const int32 E = Idx(X + dx, Y + dy);
							if ((dx || dy) && WetAt(E))
							{
								++Wet;
								bOver = bOver || (Pass == 0 && W[E] - H[E] > 2.5f && W[C] > W[E] + 3.f);
							}
						}
					}
					if (bOver || Wet == 0)
					{
						DryC.Add(C);
						(bOver ? Perched : Lone)++;
					}
				}
			}
			for (const int32 C : DryC)
			{
				W[C] = -1000.f;
				Kind[C] = 0;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: %d perched river points and %d lone ones dried"), Perched, Lone);
	}
	// Each fall's lip at the water above it, last of all (2026-11-16, island 906: the passes after the falls' shaping set the
	// river's levels again, and a lake's outlet at 160 m fed a box of water at 157 m at the edge, banks round it). The highest
	// water within 6 points upstream of the lip; the channel from there to the edge filled to it, the banks beside just
	// over it. -bfoldlip: as before.
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldlip")))
	{
		for (FBFFall& Fa : Falls)
		{
			if (Fa.PoolCm <= 0.f)
			{
				continue;
			}
			const FVector2D T((Fa.Top.X - Origin.X) / CellCm, (Fa.Top.Y - Origin.Y) / CellCm);
			const FVector2D Dir = FVector2D(Fa.Dir).GetSafeNormal();
			const float HalfW = FMath::Max(0.5f * Fa.WidthCm / CellCm, 0.6f);
			float UpLvl = -1000.f;
			for (int32 Y = FMath::Max(int32(T.Y) - 7, 1); Y <= FMath::Min(int32(T.Y) + 7, N - 2); ++Y)
			{
				for (int32 X = FMath::Max(int32(T.X) - 7, 1); X <= FMath::Min(int32(T.X) + 7, N - 2); ++X)
				{
					const FVector2D Q(X, Y);
					const int32 Cc = Idx(X, Y);
					const float Al = float(FVector2D::DotProduct(Q - T, Dir)), La = FMath::Abs(float(FVector2D::CrossProduct(Dir, Q - T)));
					if (Al <= 0.3f && Al >= -6.5f && La <= HalfW + 2.f && W[Cc] > H[Cc] + 0.05f && Kind[Cc] != 1 && (Kind[Cc] == 2 || Al >= -1.5f))          // (2026-11-17: a lake's water, or the river's right at the edge: the rapids 6 points up the river on island 20261112 raised its lip into a flat box)
					{
						UpLvl = FMath::Max(UpLvl, W[Cc]);
					}
				}
			}
			// (only where the water above stands well over the lip, as at a lake's outlet: elsewhere the lip is left as it is.
			// Run on every fall, it raised banks by island 13's big fall into columns that hid its curtain.)
			const int32 Ct = Idx(FMath::Clamp(FMath::RoundToInt(T.X), 1, N - 2), FMath::Clamp(FMath::RoundToInt(T.Y), 1, N - 2));
			const float LipNow = W[Ct] > H[Ct] + 0.05f ? W[Ct] : H[Ct];          // (the lip point's water as the passes left it)
			if (UpLvl < LipNow + 1.5f)
			{
				continue;
			}
			for (int32 Y = FMath::Max(int32(T.Y) - 8, 1); Y <= FMath::Min(int32(T.Y) + 8, N - 2); ++Y)
			{
				for (int32 X = FMath::Max(int32(T.X) - 8, 1); X <= FMath::Min(int32(T.X) + 8, N - 2); ++X)
				{
					const FVector2D Q(X, Y);
					const float Al = float(FVector2D::DotProduct(Q - T, Dir)), La = FMath::Abs(float(FVector2D::CrossProduct(Dir, Q - T)));
					const int32 Cc = Idx(X, Y);
					if (Al > 0.5f || Al < -6.5f || Kind[Cc] == 1 || Kind[Cc] == 2)
					{
						continue;
					}
					if (La <= HalfW + 0.25f)
					{
						W[Cc] = UpLvl;
						H[Cc] = FMath::Min(H[Cc], UpLvl - 0.6f);
						if (Kind[Cc] == 0)
						{
							Kind[Cc] = 3;
						}
					}
					else if (La <= HalfW + 2.5f && !(W[Cc] > H[Cc] + 0.05f && W[Cc] >= UpLvl - 0.05f))
					{
						H[Cc] = FMath::Max(H[Cc], UpLvl + 0.3f + 0.4f * (La - HalfW - 0.25f));
						W[Cc] = -1000.f;
						if (Kind[Cc] == 3)
						{
							Kind[Cc] = 0;
						}
					}
				}
			}
			Fa.Top.Z = UpLvl * 100.f;
		}
	}
	// A rim between a lake (or wide water) and a fall's cliff (2026-11-17, a closer look: above island 13's 8 m fall the
	// pond ran right to the cut cliff along a straight edge 20 m wide, an infinity pool, the water falling from only 6 m
	// of it). Near each fall, water at the cliff's edge (a neighbour 3 m or more under its level) outside the outlet's
	// notch becomes a low rim of land just over the water; -bfnorim: as before.
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfnorim")))
	{
		int32 Rimmed = 0;
		for (const FBFFall& Fa : Falls)
		{
			if (Fa.PoolCm <= 0.f)
			{
				continue;
			}
			const FVector2D T((Fa.Top.X - Origin.X) / CellCm, (Fa.Top.Y - Origin.Y) / CellCm);
			const FVector2D Dir = FVector2D(Fa.Dir).GetSafeNormal();
			const float HalfW = FMath::Max(0.5f * Fa.WidthCm / CellCm, 0.6f);
			const float TopLvl = float(Fa.Top.Z) / 100.f;
			TArray<int32> Dry;
			for (int32 Y = FMath::Max(int32(T.Y) - 8, 1); Y <= FMath::Min(int32(T.Y) + 8, N - 2); ++Y)
			{
				for (int32 X = FMath::Max(int32(T.X) - 8, 1); X <= FMath::Min(int32(T.X) + 8, N - 2); ++X)
				{
					const int32 Cc = Idx(X, Y);
					if (!(W[Cc] > H[Cc] + 0.05f) || Kind[Cc] == 1 || FMath::Abs(W[Cc] - TopLvl) > 1.5f)
					{
						continue;
					}
					const FVector2D Q(X, Y);
					const float Al = float(FVector2D::DotProduct(Q - T, Dir)), La = FMath::Abs(float(FVector2D::CrossProduct(Dir, Q - T)));
					if (La <= HalfW + 0.3f && Al > -3.f)          // (2026-11-22: +0.75 spared the diagonal neighbours, island 13's 8 m fall leaving its pond along 3 points)
					{
						continue;          // the notch the water leaves by
					}
					bool bEdge = false;
					for (int32 dy = -1; dy <= 1 && !bEdge; ++dy)
					{
						for (int32 dx = -1; dx <= 1; ++dx)
						{
							if (H[Idx(X + dx, Y + dy)] < W[Cc] - 3.f)
							{
								bEdge = true;
								break;
							}
						}
					}
					if (bEdge)
					{
						Dry.Add(Cc);
					}
				}
			}
			for (const int32 Cc : Dry)
			{
				const int32 X = Cc % N, Y = Cc / N;
				H[Cc] = W[Cc] + 0.35f + 0.25f * FMath::PerlinNoise2D(FVector2D(X * 0.4f, Y * 0.4f));
				W[Cc] = -1000.f;
				Kind[Cc] = 0;
				++Rimmed;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: %d lake/river points at a fall's cliff edge made a rim"), Rimmed);
	}
	{
		float Biggest = 0.f;
		for (const FBFFall& Fa : Falls)
		{
			Biggest = FMath::Max(Biggest, Fa.DropM);
		}
		LandformText = FString::Printf(TEXT("%d ravines%s; %d waterfalls (the biggest %.0f m)"), Ravines.Num(),
			Ravines.Num() ? *FString::Printf(TEXT(" (the deepest %.0f m)"), Ravines[0].DeepM) : TEXT(""), Falls.Num(), Biggest);
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: %s; river banks laid back at %d points; %d spire points cut; %s"), *LakeText, BanksLowered, SpiresCut, *LandformText);
	SeaSteps.SetNumUninitialized(N * N);
	for (int32 C = 0; C < N * N; ++C)
	{
		SeaSteps[C] = uint8(FMath::Min(ToSea[C], 255));
	}
	Biome.SetNumUninitialized(N * N);
	TArray<FColor> Raw;
	Raw.SetNumUninitialized(N * N);
	int32 Count[8] = {};
	float Steepest = 0.f;
	ParallelFor(N, [&](int32 Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 C = Idx(X, Y);
			const float GX = (H[Idx(FMath::Min(X + 1, N - 1), Y)] - H[Idx(FMath::Max(X - 1, 0), Y)]) / (2.f * CellM);
			const float GY = (H[Idx(X, FMath::Min(Y + 1, N - 1))] - H[Idx(X, FMath::Max(Y - 1, 0))]) / (2.f * CellM);
			const float SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY)));
			const float MX = X * CellM, MY = Y * CellM;
			const float Woods = Fbm(5, MX / 320.f, MY / 320.f, 4) + (ToWater[C] < 40 ? 0.25f * (1.f - ToWater[C] / 40.f) : 0.f);
			EBFBiome B;
			if (Kind[C] == 1)
			{
				B = EBFBiome::Sea;
			}
			else if (Kind[C] == 2)
			{
				B = EBFBiome::Lake;
			}
			else if (Kind[C] == 3)
			{
				B = EBFBiome::River;
			}
			else if (H[C] < 3.f && ToSea[C] < 20)
			{
				B = EBFBiome::Beach;
			}
			else if (SlopeDeg > 30.f || H[C] > 150.f)
			{
				B = EBFBiome::Rock;
			}
			else if (ToWater[C] < 6 && SlopeDeg < 8.f && Fbm(6, MX / 200.f, MY / 200.f, 3) > -0.1f)
			{
				B = EBFBiome::Swamp;
			}
			else if (Woods > 0.08f)
			{
				B = EBFBiome::Forest;
			}
			else
			{
				B = EBFBiome::Plains;
			}
			Biome[C] = uint8(B);
			const float Shade = 1.f + 0.12f * Fbm(7, MX / 40.f, MY / 40.f, 3);
			FColor Base = BiomeColour[uint8(B)];
			// (2026-11-15: a river's or lake's edge is wet silt and gravel, not grass to the waterline; in a gorge, bare
			// rock down to the water)
			bool bLipRock = false;
			for (const FBFFall& Fa : Falls)
			{
				bLipRock = bLipRock || (Fa.PoolCm > 0.f && FVector::Dist2D(Origin + FVector(X * CellCm, Y * CellCm, 0.f), Fa.Top) < Fa.WidthCm + 3200.f);
			}
			if (Kind[C] == 0 && bLipRock)
			{
				// (2026-11-16: the banks at a fall's lip are bare wet rock, whatever is beside them: tan sand there read as a box)
				const FColor Wr(96, 95, 92);
				Base = FColor(uint8(FMath::Lerp(float(Base.R), float(Wr.R), 0.8f)), uint8(FMath::Lerp(float(Base.G), float(Wr.G), 0.8f)), uint8(FMath::Lerp(float(Base.B), float(Wr.B), 0.8f)));
			}
			else if (Kind[C] == 0 && B != EBFBiome::Beach && ToWater[C] <= 2)
			{
				// (and by a fall's lip, wet grey rock, not tan silt: 2026-11-16)
				bool bByLip = false;
				for (const FBFFall& Fa : Falls)
				{
					bByLip = bByLip || (Fa.PoolCm > 0.f && FVector::Dist2D(Origin + FVector(X * CellCm, Y * CellCm, 0.f), Fa.Top) < Fa.WidthCm + 1600.f);
				}
				const FColor Wet = (Gorge[C] || bByLip) ? FColor(92, 91, 88) : FColor(98, 88, 66);
				const float Mix = ToWater[C] <= 1 ? 0.75f : 0.35f;
				Base = FColor(uint8(FMath::Lerp(float(Base.R), float(Wet.R), Mix)), uint8(FMath::Lerp(float(Base.G), float(Wet.G), Mix)), uint8(FMath::Lerp(float(Base.B), float(Wet.B), Mix)));
			}
			Raw[C] = FColor(uint8(FMath::Clamp(Base.R * Shade, 0.f, 255.f)), uint8(FMath::Clamp(Base.G * Shade, 0.f, 255.f)), uint8(FMath::Clamp(Base.B * Shade, 0.f, 255.f)));
		}
	});
	for (int32 C = 0; C < N * N; ++C)
	{
		++Count[Biome[C]];
		const int32 X = C % N, Y = C / N;
		if (X > 0 && Y > 0 && X < N - 1 && Y < N - 1 && Kind[C] == 0)
		{
			const float GX = (H[C + 1] - H[C - 1]) / (2.f * CellM), GY = (H[C + N] - H[C - N]) / (2.f * CellM);
			Steepest = FMath::Max(Steepest, FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY))));
		}
	}
	// Soft borders between biomes: each point's colour the average of its neighbours'.
	Colour.SetNumUninitialized(N * N);
	ParallelFor(N, [&](int32 Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			int32 Rs = 0, Gs = 0, Bs = 0, Ns = 0;
			for (int32 dy = -1; dy <= 1; ++dy)
			{
				for (int32 dx = -1; dx <= 1; ++dx)
				{
					const int32 NX = FMath::Clamp(X + dx, 0, N - 1), NY = FMath::Clamp(Y + dy, 0, N - 1);
					const FColor& Cl = Raw[Idx(NX, NY)];
					Rs += Cl.R;
					Gs += Cl.G;
					Bs += Cl.B;
					++Ns;
				}
			}
			Colour[Idx(X, Y)] = FColor(Rs / Ns, Gs / Ns, Bs / Ns, 255);
		}
	});

	// The water's own map. Currents: each point of a river flows the way its water drains, as fast as the surface's fall
	// says (0.3 m/s on the flat to 3 m/s down a steep reach), a lake drifts slowly to its outlet; smoothed so the
	// current bends with the channel, slower against the banks. The sea: how deep it is and how far each point of it
	// is from the land (a chamfer distance), for its waves and its surf.
	Flow.Init(FVector2f::ZeroVector, N * N);
	auto IsFresh = [&](int32 C) { return Kind[C] == 2 || Kind[C] == 3; };
	for (int32 C = 0; C < N * N; ++C)
	{
		if (!IsFresh(C) || Down[C] == INDEX_NONE)
		{
			continue;
		}
		const int32 D = Down[C];
		FVector2f Dir(float(D % N - C % N), float(D / N - C / N));
		const float Len = Dir.Size();
		Dir /= Len;
		float Speed = 0.06f;
		if (Kind[C] == 3)
		{
			const float Fall = FMath::Max(W[C] - (IsFresh(D) ? W[D] : H[D]), 0.f) / (Len * CellM);
			Speed = FMath::Clamp(0.3f + 15.f * FMath::Sqrt(Fall), 0.3f, 3.f);
			if (ToSea[C] <= 12)
			{
				Speed = FMath::Min(Speed, 0.5f);
			}
		}
		Flow[C] = Dir * Speed;
	}
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		const TArray<FVector2f> Was = Flow;
		ParallelFor(N, [&](int32 Y)
		{
			for (int32 X = 1; X < N - 1; ++X)
			{
				const int32 C = Idx(X, Y);
				if (!IsFresh(C) || Y == 0 || Y == N - 1)
				{
					continue;
				}
				FVector2f Sum = FVector2f::ZeroVector;
				int32 Wet = 0;
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						if (IsFresh(C + dy * N + dx))
						{
							Sum += Was[C + dy * N + dx];
							++Wet;
						}
					}
				}
				Flow[C] = Sum / float(Wet) * (Pass == 3 ? 1.f - 0.07f * (9 - Wet) : 1.f);
			}
		});
	}
	ShoreM.Init(1e6f, N * N);
	for (int32 C = 0; C < N * N; ++C)
	{
		if (Kind[C] != 1)
		{
			ShoreM[C] = 0.f;
		}
	}
	for (int32 Sweep = 0; Sweep < 2; ++Sweep)       // forward, then backward: each point the least of its neighbours + a step
	{
		const int32 S = Sweep == 0 ? 1 : -1;
		for (int32 Y = Sweep == 0 ? 0 : N - 1; Y >= 0 && Y < N; Y += S)
		{
			for (int32 X = Sweep == 0 ? 0 : N - 1; X >= 0 && X < N; X += S)
			{
				float& D = ShoreM[Idx(X, Y)];
				const int32 PY = Y - S;
				if (X - S >= 0 && X - S < N)
				{
					D = FMath::Min(D, ShoreM[Idx(X - S, Y)] + CellM);
				}
				if (PY >= 0 && PY < N)
				{
					D = FMath::Min(D, ShoreM[Idx(X, PY)] + CellM);
					if (X - 1 >= 0)
					{
						D = FMath::Min(D, ShoreM[Idx(X - 1, PY)] + 1.4142f * CellM);
					}
					if (X + 1 < N)
					{
						D = FMath::Min(D, ShoreM[Idx(X + 1, PY)] + 1.4142f * CellM);
					}
				}
			}
		}
	}
	InfoPixels.SetNumUninitialized(N * N);
	for (int32 C = 0; C < N * N; ++C)
	{
		const bool bSea = Kind[C] == 1;
		auto Byte = [](float V) { return uint8(FMath::Clamp(FMath::RoundToInt(V), 0, 255)); };
		InfoPixels[C] = FColor(bSea ? Byte(-H[C] / 40.f * 255.f) : 0, bSea ? Byte(ShoreM[C] * 2.f) : 0,
			Byte(127.5f + Flow[C].X / 3.f * 127.5f), Byte(127.5f + Flow[C].Y / 3.f * 127.5f));
	}
	PlacePlants(ToWater, Kind);

	float Lo = 1e9f, Hi = -1e9f;
	for (const float Z : H)
	{
		Lo = FMath::Min(Lo, Z);
		Hi = FMath::Max(Hi, Z);
	}
	const float Land = float(N * N - Count[uint8(EBFBiome::Sea)]);
	auto Share = [&](EBFBiome B) { return 100.f * Count[uint8(B)] / FMath::Max(Land, 1.f); };
	// The check on the water: fresh water standing higher than dry ground right beside it (it would show as water above
	// the land: none should).
	int32 Perched = 0;
	for (int32 Y = 1; Y < N - 1; ++Y)
	{
		for (int32 X = 1; X < N - 1; ++X)
		{
			const int32 C = Idx(X, Y);
			if (!(W[C] > 0.05f && W[C] > H[C]))
			{
				continue;
			}
			for (const int32 NC : { C + 1, C - 1, C + N, C - N })
			{
				if (!(W[NC] > 0.05f && W[NC] > H[NC]) && Kind[NC] != 1 && H[NC] < W[C] - 0.1f)
				{
					++Perched;
					break;
				}
			}
		}
	}
	Summary = FString::Printf(TEXT("seed %d: made in %.1f s; %.1f km2 of land, %.0f to %.0f m; plains %.0f%%, forest %.0f%%, swamp %.0f%%, rock %.0f%%, beach %.0f%%, lakes %.1f%%, rivers %.1f%% of the land; steepest %.0f deg; fresh water above the dry ground beside it at %d points"),
		Seed, FPlatformTime::Seconds() - T0, Land * CellM * CellM / 1e6f, Lo, Hi, Share(EBFBiome::Plains), Share(EBFBiome::Forest), Share(EBFBiome::Swamp),
		Share(EBFBiome::Rock), Share(EBFBiome::Beach), Share(EBFBiome::Lake), Share(EBFBiome::River), Steepest, Perched);
	StageNo = 5;
	Progress = 0.85f;
}

void ABFWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bReady)
	{
		TickPlants(DeltaSeconds);
		TickFoam(DeltaSeconds);
		// The day goes round: the sun and the moon follow the island's clock.
		Hours += DeltaSeconds * 24.f / FMath::Max(DaySeconds, 1.f);
		if (Hours >= 24.f)
		{
			Hours -= 24.f;
			++Day;
		}
		if (ABFGameMode* GM = GetWorld()->GetAuthGameMode<ABFGameMode>())
		{
			GM->SetSkyAt(Hours);
		}
	}
	if (bReady && SeaSurface)
	{
		// The sea's fine grid stays under the camera, moved in whole steps of its coarsest waved rows (32 m) so its points
		// keep landing on the same places and the waves don't swim.
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
			const double Snap = 3200.0;
			SeaSurface->SetWorldLocation(FVector(FMath::RoundToDouble(Cam.X / Snap) * Snap, FMath::RoundToDouble(Cam.Y / Snap) * Snap, Origin.Z));
		}
	}
	if (bReady || !bLandDone)
	{
		return;
	}
	if (!GroundMat)
	{
		GroundMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeTerrain.M_BoneForgeTerrain"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!GroundMat)
		{
			GroundMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/VertexColorMaterial.VertexColorMaterial"));
		}
		// The water's map, made with the land: uploaded once as a texture the water material reads.
		Info = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		Info->SRGB = false;
		Info->Filter = TF_Bilinear;
		Info->AddressX = TA_Clamp;
		Info->AddressY = TA_Clamp;
		Info->CompressionSettings = TC_VectorDisplacementmap;
		void* Pixels = Info->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Pixels, InfoPixels.GetData(), InfoPixels.Num() * sizeof(FColor));
		Info->GetPlatformData()->Mips[0].BulkData.Unlock();
		Info->UpdateResource();
		if (UMaterialInterface* Sea = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeSea.M_BoneForgeSea"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			// The sea: clear, blue-green with depth, its waves on. Lakes and rivers: a little peaty and silty (red and
			// blue soaked up sooner, more scattered back), calm but for their ripples and their currents.
			for (const bool bSea : { true, false })
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Sea, this);
				M->SetTextureParameterValue(TEXT("Info"), Info);
				M->SetVectorParameterValue(TEXT("Origin"), FLinearColor(float(Origin.X), float(Origin.Y), float(Origin.Z), 0.f));
				M->SetScalarParameterValue(TEXT("Cell"), CellCm);
				M->SetScalarParameterValue(TEXT("Count"), float(N));
				M->SetScalarParameterValue(TEXT("Waves"), bSea ? 1.f : 0.f);
				M->SetVectorParameterValue(TEXT("Absorption"), bSea ? FLinearColor(0.0045f, 0.0009f, 0.0006f) : FLinearColor(0.004f, 0.0016f, 0.0028f));
				M->SetVectorParameterValue(TEXT("Scattering"), bSea ? FLinearColor(0.0001f, 0.00035f, 0.0004f) : FLinearColor(0.0005f, 0.0009f, 0.0006f));
				(bSea ? WaterMat : FreshMat) = M;
			}
		}
		else if (UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeWater.M_BoneForgeWater")))
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Water, this);
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.012f, 0.06f, 0.09f));
			M->SetScalarParameterValue(TEXT("Opacity"), 0.78f);
			WaterMat = FreshMat = M;
		}
	}
	// A few tiles a frame, so the game keeps running while the island appears.
	const int32 PerSide = (N - 1) / ChunkCells;
	for (int32 k = 0; k < 4 && NextChunk < PerSide * PerSide; ++k, ++NextChunk)
	{
		BuildChunk(NextChunk % PerSide, NextChunk / PerSide);
	}
	Progress = 0.85f + 0.1f * float(NextChunk) / float(PerSide * PerSide);
	if (NextChunk >= PerSide * PerSide && bSeaBuilt)
	{
		// The plants: one kind and shape a frame (its model built the first time a game, then all of them placed).
		StageNo = 6;
		if (!PlantMat)
		{
			PlantMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgePlant.M_BoneForgePlant"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!PlantMat)
			{
				PlantMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/VertexColorMaterial.VertexColorMaterial"));
			}
			PlantComps.SetNum(NumKV);
		}
		if (NextPlants < NumKV)
		{
			GrowPlants(NextPlants++);
			Progress = 0.95f + 0.05f * float(NextPlants) / NumKV;
			return;
		}
		Active = this;
		bReady = true;
		PlaceCaves();
		PlaceRocks();
		DressRavines();
		DressRivers();
		BuildFalls();
		Progress = 1.f;
		StageNo = 7;
		Summary += FString::Printf(TEXT("; %s; %s; %s; %s; ready in %.1f s"), *PlantSummary(), *CaveSummary, *LandformText, *RockSummary, FPlatformTime::Seconds() - StartedAt);
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] world %s"), *Summary);
		return;
	}
	if (NextChunk >= PerSide * PerSide)
	{
		// The open sea round the island: its surface (BuildSea) and a sandy floor deep under it, far enough out that its
		// edge is lost in the haze.
		UProceduralMeshComponent* Sea = NewObject<UProceduralMeshComponent>(this);
		Sea->SetupAttachment(GetRootComponent());
		Sea->RegisterComponent();
		const double Far = 12.0 * SizeCm();
		const FVector C = GetCentre();
		const TArray<FVector> Floor = { C + FVector(-Far, -Far, -3200.0), C + FVector(Far, -Far, -3200.0), C + FVector(-Far, Far, -3200.0), C + FVector(Far, Far, -3200.0) };
		Sea->CreateMeshSection(0, Floor, { 0, 2, 1, 1, 2, 3 }, { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector }, {},
			{ BiomeColour[0], BiomeColour[0], BiomeColour[0], BiomeColour[0] }, {}, false);
		Sea->SetMaterial(0, GroundMat);
		Sea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tiles.Add(Sea);
		BuildSea();
		bSeaBuilt = true;
	}
}

void ABFWorld::GroundSection(int32 CX, int32 CY, TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FColor>& Colours) const
{
	// A tile's ground: a vertex every 4 m, two triangles a cell (none in a cell under a rock piece).
	const int32 V = ChunkCells + 1;
	Verts.Reserve(V * V);
	const float CellM = CellCm / 100.f;
	for (int32 y = 0; y < V; ++y)
	{
		for (int32 x = 0; x < V; ++x)
		{
			const int32 GX = CX * ChunkCells + x, GY = CY * ChunkCells + y;
			const int32 C = Idx(GX, GY);
			Verts.Add(Origin + FVector(GX * CellCm, GY * CellCm, H[C] * 100.f));
			const float DX = H[Idx(FMath::Min(GX + 1, N - 1), GY)] - H[Idx(FMath::Max(GX - 1, 0), GY)];
			const float DY = H[Idx(GX, FMath::Min(GY + 1, N - 1))] - H[Idx(GX, FMath::Max(GY - 1, 0))];
			Normals.Add(FVector(-DX, -DY, 2.f * CellM).GetSafeNormal());
			Colours.Add(Colour[C]);
		}
	}
	for (int32 y = 0; y < ChunkCells; ++y)
	{
		for (int32 x = 0; x < ChunkCells; ++x)
		{
			if (Caves.Num() > 0 && CellHoled(CX * ChunkCells + x, CY * ChunkCells + y))
			{
				continue;
			}
			const int32 A = y * V + x, B = A + 1, C = A + V, D = C + 1;
			Tris.Append({ A, C, B, B, C, D });
		}
	}
}

bool ABFWorld::CellHoled(int32 GX, int32 GY) const
{
	for (const FBFCave& C : Caves)
	{
		if (GX >= C.Hole.Min.X && GX < C.Hole.Max.X && GY >= C.Hole.Min.Y && GY < C.Hole.Max.Y)
		{
			return true;
		}
	}
	return false;
}

void ABFWorld::BuildChunk(int32 CX, int32 CY)
{
	const int32 V = ChunkCells + 1;
	TArray<FVector> Verts, Normals, WVerts, WNormals;
	TArray<FColor> Colours;
	TArray<int32> Tris, WTris;
	GroundSection(CX, CY, Verts, Tris, Normals, Colours);
	// Fresh water (lakes, rivers) on its own surface: a vertex at the water's level where it or a neighbour is wet, so
	// the surface meets the banks; the sea is the one wide plane.
	TArray<int32> WIndex;
	WIndex.Init(INDEX_NONE, V * V);
	TArray<uint8> WetV;
	WetV.Init(0, V * V);
	TArray<FVector> RVerts, RNormals;          // (steep reaches: white water, the fall's material)
	TArray<FVector2D> RUV;
	TArray<int32> RTris;
	for (int32 y = 0; y < V; ++y)
	{
		for (int32 x = 0; x < V; ++x)
		{
			const int32 GX = CX * ChunkCells + x, GY = CY * ChunkCells + y;
			float Level = -1000.f;
			int32 NearSq = MAX_int32;
			for (int32 dy = -2; dy <= 2; ++dy)          // two cells round: the surface runs on under the banks
			{
				for (int32 dx = -2; dx <= 2; ++dx)
				{
					const int32 NX = FMath::Clamp(GX + dx, 0, N - 1), NY = FMath::Clamp(GY + dy, 0, N - 1);
					const int32 NC = Idx(NX, NY);
					const int32 Sq = dx * dx + dy * dy;
					// The nearest wet point's level (the highest round it, as it was, on a slope was upstream's: steps
					// standing up out of the river).
					if (W[NC] > 0.05f && W[NC] > H[NC] && (Sq < NearSq || (Sq == NearSq && W[NC] < Level)))
					{
						NearSq = Sq;
						Level = W[NC];
					}
				}
			}
			// A point of dry ground takes the water's level only where its ground is higher (under a bank, hidden): where it's
			// lower, the surface dips just under it, so the shore is where the land comes up out of the water and water never
			// lies on top of lower ground beside it.
			const int32 C0 = Idx(GX, GY);
			if (Level > -999.f && !(W[C0] > 0.05f && W[C0] > H[C0]))
			{
				Level = FMath::Min(Level, H[C0] - 0.05f);
			}
			// None over the sea (a river's mouth comes down to the sea's level in MakeLand and hands over to the sea there).
			const uint8 ToSea = SeaSteps[C0];
			WetV[y * V + x] = (W[C0] > 0.05f && W[C0] > H[C0]) ? 1 : 0;
			if (Level > -999.f && ToSea > 0)
			{
				WIndex[y * V + x] = WVerts.Add(Origin + FVector(GX * CellCm, GY * CellCm, Level * 100.f));
				WNormals.Add(FVector::UpVector);
			}
		}
	}
	for (int32 y = 0; y < ChunkCells; ++y)
	{
		for (int32 x = 0; x < ChunkCells; ++x)
		{
			const int32 A = WIndex[y * V + x], B = WIndex[y * V + x + 1], C = WIndex[(y + 1) * V + x], D = WIndex[(y + 1) * V + x + 1];
			if (A != INDEX_NONE && B != INDEX_NONE && C != INDEX_NONE && D != INDEX_NONE)
			{
				// (None down a fall's face, 3 m or more across one cell: the curtain is its water there, and the surface
				// sloping down the face hid the hollow behind it, 2026-11-15.)
				const float Z0 = float(WVerts[A].Z), Z1 = float(WVerts[B].Z), Z2 = float(WVerts[C].Z), Z3 = float(WVerts[D].Z);
				// (2026-11-16, Raven: "messed up square water" on island 906: every steep square anywhere was left out, so a
				// cascade below a fall was a row of loose diamonds. Now only those on a fall's own face.)
				if (FMath::Max(FMath::Max(Z0, Z1), FMath::Max(Z2, Z3)) - FMath::Min(FMath::Min(Z0, Z1), FMath::Min(Z2, Z3)) >= 300.f && Falls.Num() > 0)
				{
					const FVector Mid = (WVerts[A] + WVerts[D]) * 0.5f;
					bool bOnFace = false;
					for (const FBFFall& Fa : Falls)
					{
						if (Fa.PoolCm <= 0.f)
						{
							continue;          // (a small fall in steps: its water drawn down them)
						}
						const FVector2D D2 = FVector2D(Fa.Dir).GetSafeNormal(), Rel = FVector2D(Mid - Fa.Top);
						const float AlongF = float(FVector2D::DotProduct(Rel, D2)), Lat = FMath::Abs(float(FVector2D::CrossProduct(D2, Rel)));
						const float Reach = float(FVector2D::DotProduct(FVector2D(Fa.Bottom - Fa.Top), D2));
						bOnFace = bOnFace || (AlongF > 0.3f * CellCm && AlongF < Reach + CellCm && Lat < 0.5f * Fa.WidthCm + 2.f * CellCm);
					}
					// (and not with a dry corner: a bank point takes the nearest water's level, which on a steep reach may be
					// metres off, and those squares stood up as green shards across the gorge)
					// (nor where a corner's water is deep, or the square stands near sheer: water left over lowered ground)
					// (2026-11-16, Raven, island 906 "messed up square water": a steep reach a point wide went as a row of loose
					// diamonds. A bank corner takes the nearest water's level, metres off on a steep reach (green shards when
					// drawn). Now: with two wet corners or more, none deep, a bank corner takes the mean of the square's own wet
					// corners, and the square is drawn as white water tumbling down.)
					const int32 Vs[4] = { y * V + x, y * V + x + 1, (y + 1) * V + x, (y + 1) * V + x + 1 };
					const int32 Ws[4] = { A, B, C, D };
					bool bDeep = false;
					int32 NWet = 0;
					float WetZ = 0.f;
					for (int32 k = 0; k < 4; ++k)
					{
						const int32 Cv = Idx(FMath::Clamp(CX * ChunkCells + Vs[k] % V, 0, N - 1), FMath::Clamp(CY * ChunkCells + Vs[k] / V, 0, N - 1));
						if (WetV[Vs[k]])
						{
							bDeep = bDeep || W[Cv] - H[Cv] > 2.5f;          // (water left over lowered ground)
							++NWet;
							WetZ += float(WVerts[Ws[k]].Z);
						}
					}
					if (bOnFace || bDeep || NWet < (FParse::Param(FCommandLine::Get(), TEXT("bfrapids2")) ? 2 : 3))          // (2026-11-17: with only two wet corners its other two at their mean stood flat over the bank as pale diamonds, islands 13 and 906 from above; -bfrapids2: as before)
					{
						continue;
					}
					WetZ /= float(NWet);
					FVector Q[4];
					float Lo = 1e9f, Hi = -1e9f;
					for (int32 k = 0; k < 4; ++k)
					{
						Q[k] = WVerts[Ws[k]];
						if (!WetV[Vs[k]])
						{
							Q[k].Z = WetZ;
						}
						Lo = FMath::Min(Lo, float(Q[k].Z));
						Hi = FMath::Max(Hi, float(Q[k].Z));
					}
					if (Hi - Lo > 500.f)
					{
						continue;          // (near sheer: a fall's, not a reach's)
					}
					const int32 R0 = RVerts.Num();
					for (int32 k = 0; k < 4; ++k)
					{
						const FVector Pw = Q[k] + FVector(0.f, 0.f, 8.f);
						RVerts.Add(Pw);
						RNormals.Add(FVector::UpVector);
						RUV.Add(FVector2D(float(Pw.X + Pw.Y) / 400.f, -float(Pw.Z) / 200.f));
					}
					RTris.Append({ R0, R0 + 2, R0 + 1, R0 + 1, R0 + 2, R0 + 3 });
					continue;
				}
				WTris.Append({ A, C, B, B, C, D });
			}
		}
	}
	UProceduralMeshComponent* Tile = NewObject<UProceduralMeshComponent>(this);
	Tile->bUseAsyncCooking = true;
	Tile->SetupAttachment(GetRootComponent());
	Tile->RegisterComponent();
	Tile->CreateMeshSection(0, Verts, Tris, Normals, {}, Colours, {}, true);
	Tile->SetMaterial(0, GroundMat);
	const int32 PerSide = (N - 1) / ChunkCells;
	if (GroundTile.Num() != PerSide * PerSide)
	{
		GroundTile.Init(INDEX_NONE, PerSide * PerSide);
	}
	GroundTile[CY * PerSide + CX] = Tiles.Num();
	Tiles.Add(Tile);
	if (WTris.Num() > 0)
	{
		// Its own component: water lets the sun through to its bed (no shadow), and nothing stands on it.
		UProceduralMeshComponent* Water = NewObject<UProceduralMeshComponent>(this);
		Water->SetupAttachment(GetRootComponent());
		Water->SetCastShadow(false);
		Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Water->RegisterComponent();
		Water->CreateMeshSection(0, WVerts, WTris, WNormals, {}, {}, {}, false);
		Water->SetMaterial(0, FreshMat);
		Tiles.Add(Water);
	}
	if (RTris.Num() > 0)
	{
		static UMaterialInterface* WhiteMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeFall.M_BoneForgeFall"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		UProceduralMeshComponent* Rapids = NewObject<UProceduralMeshComponent>(this);
		Rapids->SetupAttachment(GetRootComponent());
		Rapids->SetCastShadow(false);
		Rapids->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rapids->RegisterComponent();
		Rapids->CreateMeshSection(0, RVerts, RTris, RNormals, RUV, {}, {}, false);
		Rapids->SetMaterial(0, WhiteMat ? WhiteMat : FreshMat.Get());
		Tiles.Add(Rapids);
	}
}

void ABFWorld::BuildSea()
{
	// One axis of the grid: 2 m steps 128 m each way from its middle, then rings of 32 steps each twice as long as the
	// last (4 m to 512 m), out to 32 km. Every ring's edge is a whole number of its own steps, so moved by 32 m the grid's
	// points within the waved part (to 1.5 km, steps up to 32 m) fall on the same places.
	TArray<float> Half = { 0.f };
	float At = 0.f;
	for (int32 i = 0; i < 64; ++i)
	{
		Half.Add(At += 200.f);
	}
	for (int32 Ring = 0; Ring < 8; ++Ring)
	{
		for (int32 i = 0; i < 32; ++i)
		{
			Half.Add(At += 400.f * float(1 << Ring));
		}
	}
	TArray<float> Axis;
	for (int32 i = Half.Num() - 1; i > 0; --i)
	{
		Axis.Add(-Half[i]);
	}
	Axis.Append(Half);
	const int32 A = Axis.Num();
	TArray<FVector> V;
	TArray<FVector> Nrm;
	TArray<int32> T;
	V.Reserve(A * A);
	for (int32 y = 0; y < A; ++y)
	{
		for (int32 x = 0; x < A; ++x)
		{
			V.Add(FVector(Axis[x], Axis[y], 0.0));
		}
	}
	Nrm.Init(FVector::UpVector, A * A);
	T.Reserve((A - 1) * (A - 1) * 6);
	for (int32 y = 0; y < A - 1; ++y)
	{
		for (int32 x = 0; x < A - 1; ++x)
		{
			const int32 P = y * A + x;
			T.Append({ P, P + A, P + 1, P + 1, P + A, P + A + 1 });
		}
	}
	SeaSurface = NewObject<UProceduralMeshComponent>(this);
	SeaSurface->SetupAttachment(GetRootComponent());
	SeaSurface->SetCastShadow(false);
	SeaSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SeaSurface->RegisterComponent();
	SeaSurface->CreateMeshSection(0, V, T, Nrm, {}, {}, {}, false);
	SeaSurface->SetMaterial(0, WaterMat);
	SeaSurface->SetWorldLocation(FVector(GetCentre().X, GetCentre().Y, Origin.Z));
	Tiles.Add(SeaSurface);
}

float ABFWorld::Bilinear(const TArray<float>& Arr, float GX, float GY) const
{
	GX = FMath::Clamp(GX, 0.f, float(N - 1) - 0.001f);
	GY = FMath::Clamp(GY, 0.f, float(N - 1) - 0.001f);
	const int32 X = FMath::FloorToInt(GX), Y = FMath::FloorToInt(GY);
	const float FX = GX - X, FY = GY - Y;
	return FMath::Lerp(FMath::Lerp(Arr[Idx(X, Y)], Arr[Idx(X + 1, Y)], FX), FMath::Lerp(Arr[Idx(X, Y + 1)], Arr[Idx(X + 1, Y + 1)], FX), FY);
}

float ABFWorld::SeaWaveM(const FVector2D& XY, float T) const
{
	// The water material's waves (bone_forge_materials.py SEA_COMMON / SEA_WPO_HLSL), the up-and-down part: six long
	// Gerstner waves round the wind, calm where it's shallow; the rollers running in to the shore.
	const float GX = float((XY.X - Origin.X) / CellCm), GY = float((XY.Y - Origin.Y) / CellCm);
	const bool bOn = bLandDone && GX >= 0.f && GY >= 0.f && GX <= N - 1 && GY <= N - 1;
	const float Depth = bOn ? FMath::Clamp(-HeightM(GX, GY), 0.f, 40.f) : 40.f;
	const float Shore = bOn ? FMath::Min(Bilinear(ShoreM, GX, GY), 127.5f) : 127.5f;
	const float Amp = FMath::SmoothStep(0.5f, 9.f, Depth);
	static const float Dirs[6] = { 35.f, 10.f, 58.f, 25.f, 80.f, -5.f };
	static const float Lens[6] = { 48.f, 33.f, 23.f, 15.5f, 9.7f, 6.3f };
	const FVector2D P = (XY - FVector2D(Origin.X, Origin.Y)) / 100.0;
	float Z = 0.f;
	for (int32 i = 0; i < 6; ++i)
	{
		const float Ang = FMath::DegreesToRadians(Dirs[i]);
		const float K = 2.f * PI / Lens[i];
		const float Ph = K * float(FMath::Cos(Ang) * P.X + FMath::Sin(Ang) * P.Y) - FMath::Sqrt(9.81f * K) * T + i * 1.7f;
		Z += Amp * Lens[i] / 115.f * FMath::Sin(Ph);
	}
	const float Band = FMath::Frac(Shore / 13.f + T * 0.16f);
	if (Depth > 0.8f)
	{
		Z += FMath::Pow(FMath::Max(FMath::Sin(Band * 2.f * PI), 0.f), 3.f) * 0.22f * FMath::SmoothStep(1.f, 6.f, Shore) * (1.f - FMath::SmoothStep(20.f, 45.f, Shore));
	}
	return Z;
}

FVector ABFWorld::CurrentAt(const FVector& At) const
{
	const float GX = float((At.X - Origin.X) / CellCm), GY = float((At.Y - Origin.Y) / CellCm);
	if (!bLandDone || GX < 0.f || GY < 0.f || GX > N - 1 || GY > N - 1)
	{
		return FVector::ZeroVector;
	}
	const int32 X = FMath::Min(FMath::FloorToInt(GX), N - 2), Y = FMath::Min(FMath::FloorToInt(GY), N - 2);
	const float FX = GX - X, FY = GY - Y;
	const FVector2f F = FMath::Lerp(FMath::Lerp(Flow[Idx(X, Y)], Flow[Idx(X + 1, Y)], FX), FMath::Lerp(Flow[Idx(X, Y + 1)], Flow[Idx(X + 1, Y + 1)], FX), FY);
	return FVector(F.X, F.Y, 0.f) * 100.f;
}

// ---------------------------------------------------------------------------------------------- rock pieces (caves)

bool ABFWorld::FindCliff(const FVector& Near, float MaxCm, float MinZ, FRandomStream& R, FVector& Top, FVector& Down) const
{
	for (int32 k = 0; k < 4000; ++k)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		const float Dist = R.FRandRange(0.f, MaxCm);
		const FVector2D P(Near.X + FMath::Cos(Ang) * Dist, Near.Y + FMath::Sin(Ang) * Dist);
		float G = 0.f, GX0 = 0.f, GX1 = 0.f, GY0 = 0.f, GY1 = 0.f, Wz = 0.f;
		if (!GroundAt(P, G) || G < MinZ || WaterAt(FVector(P.X, P.Y, G + 10.f), Wz)
			|| !GroundAt(P + FVector2D(300.f, 0.f), GX1) || !GroundAt(P - FVector2D(300.f, 0.f), GX0)
			|| !GroundAt(P + FVector2D(0.f, 300.f), GY1) || !GroundAt(P - FVector2D(0.f, 300.f), GY0))
		{
			continue;
		}
		const FVector2D Grad((GX1 - GX0) / 600.f, (GY1 - GY0) / 600.f);
		if (FMath::RadiansToDegrees(FMath::Atan(Grad.Size())) < 35.f)
		{
			continue;
		}
		const FVector Dn = FVector(-Grad.X, -Grad.Y, 0.f).GetSafeNormal();
		float G10 = 0.f, G20 = 0.f, W10 = 0.f;
		if (!GroundAt(P + FVector2D(Dn) * 1000.f, G10) || !GroundAt(P + FVector2D(Dn) * 2000.f, G20) || G - G10 < 900.f || G - G20 < 1500.f
			|| WaterAt(FVector(P.X + Dn.X * 1000.f, P.Y + Dn.Y * 1000.f, G10 + 10.f), W10))
		{
			continue;
		}
		Top = FVector(P.X, P.Y, G);
		Down = Dn;
		return true;
	}
	return false;
}

float ABFWorld::CaveFloorK(float Squash)
{
	// How far under a path point its flat floor is, a share of its radius: as the dens always had it (0.55) for a round
	// passage; for a squashed chamber, inside its rounded bottom (so its floor is flat, not a bowl).
	return Squash <= 1.001f ? 0.55f : FMath::Min(0.55f, 0.85f / (1.6f * Squash));
}

float ABFWorld::CaveDensity(const FBFCave& C, const FVector& P) const
{
	// Positive in rock, negative in air (cm). The hill's own surface (the same heights the tiles are made of); rough rock
	// in from the piece's edge (none at the edge, where it sinks a little under the island's ground, which hides the
	// seam); minus the cave: round each stretch of the path a tunnel, lumpy, its lower half squashed, its floor flat.
	const float GX = float((P.X - Origin.X) / CellCm), GY = float((P.Y - Origin.Y) / CellCm);
	float Solid = float(Origin.Z) + 100.f * HeightM(FMath::Clamp(GX, 0.f, float(N - 1)), FMath::Clamp(GY, 0.f, float(N - 1))) - float(P.Z);
	const float Edge = float(FMath::Min(FMath::Min(P.X - C.Box.Min.X, C.Box.Max.X - P.X), FMath::Min(P.Y - C.Box.Min.Y, C.Box.Max.Y - P.Y)));
	// (No roughness near the mouth either: lumps there left a step up onto the cave's floor that animals couldn't climb.)
	const float Wt = FMath::Clamp((Edge - 1.5f * CellCm) / 800.f, 0.f, 1.f) * FMath::Clamp((float(FVector::Dist(P, C.Path[0])) - 500.f) / 800.f, 0.f, 1.f);
	// A den's knoll (Raven's look at the caves, 2026-11-15: the mouths sat out on gentle slopes like stone tables, the
	// tunnel breaking the hillside open behind them). The rock the tunnel runs into: over the whole way in, its top at
	// least 2 m over the tunnel's roof; a steep face at the mouth; its sides falling back to the hill well inside the
	// piece's edge. Lumpy, so it reads as an outcrop, not a mound.
	// (Deep caves and caverns, 2026-11-15: the same round their mouths, gone 15-25 m in; their passages and halls are
	// where the mountain is already over them.)
	// The hollow behind a fall (kind 4, 2026-11-15): the rock face round it rough in three dimensions, as the 4 m ground
	// can't be: lumps and ribs up to a metre or so where the hill is steep (none at the piece's edge, none on flat ground).
	if (C.Kind == 4)
	{
		const float GXc = FMath::Clamp(GX, 1.f, float(N - 2)), GYc = FMath::Clamp(GY, 1.f, float(N - 2));
		const float Gx = HeightM(GXc + 0.5f, GYc) - HeightM(GXc - 0.5f, GYc), Gy = HeightM(GXc, GYc + 0.5f) - HeightM(GXc, GYc - 0.5f);
		const float Sl = FMath::Sqrt(Gx * Gx + Gy * Gy) / (CellCm / 100.f);
		const float We = FMath::Clamp((Edge - 1.5f * CellCm) / 600.f, 0.f, 1.f) * FMath::SmoothStep(0.5f, 1.2f, Sl);
		const FVector Pr = FVector(P) + FVector(0.f, 0.f, 0.f);
		// (finer lumps, a metre across, left slivers the 50 cm grid tore into dark flecks)
		Solid += We * (100.f * FMath::PerlinNoise3D(Pr / 300.f + FVector(3.f, 8.f, 1.f)) + 40.f * FMath::PerlinNoise3D(Pr / 190.f + FVector(7.f, 2.f, 5.f))
			+ 35.f * FMath::PerlinNoise3D(FVector(Pr.X / 400.f, Pr.Y / 400.f, Pr.Z / 140.f) + FVector(1.f, 4.f, 9.f)));          // (the last flattened up and down: beds of rock)
	}
	// (2026-11-17, Raven: "the caves look weird like protrusions out of the land, the entrance should be mostly inline
	// with the terrain and the cave inside the mesh". The knoll is off: a cave is only made where the hill already covers
	// it (AddCave). -bfknoll: as before.)
	static const bool bKnoll = FParse::Param(FCommandLine::Get(), TEXT("bfknoll"));
	if (bKnoll && C.Kind != 1 && C.Kind != 4)
	{
		float S2 = 1e9f, Zc = 0.f, Rc = 0.f, MaxRad = 0.f;
		for (const float Rr : C.Radii)
		{
			MaxRad = FMath::Max(MaxRad, Rr);
		}
		for (int32 i = 0; i + 1 < C.Path.Num(); ++i)
		{
			const FVector2D A(C.Path[i]), B(C.Path[i + 1]), Q2(P);
			const FVector2D AB = B - A;
			const float T = FMath::Clamp(float(FVector2D::DotProduct(Q2 - A, AB) / FMath::Max(AB.SizeSquared(), 1.0)), 0.f, 1.f);
			const float Dd = float(FVector2D::Distance(Q2, A + AB * T));
			if (Dd < S2)
			{
				S2 = Dd;
				Zc = float(FMath::Lerp(C.Path[i].Z, C.Path[i + 1].Z, T));
				Rc = FMath::Lerp(C.Radii[i], C.Radii[i + 1], T);
			}
		}
		const float Along = float(FVector::DotProduct(P - C.Path[0], -C.Out));
		const float Lump = 70.f * FMath::PerlinNoise3D(FVector(P) / 330.f + FVector(5.f, 11.f, 2.f));
		const float Wa = FMath::SmoothStep(-140.f, 40.f, Along + 0.5f * Lump) * (C.Kind == 0 ? 1.f : 1.f - FMath::SmoothStep(1500.f, 2500.f, Along));
		// (Not a flat shelf from the side: its back rising into the hill, its crest rounded off to the sides.)
		const float Ws = 1.f - FMath::SmoothStep(Rc + 80.f, (C.Kind == 0 ? MaxRad : Rc) + 450.f, S2 + Lump);
		const float Top = Zc + Rc + 270.f + 0.22f * FMath::Max(Along, 0.f) - 0.5f * FMath::Max(S2 - Rc, 0.f)
			+ 1.6f * Lump + 60.f * FMath::PerlinNoise3D(FVector(P) / 150.f + FVector(1.f, 7.f, 3.f));
		const float Hill = float(Origin.Z) + 100.f * HeightM(FMath::Clamp(GX, 0.f, float(N - 1)), FMath::Clamp(GY, 0.f, float(N - 1)));
		if (Top > Hill && Wa * Ws > 0.f)
		{
			Solid = FMath::Max(Solid, FMath::Lerp(Hill - 60.f, Top, Wa * Ws) - float(P.Z));
		}
	}
	// (2026-11-17, Raven: "little bites taken out around the top" of a mouth flush with the hill: over the opening the
	// hill's surface met the tunnel's roof in a wedge thinner than the 50 cm grid, which tore into notches. A low brow
	// over the mouth, the hill raised up to 80 cm in a smooth swell, thickens the wedge. Capping the tunnel under the
	// surface instead sealed the mouth.)
	if (C.Kind != 1 && C.Kind != 4 && !bKnoll)
	{
		const float R0 = C.Radii[0];
		const FVector BrowAt = C.Path[0] - C.Out * (0.6f * R0) + FVector(0.f, 0.f, R0);
		const float Db = float(FVector::Dist(P, BrowAt)) / (1.7f * R0);
		if (Db < 1.f && float(P.Z) > float(C.Path[0].Z))
		{
			Solid += 80.f * FMath::Square(1.f - Db * Db) * FMath::Clamp((float(P.Z) - float(C.Path[0].Z)) / (0.5f * R0), 0.f, 1.f);
		}
	}
	float Air = -1e9f;
	for (int32 i = 0; i + 1 < C.Path.Num(); ++i)
	{
		const FVector A = C.Path[i], B = C.Path[i + 1];
		const FVector AB = B - A;
		const float T = FMath::Clamp(float(FVector::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.0)), 0.f, 1.f);
		const FVector Q = A + AB * T;
		const float Rad = FMath::Lerp(C.Radii[i], C.Radii[i + 1], T);
		const float Sq = FMath::Lerp(C.Squash[i], C.Squash[i + 1], T);
		FVector D = P - Q;
		D.Z *= (D.Z > 0.f ? 0.9f : 1.6f) * Sq;
		// (lumps as big as a tenth of a big hall, no more than 1.6 m)
		// (2026-11-17, Raven: "little bites taken out around the top" of the mouth: the lumps broke through the hill's
		// surface round the rim. Fading in over the first 7 m from the mouth.)
		const float MouthFade = (C.Kind == 4 || FParse::Param(FCommandLine::Get(), TEXT("bfknoll"))) ? 1.f : FMath::Clamp((float(FVector::Dist(P, C.Path[0])) - 150.f) / 550.f, 0.1f, 1.f);
		const float Lumpy = FMath::Clamp(0.12f * Rad, 70.f, 160.f) * MouthFade;
		float Ai = Rad + Lumpy * FMath::PerlinNoise3D(FVector(P) / FMath::Max(260.f, 0.35f * Rad) + FVector(17.f, 3.f, 9.f)) - float(D.Size());
		Ai = FMath::Min(Ai, float(P.Z - (Q.Z - CaveFloorK(Sq) * Rad)));
		Air = FMath::Max(Air, Ai);
	}
	// Skylights: a rough shaft from the roof up through the hill.
	for (const FVector4& S : C.Shafts)
	{
		const FVector A(S.X, S.Y, S.Z), B = A + FVector(0.f, 0.f, 6000.f);
		const float T = FMath::Clamp(float((P.Z - A.Z) / (B.Z - A.Z)), 0.f, 1.f);
		const FVector Q = FMath::Lerp(A, B, T);
		const float Rs = float(S.W) * (1.f + 0.6f * T);
		Air = FMath::Max(Air, Rs + 90.f * FMath::PerlinNoise3D(FVector(P) / 240.f + FVector(4.f, 9.f, 1.f)) - float(FVector::Dist(P, Q)));
	}
	// (2026-11-22, a closer look at every mouth: "scalloped bites" along the top of the rim and loose black specks over
	// it. Surface nets bead a sharp crease, and the rim is one: the hill's surface meeting the tunnel's. A smooth min, a
	// 60 cm fillet, rounds it over; -bfknoll: the hard crease as before.)
	if (!bKnoll && C.Kind != 1 && C.Kind != 4)
	{
		const float A = Solid, B = -Air, K = 60.f;
		const float Hh = FMath::Clamp(0.5f + 0.5f * (B - A) / K, 0.f, 1.f);
		return FMath::Lerp(B, A, Hh) - K * Hh * (1.f - Hh);
	}
	return FMath::Min(Solid, -Air);
}

void ABFWorld::BuildCaveMesh(FBFCave& C)
{
	// Surface nets on a 50 cm grid over the piece's box: a vertex in each cube the surface passes through (the mean of
	// where it crosses the cube's edges), a quad across each crossed grid edge; normals from the rock's slope; the colour
	// the island's ground where it faces up outside, bare rock on the faces, darker in the cave.
	const double T0 = FPlatformTime::Seconds();
	const float Res = C.Res;
	const FVector Size = C.Box.GetSize();
	const int32 NX = FMath::CeilToInt(Size.X / Res) + 1, NY = FMath::CeilToInt(Size.Y / Res) + 1, NZ = FMath::CeilToInt(Size.Z / Res) + 1;
	auto I3 = [&](int32 x, int32 y, int32 z) { return (z * NY + y) * NX + x; };
	TArray<float> S;
	S.SetNumUninitialized(NX * NY * NZ);
	ParallelFor(NZ, [&](int32 z)
	{
		for (int32 y = 0; y < NY; ++y)
		{
			for (int32 x = 0; x < NX; ++x)
			{
				S[I3(x, y, z)] = CaveDensity(C, C.Box.Min + FVector(x, y, z) * Res);
			}
		}
	});
	TArray<int32> CellV;
	CellV.Init(INDEX_NONE, (NX - 1) * (NY - 1) * (NZ - 1));
	auto C3 = [&](int32 x, int32 y, int32 z) { return (z * (NY - 1) + y) * (NX - 1) + x; };
	TArray<FVector> Verts, Normals;
	TArray<FColor> Colours;
	TArray<int32> Tris;
	static const int32 E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	for (int32 z = 0; z < NZ - 1; ++z)
	{
		for (int32 y = 0; y < NY - 1; ++y)
		{
			for (int32 x = 0; x < NX - 1; ++x)
			{
				float Cn[8];
				int32 Solid = 0;
				for (int32 k = 0; k < 8; ++k)
				{
					Cn[k] = S[I3(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1))];
					Solid += Cn[k] > 0.f ? 1 : 0;
				}
				if (Solid == 0 || Solid == 8)
				{
					continue;
				}
				FVector Sum = FVector::ZeroVector;
				int32 Cross = 0;
				for (const auto& Ed : E)
				{
					const float A = Cn[Ed[0]], B = Cn[Ed[1]];
					if ((A > 0.f) != (B > 0.f))
					{
						const float T = A / (A - B);
						const FVector PA(Ed[0] & 1, (Ed[0] >> 1) & 1, (Ed[0] >> 2) & 1), PB(Ed[1] & 1, (Ed[1] >> 1) & 1, (Ed[1] >> 2) & 1);
						Sum += FMath::Lerp(PA, PB, T);
						++Cross;
					}
				}
				const FVector P = C.Box.Min + (FVector(x, y, z) + Sum / FMath::Max(Cross, 1)) * Res;
				CellV[C3(x, y, z)] = Verts.Add(P);
				const float Dl = 25.f;
				const FVector Grad(CaveDensity(C, P + FVector(Dl, 0, 0)) - CaveDensity(C, P - FVector(Dl, 0, 0)),
					CaveDensity(C, P + FVector(0, Dl, 0)) - CaveDensity(C, P - FVector(0, Dl, 0)),
					CaveDensity(C, P + FVector(0, 0, Dl)) - CaveDensity(C, P - FVector(0, 0, Dl)));
				FVector Nm = (-Grad).GetSafeNormal();
				// Near the piece's edge, the ground's own normal and colour (as its tiles have them, between their points), so
				// no seam shows where the piece meets the island's ground.
				const float FGX = FMath::Clamp(float((P.X - Origin.X) / CellCm), 1.f, float(N - 2)), FGY = FMath::Clamp(float((P.Y - Origin.Y) / CellCm), 1.f, float(N - 2));
				const int32 BX = FMath::Min(int32(FGX), N - 3), BY = FMath::Min(int32(FGY), N - 3);
				const float FX = FGX - BX, FY = FGY - BY;
				FVector TileN = FVector::ZeroVector;
				FLinearColor TileC(0.f, 0.f, 0.f, 0.f);
				for (int32 k = 0; k < 4; ++k)
				{
					const int32 GX = BX + (k & 1), GY = BY + (k >> 1);
					const float Wk = ((k & 1) ? FX : 1.f - FX) * ((k >> 1) ? FY : 1.f - FY);
					const float DX = H[Idx(GX + 1, GY)] - H[Idx(GX - 1, GY)], DY = H[Idx(GX, GY + 1)] - H[Idx(GX, GY - 1)];
					TileN += Wk * FVector(-DX, -DY, 2.f * CellCm / 100.f).GetSafeNormal();
					TileC += Wk * FLinearColor(Colour[Idx(GX, GY)]);
				}
				const float EdgeD = float(FMath::Min(FMath::Min(P.X - C.Box.Min.X, C.Box.Max.X - P.X), FMath::Min(P.Y - C.Box.Min.Y, C.Box.Max.Y - P.Y)));
				const float Blend = FMath::Clamp((EdgeD - 1.5f * CellCm) / 800.f, 0.f, 1.f);       // 0 at the edge, as the rock's roughness
				float OnSurface = 0.f;
				GroundAt(FVector2D(P), OnSurface);
				const bool bHillSurface = FMath::Abs(OnSurface - float(P.Z)) < 150.f;
				if (bHillSurface)
				{
					Nm = FMath::Lerp(TileN.GetSafeNormal(), Nm, Blend).GetSafeNormal();
				}
				Normals.Add(Nm);
				// In the cave (near its path, under the hill's surface) dark; outside, the ground's colour where it faces up.
				float Ground = 0.f;
				GroundAt(FVector2D(P), Ground);
				const int32 GXi = FMath::Clamp(FMath::RoundToInt((P.X - Origin.X) / CellCm), 0, N - 1), GYi = FMath::Clamp(FMath::RoundToInt((P.Y - Origin.Y) / CellCm), 0, N - 1);
				const FLinearColor Outside = bHillSurface ? TileC : FLinearColor(Colour[Idx(GXi, GYi)]);
				const FLinearColor Rock(FColor(114, 113, 110)), Deep(FColor(52, 47, 42)), Dark(FColor(14, 12, 11));          // (2026-11-17: the rock's cooler grey; was 118,110,100)
				const float Under = FMath::Clamp((Ground - float(P.Z) - 100.f) / 300.f, 0.f, 1.f);         // 1: well under the hill's top
				const float Up = FMath::Clamp((float(Nm.Z) - 0.55f) / 0.3f, 0.f, 1.f);
				FLinearColor Cl = FMath::Lerp(Rock, Outside, bHillSurface ? FMath::Lerp(1.f, Up, Blend) : Up * (1.f - Under));
				Cl = FMath::Lerp(Cl, Deep, Under);
				// Deeper in from the mouth, darker: the daylight doesn't reach the back (the sky light isn't shadowed in caves).
				if (Under > 0.f)
				{
					float Along = 0.f, Best = 1e9f, Run = 0.f;
					for (int32 i = 0; i + 1 < C.Path.Num(); ++i)
					{
						const FVector A = C.Path[i], B = C.Path[i + 1];
						const float T = FMath::Clamp(float(FVector::DotProduct(P - A, B - A) / FMath::Max((B - A).SizeSquared(), 1.0)), 0.f, 1.f);
						const float Dd = float(FVector::Dist(P, A + (B - A) * T));
						if (Dd < Best)
						{
							Best = Dd;
							Along = Run + T * float(FVector::Dist(A, B));
						}
						Run += float(FVector::Dist(A, B));
					}
					// (under a skylight, lit: no darker than at the mouth within a shaft's width or two of it)
					float Lit = 1.f;
					for (const FVector4& Sh : C.Shafts)
					{
						Lit = FMath::Min(Lit, FMath::Clamp((float(FVector::Dist2D(P, FVector(Sh.X, Sh.Y, 0.f))) - 1.5f * float(Sh.W)) / 1500.f, 0.f, 1.f));
					}
					Cl = FMath::Lerp(Cl, Dark, Under * Lit * FMath::Clamp((Along - 300.f) / (C.Kind == 3 ? 3000.f : 1500.f), 0.f, 0.92f));
				}
				Colours.Add(Cl.ToFColor(true));
			}
		}
	}
	// A quad across each grid edge the surface crosses, facing out of the rock (UE's winding: cross(c - a, b - a) points out).
	// (2026-11-15: which way is out is the crossed edge's own, from its rock end to its air end. Judged from the four
	// vertex normals instead, some came out backward in the big caverns, where a normal is the hill's near its surface
	// or bent by the lumps, and those triangles weren't drawn: holes showing the sky.)
	auto Quad = [&](int32 a, int32 b, int32 c, int32 d, const FVector& Out)
	{
		if (a == INDEX_NONE || b == INDEX_NONE || c == INDEX_NONE || d == INDEX_NONE)
		{
			return;
		}
		// (2026-11-15: split across whichever diagonal keeps both halves facing out together; across the other, a folded
		// quad on a rough face had one half turned edge-on or back, a small dark fleck on the rock.)
		auto Facing = [&](int32 i, int32 j, int32 k)
		{
			const FVector& PA = Verts[i];
			return float(FVector::DotProduct(FVector::CrossProduct(Verts[k] - PA, Verts[j] - PA).GetSafeNormal(), Out));
		};
		const float Ac = FMath::Min(FMath::Abs(Facing(a, b, c)), FMath::Abs(Facing(a, c, d))) * (Facing(a, b, c) * Facing(a, c, d) > 0.f ? 1.f : -1.f);
		const float Bd = FMath::Min(FMath::Abs(Facing(a, b, d)), FMath::Abs(Facing(b, c, d))) * (Facing(a, b, d) * Facing(b, c, d) > 0.f ? 1.f : -1.f);
		const bool bOther = Bd > Ac;
		for (const FIntVector& Tr : bOther ? TArray<FIntVector>{ FIntVector(a, b, d), FIntVector(b, c, d) } : TArray<FIntVector>{ FIntVector(a, b, c), FIntVector(a, c, d) })
		{
			const FVector& PA = Verts[Tr.X];
			const FVector& PB = Verts[Tr.Y];
			const FVector& PC = Verts[Tr.Z];
			if (FVector::DotProduct(FVector::CrossProduct(PC - PA, PB - PA), Out) >= 0.f)
			{
				Tris.Append({ Tr.X, Tr.Y, Tr.Z });
			}
			else
			{
				Tris.Append({ Tr.X, Tr.Z, Tr.Y });
			}
		}
	};
	for (int32 z = 1; z < NZ - 1; ++z)
	{
		for (int32 y = 1; y < NY - 1; ++y)
		{
			for (int32 x = 0; x < NX - 1; ++x)
			{
				if ((S[I3(x, y, z)] > 0.f) != (S[I3(x + 1, y, z)] > 0.f))           // an edge along X: the four cubes round it
				{
					Quad(CellV[C3(x, y - 1, z - 1)], CellV[C3(x, y, z - 1)], CellV[C3(x, y, z)], CellV[C3(x, y - 1, z)], FVector(S[I3(x, y, z)] > 0.f ? 1.f : -1.f, 0.f, 0.f));
				}
			}
		}
	}
	for (int32 z = 1; z < NZ - 1; ++z)
	{
		for (int32 y = 0; y < NY - 1; ++y)
		{
			for (int32 x = 1; x < NX - 1; ++x)
			{
				if ((S[I3(x, y, z)] > 0.f) != (S[I3(x, y + 1, z)] > 0.f))
				{
					Quad(CellV[C3(x - 1, y, z - 1)], CellV[C3(x, y, z - 1)], CellV[C3(x, y, z)], CellV[C3(x - 1, y, z)], FVector(0.f, S[I3(x, y, z)] > 0.f ? 1.f : -1.f, 0.f));
				}
			}
		}
	}
	for (int32 z = 0; z < NZ - 1; ++z)
	{
		for (int32 y = 1; y < NY - 1; ++y)
		{
			for (int32 x = 1; x < NX - 1; ++x)
			{
				if ((S[I3(x, y, z)] > 0.f) != (S[I3(x, y, z + 1)] > 0.f))
				{
					Quad(CellV[C3(x - 1, y - 1, z)], CellV[C3(x, y - 1, z)], CellV[C3(x, y, z)], CellV[C3(x - 1, y, z)], FVector(0.f, 0.f, S[I3(x, y, z)] > 0.f ? 1.f : -1.f));
				}
			}
		}
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->bUseAsyncCooking = false;
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->RegisterComponent();
	Mesh->CreateMeshSection(0, Verts, Tris, Normals, {}, Colours, {}, true);
	Mesh->SetMaterial(0, GroundMat);
	Tiles.Add(Mesh);
	C.Verts = Verts.Num();
	C.Tris = Tris.Num() / 3;
	C.Seconds = FPlatformTime::Seconds() - T0;
}

int32 ABFWorld::AddCave(const TArray<FVector>& Path, const TArray<float>& Radii, uint8 Kind, const TArray<float>* Squash,
	const TArray<FVector4>* Shafts, const FString& Name)
{
	if (!bReady || Path.Num() < 2 || Radii.Num() != Path.Num())
	{
		return INDEX_NONE;
	}
	FBFCave C;
	C.Path = Path;
	C.Radii = Radii;
	C.Kind = Kind;
	C.Name = Name;
	C.Out = (Path[0] - Path[1]).GetSafeNormal2D();
	C.Squash.Init(1.f, Path.Num());
	if (Squash && Squash->Num() == Path.Num())
	{
		C.Squash = *Squash;
	}
	// Inside the hill (2026-11-17, Raven: the caves stood up out of the land as lumps): a den, deep cave or cavern only
	// where the hill's own ground is 2 m or more over the tunnel's roof within 12 m of the mouth, and 60 cm or more over
	// it from there on (a cavern's skylights aside); else none here, and the caller tries another cliff.
	if ((Kind == 0 || Kind == 2 || Kind == 3) && !FParse::Param(FCommandLine::Get(), TEXT("bfknoll")))
	{
		float Walked = 0.f, CoveredAt = -1.f, Thinnest = 1e9f;
		for (int32 i = 0; i + 1 < Path.Num(); ++i)
		{
			const float Seg = float(FVector::Dist(Path[i], Path[i + 1]));
			for (float t = 0.f; t < Seg; t += 100.f)
			{
				const float u = t / FMath::Max(Seg, 1.f);
				const FVector P = FMath::Lerp(Path[i], Path[i + 1], u);
				const float Roof = float(P.Z) + 0.9f * FMath::Lerp(Radii[i], Radii[i + 1], u);
				float G = -1e9f;
				GroundAt(FVector2D(P), G);
				const float Cover = G - Roof;
				if (CoveredAt < 0.f && Cover >= 200.f)
				{
					CoveredAt = Walked + t;
				}
				else if (CoveredAt >= 0.f)
				{
					Thinnest = FMath::Min(Thinnest, Cover);
				}
			}
			Walked += Seg;
		}
		const bool bShafts = Shafts && Shafts->Num() > 0;
		if (CoveredAt < 0.f || CoveredAt > 1200.f || (!bShafts && Thinnest < 60.f))          // (7 m and 1 m: 17 refused on island 13, no dens left)
		{
			++CavesRefused;
			UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: cave kind %d refused at %s: 2 m of hill over its roof from %.0f cm in, thinnest after %.0f cm, %.0f cm long"), int32(Kind), *Path[0].ToString(), CoveredAt, Thinnest, Walked);
			return INDEX_NONE;
		}
	}
	// (2026-11-17: finer for dens and deep caves, 35 and 45 cm: at 50 and 60 the slope met a mouth flush with the hill
	// in ragged notches round its top; -bfknoll: as before)
	const bool bOldRes = FParse::Param(FCommandLine::Get(), TEXT("bfknoll"));
	C.Res = Kind == 3 ? 75.f : (Kind == 2 ? (bOldRes ? 60.f : 45.f) : (Kind == 0 && !bOldRes ? 35.f : 50.f));
	for (int32 i = 0; i < Path.Num(); ++i)
	{
		C.Floor.Add(Path[i] - FVector(0.f, 0.f, CaveFloorK(C.Squash[i]) * Radii[i] - 5.f));       // (the flat floor under each point, see CaveDensity)
	}
	if (Shafts)
	{
		for (const FVector4& S : *Shafts)
		{
			C.Shafts.Add(S);
		}
	}
	{
		const FVector P = Path[0] + C.Out * 500.f;
		float G = float(P.Z);
		GroundAt(FVector2D(P), G);
		C.MouthOut = FVector(P.X, P.Y, G);
	}
	// The box: the path and 7 m round it across, on whole ground cells; from under its floor to over the hill's top.
	FBox Flat(ForceInit);
	float MaxR = 0.f, LowZ = 1e9f;
	for (int32 i = 0; i < Path.Num(); ++i)
	{
		Flat += Path[i];
		MaxR = FMath::Max(MaxR, Radii[i]);
		LowZ = FMath::Min(LowZ, float(Path[i].Z) - Radii[i]);
	}
	const float Margin = MaxR + (Kind == 0 ? 1100.f : (Kind == 1 ? 700.f : (Kind == 4 ? 900.f : 1300.f)));          // (a knoll needs room to fall back to the hill)
	const int32 X0 = FMath::FloorToInt((Flat.Min.X - Margin - Origin.X) / CellCm), Y0 = FMath::FloorToInt((Flat.Min.Y - Margin - Origin.Y) / CellCm);
	const int32 X1 = FMath::CeilToInt((Flat.Max.X + Margin - Origin.X) / CellCm), Y1 = FMath::CeilToInt((Flat.Max.Y + Margin - Origin.Y) / CellCm);
	if (X0 < 2 || Y0 < 2 || X1 > N - 3 || Y1 > N - 3)
	{
		return INDEX_NONE;
	}
	float HighZ = -1e9f, LowGround = 1e9f;
	for (int32 y = Y0; y <= Y1; ++y)
	{
		for (int32 x = X0; x <= X1; ++x)
		{
			HighZ = FMath::Max(HighZ, float(Origin.Z) + 100.f * H[Idx(x, y)]);
			LowGround = FMath::Min(LowGround, float(Origin.Z) + 100.f * H[Idx(x, y)]);
		}
	}
	if (Kind != 1 && Kind != 4)
	{
		for (int32 i = 0; i < FMath::Min(Path.Num(), Kind == 0 ? 99 : 2); ++i)
		{
			HighZ = FMath::Max(HighZ, float(Path[i].Z) + Radii[i] + 350.f);          // (the knoll's top)
		}
		for (const FVector4& S : C.Shafts)
		{
			HighZ = FMath::Max(HighZ, float(S.Z) + 200.f);
		}
	}
	C.Box = FBox(Origin + FVector(X0 * CellCm, Y0 * CellCm, FMath::Min(LowZ, LowGround) - Origin.Z - 300.f),
		Origin + FVector(X1 * CellCm, Y1 * CellCm, HighZ - Origin.Z + 300.f));
	C.Hole = FIntRect(X0 + 1, Y0 + 1, X1 - 1, Y1 - 1);       // one cell in: there the piece runs on just under the ground
	BuildCaveMesh(C);
	Caves.Add(C);
	// The tiles under it, made again without the cells it covers.
	const int32 PerSide = (N - 1) / ChunkCells;
	for (int32 cy = C.Hole.Min.Y / ChunkCells; cy <= (C.Hole.Max.Y - 1) / ChunkCells; ++cy)
	{
		for (int32 cx = C.Hole.Min.X / ChunkCells; cx <= (C.Hole.Max.X - 1) / ChunkCells; ++cx)
		{
			const int32 TI = GroundTile.IsValidIndex(cy * PerSide + cx) ? GroundTile[cy * PerSide + cx] : INDEX_NONE;
			if (Tiles.IsValidIndex(TI) && Tiles[TI])
			{
				TArray<FVector> Verts, Normals;
				TArray<int32> Tris;
				TArray<FColor> Colours;
				GroundSection(cx, cy, Verts, Tris, Normals, Colours);
				Tiles[TI]->CreateMeshSection(0, Verts, Tris, Normals, {}, Colours, {}, true);
			}
		}
	}
	// No island tree standing in its mouth; no plant inside it (2026-11-15: ferns grew in the dens, from the old ground).
	ClearTreesNear(FVector2D(Path[0]), MaxR + 800.f);
	for (int32 i = 0; i + 1 < Path.Num(); ++i)
	{
		for (float t = 0.f; t < 1.f; t += 0.2f)
		{
			ClearTreesNear(FVector2D(FMath::Lerp(Path[i], Path[i + 1], t)), FMath::Lerp(Radii[i], Radii[i + 1], t) + 300.f, true);
		}
	}
	// Its mouth in rock: a slab jutting out over it, two leaning stones at its sides, boulders fallen before it.
	if (Kind != 1 && Kind != 4)
	{
		const FVector M0 = Path[0];
		const FVector Out = (Path[0] - Path[1]).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(Out, FVector::UpVector);
		const float R0 = Radii[0];
		const float Sc = FMath::Clamp(R0 / 290.f, 1.f, 2.4f);          // (a cavern's mouth: its rocks as much bigger)
		const float Yaw = Out.Rotation().Yaw;
		FRandomStream Rc(Seed * 31 + 777 + Caves.Num());
		TArray<FBFRock> Rim;
		// Over the mouth a broad rough rock jutting out a little (an overhang); round its sides and top, rocks set into the
		// slope (their backs in the hill), none across the way in.
		// (Raven's list, 29: from the side the lip read as a boulder sat on the hill. Now it's a band of rock running back
		// into the slope, most of it buried, only its front edge out over the mouth; and two outcrops at its sides, set
		// back into the hill beside the way in, so the mouth is framed by rock coming out of the slope.)
		// (2026-11-15: now the knoll is the hill's rock round the mouth, these are set into its face: a lintel slab most
		// of it in the knoll, a metre and a half jutting over the mouth; two tall jambs either side, half in the face.)
		FBFRock Lip;
		Lip.Kind = 0;
		Lip.Seed = Rc.RandRange(0, 65535);
		Lip.Half = FVector(300.f * Sc, R0 * 1.45f, 105.f * Sc);
		Lip.At = M0 - Out * 150.f * Sc + FVector(0.f, 0.f, R0 + 55.f * Sc);
		Lip.Rot = FRotator(Rc.FRandRange(-9.f, -3.f), Yaw, Rc.FRandRange(-6.f, 6.f));
		// (2026-11-17: with no knoll, no lintel or jambs: made for the knoll's face, on the hill's own ground the lintel lay
		// out as a flat slab 6 by 8 m over the mouth; -bfknoll: as before)
		const bool bFrame = FParse::Param(FCommandLine::Get(), TEXT("bfknoll"));
		if (bFrame)
		{
			Rim.Add(Lip);
		}
		for (const float S : { -1.f, 1.f })
		{
			if (!bFrame)
			{
				break;
			}
			FBFRock Jamb;
			Jamb.Kind = 2;
			Jamb.Seed = Rc.RandRange(0, 65535);
			Jamb.Half = FVector(Rc.FRandRange(180.f, 240.f) * Sc, Rc.FRandRange(110.f, 150.f) * Sc, R0 * Rc.FRandRange(0.95f, 1.2f));
			Jamb.At = M0 - Out * 90.f * Sc + Side * S * (R0 + Jamb.Half.Y * 0.8f) + FVector(0.f, 0.f, R0 * 0.2f);
			Jamb.Rot = FRotator(Rc.FRandRange(-6.f, 2.f), Yaw + S * Rc.FRandRange(8.f, 20.f), S * Rc.FRandRange(3.f, 8.f));
			Rim.Add(Jamb);
		}
		for (int32 b = 0; b < 3; ++b)
		{
			FBFRock Bo;
			Bo.Kind = 0;
			Bo.Seed = Rc.RandRange(0, 65535);
			const float Big = Rc.FRandRange(70.f, 150.f) * Sc;
			Bo.Half = FVector(Big * 1.15f, Big, Big * 0.6f);
			const FVector P = M0 + Out * Rc.FRandRange(450.f, 900.f) * Sc + Side * ((b % 2 == 0 ? 1.f : -1.f) * Rc.FRandRange(R0 + 50.f, R0 + 400.f));
			float G = 0.f;
			if (GroundAt(FVector2D(P), G))
			{
				Bo.At = FVector(P.X, P.Y, G + Bo.Half.Z * 0.25f);
				Bo.Rot = FRotator(Rc.FRandRange(-10.f, 10.f), Rc.FRandRange(0.f, 360.f), Rc.FRandRange(-10.f, 10.f));
				Rim.Add(Bo);
			}
		}
		// (2026-11-17, Raven: "add some small rocks around" the mouth: 6-9 stones 0.4-1.2 m, half sunk in the slope round its
		// sides and top, a few fallen out on the ground before it; none across the way in)
		if (!bFrame)
		{
			const int32 Stones = Rc.RandRange(8, 11);
			for (int32 b = 0; b < Stones; ++b)
			{
				FBFRock St;
				St.Kind = 0;
				St.Seed = Rc.RandRange(0, 65535);
				const float Big = Rc.FRandRange(20.f, 60.f) * FMath::Sqrt(Sc);
				St.Half = FVector(Big * Rc.FRandRange(1.f, 1.4f), Big, Big * Rc.FRandRange(0.5f, 0.75f));
				FVector P;
				if (b < Stones - 2)
				{
					// round the sides and top: an arc over the mouth, set a little back into the hill
					const float Ang = (b < 2 ? Rc.FRandRange(-0.4f, 0.4f) : (b % 2 == 0 ? 1.f : -1.f) * Rc.FRandRange(0.6f, 1.45f));          // radians from straight up: two over it, the rest beside
					const float Rr = R0 + Rc.FRandRange(60.f, 260.f);
					P = M0 + Side * (FMath::Sin(Ang) * Rr) - Out * (FMath::Cos(Ang) * Rr * 0.8f + Rc.FRandRange(0.f, 120.f));          // (up the slope is into the hill)
				}
				else
				{
					// fallen out before it, to the sides of the way in
					P = M0 + Out * Rc.FRandRange(200.f, 600.f) + Side * ((b % 2 == 0 ? 1.f : -1.f) * Rc.FRandRange(R0 + 40.f, R0 + 250.f));
				}
				float G = 0.f;
				// (solid rock under it: the hill's heights run on over the mouth, where the cave has cut them away)
				float Gx0 = 0.f, Gx1 = 0.f, Gy0 = 0.f, Gy1 = 0.f;
				GroundAt(FVector2D(P) + FVector2D(120.f, 0.f), Gx1);
				GroundAt(FVector2D(P) - FVector2D(120.f, 0.f), Gx0);
				GroundAt(FVector2D(P) + FVector2D(0.f, 120.f), Gy1);
				GroundAt(FVector2D(P) - FVector2D(0.f, 120.f), Gy0);
				const bool bSteep = FVector2D((Gx1 - Gx0) / 240.f, (Gy1 - Gy0) / 240.f).Size() > 0.9f;          // (2026-11-22: over ~42 deg, stuck to a gorge cavern's cliff face)
				// (and its front edge too over rock: on the rim's lip, two hung half over the opening)
				const FVector Front = P + Out * St.Half.X;
				float Gf = 0.f;
				const bool bLip = GroundAt(FVector2D(Front), Gf) && CaveDensity(Caves.Last(), FVector(Front.X, Front.Y, Gf - 40.f)) <= 0.f;
				if (!bSteep && !bLip && GroundAt(FVector2D(P), G) && CaveDensity(Caves.Last(), FVector(P.X, P.Y, G - 40.f)) > 0.f)
				{
					St.At = FVector(P.X, P.Y, G + St.Half.Z * 0.1f);
					St.Rot = FRotator(Rc.FRandRange(-20.f, 20.f), Rc.FRandRange(0.f, 360.f), Rc.FRandRange(-20.f, 20.f));
					Rim.Add(St);
				}
			}
		}
		AddRocks(Rim);
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: cave %d: %s"), Caves.Num() - 1, *CaveText(Caves.Num() - 1));
	return Caves.Num() - 1;
}

int32 ABFWorld::PerchedWater(float& WorstCm, TArray<FVector>* Worst) const
{
	int32 Count = 0;
	WorstCm = 0.f;
	TArray<TPair<float, int32>> All;
	for (int32 Y = 1; Y < N - 1; ++Y)
	{
		for (int32 X = 1; X < N - 1; ++X)
		{
			const int32 C = Idx(X, Y);
			if (!(W[C] > H[C] + 1.5f) || Biome[C] == uint8(EBFBiome::Sea) || Biome[C] == uint8(EBFBiome::Lake))
			{
				continue;          // (shallow water stepping down with its bed is a cascade, not standing water; lakes are deep)
			}
			float Low = 1e9f;
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 NC = Idx(X + D.X, Y + D.Y);
				if (Biome[NC] != uint8(EBFBiome::Sea))
				{
					Low = FMath::Min(Low, FMath::Max(H[NC], W[NC]));
				}
			}
			const float Up = 100.f * (W[C] - Low);
			if (Up > 100.f)
			{
				++Count;
				WorstCm = FMath::Max(WorstCm, Up);
				All.Add({ Up, C });
			}
		}
	}
	if (Worst)
	{
		All.Sort([](const TPair<float, int32>& L, const TPair<float, int32>& R) { return L.Key > R.Key; });
		for (int32 i = 0; i < FMath::Min(3, All.Num()); ++i)
		{
			const int32 C = All[i].Value;
			Worst->Add(Origin + FVector((C % N) * CellCm, (C / N) * CellCm, W[C] * 100.f));
		}
	}
	return Count;
}

int32 ABFWorld::RiverBumps(float& WorstCm) const
{
	int32 Count = 0;
	WorstCm = 0.f;
	for (int32 Y = 1; Y < N - 1; ++Y)
	{
		for (int32 X = 1; X < N - 1; ++X)
		{
			const int32 C = Idx(X, Y);
			if (Biome[C] != uint8(EBFBiome::River) || !(W[C] > H[C]))
			{
				continue;
			}
			float Sum = 0.f;
			int32 Wet = 0;
			for (int32 k = 0; k < 9; ++k)
			{
				const int32 NC = Idx(X + k % 3 - 1, Y + k / 3 - 1);
				if (NC != C && W[NC] > H[NC] && W[NC] > 0.05f)
				{
					Sum += W[NC];
					++Wet;
				}
			}
			if (Wet >= 3)
			{
				const float Up = 100.f * (W[C] - Sum / Wet);
				WorstCm = FMath::Max(WorstCm, Up);
				Count += Up > 30.f ? 1 : 0;
			}
		}
	}
	return Count;
}

int32 ABFWorld::RiverSlopes(int32 Max, TArray<FVector>& At, TArray<FVector>& Downhill, TArray<float>& DropCm) const
{
	struct FCand { int32 C; FIntPoint D; float Drop; };
	TArray<FCand> Cands;
	auto Wet = [&](int32 C) { return W[C] > 0.05f && W[C] > H[C]; };
	for (int32 Y = 6; Y < N - 6; ++Y)
	{
		for (int32 X = 6; X < N - 6; ++X)
		{
			const int32 C = Idx(X, Y);
			if (Biome[C] != uint8(EBFBiome::River) || !Wet(C))
			{
				continue;
			}
			int32 Around = 0;
			for (int32 k = 0; k < 9; ++k)
			{
				Around += Wet(Idx(X + k % 3 - 1, Y + k / 3 - 1)) ? 1 : 0;
			}
			if (Around < 6)
			{
				continue;
			}
			for (const FIntPoint& D : { FIntPoint(3, 0), FIntPoint(0, 3), FIntPoint(2, 2), FIntPoint(2, -2) })
			{
				const int32 A = Idx(X - D.X, Y - D.Y), B = Idx(X + D.X, Y + D.Y);
				if (!Wet(A) || !Wet(B))
				{
					continue;
				}
				const float Drop = H[A] - H[B];          // its bed's drop across 6 cells (24 m) through it (the same with the water fixed or not)
				if (FMath::Abs(Drop) > 0.5f)
				{
					Cands.Add({ C, Drop > 0.f ? D : FIntPoint(-D.X, -D.Y), FMath::Abs(Drop) * 50.f });
				}
			}
		}
	}
	Cands.Sort([](const FCand& L, const FCand& R) { return L.Drop > R.Drop; });
	for (const FCand& K : Cands)
	{
		const FVector P = Origin + FVector((K.C % N) * CellCm, (K.C / N) * CellCm, W[K.C] * 100.f);
		if (At.Num() >= Max || At.ContainsByPredicate([&](const FVector& O) { return FVector::Dist2D(O, P) < 10000.f; }))
		{
			continue;
		}
		At.Add(P);
		Downhill.Add(FVector(K.D.X, K.D.Y, 0.f).GetSafeNormal());
		DropCm.Add(K.Drop);
	}
	return At.Num();
}

FString ABFWorld::WaterText(const FVector& At) const
{
	const int32 X = FMath::RoundToInt((At.X - Origin.X) / CellCm), Y = FMath::RoundToInt((At.Y - Origin.Y) / CellCm);
	FString T;
	for (int32 dy = -2; dy <= 2; ++dy)
	{
		T += TEXT("\n   ");
		for (int32 dx = -2; dx <= 2; ++dx)
		{
			const int32 C = Idx(FMath::Clamp(X + dx, 0, N - 1), FMath::Clamp(Y + dy, 0, N - 1));
			T += FString::Printf(TEXT(" [g %6.2f w %6.2f %s]"), H[C], W[C], W[C] > H[C] && W[C] > 0.05f ? TEXT("~") : TEXT(" "));
		}
	}
	return T;
}

bool ABFWorld::SteepRiver(const FVector& Near, float RadiusCm, FVector& At, FVector& Downhill) const
{
	float Best = 0.f;
	const int32 CX = FMath::RoundToInt((Near.X - Origin.X) / CellCm), CY = FMath::RoundToInt((Near.Y - Origin.Y) / CellCm);
	const int32 R = FMath::CeilToInt(RadiusCm / CellCm);
	for (int32 Y = FMath::Max(CY - R, 4); Y <= FMath::Min(CY + R, N - 5); ++Y)
	{
		for (int32 X = FMath::Max(CX - R, 4); X <= FMath::Min(CX + R, N - 5); ++X)
		{
			const int32 C = Idx(X, Y);
			if (Biome[C] != uint8(EBFBiome::River) || !(W[C] > H[C]))
			{
				continue;
			}
			for (const FIntPoint& D : { FIntPoint(3, 0), FIntPoint(-3, 0), FIntPoint(0, 3), FIntPoint(0, -3), FIntPoint(2, 2), FIntPoint(-2, 2), FIntPoint(2, -2), FIntPoint(-2, -2) })
			{
				const int32 NC = Idx(X + D.X, Y + D.Y);
				const float Drop = W[C] - W[NC];
				if (W[NC] > H[NC] && Biome[NC] == uint8(EBFBiome::River) && Drop > Best)
				{
					Best = Drop;
					At = Origin + FVector(X * CellCm, Y * CellCm, W[C] * 100.f);
					Downhill = FVector(D.X, D.Y, 0.f).GetSafeNormal();
				}
			}
		}
	}
	return Best > 0.f;
}

int32 ABFWorld::CaveAt(const FVector& P) const
{
	for (int32 i = 0; i < Caves.Num(); ++i)
	{
		const FBFCave& C = Caves[i];
		if (!C.Box.IsInsideOrOn(P))
		{
			continue;
		}
		float Top = 0.f;
		if (GroundAt(FVector2D(P), Top) && float(P.Z) < Top - 50.f && CaveDensity(C, P) < 0.f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool ABFWorld::CaveFloorAt(const FVector& P, float& Z) const
{
	// Down from just over the point to the first rock.
	const int32 I = CaveAt(P + FVector(0.f, 0.f, 60.f));
	if (I == INDEX_NONE)
	{
		return false;
	}
	for (float Dz = 60.f; Dz > -900.f; Dz -= 15.f)
	{
		if (CaveDensity(Caves[I], P + FVector(0.f, 0.f, Dz)) > 0.f)
		{
			Z = float(P.Z) + Dz;
			return true;
		}
	}
	return false;
}

void ABFWorld::PlaceCaves()
{
	// Up to five dens at the feet of the island's cliffs (none within 150 m of another, nor within 100 m of where you
	// arrive): a tunnel 2.6-3.2 m round going 14-22 m in and up a little to a chamber 4.5-6 m round. The same for a seed.
	FRandomStream R(Seed + 515);
	CavesRefused = 0;
	const FVector Start = StartPoint();
	// (2026-11-15, Raven: not every island needs everything; no two alike. Each island rolls how many dens it wants
	// (1-5), deep caves (0-2) and whether a mountain has a cavern (6 in 10); each gets what its land has room for.)
	FRandomStream Roll(Seed + 9091);
	const int32 WantDens = Roll.RandRange(1, 5), WantDeep = Roll.RandRange(0, 2);
	const bool bWantCavern = Roll.FRand() < 0.6f;
	for (int32 k = 0; k < 200 && IslandDens < WantDens; ++k)          // (2026-11-22: 60, too few once a den needs the hill over it)
	{
		const FVector Near = Origin + FVector(R.FRandRange(0.15f, 0.85f) * SizeCm(), R.FRandRange(0.15f, 0.85f) * SizeCm(), 0.f);
		FVector Top, Dn;
		if (!FindCliff(Near, 40000.f, -1e9f, R, Top, Dn))
		{
			continue;
		}
		if (FVector::Dist2D(Top, Start) < 10000.f || Caves.ContainsByPredicate([&](const FBFCave& C) { return FVector::Dist2D(C.Path[0], Top) < 15000.f; }))
		{
			continue;
		}
		// Its mouth at the cliff's foot (out from the top edge to where the ground is gentle), with gentle ground before it
		// for 25 m (an animal has to walk up to it).
		auto SlopeDeg = [&](const FVector& P)
		{
			float X0 = 0.f, X1 = 0.f, Y0 = 0.f, Y1 = 0.f;
			GroundAt(FVector2D(P) + FVector2D(300.f, 0.f), X1);
			GroundAt(FVector2D(P) - FVector2D(300.f, 0.f), X0);
			GroundAt(FVector2D(P) + FVector2D(0.f, 300.f), Y1);
			GroundAt(FVector2D(P) - FVector2D(0.f, 300.f), Y0);
			return FMath::RadiansToDegrees(FMath::Atan(FVector2D((X1 - X0) / 600.f, (Y1 - Y0) / 600.f).Size()));
		};
		float FootD = -1.f;
		for (float d = 600.f; d < 4000.f && FootD < 0.f; d += 100.f)
		{
			FootD = SlopeDeg(Top + Dn * d) < 20.f ? d : -1.f;
		}
		bool bGentle = FootD > 0.f;
		for (float d = 500.f; d <= 2500.f && bGentle; d += 500.f)
		{
			float Wz = 0.f, Gz = 0.f;
			const FVector P = Top + Dn * (FootD + d);
			bGentle = SlopeDeg(P) < 25.f && GroundAt(FVector2D(P), Gz) && !WaterAt(FVector(P.X, P.Y, Gz + 10.f), Wz);
		}
		if (!bGentle)
		{
			continue;
		}
		float GFoot = 0.f;
		GroundAt(FVector2D(Top + Dn * FootD), GFoot);
		const float R0 = R.FRandRange(260.f, 320.f), Deep = R.FRandRange(1400.f, 2200.f), Room = R.FRandRange(450.f, 600.f);
		const FVector In = -Dn;
		const FVector M0 = FVector(Top.X, Top.Y, 0.f) + Dn * (FootD - 150.f) + FVector(0.f, 0.f, GFoot + 30.f + 0.55f * R0);
		// (2026-11-17: with no knoll the hill has to cover it: refused, it tries again shorter and level, at 70% and 50%)
		// (and its mouth further up the slope, 4, 8 or 12 m in, its floor on the ground there: at a gentle foot the hill only
		// covered the tunnel 15 m in)
		bool bMade = false;
		for (int32 Try = 0; Try < 12 && !bMade; ++Try)
		{
			const float Shift = 400.f * float(Try / 3);
			FVector Ms = M0 + In * Shift;
			if (Shift > 0.f)
			{
				float Gs = 0.f;
				GroundAt(FVector2D(Ms), Gs);
				Ms.Z = Gs + 30.f + 0.55f * R0;
			}
			const int32 Len = Try % 3;
			const float Dp = Deep * (Len == 0 ? 1.f : (Len == 1 ? 0.7f : 0.5f)), Rise = Len == 0 ? 1.f : 0.f;
			const FVector Mm = Ms;
			const TArray<FVector> Path = { Mm, Mm + In * (0.4f * Dp) + FVector(0.f, 0.f, 40.f * Rise), Mm + In * (0.75f * Dp) + FVector(0.f, 0.f, 90.f * Rise), Mm + In * Dp + FVector(0.f, 0.f, 120.f * Rise) };
			if (AddCave(Path, { R0, R0 * 1.05f, R0 * 1.15f, Room }, 0, nullptr, nullptr, FString::Printf(TEXT("Den %d"), IslandDens + 1)) != INDEX_NONE)
			{
				++IslandDens;
				bMade = true;
			}
		}
	}
	PlaceBigCaves(WantDeep, bWantCavern, Roll);
	for (const FBFCave& Cv : Caves)
	{
		if (Cv.Kind != 1 && !Cv.Name.IsEmpty())
		{
			Places.Add({ Cv.Name, Cv.MouthOut, Cv.Kind });
		}
	}
	CaveSummary = FString::Printf(TEXT("%d dens (wanted %d), %d deep caves (wanted %d), %d caverns (%s)"), IslandDens, WantDens,
		Caves.FilterByPredicate([](const FBFCave& Cv) { return Cv.Kind == 2; }).Num(), WantDeep,
		Caves.FilterByPredicate([](const FBFCave& Cv) { return Cv.Kind == 3; }).Num(), bWantCavern ? TEXT("wanted one") : TEXT("none wanted"));
	CaveSummary += FString::Printf(TEXT(", %d places refused (the hill not over the tunnel)"), CavesRefused);
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] world: caves: %s"), *CaveSummary);
}

bool ABFWorld::NearFallFace(const FVector& P) const
{
	// Where a fall's hollow will be cut (BuildFalls): before its face, by its pool. No rock set on the ground there: the
	// hollow cuts that ground away from under it.
	for (const FBFFall& Fa : Falls)
	{
		// (2026-11-16: nor on its lip: a loose 4 m boulder sat on top of island 13's fall, beside the water)
		if (FVector::Dist2D(P, Fa.Top) < 4500.f + 0.5f * Fa.WidthCm)
		{
			return true;
		}
		if (Fa.PoolCm > 0.f && Fa.DropM >= 8.f && FVector::Dist2D(P, Fa.Bottom) < Fa.PoolCm + 1500.f
			&& FVector::DotProduct((P - Fa.Bottom).GetSafeNormal2D(), Fa.Dir) < 0.35f)
		{
			return true;
		}
	}
	return false;
}

void ABFWorld::DressRavines()
{
	// A ravine's rock (Raven: "large chunks carved out of it"): blocks broken off the walls lying on its floor (1.5-6 m,
	// half sunk, none in the middle third where a stream runs), ledges jutting from the walls at different heights (flat
	// topped slabs set into the wall), and a few fins standing along its rim. Named places: each ravine at its low end.
	FRandomStream R(Seed + 6161);
	int32 Pieces = 0;
	for (int32 v = 0; v < Ravines.Num(); ++v)
	{
		const FBFRavine& Rv = Ravines[v];
		TArray<FBFRock> Set;
		const float Steep = 1.75f;
		for (int32 i = 0; i + 1 < Rv.Path.Num(); ++i)
		{
			const FVector A = Rv.Path[i], B = Rv.Path[i + 1];
			const FVector Along = (B - A).GetSafeNormal2D(), Side = FVector::CrossProduct(Along, FVector::UpVector);
			for (int32 k = 0; k < 2; ++k)
			{
				const float T = R.FRand();
				const FVector P = FMath::Lerp(A, B, T);
				const float Hw = FMath::Lerp(Rv.HalfCm[i], Rv.HalfCm[i + 1], T);
				float G = float(P.Z);
				GroundAt(FVector2D(P), G);
				const float Deep = FMath::Max(G - float(P.Z), 0.f);
				if (R.FRand() < 0.55f)
				{
					// a fallen block, toward one wall
					FBFRock Bo;
					Bo.Seed = R.RandRange(0, 65535);
					const float Big = R.FRandRange(75.f, 300.f);
					Bo.Half = FVector(Big * R.FRandRange(0.9f, 1.4f), Big, Big * R.FRandRange(0.55f, 0.9f));
					const FVector At = P + Side * (R.FRand() < 0.5f ? -1.f : 1.f) * Hw * R.FRandRange(0.35f, 0.95f);
					float Gz = float(At.Z);
					GroundAt(FVector2D(At), Gz);
					Bo.At = FVector(At.X, At.Y, Gz + Bo.Half.Z * 0.3f);
					Bo.Rot = FRotator(R.FRandRange(-20.f, 20.f), R.FRandRange(0.f, 360.f), R.FRandRange(-20.f, 20.f));
					if (!NearFallFace(Bo.At))
					{
						Set.Add(Bo);
					}
				}
				// (2026-11-16: only with -bfoldwalls. The walls' own beds cover every gorge wall now; these older ledges, set
				// on their own and tinted by the ground where they landed, stood out on a fall's face as olive stacks)
				if (Deep > 600.f && FParse::Param(FCommandLine::Get(), TEXT("bfoldwalls")))
				{
					// a ledge in a wall, somewhere up it
					const float S = R.FRand() < 0.5f ? -1.f : 1.f;
					const float Up = R.FRandRange(0.2f, 0.75f) * Deep;
					const float WallAt = Hw + Up / Steep;
					FBFRock L;
					L.Kind = 1;
					L.Seed = R.RandRange(0, 65535);
					L.Half = FVector(R.FRandRange(250.f, 600.f), R.FRandRange(150.f, 260.f), R.FRandRange(60.f, 120.f));
					L.At = P + Side * S * (WallAt + 40.f) + FVector(0.f, 0.f, Up);
					L.Rot = FRotator(R.FRandRange(-4.f, 4.f), Along.Rotation().Yaw + R.FRandRange(-12.f, 12.f), R.FRandRange(-5.f, 5.f));
					L.TopZ = float(L.At.Z) + L.Half.Z * 0.5f;
					{
						const int32 LX = FMath::Clamp(FMath::RoundToInt((L.At.X - Origin.X) / CellCm), 1, N - 2), LY = FMath::Clamp(FMath::RoundToInt((L.At.Y - Origin.Y) / CellCm), 1, N - 2);
						L.Tint = Colour[Idx(LX, LY)];
						L.WallN = FVector(-Side * S * Steep + FVector(0.f, 0.f, 1.f)).GetSafeNormal();
					}
					if (!NearFallFace(L.At))
					{
						Set.Add(L);
					}
				}
			}
			if (R.FRand() < 0.35f)
			{
				// a fin on the rim
				const float S = R.FRand() < 0.5f ? -1.f : 1.f;
				float G = float(A.Z);
				GroundAt(FVector2D(A), G);
				const FVector Rim = A + Side * S * (Rv.HalfCm[i] + FMath::Max(G - float(A.Z), 0.f) / Steep + 250.f);
				float Gr = float(Rim.Z);
				if (GroundAt(FVector2D(Rim), Gr))
				{
					FBFRock Fi;
					Fi.Kind = 2;
					Fi.Seed = R.RandRange(0, 65535);
					Fi.Half = FVector(R.FRandRange(150.f, 300.f), R.FRandRange(80.f, 140.f), R.FRandRange(200.f, 420.f));
					Fi.At = FVector(Rim.X, Rim.Y, Gr + Fi.Half.Z * 0.45f);
					Fi.Rot = FRotator(R.FRandRange(-8.f, 8.f), Along.Rotation().Yaw + R.FRandRange(-20.f, 20.f), R.FRandRange(-8.f, 8.f));
					Set.Add(Fi);
				}
			}
		}
		Pieces += Set.Num();
		AddRocks(Set);
		Places.Add({ Ravines.Num() > 1 ? FString::Printf(TEXT("Ravine %d"), v + 1) : FString(TEXT("The ravine")), Rv.Path.Last(), 10 });
	}
	if (Ravines.Num() > 0)
	{
		LandformText += FString::Printf(TEXT("; %d rocks in the ravines"), Pieces);
	}
	// The walls themselves (Raven, 2026-11-15: "make gorge walls rougher"). On the 4 m ground a wall is smooth faces; real
	// gorge walls are beds of rock, some jutting as ledges, some broken into blocks, with overhangs. So on the steep
	// ground of every gorge (the ravines', and below each fall): rock set into the wall, mostly buried, its front out
	// of the face by a metre or two; flat ledges (strata) along the wall and rough blocks, now and then a run of them.
	// None in a fall's pool or before the hollow behind it, none in a cave's way. Grouped in blocks of ~120 m.
	if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldwalls")))
	{
		FRandomStream Rw(Seed + 8181);
		TMap<int32, TArray<FBFRock>> ByBlock;
		int32 Walls = 0;
		const float CellM = CellCm / 100.f;
		const float BandM = Rw.FRandRange(3.5f, 6.f);          // (the island's rock: a bed every 3.5-6 m of height)
		// (2026-11-16, Raven: the steps "look like man made blocks and not natural". One bed every BandM made courses of
		// even bricks. Real rock is laid down unevenly: thick hard beds that stand out as cliffs, thin soft ones between
		// that weather back. So the island has its own levels, each gap 0.45-1.6 of BandM, the same for every wall.)
		TArray<float> Levels;
		{
			FRandomStream Rl(Seed + 8282);
			for (float Lv = -60.f; Lv < 600.f; Lv += Rl.FRandRange(0.45f, 1.6f) * BandM)
			{
				Levels.Add(Lv);
			}
		}
		FColor RockC(150, 142, 130);          // (the island's bare rock, on average: for a bed with no bare rock near it)
		// (2026-11-16: bare rock is a point that's dry and grey, not green: the Rock biome takes in every slope over 30 deg,
		// grassed ones too, and beds coloured from those came out olive on a fall's grey face)
		auto IsBare = [&](int32 Ci)
		{
			const FColor Cc = Colour[Ci];
			// (and not tan or olive: the wet silt by a river passed "not green", and beds by a fall came out olive)
			return Biome[Ci] == uint8(EBFBiome::Rock) && !(W[Ci] > H[Ci] + 0.05f) && int32(Cc.G) <= int32(Cc.R) + 3 && float(Cc.B) >= 0.8f * float(Cc.R);
		};
		{
			FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
			int32 Num = 0;
			for (int32 C = 0; C < N * N; C += 7)
			{
				if (IsBare(C))
				{
					Sum += FLinearColor(Colour[C]);
					++Num;
				}
			}
			if (Num > 0)
			{
				RockC = (Sum / float(Num)).ToFColor(true);
			}
		}
		for (int32 Y = 3; Y < N - 3 && Walls < 7000; ++Y)
		{
			for (int32 X = 3; X < N - 3 && Walls < 7000; ++X)
			{
				const int32 C = Idx(X, Y);
				// (the gorge and the points along its rim: a fall's face is the step from its rim, never cut, down to the
				// gorge, and was left bare above half way)
				// (and a fall's pool laps its face's foot: there, beds on the face from 3 m over the water up)
				const bool bWet = W[C] > H[C] + 0.05f;
				bool bByFall = false;
				for (const FBFFall& Fa : Falls)
				{
					bByFall = bByFall || FVector::Dist2D(Origin + FVector(X * CellCm, Y * CellCm, 0.f), Fa.Bottom) < Fa.PoolCm + 4000.f;
				}
				bool bByGorge = false;
				for (int32 dy = -2; dy <= 2 && !bByGorge; ++dy)
				{
					for (int32 dx = -2; dx <= 2 && !bByGorge; ++dx)
					{
						bByGorge = Gorge[Idx(X + dx, Y + dy)] != 0;
					}
				}
				const FVector2D Grad((H[Idx(X + 1, Y)] - H[Idx(X - 1, Y)]) / (2.f * CellM), (H[Idx(X, Y + 1)] - H[Idx(X, Y - 1)]) / (2.f * CellM));
				const float Sl = float(Grad.Size());
				// (2026-11-16: and any sheer face, over ~70 deg, wherever it is: the flanks of a fall's head, outside both
				// the gorge and its pool, stood as smooth grey planes 50 m high)
				// (2026-11-16, terrain polish: the mountains were smooth grey domes. On their steeper bare slopes, in patches,
				// outcrops: the same beds, so a mountain shows bands of rock stepping up it here and there)
				// (tried: pale bars lying along the slopes, and 7,600 beds took island 13 from 25 s to 86 s to be ready. Off:
				// -bfoutcrops to see them)
				static const bool bOutcrops = FParse::Param(FCommandLine::Get(), TEXT("bfoutcrops"));
				const bool bOutcrop = bOutcrops && Biome[C] == uint8(EBFBiome::Rock) && Sl >= 0.75f && H[C] > 20.f
					&& FMath::PerlinNoise2D(FVector2D(X, Y) * 0.025f + FVector2D(Seed % 53, 11.f)) > 0.12f;
				if (!(bByGorge || bByFall || bOutcrop || Sl >= 2.75f) || (bWet && !bByFall))
				{
					continue;
				}
				if (Sl < (bOutcrop ? 0.75f : 0.85f))
				{
					continue;          // (under 40 deg: not a wall; an outcrop from 37)
				}
				// (into the wall: the wall's way over 20 m, not this point's. A sheer face on the 4 m ground is a stair of
				// columns, each its own way, and beds turned to each column stood as stacks, column by column)
				FVector2D Wide((H[Idx(FMath::Min(X + 2, N - 1), Y)] - H[Idx(FMath::Max(X - 2, 0), Y)]), (H[Idx(X, FMath::Min(Y + 2, N - 1))] - H[Idx(X, FMath::Max(Y - 2, 0))]));
				for (int32 o = -2; o <= 2; o += 4)
				{
					const int32 Yo = FMath::Clamp(Y + o, 2, N - 3), Xo = FMath::Clamp(X + o, 2, N - 3);
					Wide += FVector2D(H[Idx(X + 2, Yo)] - H[Idx(X - 2, Yo)], H[Idx(Xo, Y + 2)] - H[Idx(Xo, Y - 2)]);
				}
				const FVector2D Up = Wide.Size() > 0.01f ? Wide.GetSafeNormal() : Grad / Sl;           // into the wall
				const FVector2D Along(-Up.Y, Up.X);
				const FVector P0 = Origin + FVector((X + Rw.FRandRange(-0.4f, 0.4f)) * CellCm, (Y + Rw.FRandRange(-0.4f, 0.4f)) * CellCm, 0.f);
				bool bClear = true;
				float MinZ = bWet ? float(Origin.Z) + W[C] * 100.f + 300.f : -1e9f;          // (no bed's top lower than this: a pool, a hollow below it)
				for (const FBFFall& Fa : Falls)
				{
					// (2026-11-16: the walls either side of a fall were left smooth, standing out from the stepped rock round
					// them. Now only the fall's own strip is kept clear: the water's width and the hollow behind it, from
					// just above the lip out across its pool; the walls beside it are beds like the rest.)
					const FVector2D D2 = FVector2D(Fa.Dir).GetSafeNormal();
					const FVector2D Rel = FVector2D(P0 - Fa.Top);
					const float AlongF = float(FVector2D::DotProduct(Rel, D2)), Lat = FMath::Abs(float(FVector2D::CrossProduct(D2, Rel)));
					const float Reach = float(FVector2D::DotProduct(FVector2D(Fa.Bottom - Fa.Top), D2)) + Fa.PoolCm + 200.f;
					// (and a pool keeps only its own shore clear, a hollow the face's foot: the face itself up from them and
					// the walls round the pool are rock beds, as round them)
					const bool bAlong = AlongF > -400.f && AlongF < Reach;
					// (2026-11-16: only the curtain's own width now: 3 m either side left the lip's banks as bare 4 m columns)
					if (bAlong && Lat < 0.5f * Fa.WidthCm + 90.f)
					{
						bClear = false;
					}
					if (FVector::Dist2D(P0, Fa.Bottom) < Fa.PoolCm + 200.f)
					{
						MinZ = FMath::Max(MinZ, float(Fa.Bottom.Z) + 300.f);
					}
					if (bAlong && Fa.DropM >= 8.f && Lat < 1300.f)
					{
						MinZ = FMath::Max(MinZ, float(Fa.Bottom.Z) + 1600.f);
					}
				}
				for (const FBFCave& Cv : Caves)
				{
					if (Cv.Kind != 4 && (FVector::Dist2D(P0, Cv.MouthOut) < 1400.f || Cv.Box.ExpandBy(200.f).IsInsideXY(P0)))
					{
						bClear = false;
					}
				}
				if (!bClear)
				{
					continue;
				}
				float G = 0.f;
				if (!GroundAt(FVector2D(P0), G))
				{
					continue;
				}
				// (2026-11-15, Raven: blend in, "like they're part of the wall and not just thrown in". Scattered one by one
				// they read as stuck on. Now the wall's rock is in beds at set heights across the island (dipping a little),
				// each bed jutting as a ledge where it crosses the wall: neighbouring points' ledges meet, so a bed runs on
				// along the wall as one line, with gaps where it's broken. Mostly buried (60% of its depth), its top where the
				// bed is, in the wall's own colour, its facets shaded part way to the wall's facing. Now and then a block.)
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
				const FVector WallN = FVector(-Up.X * Sl, -Up.Y * Sl, 1.f).GetSafeNormal();
				// (its colour the nearest bare rock's: a wet point's own is the water's edge, a rim point's the grass above,
				// and beds in those came out dark against the face)
				FColor WallC = Colour[C];
				if (!IsBare(C))
				{
					WallC = RockC;
					int32 Best = MAX_int32;
					for (int32 dy = -5; dy <= 5; ++dy)
					{
						for (int32 dx = -5; dx <= 5; ++dx)
						{
							const int32 Cn = Idx(FMath::Clamp(X + dx, 0, N - 1), FMath::Clamp(Y + dy, 0, N - 1));
							if (IsBare(Cn) && dx * dx + dy * dy < Best)
							{
								Best = dx * dx + dy * dy;
								WallC = Colour[Cn];
							}
						}
					}
				}
				const float HereM = (G - float(Origin.Z)) / 100.f;
				const float Dip = 1.5f * FMath::PerlinNoise2D(FVector2D(X, Y) * 0.008f + FVector2D(Seed % 41, 3.f));
				// (half way to the ground a point up the wall and a point down it, each side its own slope: a step in the
				// ground, as at a fall's face, is all on one side of it, and the beds go all the way up it)
				float GUp = G, GDn = G;
				GroundAt(FVector2D(P0) + Up * CellCm, GUp);
				GroundAt(FVector2D(P0) - Up * CellCm, GDn);
				const float SlUp = FMath::Max((GUp - G) / CellCm, 0.3f), SlDn = FMath::Max((G - GDn) / CellCm, 0.3f);
				const float SpanUp = 0.5f * SlUp * CellM, SpanDn = 0.5f * SlDn * CellM;
				for (int32 Li = 1; Li < Levels.Num(); ++Li)
				{
					const float Lv = Levels[Li] + Dip;
					const float GapM = Levels[Li] - Levels[Li - 1];          // (this bed's thickness)
					if (Lv < HereM - SpanDn || Lv > HereM + SpanUp || Rw.FRand() > 0.92f || float(Origin.Z) + Lv * 100.f < MinZ)
					{
						continue;
					}
					const bool bSoft = GapM < 0.75f * BandM;          // (a thin bed: soft, weathered back between the hard ones)
					// (a sheer face, over ~76 deg, as at a big fall: only its hard beds, longer and fewer. Every bed there stood
					// out as stacks of small blocks; a real sheer face is mostly bare rock with a few long ledges across it)
					const bool bSheer = (Lv > HereM ? SlUp : SlDn) >= 4.f;
					if (bSheer && (bSoft || Rw.FRand() < 0.5f))
					{
						continue;
					}
					const float SlHere = Lv > HereM ? SlUp : SlDn;
					const FVector2D Off = Up * ((Lv - HereM) / SlHere * 100.f);          // (to where the wall is at the bed's height)
					// (2026-11-15, Raven: thin lips "look like a bunch of shelves in a kitchen instead of a rock face": expand
					// them down into the ground. As a stepped cliff is (Grand Canyon, sandstone gorges): each bed a block whose
					// front is a sheer face from its top nearly down to the bed below, its top the ledge, its foot and back
					// deep in the ground. Stacked, the slope is rock all the way: sheer faces and ledges, now and then a strip
					// of ground where a face stops short.)
					const float Face = Rw.FRandRange(0.75f, 1.f) * GapM * 100.f;          // (the face's height, cm)
					const float Out = FMath::Clamp(Face / SlHere, 90.f, 380.f) * (bSoft ? 0.6f : 1.f);   // (how far its front stands from the wall at its top)
					const float Back = 180.f;                                                // (and its back, into the wall)
					FBFRock Rk;
					Rk.Kind = 1;
					Rk.Seed = Rw.RandRange(0, 65535);
					Rk.Half = FVector(Rw.FRandRange(380.f, 620.f) * (bSheer ? 2.2f : 1.f), 0.5f * (Out + Back), 0.5f * (GapM * 100.f + 220.f));          // (long: a bed runs on unbroken more than it breaks)
					Rk.Rot = FRotator(Rw.FRandRange(-1.5f, 1.5f), Yaw + Rw.FRandRange(-5.f, 5.f), Rw.FRandRange(-1.5f, 1.5f));
					const FVector2D In = Up * (0.5f * (Back - Out));
					Rk.TopZ = float(Origin.Z) + Lv * 100.f;
					Rk.At = FVector(P0.X + Off.X + In.X, P0.Y + Off.Y + In.Y, Rk.TopZ - float(Rk.Half.Z) + 30.f);
					// (2026-11-16: a long bed set beside a fall's strip reached into it, hanging over the curtain: none whose length
					// crosses the strip, its ends checked as well as its middle)
					{
						const FVector2D Ax = FVector2D(FRotator(0.f, float(Rk.Rot.Yaw), 0.f).Vector());
						bool bInStrip = false;
						for (int32 e = -2; e <= 2 && !bInStrip; ++e)
						{
							const FVector2D Q = FVector2D(Rk.At) + Ax * (0.5f * float(e) * float(Rk.Half.X));
							for (const FBFFall& Fa : Falls)
							{
								const FVector2D D2 = FVector2D(Fa.Dir).GetSafeNormal(), Rel = Q - FVector2D(Fa.Top);
								const float Al = float(FVector2D::DotProduct(Rel, D2)), La = FMath::Abs(float(FVector2D::CrossProduct(D2, Rel)));
								const float Re = float(FVector2D::DotProduct(FVector2D(Fa.Bottom - Fa.Top), D2)) + Fa.PoolCm + 200.f;
								bInStrip = bInStrip || (Al > -400.f && Al < Re && La < 0.5f * Fa.WidthCm + 90.f && Rk.TopZ > float(Fa.Bottom.Z));
							}
						}
						if (bInStrip)
						{
							continue;
						}
					}
					Rk.Tint = WallC;
					Rk.WallN = WallN;
					Rk.bBed = true;
					ByBlock.FindOrAdd((Y / 30) * 1000 + X / 30).Add(Rk);
					++Walls;
				}
				bool bByLip = false;          // (2026-11-16: no loose block at a fall's lip: one sat on island 13's as a 4 m boulder)
				for (const FBFFall& Fa : Falls)
				{
					bByLip = bByLip || FVector::Dist2D(P0, Fa.Top) < 2500.f;
				}
				if (Rw.FRand() < 0.1f && G > MinZ && !bByLip)
				{
					// a block broken from the wall, still mostly in it
					FBFRock Rk;
					Rk.Kind = 0;
					Rk.Seed = Rw.RandRange(0, 65535);
					const float Big = Rw.FRandRange(110.f, 220.f);
					Rk.Half = FVector(Big * Rw.FRandRange(1.f, 1.5f), Big * Rw.FRandRange(0.8f, 1.1f), Big * Rw.FRandRange(0.7f, 1.f));
					Rk.Rot = FRotator(Rw.FRandRange(-8.f, 8.f), Yaw + Rw.FRandRange(-15.f, 15.f), Rw.FRandRange(-8.f, 8.f));
					const FVector2D In = Up * (0.6f * float(Rk.Half.Y));
					Rk.At = FVector(P0.X + In.X, P0.Y + In.Y, G);
					Rk.TopZ = float(Rk.At.Z) + float(Rk.Half.Z) * 0.5f;
					Rk.Tint = WallC;
					Rk.WallN = WallN;
					ByBlock.FindOrAdd((Y / 30) * 1000 + X / 30).Add(Rk);
					++Walls;
				}
			}
		}
		for (auto& It : ByBlock)
		{
			for (const FBFRock& Rk : It.Value)
			{
				ClearTreesNear(FVector2D(Rk.At), float(FMath::Max(Rk.Half.X, Rk.Half.Y)) + 100.f, true);
			}
			AddRocks(It.Value);
		}
		LandformText += FString::Printf(TEXT("; %d rocks in gorge walls (rock colour %d,%d,%d)"), Walls, RockC.R, RockC.G, RockC.B);
	}
}

void ABFWorld::DressRivers()
{
	// Boulders in the steep reaches (Montgomery & Buffington's mountain channels: over ~6.5% a cascade of boulders,
	// 3-6.5% steps of boulders across the bed with pools between; gentler, gravel riffles and pools). Where a river's water
	// falls over 25 cm across a point (6%), boulders in its bed and along its edges, poking out of the water, more and
	// bigger the steeper; a few on gentler reaches by the banks. Grouped in blocks of ~120 m.
	FRandomStream R(Seed + 7373);
	TMap<int32, TArray<FBFRock>> ByBlock;
	int32 Count = 0;
	for (int32 Y = 2; Y < N - 2 && Count < 1400; ++Y)
	{
		for (int32 X = 2; X < N - 2 && Count < 1400; ++X)
		{
			const int32 C = Idx(X, Y);
			if (Biome[C] != uint8(EBFBiome::River) || !(W[C] > H[C] + 0.05f))
			{
				continue;
			}
			float Drop = 0.f;
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 J = Idx(X + D.X, Y + D.Y);
				if (W[J] > H[J] + 0.05f)
				{
					Drop = FMath::Max(Drop, W[C] - W[J]);
				}
			}
			if (Drop > 2.5f)
			{
				continue;          // a fall: its own rocks
			}
			const float Chance = Drop > 0.25f ? FMath::Clamp(0.25f + 0.5f * Drop, 0.25f, 0.8f) : 0.04f;
			if (R.FRand() > Chance)
			{
				continue;
			}
			FBFRock Bo;
			Bo.Seed = R.RandRange(0, 65535);
			const float Big = Drop > 0.25f ? R.FRandRange(45.f, 70.f + 60.f * FMath::Min(Drop, 1.5f)) : R.FRandRange(35.f, 60.f);
			Bo.Half = FVector(Big * R.FRandRange(0.9f, 1.3f), Big, Big * R.FRandRange(0.55f, 0.8f));
			const FVector At = Origin + FVector((X + R.FRandRange(-0.45f, 0.45f)) * CellCm, (Y + R.FRandRange(-0.45f, 0.45f)) * CellCm, 0.f);
			float G = 0.f;
			GroundAt(FVector2D(At), G);
			// (2026-11-17, a closer look: in water deeper than it, set by its top it hung in the water over the bed, island
			// 906's rivers 1-3 m. Now taller to reach the bed where that keeps it a boulder, else none; -bfoldboulders)
			const float WaterZ = float(Origin.Z) + W[C] * 100.f;
			if (!FParse::Param(FCommandLine::Get(), TEXT("bfoldboulders")) && WaterZ - G > 1.7f * Bo.Half.Z)
			{
				const float Need = (WaterZ - G) / 1.7f;
				if (Need > 1.1f * Big)
				{
					continue;
				}
				Bo.Half.Z = Need;
			}
			// (sat on the bed, its top out of the water by a fifth of its height or more)
			Bo.At = FVector(At.X, At.Y, FMath::Max(G + Bo.Half.Z * 0.3f, Origin.Z + W[C] * 100.f - Bo.Half.Z * 0.6f));
			Bo.Rot = FRotator(R.FRandRange(-15.f, 15.f), R.FRandRange(0.f, 360.f), R.FRandRange(-15.f, 15.f));
			if (NearFallFace(Bo.At))
			{
				continue;
			}
			ByBlock.FindOrAdd((Y / 30) * 1000 + X / 30).Add(Bo);
			++Count;
		}
	}
	for (auto& It : ByBlock)
	{
		AddRocks(It.Value);
	}
	LandformText += FString::Printf(TEXT("; %d boulders in the rivers"), Count);
}

void ABFWorld::BuildFalls()
{
	// Waterfalls (Raven: "and maybe waterfalls"): over each drop of 3 m or more a curtain of falling water, a strip from
	// the lip to the pool laid over the water's own steep surface (40 cm off the rock, never into it), with
	// M_BoneForgeFall (white water streaming down). Spray and mist at its foot come from TickFoam. The biggest (and any
	// over 8 m) are named places.
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoneForge/Materials/M_BoneForgeFall.M_BoneForgeFall"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	int32 Biggest = INDEX_NONE, Undercuts = 0;
	for (int32 f = 0; f < Falls.Num(); ++f)
	{
		const FBFFall& Fa = Falls[f];
		if (Biggest == INDEX_NONE || Fa.DropM > Falls[Biggest].DropM)
		{
			Biggest = f;
		}
		const FVector Side = FVector::CrossProduct(Fa.Dir, FVector::UpVector);
		// (2026-11-15: it slid down the rock's slope. Now it leaves the lip as water does, going on out at the river's
		// speed (2-3 m/s) as it falls (x = v t, z = g t^2 / 2), spreading a little; never back into the face below: kept
		// at least 60 cm off the ground. At least 4.5 m wide.)
		const int32 Cols = 8, Rows = 18;
		TArray<FVector> V, Nm;
		TArray<FVector2D> UV;
		TArray<int32> T;
		// (2026-11-16: the river's own width and a little, so it leaves the lip's notch: at 1.6 times, it went in under the banks)
		const float Width = FMath::Max(Fa.WidthCm + 150.f, 450.f);
		const float Speed = FMath::Clamp(150.f + 25.f * Fa.DropM, 200.f, 320.f);          // cm/s off the lip
		// (2026-11-17: down to the pool's water and 40 cm into it. DropM can be short of it (falls merged, levels moved after):
		// island 906's 35 m fall stopped 2.8 m over its pool, island 13's two 1.6 m over; -bfolddrop: DropM as before.)
		const float DropCm = FParse::Param(FCommandLine::Get(), TEXT("bfolddrop")) ? Fa.DropM * 100.f : FMath::Max(Fa.DropM * 100.f, float(Fa.Top.Z - Fa.Bottom.Z) + 52.f);
		const float Fall = FMath::Sqrt(2.f * DropCm / 980.f);                     // seconds
		// (2026-11-16, Raven: "start of the waterfalls not connected": it began 2 m out past the lip, with a gap between it
		// and the river's surface, which ends at the lip's point. Now it begins a little back over the river's own surface
		// and leaves the lip from there.)
		// (and a fall under 5 m isn't reshaped: its water comes down in steps over several points, the surface drawn down
		// them as rapids; a curtain in free fall from its first point lay across the bank beside them. None there.)
		if (Fa.PoolCm <= 0.f)
		{
			continue;
		}
		const FVector Lip = Fa.Top - Fa.Dir * (0.35f * CellCm) + FVector(0.f, 0.f, 12.f);
		// (2026-11-17, Raven, circled from above and from below: it began falling at once, 0.35 of a point back, so by the lip
		// it was 1-2 m down, under the river; where the grid lies across the flow the lip's point juts out as a triangle, which
		// lay on top of it as a green point and was seen from below as the river hanging out over the ledge. Now it runs level
		// with the river until it's past the lip's point, then falls; -bfoldlipdrop: as before.)
		const float TLip = FParse::Param(FCommandLine::Get(), TEXT("bfoldlipdrop")) ? 0.f : 0.4f * CellCm / Speed;          // (to just past the lip's point: at 0.55 it stood out level 2 m, a white shelf seen side on)
		// (and the river's own width at the lip, widening as it falls: 1.5 m wider from the lip, from above it stood out
		// over the banks as a white box wider than the river; -bfoldlipdrop: as before)
		// (and as wide as the water along the lip's line: narrower, the river's surface beside it stopped at the edge as a
		// flat plate with nothing falling from it, island 906's rapids view)
		float WetLat = 0.f;
		{
			const int32 TX = FMath::RoundToInt((Fa.Top.X - Origin.X) / CellCm), TY = FMath::RoundToInt((Fa.Top.Y - Origin.Y) / CellCm);
			const FVector2D D2 = FVector2D(Fa.Dir).GetSafeNormal();
			for (int32 dy = -4; dy <= 4; ++dy)
			{
				for (int32 dx = -4; dx <= 4; ++dx)
				{
					const int32 X = FMath::Clamp(TX + dx, 0, N - 1), Y = FMath::Clamp(TY + dy, 0, N - 1), Cc = Idx(X, Y);
					const FVector2D Rel(float(X - TX), float(Y - TY));
					if (W[Cc] > H[Cc] + 0.05f && Biome[Cc] == uint8(EBFBiome::River) && FMath::Abs(float(FVector2D::DotProduct(Rel, D2))) <= 0.75f && FMath::Abs(W[Cc] * 100.f - float(Fa.Top.Z)) < 60.f)          // (river points only: a lake outlet's whole shore made it a pane 8 m wide)
					{
						WetLat = FMath::Max(WetLat, FMath::Abs(float(FVector2D::CrossProduct(D2, Rel))) * CellCm);
					}
				}
			}
		}
		const float TopW = FParse::Param(FCommandLine::Get(), TEXT("bfoldlipdrop")) ? Width : FMath::Min(Width * 1.2f, FMath::Max3(Fa.WidthCm + 90.f, 380.f, 2.f * WetLat + 150.f));          // (+40, min 320: a corner of the river showed beside it)
		float Len = 0.f;
		FVector Last = Lip;
		float Pushed = 0.f;
		for (int32 r = 0; r <= Rows; ++r)
		{
			const float u0 = float(r) / Rows;
			const float t = (Fall + TLip) * u0 * u0;            // (rows closer together near the lip, where it bends over)
			const float Down = 0.5f * 980.f * FMath::Square(FMath::Max(t - TLip, 0.f));
			const FVector Mid = Lip + Fa.Dir * (Speed * t) - FVector(0.f, 0.f, FMath::Min(Down, DropCm));
			Len += float(FVector::Dist(Mid, Last));
			Last = Mid;
			// (keep it off the face: a whole row out from the lip together, never less than the row above, so its edges stay
			// straight; a point at a time, its edges went out in spikes over the bank. Not over the lip itself, where it
			// lies on the river.)
			if (Down > 120.f && r < Rows)
			{
				for (int32 c = 0; c <= Cols; c += 2)
				{
					const float u = float(c) / Cols - 0.5f;
					FVector P = Mid + Side * u * FMath::Min(Width, Fa.WidthCm + 200.f) * (1.f + 0.35f * u0) + Fa.Dir * Pushed;
					float G = float(P.Z);
					for (int32 k = 0; k < 20 && GroundAt(FVector2D(P), G) && float(P.Z) < G + 60.f; ++k)
					{
						P += Fa.Dir * 50.f;
						Pushed += 50.f;
					}
				}
			}
			for (int32 c = 0; c <= Cols; ++c)
			{
				const float u = float(c) / Cols - 0.5f;
				const FVector P = Mid + Side * u * FMath::Lerp(TopW, Width, FMath::Clamp(Down / 400.f, 0.f, 1.f)) * (1.f + 0.35f * u0) + (Down > 120.f ? Fa.Dir * Pushed : FVector::ZeroVector);
				V.Add(P);
				Nm.Add((-Fa.Dir + FVector(0.f, 0.f, 0.3f)).GetSafeNormal());
				UV.Add(FVector2D(u + 0.5f, Len / 200.f));
			}
		}
		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < Cols; ++c)
			{
				const int32 A = r * (Cols + 1) + c;
				T.Append({ A, A + 1, A + Cols + 1, A + 1, A + Cols + 2, A + Cols + 1 });
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] falls: %d, %.1f m, top width %.0f cm (river %.0f, wet %.0f either side), pushed out %.0f cm off the face"), f, Fa.DropM, TopW, Fa.WidthCm, WetLat, Pushed);
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(this);
		M->SetupAttachment(GetRootComponent());
		M->SetCastShadow(false);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->RegisterComponent();
		M->CreateMeshSection(0, V, T, Nm, UV, {}, {}, false);
		M->SetVisibility(!FParse::Param(FCommandLine::Get(), TEXT("bfnocurtain")));
		if (Mat)
		{
			M->SetMaterial(0, Mat);
		}
		Tiles.Add(M);
		// Its lip a hard ledge jutting over the face (the caprock a fall keeps), and blocks fallen from it round the
		// pool, half in the water.
		if (Fa.PoolCm > 0.f)
		{
			FRandomStream Rr(Seed + f * 37 + 5);
			TArray<FBFRock> Set;
			FBFRock Cap;
			Cap.Kind = 1;
			Cap.Seed = Rr.RandRange(0, 65535);
			// (2026-11-16: across the banks too it stood as a lumpy bar over the lip: now just the water's width and a little)
			Cap.Half = FVector(0.45f * CellCm, 0.5f * FMath::Min(Width, Fa.WidthCm + 100.f) + Rr.FRandRange(40.f, 100.f), Rr.FRandRange(70.f, 110.f));
			const FVector OldLip = Fa.Top + Fa.Dir * (0.5f * CellCm) + FVector(0.f, 0.f, 15.f);          // (where it was set)
			Cap.At = OldLip + Fa.Dir * (Rr.FRandRange(60.f, 140.f) - Cap.Half.X) - FVector(0.f, 0.f, 15.f + 75.f + Cap.Half.Z);
			Cap.Rot = FRotator(Rr.FRandRange(-3.f, 3.f), Fa.Dir.Rotation().Yaw, Rr.FRandRange(-3.f, 3.f));
			Cap.TopZ = float(Cap.At.Z) + Cap.Half.Z;
			{
				// (the lip's own rock colour: a pale block on the bigger falls stood out)
				const int32 TX = FMath::Clamp(FMath::RoundToInt((Fa.Top.X - Origin.X) / CellCm), 1, N - 2), TY = FMath::Clamp(FMath::RoundToInt((Fa.Top.Y - Origin.Y) / CellCm), 1, N - 2);
				// (2026-11-16: not the ground 3 points aside, which by a river is wet tan silt: the nearest dry grey rock)
				Cap.Tint = FColor(124, 118, 108, 255);
				int32 Best = MAX_int32;
				for (int32 dy = -8; dy <= 8; ++dy)
				{
					for (int32 dx = -8; dx <= 8; ++dx)
					{
						const int32 Cn = Idx(FMath::Clamp(TX + dx, 0, N - 1), FMath::Clamp(TY + dy, 0, N - 1));
						const FColor Cc = Colour[Cn];
						if (Biome[Cn] == uint8(EBFBiome::Rock) && !(W[Cn] > H[Cn] + 0.05f) && int32(Cc.G) <= int32(Cc.R) + 3 && float(Cc.B) >= 0.8f * float(Cc.R) && dx * dx + dy * dy < Best)
						{
							Best = dx * dx + dy * dy;
							Cap.Tint = Cc;
						}
					}
				}
				Cap.WallN = (Fa.Dir + FVector(0.f, 0.f, 0.6f)).GetSafeNormal();
			}
			// (2026-11-16: over the lip it read as a lumpy bar lying across the top of the water; only with -bfcaprock)
			if (FParse::Param(FCommandLine::Get(), TEXT("bfcaprock")))
			{
				Set.Add(Cap);
			}
			const int32 Blocks = Rr.RandRange(4, 8);
			for (int32 b = 0; b < Blocks; ++b)
			{
				// round the pool's sides (2026-11-15: not toward the face either, where the hollow is: blocks there sat in it,
				// or hung over it on ground it cut away)
				const float Ang = (Rr.FRand() < 0.5f ? -1.f : 1.f) * Rr.FRandRange(55.f, 150.f);
				const FVector At = Fa.Bottom + Fa.Dir.RotateAngleAxis(Ang + 180.f, FVector::UpVector) * Fa.PoolCm * Rr.FRandRange(0.75f, 1.1f);
				float G = float(At.Z);
				GroundAt(FVector2D(At), G);
				FBFRock Bo;
				Bo.Seed = Rr.RandRange(0, 65535);
				// (2026-11-16: at most 2.6 m, and only down by the pool: on a sheer fall the spot beside the pool can be the
				// cliff's top, and one of these sat on island 13's lip as a 14 m boulder)
				if (G > float(Fa.Bottom.Z) + 300.f)
				{
					continue;
				}
				const float Big = Rr.FRandRange(60.f, FMath::Min(60.f + 12.f * Fa.DropM, 260.f));
				Bo.Half = FVector(Big * Rr.FRandRange(0.9f, 1.4f), Big, Big * Rr.FRandRange(0.5f, 0.8f));
				Bo.At = FVector(At.X, At.Y, FMath::Max(G, float(Fa.Bottom.Z) - 80.f) + Bo.Half.Z * 0.2f);
				Bo.Rot = FRotator(Rr.FRandRange(-20.f, 20.f), Rr.FRandRange(0.f, 360.f), Rr.FRandRange(-20.f, 20.f));
				Set.Add(Bo);
			}
			AddRocks(Set);
		}
		// The hollow behind it (Raven, 2026-11-15: "add the undercut behind the falls"). A real fall's water and spray
		// wear the softer rock under its hard lip faster, so the face goes back under the lip: a low wide hollow behind
		// the curtain, a dry ledge at its floor just over the pool. A rock piece (the 4 m ground can't overhang): its
		// floor 40 cm over the pool, as wide as 15 m, about half as tall, 1.1 times its radius back into the face; its
		// roof at least 2.5 m under the lip; the face round it rough (CaveDensity, kind 4). Falls under 8 m: none.
		if (Fa.PoolCm > 0.f && Fa.DropM >= 8.f && !FParse::Param(FCommandLine::Get(), TEXT("bfnoundercut")))
		{
			const float Sq = 1.5f;
			const float Rh = FMath::Min(FMath::Clamp(32.f * Fa.DropM, 380.f, 750.f), (Fa.DropM * 100.f - 290.f) / (CaveFloorK(Sq) + 1.f / (0.9f * Sq)));
			if (Rh >= 300.f)
			{
				// (the face: where the ground drops below the lip's height less half the fall)
				FVector Face = Fa.Top;
				for (float d = 0.f; d < 3.f * CellCm; d += 25.f)
				{
					float G = 0.f;
					const FVector Q = Fa.Top + Fa.Dir * d;
					if (GroundAt(FVector2D(Q), G) && G < float(Fa.Top.Z) - 50.f * Fa.DropM)
					{
						Face = Q;
						break;
					}
				}
				const float Zc = float(Fa.Bottom.Z) + 40.f + CaveFloorK(Sq) * Rh;
				const FVector M0 = FVector(Face.X, Face.Y, Zc) + Fa.Dir * (0.35f * Rh);
				const TArray<FVector> Path = { M0, M0 - Fa.Dir * (0.6f * Rh) + FVector(0.f, 0.f, 10.f), M0 - Fa.Dir * (1.1f * Rh) + FVector(0.f, 0.f, 20.f) };
				const TArray<float> Sqs = { Sq, Sq, Sq * 1.1f };
				if (AddCave(Path, { Rh, 0.9f * Rh, 0.6f * Rh }, 4, &Sqs, nullptr, FString()) != INDEX_NONE)
				{
					++Undercuts;
					Falls[f].HollowAt = Path[1];
					Falls[f].HollowCm = Rh;
				}
			}
		}
		if (Fa.DropM >= 8.f)
		{
			Places.Add({ FString::Printf(TEXT("Falls (%.0f m)"), Fa.DropM), Fa.Bottom + Fa.Dir * 2500.f, 11 });
		}
	}
	if (Falls.Num() > 0)
	{
		LandformText += FString::Printf(TEXT("; %d hollows behind falls"), Undercuts);
	}
	if (Biggest != INDEX_NONE && Falls[Biggest].DropM < 8.f)
	{
		Places.Add({ FString::Printf(TEXT("Falls (%.0f m)"), Falls[Biggest].DropM), Falls[Biggest].Bottom + Falls[Biggest].Dir * 2500.f, 11 });
	}
}

void ABFWorld::PlaceBigCaves(int32 WantDeep, bool bWantCavern, FRandomStream& R)
{
	// Deep caves and a cavern (Raven, 2026-11-15: "differing sizes of caves, maybe a large cavern in a mountain").
	// Both open at the foot of a steep slope (as the dens), with the hill rising over them, never within 60 m of another
	// cave or 100 m of where you arrive.
	// - A deep cave: a passage 2.6-3.2 m round winding 35-65 m in (each stretch 7-11 m, turning up to 30 deg, rising or
	//   falling a little), one or two chambers 11-17 m across on the way, its roof at least 3 m under the hill.
	// - A cavern: a mouth 12-15 m across in a mountainside, a passage widening 25 m in to a hall 36-56 m across and
	//   about 18-28 m high, under at least 8 m of rock, the mountain round it on all sides; often (6 in 10) a skylight.
	const FVector Start = StartPoint();
	auto Clear = [&](const FVector& P, float Cm)
	{
		return FVector::Dist2D(P, Start) > 10000.f && !Caves.ContainsByPredicate([&](const FBFCave& Cv)
		{
			return Cv.Path.ContainsByPredicate([&](const FVector& Q) { return FVector::Dist2D(Q, P) < Cm; });
		});
	};
	auto Ground = [&](const FVector& P) { float G = -1e9f; GroundAt(FVector2D(P), G); return G; };
	auto MouthAt = [&](const FVector& Top, const FVector& Dn, float R0, FVector& M0) -> bool
	{
		// out from the top edge to where the ground eases under 22 deg; the floor 30 cm over the ground there
		for (float d = 600.f; d < 4000.f; d += 100.f)
		{
			const FVector P = Top + Dn * d;
			const float Sl = FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(Ground(P - Dn * 300.f) - Ground(P + Dn * 300.f)) / 600.f));
			if (Sl < 22.f)
			{
				float Wz = 0.f;
				const float G = Ground(P + Dn * 300.f);
				if (WaterAt(FVector(P.X, P.Y, G + 10.f), Wz))
				{
					return false;
				}
				M0 = FVector(Top.X, Top.Y, 0.f) + Dn * (d - 150.f) + FVector(0.f, 0.f, Ground(P) + 30.f + CaveFloorK(1.f) * R0);
				return true;
			}
		}
		return false;
	};
	int32 Deep = 0;
	for (int32 k = 0; k < 300 && Deep < WantDeep; ++k)
	{
		const FVector Near = Origin + FVector(R.FRandRange(0.12f, 0.88f) * SizeCm(), R.FRandRange(0.12f, 0.88f) * SizeCm(), 0.f);
		FVector Top, Dn, M0;
		const float R0 = R.FRandRange(260.f, 320.f);
		if (!FindCliff(Near, 40000.f, -1e9f, R, Top, Dn) || !MouthAt(Top, Dn, R0, M0) || !Clear(M0, 6000.f))
		{
			continue;
		}
		TArray<FVector> Path = { M0 };
		TArray<float> Radii = { R0 }, Sq = { 1.f };
		const int32 Steps = R.RandRange(5, 7);
		TSet<int32> Rooms;
		Rooms.Add(R.RandRange(2, Steps - 1));
		if (R.FRand() < 0.5f)
		{
			Rooms.Add(Steps);
		}
		float Yaw = (-Dn).Rotation().Yaw, FloorZ = float(M0.Z) - CaveFloorK(1.f) * R0;
		bool bOk = true;
		for (int32 i = 1; i <= Steps && bOk; ++i)
		{
			Yaw += R.FRandRange(-30.f, 30.f);
			// (no turning back toward the mouth: within 70 deg of straight in)
			Yaw = (-Dn).Rotation().Yaw + FMath::Clamp(FMath::FindDeltaAngleDegrees((-Dn).Rotation().Yaw, Yaw), -70.f, 70.f);
			FloorZ += (i == 1 ? 40.f : R.FRandRange(-70.f, 110.f));
			const bool bRoom = Rooms.Contains(i);
			const float Rad = bRoom ? R.FRandRange(550.f, 850.f) : R0 * R.FRandRange(0.95f, 1.15f);
			const float Squ = bRoom ? 1.3f : 1.f;
			const FVector P = Path.Last() + FRotator(0.f, Yaw, 0.f).Vector() * R.FRandRange(700.f, 1100.f);
			const FVector Pt(P.X, P.Y, FloorZ + CaveFloorK(Squ) * Rad);
			const float Roof = float(Pt.Z) + Rad / (0.9f * Squ);
			// its roof under the hill, and the hill beside it (it doesn't break out of a side)
			const FVector Side = FVector::CrossProduct(FRotator(0.f, Yaw, 0.f).Vector(), FVector::UpVector);
			bOk = Ground(Pt) > Roof + (i == 1 ? 0.f : 300.f) && Ground(Pt + Side * (Rad + 300.f)) > Roof - 100.f && Ground(Pt - Side * (Rad + 300.f)) > Roof - 100.f
				&& (i == 1 || Clear(Pt, 4000.f));
			Path.Add(Pt);
			Radii.Add(Rad);
			Sq.Add(Squ);
		}
		if (!bOk)
		{
			continue;
		}
		if (AddCave(Path, Radii, 2, &Sq, nullptr, FString::Printf(TEXT("Deep cave %d"), Deep + 1)) != INDEX_NONE)
		{
			++Deep;
		}
	}
	if (!bWantCavern)
	{
		return;
	}
	for (int32 k = 0; k < 400; ++k)
	{
		const FVector Near = Origin + FVector(R.FRandRange(0.12f, 0.88f) * SizeCm(), R.FRandRange(0.12f, 0.88f) * SizeCm(), 0.f);
		FVector Top, Dn, M0;
		const float R0 = R.FRandRange(600.f, 750.f);
		if (!FindCliff(Near, 40000.f, float(Origin.Z) + 2500.f, R, Top, Dn) || !MouthAt(Top, Dn, R0, M0) || !Clear(M0, 8000.f))
		{
			continue;
		}
		const FVector In = -Dn;
		const float Hall = R.FRandRange(1800.f, 2800.f), HallSq = R.FRandRange(1.5f, 1.9f);
		const float F0 = float(M0.Z) - CaveFloorK(1.f) * R0;
		const float Fl[4] = { F0, F0 + 60.f, F0 + 150.f, F0 + 250.f };
		const float Rd[4] = { R0, R0 * 1.05f, 900.f, Hall };
		const float Sqs[4] = { 1.f, 1.f, 1.f, HallSq };
		const float Dd[4] = { 0.f, 1200.f, 2500.f, 2500.f + 0.9f * Hall };
		TArray<FVector> Path;
		TArray<float> Radii, Sq;
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector P = M0 + In * Dd[i];
			Path.Add(FVector(P.X, P.Y, Fl[i] + CaveFloorK(Sqs[i]) * Rd[i]));
			Radii.Add(Rd[i]);
			Sq.Add(Sqs[i]);
		}
		const FVector Hc = Path.Last();
		const float Ceil = float(Hc.Z) + Hall / (0.9f * HallSq);
		bool bOk = Ground(Hc) > Ceil + 800.f && Ground(Path[2]) > float(Path[2].Z) + 900.f / 0.9f + 400.f && Clear(Hc, 9000.f);
		for (int32 a = 0; a < 8 && bOk; ++a)
		{
			const FVector Rim = Hc + FRotator(0.f, a * 45.f, 0.f).Vector() * Hall * 1.2f;
			bOk = Ground(Rim) > float(Hc.Z) + 300.f;
		}
		if (!bOk)
		{
			continue;
		}
		TArray<FVector4> Shafts;
		if (R.FRand() < 0.6f)
		{
			const FVector At = Hc + FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Vector() * Hall * 0.3f;
			Shafts.Add(FVector4(At.X, At.Y, Ceil - 400.f, R.FRandRange(280.f, 450.f)));
		}
		if (AddCave(Path, Radii, 3, &Sq, &Shafts, Shafts.Num() > 0 ? TEXT("The lit cavern") : TEXT("The great cavern")) != INDEX_NONE)
		{
			return;
		}
	}
}

FString ABFWorld::CaveText(int32 I) const
{
	if (!Caves.IsValidIndex(I))
	{
		return FString();
	}
	const FBFCave& C = Caves[I];
	float Len = 0.f;
	for (int32 i = 0; i + 1 < C.Path.Num(); ++i)
	{
		Len += float(FVector::Dist(C.Path[i], C.Path[i + 1]));
	}
	return FString::Printf(TEXT("%.0f m long, %d points; the piece %.0f x %.0f x %.0f m, %d vertices, %d triangles, built in %.2f s; %d ground cells cut away"),
		Len / 100.f, C.Path.Num(), C.Box.GetSize().X / 100.f, C.Box.GetSize().Y / 100.f, C.Box.GetSize().Z / 100.f, C.Verts, C.Tris, C.Seconds,
		C.Hole.Width() * C.Hole.Height());
}

bool ABFWorld::GroundAt(const FVector2D& XY, float& Z) const
{
	if (!bLandDone)
	{
		return false;
	}
	const float GX = float((XY.X - Origin.X) / CellCm), GY = float((XY.Y - Origin.Y) / CellCm);
	if (GX < 0.f || GY < 0.f || GX > N - 1 || GY > N - 1)
	{
		return false;
	}
	Z = float(Origin.Z) + 100.f * HeightM(GX, GY);
	return true;
}

bool ABFWorld::WaterAt(const FVector& At, float& SurfaceZ) const
{
	if (!bLandDone)
	{
		return false;
	}
	const float GX = float((At.X - Origin.X) / CellCm), GY = float((At.Y - Origin.Y) / CellCm);
	if (GX < -200.f || GY < -200.f || GX > N + 200.f || GY > N + 200.f)
	{
		return false;               // well beyond the island: not this world
	}
	float LevelM = 0.f;            // the sea, all round the island
	bool bSea = true;
	if (GX >= 0.f && GY >= 0.f && GX <= N - 1 && GY <= N - 1)
	{
		const int32 X = FMath::Clamp(FMath::RoundToInt(GX), 0, N - 1), Y = FMath::Clamp(FMath::RoundToInt(GY), 0, N - 1);
		const float Ground = HeightM(GX, GY);
		const float Fresh = W[Idx(X, Y)];
		if (Fresh > 0.05f && Fresh > Ground)
		{
			LevelM = Fresh;
			bSea = false;
		}
		else if (Ground >= 0.f)
		{
			return false;           // dry land
		}
	}
	if (bSea && GetWorld())
	{
		LevelM = SeaWaveM(FVector2D(At), GetWorld()->GetTimeSeconds());     // riding the waves
	}
	SurfaceZ = float(Origin.Z) + 100.f * LevelM;
	return At.Z < SurfaceZ + 2000.f && At.Z > SurfaceZ - 6000.f;
}

EBFBiome ABFWorld::BiomeAt(const FVector2D& XY) const
{
	const int32 X = FMath::RoundToInt((XY.X - Origin.X) / CellCm), Y = FMath::RoundToInt((XY.Y - Origin.Y) / CellCm);
	return bLandDone && X >= 0 && Y >= 0 && X < N && Y < N ? EBFBiome(Biome[Idx(X, Y)]) : EBFBiome::Sea;
}

FVector ABFWorld::StartPoint() const
{
	// Outward from the middle in a square spiral: the first open ground (plains) that is flat and dry all round.
	const int32 Mid = N / 2;
	for (int32 Ring = 0; Ring < N / 2 - 4; ++Ring)
	{
		for (int32 dy = -Ring; dy <= Ring; ++dy)
		{
			for (int32 dx = -Ring; dx <= Ring; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != Ring)
				{
					continue;
				}
				const int32 X = Mid + dx, Y = Mid + dy;
				bool bGood = true;
				for (int32 ey = -4; ey <= 4 && bGood; ++ey)
				{
					for (int32 ex = -4; ex <= 4 && bGood; ++ex)
					{
						const int32 C = Idx(X + ex, Y + ey);
						bGood = Biome[C] == uint8(EBFBiome::Plains) && FMath::Abs(H[C] - H[Idx(X, Y)]) < 1.5f;
					}
				}
				if (bGood)
				{
					return Origin + FVector(X * CellCm, Y * CellCm, H[Idx(X, Y)] * 100.f);
				}
			}
		}
	}
	return GetCentre() + FVector(0.0, 0.0, 100.f * H[Idx(Mid, Mid)]);
}

int32 ABFWorld::ClearTreesNear(const FVector2D& At, float RadiusCm, bool bSmallToo)
{
	if (GridN == 0)
	{
		return 0;
	}
	const FVector2D L = At - FVector2D(Origin);
	const int32 CX = int32(L.X / 1600.f), CY = int32(L.Y / 1600.f), Span = FMath::CeilToInt(RadiusCm / 1600.f) + 1;
	int32 Count = 0;
	TSet<int32> Touched;
	for (int32 GY = FMath::Max(CY - Span, 0); GY <= FMath::Min(CY + Span, GridN - 1); ++GY)
	{
		for (int32 GX = FMath::Max(CX - Span, 0); GX <= FMath::Min(CX + Span, GridN - 1); ++GX)
		{
			TArray<FIntPoint>& Cell = PlantGrid[GY * GridN + GX];
			for (int32 i = Cell.Num() - 1; i >= 0; --i)
			{
				const FIntPoint Ref = Cell[i];
				const EBFPlant P = EBFPlant(Ref.X / BFPlants::Variants);
				if (!bSmallToo && int32(P) > int32(EBFPlant::TreeFern) && P != EBFPlant::Palm)
				{
					continue;
				}
				const FBFPlantSpot& S = Spots[Ref.X][Ref.Y];
				if (FVector2D::Distance(FVector2D(S.Pos.X, S.Pos.Y), L) > RadiusCm)
				{
					continue;
				}
				Food[Ref.X][Ref.Y] = 0.f;
				Cell.RemoveAtSwap(i);           // (no animal finds it again)
				if (PlantComps.IsValidIndex(Ref.X) && PlantComps[Ref.X])
				{
					PlantComps[Ref.X]->UpdateInstanceTransform(Ref.Y, FTransform(FQuat::Identity, Origin + FVector(S.Pos) - FVector(0.f, 0.f, 5000.f), FVector(0.001f)), true, false, true);
					Touched.Add(Ref.X);
				}
				++Count;
			}
		}
	}
	for (const int32 KV : Touched)
	{
		PlantComps[KV]->MarkRenderStateDirty();
	}
	return Count;
}

bool ABFWorld::NearestWater(const FVector2D& Near, EBFBiome Kind, FVector& At) const
{
	const int32 CX = FMath::Clamp(FMath::RoundToInt((Near.X - Origin.X) / CellCm), 0, N - 1);
	const int32 CY = FMath::Clamp(FMath::RoundToInt((Near.Y - Origin.Y) / CellCm), 0, N - 1);
	int64 Best = MAX_int64;
	for (int32 Y = 0; Y < N; Y += 2)
	{
		for (int32 X = 0; X < N; X += 2)
		{
			if (Biome[Idx(X, Y)] == uint8(Kind))
			{
				const int64 D = int64(X - CX) * (X - CX) + int64(Y - CY) * (Y - CY);
				if (D < Best)
				{
					Best = D;
					At = Origin + FVector(X * CellCm, Y * CellCm, FMath::Max(H[Idx(X, Y)], W[Idx(X, Y)]) * 100.f);
				}
			}
		}
	}
	return Best != MAX_int64;
}

// ---------------------------------------------------------------------------------------------- the plants

namespace
{
	// A random stream for one point of ground from a strong hash of its index. (FRandomStream is an LCG: seeded with
	// neighbouring numbers, neighbouring points draw related first numbers, and trees stood in rows seen from above.)
	struct FCellRand
	{
		uint32 S;
		static uint32 Mix(uint32 X)
		{
			X ^= X >> 16;
			X *= 0x7feb352dU;
			X ^= X >> 15;
			X *= 0x846ca68bU;
			X ^= X >> 16;
			return X;
		}
		FCellRand(uint32 Seed, uint32 Cell) : S(Mix(Seed * 0x9E3779B9u ^ Mix(Cell + 0x632BE5ABu))) {}
		uint32 Next()
		{
			S = S * 747796405u + 2891336453u;           // PCG
			const uint32 W = ((S >> ((S >> 28u) + 4u)) ^ S) * 277803737u;
			return (W >> 22u) ^ W;
		}
		float FRand() { return float(Next() >> 8) / 16777216.f; }
		float FRandRange(float A, float B) { return A + (B - A) * FRand(); }
		int32 RandRange(int32 A, int32 B) { return A + int32(Next() % uint32(B - A + 1)); }
	};
}

void ABFWorld::PlacePlants(const TArray<int32>& ToWater, const TArray<uint8>& Kind)
{
	// Every point of dry ground, every kind: how many to a hectare its biome holds (more by fresh water), thinned and
	// clumped by a slow noise of the kind's own (ferns in drifts, trees in stands), none on ground too steep or high for
	// it. Each gets a place within its 4 m square, a shape (one of three), a size, a turn, and (small ones) a lean with
	// the slope. Trees and other trunks keep 15 m clear of where a creature arrives.
	const float CellM = CellCm / 100.f;
	const float CellHa = CellM * CellM / 10000.f;
	const FVector Start = StartPoint() - Origin;
	FVector2D Off[BFPlants::Kinds];
	{
		FRandomStream R(Seed + 77);
		for (FVector2D& O : Off)
		{
			O = FVector2D(R.FRandRange(-9000.f, 9000.f), R.FRandRange(-9000.f, 9000.f));
		}
	}
	constexpr int32 Bands = 32;
	TArray<float> FernCover;
	TArray<float>& CrownCover = Crown;
	FernCover.Init(0.f, N * N);
	CrownCover.Init(0.f, N * N);
	TArray<TArray<FBFPlantSpot>> Local;
	Local.SetNum(Bands * NumKV);
	ParallelFor(Bands, [&](int32 Band)
	{
		const int32 Y0 = Band * N / Bands, Y1 = (Band + 1) * N / Bands;
		for (int32 Y = FMath::Max(Y0, 1); Y < FMath::Min(Y1, N - 1); ++Y)
		{
			for (int32 X = 1; X < N - 1; ++X)
			{
				const int32 C = Idx(X, Y);
				if (Kind[C] != 0)
				{
					continue;           // water
				}
				const uint8 B = Biome[C];
				const float GX = (H[C + 1] - H[C - 1]) / (2.f * CellM), GY = (H[C + N] - H[C - N]) / (2.f * CellM);
				const float SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY)));
				const FVector3f Normal = FVector3f(-GX, -GY, 1.f).GetSafeNormal();
				const bool bNearFresh = ToWater[C] <= 2;
				FCellRand Rc{ uint32(Seed), uint32(C) };
				for (int32 k = 0; k < BFPlants::Kinds; ++k)
				{
					const EBFPlant P = EBFPlant(k);
					const float Dens = BFPlants::Density(P, B) + (bNearFresh ? BFPlants::NearFreshWater(P) : 0.f);
					if (Dens <= 0.f || SlopeDeg > BFPlants::MaxSlope(P) || H[C] > BFPlants::MaxHeightM(P))
					{
						continue;
					}
					const bool bSmall = P == EBFPlant::Fern || P == EBFPlant::Horsetail || P == EBFPlant::Shrub || P == EBFPlant::Bennettite;
					const float Patch = bSmall ? 45.f : 110.f;
					const float Nz = FMath::PerlinNoise2D(FVector2D(X * CellM / Patch, Y * CellM / Patch) + Off[k]);
					const float Clump = bSmall ? 2.2f * FMath::SmoothStep(-0.3f, 0.35f, Nz) : 0.35f + 1.3f * FMath::SmoothStep(-0.4f, 0.4f, Nz);
					const float Expect = Dens * CellHa * Clump;
					if (P == EBFPlant::Fern || P == EBFPlant::Horsetail)
					{
						FernCover[C] += Expect * 0.45f;            // a fern ~1.2 m round on a 16 m2 point (and its shade)
					}
					else if (int32(P) <= int32(EBFPlant::TreeFern))
					{
						CrownCover[C] += Expect * 3.5f;            // a crown ~4.5 m round
					}
					const int32 Count = FMath::FloorToInt(Expect) + (Rc.FRand() < FMath::Frac(Expect) ? 1 : 0);
					for (int32 i = 0; i < Count; ++i)
					{
						const float JX = Rc.FRandRange(-0.5f, 0.5f), JY = Rc.FRandRange(-0.5f, 0.5f);
						FBFPlantSpot S;
						S.Scale = Rc.FRandRange(0.8f, 1.2f);
						S.Yaw = Rc.FRandRange(0.f, 2.f * PI);
						const int32 Var = Rc.RandRange(0, BFPlants::Variants - 1);
						const FBFPlantKind& K = BFPlants::Kind(P);
						S.Pos = FVector3f((X + JX) * CellCm, (Y + JY) * CellCm, 100.f * HeightM(X + JX, Y + JY) - K.SinkCm * S.Scale);
						S.Normal = bSmall ? FMath::Lerp(FVector3f::UpVector, Normal, 0.6f).GetSafeNormal() : FVector3f::UpVector;
						if (!bSmall && FVector2f::Distance(FVector2f(S.Pos.X, S.Pos.Y), FVector2f(float(Start.X), float(Start.Y))) < 1500.f)
						{
							continue;
						}
						Local[Band * NumKV + k * BFPlants::Variants + Var].Add(S);
					}
				}
			}
		}
	});
	// The ground takes the colour of what covers it: deep fern green under the fern beds (they show from the air, where
	// the ferns themselves are too small to draw), brown leaf litter under the trees.
	const FColor FernGreen(46, 78, 30), Litter(70, 60, 40);
	for (int32 C = 0; C < N * N; ++C)
	{
		const float F = FMath::Clamp(FernCover[C], 0.f, 1.f) * 0.85f, T = FMath::Clamp(CrownCover[C], 0.f, 1.f) * 0.55f;
		if (F <= 0.f && T <= 0.f)
		{
			continue;
		}
		FLinearColor Cl(Colour[C].R, Colour[C].G, Colour[C].B);
		Cl = FMath::Lerp(Cl, FLinearColor(FernGreen.R, FernGreen.G, FernGreen.B), F);
		Cl = FMath::Lerp(Cl, FLinearColor(Litter.R, Litter.G, Litter.B), T);
		Colour[C] = FColor(uint8(Cl.R), uint8(Cl.G), uint8(Cl.B), 255);
	}
	GridN = FMath::CeilToInt(float(SizeCm()) / 1600.f) + 1;
	PlantGrid.SetNum(GridN * GridN);
	for (int32 KV = 0; KV < NumKV; ++KV)
	{
		Spots[KV].Reset();
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			Spots[KV].Append(Local[Band * NumKV + KV]);
		}
		const FBFPlantKind& K = BFPlants::Kind(EBFPlant(KV / BFPlants::Variants));
		Food[KV].SetNumUninitialized(Spots[KV].Num());
		for (int32 i = 0; i < Spots[KV].Num(); ++i)
		{
			const FBFPlantSpot& S = Spots[KV][i];
			Food[KV][i] = K.FoodKg * S.Scale * S.Scale;
			const int32 GX = FMath::Clamp(int32(S.Pos.X / 1600.f), 0, GridN - 1), GY = FMath::Clamp(int32(S.Pos.Y / 1600.f), 0, GridN - 1);
			PlantGrid[GY * GridN + GX].Add(FIntPoint(KV, i));
		}
	}
}

float ABFWorld::RockDensity(const FBFRock& R, const FVector& P) const
{
	// In the rock's own frame: a rounded box (positive inside, cm), lumpy with noise as big as a quarter of its least
	// size; a fin's lumps stretched up and down (grooves); a slab's top cut flat.
	const FVector L = R.Rot.UnrotateVector(P - R.At);
	const float Least = float(FMath::Min3(R.Half.X, R.Half.Y, R.Half.Z));
	// (a bed in a gorge wall: squarer, its face banded. 2026-11-16: only the walls' own beds: a fall's caprock, tinted too,
	// took their shape, its ends pinched across the river's width, and hung over the lip as a dark lump)
	const bool bBed = R.bBed;
	const float Round = (bBed ? 0.12f : 0.35f) * Least;
	const FVector Q = L.GetAbs() - (R.Half - FVector(Round));
	const float Sd = float(FVector(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0), FMath::Max(Q.Z, 0.0)).Size()) + FMath::Min(float(FMath::Max3(Q.X, Q.Y, Q.Z)), 0.f) - Round;
	const FVector Jitter(R.Seed * 7.13f, R.Seed * 3.71f, R.Seed * 1.93f);
	const float Mean = float(R.Half.X + R.Half.Y + R.Half.Z) / 3.f;
	const float Amp = FMath::Min(0.24f * Mean, 0.6f * Least);
	const FVector Scale = R.Kind == 2 ? FVector(Mean * 0.6f, Mean * 0.6f, Mean * 1.6f) : FVector(Mean * 0.7f);
	float Solid = -Sd + Amp * FMath::PerlinNoise3D(L / Scale + Jitter) + 0.4f * Amp * FMath::PerlinNoise3D(L / (0.38f * Scale) + Jitter * 2.f);
	if (bBed)
	{
		// (2026-11-16, Raven: still "man made blocks and not natural". Each block had square ends, its own lumps and its
		// own flat top, so the wall read as courses of masonry. Now:
		// - its ends pinch back into the wall (a bed thins out and fades, it doesn't stop square), so neighbours' fronts
		//   run together as one wavy line;
		// - its grain is the wall's, not its own: the front's in-and-out, thin layers and joints are all taken from the
		//   world's position, the same for every block there, so where blocks overlap the rock carries straight on;
		// - joints: wandering cracks from top to bottom, through every bed alike, as real rock splits;
		// - its top rounded off into its face and dipping a little to the front, uneven, lower toward its ends.)
		const float Ends = FMath::Square(FMath::Clamp(FMath::Abs(float(L.X)) / float(R.Half.X), 0.f, 1.f));
		FVector Lp = L;
		Lp.Y += Ends * 0.9f * FMath::Min(float(R.Half.Y), 2.f * float(R.Half.Y) - 180.f);          // (local +Y is out of the wall: its front drawn back)
		const FVector Qb = Lp.GetAbs() - (R.Half - FVector(Round));
		const float SdB = float(FVector(FMath::Max(Qb.X, 0.0), FMath::Max(Qb.Y, 0.0), FMath::Max(Qb.Z, 0.0)).Size()) + FMath::Min(float(FMath::Max3(Qb.X, Qb.Y, Qb.Z)), 0.f) - Round;
		const FVector Wo(float(Seed % 97) * 3.7f, float(Seed % 89) * 2.9f, 11.f);
		const FVector Pw = P;
		const float Wave = 75.f * FMath::PerlinNoise3D(FVector(Pw.X / 800.f, Pw.Y / 800.f, Pw.Z / 1100.f) + Wo)
			+ 28.f * FMath::PerlinNoise3D(Pw / 230.f + Wo * 1.7f);
		const float Thin = 30.f * FMath::PerlinNoise3D(FVector(Pw.X / 2600.f, Pw.Y / 2600.f, Pw.Z / 95.f) + Wo * 2.3f);
		const float Jn = FMath::PerlinNoise2D(FVector2D(Pw.X / 460.f, Pw.Y / 460.f) + FVector2D(Wo.X, Wo.Y) * 0.31f);
		const float JOn = FMath::Clamp(0.5f + 1.5f * FMath::PerlinNoise2D(FVector2D(Pw.X / 1500.f, Pw.Y / 1500.f) + FVector2D(Wo.Y, Wo.X)), 0.f, 1.f);
		// (on a sheer face a bed stands out under a metre: grain as deep as that broke it into stacks of lumps, so the
		// grain is scaled to how far the bed stands out)
		const float Fit = FMath::Clamp((2.f * float(R.Half.Y) - 180.f) / 260.f, 0.3f, 1.f);
		const float Joint = 75.f * Fit * JOn * FMath::Square(FMath::Max(0.f, 1.f - FMath::Abs(Jn) / 0.12f));
		Solid = -SdB + Fit * Wave + Thin - Joint + 10.f * FMath::PerlinNoise3D(L / 90.f + Jitter);
		const float TopHere = float(R.TopZ) - Ends * 80.f - 0.14f * FMath::Max(float(L.Y), 0.f)
			+ 28.f * FMath::PerlinNoise2D(FVector2D(Pw.X / 520.f, Pw.Y / 520.f) + FVector2D(Wo.Z, Wo.X));
		const float Cut = TopHere - float(P.Z);
		const float K = 75.f;          // (how round the top's edge: a smooth min of the rock and the cut)
		const float Hk = FMath::Max(K - FMath::Abs(Solid - Cut), 0.f) / K;
		return FMath::Min(Solid, Cut) - Hk * Hk * K * 0.25f;
	}
	if (R.Kind == 1)
	{
		Solid = FMath::Min(Solid, float(R.TopZ - P.Z));
	}
	return Solid;
}

void ABFWorld::MarkRock(const FBFRock& R, int32 Add)
{
	// The ground points under it (as far as it reaches across) where it stands over 80 cm above the ground.
	if (RockCells.Num() != N * N)
	{
		RockCells.Init(0, N * N);
	}
	const float Reach = FMath::Max(float(FMath::Max(R.Half.X, R.Half.Y)) * 0.85f, 0.75f * CellCm);   // (at least its nearest ground point)
	float G = 0.f;
	if (!GroundAt(FVector2D(R.At), G) || float(R.At.Z + R.Half.Z) - G < 80.f)
	{
		return;
	}
	const int32 X0 = FMath::FloorToInt((R.At.X - Reach - Origin.X) / CellCm), X1 = FMath::CeilToInt((R.At.X + Reach - Origin.X) / CellCm);
	const int32 Y0 = FMath::FloorToInt((R.At.Y - Reach - Origin.Y) / CellCm), Y1 = FMath::CeilToInt((R.At.Y + Reach - Origin.Y) / CellCm);
	for (int32 y = FMath::Max(Y0, 0); y <= FMath::Min(Y1, N - 1); ++y)
	{
		for (int32 x = FMath::Max(X0, 0); x <= FMath::Min(X1, N - 1); ++x)
		{
			if (FVector2D::Distance(FVector2D(Origin.X + x * CellCm, Origin.Y + y * CellCm), FVector2D(R.At)) <= Reach)
			{
				uint8& K = RockCells[Idx(x, y)];
				K = uint8(FMath::Clamp(int32(K) + Add, 0, 255));
			}
		}
	}
}

int32 ABFWorld::AddRocks(const TArray<FBFRock>& Rocks)
{
	// Each rock on its own grid (its least size over five cells: chunky), surface nets, then every triangle its own
	// three corners and its own flat normal (faceted, as the plants are); grey-brown, each rock a shade of its own, moss
	// on what faces up of the boulders.
	if (Rocks.Num() == 0)
	{
		return INDEX_NONE;
	}
	// (2026-11-17, a closer look: a rock hung in the air over island 13's ravine floor. For the checks: any rock whose
	// lowest point stands a metre or more over all the ground under its footprint is logged.)
	if (FParse::Param(FCommandLine::Get(), TEXT("bflandform")))
	{
		int32 Floating = 0;
		for (const FBFRock& R : Rocks)
		{
			const FQuat Qr = R.Rot.Quaternion();
			float Lowest = 1e9f, GroundTop = -1e9f;
			for (int32 sx = -1; sx <= 1; ++sx)
			{
				for (int32 sy = -1; sy <= 1; ++sy)
				{
					for (int32 sz = -1; sz <= 1; sz += 2)
					{
						const FVector Pc = R.At + Qr.RotateVector(FVector(sx * R.Half.X, sy * R.Half.Y, sz * R.Half.Z) * 0.8f);
						Lowest = FMath::Min(Lowest, float(Pc.Z));
						float G;
						if (GroundAt(FVector2D(Pc), G))
						{
							GroundTop = FMath::Max(GroundTop, G);
						}
					}
				}
			}
			// (a wall's bed: its back, local -Y, should be in the wall; one standing clear of it hangs in the air)
			if (R.bBed || R.Tint.A > 0)
			{
				float Back = -1e9f;
				for (int32 sx = -1; sx <= 1; ++sx)
				{
					const FVector Pb = R.At + Qr.RotateVector(FVector(sx * R.Half.X * 0.6f, -R.Half.Y * 0.9f, 0.f));
					float G;
					if (GroundAt(FVector2D(Pb), G))
					{
						Back = FMath::Max(Back, G);
					}
				}
				if (Back > -1e8f && Back < Lowest + 50.f && ++Floating <= 12)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] rocks: bed clear of its wall kind %d at %s half %s, its back's ground %.0f cm under it"), int32(R.Kind), *R.At.ToString(), *R.Half.ToString(), Lowest - Back);
				}
			}
			if (GroundTop > -1e8f && Lowest > GroundTop + 100.f)
			{
				if (++Floating <= 12)
				{
					UE_LOG(LogTemp, Display, TEXT("[BoneForge] rocks: floating kind %d bed %d tinted %d at %s half %s, %.0f cm over the ground"), int32(R.Kind), R.bBed ? 1 : 0, R.Tint.A > 0 ? 1 : 0, *R.At.ToString(), *R.Half.ToString(), Lowest - GroundTop);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] rocks: %d of %d floating"), Floating, Rocks.Num());
	}
	TArray<FVector> Verts, Normals;
	TArray<FColor> Colours;
	TArray<int32> Tris;
	for (const FBFRock& R : Rocks)
	{
		const float Mean = float(R.Half.X + R.Half.Y + R.Half.Z) / 3.f;
		const float Least = float(FMath::Min3(R.Half.X, R.Half.Y, R.Half.Z));
		// (a bed in a wall finer: on the coarse grid its flat top met its sheer face in a row of teeth)
		const float Res = (R.Kind == 1 && R.Tint.A > 0) ? FMath::Clamp(FMath::Min(Mean / 4.5f, 0.9f * Least), 24.f, 55.f)
			: FMath::Clamp(FMath::Min(Mean / 2.8f, 0.9f * Least), 24.f, 90.f);
		const float Reach = float(R.Half.Size()) * 1.25f;
		const FBox Box(R.At - FVector(Reach), R.At + FVector(Reach));
		const int32 NX = FMath::CeilToInt(2.f * Reach / Res) + 1;
		auto I3 = [&](int32 x, int32 y, int32 z) { return (z * NX + y) * NX + x; };
		TArray<float> S;
		S.SetNumUninitialized(NX * NX * NX);
		for (int32 z = 0; z < NX; ++z)
		{
			for (int32 y = 0; y < NX; ++y)
			{
				for (int32 x = 0; x < NX; ++x)
				{
					S[I3(x, y, z)] = RockDensity(R, Box.Min + FVector(x, y, z) * Res);
				}
			}
		}
		TArray<int32> CellV;
		TArray<FVector> V;
		CellV.Init(INDEX_NONE, (NX - 1) * (NX - 1) * (NX - 1));
		auto C3 = [&](int32 x, int32 y, int32 z) { return (z * (NX - 1) + y) * (NX - 1) + x; };
		static const int32 E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (int32 z = 0; z < NX - 1; ++z)
		{
			for (int32 y = 0; y < NX - 1; ++y)
			{
				for (int32 x = 0; x < NX - 1; ++x)
				{
					float Cn[8];
					int32 In = 0;
					for (int32 k = 0; k < 8; ++k)
					{
						Cn[k] = S[I3(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1))];
						In += Cn[k] > 0.f ? 1 : 0;
					}
					if (In == 0 || In == 8)
					{
						continue;
					}
					FVector Sum = FVector::ZeroVector;
					int32 Cross = 0;
					for (const auto& Ed : E)
					{
						const float A = Cn[Ed[0]], B = Cn[Ed[1]];
						if ((A > 0.f) != (B > 0.f))
						{
							const float T = A / (A - B);
							Sum += FMath::Lerp(FVector(Ed[0] & 1, (Ed[0] >> 1) & 1, (Ed[0] >> 2) & 1), FVector(Ed[1] & 1, (Ed[1] >> 1) & 1, (Ed[1] >> 2) & 1), T);
							++Cross;
						}
					}
					CellV[C3(x, y, z)] = V.Add(Box.Min + (FVector(x, y, z) + Sum / FMath::Max(Cross, 1)) * Res);
				}
			}
		}
		// Its colour.
		FCellRand Rc{ uint32(R.Seed), 99u };
		const float Shade = Rc.FRandRange(0.8f, 1.12f);
		// (2026-11-15, Raven: the rocks on the walls should "blend in better, like they're part of the wall and not just
		// thrown in": set in a wall, the wall's own colour, a shade either way, no moss)
		const bool bInWall = R.Tint.A > 0;
		const FLinearColor Base = bInWall ? FLinearColor(R.Tint) * Rc.FRandRange(0.92f, 1.04f) : FLinearColor(FColor(116, 114, 110)) * Shade;          // (2026-11-17: the rock's cooler grey and as dark; at 150,142,130, facing the sun, they stood cream-white beside it from above)
		const FLinearColor Moss(FColor(64, 84, 40));
		auto Tri = [&](int32 a, int32 b, int32 c, const FVector& Inward)
		{
			// Faces out of the rock (UE's winding: cross(c - a, b - a) points out).
			FVector PA = V[a], PB = V[b], PC = V[c];
			FVector Nf = FVector::CrossProduct(PC - PA, PB - PA);
			if (FVector::DotProduct(Nf, Inward) > 0.f)
			{
				Swap(PB, PC);
				Nf = -Nf;
			}
			Nf = Nf.GetSafeNormal();
			if (Nf.IsNearlyZero())
			{
				return;
			}
			const float Up = FMath::Clamp((float(Nf.Z) - 0.6f) / 0.3f, 0.f, 1.f);
			FColor Cl = FMath::Lerp(Base, Moss, bInWall ? 0.f : (R.Kind == 0 ? 0.3f * Up : 0.12f * Up)).ToFColor(true);
			if (bInWall)
			{
				// (2026-11-16: a shade per block picked each one out as a brick. The wall's colour in bands by height, the
				// same for every block, a little lighter where a ledge's top catches dust)
				const FVector Pc = (PA + PB + PC) / 3.f;
				const FVector Wo(float(Seed % 97) * 3.7f, float(Seed % 89) * 2.9f, 11.f);
				const float Band = 1.f + 0.07f * FMath::PerlinNoise3D(FVector(Pc.X / 3000.f, Pc.Y / 3000.f, Pc.Z / 160.f) + Wo)
					+ 0.035f * FMath::PerlinNoise3D(Pc / 450.f + Wo * 0.7f) + 0.05f * Up;
				Cl = (FLinearColor(R.Tint) * Band).ToFColor(true);
			}
			const int32 First = Verts.Num();
			Verts.Append({ PA, PB, PC });
			const float ToWall = R.Kind == 1 ? 0.25f : 0.45f;          // (a bed's sheer face keeps more of its own light and shade)
			FVector Ns = R.WallN.IsZero() ? Nf : ((1.f - ToWall) * Nf + ToWall * R.WallN).GetSafeNormal();
			if (R.bBed)
			{
				// (2026-11-16: beds on a fall's shaded face came out darker than the bare face round them. The ground's
				// normals are smoothed across a 4 m step, so a sheer face of ground is shaded as if half facing the sky;
				// a bed's flat facets faced straight out. Tipped up part way, as the ground's are, they take its light.)
				Ns = (Ns + FVector(0.f, 0.f, 0.45f)).GetSafeNormal();
			}
			Normals.Append({ Ns, Ns, Ns });
			Colours.Append({ Cl, Cl, Cl });
			Tris.Append({ First, First + 1, First + 2 });
		};
		auto Quad = [&](int32 a, int32 b, int32 c, int32 d, const FVector& Inward)
		{
			if (a == INDEX_NONE || b == INDEX_NONE || c == INDEX_NONE || d == INDEX_NONE)
			{
				return;
			}
			// (across whichever diagonal keeps both halves facing out together: across the other, a folded quad showed
			// a small dark fleck)
			auto Facing = [&](int32 i, int32 j, int32 k)
			{
				return float(FVector::DotProduct(FVector::CrossProduct(V[k] - V[i], V[j] - V[i]).GetSafeNormal(), -Inward));
			};
			const float Ac = FMath::Min(FMath::Abs(Facing(a, b, c)), FMath::Abs(Facing(a, c, d))) * (Facing(a, b, c) * Facing(a, c, d) > 0.f ? 1.f : -1.f);
			const float Bd = FMath::Min(FMath::Abs(Facing(a, b, d)), FMath::Abs(Facing(b, c, d))) * (Facing(a, b, d) * Facing(b, c, d) > 0.f ? 1.f : -1.f);
			if (Bd > Ac)
			{
				Tri(a, b, d, Inward);
				Tri(b, c, d, Inward);
			}
			else
			{
				Tri(a, b, c, Inward);
				Tri(a, c, d, Inward);
			}
		};
		for (int32 z = 1; z < NX - 1; ++z)
		{
			for (int32 y = 1; y < NX - 1; ++y)
			{
				for (int32 x = 0; x < NX - 1; ++x)
				{
					const bool A = S[I3(x, y, z)] > 0.f, B = S[I3(x + 1, y, z)] > 0.f;
					if (A != B)
					{
						Quad(CellV[C3(x, y - 1, z - 1)], CellV[C3(x, y, z - 1)], CellV[C3(x, y, z)], CellV[C3(x, y - 1, z)], FVector(A ? -1.f : 1.f, 0.f, 0.f));
					}
				}
			}
		}
		for (int32 z = 1; z < NX - 1; ++z)
		{
			for (int32 y = 0; y < NX - 1; ++y)
			{
				for (int32 x = 1; x < NX - 1; ++x)
				{
					const bool A = S[I3(x, y, z)] > 0.f, B = S[I3(x, y + 1, z)] > 0.f;
					if (A != B)
					{
						Quad(CellV[C3(x - 1, y, z - 1)], CellV[C3(x, y, z - 1)], CellV[C3(x, y, z)], CellV[C3(x - 1, y, z)], FVector(0.f, A ? -1.f : 1.f, 0.f));
					}
				}
			}
		}
		for (int32 z = 0; z < NX - 1; ++z)
		{
			for (int32 y = 1; y < NX - 1; ++y)
			{
				for (int32 x = 1; x < NX - 1; ++x)
				{
					const bool A = S[I3(x, y, z)] > 0.f, B = S[I3(x, y, z + 1)] > 0.f;
					if (A != B)
					{
						Quad(CellV[C3(x - 1, y - 1, z)], CellV[C3(x, y - 1, z)], CellV[C3(x, y, z)], CellV[C3(x - 1, y, z)], FVector(0.f, 0.f, A ? -1.f : 1.f));
					}
				}
			}
		}
		MarkRock(R, 1);
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->bUseAsyncCooking = true;
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->RegisterComponent();
	Mesh->CreateMeshSection(0, Verts, Tris, Normals, {}, Colours, {}, true);
	Mesh->SetMaterial(0, GroundMat);
	Mesh->SetCullDistance(60000.f);          // (not drawn past 600 m)
	const int32 Group = RockMeshes.Add(Mesh);
	RockSets.SetNum(RockMeshes.Num());
	RockSets[Group] = Rocks;
	return Group;
}

void ABFWorld::RemoveRocks(int32 Group)
{
	if (!RockMeshes.IsValidIndex(Group) || !RockMeshes[Group])
	{
		return;
	}
	for (const FBFRock& R : RockSets[Group])
	{
		MarkRock(R, -1);
	}
	RockMeshes[Group]->DestroyComponent();
	RockMeshes[Group] = nullptr;
	RockSets[Group].Reset();
}

int32 ABFWorld::NumRocks(uint8 Kind) const
{
	int32 Count = 0;
	for (int32 g = 0; g < FMath::Min(IslandRockGroups, RockSets.Num()); ++g)
	{
		for (const FBFRock& R : RockSets[g])
		{
			Count += R.Kind == Kind ? 1 : 0;
		}
	}
	return Count;
}

bool ABFWorld::NearestRock(const FVector& Near, uint8 Kind, FBFRock& Out) const
{
	double Best = 1e30;
	for (int32 g = 0; g < FMath::Min(IslandRockGroups, RockSets.Num()); ++g)
	{
		for (const FBFRock& R : RockSets[g])
		{
			const double D = FVector::DistSquared2D(R.At, Near);
			if (R.Kind == Kind && D < Best)
			{
				Best = D;
				Out = R;
			}
		}
	}
	return Best < 1e30;
}

bool ABFWorld::RockAt(const FVector& At) const
{
	const int32 X = FMath::RoundToInt((At.X - Origin.X) / CellCm), Y = FMath::RoundToInt((At.Y - Origin.Y) / CellCm);
	return RockCells.Num() == N * N && X >= 0 && Y >= 0 && X < N && Y < N && RockCells[Idx(X, Y)] > 0;
}

void ABFWorld::PlaceRocks()
{
	// Boulders where cliffs meet gentler ground (fallen from them: 1 to 4 together, the bigger nearer the cliff), fins on
	// steep high ground; one mesh to each 256 m tile. The same rocks each time for a seed.
	const double T0 = FPlatformTime::Seconds();
	float MaxH = 0.f;
	for (int32 C = 0; C < N * N; ++C)
	{
		MaxH = FMath::Max(MaxH, H[C]);
	}
	const int32 PerSide = (N - 1) / ChunkCells;
	TArray<TArray<FBFRock>> ByTile;
	ByTile.SetNum(PerSide * PerSide);
	int32 Boulders = 0, Fins = 0;
	auto SlopeAt = [&](int32 X, int32 Y, FVector2D& Grad)
	{
		Grad = FVector2D((H[Idx(X + 1, Y)] - H[Idx(X - 1, Y)]) / (2.f * CellCm / 100.f), (H[Idx(X, Y + 1)] - H[Idx(X, Y - 1)]) / (2.f * CellCm / 100.f));
		return FMath::RadiansToDegrees(FMath::Atan(float(Grad.Size())));
	};
	auto Dry = [&](int32 X, int32 Y) { return !(W[Idx(X, Y)] > 0.05f && W[Idx(X, Y)] > H[Idx(X, Y)]) && H[Idx(X, Y)] > 0.5f; };
	auto Put = [&](const FBFRock& R)
	{
		for (const FBFCave& Cv : Caves)
		{
			if (FVector::Dist2D(R.At, Cv.MouthOut) < 1400.f || (Cv.Box.ExpandBy(200.f).IsInsideXY(R.At)))
			{
				return;          // not in a cave's way, nor over one
			}
		}
		if (NearFallFace(R.At))
		{
			return;
		}
		const int32 TX = FMath::Clamp(int32((R.At.X - Origin.X) / (CellCm * ChunkCells)), 0, PerSide - 1);
		const int32 TY = FMath::Clamp(int32((R.At.Y - Origin.Y) / (CellCm * ChunkCells)), 0, PerSide - 1);
		ByTile[TY * PerSide + TX].Add(R);
	};
	for (int32 Y = 6; Y < N - 6; Y += 3)
	{
		for (int32 X = 6; X < N - 6; X += 3)
		{
			if (!Dry(X, Y))
			{
				continue;
			}
			FCellRand Rc{ uint32(Seed) + 4242u, uint32(Idx(X, Y)) };
			FVector2D Grad;
			const float Slope = SlopeAt(X, Y, Grad);
			// The foot of a cliff: gentle here, a cliff within 12 m uphill, at least 6 m higher.
			if (Slope < 26.f && Boulders < 700)
			{
				bool bFoot = false;
				FVector2D Uphill = FVector2D::ZeroVector;
				for (int32 d = 0; d < 8 && !bFoot; ++d)
				{
					const FVector2D Dir(FMath::Cos(d * PI / 4.f), FMath::Sin(d * PI / 4.f));
					for (int32 Far = 2; Far <= 5 && !bFoot; ++Far)
					{
						const int32 UX = X + FMath::RoundToInt(Dir.X * Far), UY = Y + FMath::RoundToInt(Dir.Y * Far);
						FVector2D G2;
						if (H[Idx(UX, UY)] - H[Idx(X, Y)] > 4.f && SlopeAt(UX, UY, G2) > 30.f)
						{
							bFoot = true;
							Uphill = Dir;
						}
					}
				}
				const bool bLoose = !bFoot && Biome[Idx(X, Y)] == uint8(EBFBiome::Rock) && Rc.FRand() < 0.04f;      // a lone stone on rocky ground
				if (bLoose || (bFoot && Rc.FRand() < 0.35f))
				{
					const int32 Count = bLoose ? 1 : Rc.RandRange(1, 4);
					for (int32 b = 0; b < Count; ++b)
					{
						FBFRock R;
						R.Kind = 0;
						R.Seed = int32(Rc.Next() & 0xffff);
						const float Big = Rc.FRandRange(60.f, 230.f) * (b == 0 ? 1.f : 0.6f);
						R.Half = FVector(Big * Rc.FRandRange(0.8f, 1.3f), Big * Rc.FRandRange(0.7f, 1.1f), Big * Rc.FRandRange(0.55f, 0.9f));
						const FVector2D P2 = FVector2D(Origin.X + X * CellCm, Origin.Y + Y * CellCm) + Uphill * Rc.FRandRange(-200.f, 500.f)
							+ FVector2D(Rc.FRandRange(-600.f, 600.f), Rc.FRandRange(-600.f, 600.f));
						float G = 0.f;
						if (!GroundAt(P2, G))
						{
							continue;
						}
						R.At = FVector(P2.X, P2.Y, G + R.Half.Z * 0.45f);         // a little sunk in
						R.Rot = FRotator(Rc.FRandRange(-12.f, 12.f), Rc.FRandRange(0.f, 360.f), Rc.FRandRange(-12.f, 12.f));
						Put(R);
						++Boulders;
					}
				}
			}
			// A fin: steep, high (the top third of the island's height), now and then; along the slope's contour.
			else if (Slope > 28.f && Slope < 60.f && H[Idx(X, Y)] > 0.5f * MaxH && Fins < 220 && Rc.FRand() < 0.05f)
			{
				// A crag: two to four rough blocks leaning together out of the slope, 2 to 4.5 m tall, the tallest first.
				const FVector2D Along = FVector2D(-Grad.Y, Grad.X).GetSafeNormal();
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
				const int32 Count = Rc.RandRange(2, 4);
				for (int32 c = 0; c < Count; ++c)
				{
					FBFRock R;
					R.Kind = 2;
					R.Seed = int32(Rc.Next() & 0xffff);
					const float Tall = Rc.FRandRange(100.f, 220.f) * (c == 0 ? 1.f : Rc.FRandRange(0.5f, 0.85f));
					R.Half = FVector(Tall * Rc.FRandRange(0.6f, 0.95f), Tall * Rc.FRandRange(0.45f, 0.7f), Tall);
					const FVector2D P2 = FVector2D(Origin.X + X * CellCm, Origin.Y + Y * CellCm) + Along * Rc.FRandRange(-250.f, 250.f) + FVector2D(Rc.FRandRange(-120.f, 120.f), Rc.FRandRange(-120.f, 120.f));
					float G = 0.f;
					if (!GroundAt(P2, G))
					{
						continue;
					}
					R.At = FVector(P2.X, P2.Y, G + Tall * 0.45f);
					R.Rot = FRotator(Rc.FRandRange(-22.f, 22.f), Yaw + Rc.FRandRange(-30.f, 30.f), Rc.FRandRange(-15.f, 15.f));
					Put(R);
				}
				++Fins;
			}
		}
	}
	int32 Tris = 0;
	IslandRockGroups = 0;
	for (int32 t = 0; t < ByTile.Num(); ++t)
	{
		if (ByTile[t].Num() == 0)
		{
			continue;
		}
		for (const FBFRock& R : ByTile[t])
		{
			ClearTreesNear(FVector2D(R.At), float(FMath::Max(R.Half.X, R.Half.Y)) + 150.f, true);      // nothing grows through a rock
		}
		const int32 G = AddRocks(ByTile[t]);
		if (RockMeshes.IsValidIndex(G))
		{
			Tris += RockMeshes[G]->GetProcMeshSection(0) ? RockMeshes[G]->GetProcMeshSection(0)->ProcIndexBuffer.Num() / 3 : 0;
		}
		IslandRockGroups = RockMeshes.Num();
	}
	RockSummary = FString::Printf(TEXT("%d boulders and %d crags (%d triangles, in %.2f s)"), Boulders, Fins, Tris, FPlatformTime::Seconds() - T0);
}

void ABFWorld::GrowPlants(int32 KV)
{
	const EBFPlant P = EBFPlant(KV / BFPlants::Variants);
	const FBFPlantKind& K = BFPlants::Kind(P);
	if (Spots[KV].Num() == 0)
	{
		return;
	}
	UStaticMesh* Mesh = BFPlants::Mesh(P, KV % BFPlants::Variants, PlantMat);
	UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Comp->SetStaticMesh(Mesh);
	Comp->SetNumCustomDataFloats(1);         // how much of it has been eaten (the plant material shrinks its leaves)
	Comp->SetCastShadow(K.bShadow);
	Comp->SetCullDistances(int32(0.85f * K.CullCm), int32(K.CullCm));
	Comp->WorldPositionOffsetDisableDistance = 9000;     // the wind's sway only near the camera
	Comp->SetCanEverAffectNavigation(false);
	const bool bBlocks = Mesh && Mesh->GetBodySetup() != nullptr;
	Comp->SetCollisionEnabled(bBlocks ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bBlocks)
	{
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
	Comp->SetupAttachment(GetRootComponent());
	Comp->RegisterComponent();
	TArray<FTransform> Xf;
	Xf.Reserve(Spots[KV].Num());
	for (const FBFPlantSpot& S : Spots[KV])
	{
		const FQuat Lean = FQuat::FindBetweenNormals(FVector::UpVector, FVector(S.Normal));
		Xf.Add(FTransform(Lean * FQuat(FVector::UpVector, S.Yaw), Origin + FVector(S.Pos), FVector(S.Scale)));
	}
	Comp->AddInstances(Xf, false, true, false);
	PlantComps[KV] = Comp;
}

void ABFWorld::TickFoam(float DeltaSeconds)
{
	// Foam where a river tumbles (Raven's list, 35): near the camera, every river point whose water falls over 70 cm to
	// the point next to it downstream throws up white water now and then, more the further it falls.
	FoamClock -= DeltaSeconds;
	APlayerCameraManager* Cam = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->PlayerCameraManager : nullptr;
	if (FoamClock > 0.f || !Cam)
	{
		return;
	}
	FoamClock = 0.2f;
	ABFEffects* E = ABFEffects::Get(GetWorld());
	if (!E)
	{
		return;
	}
	const FVector C = Cam->GetCameraLocation();
	// At a waterfall's foot within 200 m: spray and mist thrown up, more the bigger it is.
	for (const FBFFall& Fa : Falls)
	{
		static const bool bNoCurtain = FParse::Param(FCommandLine::Get(), TEXT("bfnocurtain"));          // (a test: the rock behind it seen bare)
		if (!bNoCurtain && FVector::Dist(Fa.Bottom, C) < 20000.f && FMath::FRand() < FMath::Min(0.25f * Fa.DropM, 1.f))
		{
			for (int32 k = 0; k < FMath::Clamp(int32(Fa.DropM / 4.f), 1, 4); ++k)
			{
				const FVector At = Fa.Bottom + FVector::CrossProduct(Fa.Dir, FVector::UpVector) * FMath::FRandRange(-0.5f, 0.5f) * Fa.WidthCm + Fa.Dir * FMath::FRandRange(-100.f, 300.f);
				E->Foam(At, FMath::Clamp(60.f * FMath::Sqrt(Fa.DropM), 120.f, 450.f));
			}
		}
	}
	const int32 CX = FMath::RoundToInt((C.X - Origin.X) / CellCm), CY = FMath::RoundToInt((C.Y - Origin.Y) / CellCm);
	const int32 R = 15;          // 60 m round it
	for (int32 Y = FMath::Max(CY - R, 1); Y <= FMath::Min(CY + R, N - 2); ++Y)
	{
		for (int32 X = FMath::Max(CX - R, 1); X <= FMath::Min(CX + R, N - 2); ++X)
		{
			const int32 I = Idx(X, Y);
			if (Biome[I] != uint8(EBFBiome::River) || !(W[I] > H[I] + 0.05f))
			{
				continue;
			}
			float Fall = 0.f;
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 J = Idx(X + D.X, Y + D.Y);
				if (W[J] > H[J] + 0.05f)
				{
					Fall = FMath::Max(Fall, W[I] - W[J]);
				}
			}
			if (Fall > 0.7f && FMath::FRand() < FMath::Min(0.25f * Fall, 0.9f))
			{
				E->Foam(Origin + FVector(X * CellCm, Y * CellCm, W[I] * 100.f - 0.3f * Fall * 100.f), 80.f + 40.f * FMath::Min(Fall, 3.f));
			}
		}
	}
}

void ABFWorld::TickPlants(float DeltaSeconds)
{
	// Eaten plants grow back: once a second, each a share of its whole a minute (the plant material shows how much).
	RegrowClock += DeltaSeconds;
	if (RegrowClock < 1.f || Regrowing.Num() == 0)
	{
		RegrowClock = FMath::Min(RegrowClock, 1.f);
		return;
	}
	const float Dt = RegrowClock;
	RegrowClock = 0.f;
	TSet<int32> Touched;
	for (int32 i = Regrowing.Num() - 1; i >= 0; --i)
	{
		const FIntPoint Ref = Regrowing[i];
		const FBFPlantKind& K = BFPlants::Kind(EBFPlant(Ref.X / BFPlants::Variants));
		const FBFPlantSpot& S = Spots[Ref.X][Ref.Y];
		const float Whole = K.FoodKg * S.Scale * S.Scale;
		float& F = Food[Ref.X][Ref.Y];
		F = FMath::Min(Whole, F + Whole * K.RegrowPerMin * RegrowRate * Dt / 60.f);
		if (PlantComps[Ref.X])
		{
			PlantComps[Ref.X]->SetCustomDataValue(Ref.Y, 0, 1.f - F / Whole, false);
			Touched.Add(Ref.X);
		}
		if (F >= Whole)
		{
			Regrowing.RemoveAtSwap(i);
		}
	}
	for (const int32 KV : Touched)
	{
		PlantComps[KV]->MarkRenderStateDirty();
	}
}

float ABFWorld::EatPlants(const FVector& Snout, float ReachCm, float TopZ, float WantKg, FString& What, float& LeftKg)
{
	if (!bReady || GridN == 0)
	{
		return 0.f;
	}
	const FVector2f At(float(Snout.X - Origin.X), float(Snout.Y - Origin.Y));
	const int32 CX = int32(At.X / 1600.f), CY = int32(At.Y / 1600.f);
	const int32 Span = FMath::CeilToInt((ReachCm + 700.f) / 1600.f);
	FIntPoint Best(INDEX_NONE, INDEX_NONE);
	float BestGap = 1e9f;
	for (int32 GY = FMath::Max(CY - Span, 0); GY <= FMath::Min(CY + Span, GridN - 1); ++GY)
	{
		for (int32 GX = FMath::Max(CX - Span, 0); GX <= FMath::Min(CX + Span, GridN - 1); ++GX)
		{
			for (const FIntPoint& Ref : PlantGrid[GY * GridN + GX])
			{
				if (Food[Ref.X][Ref.Y] < 0.05f)
				{
					continue;
				}
				const FBFPlantKind& K = BFPlants::Kind(EBFPlant(Ref.X / BFPlants::Variants));
				const FBFPlantSpot& S = Spots[Ref.X][Ref.Y];
				// Its leaves: within FoodRadius of its middle, between FoodLow and FoodHigh up it (as big as it is).
				const float Gap = FVector2f::Distance(At, FVector2f(S.Pos.X, S.Pos.Y)) - K.FoodRadiusCm * S.Scale;
				const float Low = float(Origin.Z) + S.Pos.Z + K.FoodLowCm * S.Scale;
				if (Gap > ReachCm || Low > TopZ || Gap >= BestGap)
				{
					continue;
				}
				BestGap = Gap;
				Best = Ref;
			}
		}
	}
	if (Best.X == INDEX_NONE)
	{
		return 0.f;
	}
	const FBFPlantKind& K = BFPlants::Kind(EBFPlant(Best.X / BFPlants::Variants));
	const FBFPlantSpot& S = Spots[Best.X][Best.Y];
	const float Whole = K.FoodKg * S.Scale * S.Scale;
	float& F = Food[Best.X][Best.Y];
	const float Took = FMath::Min(F, WantKg);
	F -= Took;
	if (PlantComps[Best.X])
	{
		PlantComps[Best.X]->SetCustomDataValue(Best.Y, 0, 1.f - F / Whole, true);
	}
	Regrowing.AddUnique(Best);
	What = K.Name;
	LeftKg = F;
	return Took;
}

int32 ABFWorld::NumPlants() const
{
	int32 Sum = 0;
	for (const TArray<FBFPlantSpot>& S : Spots)
	{
		Sum += S.Num();
	}
	return Sum;
}

FString ABFWorld::PlantSummary() const
{
	FString Out = FString::Printf(TEXT("%d plants:"), NumPlants());
	for (int32 k = 0; k < BFPlants::Kinds; ++k)
	{
		int32 Count = 0, Tris = 0;
		for (int32 v = 0; v < BFPlants::Variants; ++v)
		{
			Count += Spots[k * BFPlants::Variants + v].Num();
			Tris = FMath::Max(Tris, BFPlants::Triangles(EBFPlant(k), v));
		}
		Out += FString::Printf(TEXT("%s %s %d (%d tris)"), k == 0 ? TEXT("") : TEXT(","), BFPlants::Kind(EBFPlant(k)).Name, Count, Tris);
	}
	return Out;
}

bool ABFWorld::NearestPlant(const FVector2D& Near, EBFPlant Kind, FVector& At) const
{
	double Best = 1e18;
	for (int32 v = 0; v < BFPlants::Variants; ++v)
	{
		for (const FBFPlantSpot& S : Spots[int32(Kind) * BFPlants::Variants + v])
		{
			const double D = FVector2D::DistSquared(Near, FVector2D(Origin.X + S.Pos.X, Origin.Y + S.Pos.Y));
			if (D < Best)
			{
				Best = D;
				At = Origin + FVector(S.Pos);
			}
		}
	}
	return Best < 1e18;
}

bool ABFWorld::RiverMouth(const FVector2D& Near, FVector& At, FVector& Seaward) const
{
	// The river points next to the sea (one point from it); the nearest to Near, and the way to the sea points round it.
	double Best = 1e18;
	int32 BestC = INDEX_NONE;
	for (int32 Y = 3; Y < N - 3; ++Y)
	{
		for (int32 X = 3; X < N - 3; ++X)
		{
			const int32 C = Idx(X, Y);
			if (Biome[C] != uint8(EBFBiome::River) || SeaSteps[C] != 1)
			{
				continue;
			}
			const double D = FVector2D::DistSquared(Near, FVector2D(Origin.X + X * CellCm, Origin.Y + Y * CellCm));
			if (D < Best)
			{
				Best = D;
				BestC = C;
			}
		}
	}
	if (BestC == INDEX_NONE)
	{
		return false;
	}
	const int32 X = BestC % N, Y = BestC / N;
	FVector Sum = FVector::ZeroVector;
	for (int32 dy = -3; dy <= 3; ++dy)
	{
		for (int32 dx = -3; dx <= 3; ++dx)
		{
			if (SeaSteps[Idx(X + dx, Y + dy)] == 0)
			{
				Sum += FVector(dx, dy, 0.0);
			}
		}
	}
	Seaward = Sum.GetSafeNormal();
	At = Origin + FVector(X * CellCm, Y * CellCm, 100.f * FMath::Max(W[BestC], 0.f));
	return true;
}

// ---------------------------------------------------------------------------------------------- for the animals

bool ABFWorld::FindPlant(const FVector& Near, float RadiusCm, float TopZ, FVector& At, float& LeafReachCm, float& FoodKg, float MinZ, float* OutLowZ, float* OutHighZ) const
{
	if (!bReady || GridN == 0)
	{
		return false;
	}
	const FVector2f P(float(Near.X - Origin.X), float(Near.Y - Origin.Y));
	const int32 CX = int32(P.X / 1600.f), CY = int32(P.Y / 1600.f), Span = FMath::CeilToInt(RadiusCm / 1600.f) + 1;
	float Best = 1e18f;
	for (int32 GY = FMath::Max(CY - Span, 0); GY <= FMath::Min(CY + Span, GridN - 1); ++GY)
	{
		for (int32 GX = FMath::Max(CX - Span, 0); GX <= FMath::Min(CX + Span, GridN - 1); ++GX)
		{
			for (const FIntPoint& Ref : PlantGrid[GY * GridN + GX])
			{
				const float F = Food[Ref.X][Ref.Y];
				if (F < 0.3f)
				{
					continue;
				}
				const FBFPlantKind& K = BFPlants::Kind(EBFPlant(Ref.X / BFPlants::Variants));
				const FBFPlantSpot& S = Spots[Ref.X][Ref.Y];
				if (float(Origin.Z) + S.Pos.Z + K.FoodLowCm * S.Scale > TopZ || float(Origin.Z) + S.Pos.Z + K.FoodHighCm * S.Scale < MinZ)
				{
					continue;          // its leaves out of reach above, or below where this mouth gets down to
				}
				const float Dist = FVector2f::Distance(P, FVector2f(S.Pos.X, S.Pos.Y));
				// Nearer first, but a plant worth more is worth a few steps more.
				const float Score = Dist - 150.f * FMath::Min(F, 20.f) / 20.f;
				if (Dist <= RadiusCm && Score < Best)
				{
					Best = Score;
					At = Origin + FVector(S.Pos);
					LeafReachCm = K.FoodRadiusCm * S.Scale;
					FoodKg = F;
					if (OutLowZ)
					{
						*OutLowZ = float(Origin.Z) + S.Pos.Z + K.FoodLowCm * S.Scale;
					}
					if (OutHighZ)
					{
						*OutHighZ = float(Origin.Z) + S.Pos.Z + K.FoodHighCm * S.Scale;
					}
				}
			}
		}
	}
	return Best < 1e17f;
}

bool ABFWorld::NearestShore(const FVector& Near, float RadiusCm, FVector& Stand, FVector& Water) const
{
	if (!bLandDone)
	{
		return false;
	}
	const int32 CX = FMath::RoundToInt((Near.X - Origin.X) / CellCm), CY = FMath::RoundToInt((Near.Y - Origin.Y) / CellCm);
	const int32 R = FMath::CeilToInt(RadiusCm / CellCm);
	auto Fresh = [&](int32 C) { return W[C] > 0.05f && W[C] > H[C] + 0.1f && SeaSteps[C] > 0; };
	int64 Best = MAX_int64;
	for (int32 Y = FMath::Max(CY - R, 1); Y <= FMath::Min(CY + R, N - 2); ++Y)
	{
		for (int32 X = FMath::Max(CX - R, 1); X <= FMath::Min(CX + R, N - 2); ++X)
		{
			const int32 C = Idx(X, Y);
			const int64 D = int64(X - CX) * (X - CX) + int64(Y - CY) * (Y - CY);
			if (D >= Best || Fresh(C) || H[C] < 0.f || W[C] > H[C])
			{
				continue;
			}
			const float GX = (H[C + 1] - H[C - 1]) / (2.f * CellCm / 100.f), GY = (H[C + N] - H[C - N]) / (2.f * CellCm / 100.f);
			if (GX * GX + GY * GY > 0.13f)       // steeper than ~20 deg: no way down to the water
			{
				continue;
			}
			for (const int32 NC : { C + 1, C - 1, C + N, C - N })
			{
				if (Fresh(NC))
				{
					Best = D;
					Stand = Origin + FVector(X * CellCm, Y * CellCm, H[C] * 100.f);
					Water = Origin + FVector((NC % N) * CellCm, (NC / N) * CellCm, W[NC] * 100.f);
					break;
				}
			}
		}
	}
	return Best != MAX_int64;
}

bool ABFWorld::Walkable(const FVector& At, const FBFSpecies& Body, float WadeCm) const
{
	const float GX = float((At.X - Origin.X) / CellCm), GY = float((At.Y - Origin.Y) / CellCm);
	if (!bLandDone || GX < 1.f || GY < 1.f || GX > N - 2 || GY > N - 2)
	{
		return false;
	}
	const int32 C = Idx(FMath::RoundToInt(GX), FMath::RoundToInt(GY));
	const float Depth = FMath::Max(W[C], 0.f) - H[C];
	const bool bSwims = Body.Swim == EBFSwim::Swims || Body.Swim == EBFSwim::HeadUnder;
	if (Depth * 100.f > WadeCm && !bSwims)
	{
		return false;
	}
	if (RockCells.IsValidIndex(C) && RockCells[C] > 0)
	{
		return false;          // a rock stands there
	}
	const float SX = (H[C + 1] - H[C - 1]) / (2.f * CellCm / 100.f), SY = (H[C + N] - H[C - N]) / (2.f * CellCm / 100.f);
	return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(SX * SX + SY * SY))) <= Body.ClimbMaxDeg;
}

bool ABFWorld::FindPath(const FVector& From, const FVector& To, const FBFSpecies& Body, float WadeCm, TArray<FVector>& Out, int32 MaxNodes) const
{
	// A* over every other grid point (8 m steps, 513 x 513). A step costs its time for this body: its length over the
	// body's pace on that slope (ClimbPaceAt, from its own climb test; none steeper than ClimbMaxDeg), slower through
	// water it wades, much slower swimming (if it swims at all), and slower through forest the wider it is (its trunks).
	Out.Reset();
	if (!bLandDone)
	{
		return false;
	}
	constexpr int32 S = 2;
	const int32 M = (N - 1) / S + 1;
	auto Node = [&](const FVector& P)
	{
		return FIntPoint(FMath::Clamp(FMath::RoundToInt((P.X - Origin.X) / (CellCm * S)), 1, M - 2), FMath::Clamp(FMath::RoundToInt((P.Y - Origin.Y) / (CellCm * S)), 1, M - 2));
	};
	const FIntPoint A = Node(From), B = Node(To);
	const bool bSwims = Body.Swim == EBFSwim::Swims || Body.Swim == EBFSwim::HeadUnder;
	const float SwimSlow = FMath::Max(FMath::Max(Body.Walk.SpeedCm, 0.5f * Body.Fast.SpeedCm) / FMath::Max(Body.SwimSpeedCm, 10.f), 1.f) * 1.5f;   // against how fast it goes on land
	const float Width = Body.Bounds.GetSize().Y;
	const float StepM = CellCm * S / 100.f;
	static thread_local TArray<float> G;
	static thread_local TArray<int32> Parent;
	static thread_local TArray<uint8> Closed;
	G.Init(1e30f, M * M);
	Parent.Init(-1, M * M);
	Closed.Init(0, M * M);
	struct FOpen { float F; int32 I; };
	TArray<FOpen> Heap;
	auto Less = [](const FOpen& X, const FOpen& Y) { return X.F < Y.F; };
	auto Cell = [&](int32 I) { return Idx((I % M) * S, (I / M) * S); };
	auto Heur = [&](int32 I) { return 1.15f * StepM * FVector2f::Distance(FVector2f(I % M, I / M), FVector2f(B.X, B.Y)); };
	const int32 Start = A.Y * M + A.X, Goal = B.Y * M + B.X;
	G[Start] = 0.f;
	Heap.HeapPush({ Heur(Start), Start }, Less);
	int32 Expanded = 0, Reached = INDEX_NONE, Closest = Start;
	float ClosestH = Heur(Start);
	while (Heap.Num() > 0 && Expanded < MaxNodes)
	{
		FOpen Top;
		Heap.HeapPop(Top, Less, EAllowShrinking::No);
		if (Closed[Top.I])
		{
			continue;
		}
		Closed[Top.I] = 1;
		++Expanded;
		if (Top.I == Goal)
		{
			Reached = Goal;
			break;
		}
		if (Heur(Top.I) < ClosestH)
		{
			ClosestH = Heur(Top.I);
			Closest = Top.I;
		}
		const int32 X = Top.I % M, Y = Top.I / M;
		const int32 C0 = Cell(Top.I);
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				const int32 NX = X + dx, NY = Y + dy;
				if ((dx | dy) == 0 || NX < 1 || NY < 1 || NX > M - 2 || NY > M - 2)
				{
					continue;
				}
				const int32 J = NY * M + NX;
				if (Closed[J])
				{
					continue;
				}
				const int32 C1 = Cell(J);
				const float Len = StepM * ((dx != 0 && dy != 0) ? 1.4142f : 1.f);
				const float Slope = FMath::RadiansToDegrees(FMath::Atan((H[C1] - H[C0]) / Len));
				if (FMath::Abs(Slope) > Body.ClimbMaxDeg)
				{
					continue;
				}
				float Cost = Len / FMath::Max(Body.ClimbPaceAt(Slope), 0.08f);
				if (RockCells.Num() > 0 && RockCells[C1] > 0)
				{
					Cost *= 4.f;          // rocks there: round them if there's another way (an 8 m step may still have room)
				}
				const float Depth = (FMath::Max(W[C1], 0.f) - H[C1]) * 100.f;
				if (Depth > WadeCm)
				{
					if (!bSwims)
					{
						continue;
					}
					Cost *= SwimSlow;
				}
				else if (Depth > 0.f)
				{
					Cost *= 1.f + Depth / FMath::Max(WadeCm, 1.f);
				}
				if (Crown.IsValidIndex(C1))
				{
					Cost *= 1.f + 2.5f * FMath::Min(Crown[C1], 1.5f) * FMath::Clamp(Width / 300.f, 0.3f, 3.f);
				}
				const float NG = G[Top.I] + Cost;
				if (NG < G[J])
				{
					G[J] = NG;
					Parent[J] = Top.I;
					Heap.HeapPush({ NG + Heur(J), J }, Less);
				}
			}
		}
	}
	// Not reached within the search: as far toward it as it got (it'll look again from there).
	const int32 End = Reached != INDEX_NONE ? Reached : Closest;
	TArray<int32> Rev;
	for (int32 I = End; I != -1; I = Parent[I])
	{
		Rev.Add(I);
	}
	for (int32 k = Rev.Num() - 1; k >= 0; k -= (k > 3 ? 3 : 1))      // every third step: the walker smooths between
	{
		const int32 I = Rev[k];
		const int32 C = Cell(I);
		Out.Add(Origin + FVector((I % M) * S * CellCm, (I / M) * S * CellCm, FMath::Max(H[C], 0.f) * 100.f));
	}
	if (Reached != INDEX_NONE)
	{
		Out.Add(To);
	}
	return Reached != INDEX_NONE;
}

float ABFWorld::CoverAt(const FVector2D& XY) const
{
	const int32 X = FMath::RoundToInt((XY.X - Origin.X) / CellCm), Y = FMath::RoundToInt((XY.Y - Origin.Y) / CellCm);
	return X >= 0 && Y >= 0 && X < N && Y < N && Crown.IsValidIndex(Idx(X, Y)) ? Crown[Idx(X, Y)] : 0.f;
}

float ABFWorld::Daylight() const
{
	return FMath::SmoothStep(-4.f, 8.f, 62.f * FMath::Sin((Hours - 6.f) / 24.f * 2.f * PI));
}

FString ABFWorld::ClockText() const
{
	const int32 Hr = int32(Hours), Mn = int32((Hours - Hr) * 60.f);
	const float L = Daylight();
	return FString::Printf(TEXT("Day %d, %02d:%02d (%s)"), Day, Hr, Mn, L > 0.9f ? TEXT("day") : L > 0.1f ? (Hours < 12.f ? TEXT("dawn") : TEXT("dusk")) : TEXT("night"));
}

bool ABFWorld::FreshWaterNear(const FVector& At, float RadiusCm, FVector& Water) const
{
	if (!bLandDone)
	{
		return false;
	}
	const int32 CX = FMath::RoundToInt((At.X - Origin.X) / CellCm), CY = FMath::RoundToInt((At.Y - Origin.Y) / CellCm);
	const int32 R = FMath::CeilToInt(RadiusCm / CellCm);
	float Best = 1e9f;
	for (int32 Y = FMath::Max(CY - R, 0); Y <= FMath::Min(CY + R, N - 1); ++Y)
	{
		for (int32 X = FMath::Max(CX - R, 0); X <= FMath::Min(CX + R, N - 1); ++X)
		{
			const int32 C = Idx(X, Y);
			if (W[C] > 0.05f && W[C] > H[C] + 0.05f && SeaSteps[C] > 0)
			{
				const FVector P = Origin + FVector(X * CellCm, Y * CellCm, W[C] * 100.f);
				const float D = FVector::Dist2D(P, At);
				if (D < Best && D <= RadiusCm + 0.75f * CellCm)
				{
					Best = D;
					Water = P;
				}
			}
		}
	}
	return Best < 1e8f;
}

// ---------------------------------------------------------------------------------------------- saving (life cycle step 3)

void ABFWorld::WriteState(TSharedRef<FJsonObject> Out) const
{
	Out->SetNumberField(TEXT("seed"), Seed);
	Out->SetNumberField(TEXT("day"), Day);
	Out->SetNumberField(TEXT("hours"), Hours);
	// Only the plants that have been eaten: kind-shape, which one, kg left (the rest are whole).
	TArray<TSharedPtr<FJsonValue>> Eaten;
	for (const FIntPoint& Ref : Regrowing)
	{
		if (Food[Ref.X].IsValidIndex(Ref.Y))
		{
			Eaten.Add(MakeShared<FJsonValueNumber>(Ref.X));
			Eaten.Add(MakeShared<FJsonValueNumber>(Ref.Y));
			Eaten.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Food[Ref.X][Ref.Y] * 100.f) / 100.f));
		}
	}
	Out->SetArrayField(TEXT("eaten"), Eaten);
}

void ABFWorld::ReadState(const TSharedPtr<FJsonObject>& In)
{
	if (!In.IsValid() || !bReady)
	{
		return;
	}
	double V = 0.0;
	if (In->TryGetNumberField(TEXT("day"), V))
	{
		Day = FMath::Max(1, int32(V));
	}
	if (In->TryGetNumberField(TEXT("hours"), V))
	{
		Hours = FMath::Fmod(float(V), 24.f);
	}
	const TArray<TSharedPtr<FJsonValue>>* Eaten = nullptr;
	if (!In->TryGetArrayField(TEXT("eaten"), Eaten))
	{
		return;
	}
	TSet<int32> Touched;
	for (int32 i = 0; i + 2 < Eaten->Num(); i += 3)
	{
		const int32 KV = int32((*Eaten)[i]->AsNumber()), I = int32((*Eaten)[i + 1]->AsNumber());
		if (KV < 0 || KV >= NumKV || !Food[KV].IsValidIndex(I))
		{
			continue;
		}
		const FBFPlantKind& K = BFPlants::Kind(EBFPlant(KV / BFPlants::Variants));
		const float Whole = K.FoodKg * Spots[KV][I].Scale * Spots[KV][I].Scale;
		Food[KV][I] = FMath::Clamp(float((*Eaten)[i + 2]->AsNumber()), 0.f, Whole);
		Regrowing.AddUnique(FIntPoint(KV, I));
		if (PlantComps[KV])
		{
			PlantComps[KV]->SetCustomDataValue(I, 0, 1.f - Food[KV][I] / FMath::Max(Whole, 0.001f), false);
			Touched.Add(KV);
		}
	}
	for (const int32 KV : Touched)
	{
		PlantComps[KV]->MarkRenderStateDirty();
	}
	UE_LOG(LogTemp, Display, TEXT("[BoneForge] island %d: back to %s, %d plants eaten and growing back"), Seed, *ClockText(), Regrowing.Num());
}

float ABFWorld::FoodWholeKg() const
{
	double Sum = 0.0;
	for (int32 KV = 0; KV < NumKV; ++KV)
	{
		const FBFPlantKind& K = BFPlants::Kind(EBFPlant(KV / BFPlants::Variants));
		for (const FBFPlantSpot& S : Spots[KV])
		{
			Sum += K.FoodKg * S.Scale * S.Scale;
		}
	}
	return float(Sum);
}

float ABFWorld::FoodLeftKg() const
{
	double Sum = 0.0;
	for (int32 KV = 0; KV < NumKV; ++KV)
	{
		for (const float F : Food[KV])
		{
			Sum += F;
		}
	}
	return float(Sum);
}

float ABFWorld::RegrowKgPerS(float MaxLowCm) const
{
	double Sum = 0.0;
	for (int32 KV = 0; KV < NumKV; ++KV)
	{
		const FBFPlantKind& K = BFPlants::Kind(EBFPlant(KV / BFPlants::Variants));
		for (const FBFPlantSpot& S : Spots[KV])
		{
			if (K.FoodLowCm * S.Scale <= MaxLowCm)
			{
				Sum += K.FoodKg * S.Scale * S.Scale * K.RegrowPerMin * RegrowRate / 60.0;
			}
		}
	}
	return float(Sum);
}

void ABFWorld::TourPoints(TArray<FVector>& At, TArray<FString>& Names) const
{
	// (2026-11-16, Raven: "do a terrain polish and check things from multiple angles")
	FRandomStream R(Seed + 99);
	auto Add = [&](int32 C, const TCHAR* Name)
	{
		At.Add(Origin + FVector((C % N) * CellCm, (C / N) * CellCm, FMath::Max(H[C], W[C] > H[C] ? W[C] : H[C]) * 100.f));
		Names.Add(Name);
	};
	const struct { EBFBiome B; const TCHAR* Name; } Kinds[] = { { EBFBiome::Beach, TEXT("beach") }, { EBFBiome::Plains, TEXT("plains") },
		{ EBFBiome::Forest, TEXT("forest") }, { EBFBiome::Swamp, TEXT("swamp") }, { EBFBiome::Rock, TEXT("rock") },
		{ EBFBiome::Lake, TEXT("lake") }, { EBFBiome::River, TEXT("river") } };
	for (const auto& Kd : Kinds)
	{
		TArray<int32> Got;
		for (int32 Tries = 0; Tries < 200000 && Got.Num() < 2; ++Tries)
		{
			const int32 X = R.RandRange(20, N - 21), Y = R.RandRange(20, N - 21), C = Idx(X, Y);
			if (Biome[C] != uint8(Kd.B))
			{
				continue;
			}
			// (well inside it: the same kind 3 points round)
			bool bIn = true;
			for (int32 dy = -3; dy <= 3 && bIn; dy += 3)
			{
				for (int32 dx = -3; dx <= 3 && bIn; dx += 3)
				{
					bIn = Biome[Idx(X + dx, Y + dy)] == uint8(Kd.B);
				}
			}
			if (bIn && !Got.ContainsByPredicate([&](int32 O) { return FMath::Abs(O % N - X) + FMath::Abs(O / N - Y) < 80; }))
			{
				Got.Add(C);
			}
		}
		for (int32 g = 0; g < Got.Num(); ++g)
		{
			Add(Got[g], *FString::Printf(TEXT("%s%d"), Kd.Name, g));
		}
	}
	auto IsSea = [&](int32 C) { return Biome[C] == uint8(EBFBiome::Sea); };
	auto IsLand = [&](int32 C) { return !IsSea(C) && Biome[C] != uint8(EBFBiome::Lake) && Biome[C] != uint8(EBFBiome::River); };
	int32 Peak = 0, Cliff = INDEX_NONE, Islet = INDEX_NONE;
	float CliffSl = 0.f, IsletH = 0.f;
	for (int32 Y = 2; Y < N - 2; ++Y)
	{
		for (int32 X = 2; X < N - 2; ++X)
		{
			const int32 C = Idx(X, Y);
			Peak = H[C] > H[Peak] ? C : Peak;
			const float U = 2.f * X / (N - 1) - 1.f, V = 2.f * Y / (N - 1) - 1.f;
			if (IsLand(C) && FMath::Sqrt(U * U + V * V) > 1.02f && H[C] > IsletH)
			{
				IsletH = H[C];
				Islet = C;
			}
			if (IsLand(C) && (IsSea(C + 1) || IsSea(C - 1) || IsSea(C + N) || IsSea(C - N)))
			{
				const float Sl = FMath::Max(FMath::Abs(H[C + 2] - H[C - 2]), FMath::Abs(H[C + 2 * N] - H[C - 2 * N]));
				if (Sl > CliffSl && FMath::Sqrt(U * U + V * V) < 1.f)
				{
					CliffSl = Sl;
					Cliff = C;
				}
			}
		}
	}
	Add(Peak, TEXT("peak"));
	if (Cliff != INDEX_NONE)
	{
		Add(Cliff, TEXT("seacliff"));
	}
	if (Islet != INDEX_NONE)
	{
		Add(Islet, TEXT("islet"));
	}
}

void ABFWorld::LogAround(const FVector& P, int32 R) const
{
	const int32 X0 = FMath::RoundToInt((P.X - Origin.X) / CellCm), Y0 = FMath::RoundToInt((P.Y - Origin.Y) / CellCm);
	for (int32 dy = -R; dy <= R; ++dy)
	{
		FString Row;
		for (int32 dx = -R; dx <= R; ++dx)
		{
			const int32 C = Idx(FMath::Clamp(X0 + dx, 0, N - 1), FMath::Clamp(Y0 + dy, 0, N - 1));
			Row += FString::Printf(TEXT(" %5.1f/%5.1f/%d"), H[C], W[C] > H[C] ? W[C] : -1.f, int32(Biome[C]));
		}
		UE_LOG(LogTemp, Display, TEXT("[BoneForge] around dy=%d:%s"), dy, *Row);
	}
}
