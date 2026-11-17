# Bone Forge → Unreal: rig importer

Builds an Unreal **Skeleton** (plus a skinned Skeletal Mesh) from a rig JSON file, and checks
the body OBJ's scale and facing in Unreal. Works for all six species; the T. rex is the default.

## Run it in Unreal

1. Keep the folder layout from the zips: `bone_forge/rig_joints/`, `bone_forge/body_shapes/`,
   and this `bone_forge/unreal/` folder next to them.
2. In the Unreal Editor, open **Window > Output Log**, switch the input box from `Cmd` to `Python`,
   and run (use your own path, with forward slashes):

   ```
   py "C:/path/to/bone_forge/unreal/bone_forge_unreal.py"
   ```

   For another species add `--species raptor` (trike, raptor, ankylo, brachio, ptera).
   **File > Execute Python Script** also works, but then you get the T. rex.
3. Read the result in the Output Log (lines start with `[BoneForge]`) or in
   `unreal/out/trex_unreal_report.txt`.

Requirements: UE 5.3 or newer is the safe bet (glTF skeletal import through Interchange is on by
default there), with the **Python Editor Script Plugin** enabled (it is by default in UE 5).

What you get in `/Game/BoneForge/Trex/` (names as UE 5.8's Interchange importer creates them):

| Asset | What it is |
|---|---|
| `trex_skeletal/SkeletalMeshes/trex_skeletal` | Skeletal Mesh: the body volume bound to the rig |
| `trex_skeletal/SkeletalMeshes/trex_skeletal_Skeleton` | Skeleton: one bone per rig joint (39 for the T. rex), same names and hierarchy as the JSON |
| `trex_skeletal_PhysicsAsset` | Unreal's auto-generated physics asset (`bone_forge_physics.py` replaces it with `trex_physics`, see below) |
| material slots | `Skin`, `Scute`, `Eye`, `LidHalf`, `LidClosed`, `Keratin`, `Teeth`, `Mouth` (the Pteranodon also gets a two-sided `Membrane` slot); `bone_forge_materials.py` fills them, see "Skin, eyes and blink" |
| `ObjCheck/body_0`, `head_1`, `leg_l_2` … | the plain OBJ, one Static Mesh per OBJ part, imported only to check scale and facing |

Re-running replaces those assets. The script also briefly imports into `/Game/BoneForge/_AxisProbe`
and deletes that folder again (see "How it works").

## What the report tells you

**1. Body OBJ:** imported size, which way is up, which way the head points, and cm per OBJ
unit. Expected: `1210 x 156 x 433 cm`, *"upright (Z up), facing +X … No rotation or scale
needed."* If Unreal's OBJ importer didn't convert Y-up to Z-up, it says so (e.g. *"up is +Y"*)
and prints the import rotation that fixes it.

UE 5.8 (tested 2026-11-02) does **not** convert the OBJ: it comes in facing +X but with up = −Y
and the creature's right side toward +Z, at 1 unit = 1 cm. The fix the script prints, an import
rotation of **Roll 90**, is a true rotation, not a mirror: afterwards `leg_l` is at −Y, the head at
+X and up is +Z. This only affects the plain OBJ; the Skeletal Mesh comes in through glTF and
is already in rig axes.

Running headless also works (no editor window), but the commandlet splits `-script=` on
spaces, so call a small wrapper from a path without spaces:

```
UnrealEditor-Cmd.exe MyProject.uproject -run=pythonscript -script=C:/Dev/run_bf.py -unattended -nosplash
```

where `run_bf.py` sets `sys.argv` (e.g. `--species raptor`) and `exec`s `bone_forge_unreal.py`.

**2. Skeleton:** checks every bone's name, parent and position (to 0.1 cm) against the rig JSON,
and that each bone's X axis points at its child joint.

## How it works

| File | Runs in | Job |
|---|---|---|
| `bone_forge_gltf.py` | anywhere (standard-library Python 3.9+) | rig JSON + body OBJ → skinned `.glb` |
| `bone_forge_checks.py` | anywhere | works out what Unreal did from measured bounds / bones |
| `bone_forge_unreal.py` | Unreal Editor | imports, checks, reports |

Unreal only creates a Skeleton asset when it imports a skinned mesh, so the rig is baked into a
glTF file with the body bound to it, and Unreal's own glTF importer creates the assets. The
converter also runs from a shell:

```
python bone_forge_gltf.py ../rig_joints/trex_rig.json ../body_shapes/trex_body.obj out/trex_skeletal.glb
```

The ready-made `.glb` files in `out/` can also be dragged into the Content Browser by hand.

**Axis probe.** The `.glb` assumes Unreal maps glTF (x, y, z) metres to Unreal (x, z, y) × 100 cm,
which is a swap of Y and Z. The script imports into `_AxisProbe` first, measures the bones, and
if the importer used a different axis map it rebuilds the `.glb` to match before the real
import. So the final skeleton lands in rig axes either way: X forward, Y right, Z up, origin
on the ground under the hips.

## Bone axes (for animation and physics constraints)

Each bone's **local X points at its child joint**, and **local Y stays toward the creature's right**.
The rig's motion limits then map onto Unreal rotations like this (also stored on each bone in
the `.glb` as `unreal_bend` / `unreal_side`):

| Rig JSON | Unreal local rotation |
|---|---|
| bend | **Pitch**. `+bend` = `+Pitch` for neck, spine, head, hip, shoulder, ankle, toes; `−Pitch` for tail, calf (knee), hand (wrist), jaw. |
| side | **Yaw**. Neck/spine/head: `+side` (nose to the creature's left) = `−Yaw`. Tail: `+side` (tip to its right) = `+Yaw`. Limbs: `+side` (away from the body) = `−Yaw` on `_l`, `+Yaw` on `_r`. |
| twist | **Roll** (around the bone) |

`root` keeps the world axes. End markers (`head_end`, `toe_end_l` …) copy their parent. The
raptor's `sickle_l/_r` sit 1 cm from `ball_l/_r`, so they copy the toe bone's axes too. The
`+bend` signs follow `source/body/pose.py`, the code that drew the key-pose sheets.

## Physics asset

`bone_forge_physics.py` (run in the editor of the testing-ground project, C:/Dev/BoneForgeTest, whose C++
`UBFPhysics` does the work) saves `<key>_physics` next to each `<key>_skeletal` and sets it on the mesh:

- one capsule body per bone in `collision_capsules` (`thigh (l, r)` is mirrored to `_r`), with the JSON's masses;
- one constraint per body to the nearest parent body: bend = Swing2, side = Swing1, twist = Twist, each limited to
  half its `limits_deg` range;
- Unreal's limits are symmetric, so each range is centred by turning the constraint's parent frame (Frame2) by the
  range's middle. Don't use `AngularRotationOffset` for this: Chaos ignores it at runtime, and a one-sided range
  like the knee's −10..60° ends up as ±35°;
- bodies that already overlap in the standing pose don't collide with each other.

Check: run the testing ground with `-bfragdoll`. Each species goes limp, and after 5 s the log lists any joint axis
more than 5° past its limit. Past 70° to the side a bone's bend can't be read (the plane it is measured in folds
flat), so there only the side counts; a raptor wrist folded 90° used to read as a 90° bend.

Results vary from run to run, because each fall is different. Four runs on 2026-11-03:
- every run: the Velociraptor lands on its arm, and its weight presses the light upper arm 6–8° past the inward stop.
  Widening that stop to −10° didn't help (the arm went to −16°), so the study-based limit was put back;
- once each: the Brachiosaurus' head 5° past, the Pteranodon's shoulder 7° past, and the Ankylosaurus breaking
  9 limits (a forearm twisted 54°);
- the T. rex and Triceratops: within their limits in every run.

Angular projection with shock propagation on light limbs (`ProjectionAngularAlpha` 0.8, `ShockPropagationAlpha`
0.5) didn't fix it either and made other runs worse, so it was taken out; nor did a floor on light bodies' mass (an
eighth of the parent's). Fixed 2026-11-08: every joint uses Chaos's non-linear joint solver
(`bUseLinearJointSolver = false`; the default linear one is faster and less convergent). Two runs since, all six:
no axis more than 5° past its limit, the Velociraptor's furthest 2–3° (about what the physics' elliptical cone
reaches past the rig's bend-by-side box at its corners).

## Balance (Prototype 2)

The testing ground shows where each animal's weight sits over its feet (**B** turns it on and off; it starts on):

- **Centre of mass:** every collision capsule's mass (the rig JSON's) sits at its middle and moves with its bone, so
  the centre of mass follows the live pose and the clips. The capsule masses add up to the catalog mass for all six,
  and the log says so for each species as it loads (`[BoneForge] balance <key>: … kg from the capsules (catalog …)`).
- **Support area:** the feet on the ground (standing: all of them; walking: each foot through the back half of its
  stride, as `FBFPoser` moves it). Each foot counts the bones near the ground, so the Pteranodon's folded wing finger
  doesn't, and a foot whose lowest joint is more than 10% of the leg's length (at least 10 cm) off the ground doesn't
  count at all. Seen from above, the outline round those points is the area the weight has to stay over.
- **Readout:** a second HUD line gives the mass, the centre of mass's height and how far inside or outside the feet it
  falls: *stable*, *only just balanced* (inside by less than 2% of the body length), *kept up by stepping* (walking,
  outside), or *would tip over* (standing, outside). In the world: the centre of mass (green, amber or red, drawn over
  the body), its drop line, and the support area's outline in blue.

**Standing on a slope.** Tilted whole with the ground, a T. rex facing up a 30° slope had its weight 29 cm behind
its feet, and the four-legged species' feet floated or sank. Now a standing animal (`ABFDino::AlignToGround`,
`FBFPoseInput::LegLean`):
- pitches its body with the slope about the hips, not the ground under them (the shoulders then stay at their usual
  height above the slope);
- swings its legs back by that pitch, so they stay upright under its weight, as far as 90% of every hip's and
  shoulder's range allows (the Brachiosaurus and Ankylosaurus get 22° of a 30° slope);
- if it is a biped, steps its feet under its weight (`StepBack`): it swings its legs until the centre of mass is over
  the middle of its feet, within the same range. When the range runs out, the readout says it would tip.

**Leg IK** (`ABFDino::SolveLegIK`, after `FBFPoser` every frame, not on the baked clips) puts each foot on the real
ground, traced under it:
- a planted foot's sole is turned to lie along the ground (at most 35°), then the joint it stands on at rest is put at
  its rest height above the ground (square to a slope). Two-bone IK on hip (or shoulder), knee and ankle moves the
  ankle there, the knee bending the way it already bends and never locking straight;
- a swinging foot keeps its height above the ground the stride walks on;
- when a planted foot's ground is lower than the pose's, the hips come down (`HipDrop`, at most 30% of the hip
  height) and the other legs bend more.

Walking keeps the stride along the slope, with the IK fixing each foot.

Check: run the testing ground with `-bfstance` (`-bfonly=trex,raptor` for just some). Each species stands on the
flat (side and above), walks, then stands halfway up the 30° ramp. Every screenshot's balance line goes to the log,
with how far the feet's joints sit off the ground, and a `leg IK` line with the hip drop and how far each foot moved.
Results on 2026-11-03 (the standing numbers come out the same every run; walking ones change with where in the stride
the shot falls):

| Species | Mass | Centre of mass up | Standing on the flat | Standing halfway up the 30° ramp, facing uphill |
|---|---|---|---|---|
| T. rex | 12.2 t | 3.00 m | 30 cm inside: stable | 28 cm inside: stable |
| Triceratops | 14.0 t | 1.84 m | 89 cm inside | 47 cm inside |
| Velociraptor | 23 kg | 53 cm | 3 cm inside: only just | 3 cm inside: only just |
| Ankylosaurus | 6.7 t | 1.27 m | 80 cm inside | 41 cm inside |
| Brachiosaurus | 50.0 t | 5.09 m | 99 cm inside | 9 cm inside: only just |
| Pteranodon | 37 kg | 76 cm | 23 cm inside | 7 cm inside |

Every foot is down in every standing shot, its contact joint at its rest height above the ground (1–8 cm; the
Brachiosaurus' front feet 25–29 cm, where their joints sit at rest). The Velociraptor's toes only touch the ground
over about 8 cm front to back, so 3 cm is about the most it can have: a small biped on its toes is only just
balanced. Walking, all six are outside the feet on the ground for most of the stride, which is how walking works
(each step catches the fall).

## Turning

A turn starts at the head (`ABFDino::PoseBones`, `FBFPoseInput::LookYaw` / `Turn`, live pose only):
- the head and neck swing toward where the animal is heading, each neck joint and the head taking a share by its side
  range, up to 90% of their ranges together (T. rex 35°, Triceratops 32°, Ankylosaurus 36°, Pteranodon 62°,
  Velociraptor 68°, Brachiosaurus 83°);
- the body eases into the turn after them: its yaw rate ramps up to the species' full rate (heavier turns slower:
  T. rex 86°/s, Brachiosaurus 60°/s, Velociraptor 411°/s) and slows again as it lines up;
- the spine curves into the turn and the tail swings out behind it, by how hard the body is turning;
- turning on the spot carries the feet round the hips (the body turns about them), so the legs step as if walking at
  that speed instead of sliding.

Check: `-bfturn` walks each species ahead, steers it 90° right and takes three shots from above and behind (head
leading, body following, lined up), logging the turn each time. 2026-11-03: 0.35 s in, the T. rex's head leads 29°
with its body 13° round; at 0.85 s the body is at 78° with the head still 25° ahead; then all lines up. The
Brachiosaurus' head leads 68° while its body has turned 9°.

## Splices and flight (Prototype 3)

`bone_forge/splice/splice.py` makes the splices (see its README): `raptorwings` (Velociraptor with Pteranodon wings)
and `ankylowings` (Ankylosaurus with them). They import like the other species (`BF_SPECIES=raptorwings run_bf.py`,
then `run_bf_materials.py`) and load after the six (keys 7 and 8; the older scripted runs keep to the six, the balance
run and the wing run take them too). The HUD shows each winged animal's flight check and a splice's lineage.

What the flight check (`flight_check` in the rig JSON, `EBFFlight`) does in the testing ground:
- **flies** (Pteranodon): as before.
- **glides** (raptor with wings): Space is a leap with the speed it has (a running jump, or off a height); its few
  flight muscles make wingbeats too weak to climb on (thrust scaled by (muscle share / 15%)², 4% of the Pteranodon's),
  so it can only glide down. Stall speed grows with the square root of wing loading (550 cm/s at the Pteranodon's
  182 N/m²; 496 for the raptor's 148).
- **grounded** (Ankylosaurus with wings): a hop on its legs, then it flaps as hard as it can on the spot for three
  seconds and folds its wings; off a height it falls.

The arena has a 20 m cliff west of the flat field (**H**, or "Cliff 20 m" in the picker), facing +X over the field.

The wing membrane's back edge leaves the body at `FBFSpecies::MembraneHipOffset` from the hip joint (the rig's
`membrane_hip_offset_cm`, carried round with the pelvis): on the skin for a splice, whose body is wider than the
wing's donor. Near the ground the downstroke stops where the wing tips meet it (`FBFPoseInput::FlapDownMax`), so a
failed take-off beats its wings against the ground instead of through it.

Esc opens and closes the menu (it used to quit, easy to hit by accident); the menu has a Quit button.

## Splicing editor

Menu, Splice a creature (`ABFSpliceStage`): pick a base, then for Head, Neck, Tail, Legs and Arms a donor species and a
size (3 to 400%, in steps of 5% of its size; Fit sizes the part to this animal, and the steps go on from there) and a thickness (50 to 300%: the flesh only). The pawn is removed while the screen is open; a capsule mannequin coloured by species stands on the
field. Each change runs `splice/compose.py preview` (python, hidden; one job at a time, a change made during a job waits
for it) and the panel shows mass, lineage, the leg, balance and flight checks, flags and notes. Grow body runs
`compose.py build` and shows the OBJ on a procedural mesh; any further change goes back to the mannequin. Walk it grows
if needed and brings the creature into the running game (2026-11-08; until then it imported in the editor and restarted):
`unreal/build_glb.py <key>` writes its .glb in the background ("Making it ready to walk"), Unreal's Interchange
imports it there and then (`BFRuntimeImport`: the skeletal mesh only, in memory under `/Game/BoneForge/<Cap>/`, its
material slots filled from its base species' mesh by name: `_Skin`, `_Eye`, `_LidHalf`...), and `WalkIt` adds it to
the species and spawns it to play. Test it does the same, then starts the test run on it. Everything else (gaits,
checks, IK, balance, ragdoll from the capsules, its climb limit: see Slopes) comes from its rig at runtime. The import
has to start from the core ticker, not an actor's tick (a TaskGraph recursion assert), and leaves out materials
(creating a material instance asserts outside the editor). At the next launch a creature whose mesh was never imported
in the editor is built the same way while the game starts (about 7 s each). `-bfrtimport=<key>` imports one at startup
and logs it. `splice/walk_it.py` still imports into the project and restarts, but the game no longer uses it.
It needs python with numpy on the PATH. A rig named `ed_*` with no imported mesh yet is logged, not shown as a menu error.
Legs and Arms also have Move (along the body). Below the sections: a third pair of limbs from any species (Size, Fit,
Thick, Along) and a tail club (Size, Fit). See `splice/README.txt`.
Advanced mode (2026-11-08): Neck, Tail, Legs and Arms have a Bones button at the end of their species row; it opens
that section bone by bone (one section at a time; again closes it). Each bone (the left one stands for a pair) has its
species (Rex, Tri, Rap, Ank, Bra, Pte; greyed where that species has no such bone), its Length (25 to 300%), Own length
(a swapped bone fitted to this one's length) and, below the first bone, Move knee / elbow / joint (up or down a limb, in
or out on a neck or tail, by up to 40% of the bone above). The recipe carries them as `"bones"`; `compose.py edit_bones`
does the work. Check: `-bfclicks -bfadvanced` opens the legs, swaps the shins to Triceratops, fits them, lengthens them,
moves the knees up, opens the neck, lengthens neck bone 2, grows the body and starts over (`clicks_panel_NN.png`).

**Your creatures, undo, before and after** (2026-11-12, lab plan items 1 and 2). The menu's list is rebuilt whenever it
opens (before, a creature made with Walk it only showed after a restart) and grouped: the six (keys 1 to 6, Splice from
it), Spliced for the catalog (Edit a copy), Your creatures (keys `ed_*`, newest first: Edit, Rename, Remove) and Removed
(Put back). `[` and `]` step through every creature. Names you give live in `bone_forge/my_creatures.json` (key: name),
not in the rig, so growing a creature again can't lose them; no two creatures share a name (" 2", " 3" is added).
Remove moves a creature's files (`rig_joints/<key>_rig.json`, its body OBJs and chains, `splice/<key>_report.txt`,
`unreal/out/<key>_skeletal.glb`...) into `bone_forge/removed/<key>/` with the same folders: nothing is deleted. Put back
moves them home and builds it into the running game in the background, as Walk it does (`BFSpliceStage::BringBack`).
Edit opens the creature in the splicing editor from the recipe compose.py saved in its rig (`splice.recipe`); a catalog
splice (made by splice.py) opens as its one section swap at the donor's own size, one of the six as the base. A change
makes a new creature (the key comes from the recipe) and the original stays; the editor says so. The editor's Name box
gives the name when the body is grown (Grow body, Walk it, Test it). Undo and Redo (buttons, Ctrl+Z, Ctrl+Y or
Ctrl+Shift+Z) keep the editor's whole state at each change, the last 50; opening a creature is one of them. After each
change the creature before it is drawn as thin pale bones over the mannequin (Show before toggles it; only for the same
base), and the readout says what changed: mass, the weakest legs' muscle against their load (compose.py's new
`legs_ratio`), the climb limit. Text boxes take the keys while typing (`ABFSpliceStage::IsTyping`): the game's keys and
the editor's view keys wait. Startup: a creature's .glb from last time is used when it is newer than its rig, body and
the scripts that write it (`BFRuntimeImport::BuildGlb`), instead of being rebuilt every launch (about 7 s each).
Check: `-bfmine` does it all by real clicks and typing (menu buttons are scrolled into view first: the list is longer than
the screen): Edit a copy on the T. rex with a Triceratops head, an Ankylosaurus tail, Undo twice, Redo twice (the log's
`click` lines show each recipe), a name typed, Walk it, then Rename, Remove, Put back and `]`. 2026-11-12: every step as
expected; Put back took 6.9 s. It leaves its creature (`ed_trex_hetrike_taankylo`, "Rexatops") and `my_creatures.json`
behind, and `-bfclicks` leaves the one it grows: delete those by hand after a check.

Ankles close up: `-bfankles` shows each of the six (or a `-bfonly` list, splices included) from low and near, standing
and in mid-stride (`Saved/AutoTest/<key>_ankle_*.png`). Since 2026-11-08 the six wear grown bodies too
(`<key>_grown.obj`; `BF_BODY=hand` imports the hand-built `<key>_body.obj`), so every animal's ankle is a stitched one. Check by hand: `-bfclicks` presses the panel's buttons through Slate's own mouse path (hit test included), tries
right-drag, wheel, D and Esc, and logs what each did: `took` or `NOTHING HAPPENED`. It found a Move button past the
panel's edge. Check: `-bfeditor` swaps a head and tail, resizes the legs, grows the body, and logs each readout with screenshots
(`Saved/AutoTest/editor_*.png`). Add `-bfwalk` to go on through Walk it: the same game then runs the catalog on the
new creature and closes (`-bftestit`: Test it instead, closing after the report card). 2026-11-05: a T. rex with a Triceratops head, Ankylosaurus tail
and 120% legs went through all of it and walks at 1.28 m/s, 11.0 t, capsules and catalog agreeing. 2026-11-08, built in
the running game: a T. rex on 53% Triceratops legs and an Ankylosaurus on long Brachiosaurus legs, walked straight from
the editor ("walk it: <key> built in the game (runtime mesh)").

## The world: an island from a seed (World and AI tab, step 1)

The menu's "The world" row: New island (a random seed), or a seed typed and Make it; Go to island N once it stands;
Keep it (bone_forge/my_worlds.json, by seed: the same seed makes the same island); kept islands to make again.
`ABFWorld` (BFWorld.h/.cpp) makes it in the game, 10 km east of the lab's field: a 1025 x 1025 grid at 4 m (4.1 km
across). On a worker thread: noise lays out a round island pushed in and out at the coast, broad rises, rolling hills and
a ridged rocky spine where a slow noise says mountains; 260,000 droplets of rain erode it (Sebastian Lague's droplet
erosion); every hollow is filled to where it spills (priority flood from the sea) and each cell's catchment runs
downhill (D8); the sea sits at 0, lakes at their spill level, and rivers are carved where a catchment passes 6,000 cells
(deeper and wider as it grows), then water spreads onto lower ground beside it so its shore follows the land. Biomes
from height, slope and wetness: beach, rock (steeper than 30° or above 150 m), swamp (flat, by fresh water), forest
(a slow noise, wetter near water), plains. Then 256 tiles of 64 x 64 cells (procedural meshes with collision, built a
few a frame), fresh water on its own surface reaching two cells under the banks, one wide sea plane. Ground colours are
painted on the points and shown by `M_BoneForgeTerrain` (bone_forge_materials.py `build_terrain_master`; `BF_TERRAIN_ONLY=1`
builds just it), grained by two tiles laid flat. `ABFGameMode::WaterAt` asks the island first, so the swim system works
in its sea, lakes and rivers; `EBFSpawnArea::World` puts a creature on flat open ground near the middle. Check:
`-bfworld [-bfseed=N]` (2026-11-12, seed 20261112): made in 0.4 s, ready in 0.8 s; 7.8 km2 of land from -30 to 226 m;
plains 36%, forest 31%, rock 15%, lakes 7.7%, rivers 4.6%, swamp 3%, beach 3%; a T. rex walked 13 m with its capsule 2 cm
above the ground and waded at the nearest river and lake (`world_air_south/_west`, `_ground`, `_walk`, `_river`, `_lake`).
Not yet: creatures living there (steps 3 to 6). The ground's grain still reads as a tile up close.

### Real water (2026-11-12)

The sea, lakes and rivers are single-layer water (`M_BoneForgeSea`, bone_forge_materials.py `build_sea`; it is deleted
and made afresh each run, because clearing its nodes leaves the water output behind and two won't compile): they
refract and tint by depth (absorption and scattering are per centimetre; the sea clear blue-green, fresh water a little
peaty and silty), reflect the sky, and foam where the water thins against the ground. `ABFWorld` draws the water a map
(`Info`, a 1025 x 1025 texture: the sea's depth, a sea point's distance from the shore, fresh water's current) that the
material reads. The sea: six long Gerstner waves round the wind (toward 35°, 48 m down to 6 m long, about a metre high
in open water), calm in the shallows and gone beyond 1.5 km from the camera, on a grid that follows the camera (2 m
squares near it, doubling outward to 32 km, moved in 32 m steps so the waves don't swim); rollers run in over the last
45 m and break white on the beach. Every water: twelve short ripples make its surface, carried downstream by the current
(two staggered copies faded in turn). Rivers flow the way their water drains (D8), 0.3 m/s on the flat to 3 m/s down a
steep reach, smoothed and slower by the banks, white in rapids; lakes drift to their outlets. A river spreads only 3 cells
past its channel (on a slope it ran off as a sheet), and where a river or lake meets the sea its surface comes down to
the sea's. Fresh water and the sea's surface cast no shadow. In C++: `ABFWorld::SeaWaveM` works out the same waves, so
`WaterAt` gives the sea's surface where the wave is (swimmers ride them), and `ABFGameMode::CurrentAt` gives the current,
which carries a swimmer whole and a wader by as much as the water has hold of its legs (`ABFDino::TickSwim`). Keep
`SeaWaveM` and the material's `SEA_COMMON` in step. Check (`-bfworld`): a wading T. rex carried 0.66 m in 4.7 s by a
0.19 m/s current, one 60 m out to sea rose and fell 1.3 m over 7 s (`world_river_close`, `_later`, `world_sea_out`,
`world_sea_in`, `world_sea_swim`).

### Plants and food (World and AI tab, step 2; 2026-11-12)

Late Cretaceous flora, the six's world, and no grass (it was rare until long after). `BFPlants.h/.cpp` builds each kind
in the game from its shape, three shapes a kind and three levels of detail each, once a game (`UStaticMesh::
BuildFromMeshDescriptions`, fast build): monkey-puzzle conifers (Araucaria: a bare trunk, whorls of thick leafy rope
branches in an umbrella), redwoods (tall cones of jagged drooping tiers), bald cypress (buttressed, a broad feathery
crown, knees), ginkgo (spreading limbs, clumps of leaves), tree ferns (a fibrous trunk, arching two-pinnate fronds,
coiled croziers), cycads (a diamond-patterned trunk, a crown of stiff pinnate fronds, a cone), bennettites (a squat
barrel, fronds, flower-like cones), early fan palms (Sabalites: pleated fans), flowering shrubs (magnolia kin: glossy
leaves, six-petalled cream or pink flowers), ground ferns and horsetail clumps (jointed stems, whorls of twigs). Every
point of colour carries how far it sways (vertex alpha) and, on leaves, where it shrinks to when eaten (UVs 1-2).
`M_BoneForgePlant` (`build_plant_master`): two-sided foliage, the wind's sway in gusts that travel across the island (the
same wind as the sea's), each plant's own shade (per-instance random), and its leaves pulled in by how much of it has
been eaten (per-instance custom data 0). `ABFWorld::PlacePlants` (worker thread, with the land) scatters them by biome
(per hectare: plains are fern prairie with cycads, bennettites and shrubs; forest redwoods, monkey puzzles, ginkgos, tree
ferns under them; swamp bald cypress and horsetail beds; beaches palms; rock a few hardy conifers and cycads; more ferns,
horsetails, tree ferns and ginkgos within a few metres of fresh water), clumped by each kind's own noise, none on ground
too steep or high for it, trunks 15 m clear of where a creature arrives; then `GrowPlants` puts each kind-and-shape in
as one hierarchical instanced mesh a frame (drawn out to 130 m for ferns, 4 km for trees; trunks block with a capsule).
Food: each plant holds kg of leaves (a fern 1.5, a cycad 12, a redwood 70, by its size squared) in a height band, and
grows back a share a minute. E, for a plant eater (its head's species, `FBFSpecies::Feed`), crops a mouthful (1/2000 of
its weight) from the nearest plant its snout can reach (a little more than its width round it, up to a little above its
head) and starts the feeding pose; meat eaters bite as before. The status line says what it ate and what's left.
Check (`-bfworld`, seed 20261112): 435,034 plants (361,502 ferns, 19,735 horsetail clumps, 9,898 redwoods, 5,649 monkey
puzzles, 11,942 tree ferns, 8,199 cycads ...), ready in 2.6 s; about 4 ms a frame at every view; the T. rex can't eat
them; a Triceratops stripped the nearest cycad in two mouthfuls (7 kg, 4.7 kg) and its fronds shrank into the crown
(`plants_plains`, `_forest`, `_forest_floor`, `_cycad`, `_treefern`, `_swamp`, `_beach`, `_horsetail`, `_shrub`,
`_bennettite`, `_fern`, `_air`, `eat_before`, `eat_after`). Not yet: hunger (step 6) and other creatures eating (step 3);
from the air the fern prairie is too small to see, and trunks in their own crown's shade read dark.

### Second look (2026-11-12, Raven: rivers came up over the land and looked like a checkerboard where they met the sea;
trees shouldn't stand in lines from above)

- Rivers sit in their banks. Each channel point's level is the filled surface less 15 cm, but also 20 cm under the lowest
  dry ground just outside its channel, and never above the point upstream of it (in order from the highest point down,
  each passes its level on to the point it drains to); the bed is cut under that. Rivers are at most ~20 m wide and
  spill one point past their channel (they were up to 60 m and spread 16, and lay up the valley sides as sloping sheets).
- The water's surface: a point of dry ground beside the water takes the water's level only where its ground is higher
  (under a bank, hidden); where it's lower the surface dips 5 cm under it, so water never lies on lower land beside it.
- River mouths: a river's last 48 m come down to the sea's level (easing in) with its bed cut down to keep its depth,
  flowing slow and calm there (it read as rapids: white water on the beach); a lake within 48 m of the sea and less than
  1.5 m above it is a lagoon at sea level, its bed lowered as much (lowering only its seaward edge tilted it). Within 5 cm
  of the sea the fresh water hands over to the sea's own surface.
- Ripples are warped a little by a slow noise so the twelve waves don't add up to a regular grid (the checkerboard), and
  the surf's foam is a narrower, broken line on each roller.
- The world's log counts fresh water standing above the dry ground right beside it: 54 points on seed 20261112 (none
  show, by the rule above).
- Trees in rows: each 4 m point's random numbers come from a strong hash of its index (PCG seeded by a murmur mix;
  FRandomStream seeded with neighbouring numbers draws related first numbers). Seen straight down from 300 m and 800 m
  they stand scattered; tall trees lean outward toward the picture's edges, which is only the camera's perspective.
- From the air: the ground takes the colour of what covers it (deep fern green under fern beds, leaf litter under
  crowns), ferns are drawn to 300 m and horsetails to 250 m.
- Light: the plant material adds a soft fill (`Fill` 0.15, times the eye adaptation's inverse, so the same at any
  exposure; leaves a third of it), trunks and bark are lighter; the ground has little shine (Specular 0.2: at a low angle
  the sky's reflection washed it pale), greener biome colours, and its grain from two turned samples of the tile.
- New views in `-bfworld`: `mouth_air`, `mouth_sea`, `mouth_top` (the river mouth nearest the start,
  `ABFWorld::RiverMouth`), `trees_top` (300 m straight down), `trees_top_high` (800 m), `plains_low_air`.

## Thinking plant eaters (World and AI tab, step 3; 2026-11-12)

The menu's world row: Animals (herds round you) puts a herd of each plant eater among the six on the island (Triceratops
in fives, Ankylosaurus and Brachiosaurus in threes, a few hundred metres round you); again takes them away. I shows or
hides what each is thinking over its head (the one nearest the middle of the view says its whole thought, the rest
who they are and what they're doing). `ABFLife` (BFLife.h/.cpp) runs them: each is an `ABFDino` like yours, moved the way
the companion is (move input, run), and everything its mind uses comes from its body:

- Needs: a stomach (0.4% of its weight) that fills with what it crops (a mouthful 1/2000 of its weight) and empties in a
  quarter of an hour (hunger); thirst (7 minutes from slaked to desperate); tiredness, a little from being about and more
  from running (its own winding, `ABFDino::GetExertion`), rested off.
- Senses: sight, 90 m plus 40 m a metre of eye height (to 400 m), all round but a blind spot behind, blocked by hills and
  trunks (a line trace); smell, carried on the island's wind (`ABFWorld::WindDeg`): only from upwind, as far as its
  nostrils' size says (`FBFSpecies::NostrilShare`, ~280-350 m), and a smell makes it wary rather than afraid; hearing,
  something big running near. Danger is any creature whose head doesn't eat plants and that weighs a tenth of it or more.
- Memory: up to 24 places: food (and how much was left), water it drank at, where it met danger; the oldest forgotten.
- Choosing: each act's worth from its needs (fleeing or standing ground from fear, drinking from thirst, grazing from
  hunger, resting from tiredness, rejoining from how far the herd is, wandering when nothing presses, alert from a smell),
  more for what the others near it are doing, plus a reluctance to drop what it's doing; it does the worthiest. Every
  change is logged with why (`[BoneForge] life: Trike 2 -> standing its ground: saw a Tyrannosaurus rex 82 m off: ...`).
- Paths: `ABFWorld::FindPath`, A* on 8 m steps for its body: its pace on each slope from its own climb test
  (`ClimbPaceAt`, none steeper than `ClimbMaxDeg`), water it can wade (45% of its height) or swim (if its swim test says
  it floats, slower), forest the thicker the wider it is. It walks the path, keeps its room from the others, side-steps
  when stuck and looks for another way if it stays stuck.
- Eating: `ABFWorld::FindPlant` (nearest worth it within 40 m round it and its herd, whose leaves it can reach), walks until
  its mouth is at it, crops mouthfuls with the feeding pose, the next plant when one is stripped. Drinking:
  `ABFWorld::NearestShore` (dry, gentle ground at fresh water's edge; never the sea), head down until slaked.
- Herds: keep together (rejoining when far), do as the others near them do, and take up an alarm (its fear, and where the
  danger is). A herd that outweighs the meat eater two and a half times stands its ground, closes up and faces it
  (charging if it comes within 20 m); less, it runs, the way that leads furthest from the danger its body can go,
  walking again when winded.
- An animal that goes limp gets up again after 5 s.

Check: `-bflife [-bfseed=N]` (2026-11-12, seed 20261112, needs twice as fast): 11 animals in 3 herds, 119 choices in
150 s of watching then 50 s of a T. rex walking at them from upwind; the Ankylosaurus ate (up to 3 full meals) and drank
(up to 4 times); the Triceratops grazed horsetails and ferns; a Brachiosaurus walked 150 m to water, its thought counting
down the distance. The T. rex at the Triceratops (5 x 14 t against its 12 t): all five stood their ground facing it,
smelled from 200 m and seen from 80 m. At the Ankylosaurus (3 x 7 t): all three ran (`life_herd*`, `life_threat*`).
Not yet: hunters that hunt them (step 4), nights and days (step 5), the plant eaters crossing paths with each other's
herds, and much of the island's plant food is small (a fern 1.5 kg against a Triceratops' 7 kg mouthful), so
grazers walk a lot between plants.

## Hunters (World and AI tab, step 4; 2026-11-12)

Animals (the menu) now puts the hunters in too: a T. rex on its own and a pack of three raptors, further out than the
herds. They live as the plant eaters do (needs, senses, memory, paths, drinking, resting) and hunt (`ABFLife::HunterThink`,
`HunterDo`, `ChoosePrey` in BFLife.cpp), everything from their bodies and their prey's:

- Fighting: every animal has health, armour and a weapon (Triceratops horns 1.6, Ankylosaurus club 1.4 and plates 0.55,
  sauropod tail 1.0, teeth 1.0, raptor claws 0.9). A bite or a blow takes 15% x the square root of the attacker's weight
  over the victim's x the weapon x (1 - armour): a T. rex takes 9% of an Ankylosaurus a bite (11 to kill), 14% of a
  Triceratops. At 0 it dies (goes limp) and leaves a carcass of 60% of its weight. A plant eater standing its ground
  charges a meat eater that comes within 20 m and hits it with its horns or club; a hunter hurt below 45% backs off.
- Hunger: a meat eater's stomach is 4% of its weight and a meal lasts half an hour; it starts hungry.
- Choosing prey: of what it sees, or smells (only the prey upwind of it: the wind carries the scent to it), the meat it
  would get, times whether it can catch it (its top speed against the prey's, more if the prey is winded or hurt),
  over what it would have to fight (the prey's weapon and armour and weight, or its whole herd's if that would stand
  together), and the distance. Prey its teeth would barely scratch it leaves alone. A pack takes the prey one of it took.
  It remembers its quarry and where it last sensed it for 40 s, and goes back there.
- Searching: with nothing in sight or on the wind, it walks into the wind, zig-zagging, so whatever is ahead comes to its
  nose.
- Stalking: round to downwind of the prey (each of a pack from its own side), walking, then creeping the last 180 m.
  The prey's eyes: head down (grazing, drinking) they see half as far; a creeping or still hunter is hard to make out;
  under trees (`ABFWorld::CoverAt`, the trees' crowns) harder still.
- The rush: from its sprint's reach (6 s of its top speed, 25-50 m) or once the prey has seen it within 1.6 times that;
  seen from further, it lopes after it, and after 25 s getting nowhere it gives up for a minute and waits for the herd
  to settle. A rush that lasts 30 s, or leaves it spent, it gives up (that prey left alone for 40 s).
- The kill: bites every 1.2 s once its jaws reach; the prey runs, bleeding health; dead, the hunter feeds (a mouthful
  of 0.4% of its weight every 1.5 s) until gorged, rests, drinks and comes back to it. Raptors keep out of a T. rex's way.
- You: E bites whatever your snout reaches the same way (the status line says its health), and a meat eater eats from a
  carcass with E; a herd standing its ground can hurt you (your health on the status line; at 0 you go limp: K to get up).

Check: `-bfhunt [-bfseed=N]` (2026-11-12): you a Brachiosaurus (no threat), the herds and hunters round you, the T. rex
started 120 m downwind of the Ankylosaurus (in the game it finds them itself, by walking into the wind and smelling
them, over a few minutes). The T. rex picked Ankylo 3 (seen, 101 m, its herd of 20 t too light to stand against 12 t),
charged from 72 m and was spent; picked again, crept to 64 m, charged, landed 11 bites and killed it (4 t of meat);
fed, rested gorged, drank, came back to feed: 586 kg eaten, 3 full meals. The other two Ankylosaurus ran and swam away
(`hunt_rex_stalking`, `_charging`, `_feeding`, a picture every 20 s). The Triceratops herd (70 t) it never chose.
Weights and speeds from the bodies: Triceratops 14 t, runs 4.2 m/s; Ankylosaurus 6.7 t, 2.2 m/s; Brachiosaurus 50 t,
2.2 m/s; T. rex 12.2 t, 7.5 m/s; raptor under 50 kg, 8.6 m/s.
Not yet: the raptors are too small for any of the island's plant eaters (their claws would barely scratch a 7 t
Ankylosaurus), so they roam and would scavenge; there's nothing their size to hunt. A T. rex can't run a herd down (its own
winding stops it within seconds, as the real one's would), so it hunts by ambush and fails in the open; in the game,
finding prey from far off takes it minutes.

### Raptors as scavengers (2026-11-12, Raven)

The raptors (under 50 kg) can't hurt any plant eater on the island, so they live as scavengers: a kill they know of
(within 150 m, or its smell on the wind from upwind of them, further than a live animal's) draws them; while a much
bigger meat eater is on it they wait 55 m off on its far side; while it rests or sleeps they slip in and eat. When
smaller animals and young are added, their prey choice already takes anything their teeth would hurt.
A gorged hunter lies up until it's properly hungry again (hunger 0.35) before going back to its kill: the scavengers'
chance. Check (`-bfhunt`, its raptor minutes): after the T. rex's kill the raptors smelled it from 200 m, came in and
waited 72-78 m off; in those 3 minutes the T. rex was still on its first meal (it rested only briefly), so they didn't
get to eat yet.

## Watching (World and AI tab, step 5; 2026-11-12)

- Day and night: the island keeps a clock (`ABFWorld::GetHours`, a 24-hour day in 24 minutes of game time, from 8 in
  the morning). The sun rises in the east, stands 62 deg up at noon in the south and sets in the west; at night a dim
  blue moon lights it (`ABFGameMode::SetSkyAt`), and the exposure darkens with the light (an unbound post-process
  volume's auto-exposure bias, to -2.5 at night) so the eye doesn't make night into day.
- The animals by night: their eyes see a third as far (smell works as by day: a hunter's nose finds prey in the dark);
  the herds sleep together, a T. rex too unless it's starving; the thirsty still go to water.
- O: the island's life at a glance (top right): the time, how many of each kind live and how many have died, kills,
  carcasses and the meat left, and every animal's act, needs, health and thought.
- N: the camera follows the next living animal (behind and above it, easing after it); M stops.
- Time: . past full speed now goes to twice and four times (`, slower`), to watch a day go by.

Check: `-bfwatch` (2026-11-12): the panel open, the camera on Trike 1, the clock set to 11:00 (everyone awake), 18:25
(11 of 15 resting or asleep), 23:12 (the herds asleep but for the thirsty walking to water at 0.6-0.8 thirst and the
starving hunters searching) and 06:25 (everyone up) (`watch_day`, `_dusk`, `_night`, `_dawn`).

## Game pace (2026-11-12, Raven)

The bodies' real speeds (walks of 1.1-1.6 m/s, a T. rex's sprint 7.5 m/s) made the island too slow to play: crossing it
at a walk took the best part of an hour. Every animal now moves `ABFDino::GamePace` times faster (1.8 by default; the
menu's Game pace row: 1, 1.4, 1.8, 2.2, 2.6; kept in my_creatures.json as `game_pace`): its walk, run and swim speeds,
its stopping (braking) as much, its turning by the square root; its steps quicken with it (the gait's cadence is its
speed over its stride, so the feet keep pace), and it runs `ABFDino::Endurance` (3) times as long before it's winded.
Everything stays in proportion between the species, so the animals' minds needed no retuning (they read the game
speeds). `ABFDino::ApplyPace` sets it live on every animal when the pace is changed; `GetRealWalkCm`/`GetRealFastCm`
keep the body's own. The polish pass at 1.8: bfauto, bftest (six report cards), bfclicks, bfjump (jumps as worked out:
T. rex 29.2 cm, raptor 70.7, Ankylosaurus 9.6), bfworld, bflife, bfhunt (the T. rex now made two kills in its five
minutes), bfwatch all ran without crashes. Not measured: foot sliding at the new pace (by construction there should be
none); the Pteranodon's flight has its own speeds and isn't paced.

## Your needs (World and AI tab, step 6; 2026-11-12)

On the island your creature has needs from its body as the animals do (`ABFPlayerController::TickNeeds`): a stomach (a
plant eater's 0.4% of its weight, digested in 15 minutes; a meat eater's 4%, in 30), thirst (10 minutes from slaked to
parched), tiredness (from being up, more from running), and health (lost slowly when starving or parched, five minutes
to death; won back fed and watered; also what the animals' horns and clubs take, `ABFLife::PlayerHealth`). The status
line shows them. E: at fresh water with your mouth to it (`ABFWorld::FreshWaterNear`; not the sea) a long draught (head
down); else a mouthful of plants (a plant eater) or of a kill (a meat eater); else a bite. Z lies up to rest (faster at
night; a move key gets up). Exhausted (90%), it can't run. Dead, K starts again with fresh needs.
Check: `-bfneeds` (2026-11-12): a Triceratops at the nearest lake drank (thirst 20% to 0), cropped three mouthfuls of
cycad (hunger 30% to 12%), rested 10 s at twenty times the rate (tired 80% to 0), then starved and parched at thirty
times: health fell 10% a second and it died ("You died of hunger and thirst") (`needs_drink`, `needs_eat`, `needs_dead`).

## The young (2026-11-13, Raven: babies, and prey for the raptors)

compose.py takes `"size"`: the whole creature grown at that size, every length (`..._cm`, `..._m`, the bones' flesh
profiles) times it, every area times its square, every mass times its cube, and its legs, gaits, swimming and climbing
worked out again for the smaller body (`scale_rig`; the features' meshes scaled with it). A young one is its kind at 30%
of its length with a head 130% of its share (young animals' big heads): key `ed_<kind>_he130_z30`, named "Young
Triceratops". So its strength, speed and senses are its own: a young Triceratops 430 kg, legs 5.5 times what its weight
needs (the adult's about twice: muscle goes with the square, weight with the cube), runs 5.4 m/s at the game pace (the
adult 7.6), sees 129 m; a young Ankylosaurus 189 kg; a young Brachiosaurus 1.36 t.
Each plant eater herd has two young (`ABFLife::SpawnYoung`) beside the herd's first adult, their mother. Their bodies are
grown the first time they're wanted (`TickYoung`: compose.py, then build_glb.py, then the runtime import, one kind at a
time; afterwards they're kept), and they join the herds once in. The young keep to their mother (rejoining her more the
further they stray), graze and drink as the herd does, and run when frightened, never stand; a mother whose young is
frightened near her stands for it against anything under one and a half times her weight. The hunters weigh them as
any prey (`ChoosePrey`): smaller, slower, easier to hurt, but defended while near the herd, so a straggler is what's taken.
Check: `-bfyoung` (2026-11-13): the young of all three herds came in; Young Trike 1 strayed 60 m and the raptor pack
was put downwind of it: Raptor 1 picked it ("its herd would stand together"), the pack crept to 50 m and rushed, bit
it down to 80%; its mother stood her ground over it and killed Raptor 2 with her horns; the other two raptors fed on
their dead packmate and lay up; the young one went back to the herd and to drink (`young_herd0..2`, `young_stray_*`).
Not yet: the young don't grow up, and nothing is born (no breeding); small kinds of their own (not young) need new bodies.

## The life cycle (plan, 2026-11-13)

Raven's choices: egg to adult in about 10 hours of play (so the island is saved between sessions), you take part and
carry on as your offspring when you die, babies vary a little from their parents, food decides the numbers (no cap).
Raptors eating their own kind stays (survival of the fittest).

1. Growing up in stages: four bodies a kind (hatchling, juvenile, subadult, adult), each worked out from its own size;
   the growth clock moves an animal into its next body in place (same mind, memory, needs, family).
2. Each stage its own way: hatchlings keep to the nest or their mother and hide; juveniles keep with the herd; subadults
   roam but don't breed; young hunters take smaller prey.
3. Saving the island: its seed, time and plants, every animal, nest and egg, saved and loaded.
4. Adults, courtship and mating: males and females; a female breeds when well fed and healthy, with time between
   clutches; males court with displays; rivals contest by display and shoving, rarely to the death.
5. Nests and eggs: she chooses a site, scrapes a nest, lays a clutch sized by her body; care follows the fossils
   (Triceratops, Ankylosaurus and T. rex guard, Brachiosaurus lay and leave, raptors brood); eggs are food.
6. Hatching: eggs crack, hatchlings take to their mother or fend for themselves; most won't grow up.
7. Inheritance: size, colours and a little strength and speed vary around the parents'.
8. Populations by food: births, deaths and the plants' regrowth set the numbers; animals far from you live on in a
   simpler simulation; the watch panel shows births, deaths, generations and families.
9. You in the cycle: court, nest, raise; when you die you carry on as one of your offspring.
10. Wild Pteranodons (Raven, 2026-11-13): flying minds (take off, soar on the wind, land, roost on cliffs and tall trees),
    fish in the sea, lakes and rivers for them to catch, colonies nesting on high ground, flight at the game pace too.

## Life cycle steps 8-10 (2026-11-14)

Step 8, far off: beyond 1.5 km from you (`ABFLife::FarCm`) an animal's body is put away (hidden, no ticks, no collision)
and it lives a simpler life once a second (`TickFar`): its needs; water when thirsty (the nearest shore, walked to over
ground it can stand on, `MoveFar`); plants when hungry (`FindPlant`/`EatPlants`, the real ones, so it eats the island
down as the full mind does); a herd eaten out moves on; hunters take a carcass, else chase the easiest prey near them not
under a herd that would stand, one chase in 90 s, one in eight a kill; a male goes to a female in condition and mates;
a female due lays; a mother minds her eggs; asleep at night; with its herd or mother otherwise. Within 1.2 km
(`WakeCm`) it's back in full on the ground where it got to. Breeding, growing, old age, the young's chances go on as
ever. The watch panel shows each living one's sex, generation, mother, father and young living, and how many are far off.
Check `-bffar`: you 1.85 km off for six minutes: 26 drinks, 329 meals, a mating; back: all 19 woke on the ground.
Step 9, you: `SyncPlayer` puts you among them (your kind and stage from your body, a herd of your own, a sex from the
menu: "You: female/male"), your condition from your needs; X (`PlayerAct`): a male by a female of his kind in condition
displays ten seconds and has her (unless a stronger rival courting her drives him off); a female whose eggs are due
scrapes a nest and lays where she stands. Males court you as any female; your young keep to you; when you die, K carries
you on as your oldest living young (`CarryOn`: its body, needs, genes, generation), and you grow up in it (the
controller takes each new body). Mates are `ClutchFather` (Father is an animal's own father). You're saved with "you"
(sex, generation, age, genes), not as one of the animals; fights with you still go through your own health.
Check `-bfyou`: male: X by Trike 5, she carries your eggs; female: courted by Trike 4, laid with X, your young near you;
died, carried on as Hatchling Trike 13 (generation 1), grew into a juvenile and still played it.
Step 10, wild Pteranodons: a colony of six on the highest open ground within 1.5 km with the sea within 600 m
(`PlaceColony`), fish shoals in the sea along that coast and lakes near (`FShoal`, kg, filling again in half an island
hour; placed over the deepest water within 40 m, none under 1.5 m); their own mind (`FlierTick`): roosting at night, tired or full; hungry by day out to the nearest
shoal high, a dive to the surface over it (two in five take a fish), up again; soaring in a wide circle over the colony
otherwise; courting and nesting on the ground at the colony (they brood). `FlyTo` steers (holding a height over ground or
water, beating when low or slow, a long descent to land). Flight goes at the game pace (`ABFDino::TickFlight`). Far off
they fish simply. Check `-bfptera`: 31 dives, 12 catches by two minutes, then back to roost full; at night they roost.
2026-11-14: the fish are drawn (`DrawFish`, instanced: 0.6 m dark grey-blue bodies with tail fins, circling each shoal
just under the surface, one leaping up to 1.5 m every 6 s; the count follows the shoal's kg). Dives skim (under 3 m
`FlyTo` pitches down to -35° at 0.3 thrust, aiming 50 cm over the water; a catch counts within 9 m above the surface).
Fliers court by air and nest where they land; a ready female rests, a due one lays first. Hatchlings are fliers
(`HatchOne` sets `bFlier`); young bodies at 30%, 60% and 85% size all get the verdict "flies". Hatchlings can't fish: a
parent brings fish to a hungry one (`NeedsFeeding`, 0.6 kg each); juveniles fish themselves. Check `-bfpterayoung`
(GrowRate 60, 600 s): 6 hatched, fed by their parents 3 times, 23 catches, one grew into a juvenile; fish pictures.
Many hatchlings still die young (the young-death hazard, by design).

Nest trees and ledges (Raven, 2026-11-14): `BuildPerches` (from the colony's place, so a load builds them again) puts
up to four giant redwoods (65-78 m) within 55 m of the colony, the island's trees cleared 15 m round each
(`ABFWorld::ClearTreesNear`). Each has three shelf branches 24-57 m up (a 1 m limb, a fork under it, a 6.6 m mat of twigs),
foliage sprays (none over a shelf) and a tiered crown. Up to four rock ledges go on real cliffs within 600 m (the ground
at least 9 m lower 10 m out); for now they are plain slabs. Each flier owns a perch (`PerchFor`): it roosts there at
night or when tired, and a female nests in its middle (the eggs and hatchlings stay up there). A courting male lands
beside her, and a parent carrying a fish flies up to its young. A perch landing (`ABFDino::PerchOn`) comes in from 18 m
out, slows to a hover and sets down; on the perch it can't walk off the edge, and it leaves outward. Hatchlings stay in
the nest till they can fly. Checks: `-bfpterayoung` 9 hatched, 8 feeds, all nests up high, 42 perch landings (none
needed placing); `-bfptera` night: 6 of 6 roosting; `-bfsave` 34/34.

Shorter checks (2026-11-14): `-bfpteranest` stages a mother on her tree nest with eggs hatching in 15 s at 7:30 (young
deaths off), then watches the parents fish and feed (PASS: hatched, still in the nest, fed, landed on perches): about
40 s of play, 2 min 44 s with the engine's start. `-bfspeed=2` runs the whole game (and the checks' clocks) twice as
fast; `-bfchain=bfptera+bfday` runs further island checks in the same launch (the engine starts once; any of bfyoung,
bfgrow, bfbreed, bfday, bffar, bfyou, bfptera, bfpterayoung, bfpteranest). `-bfptera` at 2x: 4 min of play instead of
7.5, same passes. A smaller test island wasn't worth it: the island stands 10 s after the engine starts. Keep pictures
and landing checks at 1-2x; faster than that, walking and landings get coarse.

Rock pieces (Raven chose the hybrid, 2026-11-14): the height map stays the island; caves, tunnels, overhangs and arches
are separate pieces of 3D rock laid into it. Step 1 built: `ABFWorld::AddCave(Path, Radii)` makes one. Its box covers
the path and 7 m round it, from under its floor to over the hill. In that box the rock is a 3D field (`CaveDensity`):
the hill's own heights, rough rock from 1.5 cells in, and minus the cave (a lumpy tube round each stretch of the path,
its lower half squashed, its floor flat). That field becomes a mesh by surface nets on a 50 cm grid (`BuildCaveMesh`:
a vertex per crossed cube, a quad per crossed edge, faces turned outward, normals from the field), with collision.
The tiles under it are made again without the cells it covers, one cell in from the box's edge. In that ring the piece
runs on 4-14 cm under the ground, with the ground's own normals and colours, so no seam shows. Inside, it darkens with
distance from the mouth (the sky light isn't shadowed in caves). Island trees in its mouth are taken away.
`ABFWorld::FindCliff` is shared with the ledges. Check `-bfcave` (2.5 min): a cliff 707 m from the start, a 27 m cave (a
3 m tunnel to a 5.5 m chamber), the piece 36 x 56 x 65 m with 21k vertices and 41k triangles, built in 0.07 s. Roof
6.4 m over the chamber's middle, floor 3 m under it, the way out clear, 11.7 m of rock over the roof; a Velociraptor put
in the chamber stands on its floor. Pictures: front, side, above, far, inside looking out, inside looking in.
Not yet: the animals don't know caves (`GroundAt` gives the hill's top over one; pathfinding goes over it). The mouth
is a smooth hole in the slope, with no rock rim or overhang yet (step B's rock shapes). Caves are only made by the check
so far, not by the island itself.

Step B built, rock shapes (Raven chose it next, 2026-11-14). An `FBFRock` is a rounded box, turned and sized, made
lumpy with noise (a quarter of its mean size, at most 0.6 of its least). There are three kinds: a boulder, a slab (its
top cut flat at `TopZ`, to stand on) and a crag rock (its lumps stretched up and down). `ABFWorld::AddRocks` meshes a
group with surface nets on a chunky grid (its mean size / 2.8, 24-90 cm). Every triangle gets its own flat normal
(faceted, like the plants) in grey-brown, each rock its own shade, with moss on the boulders' tops. The group gets
collision, isn't drawn past 600 m, and marks the ground points it stands on (`RockCells`). Animals' `Walkable` refuses
those points, and `FindPath` costs a step onto one 4 times as much. `RemoveRocks` takes a group away again.
The island places its own (`PlaceRocks`, when it's made; the same rocks for a seed):
- boulders, 1 to 4 together, where gentle ground meets a slope over 30 degrees within 8-20 m that's 4 m higher, plus a
  lone stone now and then on rocky ground (up to 700);
- crags, 2 to 4 leaning rough rocks together, on steep ground in the top half of the island's height (up to 220);
- one mesh a 256 m tile, with anything that grew where a rock stands taken away.
The ledges are rock slabs now, held up by a lump under each (`LedgeRocks`, built again with the perches). A cave's
mouth gets a broad rock over it and four rocks set into the slope round its sides and top, plus three boulders before
it. Check `-bfrocks` (2.5 min): 702 boulders and 198 crags (577 crag rocks), 554k triangles, placed in 0.66 s, 185
frames a second while looking at them. Pictures from two sides of the nearest boulders, a crag and a ledge; the ground
under a boulder marked. The same launch with `-bfchain=bfcave+bfptera`: the cave check and both Pteranodon checks
pass.
Not yet: no Pteranodon used a ledge in the checks (six adults take the first six perches, which are tree shelves; step
D's choosing would send them there). From the side, the cave mouth's rocks still look piled on the slope more than
part of it.

Animals use caves (2026-11-14).
- The island carves its own dens (`PlaceCaves`): up to five, each with its mouth at a cliff's foot (out from the top
  edge to where the slope is under 20 degrees) and gentle dry ground before it for 25 m.
- A cave knows its kind (0 a den, 1 an alcove), the ground just outside its mouth, which way is out, and floor points
  from the mouth in to the back.
- `CaveAt` and `CaveFloorAt` tell the animals when they're inside a cave and where its floor is. `SpawnBody` puts a
  body on the floor, and `GoTo` from inside leads out along the floor and through the mouth first.
- Raptors (small hunters) sleep in the nearest den within 600 m at night: to its mouth, in along the floor, a bed each
  in the chamber; out again by day.
- Den mouths keep only the rock over them. The rocks at their sides walled the way in, and roughness near a mouth left
  a step up onto the floor, so both were taken out.
- Alcoves: up to three shallow caves (8.5 m deep) 6 m under the top edge of cliffs within 600 m of the colony, each with
  a perch at its back. A female carrying eggs moves into a free one (`PerchFor`); the others roost half on ledges and
  half in the trees, so the ledges get used now.
- Checks: `-bfden` all 4 raptors asleep in the den by 100 s, all 4 out in the morning within 30 s (after fixing a loop
  that sent them back out to the mouth); `-bfpteranest` the mother in an alcove, 3 hatched and still in it, fed, 6 real
  landings. `-bfrocks`, `-bfcave`, `-bfptera` and `-bfsave` (34/34) still pass.

Mouths reach their food (Raven, 2026-11-14).
- `ABFDino::SetMouthTarget`: standing, the neck bends down (now to nearly its whole range, 1.9 of `NeckDown`) or up a
  little more each frame till the snout is at the target's height, and the head turns toward it. It learns the lowest
  its snout gets (`GetMouthLowCm`, its neck settled at full stretch) and `FindPlant` skips plants whose leaves are all
  lower than that.
- `ABFLife::MouthTo` turns it to face the food and steps it up or back till its snout is over it, before a mouthful.
  Grazing aims at the plant's leaves (the height in their band nearest its head), drinking at the water's surface, a
  hunter at its kill.
- Check `-bffeed` (3 min at 2x): before, the snouts were 5 m from their food at the middle (Brachiosaurus 10.5 m, all 0%
  within 50 cm); now 32 cm. Triceratops and Ankylosaurus are within 50 cm every time, Brachiosaurus 71%, T. rex at a
  kill 81%, Triceratops drinking 87%.
- Not yet: the one Brachiosaurus drink in the check was 11 m off the water (it drank after 8 s anyway). Its lowest
  snout came out at 6.5 m over its feet, which may be its capsule rather than its legs. Not pinned down.

Rivers on slopes (Raven, seed 2: the water domes up; fixed 2026-11-14).
- The cause: water stood metres above where it would spill. The pits on a hillside are filled as ponds before the
  rivers are cut, and a river cut through a pond's rim left the pond at its old level. Each spot in a river's width also
  took the highest channel level within reach, which on a slope is upstream's. So there were flat sheets of water up to
  6.4 m deep over ground beside a river 5 m lower: a bulge out over the slope, beyond the banks.
- The fix (`MakeLand`, after the rivers are cut and spread), run till nothing changes: no fresh water stands above dry
  ground beside it (it's cut to the bank or its own bed), and none stands higher over the water beside it than its
  bed is over that water's bed, plus 30 cm. A river's surface still follows its bed down; a pond, or water copied from
  upstream, runs down. Points left dry become ground again.
- Drawing: each water vertex beside the water takes the nearest wet point's level, not the highest within two cells.
- Check `-bfriver` (`-bfriverseed=N`, `-bfnospill` for the before): on seed 2, water over 1.5 m deep standing over 1 m
  above what's beside it went from 268 points (worst 6.4 m) to 94 (worst 4.6 m); the drain lowered 1,304. The 94 left
  are cascades, about 1.5 m of water stepping down with a bed that drops 5 m in 4 m, not domes.
- Four places a river runs down a slope, picked by the bed's drop so before and after are the same places, pictured
  side-on from 20 m and 8 m up. Before: a sheet bulging over the slope; after: in its banks, stepping down.
- `-bffeed`, `-bfptera` and `-bfsave` (33/33) still pass on the new water.
- Not yet: the riverbed's 5 m steps make the water a staircase of cascades in places, a little choppy where it steps.

River runs (Raven, "polish it up", 2026-11-14). Before the bed is cut, no river point's level may stand more than 18 cm
per metre above any point up to 24 cells downstream, so above a step the river cuts a sloping chute into it instead of
standing in a flat stair (only ever lowered, so it stays in its banks). `-bfnoruns` for the before. Seed 2: the
standing-water points went from 94 (worst 4.6 m) to 84 (worst 2.3 m); 160 points were lowered. Not yet: steep canyon
stretches still step, and steps would read better with foam (plan item 35).

Realism plan (Raven said yes to all 35, 2026-11-14), worked through in batches, each checked with numbers and two-angle
pictures:
- Eating: 1 Brachiosaurus bends its forelegs to reach water (`ABFDino::FeedDip`), 2 sipping not chewing when drinking
  (`SetSipping`), 3 hunters tear at kills (`TearClock`: jaws clamped, head hauled back and shaken), 4 browsers strip
  branches, 5 grazers step between mouthfuls, 6 raptors pin carcasses with a foot, 7 swallowing head-toss, 8 carcasses
  shrink to bones that fade, 9 blood on the ground and muzzles (full realism), 10 wounds on prey that got away.
- Movement: 11 walk/trot/run blending, 12 turning on the spot with real steps, 13 steady heads (raptors, Pteranodons),
  14 leaning into starts and stops, 15 wading (slower, splashes), 16 careful slopes, 17 tired after a chase, 18 limping.
- Behaviour: 19 alarm freeze and stare, 20 threat displays, 21 rivals spar in mating season, 22 lying down to sleep,
  23 mothers nuzzle and guard young, 24 young play, 25 heads track what they notice, 26 hunters sniff scent trails,
  27 herd spacing and following in a line, 28 calls answered by the herd.
- World: 29 natural cave mouths, 30 footprints, 31 dust, 32 grass bending, 33 better skins and wetness, 34 lighting and
  fog, 35 foam at cascades.

Realism batch 1 (2026-11-14), built and checked:
- Drinking (1, 2): a four-legged animal whose neck is bent as far as it goes, the water still lower, pitches its front
  down on bent forelegs (`FeedDip`, up to 35% of a leg's height; the Brachiosaurus dips 8-17 deg and gets its snout to
  the water). It walks up to stand a neck's length back from the edge (`MouthAheadCm`, learned each time its mouth gets
  there). The Feed idle sips while drinking (`SetSipping`): lips at the water, small draws, a head-lift to swallow.
- At a kill (3, 7): after each bite the jaws stay clamped and it hauls the mouthful free (`TearClock`: a raptor whips its
  head, a T. rex wrenches with its neck), then tosses its head to gulp it. It backs off a carcass its body is standing
  over. The carcass's place follows the ragdoll (it slides metres from where it was struck) and the mouth goes to the
  top of its flank. A hungry hunter no longer leaves meat to rejoin its pack.
- Grazing (4, 5): browsers strip a spray of leaves (the head drawn along the branch and down); ground grazers crop left,
  middle, right across a patch, then step on into it (`CropN`).
- Carcasses (8, 9): blood pools spread under a kill over 90 s (more as it's torn into), wet and bright, drying dark
  over ten minutes, gone in an hour. Eaten past 65% (or its body put away at ten minutes), the body gives way to its
  skeleton as it lay (`CarcassBones`: each bone a rod from its parent, the ribs a cage, the skull an egg), red at first,
  bleaching, sinking away after an hour and a half.
- Blood on skin (9, 10): `M_BoneForgeSkin` has four blood spots (`Blood0..3`, mesh space + radius; `BloodColour`,
  `BloodWet`), broken up by the scales' height. Spot 0 is the muzzle (`BloodyMuzzle`, a mouthful at a kill), 1-3 are
  wounds where it was bitten (`AddWound`: the bone nearest the attacker's snout). Fresh is wet and bright, drying dark;
  water washes it off; it fades as it heals.
- Movement (13, 17, 18): small animals (under 400 kg) hold their heads still while their bodies sway (raptor: head sways
  1.1 cm against the hips' 1.7); winded and standing, the head hangs and the mouth's open; badly bitten (health under
  75%) it limps (the weight off one leg, the head bobbing, up to 45% slower) till it heals.
- Behaviour (19, 20, 22, 25, 28): alarmed, it freezes, head high, staring at the danger (`SetGaze`); startled, it
  holds still a moment before bolting (unless close); the one that raises an alarm calls and its herd answers, one
  after another (`Call`: mouth wide, head up; silent, no sounds yet); facing a meat eater the Triceratops lowers its
  horns at it, the Ankylosaurus swings its club, all bellow; uneasy grazers look up toward what they noticed. Resting,
  once settled, it lies down (`SetLying`: the body lowered to its belly, the legs folding under by the leg IK, the neck
  down, eyes shut asleep) and gets up before it can walk.
- More behaviour (21, 23, 26, 27): matched rivals spar before one gives way (head to head, horns low, butting and
  shoving each other back a step, about six seconds); a mother now and then noses one of her young; a hunter out of
  sight of its quarry follows its scent nose to the ground, searching with its nose up into the wind, creeping with
  its head low and eyes on it; a herd moving to new grazing goes in a line, each in the tracks of the one ahead.
- Movement (16): steep downhill it goes carefully, down to 55-75% of its pace (heavier, slower). Already there before
  this list and left as they were: walk, trot and run blending by speed (11), turning on the spot with real steps
  (12), the neck and tail lagging and overshooting as it speeds up and slows (14), half speed wading (15).
- The world (15, 29-35): `ABFEffects` (new) draws footprints on soft ground (darker the heavier the animal, fading over
  five minutes, none on rock), dust puffs from running feet on dry open ground, and splashes and ripples where a foot
  goes into water; `M_BoneForgeMark`, `M_BoneForgePuff` (colour, opacity per instance). Plants lean away and are pressed
  down round the twelve animals nearest the camera (`MPC_BoneForgePush`, the plant material's "Pushed aside"). Skin is
  darker and glossy out of the water, drying in about a minute and a half (`Wet`). Mist lies low from before dawn and
  burns off by mid-morning, a thinner haze at dusk (`ABFGameMode::SetSkyAt`). Foam where a river's water falls over
  70 cm near the camera (`ABFWorld::TickFoam`). Cave mouths: the lip lies along the slope as a layer of the hill's rock
  with outcrops set into the hill at its sides (raptors still den: `-bfden` 4/4).
- Checks: `-bfworldfx` (mist at 6:30; the raptor runs: prints and dust; into water: splashes, wet; foam at the
  steepest river near). The realism checks run in `TickRealismChecks` (TickAuto grew too big to optimise).
- Checks: `-bfcarcass` (a kill near the start: pools, wound, muzzle, tears, bones, two sides each), `-bfbehave` (night:
  how many lie down; day: a T. rex 70 m from a herd: stares, startles, calls, answers; a bitten Triceratops limps),
  `-bffeed` (now with drinking pictures and tearing pictures).

Look pass (2026-11-15, Raven: "improve how things look; look at the cave entrances from different angles"):
- Cave mouths (`-bfcavelook`: every den from an animal's eye, high in front, both front quarters, side on, far off).
  Seen from those angles, the mouths sat out on gentle slopes like stone tables, and the tunnel broke the hillside open
  behind them as a black gash. Now each den has a knoll (`CaveDensity`, Kind 0): rock over the whole way in, its top at
  least 2.7 m over the tunnel's roof, rising into the hill behind, rounded and lumpy to the sides, a steep face at the
  mouth. Its piece is 11 m wider each side so the knoll falls back to the hill inside it. Set into the face: a lintel
  slab jutting 1.5 m over the mouth and two tall grooved jambs; the boulders before it sit lower. `-bfden` still
  passes (4 of 4 in at night, out in the morning; the way in is clear).
- Footprints: shaped now (a big theropod's three broad toes from a heel pad; a raptor's two, side by side, as
  dromaeosaurs walked with the sickle claw held up; a four-legged animal's round pad with four blunt toe marks), lying on the ground's slope, not level through it. On grass the foot crushes it into brown mud
  instead of a darker green; pressed a little deeper. `MaxPrints` 2000 (5 pieces each).
- Dust: tan dry earth whatever grows there (it was green on the plains), bigger, thicker, hanging 2-3.5 s, thrown back
  from the foot; heavy animals (over 3 t) raise it walking too.
- Splashes: 4-14 drops (more when fast or heavy), whiter and bigger, a crown of foam where the foot went in, a brighter
  ring. The `-bfworldfx` check now stops the raptor for a picture straight down on its tracks (`fx_prints_close`) and
  wades it along the shallows at a walk, the camera close.
  `-bffxkind=trex` (or trike, ...) runs it with another animal. Seen: raptor, T. rex, Triceratops prints; splashes.

Plan, landforms and variety (Raven, 2026-11-15; waiting on Raven's answers before building). Raven's report from a
world launched from the desktop (island 2): no caves found; plants smear where animals walk; rivers cut big unnatural
channels; the lakes all the same depth; footprints at two heights; rocks on the mountainside look right.
Fixes first:
1. Footprints: put each on the ground under the foot (a trace down), and none where the foot is over 15 cm above it
   (a Pteranodon's folded hands left prints in the air). Fading prints sink into the ground instead of fading to a
   colour, which showed as pale discs.
2. Plants smearing: the push moves each leaf away from the foot on its own, so a plant is torn apart. Bend the whole
   plant over from its base instead (its leaves keep their length), less far, easing back.
3. River channels: the channel is cut straight down into the 4 m grid (cliff walls 3-4 m high). Give the banks a slope
   (no steeper than about 35 deg, gentler in soft ground) and let a big river's valley widen; true rock walls only
   where the river runs through rock (gorges, below).
4. Lakes: each has a depth of its own (a shallow marshy pond 0.5-1 m; a lowland lake 2-5 m; a mountain tarn 6-12 m),
   deepest away from the shore, with shallow shelves at the edges where animals drink.
New landforms, each seed rolling which it has (no two islands alike; a small island may have none of the big ones):
5. Caves in three sizes: dens (as now, 0-5); deeper caves (a winding passage to two or three chambers, 30-60 m);
   a cavern in a mountain (a big mouth, a hall 30-60 m across and 15-25 m high, maybe a hole in the roof letting in
   light, maybe a pool). Built as rock pieces on the height map, as the dens are.
6. Ravines: a long crack in high ground (100-400 m), steep rock walls with shelves and big blocks broken off them, one
   end opening to a slope that walks down to its floor, a stream along the bottom.
7. Waterfalls: where a river comes to a drop (a ravine's edge, a cliff, a rock band): falling water, a plunge pool, spray
   and mist; rapids above it.
8. Finding them: a few places named on the O panel (the cave, the falls) with their direction and distance.

Landforms and variety, built (2026-11-15, the plan above; island kept at 4 km, Raven didn't pick a size):
- Footprints sit on the ground under the foot (a trace down; none if the ankle is over 15 cm + 60% of the foot's size
  above it, as a Pteranodon's folded hands were). Old prints shrink away instead of fading to a colour (pale discs).
- Plants bend over from their base, away from feet near them (by where the plant stands, not each leaf), keeping their
  leaves' length; nothing past 4.5 m up. (They were torn into stretched smears.)
- River banks lie back to ~32 deg within 28 m of a river (`BanksLowered`; `-bfoldbanks` for the before); not in
  ravines. The river check's standing-water count is unchanged (84, worst 2.3 m on seed 2: the old canyon steps).
- Lakes have depths of their own (`LakeText`): ponds 0.6-1.3 m, lowland lakes 2-5.5 m, mountain tarns 6-12 m,
  deepest away from the shore, shelving at the edges, lumpy beds.
- Caves in three sizes, each island rolling how many it wants (`CaveSummary`): dens 1-5 (as before); deep caves 0-2
  (Kind 2: a passage winding 35-65 m with one or two chambers, its roof 3 m under the hill); a cavern 6 in 10
  (Kind 3: a 12-15 m mouth, a hall 36-56 m across and ~18-28 m high under 8 m of rock, often a skylight shaft).
  `AddCave` takes per-point `Squash` (flat chambers) and `Shafts`; big caves mesh at 60-75 cm. The cave mesh's
  triangles now face by the crossed edge (rock to air), not the vertex normals (holes to the sky in big caverns), and
  M_BoneForgeTerrain is two-sided (folds). No plants inside a cave's tunnel any more.
- Ravines (0 for 4 in 10 islands, else 1, sometimes 2): started where a stream already runs (a flood-filled catchment
  of 6,500-60,000 cells, 25-170 m up), following it down 150-350 m, the floor climbing ~1 deg from where it opens so
  it deepens upstream to as much as 40 m; walls ~65 deg, ragged; dressed with fallen blocks, ledges and rim fins
  (`DressRavines`). A ravine beside a river's lip isn't its bank (the river goes over, not down a trench).
- Waterfalls: a river drop of 3 m or more at a step is left as a fall (the runs don't ramp across it; `-bfnofalls`),
  recorded top to bottom (`Falls`), drawn as a curtain (`BuildFalls`, M_BoneForgeFall: white water streaming down),
  spray and mist at its foot (`TickFoam`). Seen: 53 m into the ravine on island 13, 26 m on island 20261112.
- Named places (`Places`) on the O panel: each cave, ravine and big fall, nearest first, distance and compass
  direction (+X east, +Y north).
- Checks: `-bfcavelook -bfcaveseed=N` (every cave, 8 views incl. inside), `-bflandform -bfcaveseed=N` (ravines from
  rim, floor and head; the two biggest falls front and side; the places panel).
- Not yet: falls are narrow (a stream's 3 m) and run down the wall's slope rather than leaping out; caverns have no
  pool; ravine walls are on the 4 m grid (smooth faces, no overhangs).

Falls and rivers, more natural (2026-11-15, Raven: "work on the waterfalls and rivers and the terrain around them to
look more natural"). References: a fall drops from a hard caprock lip over a face often undercut beneath it, into a
plunge pool, the falls retreating upstream and leaving a gorge (CU Boulder's East Fork Falls survey,
colorado.edu/program/hydrosciences/2006/04/23); mountain channels by slope (Montgomery & Buffington 1997): pool-riffle
under 1.5%, plane bed to 3%, step-pool 3-6.5% (boulder steps across the bed with pools between), cascade over 6.5%
(a boulder bed).
- Each fall of 5 m or more is reshaped in MakeLand (`-bfoldfalls` for the before): all its drop at the lip (the points it
  stepped down are carved to the pool), a round plunge pool at its foot (radius a fifth of the drop, 6-16 m; 1.5-6 m
  deep) narrowing to the river's channel, its sides rising at ~60 deg, ragged (a gorge below the fall).
- The curtain leaves the lip as water does: out at 2-3 m/s as it falls (x = v t, z = g t^2 / 2), spreading, kept 60 cm
  off the face, at least 6.5 m wide (it covers the river's own steep surface down the face). A caprock slab juts over
  the lip; 4-8 blocks lie round the pool, half in the water.
- Boulders in the rivers (`DressRivers`, up to 1,400): where the water falls over 25 cm across a point (6%), boulders in
  the bed and along the edges poking out of the water, more and bigger the steeper (step-pools, cascades); a few on
  gentle reaches.
- A river's or lake's edge is wet silt and gravel (two points wide), bare rock in a gorge, not grass to the waterline.
- Ravine walls ~60 deg (65 stood as flat plates on the 4 m grid).
- Check: `-bflandform` also pictures the steepest river near the start (rapids, from beside it and down it).

Undercuts and rough gorge walls (2026-11-15, Raven: "add the undercut behind the falls and make gorge walls rougher"):
- The hollow behind a fall (falls of 8 m or more; `-bfnoundercut` for the before): a rock piece (cave kind 4, the 4 m
  ground can't overhang) cut into the face at its foot, a low wide hollow behind the curtain: radius 3.8-7.5 m,
  squashed 1.5 (about half as tall as wide), 1.1 radii back into the face, a dry ledge 40 cm over the pool, its roof at
  least 2.5 m under the lip. The rock face round it is rough in 3D (lumps up to ~1.4 m and flattened beds, on steep
  ground only). No knoll or mouth rocks, no name; ptera nests and the cave check leave it alone.
- The river's own water surface is no longer drawn down a fall's face (3 m or more across a cell): the curtain is its
  water there, and that sloping sheet hid the hollow.
- Fallen blocks lie round the pool's sides (55-150 deg off the fall's line), not toward the face; no rock is set where
  a hollow will be cut (`NearFallFace`): they hung over it on ground it cut away.
- Gorge walls (`-bfoldwalls` for the before): on every steep (over 40 deg) gorge point, ravines and below falls, 4 in
  10 get rock set into the wall, 45% of its depth buried: flat ledges (strata, 5-13 m long, 1.7-3.2 m thick) or rough
  blocks, up to 2,600. Ravine walls' steepness varies along them (+/-32%, 45-70 deg).
- Blended in (Raven: "blend in better like they're part of the wall and not just thrown in"; the hollows needn't
  show): the wall's rock is now in beds at set heights across the island (one every 3.5-6 m of height, dipping
  gently), each bed a long ledge (6.6-11 m) where it crosses a steep gorge point, its top at the bed's height, 70%
  of its depth in the wall (out 60-90 cm), so neighbouring ledges join into one line along the wall with a few gaps;
  a block now and then (1 in 10 points). Wall rocks, the ravines' own ledges and a fall's caprock take the ground's
  colour where they sit (`FBFRock::Tint`, no moss), their facets shaded part way to the wall's facing (`WallN`).
- Stepped cliffs (Raven: the thin lips looked "like a bunch of shelves in a kitchen"; expand them down into the
  ground): each bed is now a tall block, its front a sheer face from the bed's top 75-100% of the way down to the bed
  below (standing out from the wall up to 3.8 m at its top), its top the ledge, its foot and back 1.8-2.2 m into the
  ground. Stacked, a gorge wall is rock all the way: sheer faces and ledges like a stepped sandstone cliff. A bed's
  edges are squarer (rounding 12% of its least size, not 35%) and its face is weathered in horizontal bands with
  joints top to bottom (`RockDensity`, `bBed`); its shading leans 25% to the wall's facing, not 45%. Checked on
  islands 13 and 20261112 (front and side of the falls).
- Round two (2026-11-16, Raven: "keep going, if it's still not right make it better"):
  - Spires gone: a last pass in `MakeLand` (after the banks pass, which could leave new ones) takes an opening of the
    ground by gorges and falls (lowest round each point, then highest of those): everything under 12 m wide is taken
    away and no point stands over 1.5 m above it. Blades two points thick go too; walls (edges of wide high ground)
    keep their height. `-bfoldspires` turns it off; the world line logs "N spire points cut" (123 on island 13).
  - Fall faces dressed: only the water's own strip is kept clear (its width + 3 m), a pool keeps its shore (no bed
    under 3 m over the water), and the hollow the face's foot (no bed under 16 m within 13 m of the fall's line).
    Beds now go on the gorge, its rim (2 points out), anything within 40 m of a fall's foot, and any sheer face over
    ~70 deg anywhere. Each point's beds reach half way to the ground a point up and a point down, each side its own
    slope, so a 63 m face is covered top to bottom (it was bare above half way). Cap 7,000.
  - A bed with no bare rock under it takes the nearest rock's colour (or the island's average rock), not the grass
    on a rim.
  - Bed lumps capped at 55 cm (big blocks in narrow gorges were bulgy); beds are meshed finer (`Res` Mean/4.5, up to
    55 cm) so a flat top meets its face without a row of teeth. Island 13 now takes ~12 s to be ready (was ~7).
  Rock shapes split their quads the same diagonal-picking way as the rock pieces (no dark flecks).
- Natural beds (2026-11-16, Raven: they "look like man made blocks and not natural"):
  - Uneven beds: the island has its own bed levels, each gap 0.45-1.6 of `BandM`, shared by every wall. Thin (soft)
    beds stand out 60% as far as thick (hard) ones, so hard beds jut as cliffs with soft ones weathered back.
  - A bed's ends pinch back into the wall (no square ends); its front's in-and-out, thin layers and joints come from
    the world position, not the block, so overlapping blocks carry on as one rock. Joints are wandering cracks top
    to bottom through every bed, coming and going. The top is uneven, dips a little to the front, drops toward the
    ends, and is rounded into the face (smooth min, 75 cm). All this grain is scaled to how far a bed stands out.
  - Colour: banded by height the same everywhere (no shade per block), a little lighter on ledge tops; taken only
    from dry, grey (not green) rock (`IsBare`); the log's gorge-walls entry gives the island's rock colour.
  - Beds face the wall's way over 20 m (a 5x5 slope), not their own point's: a sheer face on the 4 m ground is a
    stair of columns and beds turned to each column stood as stacks. On sheer faces (over ~76 deg) only hard beds,
    half of them, 2.2x as long: a few long ledges across a mostly bare face. Shading normals of beds tipped up a
    little, like the ground's smoothed normals.
  - The ravine pass's own wall ledges are off unless `-bfoldwalls` (they were tinted by wherever they landed).
  - Island 13 now takes ~20 s to be ready (was ~12): the beds' density costs more.
- Falls and water fixes (2026-11-16, Raven's pictures from islands 906 and 22436):
  - Spikes 15-30 m round a big fall's pool were river points of the old cascade beside the pool: the fall reshape only
    brought land down. Now river points there come down too and dry out (water left over lowered ground is cleared).
    The last spire pass also runs within 2 points of any water.
  - Steep river squares (3 m+ across a point) were all left out, so a steep reach was a row of loose diamonds. Now a
    square with 2+ wet corners (none deep, under 5 m tall) is drawn as white water (M_BoneForgeFall), a bank corner at
    the mean of its wet corners. Only a reshaped fall's own face is left open for the curtain.
  - The curtain starts 1.4 m back over the river's surface, meeting it, and is pushed off the face a row at a time
    (its edges spiked out over the bank point by point). Falls under 5 m (not reshaped, water in steps) get no curtain:
    their water runs down as rapids.
  - The caprock bar over the lip is gone (`-bfcaprock` puts it back); only the walls' own beds take the bed shape
    (`FBFRock::bBed`); bare rock for colour must be grey, not tan or olive.
- The sea (2026-11-16, Raven: "the map is mostly ocean ... a bigger island or some smaller masses out in the sea"):
  the island's coast is further out (0.7-0.97 of the way to the edge, was 0.62-0.92; ~10 km2 of land, was ~8);
  5-9 islets (90-440 m across: knolls and low cays, with shallows round them) and 4-8 sea stacks out past the coast;
  the sea floor has banks and shoals (some 2-6 m down) and deepens toward the edge. `-bfoldsea`: as before.
  `-bflandform` takes two views from high over the coast (`land_sea_ne`, `land_sea_w`).
- Terrain polish (2026-11-16, Raven: "do a terrain polish and check things from multiple angles"):
  - `-bflandform -bfterrain`: a tour instead of the falls: two of each biome (beach, plains, forest, swamp, rock, lake,
    river), the peak, a sea cliff and an islet, each from 2.6 m off the ground and from high and far on another side
    (`tour_<place>_low/high`; `ABFWorld::TourPoints`).
  - The ground's grain (`GROUND_HLSL`, rebuild with `BF_TERRAIN_ONLY=1` + `run_bf_materials.py`) was two dinosaur
    scale tiles laid flat: everything read as a cracked tiled floor with pale ovals. Now procedural noise only (30 cm to
    25 m, no cells): grass in clumps with yellower dry patches, sand with faint ripples, bare rock (steep or grey) banded
    by height with darker streaks.
  - Mountains: gullies and spurs 40-80 m apart, up to ~9 m deep, on the ridged mountains before the rain runs
    (`-bfoldsea` turns them off with the sea).
  - Tried and left off: rock outcrop beds on steep mountain slopes (`-bfoutcrops`: pale bars, and island 13 went from
    25 s to 86 s); a notch cut into a fall's lip (`-bflipnotch`: at a lake's outlet it stepped down in a box).
  - Falls: the curtain is the river's width + 1.5 m (was 1.6x, under the banks); ground by a fall's lip is wet grey
    rock, not tan silt; the river's surface is drawn up to the lip.
  - Still: a big fall's lip seen from below is blocky (the 4 m ground steps in columns beside it).
- Waterfalls round two (2026-11-16, Raven: "work more on waterfalls"):
  - `-bflandform` takes two more views of each big fall: `fallN_lip` (up the river, low, over the edge) and
    `fallN_lipside` (from high on the bank beside it). The world log prints the ground round each fall's lip
    (`around dy=..`: ground / water / biome per point, `ABFWorld::LogAround`).
  - Falls one straight after another (a fall's foot within 14 m of the next one's top) are merged into one (island
    906: a small fall from a lake into its own pool, then the 128 m one, stacked two pools into a box at the top).
  - A lake's outlet: last of all in `MakeLand` (the passes after the falls' shaping set the river's levels again),
    where the water within 6 points upstream stands 1.5 m or more over the lip, the channel to the edge is filled to
    it and the banks beside it set just over it: the fall leaves the lake's own surface through a notch. The older
    in-loop version is `-bfinlooplip`; `-bfoldlip` turns off the merge and the outlet.
  - Wall beds whose length (not just their middle) crosses a fall's strip are left out (one hung over island 13's
    curtain); the strip is the curtain's width + 0.9 m (was + 3 m); no loose boulders within 45 m of a fall's top,
    and the pool's blocks only by the pool (at most 2.6 m; one sat on island 13's lip as a 14 m boulder).
  - Ground within 32 m of a lip is bare wet grey rock.
  - Still: the banks right at island 906's lip are tan; island 13's big fall now shows as two tiers.
 Every seed's island has
  changed shape, so an island saved before this will load with its animals where the old ground was. Island 13
  now takes ~25 s to be ready.
- Lone spires by a gorge brought down to 1.5 m over the third highest point round them.
- Rock pieces' quads split across whichever diagonal keeps both halves facing out (folded quads showed as dark flecks).
- Checks: `-bflandform` takes 4 views a fall (front, side, the hollow from the pool, inside it looking along);
  `-bfnocurtain` hides the curtain and spray to see the rock bare.
- Still: tall blade-like spires stand beside the 63 m fall on island 13 (the spire pass didn't take them: not lone
  points); island 2's 15 m fall is dense enough that the hollow barely shows through it.

Editor (Raven: "I don't know how to make the wings larger so it can fly or edit the base body"):
- Under Start from: what the body is (the trunk and its flight muscle come from the animal you start from; picking
  another starts over, Undo brings it back), and a Whole body Size (compose.py's `size`, 10-300%).
- The Arms row reads Wings when its arms are a Pteranodon's (Size +/- there makes the wings bigger or smaller).
- Under the flight readout, what to change to fly (`FlightHelp`): only a Pteranodon body has the flight muscle;
  wings from Pte in the Arms row; bigger wings (Size +, or the finger bones); or lighter.
- compose.py: the flight check is worked out again after a whole-size change (it kept the 100% numbers; at 123% it now
  reads 224 N/m2, at 160% grounded). splice.py says "1.2 times the limit", not "1 times".
- Check: `-bfclicks -bfwings`. The torso itself still can't be lengthened or thickened on its own.

Plan, cliffs and caverns (Raven, 2026-11-14). The island is a height map, so it can't hold an overhang or a
hole: anything under rock has to be carved into it or built on top of it. In order:
A. Rock bands: on steep high ground, step the height map into layers (a flat shelf 2-6 m deep every 8-20 m of height,
   the faces between them steeper), so the ledges are part of the mountain, in its rock colour. Fliers roost on them.
B. Rock shapes: irregular rock built in the game (a noisy, faceted lump, like the plants are built), for slabs jutting
   out of the faces, overhangs, boulders at the foot, fins on the ridges. They replace the plain grey slabs.
C. Alcoves and shallow caves: dig a pocket into a cliff face (the height map lowered to a floor), then roof it with a
   rock shell that meets the face. Each is a real hollow, a few metres to about 15 m deep, that a small animal can walk
   into. Real tunnels through a mountain would need a different kind of ground and aren't in this plan.
D. Fliers choose: alcoves for nests (out of the weather, hidden, out of reach of raptors), open ledges and the shelves
   in the trees for roosting, ledges that face the wind for taking off. A count of what each one is used for.
E. Later: other animals use them (shelter at night or in bad weather, hunters denning).
Check: pictures of each from two sides and from the air; fliers land in and nest in the alcoves; nothing walks
through the rock.

## Acting like real creatures (plan, 2026-11-13)

Raven: "I want the ai to act more like real creatures". The long generations runs showed why it matters: herds stripped
the ground round one spot and starved, hunters never found prey, animals turned back and forth between hunger and thirst.
Six parts, each with a check, then the long run again.

A. Seeing things through, and a day's rhythm. An animal finishes what it starts (eats to full, drinks its fill, rests till
   rested) unless danger comes. A day: plant eaters graze most of the daylight, most at dawn and dusk, rest in the midday
   heat, sleep together at night; raptors hunt at dawn, dusk and by night; a T. rex hunts by day and sleeps a big meal off
   (the bigger the meal the longer). Check `-bfday`: one island day, each kind's time spent on each act against its
   budget.
B. Herds that decide together. A herd moves when enough of it starts the same way (a quorum, the leader counting most),
   travels together, drinks together, rests together; while it grazes one or two keep watch heads up, by turns (a watcher
   sees further and calls the alarm). Check: how spread out the herds stay, how often they act together, how far off a
   hunter is seen with and without a watcher.
C. Home ranges and known places. Each herd learns its range as it goes (the patches it has grazed and how long ago, the
   water, the trails, where danger was), shares it with the herd and the young learn it from their mothers; it grazes
   round a circuit so a patch has time to grow back. Hunters hold territories, patrol between the herds' grounds and the
   water, wait in ambush at water holes. Check: three island days; herds stay fed (half full or more on average), the
   patches they use change, hunters find prey.
D. Hunting as it really goes. Most hunts fail (about one in four succeeds): the prey's watchfulness, its speed, the
   cover, how close the hunter got before it was seen; hunters pick the young, old, hurt and the stragglers; a big kill
   feeds them for days and they guard it; raptors shadow a T. rex for scraps. Check: kills per hunt, and who is taken.
E. Rank and family. Contest winners rank higher: they eat first, mate first, keep to the middle of the herd (the safest
   place), the low-ranking at its edges. Mothers keep their young close; fed young play (short chases); the young learn
   the range from their mother. Check: ranks settle and last; how the young fare with a mother and without.
F. Balance and the long run. `-bfgens` 90 minutes again with all of it: no kind dying out (newcomers barely needed), plant
   eaters filling their room, about one hunter to seven of them, the genes charted, the line-up pictures.

Built (A-E, 2026-11-13; `ABFLife::Think`/`HunterThink`/`Do`): grazing peaks 5-9 and 16-20, a midday rest 11-14:30 (not
when hungry or thirsty), rests seen through; herds drink, graze and rest with the others sooner; a herd that moves on is
followed to its new ground; a watcher by turns (every 45 s the next) while the herd grazes, seeing 40% further; the young
play (short chases) fed, safe and in daylight; raptors hunt more at dawn, dusk and night; a hunter over 70% full lies up;
`FBFHerd::Patches`/`PatchAt` (grazing grounds, back to one grown back after half an island hour) and `Waters` (shared;
an animal without water of its own takes the herd's; a hatchling copies its mother's memory); hunters make for the
herds' grounds and lie in wait at their water (only a herd that wouldn't stand against the pack); prey choice favours
the young, the old and stragglers (not under a herd that would stand), a herd that would stand is left alone unless the
hunter is starving; small hunters shadow a big one hunting or feeding; contests won and lost give rank (±5% a step in
the next contest), the low-ranking keep wider spacing (the herd's edge). Counts in the watch line: charges, kills,
ambushes, herd moves, splits. Check `-bfday` (an island day from 4:30, each hour each kind's acts).
Fixed on the way (five rounds of `-bfday`): hunters were all killed by the herds they ambushed or attacked (now: leave
herds that would stand, back off when charged by something twice the pack or gored by two, no feeding while gored); a
herd faced a still hunter for six hours (fear fades when the threat keeps still and far, back to grazing with a watcher);
seeing a walk to water through had no end (now while it gets 10 m nearer every 40 s; stuck within 12 m it drinks where it
stands; water it couldn't reach is remembered and avoided); hungry young only followed their mother (now they graze near
her); a Triceratops tipped over and over on a bank (it gets up on the flattest ground within 4 m). Day 5: nothing
starved, no tip-overs, 63 drinks against 4 give-ups, both T. rex and all three raptors alive, 2 kills in 17 charges.
`GoTo` still reports a path found even when the search fell back to a straight line (the checks above rely on progress
instead).

### Step 1: growing up (2026-11-13)

`ABFLife::StageBody`/`StageKey`: hatchling 15% of the adult's length with a 140% head, juvenile 30% (130%), subadult
60% (110%); the raptors (under 50 kg grown) 30%, 50%, 75%. Keys `ed_<kind>_he<head>_z<size>`. `Grow`: each young
animal's age (game seconds since hatching, times `GrowRate`) moves it on: a hatchling to 1.5 h, a juvenile to 4 h, a
subadult to 9 h, then an adult (with the hour in the egg, ten hours). Its next body is spawned where it stands and the
old one goes; `ApplyBody` works its senses, mouthful, stomach (as full as it was), reach, wading depth, speed and armour
(half an adult's for the young, 80% for a subadult) out again. Bodies come through one queue (`BodyIndex`,
`TickYoung`): grown by compose.py if there's no rig yet, made ready by build_glb.py, brought in at runtime; a body that
fails is not asked for again. Hatchlings and juveniles keep to their mother; subadults go about as adults do.
Check: `-bfgrow` (two hatchlings more for each herd, growing 600 times as fast).

### Step 3: saving the island (2026-11-13)

`bone_forge/islands/island_<seed>.json`, written by `ABFPlayerController::SaveIsland`: every two minutes on the island,
when you make or go to another island, when the game closes, and with the menu's Save the island. It holds the island's
clock and every eaten plant with the kg left on it (`ABFWorld::WriteState`; the uneaten plants come from the seed), every
living animal (`ABFLife::SaveState`: its body's key, name, herd, mother, kind, stage, age, growths, place, how full and
thirsty and tired, health, kills, memory of food, water and danger), the herds and the carcasses, and you (your creature,
place, needs, health). When an island is made from a seed with a save (`RestoreIsland`), it comes back as it was: the
clock and plants at once, the animals each into its body (`TickAwaiting`; a growth stage's body is brought in first if
it isn't yet, so a new session may take a few seconds per stage body), and you as what you were, where you were. Start
it afresh deletes the save and makes the island new. Kept islands with a save say "(saved)". The checks never read or
write saves except `-bfsave` (which moves any real save of island 20261112 aside and puts it back).
Growing doesn't go on while the game is closed: ten hours means ten hours played.

### Steps 4-6: courtship, nests, eggs, hatching (2026-11-13)

Sexes: a herd's first animal is female (the mother), then male, female, ...; a hatchling's is chance. A female comes into
condition (`ABFLife::IsReady`) grown, over half full, healthy (70%), not parched, not carrying or minding eggs, her
`ClutchIn` run out (`BreedEvery`, 2,880 game seconds: two island days between clutches), and fewer than `MaxAnimals`
(450, what `-bfstress` says the game runs) alive. Courting (`EBFAct::Court`): a fit, fed male goes to the nearest female
of his kind in condition within 600 m and displays beside her (head up, jaws wide, looking round) for 10 s; another male
of his kind courting within 50 m is a rival: once both have displayed the weaker (weight times health, with some luck)
gives way and leaves her be for 3 minutes; matched within 25%, they shove first (3-12% of the loser's health). Unrivalled,
she takes him: carrying eggs (`bGravid`), she lays after `Gestation` (600 s). Nesting (`EBFAct::Nest`): a site 15-70 m
off (dry, gentle, away from other nests; open ground for the big ones, cover for the brooders), a 12 s scrape, a clutch
sized by her body (`1.5 log10 kg`: 3 for a raptor, 6 for a Triceratops, Ankylosaurus or T. rex, 7 for a Brachiosaurus),
each egg 1/3000 of her weight (0.2 to 6 kg), drawn in a ring in a scrape. Care by the fossils (`CareOf`): Brachiosaurus
lay and leave, raptors brood (sit on the eggs), the rest guard (stand a body length off, looking round; stand their
ground over the nest against anything under 1.5 times their weight). Eggs take `Incubation` (3,600 s: an hour) and hatch
one at a time into hatchling bodies (stage 1): with their mother if she minds them, else as their own group. A nest is
also a carcass for raiders (`FBFCarcass::Nest`): hunters smell it as they do a kill (not their own pack's), wait while a
guarding mother much bigger than the pack is near, and break into eggs (each egg broken into is gone).
Deaths that need no hunter (`Toll`): an empty stomach or a parched throat costs health (10 minutes from whole to dead),
fed and watered it heals slowly; the young die by chance (a hatchling's stage survived 2 in 5, a juvenile's 7 in 10;
`YoungDeathRate`, 0 in `-bfgrow`). All on the growth clock (`GrowRate`). The watch panel (O) shows matings, contests,
nests, eggs laid, hatched and eaten, the young that died and the starved. Saved: sexes, clutches, fathers, nests.
Check: `-bfbreed` (every adult female in condition, everything 60 times as fast, the raptors put by a Brachiosaurus nest).

Polish after Raven's "not right" list (2026-11-13): the scrape is rough dark earth (the basic material's sheen made the
flat disc mirror the sky, grey); the T. rex is a pair (a female and a male) so it can breed; a brooder stays on her eggs
till near starving (score 0.95), a guard stands 0.8 of her length off them; a hatchling hunter doesn't hunt (it feeds
from kills) and young hunters keep to their mother; a courted female's thought says who's courting her; the young's
labels show only within 40 m (a clutch was a heap of text). At the game's own pace (`-bfbreed -bfslow`, 21 minutes):
6 pairs mated (T. rex too), 34 eggs in 6 nests, raiders found nests and ate 6 eggs, nothing hatched (an hour). That run
had 19 rival contests (the two Triceratops males over each female in turn): a beaten male now keeps out of courting for
600 s of the growth clock. Starving takes two island days from an empty stomach, dying of thirst one (10 minutes had
starved young that spent a check fleeing).

### Step 7: inheritance (2026-11-13)

`FBFGenes`: size (the body scaled, `SetActorScale3D`, its weight with the cube), hue and shade (on its kind's look, or
your look for that kind, `ApplyLook`), strength (its weapon, its weight in a contest) and speed (`ABFDino::SpeedGene` on
every gait). The first animals: a bell-shaped spread round their kind (size about ±3%, hue ±7°, shade ±0.05, strength
±5%, speed ±3%; clamped to 0.9-1.1, ±25°, ±0.2, 0.8-1.2, 0.88-1.12). A hatchling: the mean of its mother's and father's
and half the founders' spread either way (a family resembles itself); its generation one past its elder parent's.
Applied to every body it wears (`ApplyGenes`: spawning, growing, getting up, coming back from a save); saved. The log
line of each hatching gives its genes and its parents'; the report each animal's sex, generation and genes; the watch
panel the generations so far. Check (`-bfbreed`): 64 young, sizes 0.95 to 1.06, all 51 with both parents known within
0.06 of their mean.

### How many animals the island holds (`-bfstress`, 2026-11-13)

The island, you a Brachiosaurus, the herds; every 20 s fifteen more (a herd each of Triceratops, Ankylosaurus,
Brachiosaurus, raptors, a T. rex, 300 m to 2 km from you), each step's frame time measured over 15 s (whole, game thread,
render thread, graphics card, their minds), until a frame takes 50 ms (20 a second) or 40 steps. On this PC:

| Animals | as they were | pose by distance | + quicker senses | + far movement, balance near only |
|---|---|---|---|---|
| 100 | 17.7 ms | 9.4 ms | 9.5 ms | about 8 ms |
| 200 | 34.8 ms | 18.6 ms | 17.8 ms | about 15 ms |
| 300 | over 50 | 31.5 ms | 27.7 ms | 25.6 ms (316) |
| 440 | | 49.2 ms | 36.7 ms | 34.0 ms (454) |
| 530 | | | 46.1 ms | 40.7 ms (533) |

20 frames a second at about 270 animals as they were, 440 with the pose by distance, 520 with quicker senses too,
and past 533 (the check's 40-step limit) at the end. Balance (tipping over) is judged only for bodies posed every frame:
judged on a pose a few frames old, far bodies tipped over hundreds of times a run; none now.
The cost was the bodies, not the minds: each body worked out its pose (legs, tail on the ground, skin, balance) every
frame wherever it was. Now `ABFDino::DetailEvery` (set by `ABFLife` every frame): within 100 m and in view every frame,
to 300 m every 2nd, to 800 m every 4th, beyond or out of view every 8th (the time between made up in one step); their
movement 30 times a second at every 4th, 15 at every 8th. The minds grew with the cube of their numbers (each animal's
senses looked every hunter up in the whole list, twice): hunters beyond what it could sense are skipped first and
`IndexOf` is cached; minds 0.6 ms at 250 animals (was 1.9). `-bffulldetail` turns detail by distance off.
Food: the island's leaves grew back so fast it could have fed about 25,000 Triceratops, so food never decided anything.
`ABFWorld::RegrowRate` (0.025: a fortieth of each kind's own pace) brings that to roughly the mix the game can run;
`-bfstress` prints what the leaves in each kind's reach could feed.

## Look: colours and coverings (lab plan item 3)

L opens a panel for the animal being played: whose colours (its own or any of the six's schemes), turned round the colour
wheel (Hue, 15° a step) and lighter or darker (Shade, 10% a step), and whose covering (pebbly scales, big cone scales,
feathers, armour plates, sauropod skin, fuzz: each species' own tiles). Its own look follows the fossils, and the
panel says what they show. `ABFDino::ApplyLook` sets the skin material's parameters live (`BackColour`..`AccentColour`,
the four region tiles, `TileCm`, `BumpCm`; the toes keep their keratin, the eyes take the scheme's iris), so nothing is
rebuilt; a borrowed covering's tiles are sized to this body by the square root of the lengths (a T. rex's feathers 22 cm a
tile against the raptor's 9). The six schemes are a C++ copy of `bone_forge_skin.py` SKIN (`BFLook.cpp`: keep them in
step). Looks are kept per creature in `bone_forge/my_creatures.json` under `looks` (the six's too) and put on whenever it
appears. The pattern's placement (saddles, bars, mottling) is painted into the body when it's built, so it stays the
body's own: the raptor's cream pattern colour on a T. rex lands on the T. rex's saddles as bold stripes. A covering is
surface detail, best seen close; per-section coverings (feathered arms only) would need a mark on the skin at build
time, not done. Check: `-bflook` clicks the panel's buttons on a T. rex (raptor colours and feathers; then 60° round the
wheel, 20% darker, fuzz) and a Brachiosaurus (Pteranodon colours, armour plates), side and front pictures, checks the
look survives a respawn, and puts both back to their own (`look on <key>: ... back tile now T_.._H, 4 of 4 tiles loaded`).

## A second animal beside yours (lab plan item 6)

Every creature in the menu has Beside you: a second animal (`ABFGameMode::SpawnCompanion`, no controller, its physics
running anyway: `bRunPhysicsWithNoController`) next to yours. The menu's line above the list sets what it does: Stay
(it idles), Follow (it walks to a spot beside you, runs when far behind, stands once there), Copy me (your move, your
Shift and your Space, so the two gaits run side by side from one command) and Send away; Person for scale puts a plain
1.8 m figure on your animal's left. They come along when you pick another creature. Not AI: it does what you do or goes
where you are. Check: `-bfpair` (2026-11-12): a Velociraptor beside the T. rex copying W+Shift ran 8.6 m/s against 7.5,
the same heading, 13.5 m ahead after 6 s; following a walk it kept 4.4 m off and stopped 4.1 m away; person, send away.

## Skin folds (lab plan item 8, first try)

A knee or elbow bent past its rest bunches its skin on the side it closes on. `ABFDino::TickSkinDetail` sends, for each
walking limb's middle joint, `FoldA` (the joint in rest mesh space, its bend past rest over 60°), `FoldB` (the side it
closes on, seen from the upper bone's rest frame, and the zone: 0.45 of the lower bone) and `FoldC` (the lower bone's rest
direction); `FOLD_HLSL` in `bone_forge_materials.py` puts ridges across the limb there, a quarter of the zone apart:
valleys darkened (colour), ridges pushed out along the skinned normal (up to 0.15 of the zone). Material only: nothing is
rebuilt, any creature has them, and they come and go with each stride. Rebuild the material with `run_bf_materials.py`.
Check: `-bffolds` runs the T. rex and Triceratops, pauses at the deepest knee bend (43° and 50° past rest), and takes the
same frame from the side and from behind with the folds on and off (`_fold_side_on/_off`, `_fold_back_on/_off`;
`-bffolddebug` makes one fold zone cover the whole body, to see the material respond at all). The first strength
(ridges 0.07 of the zone, valleys half as dark) didn't show in the pictures; the second does, as curved ridges round the
back of the knee. If it doesn't read as real skin, the next step is fold shapes built into the body (morph targets).

## Jumping (lab plan item 4)

Space on the ground jumps, for an animal without wings (with wings Space still takes off). How high comes from the leg
check (`FBFSpecies::Load`): the walking pairs' muscle (`muscle_n`, summed) over the weight they carry (`load_n`), times
`BFJumpLever` 0.3, the share of a muscle's pull that reaches the ground through a leg's levers, is `JumpPush`, the push
over its weight. The hips drop a quarter of their height (`JumpCrouchCm`) and push from there; past 1 it leaves the ground
and rises crouch x (push - 1), at most `BFJumpMaxCm` 150 cm (Borelli's law: the work a kilo of muscle can do, so animals
of any size jump about as high). At 1 or under it crouches and heaves without leaving the ground, and the status line
says why. `ABFDino::TickJump`: Crouch (the hips drop by `CrouchCm` on top of the leg IK's own `HipDrop`, quickly, the
feet held), launch (`LaunchCharacter`, up only, so a running jump goes on forward), Air (no leg IK; `FBFPoseInput::Tuck`
draws the walking legs up as in mid-swing, each in its own range), Land (a crouch as deep as the fall was fast, then up).
A jump lane on the field south of the spawn has logs 0.5, 1 and 2 m high. Check: `-bfjump` (`-bfonly=` works) presses
Space on each animal (a winged one's legs are jumped straight), pictures `_jump_crouch`, `_jump_top`, `_jump_land`, and
logs `jump <key>: worked out .. cm, rose .. cm`. 2026-11-12: Pteranodon 139 / 138.6, Velociraptor 71 / 70.7, T. rex 29 /
29.2, Ankylosaurus 10 / 9.6; the Triceratops (0.99 times its weight) and Brachiosaurus (0.73) heave and stay down. The
first picture of a run stalls the game for about half a second: taken mid-jump it cost the T. rex 12 cm, so the check
takes a standing picture first, and a landing only counts once it is coming down. Mid-jump the Pteranodon's membrane
hangs oddly from its tucked legs (only the check jumps it: with wings, Space takes off).

## Watching: slow motion, pause, free camera (lab plan item 7)

`,` slows the world (full speed, a half, a quarter, an eighth) and `.` speeds it back up; `P` pauses and `/` lets one
frame through while paused; `V` swaps to a free camera; `U` hides the on-screen text; `F9` saves a picture without the
text to `Saved/Pictures/<key>_<date>_<time>.png`. Time is the world's (`UGameplayStatics::SetGlobalTimeDilation`; paused is
its smallest, 0.0001), so strides, breathing, the jaw, feathers and physics all slow together. The free camera
(`ACameraActor`) runs on real time (`FApp::GetDeltaTime`), so it flies at its own pace in slow motion or paused: WASD,
Space up, Ctrl down, Shift faster, the wheel its speed, the mouse to look. It moves with the animal, so a walking or
running one stays in view, and an animal that was moving when it went on (W, with or without Shift) keeps going
straight at that pace. Picking a creature or respawning puts the view back on the animal. The status line says what is
on (paused, slow motion 1/N, free camera). Check: `-bfslowmo` taps each key through Slate, then walks the T. rex.
2026-11-12: at a quarter, 2.00 game s in 8.00 real s and the same 0.442 strides a game second as at full speed; paused,
it stays put; the free camera flew 803 cm in a real second while paused and followed the walking animal within 2.3 cm.

## Realistic animation (roadmap step 7)

Following the design doc's Movement tab (its motion rules and small movements), checked against real animals (below).

- **Gaits from the legs** (`splice/splice.py splice_gaits`): the six keep the doc's gaits; a splice's are scaled from the
  species it was built on by dynamic similarity (speed with the square root of leg height, stride in proportion) and by
  its legs' strength for their load (the leg check's ratio). Running or bounding needs legs 4 times their load, trotting
  2.5; weaker ones fall back to a fast walk under Froude 0.5. Written to the rig's `gaits` and `gait_check`.
- **Breathing**: the ribcage (spine_02 to chest) swells 1.2%, the neck and arms keeping their size; the doc's bird rule,
  17.2 M^-0.31 breaths a minute (`BFBreathsPerSecond`: 0.9 for the T. rex, 6.5 for the raptor), up to 2.5 times as
  fast and deep when winded by running or swimming. Winded and standing, it pants with the throat fluttering.
- **Secondary motion** (FBFPoser): the tail bounces with each footfall; the neck holds the head level against the hips'
  bob (bipeds: the T. rex's head moves under 1 cm to its hips' 7; short-necked quadrupeds still nod, as horses do);
  the head going down lifts the tail; a sprung surge makes the neck, tail and carried arms (the raptor's feathered
  wings) lag and overshoot on starting and stopping; carried arms swing with the stride. Two-legged walkers sway their
  hips toward the standing leg (a sixth of the hip width, the doc's rule) and roll a little; heavy four-legged ones sway
  by mass (at most 2% of hip height). The belly jiggles: a damped spring the stride's bounce and landings drive (about
  4 Hz in the raptor, 2 Hz in the giants; up to 4 cm on a running T. rex), shown by the skin material's world position
  offset on the belly (vertex colour R) between the hips and the shoulders (`Jiggle`, `BellyX0`/`X1` set per frame on
  dynamic instances of every skin slot).
- **Feet stay planted** (SolveLegIK, the doc's first motion rule): walking, a planted foot holds where it landed (fore
  and aft once the walk is under way; sideways always), the leg taking up the ankle's arc about the toes; the swing cap
  is 90% of the hip's range (two thirds left the Triceratops' and Ankylosaurus' strides short). Held feet are reached
  within 3 cm (the Brachiosaurus 9: its legs can't stretch to the end of its stance). Running, only sideways. Standing,
  feet hold while an idle behaviour moves the body (at most 1.6 cm of slip), and otherwise go where the pose puts them.
- **Idle behaviours** (`ABFDino::TickLife`), every 4-9 s standing: look round (the big ones sweep slowly, the raptor and
  Pteranodon snap their heads round and hold, as birds do), sniff (bipeds tip forward at the hips, the tail rising
  behind), shift weight, swish the tail, yawn, shake off after a swim. Off in the scripted checks.
- **Wings matched to the flight check**: fliers beat strongly; gliders' few flight muscles make shallow beats; a failed
  take-off stays on its legs (only the wings take the flight pose, `FBFPoseInput::WingsOnly`), heaves with each early
  downstroke, tires (slower, shallower beats) and gives up after four seconds.

- **Feeding and chewing** (the doc's small movements, chewing; `EBFIdle::Feed`, plant eaters only, by the head's
  species, `FBFSpecies::HeadKey`/`Feed`, so a T. rex with a Triceratops head grazes): the head goes down to the plants
  (a sniff at them first), the jaw opens on them, snaps shut and the head tugs the mouthful free. The Triceratops then
  chews, about a stroke a second, its tooth rows shearing past each other as the jaw closes: up and down, since its jaw
  has no side-to-side range in the rig and ceratopsian teeth sliced vertically. The Ankylosaurus snips and swallows
  after a couple of small movements of the jaw. The Brachiosaurus reaches up (its neck raised to about half its upward
  range), strips the leaves (the head pulls back down a little) and swallows with no chewing.
- **Nostrils flare** (the doc: slightly on each breath in, more when sniffing): the eye material's world position
  offset grows the nostrils' rims along the side of the head (`NostrilFlare` cm, `HeadRight`, set per frame), by a
  tenth of their size breathing in at rest, a quarter winded, a third on each sniff. The nostrils are painted all
  pupil and no iris, which picks them out of the eye material; their size is the head species' share of the skull's
  length (parts_builder FACE), so a scaled head's nostrils flare in proportion.
- **Feathers** (the Velociraptor's folded wings, on it and the splices that keep its arms): each feather moves on its
  own. `bone_forge_gltf.feather_uv` writes texture coordinates for them (U root 0 to tip 1, V where its root sits
  along the wing; 0 on everything else), and the skin material bends each by U squared: the tips lag the hand as it
  moves (a stiff damped spring, about 12 Hz, `FeatherSwayL`/`R`; at 5 Hz the raptor's running bounce rang the tips to
  a third of their length), ruffle up and out when it shakes off water
  (`FeatherLift`), and flutter across their own plane in the airflow, faster and harder the faster it runs, each at its
  own phase (`FeatherFlutter`, `FeatherU`, `FeatherNL`/`NR`). The game checks the mesh for those coordinates
  (`ABFDino::HasFeathers`), so a body imported before them simply doesn't flutter.
- **Skin sliding** over the hips and shoulders: the geometry turns with a limb only over the narrow band its skin
  weights blend in, but a real skin slides as one sheet, the limb's skin lagging it near the joint and the body's
  dragged along. So the skin material reads its scale tiles from where that sheet would have carried them: turned about
  the joint (the mesh's Y, the limbs' swing axis) by the limb's swing from rest times (s - g), s the limb's own skin (a
  narrow band at the joint) and g the sheet's (a wide one, most of the limb's length either side), fading out away from
  the limb's line. `SlideA0..5` (joint, swing in radians) and `SlideB0..5` (rest direction, length) per walking limb,
  per frame. Nothing moves far up the body or far down the limb.
- **Frills and crests** stay rigid on the head: the Triceratops' frill and the Pteranodon's crest are bone, part of
  the skull, so in a real animal they move only with the head (gltf: rigid on `head`).

Not done: skin creasing on the inside of a bend (the slide stretches and squeezes the pattern there, but no folds form),
nostril flare on the Pteranodon (it has no nostrils modelled), food (the feeding is mimed: no plants in the arena).

Checks: `-bfidle` (the six or `-bfonly`): each behaviour from the side and 3/4 (the shake in four frames) with how far
the feet moved, walking (hips' and head's bob, how far held feet are off the pose and out of reach), running (belly
swing, feather tips), stopping (surge), breathing at rest and winded, then close-ups: the face at rest and at full
flare sniffing (`life_nose_rest`/`_sniff`), a plant eater cropping and chewing (`life_feed_crop`, `_chew_open`,
`_chew_shut`, with the jaw's range), the feathers before and during a shake (`life_feathers_rest`/`_shake`), and the
same point of a stride with the skin sliding and a stride later without it (`life_slide_on`/`_off`: their difference
is the slide). `-bfsplice` adds a later frame of a take-off attempt. The clips bake the stride-following parts and the
idle clip's breathing (re-bake with run_bf_anims.py); the rest is live only.

Results (2026-11-08, `-bfidle` on the six and two splices): nostril rims grow 0.87 cm sniffing on the T. rex (5 cm
nostrils), 1.14 on the Triceratops, 0.16 on the raptor; the Triceratops' jaw works 0 to 0.22 open chewing, the
Ankylosaurus' and Brachiosaurus' 0 to 0.08; feather tips move 1.2 cm running and 4.4 cm shaking (9.1 cm feathers);
the skin slides up to 5.6 cm at the T. rex's hips, 5.2 the Brachiosaurus', 4.0 the Triceratops', 1.3 the raptor's.
Splices whose legs can't walk are skipped (an idle never ends on a collapsed animal). The re-imported raptor bodies
pass `-bftest` exactly as before.

Real-animal reference: resting elephants breathe about 6 times a minute (the bird rule is slower: dinosaurs' air sacs);
elephants walk best near Froude 0.09 and never leave the ground (Ren and Hutchinson); titanosaur trackways show 1.3 m/s
at a 2.2 m hip; tyrannosaurid trackways 1.25-2.2 m/s (the T. rex's 1.28 m/s is the doc's, from its tail's natural
frequency). The Brachiosaurus' 1.08 m/s walk (Froude 0.03) is on the slow side of these.

## Test arena and report (roadmap step 6)

T (or the menu's Test button, or Test it in the splicing editor) runs every test on the animal in turn and ends on a
report card, each line Can or Can't and why in plain words; it is saved to `Saved/Reports/<key>.txt` too.

- Stand: the balance (centre of mass inside the feet), or legs that give way (the leg check).
- Jump (2026-11-12): a standing jump, its measured height, or why it can't (below).
- Walk, Run: speeds reached on the flat. Since 2026-11-12 the run builds as a player's does (Shift held from a walk) and
  says when it got to 95% of its top speed, in m/s and km/h.
- Stop (2026-11-12): let go at that speed, the seconds and metres to stand still.
- Turn (2026-11-12): back on the flat, running the other way at its top speed and turned a quarter: the seconds, and the
  circle that quarter belongs to (4 x path / pi across).
- Run and Jump are set beside the base animal's numbers for a splice ("Tyrannosaurus rex: 7.5 m/s"), and ranked among
  the six for one of them ("second of the six").
- Climb: the 40 degree ramp, then gentler ones until one is climbed (stuck = no height gained for 8 s, so a slow
  climber isn't cut short), with the climb check's reason for its limit.
- Swim: dropped into the deep end of the pool, then six seconds paddling: whether it floated or sank, its speed,
  and how long its nostrils were under water.
- Glide (with wings): off the 20 m cliff, distance and glide ratio. Fly (with wings): a running take-off on the flat.

Slopes follow the climb check (`splice/splice.py climb_check`): each animal's walkable floor angle is its limit, so on
anything steeper it slides back, and facing uphill it keeps the check's share of its speed for that slope (it slows near
its limit). The status line shows the climb check on a slope.

The pool (J, or Pool in the menu) is 12 m deep, 120 m by 40 m, south of the spawn, with a 15 degree slope in: an animal
wades in (at half speed) and floats off its feet where the water is deep enough. Floating it sits where the swim check
(`splice/splice.py swim_check`) says: its centre of mass that deep, pitched that way, neck raised so its nostrils clear
by 30 cm, legs paddling, a tippy one rocking, at the check's speed. Sinkers walk along the bottom. Water:
`M_BoneForgeWater` (`bone_forge_materials.py`), translucent, no collision. With the camera under the surface the view
turns underwater (`ABFDino::TickUnderwaterView`): a blue-green cast, a little less colour, contrast and light, darker
edges, and the world's fog thickened to the water's murk (`ABFGameMode::SetUnderwaterView`, about half gone at 15 m).

Checks: `-bftest` (the six, or a `-bfonly` list; a picture of each card, `<key>_test_report.png`), `-bfswim` (into the
deep end, side, three-quarter and underwater shots, state and depth logged), `-bftestone` (Test it coming back from Walk
It), `-bftestit` (the editor's Test it button pressed through Slate on the Ankylosaurus with fitted Brachiosaurus legs:
grow, import, restart, test; the game stays open on the report card).

## Catalog of six: splices and grown bodies

The testing ground loads the six, then every other rig in the rig folder (`BFSpeciesKeys`), so a new splice from
`splice/splice.py` shows up once it is imported. Splices' bodies are grown from the bones' tissue profiles
(`body_tools/grow_body.py`), and they wear their base species' skin (`bone_forge_gltf.py` and
`bone_forge_materials.py` look it up from the rig's `splice.base`). The exporter skins a `body_grown` part chain by
chain (`grown_weights`): `grow_body.py` writes `<body>_chains.json` beside the OBJ, which chain of flesh (trunk, each
limb) every skin vertex grew from, and each vertex is weighted to that chain's bones, shared across the fillet where
two chains blend. Skinning the one skin to every bone at once (as before) put a belly vertex on the legs and its
neighbour on the arms, a tear waiting to happen; `tests/test_gltf.py` now checks every splice.

The six wear grown bodies since 2026-11-08 (`bone_forge_unreal.py` takes `body_shapes/<key>_grown.obj` when it exists;
`BF_BODY=hand` for the hand-built one). Two exporter changes came with that, for every grown skin. Paint: a grown
skin vertex is painted by the chains it grew from, blended by their shares (the trunk as the body, a leg as that
leg), so grown legs keep the hand-built legs' paint (paler below, the feet's scales fading in above the ankle) with
no line where a leg leaves the body; before, the whole skin was painted as body. Shading across the stitched seams:
the skin's edge and its foot's ring each took normals from their own faces, a few degrees apart, which drew a faint
line round every ankle and wrist and a step in the belly paint; now the skin's edge takes the foot's normal and the
row above eases halfway (`seam_normals`), and coincident vertices of one part (the foot cut's duplicates) share
their normal.

The leg check (`leg_check` in the rig, `EBFLegs`): legs that can only stand can't walk (no walking speed); legs that
buckle give way 1.2 s after the animal appears (it goes limp). The HUD shows the leg check for splices and for any
animal whose legs aren't strong enough to walk. A splice standing on front legs too long for its body has
`hind_hang_cm` in its rig (splice.py `front_stance`): `FBFSpecies` then leaves the hind legs out of its walking
limbs, so they hang as they are and the front pair walk alone (2026-11-08), stepping under its weight the way a
biped's legs do (`bHindHang`).

Tipping (2026-11-08): standing still with its centre of mass more than 2% of its length (`TIP_SHARE`) outside its
feet, and stepping (for a biped, or one on its front legs alone) already as far as its hips or shoulders go, an animal
tips over a second later: it goes limp and the log says `<key> tips over`. The splicing editor's balance readout
uses the same rule, so "Tips over" there means it falls in the game. `-bfcatalog` now takes `cat_stand` 0.6 s after
each splice appears, before buckling legs (1.2 s) or tipping (1.5 s at the earliest) take it down, and `-bfstance`
takes `stance_stop` as it comes to a stop on the ramp. The camera stands off by the animal's length or height,
whichever is greater (it sat inside a 5 m tall Velociraptor on Brachiosaurus arms).

Slopes (2026-11-08): on both pairs the body is pitched so each pair keeps its standing height over the ground under
its own feet (`AlignToGround`, `bQuadPitch`); pitched by the slope itself, a body that stands tilted (a T. rex on
Brachiosaurus arms, 31° head-up) raised its shoulders more than the ground rose under its front feet, and they lost it.
For a level back the two are the same.

The creature's own slope test (2026-11-08, `ABFDino::MeasureStanding`): the first time a species is spawned
(`ABFGameMode::SpawnDino`), whatever made it, an unseen copy of it (never ticked, collision off, destroyed at once)
stands still on imaginary slopes: `TraceGround` answers the three ground traces (AlignToGround, leg IK, balance) from
a plane instead of the world, and `ProbeStands` runs the game's own frame on it (tilt, pose, leg IK, balance and the
tipping rule) for a settling second and then 2.5 s at 30 frames a second. 5° steps, then 1° steps, facing up and then
down: about 20 tries in 15-45 ms. The steepest it stands on facing uphill caps its climb check (the walkable slope:
it never walks up a slope it would fall over on when it stops) and rewrites the climb summary; facing downhill is
added to the summary when it's lower (it can still walk down steeper ones and tips over forwards if it stops there).
The log says `stand test <key>: stands still facing uphill on slopes up to N degrees, downhill M ...; climb limit A ->
B`. Nothing is measured offline or written into rigs any more (`splice/slope_balance.py` is gone). Results: T. rex 42
up / 28 down, Velociraptor 37 / 36 (it tipped standing on the 40° ramp), Triceratops and Ankylosaurus 37 / 37,
Pteranodon 44, Brachiosaurus 23, the Velociraptor on short Brachiosaurus arms 10, the Pteranodon on T. rex legs 25,
the T. rex on 90% Brachiosaurus arms 26, the T. rex with an Ankylosaurus tail 42 up but 12 down.

Check: `-bfslopes` (`-bfonly=` works) walks each animal halfway up the 10/20/30/40° ramps, stops and stands it 3.5 s,
logs `slopes <key> <deg> deg: stood: <balance> | TIPPED OVER | didn't get halfway up | its legs buckle` beside its
climb limit, and takes `<key>_slopes_<deg>_side` and `_feet`. A tip logs where the weight and the feet were along the
body, the body's pitch, the legs' lean, and each foot joint's place along the body and height off the ground. With
the self-test every animal stands on every ramp it can walk up and can't walk up the steeper ones (the 10° ramp is
too long for the run's timer, so "didn't get halfway up" there is the run, not the animal).

Found by that sweep and fixed the same day: (1) each leg is held upright as far as 90% of its own hip or shoulder
range goes (FBFPoser), not the stiffest joint's: a T. rex on Brachiosaurus arms had its hind legs held to the arms'
21° (its hips go 36°). (2) A planted foot standing on a ball and toes (`FLegBones::Pivot`/`Toe`) is set on the ground
by its ball, and its toes bend at the ball (at most 40°) onto the ground under their tips; set by its toe tips with
the sole level to the ground under the ankle, a foot at the foot of a ramp stood on its toe tips with its ball half a
metre up, and that T. rex, front feet up the 30° ramp it can't climb, fell over backwards. It now stands there, 5 cm
inside its feet. On even ground and even slopes the foot is where it was.

Tails on the ground (2026-11-09, `ABFDino::SolveTailGround`): the body pitches with the ground under it, so with its
hips still on the flat and its front up a ramp (or coming down, its hips on the slope and its front on the flat) the
tail swung into the ground: a T. rex's 48 cm going up the 40° ramp, a Triceratops' 74 cm coming down the 30°, the
T. rex with a tail club's 120 cm, the T. rex on Brachiosaurus arms' 187 cm. Now the ground is terrain to the tail
(every one of them 0.9-1.0 cm above it in the `-bftails` run, the self-test's slope limits unchanged). At setup the creature finds its tail's bones
(base to tip, and what hangs off them: a club) and the lowest points of its own skin, the lowest vertex in each third
of each bone, each vertex going with the bone that carries most of it (the collision capsules' undersides in a cooked
game without the vertices); the log says `tail <key>: N joints, M bones with skin, K low points`. Every frame, after
leg IK, from the base to the tip each joint bends up just enough (toward the ground's normal) to lift the skin it
carries 1 cm clear of the ground, so the tail runs straight down to where it meets the ground and lies along it from
there. Its own joint bends first as far as its range goes (`limits_deg`), then the joints before it, nearest first
(a stiff tail lifts from further up); with every joint back to the base out of range and the tail still in the
ground, the ground bends it further anyway, over that joint and the two before it, so it curves rather than kinks
(the Triceratops and Ankylosaurus, ±1-4° a joint, go 9-18° past). Off when limp, flying or floating.

Check: `-bftails` (`-bfonly=` works) walks each animal up the steepest ramp it climbs, stops it halfway, turns it and
walks it back down, taking `<key>_tails_up_foot` (its hips at the ramp's foot going up), `_up_close` (close on the
tail from behind), `_up_middle`, `_down_foot` and `_down_close`, and logs `tails <key> on the <deg> deg ramp, <when>:
tail as posed N cm into the ground, its lowest skin now M cm above it, bent up A deg at one joint at most (B past the
joint's range)` and the worst of the walk. `-bfnotailfix` turns the bending off for pictures of it before. `-bfslopes`
adds the same note to each ramp's line.

Holding Shift (2026-11-09, `ABFDino::TickSprint`): Shift is an accelerator, on land, in the water and in the air.
`SprintBuild` (0..1) climbs over `SprintSeconds` (1.2 M^0.15, 1.5-6 s: 1.9 Velociraptor, 2.1 Pteranodon, 4.5 Ankylosaurus,
4.9 T. rex, 5.0 Triceratops, 6 Brachiosaurus) while Shift is held and the animal is going somewhere (in the air, W), and
falls in half that time otherwise; `SprintEase` = u(1.5 - 0.5u), quickest at first and tapering near the top. Stopped or
blocked, it resets. `GroundSpeedNow` goes from the walk to the fast gait by it (the gait blend and the cadence already
follow the real speed). In the water `MaxFlySpeed` goes up to 1.6 x `SwimSpeedCm`. A flier's thrust grows to 1 + 1.2 x
min(flight muscle / 15%, 2) (the Pteranodon's 20%: 2.6 times the thrust, 14.1 to 22.7 m/s), its beats 35% quicker and 20%
deeper. A glider has no muscle to spare: it tips forward 12°, which also keeps it above its stall (`raptorwings` off the
20 m cliff covers 91 m instead of 40). Stopping: `BrakingDecelerationWalking` 3000 M^-0.2 cm/s², braking friction off
(from the top the T. rex takes 1.6 s and 6 m). Turning at speed: `GroundFriction` eases from 8 down to the body's turn
rate in rad/s and `MaxAcceleration` drops near the top, so the path swings about as fast as the body can: on a sudden
quarter turn at top speed body and path are at most 32° apart for the T. rex (74 before). Drive the friction by Shift,
not by the actual speed: a turn loses speed, which raised the friction and sharpened the turn (54). The checks that want
a gait go straight to it (`SetRunning(bRun, true)`, on the ground only); baked clips (C) get the speed but not the deeper
wingbeats.

Check: `-bfsprint` (`-bfonly=` works) presses the real keys through Slate, so the ordinary input code reads them, for
every animal that can walk: Shift alone (nothing happens), W + Shift, Shift let go, a sudden quarter turn at top speed, W
let go; then in the deep end, paddling and holding Shift; a flier cruising and holding Shift; a glider's two leaps off the
cliff, with and without. The log has `sprint <key>: ...` with the time to each quarter of the way, and `_sprint_half`,
`_sprint_top`, `_b`, `_c` and `_34` are taken from the side, driven by the script so the camera can look that way.

**The mouth on the move** (2026-11-09, `ABFDino::TickLife`, `BFBreathMouth` in `BFPoser.h`). On its feet and moving, the
breath keeps time with the legs: one every two strides at its walk, one a stride at its top speed (standing, the resting
rate as before), and the ribcage, nostrils and jaw share that one clock. The jaw is `FBFPoseInput::Jaw`, separate from
`Mouth` because breathing must not lift the head. Share of the jaw's own travel: 0.14 at a walk, shutting between breaths;
0.30 at top speed, never shutting (35% of that stays open); +0.12 winded, each footfall adding up to 15% more. So a T. rex
gapes up to about 9° walking and 29° winded at a run; a Triceratops 4° and 13°, because its jaw travels less. Speeding up and
slowing down ease it (rate 3/s); a bite clears it fast (14/s) so the jaw can snap shut; Q (`Mouth`) takes over when wider. In
the air and water it fades out. Winded and standing, the throat flutter now moves the jaw alone too, fading in over 0.5 s.
Breathing fast (2026-11-12, lab plan item 10) the jaw pumps less: from 1.5 to 3.5 breaths a second its opening and
closing falls to a quarter, held about its middle opening (`BFBreathMouth`'s `Pump`), so a sprinting Velociraptor (3.4
a second) pants with its mouth open instead of chattering. The big animals breathe a stride at a second or slower and
keep the full pump. The baked walk and run clips have it (a breath a stride, so they loop; rested); re-bake with `run_bf_anims.py`.

Check: `-bfmouth` (`-bfonly=` works) presses the real keys for every animal that can walk: standing (shut), W, W + Shift at top
speed, E at speed (must reach 0), Q held at speed (must reach 1), Q let go, stopped. The log has `mouth <key> ...` with the
jaw's range and how many strides per breath; `_mouth_walk_open`, `_walk_shut`, `_run_open`, `_run_shut`, `_run_open_front` and
`_run_body` are taken at the top and bottom of its movement.

Check: `-bfcatalog` (`-bfonly=` works) takes every splice: a side and a three-quarter look, then a walk, with the leg
check, flight check, balance and how the walk went logged. 2026-11-04: the T. rex with a Triceratops head (stable,
30 cm inside its feet), the T. rex with an Ankylosaurus tail, the Pteranodon on T. rex legs and the winged raptor
walk; the raptor with a Brachiosaurus neck or a T. rex head, the Triceratops on raptor legs and the Ankylosaurus
on Pteranodon wings collapse (see `splice/README.txt` for the numbers).

Check: `-bfsplice` (`-bfonly=` works) takes each winged species: a look standing, a running take-off on the flat
(flapping as hard as it can), then a run off the cliff, aiming 10° down; each attempt is logged. 2026-11-03:

| | Flight check | Running take-off | Off the 20 m cliff |
|---|---|---|---|
| Pteranodon | flies (182 N/m², muscle 20%) | climbed 33.6 m in 7 s, still flying | 110 m in 13.5 s, glide ratio 5.6 |
| Velociraptor + Pteranodon wings | glides (148 N/m², muscle 3%) | a leap: 0.3 m up, 5 m, 0.8 s | 110 m in 17.4 s, glide ratio 5.7 |
| Ankylosaurus + Pteranodon wings | grounded (32,405 N/m²) | a 10 cm hop | fell 19 m in 2.1 s, 4.9 m out |

In this arcade model the glide angle is where the animal aims, so gliders aimed alike glide alike; wing loading
shows in the stall speed and muscle in whether it can climb. Standing, the winged raptor (30 kg) is only just
balanced (3 cm inside its feet) and the winged Ankylosaurus stable (78 cm).

## Animation clips

`bone_forge_anims.py` (run in the testing-ground editor, like the physics script; the C++ `UBFAnimBaker` does the
work) bakes the same procedural pose the testing ground draws live (`FBFPoser`) into real assets in
`<key>_skeletal/Anims/`:

| Asset | What it is |
|---|---|
| `<key>_idle` | 10 s loop: slow tail and neck sway |
| `<key>_walk`, `<key>_run` | one stride each, in place, at the rig's walk and fast-gait speeds (the run is the fast gait: trot, amble, bound…) |
| `<key>_bite`, `<key>_roar` | 0.9 s and 1.8 s one-shots |
| `<key>_glide`, `<key>_flap` | Pteranodon only: one wingbeat each, so they blend in step |
| `<key>_locomotion` | Blend Space 1D on **Speed** (cm/s): idle at 0, walk at walk speed, run at fast-gait speed |
| `<key>_flight` | Blend Space 1D on **Flap** (0..1): glide to flap |

Clips are in place (no root motion) and keyed about 30 times a second; each loop's last key is its first.
The script prints, per clip, how many joint axes go past `limits_deg`, and for any that do, each joint's
measured range against its limit. All clips of all six: 0. The flight pose is built within the limits
(`FBFPoser::ClampToLimits`): hinges only fold, and a joint the rig's `flight_pose_cm` asks past a limit stops
at it. The Pteranodon's shoulder range in `ptera_rig.json` was widened for the flight stroke (bend −150..50,
side −30..90; the walking range was ±45 and −30..60), with a `limit_note` saying so. The wing membrane isn't in
the mesh; the testing ground rebuilds it from the bones every frame.

Check: in the testing ground, **C** switches between the live pose and the clips (Q roars, E bites), or run it
with `-bfclips` for a walk, run and bite of each species plus the Pteranodon's flap and glide, with screenshots.

## Skin weights are starter weights

They exist so Unreal will accept the skeleton and so you can pose it in the Skeleton editor:

- Each OBJ part only uses its own body section's bones, plus the bone it hangs from (pelvis, chest, last neck bone).
- Weights fall off with distance to the bone, then get smoothed. There is a clean split at the mouth line.
- Max 4 bones per vertex, as the Look and Model Spec asks.
- Frill, horns and the Pteranodon crest are rigid on `head`.
- End markers, `root` and the sickle claws carry no weight.

Expect to re-weight once the sculpted mesh exists. Known limits of the current body volumes:

- heads are a skull plus separate `jaw`, `tongue`, `teeth_upper`, `teeth_lower` parts (body_tools/head_builder.py):
  jaw, tongue and lower teeth are rigid on `jaw`, upper teeth on `head`, so the mouth opens without stretching;
- legs and feet come from `body_tools/leg_builder.py` (thigh, calf and species feet built along the rig joints;
  the T. rex feet are scaled 1.5×), which rewrites the leg/arm parts of the body OBJs; re-run it after
  `trike_body_v2.py`;
- the OBJ has no UVs, so Unreal may warn about UVs/tangents (harmless for now; the headless import also logs an
  Interchange "UVs.IsValidIndex" ensure for the plain check-OBJ, same cause);
- the raptor's feather plates and the Pteranodon membranes are placeholder sheets (the membrane is meant to be rebuilt from the joints per pose).

## Tests (run outside Unreal)

```
python tests/test_gltf.py            # builds all six .glb files and checks them byte by byte
python tests/test_unreal_script.py   # runs bone_forge_unreal.py against a stand-in for the unreal module
```

`test_gltf.py` checks every species. It confirms:

- bone names, parents and positions match the JSON;
- each bone's X axis points at its child;
- inverse bind matrices are correct;
- bind-pose skinning returns every vertex to its OBJ position;
- weights sum to 1 with at most 4 bones per vertex;
- no left part uses right bones;
- there are no weight seams.

The files also pass the Khronos glTF Validator with 0 errors and 0 warnings.

`test_unreal_script.py` fakes Unreal's importers with different axis conventions to test the
reporting and the axis-probe correction. It can't prove the real editor API calls; only a run
in Unreal does that.

`test_renders/trex_key_poses.png`: the T. rex `.glb` posed in three.js with the key-pose recipes
from `source/body/poses.py` (each bend/side value applied through the bone axes above). It matches
`body_shapes/trex_poses.png`.

**neck_blend parts (2026-11-03).** `body_tools/parts_builder.py` adds a `neck_blend` object to the
Brachiosaurus OBJ (the neck running into the back of the skull and the throat). `bone_forge_gltf.part_bones`
weights any part named `neck*` to the Neck-section bones and the head bone, never the jaw. The T. rex had one too,
but an overlapping separate mesh always shows a crossing line, so its neck is now swollen into the skull in the
body itself (`head_builder.NECK_SWELL`) and the blend piece is dropped.

## Skin, eyes and blink

The models have no UVs, so the skin comes in two layers that need none (`bone_forge_skin.py` holds each species'
colours, tiles, scale size and bump height, from `body_shapes/colour_schemes.png` and the design doc's Skin
Textures tab):

- Claws, hooves and nails (parts `leg_claws`, `arm_claws`) take the species' smooth `Keratin`, like its horns. Toes and
  fingers (`leg_toes`, `arm_fingers`) take `Scute`: the skin material with its scales, in a colour a third of the way from
  the keratin to the belly (`SCUTE_TOWARDS_BELLY`), so the claws stand out at their ends. The builders file them apart
  (`leg_builder.digits`).
- `bone_forge_gltf.py` gives each triangle a material slot (`bone_forge_skin.material_for`) and paints every vertex
  (COLOR_0): R belly countershading, G pattern (T. rex saddles, raptor bars, Triceratops mottling, the darker top
  of the Brachiosaurus neck, the Pteranodon's dark wings, Ankylosaurus armour), B accent (warm T. rex face,
  Triceratops frill rim, Pteranodon crest), A region (0 body, 0.5 feet and hands, 1 face; the feet fade in from the leg over a wide band above the ankle,
  and nothing else sits between them, so no third tile shows at the ankle). Eyes are painted with the iris
  in R and the pupil in G around the way each eye looks (`eye_fwd`); nostrils are painted all pupil.
- `bone_forge_materials.py` (in the testing-ground editor: `run_bf_materials.py`) imports the height tiles from
  `textures/tiles` and builds `M_BoneForgeSkin`: each region's tile is projected from three sides onto the
  standing pose (pre-skinned position, so the scales ride on the skin), bumped in screen space (no tangents
  needed) and darkened in the grooves. `MI_<Sp>_Skin` and `MI_<Sp>_Eye` set the colours (sRGB → linear) and tiles.

Faces have two layers. `body_tools/face_sculpt.py` sculpts the skull's anatomy into the head and jaw meshes (soft
brushes along the surface normals, one surface, no seams): brow bosses over sunken eye sockets, cheekbones, jaw
muscles behind the eyes, the hollow in front of the eye, nostril hollows, lip lines; the T. rex's lacrimal horns and
rough nasals, the Triceratops' horn bosses and cheek ridge (plus a keratin `horn_jugal` cheek horn from
`trike_body_v2.py`), the Ankylosaurus' brow bosses and head plates, the Brachiosaurus' nasal arch and nostril
hollows. Then `body_tools/parts_builder.py` (`FACE`) places the eyeballs, nostrils and three eyelid sets per eye
on the sculpted skull, so the eyes sit in their sockets. The skull and jaw are painted from blurred normals
(`bone_forge_gltf.blurred_head_normals`), so the sculpted bumps shade as shape and don't blotch the belly paint.
Eye size and place follow the skull pictures in the reference library (eye socket length as a share of skull
length, and where it sits along the skull):

| Species | Socket | Where |
|---|---|---|
| T. rex | ~8% of the skull, a small eye | upper back third, under a brow boss, facing partly forward |
| Triceratops | small | just under and behind the base of the brow horn (anchored to it) |
| Velociraptor | ~17%, big | a quarter of the way from the back of a long, low, flat skull |
| Ankylosaurus | ~12% | mid-skull, high on the side of a skull that is triangular from above |
| Brachiosaurus | large | high, a third of the way from the back, below the nasal arch |
| Pteranodon | small | at the back of the beak, in front of the crest |

**Blink.** The posable mesh can't play morph targets, so each eye has an open lid (always shown) and two more lid
sets on their own material slots, `<Sp>_LidHalf` and `<Sp>_LidClosed`. `ABFDino::TickBlink` hides them, then every
2.5–6 s shows half, closed, half and open again over about 0.2 s (`ShowMaterialSection`).
