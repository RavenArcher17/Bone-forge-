"""Runs bone_forge_unreal.py against a stand-in for Unreal's `unreal` Python module.

    python tests/test_unreal_script.py

The stand-in fakes the editor's importers: the OBJ becomes a Static Mesh whose bounding box
is the OBJ under some axis map, and the .glb becomes a Skeletal Mesh whose reference pose is
the glTF joints under some axis map. Trying different maps checks that the script reports
scale and facing correctly and corrects the skeleton when the glTF importer's axes differ
from the expected ones. It does NOT prove the real editor API calls; only Unreal can.
"""
import os
import shutil
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import bone_forge_gltf as bfg  # noqa: E402
from test_gltf import read_glb, quat_to_matrix  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OBJ_SWAP = ((1, 0, 0), (0, 0, 1), (0, 1, 0))        # OBJ (x, y, z) -> Unreal (x, z, y): what Bone Forge expects
OBJ_AS_IS = ((1, 0, 0), (0, 1, 0), (0, 0, 1))       # importer ignores Y-up: creature lies on its side
GLTF_SWAP = bfg.UE_FROM_GLTF                        # glTF (x, y, z) m -> Unreal (x, z, y) * 100
GLTF_FRONT_Z = ((0, 0, 100), (-100, 0, 0), (0, 100, 0))  # an importer that keeps glTF's +Z front as Unreal +X


# ---------------------------------------------------------------- the fake `unreal` module
def make_fake_unreal(obj_map, obj_scale, gltf_map):
    u = types.ModuleType("unreal")
    store = {}
    u.messages = []

    class Vector:
        def __init__(self, x=0.0, y=0.0, z=0.0):
            self.x, self.y, self.z = x, y, z

    class Quat:
        def __init__(self, x, y, z, w):
            self.x, self.y, self.z, self.w = x, y, z, w

    class Box:
        def __init__(self, lo, hi):
            self.min, self.max = Vector(*lo), Vector(*hi)

    class Transform:
        def __init__(self, q, t):
            self.rotation, self.translation, self.scale3d = Quat(*q), Vector(*t), Vector(1, 1, 1)

    class _Asset:
        def __init__(self, path, props=None):
            self._path, self._props = path, props or {}

        def get_name(self):
            return self._path.rsplit("/", 1)[-1]

        def get_path_name(self):
            return self._path

        def get_class(self):
            return types.SimpleNamespace(get_name=lambda: type(self).__name__)

        def get_editor_property(self, k):
            return self._props[k]

    class StaticMesh(_Asset):
        def get_bounding_box(self):
            return Box(*self._props["bounds"])

    class SkeletalMesh(_Asset):
        pass

    class Skeleton(_Asset):
        pass

    class SkeletalMeshComponent:
        def set_skeletal_mesh_asset(self, m):
            self.m = m

        def get_num_bones(self):
            return len(self.m._props["bones"])

        def get_bone_name(self, i):
            return self.m._props["bones"][i][0]

        def get_parent_bone(self, name):
            return next(b[1] for b in self.m._props["bones"] if b[0] == name) or "None"

        def get_ref_pose_transform(self, i):
            _, _, q, t = self.m._props["bones"][i]
            return Transform(q, t)

    class AssetImportTask:
        def __init__(self):
            self.p = {"imported_object_paths": []}

        def set_editor_property(self, k, v):
            self.p[k] = v

        def get_editor_property(self, k):
            return self.p[k]

    def fake_import(task):
        path, dest = task.p["filename"], task.p["destination_path"]
        name = os.path.splitext(os.path.basename(path))[0]
        if path.endswith(".obj"):
            verts, parts = bfg.load_obj(path)
            used = {i for _, tris in parts for t in tris for i in t}
            pts = [bfg.v_scale(bfg.m_vec(obj_map, verts[i]), obj_scale) for i in used]
            lo = tuple(min(p[i] for p in pts) for i in range(3))
            hi = tuple(max(p[i] for p in pts) for i in range(3))
            assets = [StaticMesh(f"{dest}/{name}", {"bounds": (lo, hi)})]
        else:  # .glb: joints under the importer's axis map, as Unreal local reference pose
            gl, _ = read_glb(path)
            q_ue = bfg.m_orthonormal_part(gltf_map)
            parent = {c: i for i, nd in enumerate(gl["nodes"]) for c in nd.get("children", [])}
            world = {}

            def world_of(i):
                if i not in world:
                    nd = gl["nodes"][i]
                    r, t = quat_to_matrix(nd["rotation"]), tuple(nd["translation"])
                    if i in parent:
                        pr, pt = world_of(parent[i])
                        r, t = bfg.m_mul(pr, r), bfg.v_add(pt, bfg.m_vec(pr, t))
                    world[i] = (r, t)
                return world[i]

            bones = []
            for i in gl["skins"][0]["joints"]:
                r, t = world_of(i)
                r_ue, t_ue = bfg.m_mul(bfg.m_mul(q_ue, r), bfg.m_t(q_ue)), bfg.m_vec(gltf_map, t)
                if i in parent:
                    pr, pt = world_of(parent[i])
                    pr_ue, pt_ue = bfg.m_mul(bfg.m_mul(q_ue, pr), bfg.m_t(q_ue)), bfg.m_vec(gltf_map, pt)
                    lr, lt = bfg.m_mul(bfg.m_t(pr_ue), r_ue), bfg.m_vec(bfg.m_t(pr_ue), bfg.v_sub(t_ue, pt_ue))
                    par = gl["nodes"][parent[i]]["name"]
                else:
                    lr, lt, par = r_ue, t_ue, None
                bones.append((gl["nodes"][i]["name"], par, bfg.quat_from_matrix(lr), lt))
            mesh = gl["meshes"][0]["name"]
            skel = Skeleton(f"{dest}/{mesh}_Skeleton")
            assets = [SkeletalMesh(f"{dest}/{mesh}", {"bones": bones, "skeleton": skel}), skel]
        for a in assets:
            store[a.get_path_name()] = a
        task.p["imported_object_paths"] = [a.get_path_name() for a in assets]

    class EditorAssetLibrary:
        @staticmethod
        def load_asset(p):
            return store.get(p)

        @staticmethod
        def does_directory_exist(d):
            return any(p.startswith(d + "/") for p in store)

        @staticmethod
        def list_assets(d, recursive=True, include_folder=False):
            return [p for p in store if p.startswith(d + "/")]

        @staticmethod
        def delete_directory(d):
            for p in [p for p in store if p.startswith(d + "/")]:
                del store[p]
            return True

    class MathLibrary:
        @staticmethod
        def make_rot_from_xz(x, z):
            return types.SimpleNamespace(roll=0.0, pitch=0.0, yaw=0.0)

    u.Vector, u.StaticMesh, u.SkeletalMesh, u.SkeletalMeshComponent = Vector, StaticMesh, SkeletalMesh, SkeletalMeshComponent
    u.AssetImportTask, u.EditorAssetLibrary, u.MathLibrary = AssetImportTask, EditorAssetLibrary, MathLibrary
    u.AssetToolsHelpers = types.SimpleNamespace(get_asset_tools=lambda: types.SimpleNamespace(
        import_asset_tasks=lambda tasks: [fake_import(t) for t in tasks]))
    u.new_object = lambda cls: cls()
    u.SystemLibrary = types.SimpleNamespace(get_engine_version=lambda: "fake")
    u.log = lambda m: u.messages.append(("info", m))
    u.log_warning = lambda m: u.messages.append(("warn", m))
    u.log_error = lambda m: u.messages.append(("error", m))
    u.store = store
    return u


OUT = tempfile.mkdtemp(prefix="bone_forge_test_")  # keeps the fake runs out of unreal/out


def run(obj_map, obj_scale, gltf_map, species="trex"):
    sys.modules["unreal"] = make_fake_unreal(obj_map, obj_scale, gltf_map)
    sys.modules.pop("bone_forge_unreal", None)
    import bone_forge_unreal  # noqa: F401  (importing does not run main)
    report = bone_forge_unreal.main(["--species", species, "--root", ROOT, "--out", OUT])
    return report.lines, sys.modules["unreal"]


def expect(cond, what, lines):
    if not cond:
        print("FAIL:", what)
        print("\n".join("    " + line for line in lines))
        sys.exit(1)
    print("ok  ", what)


def main():
    lines, u = run(OBJ_SWAP, 1.0, GLTF_SWAP)
    text = "\n".join(lines)
    expect("No rotation or scale needed" in text, "expected importers: OBJ reported upright, facing +X, 1 unit = 1 cm", lines)
    expect("433 cm (X x Y x Z)" in text and "height on Z 4.33 m" in text, "expected importers: T. rex is 4.33 m tall", lines)
    expect("Importer axis map is as expected" in text, "expected importers: probe finds the expected glTF axis map", lines)
    expect("All 39 bone positions match" in text and "skeleton matches the rig JSON" in text,
           "expected importers: all 39 bones match the rig", lines)
    expect(not any(p.startswith("/Game/BoneForge/_AxisProbe/") for p in u.store), "probe folder is deleted", lines)
    expect(any(p.endswith("/Trex/SK_Trex") for p in u.store), "skeletal mesh lands in /Game/BoneForge/Trex", lines)

    lines, _ = run(OBJ_AS_IS, 1.0, GLTF_FRONT_Z)
    text = "\n".join(lines)
    expect("up is +Y" in text and "fix_rotation" in text, "OBJ importer without Y-up conversion: reported as lying on its side", lines)
    expect("Importer axis map differs" in text and "skeleton matches the rig JSON" in text,
           "different glTF axis map: detected, rebuilt, and the final skeleton matches", lines)
    expect("(0.0, 0.0, 100.0), (-100.0, 0.0, 0.0), (0.0, 100.0, 0.0)" in text,
           "the corrected axis map is exactly the importer's map", lines)

    lines, _ = run(OBJ_SWAP, 100.0, GLTF_SWAP)
    expect("scale 100" in "\n".join(lines), "OBJ read as metres: reported as scale 100", lines)

    for sp in ("trike", "raptor", "ankylo", "brachio", "ptera"):
        lines, _ = run(OBJ_SWAP, 1.0, GLTF_SWAP, sp)
        expect("skeleton matches the rig JSON" in "\n".join(lines) and "No rotation or scale needed" in "\n".join(lines),
               f"{sp}: OBJ and skeleton check out", lines)


if __name__ == "__main__":
    try:
        main()
    finally:
        shutil.rmtree(OUT, ignore_errors=True)
