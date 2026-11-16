"""Bakes and saves animation clips for each imported Bone Forge Skeletal Mesh (run inside Unreal).

    py "<path>/bone_forge/unreal/bone_forge_anims.py" [--species trex ...]

Needs the testing-ground project's C++ (UBFAnimBaker in C:/Dev/BoneForgeTest), which samples the same
procedural pose the testing ground draws live (FBFPoser) and writes, into <key>_skeletal/Anims:
- <key>_idle, _walk, _run: in-place loops (walk and run last one stride at the rig's gait speeds);
- <key>_bite, _roar: one-shots;
- <key>_glide, _flap: one wingbeat each (species with a flight pose);
- <key>_locomotion: blend space on Speed (cm/s): idle 0, walk at walk speed, run at the fast gait's speed;
- <key>_flight: blend space on Flap 0..1: glide to flap.
Prints the baker's report: per clip, joint axes past limits_deg and the loop seam.
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
        report = unreal.BFAnimBaker.bake_clips(RIG_FOLDER, key)
        for line in report.splitlines():
            unreal.log(f"[BoneForge] anims {line}")
        if report.startswith("ERROR"):
            failed += 1
            continue
        folder = report.splitlines()[0].split(" -> ")[1]
        if not unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=False, recursive=True):
            unreal.log_error(f"[BoneForge] could not save {folder}")
            failed += 1
    unreal.log(f"[BoneForge] anims done, {failed} problem(s)")


if __name__ == "__main__":
    main(sys.argv[1:])
