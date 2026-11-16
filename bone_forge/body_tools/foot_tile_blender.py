"""Make the theropod foot skin tile: textures/tiles/theropod_foot_height.png and _normal.png, 1024 x 1024, seamless.

Raven, 2026-11-03: the T. rex feet "look man made and don't blend well". foot_scutes is a regular brick grid, and the
skin (no UVs) lays it across the feet from fixed directions, so its rows ignore the toes and the sides and soles are
bricks too. Bird and crocodile feet are covered in rounded, domed scales of mixed sizes with a few big ones among
them; this draws those as a jittered Voronoi pattern (a power diagram, so the scales vary in size) with a dome and a
height per scale, so it reads as skin from any side and sits close to the body's pebble scales at the ankle.

    blender -b -P foot_tile_blender.py -- <tiles_dir>
"""
import os
import sys

import bpy
import numpy as np

out_dir = sys.argv[sys.argv.index("--") + 1]
N, G = 1024, 18                      # pixels; scales across the tile (the body's pebbles are about 35 across)
rng = np.random.default_rng(11)
jit = rng.uniform(0.12, 0.88, (G, G, 2))
weight = rng.uniform(-0.10, 0.10, (G, G))
weight[rng.uniform(size=(G, G)) < 0.14] = 0.28            # a few big scales; their neighbours shrink around them
lift = rng.uniform(0.80, 1.0, (G, G))

ys, xs = np.mgrid[0:N, 0:N].astype(np.float64) * (G / N)
cx, cy = np.floor(xs).astype(int), np.floor(ys).astype(int)
d1 = np.full((N, N), np.inf)
d2 = np.full((N, N), np.inf)
r1 = np.zeros((N, N))
own = np.zeros((N, N), dtype=int)
p1 = np.zeros((2, N, N))         # the nearest and second-nearest scale centres
p2 = np.zeros((2, N, N))
for oy in (-2, -1, 0, 1, 2):          # 5 x 5: a big scale can reach past the next cell
    for ox in (-2, -1, 0, 1, 2):
        gx, gy = (cx + ox) % G, (cy + oy) % G                # wrap: the tile repeats seamlessly
        px, py = cx + ox + jit[gy, gx, 0], cy + oy + jit[gy, gx, 1]
        dist2 = ((xs - px) * 0.9) ** 2 + (ys - py) ** 2       # a little wider than tall
        d = dist2 - weight[gy, gx]
        first = d < d1
        second = ~first & (d < d2)
        p = np.stack([px, py])
        p2 = np.where(first, p1, np.where(second, p, p2))
        p1 = np.where(first, p, p1)
        d2 = np.where(first, d1, np.minimum(d2, d))
        r1 = np.where(first, np.sqrt(dist2), r1)
        own = np.where(first, gy * G + gx, own)
        d1 = np.where(first, d, d1)

# Distance to the border with the next scale (the power diagram's bisector): the difference of the two squared
# distances over twice the gap between their centres. Without the gap two close centres smear a groove into a scale.
gap = np.sqrt(((p1[0] - p2[0]) * 0.9) ** 2 + (p1[1] - p2[1]) ** 2)
edge = np.clip((d2 - d1) / (2 * np.maximum(gap, 1e-3)) / 0.07, 0.0, 1.0)     # 0 in the groove, 1 inside a scale
edge = edge * edge * (3 - 2 * edge)
size = np.sqrt(np.maximum(0.25 + weight.ravel()[own], 0.05))
dome = 1.0 - 0.5 * np.clip(r1 / (size * 1.25), 0.0, 1.0) ** 2
u, v = np.mgrid[0:N, 0:N] * (2 * np.pi / N)
wrinkle = 0.025 * (np.sin(37 * u + 3 * v) + np.sin(29 * v - 5 * u + 1.3))    # fine skin texture, periodic
h = edge ** 0.6 * dome * lift.ravel()[own] + wrinkle * edge
h = (h - h.min()) / (h.max() - h.min())


def save(name, rgb):
    img = bpy.data.images.new(name, N, N, alpha=False)
    img.colorspace_settings.name = "Non-Color"
    px = np.ones((N, N, 4), dtype=np.float32)
    px[..., :3] = np.flipud(rgb)                              # Blender's pixel rows run bottom to top
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = os.path.join(out_dir, f"{name}.png")
    img.file_format = "PNG"
    img.save()
    print("wrote", img.filepath_raw)


save("theropod_foot_height", np.repeat(h[..., None], 3, axis=2))
k = 6.0                                                       # normal map, OpenGL style (green up), like the others
dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * k
dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * k
n = np.stack([-dx, dy, np.ones_like(h)], axis=2)
n /= np.linalg.norm(n, axis=2, keepdims=True)
save("theropod_foot_normal", n * 0.5 + 0.5)
