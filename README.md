# Bone Forge

A creature designer and living-island sim in Unreal Engine 5.8 (C++).

- `Source/`, `Content/`, `Config/`: the Unreal project (open `BoneForgeTest.uproject`).
- `bone_forge/`: the Python pipeline (rigs, bodies, splicing, glTF build, Unreal import scripts) and the creature files the game reads while it runs. The main notes are in `bone_forge/unreal/README.md`.

Big files (uasset, glb, obj, png) are stored with Git LFS: install it before cloning (`git lfs install`).

After cloning, set `RigFolder` in `Config/DefaultGame.ini` to this clone's `bone_forge/rig_joints` folder.
