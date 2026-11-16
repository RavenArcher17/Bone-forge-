"""Preview a species' skin in Blender: the glb's vertex paint (bone_forge_skin.py) under the colour scheme, with the
scale tiles box-projected and bumped the way the Unreal material does it (bone_forge_materials.py).

    blender -b -P skin_preview_blender.py -- <species> <out_dir> [views]

Reads unreal/out/<species>_skeletal.glb and textures/tiles; writes <out_dir>/<species>_skin_<view>.png for the views
(default 34,side,head; also front, feet). Absolute paths.
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "unreal"))
import bone_forge_skin as skin  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
key, out_dir = argv[0], argv[1]
views = argv[2].split(",") if len(argv) > 2 else ["34", "side", "head"]
S = skin.SKIN[key]
TILES = os.path.join(ROOT, "textures", "tiles")

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "unreal", "out", f"{key}_skeletal.glb"))
meshes = [o for o in scene.objects if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
for o in [o for o in scene.objects if o.type == "MESH" and o not in meshes]:
    bpy.data.objects.remove(o)


def rgb(h):
    return (*[c ** 2.2 for c in skin.hex_rgb(h)], 1.0)       # sRGB hex -> linear


def tile_height(nt, name, vec):
    img = bpy.data.images.load(os.path.join(TILES, f"{name}_height.png"), check_existing=True)
    img.colorspace_settings.name = "Non-Color"
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = img
    t.projection = "BOX"
    t.projection_blend = 0.35
    nt.links.new(vec, t.inputs["Vector"])
    return t.outputs["Color"]


def math_node(nt, op, *ins, clamp=False):
    m = nt.nodes.new("ShaderNodeMath")
    m.operation = op
    m.use_clamp = clamp
    for i, v in enumerate(ins):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            m.inputs[i].default_value = v
        else:
            nt.links.new(v, m.inputs[i])
    return m.outputs[0]


def mix(nt, fac, a, b, color=True):
    m = nt.nodes.new("ShaderNodeMix")
    m.data_type = "RGBA" if color else "FLOAT"
    ins = (m.inputs[6], m.inputs[7]) if color else (m.inputs[2], m.inputs[3])
    nt.links.new(fac, m.inputs[0])
    for i, v in zip(ins, (a, b)):
        if isinstance(v, tuple):
            i.default_value = v
        elif isinstance(v, (int, float)):
            i.default_value = v
        else:
            nt.links.new(v, i)
    return m.outputs[2] if color else m.outputs[0]


def skin_material():
    m = bpy.data.materials.new("Skin")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    attr = nt.nodes.new("ShaderNodeVertexColor")
    attr.layer_name = ""
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(attr.outputs["Color"], sep.inputs[0])
    R, G, B = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    A = attr.outputs["Alpha"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sc = nt.nodes.new("ShaderNodeVectorMath")
    sc.operation = "SCALE"
    sc.inputs["Scale"].default_value = 100.0 / S["tile_cm"]
    nt.links.new(tc.outputs["Object"], sc.inputs[0])
    back_t, belly_t, face_t, feet_t = S["tiles"]
    hb = tile_height(nt, back_t, sc.outputs[0])
    hl = tile_height(nt, belly_t, sc.outputs[0]) if belly_t != back_t else hb
    hf = tile_height(nt, face_t, sc.outputs[0])
    ht = tile_height(nt, feet_t, sc.outputs[0])
    h = mix(nt, R, hb, hl)
    h = mix(nt, math_node(nt, "MULTIPLY", A, 2.0, clamp=True), h, ht)                                  # feet 0.5
    h = mix(nt, math_node(nt, "SUBTRACT", math_node(nt, "MULTIPLY", A, 2.0), 1.0, clamp=True), h, hf)     # face 1
    c = mix(nt, R, rgb(S["back"]), rgb(S["belly"]))
    c = mix(nt, G, c, rgb(S["pattern"]))
    c = mix(nt, B, c, rgb(S["accent"]))
    hv = nt.nodes.new("ShaderNodeRGBToBW")
    nt.links.new(h, hv.inputs[0])
    shade = math_node(nt, "MULTIPLY_ADD", hv.outputs[0], 0.6, 0.4)          # darker in the grooves
    cm = nt.nodes.new("ShaderNodeMix")
    cm.data_type = "RGBA"
    cm.blend_type = "MULTIPLY"
    cm.inputs[0].default_value = 1.0
    nt.links.new(c, cm.inputs[6])
    sh = nt.nodes.new("ShaderNodeCombineColor")
    for i in range(3):
        nt.links.new(shade, sh.inputs[i])
    nt.links.new(sh.outputs[0], cm.inputs[7])
    nt.links.new(cm.outputs[2], bsdf.inputs["Base Color"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Distance"].default_value = S["bump"] / 100.0
    bump.inputs["Strength"].default_value = 1.0
    nt.links.new(hv.outputs[0], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    bsdf.inputs["Roughness"].default_value = 0.72
    return m


def flat_material(name, colour, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = colour
    b.inputs["Roughness"].default_value = rough
    return m


def membrane_material():
    m = flat_material("Membrane", rgb(S["back"]), 0.6)
    nt = m.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sc = nt.nodes.new("ShaderNodeVectorMath")
    sc.operation = "SCALE"
    sc.inputs["Scale"].default_value = 100.0 / 60.0
    nt.links.new(tc.outputs["Object"], sc.inputs[0])
    h = tile_height(nt, S.get("membrane_tile", "ptera_membrane"), sc.outputs[0])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Distance"].default_value = 0.002
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])
    return m


def eye_material():
    """Iris and pupil from the vertex paint (R iris, G pupil), glossy."""
    m = flat_material("Eye", rgb("#1c140c"), 0.08)
    nt = m.node_tree
    attr = nt.nodes.new("ShaderNodeVertexColor")
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(attr.outputs["Color"], sep.inputs[0])
    c = mix(nt, sep.outputs[0], rgb("#2a2018"), rgb(S["iris"]))
    c = mix(nt, sep.outputs[1], c, (0.005, 0.005, 0.005, 1.0))
    nt.links.new(c, nt.nodes["Principled BSDF"].inputs["Base Color"])
    return m


MATS = {"Skin": skin_material(), "Membrane": membrane_material(), "Keratin": flat_material("Keratin", rgb(S["keratin"]), 0.45),
        "Teeth": flat_material("Teeth", rgb("#e8dfc8"), 0.35), "Mouth": flat_material("Mouth", rgb("#9e4a46"), 0.5),
        "Eye": eye_material()}
import bmesh  # noqa: E402
for o in meshes:
    # The blink's half-closed and closed eyelids stay hidden until the game blinks (BFDino).
    hide = [i for i, sl in enumerate(o.material_slots) if sl.material and sl.material.name.split("_")[-1].split(".")[0] in ("LidHalf", "LidClosed")]
    if hide:
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in hide], context="FACES")
        bm.to_mesh(o.data)
        bm.free()
    for slot in o.material_slots:
        if slot.material:
            kind = slot.material.name.split("_")[-1].split(".")[0]
            slot.material = MATS.get(kind, MATS["Skin"])
    for p in o.data.polygons:
        p.use_smooth = True

bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0))
floor = bpy.context.object
floor.scale = (4000, 4000, 1)
floor.data.materials.append(flat_material("Floor", (0.32, 0.32, 0.30, 1), 0.9))
sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
sun.data.energy = 4.0
sun.data.angle = math.radians(6)
sun.rotation_euler = (math.radians(40), 0, math.radians(50))
scene.collection.objects.link(sun)
world = bpy.data.worlds.new("W")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.75, 0.80, 0.88, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.6
scene.world = world
engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items]
scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in engines else "BLENDER_EEVEE_NEXT"
scene.eevee.taa_render_samples = 48
scene.render.resolution_x, scene.render.resolution_y = 1200, 900
scene.view_settings.view_transform = "AgX"
cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
cam.data.lens = 50
cam.data.clip_end = 10000
scene.collection.objects.link(cam)
scene.camera = cam

dg = bpy.context.evaluated_depsgraph_get()
pts, head_pts, feet_pts = [], [], []
for o in meshes:
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    col = me.color_attributes[0] if me.color_attributes else None
    alpha = {}
    if col is not None:
        if col.domain == "POINT":
            alpha = {i: c.color[3] for i, c in enumerate(col.data)}
        else:
            alpha = {lp.vertex_index: col.data[lp.index].color[3] for lp in me.loops}
    # The head is what the head and jaw bones carry (the face region also fades through the ankles and wrists).
    skull = {g.index for g in o.vertex_groups if g.name in ("head", "jaw")}
    for i, v in enumerate(me.vertices):
        q = ev.matrix_world @ v.co
        pts.append(tuple(q))
        a = alpha.get(i, 0.0)
        if sum(g.weight for g in o.data.vertices[i].groups if g.group in skull) > 0.5:
            head_pts.append(tuple(q))
        elif 0.45 < a < 0.55:
            feet_pts.append(tuple(q))
    ev.to_mesh_clear()
ALL = np.array(pts)
HEAD = np.array(head_pts) if len(head_pts) > 50 else ALL[ALL[:, 0] > ALL[:, 0].max() - 0.12 * np.ptp(ALL[:, 0])]
FEET = np.array(feet_pts) if feet_pts else ALL


def shoot(label, P, to_cam, margin=1.08):
    P = P[:: max(1, len(P) // 20000)]
    d = Vector(to_cam).normalized()
    right = d.cross(Vector((0, 0, 1))).normalized() * -1
    upv = right.cross(d).normalized() * -1
    r, u, f = np.array(right), np.array(upv), -np.array(d)
    tx = math.tan(math.atan(36 / 2 / cam.data.lens))
    ty = tx * scene.render.resolution_y / scene.render.resolution_x
    centre = (P.max(0) + P.min(0)) / 2
    dist = max(np.ptp(P @ r) / tx, np.ptp(P @ u) / ty)
    for _ in range(40):
        Q = P - (centre - f * dist)
        z = Q @ f
        x, y = (Q @ r) / z, (Q @ u) / z
        centre = centre + r * (x.max() + x.min()) / 2 * dist + u * (y.max() + y.min()) / 2 * dist
        dist *= max((x.max() - x.min()) / 2 / tx, (y.max() - y.min()) / 2 / ty) * margin
    cam.location = Vector(centre - f * dist)
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(out_dir, f"{key}_skin_{label}.png")
    bpy.ops.render.render(write_still=True)
    print("wrote", scene.render.filepath)


# glTF import: X forward, Z up, the animal's right at -Y (as the OBJ import).
SHOTS = {"34": (ALL, (0.75, -0.62, 0.32), 1.08), "side": (ALL, (0.0, -1.0, 0.06), 1.08),
         "front": (ALL, (1.0, 0.0, 0.10), 1.08), "head": (HEAD, (0.65, -0.70, 0.25), 1.2),
         "face": (HEAD, (0.2, -1.0, 0.12), 1.15),
         "feet": (FEET, (0.55, -0.80, 0.22), 1.25)}
for v in views:
    P, d, m = SHOTS[v]
    shoot(v, P, d, m)
