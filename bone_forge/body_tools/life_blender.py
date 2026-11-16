"""Draw a species' life-drawing sheet as black-on-white line art in Blender, from its skinned model.

    blender -b -P life_blender.py -- <species> <out.png>      (trex, trike, raptor, ankylo, brachio, ptera)

Same layout as the design doc's Life Drawings tab: side and front views with the rig's bones drawn faint grey
inside the body, the mouth open (posed with the rig's jaw), and close-ups of the hand and foot from the side and
from below, each close-up with its own scale bar. Everything is drawn from unreal/out/<species>_skeletal.glb, so
the sheet shows the mesh Unreal gets (legs and feet from leg_builder.py). Posing uses poses_blender.py's maths.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "unreal"))
import bone_forge_gltf as bf  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
key, out_path = argv[0], os.path.abspath(argv[1])
rig_path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
glb_path = os.path.join(ROOT, "unreal", "out", f"{key}_skeletal.glb")

info_root, joints, topo = bf.load_rig(rig_path)
frames = bf.bone_frames(joints, topo)
info = {n: joints[n].info for n in topo}
QUAD = key in ("trike", "ankylo", "brachio")
NAME = {"trex": "TYRANNOSAURUS REX", "trike": "TRICERATOPS HORRIDUS", "raptor": "VELOCIRAPTOR MONGOLIENSIS",
        "ankylo": "ANKYLOSAURUS MAGNIVENTRIS", "brachio": "BRACHIOSAURUS ALTITHORAX", "ptera": "PTERANODON"}[key]
SUB = {"trex": "Body over the skeleton · heavy legs on 3 big clawed toes · about 12 m long",
       "trike": "Body over the skeleton · pillar legs on padded, hoofed feet · about 7.9 m long",
       "raptor": "Body over the skeleton · walks on toes III and IV, sickle claw held up · about 2 m long",
       "ankylo": "Body over the skeleton · short thick legs on hoofed pads · about 7 m long",
       "brachio": "Body over the skeleton · column legs, elephant feet, horseshoe hands · about 22 m long",
       "ptera": "Body over the skeleton"}[key]
# (hand caption, foot side caption, foot from below caption)
NOTES = {
    "trex": ("hand: two hooked claws", "stands on its toes, ankle high off the ground; dewclaw off the ground at the back",
             "3 thick padded toes with claws on a broad pad, like the footprint (R5)"),
    "raptor": ("hand: 3 long fingers with curved claws (wing feathers not modelled)",
               "walks on toes III and IV; sickle claw on toe II held up; small dewclaw",
               "2 toes touch the ground; toe II raised"),
    "trike": ("hand: 5 fingers, I to III end in hooves", "thick pillar leg on a soft heel pad",
              "4 hoofed toes on a big rounded pad"),
    "ankylo": ("hand: short and hoof-like, 5 fingers (partly fiction)", "short, thick leg on a heel pad",
               "3 hoofed toes on a wide pad"),
    "brachio": ("hand: tall column, no pad behind, one thumb claw", "elephant-like foot on a thick heel pad",
                "claws on the 3 inner toes"),
    "ptera": ("hand", "foot", "foot from below"),
}[key]


# ------------------------------------------------------------------ rig maths (as poses_blender.py)
def limit(n, axis):
    lim = info[n].get("limits_deg") or {}
    return lim.get(axis) or [0, 0]


def share_deg(n, axis, s):
    lo, hi = limit(n, axis)
    return s * hi if s >= 0 else -s * lo


def M3(frame):
    return Matrix([[frame[r][c] for c in range(3)] for r in range(3)])


def P(n):
    return Vector(joints[n].pos)


def fk(angles):
    R, pos = {}, {}
    for n in topo:
        p = joints[n].parent
        if p is None:
            R[n], pos[n] = Matrix.Identity(3), P(n)
            continue
        pos[n] = pos[p] + R[p] @ (P(n) - P(p))
        F = R[p] @ M3(frames[n])
        y, z = F.col[1], F.col[2]
        bend, side = angles.get(n, (0, 0))
        ys = bf.side_yaw_sign(n, frames[n]) or 0
        D = Matrix.Rotation(math.radians(side * ys), 3, z) @ Matrix.Rotation(math.radians(bend * bf.bend_pitch_sign(n)), 3, -y)
        R[n] = D @ R[p]
    return R, pos


S = Matrix.Diagonal((1, -1, 1))   # rig axes (Y right) -> Blender (Y left); glb metres


def to_b(v):
    return (S @ v) / 100.0


# ------------------------------------------------------------------ scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=glb_path)
arm = next(o for o in scene.objects if o.type == "ARMATURE")
meshes = [o for o in scene.objects if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
for o in [o for o in scene.objects if o.type == "MESH" and o not in meshes]:
    bpy.data.objects.remove(o)


def material(name, rgb):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (*rgb, 1)
    nt.links.new(em.outputs[0], nt.nodes.new("ShaderNodeOutputMaterial").inputs[0])
    return m


white, ink, grey = material("Flat", (1, 1, 1)), material("Ink", (0.05, 0.05, 0.05)), material("Grey", (0.62, 0.62, 0.62))
for m in meshes:
    m.data.materials.clear()
    m.data.materials.append(white)
    for p in m.data.polygons:
        p.use_smooth = True

body_col = bpy.data.collections.new("Body")
bones_col = bpy.data.collections.new("Bones")
labels = bpy.data.collections.new("Labels")
for c in (body_col, bones_col, labels):
    scene.collection.children.link(c)


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
        b = a.data.bones[n]
        pb = a.pose.bones[n]
        if b.parent is None:
            pb.matrix_basis = b.matrix_local.inverted() @ M
        else:
            pr = b.parent.matrix_local
            pb.matrix_basis = b.matrix_local.inverted() @ pr @ want.get(b.parent.name, pr).inverted() @ M


def inside(p, box):
    return box[0] <= p.x <= box[1] and box[2] <= p.y <= box[3] and box[4] <= p.z <= box[5]


def panel(angles=None, box_cm=None, bones=False):
    """A frozen copy of the model (posed, optionally cut to a rig-space box) under an empty at the origin."""
    a = arm.copy()
    a.data = arm.data
    scene.collection.objects.link(a)
    kids = []
    for m in meshes:
        mc = m.copy()
        scene.collection.objects.link(mc)
        mc.parent = a
        mc.matrix_parent_inverse = m.matrix_parent_inverse.copy()
        for mod in mc.modifiers:
            if mod.type == "ARMATURE":
                mod.object = a
        kids.append(mc)
    R, pos = fk(angles or {})
    apply_pose(a, R, pos)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    root = bpy.data.objects.new("Panel", None)
    scene.collection.objects.link(root)
    box = None
    if box_cm:   # rig cm box -> Blender metres (Y flips)
        box = (box_cm[0] / 100, box_cm[1] / 100, -box_cm[3] / 100, -box_cm[2] / 100, box_cm[4] / 100, box_cm[5] / 100)
    for mc in kids:
        ev = mc.evaluated_get(dg)
        me = bpy.data.meshes.new_from_object(ev)
        me.transform(mc.matrix_world)
        if box:
            bm = bmesh.new()
            bm.from_mesh(me)
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if not inside(v.co, box)], context="VERTS")
            bm.to_mesh(me)
            bm.free()
        if len(me.vertices) == 0:
            continue
        o = bpy.data.objects.new("Part", me)
        body_col.objects.link(o)
        o.parent = root
    if bones:
        root_len = max(P(n).x for n in topo) - min(P(n).x for n in topo)
        r = root_len * 0.0045 / 100
        bm = bmesh.new()
        for n in topo:
            p = joints[n].parent
            if p is None or joints[p].parent is None:
                continue
            A, B = to_b(pos[p]), to_b(pos[n])
            d = B - A
            if d.length < 1e-4:
                continue
            q = Vector((0, 0, 1)).rotation_difference(d.normalized())
            M = Matrix.Translation((A + B) / 2) @ q.to_matrix().to_4x4()
            bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=r, radius2=r, depth=d.length, matrix=M)
            bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=r * 1.9, matrix=Matrix.Translation(B))
        me = bpy.data.meshes.new("Bones")
        bm.to_mesh(me)
        bm.free()
        me.materials.append(white)
        o = bpy.data.objects.new("Bones", me)
        bones_col.objects.link(o)
        o.parent = root
    for mc in kids:
        bpy.data.objects.remove(mc)
    bpy.data.objects.remove(a)
    return root, pos


def extent(root):
    bpy.context.view_layer.update()
    pts = []
    for c in root.children:
        if c.users_collection[0] is bones_col:
            continue
        mw = c.matrix_world
        pts += [mw @ v.co for v in c.data.vertices]
    return min(p.x for p in pts), max(p.x for p in pts), min(p.z for p in pts), max(p.z for p in pts)


def turn(root, M):
    root.matrix_world = M.to_4x4() @ root.matrix_world


def place(root, scale, x0, top):
    """Scales the panel and moves its top-left corner to (x0, top). Returns its (width, height)."""
    root.scale = (scale,) * 3
    e = extent(root)
    root.location += Vector((x0 - e[0], 0, top - e[3]))
    return e[1] - e[0], e[3] - e[2]


# ------------------------------------------------------------------ panels
FRONT = Matrix.Rotation(math.radians(-90), 3, "Z")                 # head toward the camera
BELOW = Matrix.Rotation(math.radians(-90), 3, "Y") @ Matrix.Rotation(math.radians(-90), 3, "X")   # sole to camera, toes up

side, rest = panel(bones=True)
front, _ = panel(bones=True)
turn(front, FRONT)

# Mouth open: jaw most of the way, head raised a little, cut to the head.
open_angles = {"jaw": (share_deg("jaw", "bend", 0.9), 0), "head": (share_deg("head", "bend", 0.25), 0)}
_, opos = fk(open_angles)
hp = [opos[n] for n in ("head", "head_end", "jaw", "jaw_end") if n in opos]
hl = (opos["head_end"] - opos["head"]).length
mg = hl * {"trike": 0.75, "ankylo": 0.45}.get(key, 0.3)
hbox = (min(p.x for p in hp) - mg * 0.6, max(p.x for p in hp) + mg, -1e4, 1e4, min(p.z for p in hp) - mg, max(p.z for p in hp) + mg)
mouth, _ = panel(open_angles, hbox)
mouth_front, _ = panel(open_angles, hbox)
turn(mouth_front, FRONT)


def limb_box(upper, low, end, ground_low=True):
    """Box around the right limb's lower end (rig cm): from above the ankle/wrist to past the toe tips."""
    F, E = rest[low + "_r"], rest[end + "_r"]
    U = rest[upper + "_r"]
    reach = max((E - F).length, 0.3 * F.z, 25 * (1 if key != "raptor" else 0.1))
    zmax = F.z + max(0.45 * (U.z - F.z), 0.4 * reach)
    return (min(F.x, E.x) - 1.1 * reach, max(F.x, E.x) + 1.3 * reach, 0.15 * F.y, F.y + 3 * reach,
            -1.0 if ground_low else E.z - reach, zmax)


foot_box = limb_box("calf", "foot", "toe_end")
foot_side, _ = panel(None, foot_box)
foot_below, _ = panel(None, foot_box)
turn(foot_below, BELOW)
if QUAD:
    hand_box = limb_box("lowerarm", "hand", "finger_end")
else:   # the small arm held up under the chest
    H0, H1 = rest["lowerarm_r"], rest["finger_end_r"]
    ln = (H1 - H0).length + (rest["hand_r"] - rest["lowerarm_r"]).length
    hand_box = (H0.x - 0.4 * ln, H1.x + 0.5 * ln, 0.8 * H0.y, H0.y + 3 * ln, H1.z - 0.6 * ln, H0.z + 0.5 * ln)
hand_side, _ = panel(None, hand_box)
hand_below = None
if QUAD:
    hand_below, _ = panel(None, hand_box)
    turn(hand_below, BELOW)
for o in [arm] + meshes:
    bpy.data.objects.remove(o)

# ------------------------------------------------------------------ layout (Blender metres, X right, Z up)
e = extent(side)
SW0 = e[1] - e[0]                         # side view width at scale 1
TS = SW0 * 0.018                          # text size unit


def text(s, at, size, align="LEFT", col=ink):
    bpy.ops.object.text_add(location=at, rotation=(math.radians(90), 0, 0))
    t = bpy.context.object
    t.data.body = s
    t.data.size = size
    t.data.align_x = align
    t.data.materials.append(col)
    labels.objects.link(t)
    scene.collection.objects.unlink(t)


def bar(x0, x1, z, thick, mat):
    bpy.ops.mesh.primitive_plane_add(size=1, location=((x0 + x1) / 2, 2, z))
    g = bpy.context.object
    g.scale = (x1 - x0, thick, 1)
    g.rotation_euler = (math.radians(90), 0, 0)
    g.data.materials.append(mat)
    labels.objects.link(g)
    scene.collection.objects.unlink(g)


def scale_bar(x0, z, scale, target_w):
    """A 0..N cm bar about target_w wide on the sheet, for a panel drawn at `scale`."""
    cm = target_w / scale * 100
    nice = max(n for n in (1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500) if n <= max(cm, 1))
    w = nice / 100 * scale
    bar(x0, x0 + w, z, TS * 0.35, ink)
    for k in range(5):
        if k % 2:
            bar(x0 + w * k / 5, x0 + w * (k + 1) / 5, z, TS * 0.2, white)
    text("0", (x0, 0, z + TS * 0.6), TS * 0.7)
    text(f"{nice} cm", (x0 + w, 0, z + TS * 0.6), TS * 0.7, "RIGHT")


top = 0.0
text(f"{NAME} · LIFE REFERENCE", (0, 0, top), TS * 2.0)
text(SUB, (0, 0, top - TS * 2.2), TS * 1.0, col=grey)
top -= TS * 5.5

# Row 1: side and front, same scale, on one ground line.
text("SIDE VIEW", (0, 0, top), TS * 1.3)
fe = extent(front)
row_top = top - TS * 2.5
sw, sh = place(side, 1.0, 0, row_top)
fw, fh = place(front, 1.0, sw + SW0 * 0.08, row_top - (sh - (fe[3] - fe[2])))
ground = row_top - sh
bar(-SW0 * 0.01, sw + SW0 * 0.01, ground, TS * 0.12, grey)
bar(sw + SW0 * 0.06, sw + SW0 * 0.1 + fw, ground, TS * 0.12, grey)
text("FRONT VIEW", (sw + SW0 * 0.08, 0, top), TS * 1.3)
text("Faint grey = the rig's bones inside the body", (sw + SW0 * 0.1 + fw, 0, ground - TS * 2.2), TS * 0.8, "RIGHT", grey)
SHEET_W = sw + SW0 * 0.1 + fw
top = ground - TS * 5

# Row 2: mouth open, side and front, scaled to about 40% of the sheet width.
text("MOUTH OPEN · jaws and teeth", (0, 0, top), TS * 1.3)
me_ = extent(mouth)
s2 = min(0.42 * SHEET_W / (me_[1] - me_[0]), 0.32 * SHEET_W / (me_[3] - me_[2]))
row_top = top - TS * 3
mw, mh = place(mouth, s2, 0, row_top)
mfw, mfh = place(mouth_front, s2, SHEET_W * 0.58, row_top)
scale_bar(0, row_top - max(mh, mfh) - TS * 2, s2, SHEET_W * 0.12)
top = row_top - max(mh, mfh) - TS * 6

# Row 3: hand and foot close-ups.
text("HANDS AND FEET · claws, pads and toes", (0, 0, top), TS * 1.3)
cells = [(hand_side, NOTES[0])] + ([(hand_below, "hand from below")] if hand_below else []) + \
        [(foot_side, NOTES[1]), (foot_below, NOTES[2])]
cw = SHEET_W / len(cells)
ch = SHEET_W * (0.3 if len(cells) == 3 else 0.26)
row_top = top - TS * 3
bottoms = []
for i, (root, note) in enumerate(cells):
    ex = extent(root)
    s3 = min(0.85 * cw / (ex[1] - ex[0]), ch / (ex[3] - ex[2]))
    w, h = place(root, s3, i * cw, row_top)
    root.location.x += (0.85 * cw - w) / 2
    if root in (foot_side, hand_side) and (root is foot_side or QUAD):
        bar(i * cw, i * cw + 0.85 * cw, row_top - h, TS * 0.12, grey)
    scale_bar(i * cw, row_top - h - TS * 2.2, s3, cw * 0.3)
    words = note.split(" ")
    lines, cur = [], ""
    for w_ in words:     # wrap at about 40 characters
        if len(cur) + len(w_) > 40:
            lines.append(cur)
            cur = w_
        else:
            cur = (cur + " " + w_).strip()
    lines.append(cur)
    for k, ln in enumerate(lines):
        text(ln, (i * cw, 0, row_top - h - TS * (4.4 + 1.3 * k)), TS * 0.85, col=grey)
    bottoms.append(row_top - h - TS * (4.4 + 1.3 * len(lines)))
bottom = min(bottoms) - TS * 2

# ------------------------------------------------------------------ camera, lines, render
pad = SHEET_W * 0.04
xmin, xmax, zmin, zmax = -pad, SHEET_W + pad, bottom - pad, TS * 3 + pad
cam_data = bpy.data.cameras.new("Cam")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
cam.rotation_euler = (math.radians(90), 0, 0)
w, h = xmax - xmin, zmax - zmin
cam.location = ((xmin + xmax) / 2, -100, (zmin + zmax) / 2)
cam_data.ortho_scale = max(w, h)
cam_data.clip_end = 1000
cam_data.sensor_fit = "HORIZONTAL" if w >= h else "VERTICAL"
res = 3000
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
scene.render.line_thickness = 2.4
fs = scene.view_layers[0].freestyle_settings
fs.crease_angle = math.radians(135)
body_ls = fs.linesets[0] if fs.linesets else fs.linesets.new("Body")
body_ls.select_silhouette = True
body_ls.select_border = True
body_ls.select_crease = True
body_ls.select_by_collection = True
body_ls.collection = body_col
body_ls.linestyle = body_ls.linestyle or bpy.data.linestyles.new("Ink")
body_ls.linestyle.color = (0, 0, 0)
bone_ls = fs.linesets.new("Bones")
bone_ls.select_silhouette = True
bone_ls.select_border = False
bone_ls.select_crease = False
bone_ls.select_by_visibility = False      # drawn through the body
bone_ls.select_by_collection = True
bone_ls.collection = bones_col
bone_ls.linestyle = bpy.data.linestyles.new("Bones")
bone_ls.linestyle.color = (0.72, 0.72, 0.72)
bone_ls.linestyle.thickness = 1.2

scene.render.filepath = out_path
bpy.ops.render.render(write_still=True)
print("[life] wrote", out_path)
