"""Bone Forge rig importer, part 3: run this inside the Unreal Editor.

What it does, for one species (T. rex by default):
  1. Imports the body OBJ as a Static Mesh and reports its scale and facing
     (size, which way is up, which way the head points, cm per OBJ unit).
  2. Builds a skinned .glb from the rig JSON + body OBJ (bone_forge_gltf.py) and imports it
     as a Skeletal Mesh. That creates the Skeleton asset, with one bone per rig joint.
     A first import goes into a temporary _AxisProbe folder to measure how the importer
     maps glTF axes; if that differs from the expected map, the .glb is rebuilt to match.
  3. Checks every bone against the rig JSON: name, parent, position, bone axis.
The report goes to the Output Log and to out/<species>_unreal_report.txt next to this script.

Run it from the Output Log (switch the input box from Cmd to Python, or type py first):
    py "C:/path/to/bone_forge/unreal/bone_forge_unreal.py"
    py "C:/path/to/bone_forge/unreal/bone_forge_unreal.py" --species raptor
or use File > Execute Python Script.

Options
  --species NAME     trex, trike, raptor, ankylo, brachio or ptera (default trex)
  --root DIR         folder holding rig_joints/ and body_shapes/ (default: the folder above this script)
  --dest PATH        content folder (default /Game/BoneForge); assets go in <dest>/<Species>
  --skip-obj-check   don't import the OBJ as a Static Mesh
  --out DIR          where the .glb and the report are written (default: out/ next to this script)

Needs the Python Editor Script Plugin (on by default in UE 5) and Unreal's glTF import
(Interchange; on by default from UE 5.3). Re-running replaces the assets it made before.
"""

import argparse
import importlib
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(globals().get("__file__") or getattr(sys, "argv", [""])[0]))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import bone_forge_gltf as bfg  # noqa: E402
import bone_forge_checks as bfc  # noqa: E402

importlib.reload(bfg)  # pick up edits when the script is re-run in the same editor session
importlib.reload(bfc)

PROBE_FOLDER = "_AxisProbe"


class Report:
    def __init__(self):
        self.lines = []

    def __call__(self, text="", level="info"):
        self.lines.append(text)
        {"info": unreal.log, "warn": unreal.log_warning, "error": unreal.log_error}[level]("[BoneForge] " + text)


def import_file(path, dest, report):
    """Import one file with Unreal's default (automated) settings; return the loaded assets."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths") or [])
    if not paths and unreal.EditorAssetLibrary.does_directory_exist(dest):
        paths = list(unreal.EditorAssetLibrary.list_assets(dest, recursive=False, include_folder=False))
    assets = [unreal.EditorAssetLibrary.load_asset(p) for p in paths]
    assets = [a for a in assets if a is not None]
    report(f"Imported {os.path.basename(path)} -> {dest}: " +
           (", ".join(f"{a.get_name()} ({a.get_class().get_name()})" for a in assets) or "nothing"))
    return assets


def read_bones(skm):
    """Bones of a skeletal mesh in component space: [{name, parent, pos, x_axis}] (cm)."""
    comp = unreal.new_object(unreal.SkeletalMeshComponent)
    for setter in ("set_skeletal_mesh_asset", "set_skinned_asset_and_update", "set_skeletal_mesh"):
        if hasattr(comp, setter):
            getattr(comp, setter)(skm)
            break
    n = comp.get_num_bones()
    names = [str(comp.get_bone_name(i)) for i in range(n)]
    parents = {}
    for name in names:
        p = str(comp.get_parent_bone(name))
        parents[name] = None if p in ("None", "") else p
    if hasattr(comp, "get_ref_pose_transform"):
        local = {}
        for i, name in enumerate(names):
            t = comp.get_ref_pose_transform(i)
            q, v, s = t.rotation, t.translation, t.scale3d
            local[name] = ((q.x, q.y, q.z, q.w), (v.x, v.y, v.z), (s.x, s.y, s.z))
        return bfc.compose_bones(names, parents, local)
    # Older engines: spawn the mesh far below the level, read component-space sockets, remove it.
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actors.spawn_actor_from_object(skm, unreal.Vector(0, 0, -100000))
    try:
        mc = actor.get_editor_property("skeletal_mesh_component")
        out = []
        for name in names:
            t = mc.get_socket_transform(name, unreal.RelativeTransformSpace.RTS_COMPONENT)
            v, x = t.translation, t.rotation.get_axis_x()
            out.append({"name": name, "parent": parents[name], "pos": (v.x, v.y, v.z), "x_axis": (x.x, x.y, x.z)})
        return out
    finally:
        actors.destroy_actor(actor)


def rotator_for(m):
    """Unreal rotator for a rotation matrix given as rows."""
    x = unreal.Vector(m[0][0], m[1][0], m[2][0])
    z = unreal.Vector(m[0][2], m[1][2], m[2][2])
    r = unreal.MathLibrary.make_rot_from_xz(x, z)
    return f"Roll {r.roll:.0f}, Pitch {r.pitch:.0f}, Yaw {r.yaw:.0f}"


def check_obj(obj_path, dest, report):
    report(f"--- 1. Body OBJ as a Static Mesh: {obj_path}")
    meshes = [a for a in import_file(obj_path, dest, report) if isinstance(a, unreal.StaticMesh)]
    if not meshes:
        report("No Static Mesh was created from the OBJ.", "error")
        return False
    lo, hi = [1e30] * 3, [-1e30] * 3
    for m in meshes:
        box = m.get_bounding_box()
        for i, c in enumerate("xyz"):
            lo[i] = min(lo[i], getattr(box.min, c))
            hi[i] = max(hi[i], getattr(box.max, c))
    ok, lines = bfc.obj_report(obj_path, lo, hi)
    for line in lines:
        report("    " + line, "info" if ok else "warn")
    if not ok:
        err, s, k = bfc.best_obj_fit(bfc.match_bounds(*bfc.obj_expected_bounds(obj_path), lo, hi))
        if bfg.m_det(s) > 0 and err < 1.0 * max(1.0, k):
            report(f"    fix_rotation: set the importer's rotation to {rotator_for(bfg.m_t(s))} "
                   f"and its uniform scale to {1 / k:g}.", "warn")
    return ok


def build_and_import(rig, obj, glb, dest, ue_map, report):
    s = bfg.build_glb(rig, obj, glb, ue_map)
    report(f"Built {glb}: {s['bones']} bones, {s['vertices']} vertices, {s['triangles']} triangles, "
           f"max {s['max_influences']} bones per vertex")
    assets = import_file(glb, dest, report)
    skm = next((a for a in assets if isinstance(a, unreal.SkeletalMesh)), None)
    return skm, assets


def main(argv=None):
    ap = argparse.ArgumentParser(prog="bone_forge_unreal.py")
    ap.add_argument("--species", default="trex")
    ap.add_argument("--root", default=os.path.dirname(HERE))
    ap.add_argument("--dest", default="/Game/BoneForge")
    ap.add_argument("--skip-obj-check", action="store_true")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    args = ap.parse_args(getattr(sys, "argv", [])[1:] if argv is None else argv)

    key = args.species
    rig = os.path.join(args.root, "rig_joints", f"{key}_rig.json")
    obj = os.path.join(args.root, "body_shapes", f"{key}_body.obj")
    # The six starter species wear the bodies grown from their bones (body_tools/grow_body.py, <key>_grown.obj) like
    # every splice does (a splice's _body.obj is grown already); BF_BODY=hand imports the hand-built one instead.
    grown = os.path.join(args.root, "body_shapes", f"{key}_grown.obj")
    if os.path.isfile(grown) and os.environ.get("BF_BODY", "grown") != "hand":
        obj = grown
    for p in (rig, obj):
        if not os.path.isfile(p):
            raise FileNotFoundError(f"{p} not found (use --root to point at the bone_forge folder)")
    folder = f"{args.dest.rstrip('/')}/{key[:1].upper()}{key[1:]}"
    out_dir = args.out
    glb = os.path.join(out_dir, f"{key}_skeletal.glb")
    report = Report()
    report(f"Bone Forge import: {key}  (Unreal {unreal.SystemLibrary.get_engine_version()})")

    obj_ok = True if args.skip_obj_check else check_obj(obj, folder + "/ObjCheck", report)

    report("--- 2. Skeleton from the rig JSON")
    probe = f"{args.dest.rstrip('/')}/{PROBE_FOLDER}"
    ue_map = bfg.UE_FROM_GLTF
    skm, _ = build_and_import(rig, obj, glb, probe, ue_map, report)
    if skm is None:
        report("No Skeletal Mesh was created from the .glb. Unreal's glTF import (Interchange) is needed: "
               "UE 5.3 or newer, or enable it under Edit > Plugins.", "error")
        return report
    ok, _, (rms, s, k) = bfc.check_skeleton(rig, read_bones(skm))
    unreal.EditorAssetLibrary.delete_directory(probe)
    if ok:
        report("Importer axis map is as expected: glTF (x, y, z) m -> Unreal (x, z, y) * 100 cm.")
    elif rms < 0.5 and bfg.m_det(s) > 0:
        ue_map = bfc.corrected_map(ue_map, s, k)
        report(f"Importer axis map differs (skeleton came in {bfc.describe(s)}, scale {k:g}); "
               f"rebuilding the .glb for map {ue_map}.", "warn")
    else:
        report("Probe import doesn't match the rig under any axis change; importing anyway, see below.", "warn")

    skm, assets = build_and_import(rig, obj, glb, folder, ue_map, report)
    if skm is None:
        report("No Skeletal Mesh was created on the final import.", "error")
        return report
    skeleton = skm.get_editor_property("skeleton")
    bones = read_bones(skm)
    ok, lines, _ = bfc.check_skeleton(rig, bones)
    report(f"Skeletal Mesh {skm.get_path_name()}, Skeleton {skeleton.get_path_name() if skeleton else '-'}, "
           f"{len(bones)} bones")
    for line in lines:
        report("    " + line, "info" if ok else "warn")
    report("--- Result: " + ("OBJ scale and facing OK, " if obj_ok else "OBJ needs a fix (see above), ") +
           ("skeleton matches the rig JSON." if ok else "skeleton does NOT match the rig JSON (see above)."),
           "info" if ok and obj_ok else "warn")

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{key}_unreal_report.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(report.lines) + "\n")
    unreal.log(f"[BoneForge] Report written to {path}")
    return report


if __name__ != "bone_forge_unreal":  # run when executed as a script (py ... / Execute Python Script), not on import
    main()
