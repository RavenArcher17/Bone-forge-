"""Render a body OBJ as a shaded clay model from four angles, for looking the model over.

    blender -b -P views_blender.py -- <body.obj> <out_dir> <name> [views]

Writes <out_dir>/<name>_34.png (front right, slightly above), _side.png, _front.png and _feet.png (a low close-up of
the feet on the near side; both feet for the two-legged animals). Use absolute paths: Blender -b drops relative
output paths silently. compose_views.py lays the four out as one sheet. views (comma-separated) picks other shots
instead: head, headside (where the head meets the neck), hands, tail (the tail end from above), limbs (the
near legs whole), chest (the front of the body).
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
obj_path, out_dir, name = argv[0], argv[1], argv[2]
wanted = argv[3].split(",") if len(argv) > 3 else ["34", "side", "front", "feet"]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# OBJ: cm, Y up, X forward, Z = creature's right. In Blender: X forward, Z up, the creature's right at -Y.
bpy.ops.wm.obj_import(filepath=obj_path, up_axis="Y", forward_axis="NEGATIVE_Z")
parts = [o for o in scene.objects if o.type == "MESH"]


def verts(objs):
    return [o.matrix_world @ v.co for o in objs for v in o.data.vertices]


everything = verts(parts)
limbs = [o for o in parts if o.name.startswith(("leg_", "arm_"))]
# A grown body's legs are part of its one skin (body_grown); only the feet and hands are leg_/arm_ parts. So the hip
# height can come from the rig instead (BF_HIP, cm), which also gives a hand-built and a grown body the same shots.
hip = float(os.environ["BF_HIP"]) if os.environ.get("BF_HIP") else max(v.z for v in verts([o for o in limbs if o.name.startswith("leg_")]))
two_legged = not any(o.name.startswith("arm_") for o in limbs) or name.split("_")[0] in ("trex", "raptor")
right = lambda o: "_r_" in o.name + "_"      # leg_r_3, leg_foot_r_2, leg_toes_r_22
near = [o for o in limbs if right(o)] if not two_legged else [o for o in limbs if o.name.startswith("leg_")]
feet = [v for v in verts(near) if v.z < 0.3 * hip]
# The near legs whole: on a grown body their flesh is the skin below the hips on that side (Blender -Y is the right).
limb_pts = verts(near) + [v for v in verts([o for o in parts if o.name.startswith("body_grown")])
                          if v.z < 1.05 * hip and (two_legged or v.y < 0)
                          and (not two_legged or abs(v.x - sum(p.x for p in feet) / max(len(feet), 1)) < 0.6 * hip)]
heads = verts([o for o in parts if o.name.startswith(("head", "jaw", "crest"))])
if heads:
    hx0, hx1 = min(v.x for v in heads), max(v.x for v in heads)
    heads += [v for v in verts([o for o in parts if o.name.startswith("body")]) if v.x > hx0 - 0.9 * (hx1 - hx0)]
hands = verts([o for o in parts if o.name.startswith("arm_")])
tail_x = min(v.x for v in everything)
tail = [v for v in everything if v.x < tail_x + 0.12 * (max(v.x for v in everything) - tail_x)]
hx_ = max(v.x for v in everything)
chest = [v for v in verts([o for o in parts if o.name.startswith(("body", "neck"))]) if hx_ - 0.36 * (hx_ - tail_x) < v.x < hx_ - 0.16 * (hx_ - tail_x)]

clay = bpy.data.materials.new("Clay")
clay.use_nodes = True
bsdf = clay.node_tree.nodes["Principled BSDF"]
bsdf.inputs["Base Color"].default_value = (0.52, 0.45, 0.37, 1)
bsdf.inputs["Roughness"].default_value = 0.65
dark = bpy.data.materials.new("Teeth")
dark.use_nodes = True
dark.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.85, 0.83, 0.78, 1)
for o in parts:
    o.data.materials.clear()
    o.data.materials.append(dark if o.name.startswith(("teeth", "horn", "armor_spikes")) else clay)
    for p in o.data.polygons:
        p.use_smooth = True

bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0))
floor = bpy.context.object
floor.scale = (400000, 400000, 1)
fm = bpy.data.materials.new("Floor")
fm.use_nodes = True
fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.50, 0.50, 0.48, 1)
fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
floor.data.materials.append(fm)

sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
sun.data.energy = 4.5
sun.data.angle = math.radians(8)
sun.rotation_euler = (math.radians(40), 0, math.radians(50))    # from the front right, above
scene.collection.objects.link(sun)
world = bpy.data.worlds.new("W")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.78, 0.82, 0.88, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.45
scene.world = world

engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items]
scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in engines else "BLENDER_EEVEE_NEXT"
scene.eevee.taa_render_samples = 48
scene.render.resolution_x, scene.render.resolution_y = 1200, 900
scene.view_settings.view_transform = "Standard"

cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
cam.data.lens = 50
cam.data.clip_end = 1000000
scene.collection.objects.link(cam)
scene.camera = cam


def shoot(label, pts, to_cam, margin=1.08):
    """Aim along -to_cam at the points and move the camera until their perspective projection fills the frame."""
    d = Vector(to_cam).normalized()
    up = Vector((0, 0, 1))
    right = d.cross(up).normalized() * -1 if abs(d.z) < 0.99 else Vector((1, 0, 0))
    upv = right.cross(d).normalized() * -1
    P = np.array([tuple(p) for p in pts])
    P = P[:: max(1, len(P) // 20000)]
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
    scene.render.filepath = os.path.join(out_dir, f"{name}_{label}.png")
    bpy.ops.render.render(write_still=True)
    print("wrote", scene.render.filepath)


# Camera directions (from the animal to the camera): X forward, -Y = the animal's right, Z up.
SHOTS = {"34": (everything, (0.75, -0.62, 0.32), 1.08), "side": (everything, (0.0, -1.0, 0.06), 1.08),
         "front": (everything, (1.0, 0.0, 0.10), 1.08), "feet": (feet, (0.55, -0.80, 0.22), 1.25),
         "head": (heads, (0.65, -0.70, 0.25), 1.15), "headside": (heads, (0.0, -1.0, 0.05), 1.15),
         "hands": (hands, (0.70, -0.65, 0.10), 1.2), "tail": (tail, (-0.45, -0.45, 0.75), 1.2),
         "limbs": (limb_pts, (0.35, -0.90, 0.15), 1.1), "chest": (chest, (0.55, -0.75, 0.2), 1.1)}
# A close look at one spot (BF_FOCUS="x,y,z,radius" in Unreal cm; BF_DIR="x,y,z", Blender axes, from it to the
# camera): the skin within the radius of it, e.g. a knee.
if os.environ.get("BF_FOCUS"):
    fx, fy, fz, fr = (float(c) for c in os.environ["BF_FOCUS"].split(","))
    fp = Vector((fx, -fy, fz))                   # Unreal (X fwd, Y right, Z up) -> Blender (the right at -Y)
    fd = tuple(float(c) for c in os.environ.get("BF_DIR", "0.35,-0.9,0.15").split(","))
    SHOTS["focus"] = ([v for v in everything if (v - fp).length < fr], fd, 1.05)
for label in wanted:
    pts, direction, margin = SHOTS[label]
    shoot(label, pts, direction, margin)
