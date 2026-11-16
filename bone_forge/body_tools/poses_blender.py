"""Draw a species' key-poses sheet as black-on-white line art in Blender, posed with its rig.

    blender -b -P poses_blender.py -- <species>            (trex, trike, raptor, ankylo, brachio, ptera)

Poses the skinned glb from unreal/out/<species>_skeletal.glb, so the sheet shows exactly the mesh and
skin weights Unreal gets. Every angle is a share of that joint's limit from the rig JSON (+ share of the
max, - share of the min), turned with the same bone axes and signs as the importer (bone_forge_gltf.py).
Pteranodon flight views use flight_pose_cm the same way the testing ground does (BFDino::BuildFlightPose).
Writes body_shapes/<species>_poses.png.
"""
import fnmatch
import math
import os
import sys

import bpy
from mathutils import Euler, Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "unreal"))
import bone_forge_gltf as bf  # noqa: E402

key = sys.argv[sys.argv.index("--") + 1]
rig_path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
glb_path = os.path.join(ROOT, "unreal", "out", f"{key}_skeletal.glb")
out_path = os.path.join(ROOT, "body_shapes", f"{key}_poses.png")

info_root, joints, topo = bf.load_rig(rig_path)
frames = bf.bone_frames(joints, topo)
info = {n: joints[n].info for n in topo}
NAME = {"trex": "TYRANNOSAURUS REX", "trike": "TRICERATOPS", "raptor": "VELOCIRAPTOR",
        "ankylo": "ANKYLOSAURUS", "brachio": "BRACHIOSAURUS", "ptera": "PTERANODON"}[key]
QUAD = key in ("trike", "ankylo", "brachio", "ptera")

# ------------------------------------------------------------------ poses
# Each pose: (label, view, {bone pattern: (bend share, side share)}, pelvis tilt in degrees).
# view "side" = right side, head to the right; "top" = from above; "front" = from the front.
WALK_DOWN = {"thigh_r": (0.5, 0), "calf_r": (0.1, 0), "thigh_l": (-0.55, 0), "calf_l": (0.3, 0), "foot_l": (0.3, 0)}
WALK_PASS = {"thigh_r": (0.0, 0), "calf_r": (0.05, 0), "thigh_l": (0.15, 0), "calf_l": (0.7, 0), "foot_l": (0.4, 0)}
if QUAD:  # front legs half a stride behind the hind legs on the same side
    WALK_DOWN.update({"upperarm_l": (0.5, 0), "upperarm_r": (-0.5, 0), "lowerarm_r": (-0.4, 0)})
    WALK_PASS.update({"upperarm_r": (0.15, 0), "lowerarm_r": (-0.7, 0), "upperarm_l": (0.0, 0)})
POSES = [
    ("WALK: RIGHT FOOT DOWN", "side", WALK_DOWN, 0),
    ("WALK: LEGS PASSING", "side", WALK_PASS, 0),
    ("LOOK UP", "side", {"neck_*": (0.8, 0), "head": (0.5, 0), "tail_*": (0.6, 0)}, 0 if QUAD else 12),
    ("BITE LOW, JAW OPEN", "side", {"neck_*": (-0.8, 0), "head": (-0.5, 0), "jaw": (0.9, 0), "tail_*": (0.4, 0)},
     0 if QUAD else -14),
    ("TURN (FROM ABOVE)", "top", {"spine_*": (0, 0.8), "chest": (0, 0.8), "neck_*": (0, 0.8), "head": (0, 0.6),
                                  "tail_*": (0, -0.6), "pelvis": (0, 0.5)}, 0),
]
if key == "ptera":
    POSES += [
        ("FLIGHT (FROM ABOVE)", "flight_top", {}, 0),
        ("FLIGHT (FROM THE FRONT)", "flight_front", {}, 0),
        ("LAUNCH CROUCH", "side", {"thigh_*": (0.5, 0), "calf_*": (0.8, 0), "foot_*": (0.5, 0),
                                   "upperarm_*": (0.4, 0), "lowerarm_*": (0.6, 0), "neck_*": (0.4, 0)}, 20),
    ]
else:
    POSES.append(("TAIL SWING (FROM ABOVE)", "top", {"tail_*": (0, 0.9), "neck_*": (0, -0.3)}, 0))
    if QUAD:
        POSES.append(("STOMP: FRONT FOOT RAISED", "side",
                      {"upperarm_r": (1.0, 0), "lowerarm_r": (0.8, 0), "hand_r": (-0.6, 0), "neck_*": (-0.3, 0),
                       "head": (-0.4, 0), "jaw": (0.5, 0)}, 0))
    else:
        POSES.append(("ROAR", "side", {"neck_*": (0.4, 0), "head": (0.3, 0), "jaw": (1.0, 0),
                                       "upperarm_*": (0.6, 0.5), "tail_*": (0.3, 0)}, 10))


def limit(n, axis):
    lim = info[n].get("limits_deg") or {}
    return lim.get(axis) or [0, 0]


def share_deg(n, axis, s):
    lo, hi = limit(n, axis)
    return s * hi if s >= 0 else -s * lo


def angles_for(recipe, tilt):
    out = {}
    for pat, (b, s) in recipe.items():
        for n in topo:
            if fnmatch.fnmatch(n, pat):
                out[n] = (share_deg(n, "bend", b), share_deg(n, "side", s))
    if tilt:  # whole body nose up/down at the hips, legs kept where they were
        b, s = out.get("pelvis", (0, 0))
        out["pelvis"] = (b + tilt, s)
        for leg in ("thigh_l", "thigh_r"):
            lb, ls = out.get(leg, (0, 0))
            out[leg] = (lb - tilt, ls)
    return out


def M3(frame):
    return Matrix([[frame[r][c] for c in range(3)] for r in range(3)])


def P(n):
    return Vector(joints[n].pos)


def fk(angles):
    """World rotation and position of every joint (rig axes, cm) for rig angles {bone: (bend, side)}."""
    R, pos = {}, {}
    for n in topo:
        p = joints[n].parent
        if p is None:
            R[n], pos[n] = Matrix.Identity(3), P(n)
            continue
        pos[n] = pos[p] + R[p] @ (P(n) - P(p))
        F = R[p] @ M3(frames[n])
        x, y, z = F.col[0], F.col[1], F.col[2]
        bend, side = angles.get(n, (0, 0))
        ys = bf.side_yaw_sign(n, frames[n]) or 0
        D = Matrix.Rotation(math.radians(side * ys), 3, z) @ Matrix.Rotation(math.radians(bend * bf.bend_pitch_sign(n)), 3, -y)
        R[n] = D @ R[p]
    return R, pos


def flight_fk():
    """The wings-spread pose, as BFDino::BuildFlightPose builds it (beak kept level, jaw and crest follow the head)."""
    fp = info_root.get("flight_pose_cm", {})
    T = {k: Vector(v) for k, v in fp.items() if k not in ("jaw", "jaw_end", "crest_end")}
    if "head" in T:
        T["head_end"] = T["head"] + Vector((1, 0, -0.12)).normalized() * (P("head_end") - P("head")).length
    R, pos = {}, {}
    for n in topo:
        p = joints[n].parent
        if p is None:
            R[n], pos[n] = Matrix.Identity(3), P(n)
            continue
        pos[n] = T.get(n, pos[p] + R[p] @ (P(n) - P(p)))
        mc = bf.main_child(joints[n])
        D = Matrix.Identity(3)
        if mc is not None and mc in T:
            d0 = R[p] @ (P(mc) - P(n))
            d1 = T[mc] - pos[n]
            if d0.length > 1e-3 and d1.length > 1e-3:
                D = d0.rotation_difference(d1).to_matrix()
        R[n] = D @ R[p]
    return R, pos


# ------------------------------------------------------------------ scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=glb_path)
arm = next(o for o in scene.objects if o.type == "ARMATURE")
# Only the skinned meshes: the glTF importer also adds an icosphere it uses as a bone display shape.
meshes = [o for o in scene.objects if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
for o in [o for o in scene.objects if o.type == "MESH" and o not in meshes]:
    bpy.data.objects.remove(o)

white = bpy.data.materials.new("Flat")
white.use_nodes = True
nt = white.node_tree
nt.nodes.clear()
em = nt.nodes.new("ShaderNodeEmission")
em.inputs["Color"].default_value = (1, 1, 1, 1)
nt.links.new(em.outputs[0], nt.nodes.new("ShaderNodeOutputMaterial").inputs[0])
no_membrane = {}   # flight views draw the membrane from the posed bones instead (as BFDino::UpdateMembrane does)
for m in meshes:
    idx = [i for i, s in enumerate(m.material_slots) if s.material and "membrane" in s.material.name.lower()]
    if idx:
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(m.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in idx], context="FACES")
        nm = m.data.copy()
        bm.to_mesh(nm)
        bm.free()
        nm.materials.clear()
        no_membrane[m.name] = nm
for me in [m.data for m in meshes] + list(no_membrane.values()):
    me.materials.clear()
    me.materials.append(white)
    for p in me.polygons:
        p.use_smooth = True


def membrane(a, pos, side):
    """A sheet from the leading edge (arm, wing finger) to the trailing edge (hip, knee, ankle, then bowed to the tip)."""
    lead = [n + side for n in ("upperarm", "lowerarm", "hand", "wingfinger_01", "wingfinger_02",
                               "wingfinger_03", "wingfinger_04", "wing_end")]
    lead = [pos[n].copy() for n in lead if n in pos]
    trail = [pos[n + side].copy() for n in ("thigh", "calf", "foot") if n + side in pos]
    tip, ankle = lead[-1], trail[-1]
    for k in range(1, 9):
        t = k / 8
        p = ankle.lerp(tip, t)
        p.x -= math.sin(t * math.pi) * 0.12 * (tip - ankle).length   # trailing edge bows back
        trail.append(p)

    def resample(pts, n):
        d = [0.0]
        for i in range(1, len(pts)):
            d.append(d[-1] + (pts[i] - pts[i - 1]).length)
        out = []
        for k in range(n):
            s = d[-1] * k / (n - 1)
            i = min(max(j for j in range(len(d)) if d[j] <= s + 1e-6), len(pts) - 2)
            u = (s - d[i]) / max(d[i + 1] - d[i], 1e-6)
            out.append(pts[i].lerp(pts[i + 1], u))
        return out
    N, M = 28, 7
    L, T = resample(lead, N), resample(trail, N)
    verts = [to_b_pos(L[i].lerp(T[i], j / (M - 1))) for i in range(N) for j in range(M)]
    faces = [(i * M + j, i * M + j + 1, (i + 1) * M + j + 1, (i + 1) * M + j) for i in range(N - 1) for j in range(M - 1)]
    me = bpy.data.meshes.new("Membrane")
    me.from_pydata(verts, [], faces)
    me.materials.append(white)
    o = bpy.data.objects.new("Membrane", me)
    scene.collection.objects.link(o)
    o.parent = a

S = Matrix.Diagonal((1, -1, 1))   # rig axes (Y right) -> Blender (Y left); glb metres


def to_b_pos(v):
    return (S @ v) / 100.0


def apply_pose(a, R, pos):
    """Sets pose bones so each bone sits at its posed joint, turned by R from its rest orientation."""
    W = a.matrix_world
    Wi = W.inverted()
    want = {}
    for n in topo:
        b = a.data.bones.get(n)
        if b is None:
            continue
        rest = b.matrix_local
        rot = (S @ R[n] @ S).to_4x4()
        Mw = Matrix.Translation(to_b_pos(pos[n])) @ rot @ (W @ rest).to_3x3().normalized().to_4x4()
        want[n] = Wi @ Mw
    for n, M in want.items():
        b = a.data.bones[n]
        pb = a.pose.bones[n]
        if b.parent is None:
            pb.matrix_basis = b.matrix_local.inverted() @ M
        else:
            pr = b.parent.matrix_local
            pp = want.get(b.parent.name, pr)
            pb.matrix_basis = b.matrix_local.inverted() @ pr @ pp.inverted() @ M


# Check the glb rest pose sits on the rig joints
err = max(((arm.matrix_world @ arm.data.bones[n].head_local) - to_b_pos(P(n))).length
          for n in topo if n in arm.data.bones)
print(f"[poses] {key}: rest bone heads within {err * 100:.3f} cm of the rig JSON")

copies = []
for i, (label, view, recipe, tilt) in enumerate(POSES):
    a = arm.copy()
    a.data = arm.data
    scene.collection.objects.link(a)
    for m in meshes:
        mc = m.copy()
        scene.collection.objects.link(mc)
        mc.parent = a
        mc.matrix_parent_inverse = m.matrix_parent_inverse.copy()
        for mod in mc.modifiers:
            if mod.type == "ARMATURE":
                mod.object = a
    R, pos = flight_fk() if view.startswith("flight") else fk(angles_for(recipe, tilt))
    apply_pose(a, R, pos)
    if view.startswith("flight"):
        for c in a.children:
            if c.name.split(".")[0] in no_membrane:
                c.data = no_membrane[c.name.split(".")[0]]
        membrane(a, pos, "_l")
        membrane(a, pos, "_r")
    a.rotation_mode = "XYZ"   # glTF armatures come in as quaternions
    if view == "top":
        a.rotation_euler = Euler((math.radians(90), 0, 0))
    elif view == "flight_top":   # head up the page, wings across
        a.rotation_euler = (Matrix.Rotation(math.radians(-90), 3, "Y") @ Matrix.Rotation(math.radians(90), 3, "X")).to_euler("XYZ")
    elif view == "flight_front":
        a.rotation_euler = Euler((0, 0, math.radians(-90)))
    copies.append((label, view, a))
for o in [arm] + meshes:
    bpy.data.objects.remove(o)

dg = bpy.context.evaluated_depsgraph_get()


def extent(a):
    dg.update()
    pts = []
    for c in a.children:
        ev = c.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        pts += [mw @ v.co for v in me.vertices]
        ev.to_mesh_clear()
    return (min(p.x for p in pts), max(p.x for p in pts), min(p.z for p in pts), max(p.z for p in pts))


bpy.context.view_layer.update()
ext = [extent(a) for _, _, a in copies]
side_w = max(e[1] - e[0] for e, c in zip(ext, copies) if c[1] in ("side", "top"))
cell_w = side_w * 1.08
flight_scale = 1.0
for i, (label, view, a) in enumerate(copies):   # wings spread are ~2x the body: draw flight views smaller
    w = ext[i][1] - ext[i][0]
    if view.startswith("flight") and w > side_w:
        flight_scale = min(flight_scale, side_w / w)
for i, (label, view, a) in enumerate(copies):
    if view.startswith("flight") and flight_scale < 1:
        a.scale = (flight_scale,) * 3
        bpy.context.view_layer.update()
        ext[i] = extent(a)
COLS = 3
label_h = side_w * 0.05
rows = [list(range(i, min(i + COLS, len(copies)))) for i in range(0, len(copies), COLS)]

labels = bpy.data.collections.new("Labels")
scene.collection.children.link(labels)
ink = bpy.data.materials.new("Ink")
ink.use_nodes = True
ink.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0, 0, 0, 1)
grey = bpy.data.materials.new("Grey")
grey.use_nodes = True
grey.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.55, 0.55, 0.55, 1)


def text(s, at, size, align="LEFT"):
    bpy.ops.object.text_add(location=at, rotation=(math.radians(90), 0, 0))
    t = bpy.context.object
    t.data.body = s
    t.data.size = size
    t.data.align_x = align
    t.data.materials.append(ink)
    labels.objects.link(t)
    scene.collection.objects.unlink(t)
    return t


def line(x0, x1, z):
    bpy.ops.mesh.primitive_plane_add(size=1, location=((x0 + x1) / 2, 2, z))
    g = bpy.context.object
    g.scale = (x1 - x0, side_w * 0.0025, 1)
    g.rotation_euler = (math.radians(90), 0, 0)
    g.data.materials.append(grey)
    labels.objects.link(g)
    scene.collection.objects.unlink(g)


top = 0.0
for row in rows:
    h = max(ext[i][3] - ext[i][2] for i in row)
    floor = top - label_h * 1.6 - h
    for col, i in enumerate(row):
        label, view, a = copies[i]
        x0, x1, z0, z1 = ext[i]
        cx = col * (cell_w + side_w * 0.08)
        dx = cx + (cell_w - (x1 - x0)) / 2 - x0
        if view == "side":
            dz = floor - z0
            line(cx, cx + cell_w, floor)
        else:
            dz = floor + (h - (z1 - z0)) / 2 - z0
        a.location += Vector((dx, 0, dz))
        text(label, (cx, 0, top - label_h), label_h * 0.55)
    top = floor - label_h * 1.2

bpy.context.view_layer.update()
xmin, xmax = 0.0, (COLS - 1) * (cell_w + side_w * 0.08) + cell_w
zmin, zmax = top, label_h * 3.2
text(f"{NAME} · KEY POSES", (xmax, 0, label_h * 2.0), label_h * 0.95, "RIGHT")
sub = "Posed with the rig · every angle is a share of that joint's limit · one scale for all poses"
if flight_scale < 1:
    sub += f" (flight views at {flight_scale:.2f}×)"
text(sub, (xmax, 0, label_h * 0.9), label_h * 0.42, "RIGHT")

pad = 0.03 * (xmax - xmin)
cam_data = bpy.data.cameras.new("Cam")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
cam.rotation_euler = (math.radians(90), 0, 0)
w, h = xmax - xmin + 2 * pad, zmax - zmin + 2 * pad
cam.location = ((xmin + xmax) / 2, -100, (zmin + zmax) / 2)
cam_data.ortho_scale = max(w, h)
cam_data.clip_end = 1000
cam_data.sensor_fit = "HORIZONTAL" if w >= h else "VERTICAL"
res = 2400
scene.render.resolution_x = res if w >= h else int(res * w / h)
scene.render.resolution_y = int(res * h / w) if w >= h else res

engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items]
scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in engines else "BLENDER_EEVEE_NEXT"
scene.view_settings.view_transform = "Standard"
world = bpy.data.worlds.new("W")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)
scene.world = world

scene.render.use_freestyle = True
scene.render.line_thickness_mode = "ABSOLUTE"
scene.render.line_thickness = 2.0
fs = scene.view_layers[0].freestyle_settings
fs.crease_angle = math.radians(135)
ls = fs.linesets[0] if fs.linesets else fs.linesets.new("Lines")
ls.select_silhouette = True
ls.select_border = True
ls.select_crease = True
ls.select_by_collection = True
ls.collection = labels
ls.collection_negation = "EXCLUSIVE"
if ls.linestyle is None:
    ls.linestyle = bpy.data.linestyles.new("Ink")
ls.linestyle.color = (0, 0, 0)

scene.render.filepath = out_path
bpy.ops.render.render(write_still=True)
print("[poses] wrote", out_path)
