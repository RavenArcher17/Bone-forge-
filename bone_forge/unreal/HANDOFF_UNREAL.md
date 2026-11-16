# Bone Forge: Unreal importer handoff

## Goal
Test the Bone Forge rig importer in Unreal (and Blender) on the user's computer. A cloud session
wrote and tested it, but couldn't run Unreal. Read the main HANDOFF.md (in bone_forge_source.zip)
for the project; this note only covers the Unreal import step.

## Setup
Unzip `bone_forge_unreal_importer.zip` so the `unreal/` folder sits next to the folders from the
earlier zips:

```
bone_forge/
  rig_joints/     (dino_rig_joints.zip: *_rig.json)
  body_shapes/    (dino_body_shapes.zip: *_body.obj)
  unreal/         (this zip: scripts, tests, README.md)
```

## Do this
1. In the Unreal Editor (UE 5.3+, Python Editor Script Plugin on), Output Log in Python mode:
   `py "<path>/bone_forge/unreal/bone_forge_unreal.py" --species trex`
2. Read the `[BoneForge]` lines in the Output Log, or `unreal/out/trex_unreal_report.txt`. The expected result:
   - OBJ: `1210 x 156 x 433 cm`, "upright (Z up), facing +X ... No rotation or scale needed."
   - Skeleton: `/Game/BoneForge/Trex/SK_Trex` + `SK_Trex_Skeleton`, "All 39 bone positions match the rig JSON",
     "Every bone's X axis points at its child joint".
3. Open SK_Trex and rotate a few bones (neck Pitch, jaw Pitch, thigh Pitch) to check the skin follows.
4. If something fails: the editor-side calls in `bone_forge_unreal.py` were never run in a real editor
   (only against a stand-in). Fix them there:
   - import: `AssetImportTask`;
   - reading bones: a transient `SkeletalMeshComponent` with `get_ref_pose_transform`, falling back to spawning an actor;
   - bounds: `StaticMesh.get_bounding_box`.
   The maths in `bone_forge_gltf.py` / `bone_forge_checks.py` is tested: `python tests/test_gltf.py`,
   `python tests/test_unreal_script.py`.
5. Then the other species: `--species trike|raptor|ankylo|brachio|ptera`.
6. Blender check: File > Import > glTF 2.0, load `unreal/out/trex_skeletal.glb`. The armature should
   have 39 bones. Blender imports glTF Y-up as Z-up, so the T. rex faces +X with its right side
   toward −Y; in Blender that is not mirrored. The rig limits are stored as glTF extras on each joint, which Blender normally shows as custom properties (unchecked).

## What's established
- The T. rex OBJ measures right: 4.33 m tall, 12.10 m long, head +X, leg_l at −Y, feet at Z 0.
  It's Y-up, so the importer must convert it. How Unreal's OBJ importer does that is unknown; the
  script measures it and prints a fix rotation if needed.
- The importer bakes the rig JSON + body OBJ into a skinned glTF (Unreal only creates a Skeleton
  from a skinned mesh). It assumes Unreal maps glTF (x, y, z) m to (x, z, y) × 100 cm.
- A probe import into `/Game/BoneForge/_AxisProbe` measures the real axis map. The script rebuilds
  the file if the map differs, then deletes that folder.
- Bone axes: X points at the child joint, Y toward the creature's right.
  - bend = Pitch: + except tail, calf, hand, jaw, which are −.
  - side = Yaw: neck/spine/head −, tail +, limbs − on _l and + on _r.
  - twist = Roll.
- Skin weights are starters:
  - distance-based and smoothed, max 4 bones per vertex;
  - frill, horns and the Pteranodon crest are rigid on the head;
  - the raptor sickle claws are bones with no weight;
  - opening the jaw stretches the head (no mouth cavity in the body volume).
- Outside Unreal, all six species pass:
  - the Khronos glTF validator, with 0 errors and 0 warnings;
  - bones within 0.01 cm of the JSON, and bind-pose skinning exact;
  - the T. rex posed with the poses.py recipes matches trex_poses.png (`unreal/test_renders/`).

## Design doc
The "In Unreal" section on the Joints and Motion tab records all this and has an unticked task:
"Run bone_forge_unreal.py in Unreal and note the report here". Tick it and add the result once it has run.
Link: https://claude.ai/artifact/6iPmLRGmb4UEpWpbUgSqJg

## Next after it passes
- A physics asset from the rig JSON's `collision_capsules`.
- Constraint limits from `limits_deg`, using the axis mapping above.
