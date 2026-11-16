"""The handoff's Blender check, for every species: import unreal/out/<key>_skeletal.glb and compare it with the rig.

    blender -b -P blender_gltf_check.py

Per species: armature bone count = rig joints, every bone head within 0.1 cm of the rig JSON (Blender: X forward,
Y = the creature's LEFT, Z up, metres), left legs on +Y (so not mirrored), head forward of the hips, the rig limits
present as custom properties, and the mesh skinned to the armature. Prints one PASS/FAIL line per species.
"""
import json
import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
failed = 0
for key in ["trex", "trike", "raptor", "ankylo", "brachio", "ptera"]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "unreal", "out", f"{key}_skeletal.glb"))
    with open(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), encoding="utf-8") as f:
        rig = json.load(f)
    arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    skinned = [o for o in bpy.context.scene.objects
               if o.type == "MESH" and any(m.type == "ARMATURE" and m.object == arm for m in o.modifiers)]
    problems = []
    if len(arm.data.bones) != len(rig["joints"]):
        problems.append(f"{len(arm.data.bones)} bones, rig has {len(rig['joints'])}")
    worst = 0.0
    for j in rig["joints"]:
        b = arm.data.bones.get(j["name"])
        if b is None:
            problems.append(f"missing {j['name']}")
            continue
        x, y, z = j["position_cm"]
        h = arm.matrix_world @ b.head_local
        worst = max(worst, ((h.x * 100 - x) ** 2 + (h.y * 100 + y) ** 2 + (h.z * 100 - z) ** 2) ** 0.5)
    if worst > 0.1:
        problems.append(f"bone heads off by up to {worst:.2f} cm")
    head = lambda n: arm.matrix_world @ arm.data.bones[n].head_local
    if not head("thigh_l").y > 0 > head("thigh_r").y:
        problems.append("left leg not on +Y (mirrored)")
    if not head("head").x > head("pelvis").x:
        problems.append("head not forward of the hips")
    with_limits = sum(1 for j in rig["joints"] if j.get("limits_deg"))
    props = sum(1 for b in arm.data.bones if any("bend" in k or "limit" in k for k in b.keys()))
    if props < with_limits:
        problems.append(f"limits on {props} bones, rig has {with_limits}")
    if not skinned:
        problems.append("no mesh skinned to the armature")
    failed += bool(problems)
    print(f"[blender-check] {key}: {'PASS' if not problems else 'FAIL'}  {len(arm.data.bones)} bones, "
          f"heads within {worst:.3f} cm, limits on {props} bones, {len(skinned)} skinned mesh"
          + ("" if not problems else "  -- " + "; ".join(problems)))
print(f"[blender-check] {6 - failed}/6 pass")
