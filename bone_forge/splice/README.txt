Bone Forge splices (Prototype 3 and the catalog of six)

    python splice/splice.py                    # every recipe in splice.py
    python splice/splice.py rextrikehead       # one

A splice swaps one body section (Bone Data tab: Head, Neck, Tail, Hip = legs, Shoulder = arms) from a donor species
onto a base species, at the base's own sockets, and grows the body around the mixed skeleton. It is written out
like another species, so the whole pipeline takes it unchanged:
  rig_joints/<key>_rig.json        joints, limits, capsules, tissue profiles, mass, balance point, flight pose,
                                   flight and leg checks, lineage
  body_shapes/<key>_features.obj   the bone-part meshes: the base's (head, feet, horns, armour...) without its own
                                   section's, plus the donor's section's, each moved with its bones
  body_shapes/<key>_body.obj       those plus the flesh grown from every bone's tissue (body_tools/grow_body.py)
  body_shapes/<key>_body_chains.json  which chain of flesh (trunk, each limb) every skin vertex grew from, for
                                   skinning: the skin follows that chain's bones, blended where chains meet
  splice/<key>_report.txt          the same in plain words
Then, like any species: BF_SPECIES=<key> run_bf.py (Unreal import) and run_bf_materials.py; a splice wears its base
species' skin. The testing ground loads every rig in the rig folder.

Grown bodies (body_tools/grow_body.py)
  Every bone carries a tissue profile (its "muscle anchors"): the flesh's cross-section at stations along it,
  measured from the six hand-built bodies. Bones run in chains (the trunk from the tail tip through hips, back and
  neck to the head; each limb from hip or shoulder to the ankle or wrist), and along a chain the flesh is one sweep
  whose cross-section flows from station to station along a smooth curve through the joints; chains blend where
  they meet. So a Brachiosaurus neck tapers into a Velociraptor chest the way an animal's own flesh would: no seams,
  no collars. Detailed parts stay bone-part meshes: heads, jaws, teeth, eyes, horns, frills, crests, armour, tail
  clubs, feathers, and the feet and hands. A foot part is the hand-built leg below a flat cut across the shin (55%
  of the way from knee to ankle, square to the shin), its faces clipped exactly at the plane, so ankles, feet and
  toes are the hand-built ones. The grown skin is cut at the same plane and stitched to it (stitch_feet): its last
  stretch is bent round the leg, angle by angle, so that at the plane it lies on the foot's ring and leaves it at
  the foot's own slope. Two surfaces merely meeting at a ring always showed a line, a band or a boot top. A
  Pteranodon's folded wing hand keeps the older cut (faces nearest the wrist's bones), and no flesh runs on where
  the next bone folds back. The skin is weighted chain by chain: trunk flesh to the back, hips, tail and neck, a leg's flesh to that
  leg, shared across the fillet where they blend, so the belly follows the body the way a hand-built one does.
  Each half of a section (above and below its middle) is a superellipse with its own squareness, measured from the
  hand-built body: the trunks are ellipses above but broad and square below the flanks, and plain ellipses grew them
  up to 25 cm slimmer at the Brachiosaurus' belly corners. A station carries it as its 6th and 7th numbers (2 is an
  ellipse, 4 the squarest; a rig whose stations have five numbers grows ellipses, as before). The tail's taper runs
  on to its tip (its last measured station sits short of the tip, and the tail used to end in a rod with a knob).
  Armour pieces (the Ankylosaurus' plates and spikes) are seated on the grown skin, about half of each sunk in,
  where they stood off it or were buried. After meshing, the skin is smoothed (the voxel grid's faint terraces),
  every crease is relaxed (the grown skin has none of its own) and the stitched band above each foot's ring is
  evened out. Since 2026-11-08 the six wear their grown bodies in the game too (bone_forge_unreal.py imports
  body_shapes/<key>_grown.obj; BF_BODY=hand brings back the hand-built <key>_body.obj, which the tissue is measured
  from). Grown against hand-built, a median 1.4-2.2 cm apart where they differ (the Brachiosaurus' trunk 4 cm fuller).
  The voxels are a 300th of the animal's biggest size; a part far too big for the animal (a Brachiosaurus upper arm
  on a Velociraptor) spread the grid far past the body at those small voxels, 66 million cells and 9 minutes, so past
  MAX_CELLS (24 million; the six grow on 6 to 16) the voxels grow to fit: that one grows in about 3 minutes. And no
  coarser than LIMB_VOXELS across the thinnest limb (the six have 1.9 or more). A foot's cut is kept to its own limb's
  skin, the trunk's included (a body tilted onto long front legs hangs its hind feet by its tail, and their cuts bit
  holes in the tail).

Placing the donor's section
  - A swapped part keeps its own size: a size mismatch is allowed and the checks show what it costs.
  - Base joints that hung off the swapped section (the head on a swapped neck) ride on the donor's section where the
    donor's own did.
  - Legs change the hips' height. A two-legged animal is lifted or lowered whole. A four-legged one tilts about its
    shoulders until its hips sit on the new legs, its front legs staying upright (a Triceratops on Velociraptor legs
    tilts 26° tail-down); past 45° of tilt it stands up on the new legs and carries its front limbs (a Pteranodon on
    T. rex legs, hips 2.3 m higher, becomes two-legged).
  - Front legs too long for the body (2026-11-08, front_stance): new walking front legs tilt a four-legged animal
    head-up about its hips as far as 45 degrees; past that it is lifted the rest of the way, so its hind feet hang off
    the ground and it stands on its front legs alone (a Triceratops on Brachiosaurus arms: 45 degrees and 143 cm).
    The same when front limbs would otherwise go through the ground: on a two-legged animal (folded and still under
    it, or a single long bone from Advanced mode) it stands on them instead, tilted and lifted the same way, and
    becomes four-legged (a T. rex on 90% Brachiosaurus arms tilts 31 degrees and stands on all fours; a Velociraptor
    on Brachiosaurus arms is lifted 3.6 m and stands on them alone); a walking wing whose knuckle can't come up far
    enough tilts the body a little (a Pteranodon with 120% arms, 17 degrees). The rig keeps "hind_hang_cm"; the leg
    check then counts the front legs alone, the balance readout their feet (stepping like a two-legged animal), and
    the game leaves the hind legs out of its walking limbs, hanging as they are, and steps the front pair under its
    weight. How far it must rise is measured with the limb as it stands on its donor (measured turned, the body came
    up short and the arm stayed turned 25° to reach the ground, its hand tipped into it), and the limb is placed on the
    body's moved outline, allowed its donor's own splay (a Brachiosaurus elbow stands 45 cm out from its shoulder).
  - Limbs whose flesh would cross the body's midline (far thicker than the body is wide: Brachiosaurus arms on a
    Velociraptor, T. rex legs on a Pteranodon, Triceratops shins fitted to a T. rex's) are mounted out until each
    side's stops 1 cm short of it, before they are posed; they grew into one column and their feet couldn't be
    stitched. It is measured on the limb's flesh outline as grown from its tissue, past the first quarter of the limb
    and outside the body only (a T. rex's own thighs meet inside its belly). The flesh runs from the old socket out to
    the mount (mount_from_cm). A walking limb is never aimed under the ground (a resized part's own feet can sit below
    its own ground).
  - raptorbrachioneck's tiny arms turned out to lie inside the Brachiosaurus neck's base (elbow and wrist 3-4 cm
    under its surface, only the finger tips out), and stitching the skin to a hand buried in other flesh cut holes in
    the neck: a hand or foot whose ring is inside another chain's flesh is no longer stitched (grow_body.buried).
  - Tails (carry_tail): a tilt swings the tail with the body, and head-up or tail-down that put tails through the
    ground (the Triceratops on Velociraptor legs: 168 cm). A tail that would go under the ground lifts from its root,
    as one piece over its first third, only as far as clears its flesh, at most 30 degrees; past that the rest of it
    lies along the ground, each joint turning up just enough to keep the next off it. A lift past the joints' own range
    (a Triceratops tail bends 8 degrees up at its root) is held anyway: those joints' limits turn with the new rest
    pose, widened to keep it inside them.
  - Wings (a walking front limb) on a two-legged animal are carried folded: a search over the shoulder, elbow, wrist
    and knuckle bends (within their limits) for the fold that stays off the ground, below the head, inside the body's
    length, outside its flank and on its own side. On a four-legged animal the limb keeps walking: only its shoulder
    turns, until the wing knuckle is as high off the ground as on its donor.
  - A joint whose rest pose turned has its limits turned with it, so the limb still reaches its donor poses.
  - Wings mount out on the skin at the widest point of the body there (the Ankylosaurus' shoulder socket is 40 cm
    inside its flank; out at its widest a wing flaps clear of the barrel body); the joint keeps "mount_from_cm", the
    old socket, and the grown flesh runs from there out to the wing like a shoulder girdle reaching the body wall.
  - The donor's imported wing membrane is left out; the testing ground builds the membrane live, its back edge on
    the skin over the hip (membrane_hip_offset_cm).

The checks
  - Mass: the capsules carry the weight in the game, so the swap moves theirs: the base's section capsules out, the
    donor's in. That takes a section's whole root with it (a tail's capsules carry its thick base, which the mass
    breakdown files under Core), so the total is always the capsules' sum. The balance point from the capsules as
    they sit.
  - Flight (Weight and Physics tab, "Can it fly?"): flies if weight / wing area <= 245 N/m² and flight muscle >= 15%
    of the weight; passes the first only = glides; fails the first = grounded. Flight muscle stays with the torso
    (FLIGHT_MUSCLE_SHARE, game estimates), so wings alone never bring it.
  - Legs: each pair of walking legs' muscle (the thickest cross-section of its thigh or upper arm from the tissue
    profile, 70% of it muscle, at 30 N/cm², about what vertebrate muscle pulls) against the share of the weight it
    carries (front and back split by where the centre of mass sits between hips and shoulders). At least twice its
    share: walks; at least its share: stands but can't walk; less: the legs buckle, and in the testing ground the
    animal collapses a moment after it appears. All six catalog animals walk (the tightest, the Brachiosaurus' front
    legs, at 2.2 times).

Gaits (splice_gaits, in every splice's rig as gaits and gait_check)
  The six keep the design doc's gaits. A splice's are the species it was built on, scaled by dynamic similarity
  (animals of one shape move alike at the same Froude number v^2/gh): speed with the square root of its walking legs'
  height (hips and shoulders, their geometric mean) and stride in proportion; then by its legs' strength for their
  load against that species' (the leg check's ratio): weaker walks and runs slower, stronger runs up to 30% faster.
  Running or bounding needs legs 4 times their load (the T. rex, 4.6, just runs), trotting 2.5; weaker legs fall back
  to a fast walk under Froude 0.5. Legs that can't walk keep the base's gaits, unused. For example the T. rex with
  120% hind legs runs at 10.7 m/s, the Pteranodon on T. rex legs walks at 2.2 m/s with 3.7 m strides.

The climb check (climb_check, in every rig as climb_check)
  The steepest slope it walks up, the lower of two limits. Grip: its feet slip on a slope steeper than atan(grip), a
  game value per species (GRIP: hooked claws 1.0, the T. rex's clawed toes 0.9, padded feet 0.8, hoof-like toes 0.75),
  each pair's feet weighted by the weight on them, so spliced legs bring their own feet. Strength: each walking pair
  carries its share of the weight, shifted onto the downhill legs as the body tilts (half the shift: it leans into the
  slope, LEAN_IN), and on a slope with rise s per metre it works as if carrying 1 + CLIMB_PUSH x s (1.5) times that,
  plus walking's own load (the leg check's walking margin, less when it goes slower). So it slows as the slope
  steepens, down to a crawl (CRAWL, a quarter of its walking speed), and steeper than that it can't climb. Legs that
  can't walk can't climb. Written: max_deg, grip_deg, strength_deg, why, and speed_share (the share of its speed kept,
  every 5 degrees from flat). The six: Pteranodon and Velociraptor 45 degrees (grip), T. rex 42 (grip), Triceratops
  and Ankylosaurus 37 (hooves slip), Brachiosaurus 23 (its hind legs can't push its 50 t up any steeper).
  The third limit, balance, the creature finds itself in the game (2026-11-08, ABFDino::MeasureStanding): the first
  time it appears, an unseen copy of it stands still on imaginary slopes, facing up them and then down, with the
  game's own stance, leg IK and tipping rule (5 degree steps, then 1 degree steps; 20-ish tries, about 30 ms), and it
  never walks up a slope steeper than it can stand still on. So a new creature needs no measuring run and nothing
  written into its rig: whatever its skeleton and weight, its limit is worked out when it first walks. (Before, a
  Python model of the game's stance missed by up to 20 degrees, then a -bfslopes run wrote what it saw into the rigs;
  both are gone.) Facing downhill is tested too and told in the climb summary when it's lower; it can still walk down
  steeper slopes, and stopping on one it tips over forwards. Found that way: Velociraptor 37 uphill (it tipped over
  standing on the 40 degree ramp), the Velociraptor on short Brachiosaurus arms 10, the Pteranodon on T. rex legs 25,
  the T. rex on 90% Brachiosaurus arms 26, the T. rex with an Ankylosaurus tail 42 uphill but 12 facing down (the
  club tail doesn't balance its head). How it's done: unreal/README.md, Slopes.

The swim check (swim_check, in every rig as swim_check)
  Whole-body density is a game value per species (SWIM_DENSITY: air-sac theropods 880 kg/m3, Brachiosaurus 800,
  Pteranodon 650, Triceratops 970, the armoured Ankylosaurus 1040 so it sinks), with the air in the trunk (and the neck
  of those with air sacs), high in the flesh. Each capsule keeps its own species' density, so a spliced Brachiosaurus
  neck brings its air with it. The body, as small spheres, is turned and sunk until it displaces its own weight and
  settles with its centre of buoyancy over its centre of mass. Verdicts: swims; head_under (nostrils under even with
  the neck raised); sinks. A body that left to itself would roll over is "tippy": it still swims, paddling to stay
  upright (Henderson 2004 found floating sauropods tipsy). It also gives the neck lift that keeps the nostrils 30 cm
  clear and the depth of the testing ground's own centre of mass, which floats it there. The neck lift is shared out
  over the neck's joints and the head by their upward range, each bending everything beyond it, the way the testing
  ground poses it (at most two thirds of the whole range, 60 degrees).
  Speed: paddling thrust (PADDLE_SHARE, a quarter of a percent of the walking legs' full muscle pull) against the
  water's drag (drag coefficient 0.6) on the trunk's widest section as far as it's under water, v = sqrt(2F / (rho Cd
  A)); a tippy swimmer keeps four fifths of it. Thrust and drag both grow with size squared, so a big animal swims no
  faster than a small one unless its legs are stronger for its bulk: T. rex 1.4 m/s, Pteranodon 1.4, Brachiosaurus
  0.9, Velociraptor 0.7.

The splicing editor (compose.py; the testing ground's menu, Splice a creature)
  A creature is a recipe: one base species, and for each section (Head, Neck, Tail, Hip = legs, Shoulder = arms) a donor
  species and a size from 3% to 400%, or Fit: the same bones and shape at the base's own size (legs by the height of
  their socket, anything else by how far the section reaches). A thickness from 50% to 300% widens a part's flesh, foot
  included, and leaves its joints where they are: its mass and muscle go with the square of it. compose.py chains splice.py over the sections, legs first and arms last.
    python splice/compose.py preview <recipe.json> <out.json>   rig only: mass, lineage, checks, capsules (about a second)
    python splice/compose.py build <recipe.json> <out.json>     and the body grown (about a minute): key ed_<...>
  A resized section is simply a differently sized part (make_part: density the same, so mass goes with the cube, a wing's
  area with the square), so the placing rules above all still hold. New walking front legs on a four-legged animal tilt
  it about its hips, as far as 45 degrees; past that the readout flags that the feet don't reach the ground. A part over
  3 times heavier or lighter than the section it replaces is flagged, not refused. So is standing on its front legs
  alone, and any joint still more than 10 cm under the ground (UNDERGROUND_CM; since front_stance and carry_tail, no
  test recipe has one: before them Brachiosaurus arms on a Velociraptor went 334 cm under). The wing-folding search takes up to
  20 s and is remembered in _fold_memo.json. Balance in the readout: a two-legged animal (or one on its front legs
  alone) may step its feet up to 90% of its hip's (shoulder's) range, as the testing ground does; it tips over when its
  weight is still more than TIP_SHARE (2%) of its length outside its feet, and the testing ground then topples it
  (ABFDino::TickBalance, a second after it stands still that way).
  Move ("move": share of the trunk's length, + forward): legs or arms slide along the body after the swaps. Walking limbs
  keep their height, so the feet stay down; carried ones ride the spine. The limb hangs from the nearest spine bone and
  its flesh starts on the spine there (mount_from_cm). Front and back legs keep their order and stay on the trunk.
  Add ("add": pair, club): a third pair of limbs with _2 in its names (thigh_2_l; parts leg2_foot_l). Legs and pillar
  arms walk, sized to reach the ground; other arms are carried; wings fold at rest, and are the creature's flight if it
  had none ("wing_pair": "_2"), with flight muscle still the torso's. "extra_pair" in the rig tells the game about it. The
  load is shared as on equal springs. "club" puts an Ankylosaurus' tail club on the last tail bone (solid bone by size).
  Bones ("bones": {bone: {donor, scale, fit, move}}; the editor's Advanced mode, a section's Bones button): single bones
  of the neck, tail, legs (thigh, shin) or arms (upper arm, forearm, hand) changed inside the section, after its swap
  and size; the left bone stands for the pair. A bone from another species (any that has that bone: "donor") brings
  that species' length, flesh (tissue), motion limits, lineage and capsule; fitted ("fit", the panel's Own length) it
  keeps this bone's length and the donor's flesh is scaled to it, so its girth and mass grow with it (density the same:
  a Triceratops shin fitted to a T. rex's is about twice as thick and weighs 1.1 t). "scale" (25% to 300%) is its
  length only: the flesh keeps its girth (the section's Thick is for girth) and its mass goes with its length.
  "move" (-40% to +40%) slides its joint along the bone above (a knee up or down the thigh): the bones either side
  change length, the joints below stay, so a leg keeps its height. The joints below a resized bone ride along; walking
  legs whose height changed are set back on the ground (the section moves, splice.py lifts or tilts the body to it);
  bone parts ride the joint nearest them. edit_bones writes it as a part like make_part's, so the placing rules above
  still hold. Key bits b<bone><donor>[f][<scale%>][m<move%>]: ed_trex_bcalftrikefm-15_bneck02150 is "T. rex with
  Triceratops shins (fitted) (knees 15% higher), neck bone 2 150%". The readout's "bones" lists each section's
  changeable bones with their length, species and which species could give them.
  Keys over 48 characters become ed_<base>_<hash>: an imported mesh's path repeats the key three times.
  A walking third pair slides along the body to the nearest place its thigh's flesh clears the animal's own walking
  legs' (2 cm apart; set close, it grew into them and left an untidy seam); with no room between them it is flagged.
  grow_body keeps at least LIMB_VOXELS (1.8) voxels across the thinnest limb's half-thickness when the cell cap allows
  (raptorrexhead's arms now grow on 0.67 cm voxels instead of 1.2). When it doesn't (one part making the box far
  bigger than a small limb), that limb grows on its own finer grid (fine_regions, field_on: the same field, the
  fillets at the body's own blend), meshed in a box around it and zipped into the body's mesh (patch_in, zipper); if
  the two edges don't pair up the body's own mesh is kept. Tested on a Velociraptor forced to 2 cm voxels: broken
  spikes and floating hands without it, hands stitched to 0.01 cm with it, no line where the patch meets the body. No
  creature built so far needs it; the six and the splices grow byte-identical.
  Walk it (2026-11-08): the game builds the creature while it runs and you play it at once: unreal/build_glb.py <key>
  writes its .glb, Unreal imports it in memory, and everything it does comes from its rig at runtime (unreal/README.md,
  Splicing editor). Swaps, sizes, Move, Add and single bones alike. walk_it.py (waits for the game to close, imports
  the creature into the project with run_bf.py and run_bf_materials.py, restarts the game with -bfstart=<key>) still
  works but the game no longer uses it.

Results (2026-11-04; in the testing ground with -bfcatalog)
  raptorwings        Velociraptor + Pteranodon wings     30 kg     walks (17x)     glides 110 m off the 20 m cliff
  ankylowings        Ankylosaurus + Pteranodon wings     6.6 t     front legs buckle (0.07x); can't fly (132x the limit)
  rextrikehead       T. rex + Triceratops head           12.7 t    walks (4.4x), stable 30 cm inside its feet
  rexankylotail      T. rex + Ankylosaurus tail          9.8 t     walks (5.8x), stable
  raptorbrachioneck  Velociraptor + Brachiosaurus neck   4.6 t     legs buckle (0.11x); would tip 1.6 m forward anyway
  trikeraptorlegs    Triceratops on Velociraptor legs    12.1 t    hind legs buckle (0.08x); body tilted 26° tail-down
                                                                   (rebuilt 2026-11-08: its tail had been 168 cm under
                                                                   the ground; now lifted 30°, the rest along the ground)
  pterarexlegs       Pteranodon on T. rex legs           1.1 t     walks (51x) on two legs; can't fly (22x the limit)
  raptorrexhead      Velociraptor + T. rex head          1.1 t     legs buckle (0.45x); would tip 1.2 m forward
