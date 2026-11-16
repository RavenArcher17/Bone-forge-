"""Render the Pteranodon in its flight pose as a clay model, wing membranes spread, for looking the model over.

    blender -b -P flight_blender.py -- <out_dir>

Standing, the membranes are folded into thin strips along the arms (as a bat folds its wings), so the standing
clay renders barely show them. In flight the testing ground builds each membrane from the posed bones
(BFDino::UpdateMembrane): arm and wing finger along the front, hip, knee and ankle along the back, the trailing
edge bowed back to the wing tip. This poses unreal/out/ptera_skeletal.glb the same way (flight_pose_cm, as
poses_blender.py does) and writes <out_dir>/ptera_flight_34.png, _below.png and _front.png. Absolute paths.
"""
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "unreal"))
import bone_forge_gltf as bf  # noqa: E402

out_dir = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out_dir, exist_ok=True)
info_root, joints, topo = bf.load_rig(os.path.join(ROOT, "rig_joints", "ptera_rig.json"))
S = Matrix.Diagonal((1, -1, 1))   # rig axes (Y right) -> Blender (Y left); glb metres


def P(n):
    return Vector(joints[n].pos)


def to_b(v):
    return (S @ v) / 100.0


def flight_fk():
    """As poses_blender.flight_fk: the wings-spread pose BFDino::BuildFlightPose builds."""
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
            d0, d1 = R[p] @ (P(mc) - P(n)), T[mc] - pos[n]
            if d0.length > 1e-3 and d1.length > 1e-3:
                D = d0.rotation_difference(d1).to_matrix()
        R[n] = D @ R[p]
    return R, pos


def apply_pose(a, R, pos):
    W = a.matrix_world
    Wi = W.inverted()
    want = {}
    for n in topo:
        b = a.data.bones.get(n)
        if b is None:
            continue
        rot = (S @ R[n] @ S).to_4x4()
        want[n] = Wi @ (Matrix.Translation(to_b(pos[n])) @ rot @ (W @ b.matrix_local).to_3x3().normalized().to_4x4())
    for n, M in want.items():
        b, pb = a.data.bones[n], a.pose.bones[n]
        if b.parent is None:
            pb.matrix_basis = b.matrix_local.inverted() @ M
        else:
            pr = b.parent.matrix_local
            pb.matrix_basis = b.matrix_local.inverted() @ pr @ want.get(b.parent.name, pr).inverted() @ M


def membrane(pos, side, mat):
    """As poses_blender.membrane (and BFDino::UpdateMembrane), a little thick so it shades from both sides."""
    lead = [pos[n + side].copy() for n in ("upperarm", "lowerarm", "hand", "wingfinger_01", "wingfinger_02",
                                           "wingfinger_03", "wingfinger_04", "wing_end") if n + side in pos]
    trail = [pos[n + side].copy() for n in ("thigh", "calf", "foot") if n + side in pos]
    tip, ankle = lead[-1], trail[-1]
    for k in range(1, 9):
        t = k / 8
        p = ankle.lerp(tip, t)
        p.x -= math.sin(t * math.pi) * 0.12 * (tip - ankle).length
        trail.append(p)

    def resample(pts, n):
        d = [0.0]
        for i in range(1, len(pts)):
            d.append(d[-1] + (pts[i] - pts[i - 1]).length)
        out = []
        for k in range(n):
            s = d[-1] * k / (n - 1)
            i = min(max(j for j in range(len(d)) if d[j] <= s + 1e-6), len(pts) - 2)
            out.append(pts[i].lerp(pts[i + 1], (s - d[i]) / max(d[i + 1] - d[i], 1e-6)))
        return out
    N, M = 40, 10
    L, T = resample(lead, N), resample(trail, N)
    verts = [to_b(L[i].lerp(T[i], j / (M - 1))) for i in range(N) for j in range(M)]
    faces = [(i * M + j, i * M + j + 1, (i + 1) * M + j + 1, (i + 1) * M + j) for i in range(N - 1) for j in range(M - 1)]
    me = bpy.data.meshes.new("Membrane" + side)
    me.from_pydata(verts, [], faces)
    me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = True
    o = bpy.data.objects.new("Membrane" + side, me)
    bpy.context.scene.collection.objects.link(o)
    o.modifiers.new("Thick", "SOLIDIFY").thickness = 0.01
    return o


bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "unreal", "out", "ptera_skeletal.glb"))
arm = next(o for o in scene.objects if o.type == "ARMATURE")
meshes = [o for o in scene.objects if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
for o in [o for o in scene.objects if o.type == "MESH" and o not in meshes]:
    bpy.data.objects.remove(o)


def material(name, rgb, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n = m.node_tree.nodes["Principled BSDF"]
    n.inputs["Base Color"].default_value = (*rgb, 1)
    n.inputs["Roughness"].default_value = rough
    return m


clay = material("Clay", (0.52, 0.45, 0.37), 0.65)
skin = material("Membrane", (0.42, 0.30, 0.24), 0.7)
for m in meshes:   # the folded skinned membranes give way to the spread ones built from the bones
    idx = [i for i, s in enumerate(m.material_slots) if s.material and "membrane" in s.material.name.lower()]
    if idx:
        bm = bmesh.new()
        bm.from_mesh(m.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in idx], context="FACES")
        bm.to_mesh(m.data)
        bm.free()
    m.data.materials.clear()
    m.data.materials.append(clay)
    for p in m.data.polygons:
        p.use_smooth = True

R, pos = flight_fk()
apply_pose(arm, R, pos)
wings = [membrane(pos, s, skin) for s in ("_l", "_r")]

sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
sun.data.energy = 4.0
sun.data.angle = math.radians(8)
sun.rotation_euler = (math.radians(35), 0, math.radians(50))
scene.collection.objects.link(sun)
world = bpy.data.worlds.new("W")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.70, 0.78, 0.88, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.8
scene.world = world
engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items]
scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in engines else "BLENDER_EEVEE_NEXT"
scene.eevee.taa_render_samples = 48
scene.render.resolution_x, scene.render.resolution_y = 1200, 900
scene.view_settings.view_transform = "Standard"
cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
cam.data.lens = 50
cam.data.clip_end = 1000
scene.collection.objects.link(cam)
scene.camera = cam

dg = bpy.context.evaluated_depsgraph_get()
pts = []
for o in meshes + wings:
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    pts += [tuple(ev.matrix_world @ v.co) for v in me.vertices]
    ev.to_mesh_clear()
P_ = np.array(pts)[:: max(1, len(pts) // 20000)]


def shoot(label, to_cam, margin=1.08):
    d = Vector(to_cam).normalized()
    right = d.cross(Vector((0, 0, 1))).normalized() * -1 if abs(d.z) < 0.99 else Vector((0, 1, 0))
    upv = right.cross(d).normalized() * -1
    r, u, f = np.array(right), np.array(upv), -np.array(d)
    tx = math.tan(math.atan(36 / 2 / cam.data.lens))
    ty = tx * scene.render.resolution_y / scene.render.resolution_x
    centre = (P_.max(0) + P_.min(0)) / 2
    dist = max(np.ptp(P_ @ r) / tx, np.ptp(P_ @ u) / ty)
    for _ in range(40):
        Q = P_ - (centre - f * dist)
        z = Q @ f
        x, y = (Q @ r) / z, (Q @ u) / z
        centre = centre + r * (x.max() + x.min()) / 2 * dist + u * (y.max() + y.min()) / 2 * dist
        dist *= max((x.max() - x.min()) / 2 / tx, (y.max() - y.min()) / 2 / ty) * margin
    cam.location = Vector(centre - f * dist)
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(out_dir, f"ptera_flight_{label}.png")
    bpy.ops.render.render(write_still=True)
    print("wrote", scene.render.filepath)


# Blender axes: X forward, Y the animal's left, Z up.
shoot("34", (0.55, -0.55, 0.62))
shoot("below", (0.25, 0.0, -1.0))
shoot("front", (1.0, 0.0, 0.08))
