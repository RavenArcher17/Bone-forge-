"""Draw a body OBJ's turnaround sheet (side, front, back, top, 3/4) as black-on-white line art in Blender.

    blender -b -P turnaround_blender.py -- <body.obj> <out.png> "<TITLE>"

All views are one scale: one orthographic camera looks at five copies of the model, each turned to
show one view. Lines are Freestyle (silhouette, creases, borders) over flat white surfaces.
"""
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
obj_path, out_path, title = argv[0], argv[1], argv[2] if len(argv) > 2 else "TURNAROUND"

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# OBJ: cm, Y up, X forward, Z = creature's right. Blender's importer maps OBJ Y-up to Z-up.
bpy.ops.wm.obj_import(filepath=obj_path, up_axis="Y", forward_axis="NEGATIVE_Z")
parts = [o for o in scene.objects if o.type == "MESH"]
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for p in body.data.polygons:
    p.use_smooth = True

white = bpy.data.materials.new("Flat")
white.use_nodes = True
nt = white.node_tree
nt.nodes.clear()
em = nt.nodes.new("ShaderNodeEmission")
em.inputs["Color"].default_value = (1, 1, 1, 1)
out = nt.nodes.new("ShaderNodeOutputMaterial")
nt.links.new(em.outputs[0], out.inputs[0])
body.data.materials.clear()
body.data.materials.append(white)

bb = [body.matrix_world @ Vector(c) for c in body.bound_box]
L = max(v.x for v in bb) - min(v.x for v in bb)
H = max(v.z for v in bb) - min(v.z for v in bb)
W = max(v.y for v in bb) - min(v.y for v in bb)

# The camera looks along +Y (from -Y). Blender's OBJ import puts the creature's right side at -Y,
# so the plain copy shows the right side with the head to the right of the sheet.
views = [
    ("SIDE", Euler((0, 0, 0)), Vector((0, 0, 0))),
    ("FRONT", Euler((0, 0, math.radians(-90))), Vector((L * 0.5 + W * 0.9, 0, 0))),
    ("BACK", Euler((0, 0, math.radians(90))), Vector((L * 0.5 + W * 2.4, 0, 0))),
    ("TOP", Euler((math.radians(90), 0, 0)), Vector((0, 0, -W * 0.5 - H * 0.35))),
    ("3/4 VIEW (from the front right, slightly above)", Euler((math.radians(18), 0, math.radians(-35))), Vector((0, 0, -W * 1.0 - H * 1.75))),
]
cx = sum(v.x for v in bb) / 8
copies = []
for name, rot, off in views:
    c = body.copy()
    c.data = body.data
    scene.collection.objects.link(c)
    c.rotation_euler = rot
    c.location = off
    c.location.x -= cx if name != "FRONT" and name != "BACK" else 0
    copies.append((name, c))
bpy.data.objects.remove(body)


labels = bpy.data.collections.new("Labels")
scene.collection.children.link(labels)


def text(s, at, size):
    bpy.ops.object.text_add(location=at, rotation=(math.radians(90), 0, 0))
    t = bpy.context.object
    t.data.body = s
    t.data.size = size
    t.data.align_x = "LEFT"
    m = bpy.data.materials.new("Ink")
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0, 0, 0, 1)
    t.data.materials.append(m)
    labels.objects.link(t)
    bpy.context.scene.collection.objects.unlink(t)
    return t


scene.view_layers[0].update()
label = H * 0.2
for name, c in copies:
    bb = [c.matrix_world @ Vector(v) for v in c.bound_box]
    x0, z1 = min(v.x for v in bb), max(v.z for v in bb)
    text(name, (x0, -400, z1 + label * 0.6), label * 0.55)
    if name in ("SIDE", "FRONT", "BACK"):   # ground line
        x1 = max(v.x for v in bb)
        bpy.ops.mesh.primitive_plane_add(size=1, location=((x0 + x1) / 2, 0, -1.5))
        g = bpy.context.object
        g.scale = (x1 - x0 + 60, 3, 1)
        g.rotation_euler = (math.radians(90), 0, 0)
        g.data.materials.append(bpy.data.materials["Ink"])

# Frame everything
objs = [o for o in scene.objects if o.type in ("MESH", "FONT")]  # noqa: labels included
scene.view_layers[0].update()
pts = [o.matrix_world @ Vector(v) for o in objs for v in o.bound_box]
xmin, xmax = min(p.x for p in pts), max(p.x for p in pts)
zmin, zmax = min(p.z for p in pts), max(p.z for p in pts)
pad = 0.06 * (xmax - xmin)
title_h = (zmax - zmin) * 0.07
zmax += title_h
tt = text(title, (xmax, -400, zmax - title_h * 0.65), title_h * 0.45)
tt.data.align_x = "RIGHT"

cam_data = bpy.data.cameras.new("Cam")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
cam.rotation_euler = (math.radians(90), 0, 0)
w, h = xmax - xmin + 2 * pad, zmax - zmin + 2 * pad
cam.location = ((xmin + xmax) / 2, -5000, (zmin + zmax) / 2)
cam_data.ortho_scale = max(w, h)
cam_data.clip_end = 20000
res_x = 2400
scene.render.resolution_x = res_x
scene.render.resolution_y = int(res_x * h / w) if w >= h else res_x
cam_data.sensor_fit = "HORIZONTAL" if w >= h else "VERTICAL"
if w < h:
    scene.render.resolution_x = int(res_x * w / h)

scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE_NEXT"
scene.view_settings.view_transform = "Standard"
world = bpy.data.worlds.new("W")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)
scene.world = world
scene.render.film_transparent = False

scene.render.use_freestyle = True
scene.render.line_thickness_mode = "ABSOLUTE"
scene.render.line_thickness = 2.2
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
print("[turnaround] wrote", out_path)
