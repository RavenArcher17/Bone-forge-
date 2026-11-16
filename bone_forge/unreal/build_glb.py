"""Writes a creature's skinned .glb for the testing ground to import while it runs (BFRuntimeImport):

    python unreal/build_glb.py <key> [out_dir]

The same file bone_forge_unreal.py builds and imports in the editor: the rig's skeleton and its body skinned to it,
with the importer's axis map the editor's probe import measured (glTF x, y, z m -> Unreal x, z, y cm, bfg.UE_FROM_GLTF).
The six use their grown bodies (<key>_grown.obj) as the editor import does; a splice's <key>_body.obj is grown already.
Prints one line: ok <glb path> <bones> <vertices> <triangles>, or error <why>.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bone_forge_gltf as bfg  # noqa: E402

ROOT = os.path.dirname(HERE)


def main(argv):
    if not argv:
        print("error usage: build_glb.py <key> [out_dir]")
        return 2
    key = argv[0]
    out_dir = argv[1] if len(argv) > 1 else os.path.join(HERE, "out")
    rig = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
    obj = os.path.join(ROOT, "body_shapes", f"{key}_body.obj")
    grown = os.path.join(ROOT, "body_shapes", f"{key}_grown.obj")
    if os.path.isfile(grown) and os.environ.get("BF_BODY", "grown") != "hand":
        obj = grown
    for p in (rig, obj):
        if not os.path.isfile(p):
            print(f"error {p} not found")
            return 1
    os.makedirs(out_dir, exist_ok=True)
    glb = os.path.join(out_dir, f"{key}_skeletal.glb")
    s = bfg.build_glb(rig, obj, glb, bfg.UE_FROM_GLTF)
    print(f"ok {glb} {s['bones']} {s['vertices']} {s['triangles']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
