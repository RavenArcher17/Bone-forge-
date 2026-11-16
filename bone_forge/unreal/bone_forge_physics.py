"""Builds and saves a physics asset for each imported Bone Forge Skeletal Mesh (run inside Unreal).

    py "<path>/bone_forge/unreal/bone_forge_physics.py" [--species trex ...]

Needs the testing-ground project's C++ (UBFPhysics in C:/Dev/BoneForgeTest), which does the work:
- one capsule body per bone in the rig JSON's collision_capsules, with the JSON's masses;
- one constraint per body to the nearest parent body, limited to limits_deg with the importer's axes
  (bend = Swing2/Pitch, side = Swing1/Yaw, twist = Twist; each range centred with AngularRotationOffset);
- no collision between bodies that already overlap in the standing pose.
Saves <key>_physics next to <key>_skeletal and sets it as the mesh's physics asset.
"""
import argparse
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RIG_FOLDER = os.path.join(os.path.dirname(HERE), "rig_joints")
SPECIES = ["trex", "trike", "raptor", "ankylo", "brachio", "ptera"]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--species", nargs="*", default=SPECIES)
    args = ap.parse_args(argv)
    failed = 0
    for key in args.species:
        report = unreal.BFPhysics.create_physics_asset(RIG_FOLDER, key)
        unreal.log(f"[BoneForge] physics {report}")
        if report.startswith("ERROR"):
            failed += 1
            continue
        pa_path = report.split(" -> ")[1]
        cap = key[0].upper() + key[1:]
        mesh_path = f"/Game/BoneForge/{cap}/{key}_skeletal/SkeletalMeshes/{key}_skeletal.{key}_skeletal"
        for path in (pa_path, mesh_path):
            asset = unreal.load_asset(path)
            if asset is None or not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
                unreal.log_error(f"[BoneForge] could not save {path}")
                failed += 1
    unreal.log(f"[BoneForge] physics assets done, {failed} problem(s)")


if __name__ == "__main__":
    main(sys.argv[1:])
