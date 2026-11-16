// Life on the island (design doc, World and AI tab, step 3): herds of plant eaters that think for themselves.
// Every animal is an ABFDino like the one you play, moved the way the companion is (move input, run), and everything its
// mind uses comes from its body: its mass, its walking and swimming speed, the slopes its legs climb (its own climb test),
// how high its eyes stand (how far it sees), what its head eats (FBFSpecies::Feed), how big a mouthful is.
//
// Needs: a stomach that fills with what it crops and empties as it digests (hunger), thirst, tiredness (more for
// running: its own winding, ABFDino::GetExertion). Senses: sight (range from its eyes' height; a blind spot behind; trees
// and hills block it), smell (carried on the island's wind: a meat eater upwind is smelled from far off, one downwind
// not at all), hearing (something big running near). Memory: where it found food and water, where it met danger.
// Choosing: each need gives each act a worth; it does the worthiest, with a little reluctance to drop what it's doing.
// Paths: ABFWorld::FindPath for its body (slopes it can climb, water it can wade or swim, forest the thicker the wider
// it is). Herds keep together (each keeps its distance, follows the others), call an alarm the others take up, and
// face a threat together when they outweigh it enough, or run.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "BFRuntimeImport.h"
#include "BFLife.generated.h"

class ABFDino;
class ABFWorld;
class UTextRenderComponent;

UENUM()
enum class EBFAct : uint8
{
	Idle, Graze, Drink, Rest, Flee, Defend, Regroup, Wander, Alert, Hunt, Feast, Retreat, Court, Nest, Count
};

struct FBFMemory
{
	FVector At = FVector::ZeroVector;
	float Value = 0.f;          // food: kg left there; water: 1; danger: how frightening
	float Time = 0.f;           // when (game seconds)
	uint8 Kind = 0;             // 0 food, 1 water, 2 danger
};

/** What an animal inherits (life cycle step 7): its size (its body scaled, its weight with it), its colours turned round
 *  the wheel and lighter or darker, its strength (how hard it hits, how it fares in a contest) and its speed, each a
 *  little either side of its kind's; a hatchling's the mean of its parents' and a little more either way. */
struct FBFGenes
{
	float Size = 1.f, Hue = 0.f, Shade = 0.f, Strength = 1.f, Speed = 1.f;
};

struct FBFAnimal
{
	TWeakObjectPtr<ABFDino> Body;
	FString Name;
	int32 Herd = 0;
	// Needs (0 none .. 1 desperate), and the stomach behind hunger.
	float StomachKg = 0.f, StomachMaxKg = 1.f, DigestKgPerS = 0.f;
	float Thirst = 0.f, Tired = 0.f, Fear = 0.f, Suspicion = 0.f;
	// From its body.
	float SightCm = 0.f, SmellCm = 0.f, HearCm = 0.f, MouthfulKg = 0.f, ReachCm = 0.f, Spacing = 0.f, WadeCm = 0.f, MassKg = 0.f;
	// What it's doing and where it's going.
	EBFAct Act = EBFAct::Idle;
	float ActSince = 0.f;
	FVector Goal = FVector::ZeroVector;
	bool bGoal = false;
	TArray<FVector> Path;
	int32 PathI = 0;
	FVector FoodAt = FVector::ZeroVector;      // the plant it's cropping
	float FoodReach = 0.f;
	float FoodLowZ = 0.f, FoodHighZ = 0.f;     // the height its leaves are at
	float SparS = 0.f;            // seconds into a sparring bout with a rival
	float NuzzleUntil = 0.f;      // nuzzling its young till then
	int32 NuzzleWho = -1;
	float CallIn = -1.f;          // seconds till it answers a herd-mate's alarm call (-1: none due)
	float CalledAt = -100.f;      // when it last called
	float MouthAheadCm = 0.f;                  // how far ahead of its middle its snout was, mouth down at food or water (learned)
	int32 CropN = 0;                           // mouthfuls taken: where in the patch the next one is cropped
	FVector MouthAt = FVector::ZeroVector;     // where its mouth is going (food, water, a kill), and how long it has been at it
	float MouthS = 0.f;
	bool bFood = false;
	FVector WaterAt = FVector::ZeroVector;     // the water it's drinking from
	float BiteClock = 0.f, ThinkIn = 0.f, StuckClock = 0.f, SideStep = 0.f;
	int32 Stucks = 0;
	FVector LastPos = FVector::ZeroVector, LastStep = FVector::ZeroVector;
	int32 SpeciesIndex = 0;
	float DownFor = 0.f;           // how long it has lain limp (it gets up again)
	int32 Falls = 0;
	// Danger: what it fears, where it was last sensed, how, and how long ago.
	TWeakObjectPtr<ABFDino> Threat;
	FVector ThreatAt = FVector::ZeroVector;
	float ThreatAgo = 1e9f;
	FString ThreatHow;
	bool bStandsGround = false;
	// Fighting (step 4): health 1 whole .. 0 dead; its armour (a share of each bite it shrugs off) and its weapon (how
	// hard it hits: horns, a tail club, teeth); its top speed (who can catch whom).
	bool bHunter = false, bDead = false, bYoung = false;
	int32 Mother = INDEX_NONE;     // a young one's mother
	// Growing up: its kind (the adult's key), its stage (1 hatchling, 2 juvenile, 3 subadult, 4 adult), its age (game
	// seconds since it hatched), how many times it has grown into a new body.
	FString Base;
	int32 Stage = 4;
	float AgeS = 0.f;
	int32 Growths = 0;
	// Breeding (life cycle steps 4-6): its sex; a female's clutch (carrying eggs, when she lays, when she's next in
	// condition), its father, her nest; a male's courting (whom, how long he has displayed, when he can court again, a
	// female he gave up on to a rival and until when).
	bool bFemale = false, bGravid = false;
	float LayIn = 0.f, ClutchIn = 0.f, MateIn = 0.f;
	int32 Father = INDEX_NONE, NestI = INDEX_NONE, CourtTarget = INDEX_NONE, IgnoreFemale = INDEX_NONE;
	int32 ClutchFather = INDEX_NONE;     // the father of the eggs she carries (Father is its own father)
	float DisplayS = 0.f, IgnoreUntil = 0.f, ScrapeS = 0.f, LookClock = 0.f;
	FVector NestSite = FVector::ZeroVector;
	int32 Matings = 0, Clutches = 0;
	// Acting like a real creature (2026-11-13): rank (contests won and lost), playing (the young, and with whom), lying
	// in wait at water (a hunter: where, till when), keeping watch for the herd.
	int32 Wins = 0, Losses = 0;
	bool bPlay = false, bWatch = false;
	int32 PlayWith = INDEX_NONE;
	FVector AmbushAt = FVector::ZeroVector;
	float AmbushUntil = 0.f;
	// Progress toward where it's going: the nearest it has come, and when (a walk that stops getting closer is given up).
	float ProgressDist = 1e9f, ProgressAt = 0.f;
	// Far from you (life cycle step 8): its body put away (hidden, still), a simpler life run once a second.
	bool bDormant = false;
	// You (life cycle step 9): this one is you (no mind of its own here; your needs come from your own).
	bool bPlayer = false;
	// A flier (step 10, the Pteranodons): its own mind (roosting, fishing, soaring); the fishing trip's pass count.
	bool bFlier = false;
	int32 Passes = 0;
	// Feeding a young one that can't fish yet: whom, and the fish it's carrying to it.
	int32 FeedFor = INDEX_NONE;
	float CarryKg = 0.f;
	float CircleAng = 0.f;
	int32 PerchI = INDEX_NONE;     // a flier's own perch (a nest tree's shelf, a ledge): where it roosts and nests
	float FarClock = 0.f;
	// The chances of the wild for the young: how much risk it can take (drawn once), how much it has run.
	float Fate = -1.f, Risk = 0.f;
	// Its condition: how well fed it has been lately (its stomach's fullness averaged over about an island hour of needs).
	float Condition = 0.6f;
	// Inherited (step 7): its genes and its generation (the first animals 0; a hatchling one more than its elder parent).
	FBFGenes Genes;
	int32 Generation = 0;
	// Brought back from a save, waiting for its body (grown or brought in first): which, where.
	bool bAwaiting = false;
	FString AwaitKey;
	FVector AwaitAt = FVector::ZeroVector;
	float AwaitYaw = 0.f;
	float Health = 1.f, Armour = 0.f, Weapon = 1.f, FastCm = 0.f, AttackClock = 0.f;
	// Hunting: the prey it has chosen, stalking (0) or charging (1), when the charge began, prey it gave up on (until when).
	int32 Prey = INDEX_NONE, HuntPhase = 0;
	bool bCreeping = false, bFollowing = false;
	float PreyKnownAt = -1e9f;
	float FollowSince = 0.f;
	FVector PreyLastAt = FVector::ZeroVector;
	float HuntClock = 0.f, ChargeAt = 0.f;
	TMap<int32, float> GaveUp;
	int32 Kills = 0, Bites = 0, HitsTaken = 0;
	TArray<FBFMemory> Memory;
	FString Thought;
	TObjectPtr<UTextRenderComponent> Label = nullptr;
	// What it has done (for the log).
	int32 Decisions = 0, Flights = 0, Drinks = 0, Paths = 0, Meals = 0;
	float EatenKg = 0.f, WalkedCm = 0.f;
};

/** What's left of a kill: meat for hunters and scavengers, going off in time. A nest's eggs are one too (Nest). */
struct FBFCarcass
{
	FVector At = FVector::ZeroVector;
	float MeatKg = 0.f;
	float Time = 0.f;
	FString Of;
	int32 Nest = INDEX_NONE;
	// Drawn (an animal's, not eggs): blood pooling under it, then, eaten down, its skeleton as it lay, meat on the bones
	// at first, bleaching, sinking away in the end.
	int32 Animal = INDEX_NONE;
	float FleshZ = 0.f;          // the height of its trunk's middle as it lies (0: not known)
	float StartKg = 0.f, LenCm = 0.f, DrawClock = 0.f, BonesAt = -1.f;
	TObjectPtr<class UInstancedStaticMeshComponent> Pools = nullptr, Bones = nullptr;
	TObjectPtr<class UMaterialInstanceDynamic> PoolMat = nullptr, BoneMat = nullptr;
	TArray<FTransform> BoneXf;          // the skeleton's pieces as laid (world)
	FTransform SkullXf = FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector);   // the skull (an egg, the engine's sphere)
	TObjectPtr<class UInstancedStaticMeshComponent> Skull = nullptr;
};

/** A nest (life cycle step 5): where, whose, its eggs (how many, each how heavy), how far along they are (game seconds
 *  of the hour they take), how its mother cares for it (0 lays and leaves, 1 guards, 2 broods), the eggs drawn. */
struct FBFNest
{
	FVector At = FVector::ZeroVector;
	FString Base;
	int32 Herd = 0, Mother = INDEX_NONE, Father = INDEX_NONE;
	int32 Eggs = 0, Laid = 0, Hatched = 0, Eaten = 0;
	float EggKg = 1.f, IncubS = 0.f, HatchClock = 0.f;
	int32 Care = 1;
	int32 Carcass = INDEX_NONE;
	int32 YoungHerd = INDEX_NONE;     // lay-and-leave hatchlings keep together as their own group
	TObjectPtr<class UInstancedStaticMeshComponent> EggComp = nullptr;
	TObjectPtr<class UStaticMeshComponent> Scrape = nullptr;
};

struct FBFHerd
{
	FString Kind;
	TArray<int32> Members;
	int32 Leader = 0;
	FVector Home = FVector::ZeroVector;
	int32 YoungBorn = 0;
	float MovedAt = -1e9f;      // when it last moved on to fresh grazing
	// Its range as it learns it: grazing grounds it has used (and when it last ate them out), the water it knows.
	TArray<FVector> Patches;
	TArray<float> PatchAt;
	TArray<FVector> Waters;
};

UCLASS()
class ABFLife : public AActor
{
	GENERATED_BODY()

public:
	ABFLife();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Puts herds of the plant eaters among the six on the island, a few hundred metres round Near. */
	void Populate(ABFWorld* World, const FVector& Near);
	/** You biting (E): whoever your snout reaches takes a bite as one of the hunters' would. What happened, or empty. */
	FString PlayerBite(ABFDino* Player);
	/** You eating (E, a meat eater): a mouthful from a carcass in reach. What happened, or empty. */
	FString PlayerEat(ABFDino* Player, float* OutKg = nullptr);
	/** Your health against the animals that fight back (1 whole .. 0 dead), and what last happened to you. */
	float PlayerHealth = 1.f;
	FString PlayerNote;
	double PlayerNoteUntil = 0.0;
	int32 NumCarcasses() const { return Carcasses.Num(); }
	FVector CarcassAt(int32 I) const { return Carcasses.IsValidIndex(I) ? Carcasses[I].At : FVector::ZeroVector; }
	/** The hunters among them (step 4): put in with the herds (Populate). */
	bool bHunters = true;
	/** The young (two to each plant eater herd, their bodies grown at 30% the first time; their mother guards them). */
	bool bYoung = true;
	static FString YoungKey(const FString& Base);
	/** A growth stage's body (1 hatchling, 2 juvenile, 3 subadult): its share of the adult's length and head size, its key. */
	static bool StageBody(const FString& Base, int32 Stage, float& Size, int32& HeadPct);
	static FString StageKey(const FString& Base, int32 Stage);
	static FString StageName(int32 Stage);
	/** Growing up runs this many times faster (the checks). */
	float GrowRate = 1.f;
	int32 BodiesWaiting() const { return BodyQueue.Num(); }
	/** Two young at a stage for a herd (the checks; breeding will lay eggs). */
	void WantYoung(int32 HerdI, int32 Stage);
	/** Asks for a body ahead of need (grown and brought in, in turn). */
	void Prefetch(const FString& Key);
	int32 YoungNumber = 0;
	int32 YoungWaiting() const { return YoungWanted.Num(); }
	void Clear();
	/** Saving (life cycle step 3): every animal living (its body, family, age, needs, health, memory), the herds, the
	 *  carcasses; and back again (each animal waits for its body if that has to be brought in first). */
	TSharedRef<class FJsonObject> SaveState() const;
	void LoadState(ABFWorld* World, const TSharedPtr<class FJsonObject>& In);
	int32 NumAwaiting() const;
	int32 NumAlive() const;
	/** Breeding (life cycle steps 4-6). Game seconds between a female's clutches, from mating to laying, in the egg. */
	float BreedEvery = 2880.f, Gestation = 600.f, Incubation = 3600.f;
	/** No female comes into condition while this many live (what the game can run: -bfstress, 2026-11-13). */
	int32 MaxAnimals = 450;
	/** How many of the young die by the chances of the wild (0 none: the growing-up check). */
	float YoungDeathRate = 1.f;
	int32 Births = 0, EggsLaid = 0, EggsEaten = 0, NumMatings = 0, Contests = 0, DiedYoung = 0, Starved = 0, OldAge = 0;
	/** Game seconds of life on the growth clock (30 hours: about three times growing up); 0 none. */
	float Lifespan = 108000.f;
	/** CSV rows, one a kind (the many-generation run): time, kind, alive, adults, mean and most generation, each gene's mean and spread. */
	FString GeneRows(float Clock) const;
	/** Balance (no kind crowds the others out, none dies out): the hunters together get HunterShare of the room
	 *  (MaxAnimals), the plant eaters the rest, each kind an equal part of its side's; a kind down to its last one, or to
	 *  no adults of one sex, gets a newcomer from across the island (fresh genes) after ImmigrateS on the growth clock. */
	float HunterShare = 0.15f, ImmigrateS = 3600.f;
	int32 Newcomers = 0;
	int32 Charges = 0, HerdMoves = 0, Splits = 0, Ambushes = 0;
	/** Far from you (life cycle step 8): beyond FarCm an animal's body is put away and it lives a simpler life (needs,
	 *  grazing, water, its herd, hunting, breeding, growing: on the island's real plants and water, once a second); within
	 *  WakeCm it is back in full. 0: everyone in full. */
	float FarCm = 150000.f, WakeCm = 120000.f;
	int32 NumDormant() const;
	int32 FarKills = 0, FarMeals = 0, FarDrinks = 0;
	/** The Pteranodons (life cycle step 10): their colony on high ground by the sea; fish in shoals in the sea and lakes
	 *  near it (kg, growing back); how many dives and catches. */
	FVector Colony = FVector::ZeroVector;
	struct FShoal { FVector At = FVector::ZeroVector; float FishKg = 0.f, MaxKg = 0.f; };
	TArray<FShoal> Shoals;
	int32 Dives = 0, Catches = 0, Feeds = 0;
	/** The fish drawn (one instance a fish: circling under each shoal's surface, now and then one leaping). */
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> FishComp;
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> FinComp;
	void DrawFish(float Now);
	/** A young flier that can't fish for itself yet (a hatchling, or a body that can't fly): its parents feed it. */
	bool NeedsFeeding(const FBFAnimal& Y) const;
	bool bFliers = true;
	/** Where the fliers roost and nest, up off the ground (2026-11-14): broad shelf branches on the colony's giant
	 *  redwoods, and rock ledges on the steep high ground near. Top: the middle of its flat top; Out: away from the trunk
	 *  or the rock face (they land in from there); whose it is. */
	struct FPerch { FVector Top = FVector::ZeroVector; FVector Out = FVector(1.f, 0.f, 0.f); int32 Owner = INDEX_NONE; bool bLedge = false; bool bAlcove = false; };
	TArray<FPerch> Perches;
	int32 NestTrees = 0;
	int32 LedgeRocks = INDEX_NONE;       // the ledges' rocks on the island (a group of its rocks)
	/** The perch whose top is within RadiusCm of At (across) and not far below it, or none. */
	int32 PerchNear(const FVector& At, float RadiusCm) const;
	/** Built from the colony's place (the same each time: a load builds them again). */
	void BuildPerches();
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> BarkComp;
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> ShelfComp;
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> LeafComp;
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> StoneComp;
	UPROPERTY()
	TObjectPtr<class UInstancedStaticMeshComponent> SpireComp;
	int32 RoomFor(const FString& Kind) const;
	/** For the pictures: everyone holds still (minds and bodies' steering stop). */
	bool bFrozen = false;
	int32 NumNests() const { return Nests.Num(); }
	const FBFNest& GetNest(int32 I) const { return Nests[I]; }
	/** For the checks: the animal to change (needs, place). */
	FBFAnimal& Edit(int32 I) { return Animals[I]; }
	/** You in the life cycle (step 9): your place among them (a sex, a condition, clutches, young that keep to you), synced
	 *  each frame from your needs; X to court (a male, by a female of your kind in condition) or to lay (a female whose
	 *  eggs are due); and when you die, carrying on as your oldest living young. */
	bool bPlayerFemale = true;
	int32 PlayerI = INDEX_NONE;
	void SyncPlayer(ABFDino* Pawn, float Full, float PThirst, float PHealth, float Dt);
	FString PlayerAct(ABFDino* Pawn);
	FString PlayerStatus() const;
	int32 YourYoung() const;
	/** The young you carry on as (its body), or null; you are it from now on. */
	ABFDino* CarryOn(FString& Note);
	/** A body you grew into (growing up as one of your young): the controller takes it; then the old one goes. */
	TWeakObjectPtr<ABFDino> PendingPlayerBody, OldPlayerBody;
	int32 PlayerCourt = INDEX_NONE;
	float PlayerDisplayS = 0.f;
	/** For the checks: every adult female of a herd well fed and in condition now. */
	void MakeReady(int32 HerdI);
	/** For the quick nest check: a flier mother put on her perch with a clutch (by Father) hatching in HatchInS real
	 *  seconds, brooding it. False if she has no perch. */
	bool StageNest(int32 MotherI, int32 FatherI, float HatchInS);
	FString BreedingText() const;
	/** Why grown females aren't in condition, the males' state, the hunters' doings (the many-generation run's log). */
	FString WhyNotText() const;
	/** How many of each kind are at each act now (the day check). */
	FString ActsText() const;
	/** Genes for one of the first animals (a spread round its kind), and a hatchling's from its parents'. */
	static FBFGenes FounderGenes(FRandomStream& R);
	static FBFGenes ChildGenes(const FBFGenes& Mum, const FBFGenes& Dad, FRandomStream& R);
	static FString GenesText(const FBFGenes& G);
	/** A herd of Count of a kind (its adult key), Dist from Near toward Yaw (or the nearest dry, planted ground about
	 *  there). How many were put in. */
	int32 AddHerd(const FString& Key, int32 Count, const FVector& Near, float Dist, float Yaw, bool bHunter, FRandomStream& R, bool bLog = true);
	/** The cost of their minds (the population check): seconds spent in Tick, frames, the worst frame. */
	double TickSecs = 0.0, TickWorst = 0.0;
	int32 TickFrames = 0;
	int32 Num() const { return Animals.Num(); }
	/** (The checks) Killer kills Victim where it stands (a wound, a carcass): the carcass's index. */
	int32 TestKill(int32 Victim, int32 Killer);
	/** (The checks) Killer bites Victim for Share of its health. */
	void TestHurt(int32 Victim, int32 Killer, float Share) { if (Animals.IsValidIndex(Victim) && Animals.IsValidIndex(Killer)) { Hurt(Victim, Share, Animals[Killer].Name, Animals[Killer].Body.Get(), GetWorld()->GetTimeSeconds(), true); } }
	/** (The checks) A carcass eaten down to Left of its meat. */
	void TestEatDown(int32 C, float Left) { if (Carcasses.IsValidIndex(C)) { Carcasses[C].MeatKg = Left * Carcasses[C].StartKg; Carcasses[C].DrawClock = 0.f; } }
	int32 BonesShown() const { return CarcassBonesShown; }
	FString CallsText() const { return FString::Printf(TEXT("%d alarm calls, %d answers, %d startles, %d nuzzles, %d butts sparring"), AlarmCalls, AlarmAnswers, Startles, Nuzzles, Butts); }
	int32 PoolsShown() const { return CarcassPoolsShown; }
	int32 BonePieces(int32 C) const { return Carcasses.IsValidIndex(C) ? Carcasses[C].BoneXf.Num() : 0; }
	FVector BoneCentre(int32 C) const
	{
		FVector S = FVector::ZeroVector;
		if (!Carcasses.IsValidIndex(C) || Carcasses[C].BoneXf.Num() == 0) { return S; }
		for (const FTransform& T : Carcasses[C].BoneXf) { S += T.GetLocation(); }
		return S / Carcasses[C].BoneXf.Num();
	}
	const FBFAnimal& Get(int32 I) const { return Animals[I]; }
	bool IsValidAnimal(int32 I) const { return Animals.IsValidIndex(I) && Animals[I].Body.IsValid(); }
	FVector HerdCentre(int32 Herd) const;
	int32 NumHerds() const { return Herds.Num(); }
	/** Each animal's name, what it's doing, its needs and its thought (for the log). */
	FString Report() const;
	static const TCHAR* ActName(EBFAct A);
	/** The watch panel (O): the time, the counts, the kills, each animal's state and thought. */
	FString WatchText(const FString& Clock) const;
	/** Their thoughts shown over their heads (I key). */
	static bool bShowThoughts;
	/** Needs grow this many times faster (the checks: to see a day's needs in minutes). */
	float NeedRate = 1.f;

private:
	void Sense(FBFAnimal& A, float Now);
	void Think(FBFAnimal& A, int32 Index, float Now);
	void Do(FBFAnimal& A, int32 Index, float Dt, float Now);
	void Steer(FBFAnimal& A, int32 Index, float Dt);
	void Choose(FBFAnimal& A, EBFAct Act, float Now, const FString& Why);
	bool GoTo(FBFAnimal& A, const FVector& To);
	void Remember(FBFAnimal& A, uint8 Kind, const FVector& At, float Value, float Now);
	const FBFMemory* Recall(const FBFAnimal& A, uint8 Kind, const FVector& From) const;
	FVector Snout(const FBFAnimal& A) const;
	float HerdMassNear(const FBFAnimal& A, float RadiusCm) const;

	TArray<FBFAnimal> Animals;
	TArray<FBFHerd> Herds;
	TArray<TWeakObjectPtr<ABFDino>> Hunters;     // meat eaters on the island (refreshed each second)
	float HuntersClock = 0.f;
	TWeakObjectPtr<ABFWorld> Island;
	// Hunting (step 4).
	void HunterThink(FBFAnimal& A, int32 Index, float Now);
	void HunterDo(FBFAnimal& A, int32 Index, float Dt, float Now);
	int32 ChoosePrey(FBFAnimal& A, int32 Index, float Now, FString& Why);
	/** A bite's share of a victim's health: the attacker's weight against the victim's, its weapon, the victim's armour. */
	static float HitShare(float AttackerKg, float Weapon, const FBFAnimal& Victim);
	void Hurt(int32 Victim, float Share, const FString& By, ABFDino* Attacker, float Now, bool bScare = true);
	/** The next number for a new animal of a kind (no two of a kind share a name). */
	int32 NextNumber(const FString& Kind) const;
	int32 IndexOf(const ABFDino* Body) const;
	mutable TMap<const ABFDino*, int32> IndexCache;     // body -> animal (checked on use, filled on a miss)
	float PackKg(const FBFAnimal& A) const;
	int32 NearestCarcass(const FVector& At, float RadiusCm) const;
	int32 SmellCarcass(const FBFAnimal& A) const;
	const ABFDino* BiggerAt(const FBFAnimal& A, const FVector& At, float RadiusCm) const;
	TArray<FBFCarcass> Carcasses;
	int32 AddAnimal(int32 Index, int32 HerdI, const FVector& At, float Yaw, const FString& Name, bool bHunter, FRandomStream& R, bool bLog);
	void SpawnYoung(int32 HerdI, int32 SpeciesIndex, float Now);
	void TickYoung(float Now);
	int32 BodyIndex(const FString& Key);
	void ApplyBody(FBFAnimal& A);
	void Grow(FBFAnimal& A, int32 Index, float Dt, float Now);
	bool SpawnBody(FBFAnimal& A, int32 SpeciesI, const FVector& At, float Yaw);
	void SetDormant(FBFAnimal& A, bool bOn);
	void TickFar(FBFAnimal& A, int32 Index, float Dt, float Now);
	bool MoveFar(FBFAnimal& A, const FVector& To, float StepCm);
	void PlaceColony(const FVector& Near, FRandomStream& R);
	void FlierTick(FBFAnimal& A, int32 Index, float Dt, float Now);
	void FlyTo(FBFAnimal& A, const FVector& To, float AltCm, bool bLand);
	/** Its mouth to To (eating, drinking, at a kill): it turns to face it, steps up or back a little till its snout is
	 *  over it (its neck reaching down or up to it: ABFDino::SetMouthTarget). True once its snout is there (or it has
	 *  tried long enough: it eats from as near as it gets). */
	bool MouthTo(FBFAnimal& A, const FVector& To, float Dt);
	int32 PerchFor(FBFAnimal& A, int32 Index);
	FVector PerchSpot(int32 P, const class ABFDino* D, bool bVisitor) const;
	bool AtPerch(const FBFAnimal& A, int32 P) const;
	/** Up to perch P (a visitor beside its owner): off the ground first, out to it, in onto it. True once on it. */
	bool GoPerch(FBFAnimal& A, int32 P, bool bVisitor);
	/** Its genes on its body: scaled, its pace, its colours. */
	void ApplyGenes(FBFAnimal& A);
	// Breeding.
	bool IsReady(const FBFAnimal& F) const;
	void BreedScores(FBFAnimal& A, int32 Index, float Now, float* Score);
	void DoBreed(FBFAnimal& A, int32 Index, float Dt, float Now);
	void LayEggs(FBFAnimal& A, int32 Index, float Now);
	void TickNests(float Dt, float Now);
	/** The kill's look: blood pools spread under it (wet, then drying dark), and once it's eaten down past a third
	 *  (or its body is put away) the body gives way to its bones, laid as its bones lay. */
	void TickCarcasses(float Dt, float Now);
	void DressCarcass(int32 C, int32 Animal);
	void CarcassBones(FBFCarcass& K, const ABFDino& Body);
	int32 CarcassBonesShown = 0, CarcassPoolsShown = 0;
	int32 AlarmCalls = 0, AlarmAnswers = 0, Startles = 0, Nuzzles = 0, Butts = 0;          // (for the checks)          // (for the checks)
	void HatchOne(int32 NestI, float Now);
	void DrawEggs(FBFNest& N);
	void Toll(FBFAnimal& A, int32 Index, float Dt, float Now);
	void Die(int32 Index, const FString& How, float Now);
	TArray<FBFNest> Nests;
	TArray<int32> ReadyFemales;
	// Balance: each kind ever seen (and whether it hunts, its adult body), how many of it live, its newcomer clock.
	struct FKindInfo { bool bHunter = false; int32 SpeciesI = INDEX_NONE; int32 Alive = 0, Females = 0, Males = 0; float Wait = 0.f; };
	TMap<FString, FKindInfo> KindInfo;
	void TickBalance(float Dt, float Now);
	FString BreedWhy;
	int32 AliveNow = 0;
	float AliveClock = 0.f;
	void TickAwaiting();
	float AwaitClock = 0.f;
	TArray<FString> BodyQueue;     // bodies to grow, make ready and bring in, in turn
	TSet<FString> BodyFailed;
	TArray<TPair<FString, TArray<int32>>> YoungWanted;     // young bodies to grow, and the herds that want them
	FString YoungNow;
	int32 YoungStage = 0;          // 0 next, 1 growing, 2 making ready, 3 import queued, 4 importing
	FProcHandle YoungProc;
	TSharedPtr<UE::Interchange::FImportResult, ESPMode::ThreadSafe> YoungImport;
};
