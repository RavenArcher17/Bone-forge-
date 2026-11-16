"""Rebuilds the walking legs and feet of a body OBJ from its rig joints, with real muscle and real toes.

Version 1 legs were too thin to carry their animals (the T. rex shin was 14 cm deep under 12 tonnes) and
ended in one flat paddle. Here every leg follows its rig joints (hip, knee, ankle, ball) with:
  - a deep thigh that starts inside the body and bulges out past the flank (the drumstick);
  - a calf muscle behind the upper shin, tapering to a bony ankle and metatarsus;
  - species feet from the design doc's Life Drawings and Mouths and Feet tabs: three padded clawed toes and a
    dewclaw (T. rex); toes III and IV, the raised sickle claw and a dewclaw (raptor). The big plant-eaters stand on
    pillar feet (Raven's reference pictures, 2026-11-03): the leg runs straight down and flares into a round sole,
    short toes swell out of its front rim, each ending in a broad nail or hoof - 4 hoofed toes and 5 fingers
    (I to III hoofed) on the Triceratops, 3 and 5 on the Ankylosaurus, elephant feet with 5 nailed toes (the inner
    3 longer and blunt) and a nailed column hand with one thumb claw on the Brachiosaurus.
Quadrupeds get the same for the front legs (arm_l, arm_r). Arms that don't walk (T. rex, raptor) and the
Pteranodon (wings, slim legs) are left alone. Every other part of the OBJ is copied as it is.

Sizes are half front-to-back and half side-to-side in cm at ten stations: top (inside the body), hip, 40%
and 78% down the thigh, knee, 28% and 62% down the shin, ankle, middle of the metatarsus, ball.

    python leg_builder.py [species ...]       (default: trex trike raptor ankylo brachio)

Reads and rewrites ../body_shapes/<species>_body.obj and its _sections.csv leg/arm rows. Run it after
trike_body_v2.py, which writes the older Triceratops legs.
"""
import csv
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
from trike_body_v2 import add, sub, mul, dot, norm, lerp, tube, blob, horn, to_obj  # noqa: E402

ROOT = os.path.dirname(HERE)

# (fb, side) half sizes at the ten stations, cm.
HIND = {
    "trex": [(64, 38), (76, 48), (80, 52), (64, 44), (46, 36), (50, 35), (38, 29), (29, 25), (25, 23), (26, 26)],
    "raptor": [(6.5, 3.8), (7.5, 4.6), (7.8, 4.8), (6.0, 4.0), (4.0, 3.3), (4.4, 3.2), (3.0, 2.4), (2.2, 1.9), (1.8, 1.7), (1.9, 2.0)],
    "trike": [(46, 32), (52, 38), (52, 40), (44, 38), (36, 36), (36, 34), (30, 31), (27, 29), (26, 28), (28, 30)],
    "ankylo": [(36, 26), (40, 30), (40, 30), (34, 28), (27, 25), (27, 24), (22, 21), (19, 19), (18, 18), (20, 20)],
    "brachio": [(70, 50), (80, 58), (76, 56), (64, 52), (56, 48), (54, 46), (48, 42), (44, 40), (44, 40), (48, 44)],
}
FORE = {
    "trike": [(30, 24), (34, 28), (32, 27), (28, 25), (25, 24), (24, 23), (21, 21), (19, 20), (19, 20), (21, 22)],
    "ankylo": [(28, 20), (31, 24), (30, 23), (26, 21), (22, 20), (21, 19), (18, 17), (16, 16), (16, 16), (18, 18)],
    "brachio": [(60, 42), (66, 48), (62, 46), (54, 44), (48, 42), (46, 40), (42, 38), (40, 37), (40, 37), (42, 40)],
}
# Feet scaled up about the point under the ball, so they stay on the ground (Raven 2026-11-03: the T. rex
# stands on two feet under 12 tonnes).
FOOT_SCALE = {"trex": 1.5}
# Rings turn over this many samples either side of a knee or elbow, so a sharp bend doesn't fold the tube into a
# jagged crease (Raven's review renders, 2026-11-03).
TURN_SPAN = 9
FEET = {"trex": ("theropod", None), "raptor": ("raptor", None), "trike": ("hoof4", "hoof5"),
        "ankylo": ("hoof3", "hoof5"), "brachio": ("sauropod_pes", "sauropod_manus")}


def turn(d, sgn, deg):
    """Horizontal direction d turned deg degrees outward (toward the creature's side sgn)."""
    a = math.radians(deg) * sgn
    c, s = math.cos(a), math.sin(a)
    return norm((d[0] * c - d[1] * s, d[0] * s + d[1] * c, 0.0))


def flat(v):
    return norm((v[0], v[1], 0.0))


def leg_tube(A, K, F, B, sizes, H, sgn):
    """The leg from inside the body down to the ball, following hip A, knee K, ankle F and ball B."""
    top = (A[0] - 0.03 * H, A[1] * 0.7, A[2] + 0.06 * H)
    back = flat(sub(A, K)) if abs(A[0] - K[0]) > 1 else (-1.0, 0.0, 0.0)
    calf = add(lerp(K, F, 0.28), mul((-1.0, 0.0, 0.0), 0.035 * H))   # the calf muscle sits behind the shin
    pts = [top, A, lerp(A, K, 0.4), lerp(A, K, 0.78), K, calf, lerp(K, F, 0.62), F, lerp(F, B, 0.5), B]
    return ease_knee(tube(pts, sizes, sides=24, per_seg=5, turn_span=TURN_SPAN), K, A, F, 24, max(sizes[4]))


def ease_knee(P, K, A, F, sides, radius):
    """Unfold the inside of a knee or elbow. There the tube's rings are thicker than the bend is tight, so they cross:
    the back of the thigh runs on past the top of the shin and folds back, a shelf that bending the joint closes into
    a ledge (Raven, 2026-11-06). Each line of the tube on the inside of the bend is relaxed along its own length, near
    the joint only, into one smooth hollow. The outside of the joint, the hip and the ankle stay as they were."""
    K, A, F = np.array(K), np.array(A), np.array(F)
    up, down = A - K, F - K
    inner = up / np.linalg.norm(up) + down / np.linalg.norm(down)
    if np.linalg.norm(inner) < 0.08:         # a straight leg has no inside
        return P
    inner /= np.linalg.norm(inner)
    V = np.array(P.v, dtype=float)
    rings = (len(V) - 2) // sides            # the two last vertices are the end caps' centres
    G = V[:rings * sides].reshape(rings, sides, 3)
    d = G - K
    g = np.maximum(np.linalg.norm(d, axis=2), 1e-6)

    def step(e0, e1, x):
        t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
        return t * t * (3 - 2 * t)
    m = (1.0 - step(0.9 * radius, 1.9 * radius, g)) * step(-0.1, 0.45, (d @ inner) / g)
    m = m[..., None]
    for _ in range(90):
        along = 0.5 * (G[:-2] + G[2:]) - G[1:-1]
        G[1:-1] += 0.7 * m[1:-1] * along
    for _ in range(4):                       # and round the ring a little, so neighbouring lines stay together
        G += 0.3 * m * (0.5 * (np.roll(G, 1, axis=1) + np.roll(G, -1, axis=1)) - G)
    V[:rings * sides] = G.reshape(-1, 3)
    P.v = [tuple(float(c) for c in v) for v in V]
    return P


def tag(piece, kind):
    """Mark a piece as a "toe" or a "claw" (a claw, hoof or nail): digits() files them apart from the leg's skin."""
    piece.kind = kind
    return piece


def digits(word, side, pieces):
    """A limb's pieces as separate parts: word_side (the flesh), its toes or fingers, and its claws. They aren't skin:
    bone_forge_skin gives them keratin (Raven, 2026-11-06)."""
    toes = "toes" if word == "leg" else "fingers"
    out = {f"{word}_{side}": [P for P in pieces if getattr(P, "kind", None) is None],
           f"{word}_{toes}_{side}": [P for P in pieces if getattr(P, "kind", None) == "toe"],
           f"{word}_claws_{side}": [P for P in pieces if getattr(P, "kind", None) == "claw"]}
    return {k: v for k, v in out.items() if v}


def toe(B, d, length, rv, rw, sgn, claw=0.0, hoof=False, ground=0.0):
    """A toe lying on the ground from the ball along d, its sole at z = ground, with a claw or hoof at the tip."""
    base = add(B, mul(d, 0.15 * length))
    p0 = (base[0], base[1], max(B[2], rv + ground))
    p1 = add(B, mul(d, 0.45 * length))
    p1 = (p1[0], p1[1], ground + rv * 0.95)
    p2 = add(B, mul(d, 0.8 * length))
    p2 = (p2[0], p2[1], ground + rv * 0.8)
    tip = add(B, mul(d, length))
    tip = (tip[0], tip[1], ground + rv * 0.65)
    pieces = [tag(tube([B, p0, p1, p2, tip], [(rv * 1.1, rw * 1.1), (rv, rw), (rv * 0.9, rw * 0.92), (rv * 0.8, rw * 0.8), (rv * 0.62, rw * 0.62)],
                       sides=28, per_seg=8), "toe")]
    if hoof:
        c = add(tip, mul(d, rv * 0.35))
        pieces.append(tag(blob((c[0], c[1], ground + rv * 0.55), (rv * 0.75, rw * 0.85, rv * 0.6), flat_bottom=ground, lat=16, lon=28), "claw"))
    elif claw > 0:
        cb = add(tip, mul(d, -0.2 * rv))
        cb = (cb[0], cb[1], cb[2] + 0.1 * rv)
        ct = add(tip, mul(d, claw))
        ct = (ct[0], ct[1], ground + 0.4)
        pieces.append(tag(horn(cb, ct, (0.0, 0.0, 0.25 * claw), rv * 0.55, sides=10, steps=10), "claw"))
    return pieces


def dewclaw(F, B, d, L, m, sgn):
    """Digit I: a small clawed toe high on the inside back of the metatarsus, off the ground."""
    hb = add(add(lerp(F, B, 0.78), (0.0, -sgn * m * 0.35, 0.0)), mul(d, -0.5 * m))
    hd = norm(add(mul(d, -0.75), (0.0, -sgn * 0.15, -0.6)))
    ht = add(hb, mul(hd, 0.28 * L))
    return [tag(tube([hb, ht], [(m * 0.3, m * 0.3), (m * 0.2, m * 0.2)], sides=10, per_seg=3), "toe"),
            tag(horn(ht, add(ht, mul(hd, 0.14 * L)), (0.0, 0.0, -0.03 * L), m * 0.18, sides=8, steps=8), "claw")]


def foot(kind, F, B, E, meta, sgn):
    """Toes, claws and pads for one foot. F ankle (wrist), B ball, E toe end, meta = (fb, side) at the ball."""
    d = flat(sub(E, B)) if math.hypot(E[0] - B[0], E[1] - B[1]) > 1 else (1.0, 0.0, 0.0)
    L = max(math.hypot(E[0] - B[0], E[1] - B[1]), 1.0)
    m = 0.5 * (meta[0] + meta[1])
    out = []
    if kind == "theropod":
        # Digits II (inner), III (middle, longest), IV (outer); the small first digit (dewclaw) sits high on
        # the inside of the metatarsus, off the ground. Toes spread about 25 degrees each side.
        for deg, k in ((-12, 0.8), (6, 1.0), (28, 0.86)):   # the inner toe points forward so the feet stay apart
            out += toe(B, turn(d, sgn, deg), L * k + 0.3 * m, m * 0.6, m * 0.52, sgn, claw=0.3 * L)
        out += dewclaw(F, B, d, L, m, sgn)
        out.append(blob((B[0] + 0.2 * m, B[1], m * 0.6), (m * 1.45, m * 0.85, m * 0.6), flat_bottom=0.0, lat=24, lon=48))   # fine: the toes' colour ends along it
    elif kind == "raptor":
        # Digits III and IV carry the weight; II is held up off the ground with its big sickle claw; a small
        # dewclaw (digit I) sits on the inside of the metatarsus.
        for deg, k in ((0, 1.0), (16, 0.9)):
            out += toe(B, turn(d, sgn, deg), L * k, m * 0.5, m * 0.55, sgn, claw=0.3 * L)
        sd = turn(d, sgn, -14)
        s0 = add(B, (0.0, -sgn * m * 0.5, 0.3 * m))
        s1 = add(add(s0, mul(sd, 0.35 * L)), (0.0, 0.0, 0.32 * L))
        out.append(tag(tube([s0, s1], [(m * 0.5, m * 0.5), (m * 0.38, m * 0.4)], sides=12, per_seg=3), "toe"))
        tip = add(add(s1, mul(sd, 0.55 * L)), (0.0, 0.0, -0.2 * L))
        out.append(tag(horn(s1, tip, (0.0, 0.0, 0.22 * L), m * 0.42, sides=10, steps=12), "claw"))
        out += dewclaw(F, B, d, L, m, sgn)
        out.append(blob((B[0], B[1], m * 0.55), (m * 1.2, m * 1.1, m * 0.55), flat_bottom=0.0, lat=24, lon=48))
    return out


# ---------------------------------------------------------------- pillar feet (hoofed and sauropod)
# Raven 2026-11-03: the Triceratops toes looked human (a row of separate round toes on a pad). Big plant-eaters
# stand on a column that flares into a round foot, with short toes that barely part, each ending in a broad
# nail or hoof (his reference pictures: a rhino-like foot drawing and elephant feet). The leg tube itself now
# runs down to the ground and its last ring is the sole; the toes swell out of the front of the rim.
PILLAR = {
    # kind: (footprint half sizes in units of the foot's thickness m, toe angles, toe sizes, what each toe ends in)
    "hoof3": ((1.25, 1.25), (-30, 0, 30), (0.9, 1.0, 0.9), "nnn"),
    "hoof4": ((1.3, 1.22), (-38, -13, 12, 37), (0.9, 1.0, 1.0, 0.85), "nnnn"),
    "hoof5": ((1.15, 1.2), (-52, -26, 0, 26, 52), (0.95, 1.0, 1.0, 0.75, 0.6), "nnn.."),     # fingers I to III hoofed
    "sauropod_pes": ((1.35, 1.3), (-42, -16, 10, 36, 62), (1.05, 1.05, 1.0, 0.8, 0.65), "cccnn"),
    "sauropod_manus": ((1.1, 1.05), (-60, -30, 0, 30, 60), (0.7, 0.75, 0.75, 0.75, 0.7), "nnnnn"),
}


def oriented_blob(center, u, half, flat_bottom=None, lat=16, lon=32, exponent=2.4):
    """blob() with its first axis along the horizontal direction u."""
    P = blob((0.0, 0.0, center[2]), half, flat_bottom=flat_bottom, lat=lat, lon=lon, exponent=exponent)
    c, s = u[0], u[1]
    P.v = [(center[0] + x * c - y * s, center[1] + x * s + y * c, z) for x, y, z in P.v]
    return P


def rim(u, half):
    """Distance from the footprint's centre to its (elliptical) rim along the horizontal direction u."""
    return 1.0 / math.sqrt((u[0] / half[0]) ** 2 + (u[1] / half[1]) ** 2)


def nail_toe(c, u, r, m, end="n"):
    """A short toe swelling out of the rim of a pillar foot (r along u from the footprint centre c), ending in a
    broad nail or hoof (n), a longer blunt claw (c) or nothing (.). m = the toe's size."""
    def at(t, z):
        return (c[0] + u[0] * (r + t * m), c[1] + u[1] * (r + t * m), z * m)
    # The toe is a tall lobe mostly inside the foot, so the front of the foot comes down into it; the nail sits
    # at its bottom front and stands a little proud of it.
    out = [tag(oriented_blob(at(-0.33, 0.30), u, (0.40 * m, 0.33 * m, 0.30 * m), flat_bottom=0.0), "toe")]
    if end == "n":
        out.append(tag(oriented_blob(at(0.04, 0.20), u, (0.13 * m, 0.32 * m, 0.22 * m), flat_bottom=0.0, exponent=2.6), "claw"))
    elif end == "c":
        out.append(tag(oriented_blob(at(0.13, 0.24), u, (0.24 * m, 0.24 * m, 0.25 * m), flat_bottom=0.0, exponent=2.2), "claw"))
    return out


def pillar_foot(kind, F, B, E, meta, sgn):
    """Footprint centre, half sizes and toes of a pillar foot under ankle (wrist) F. B is the ball (wrist end of the
    hand), E the toe end. The footprint sits under the middle of F-B, so the leg comes straight down into it."""
    d = flat(sub(E, B)) if math.hypot(E[0] - B[0], E[1] - B[1]) > 1 else (1.0, 0.0, 0.0)
    m = 0.5 * (meta[0] + meta[1])
    (hf, hs), spread, sizes, ends = PILLAR[kind]
    c = ((F[0] + B[0]) / 2, (F[1] + B[1]) / 2, 0.0)
    half = (hf * m, hs * m)
    out = []
    for deg, k, end in zip(spread, sizes, ends):
        u = turn(d, sgn, deg)
        out += nail_toe(c, u, rim(u, half), 1.25 * m * k, end)
    if kind == "sauropod_manus":     # the thumb claw, pointing in and back off the inner side of the column
        u = turn(d, sgn, -80)
        cb = (c[0] + u[0] * rim(u, half) * 0.9, c[1] + u[1] * rim(u, half) * 0.9, m * 0.45)
        ct = add(cb, (-0.15 * m, -sgn * 0.45 * m, 0.1 * m))
        out.append(tag(horn(cb, ct, (0.0, 0.0, -0.1 * m), m * 0.2, sides=10, steps=10), "claw"))
    return c, half, out


def pillar_leg(A, K, F, c, half, sizes, H):
    """The leg as leg_tube, but from the ankle (wrist) it flares down into the foot: the last ring is the sole."""
    top = (A[0] - 0.03 * H, A[1] * 0.7, A[2] + 0.06 * H)
    calf = add(lerp(K, F, 0.28), mul((-1.0, 0.0, 0.0), 0.035 * H))
    # The shin bends into the upright foot over its whole lower half, not at one point: a tube that turns
    # sharply pinches into a crease, which read as a cuff around the ankle.
    ankle = (c[0] + (F[0] - c[0]) * 0.3, c[1] + (F[1] - c[1]) * 0.3, F[2] * 0.9)
    foot = (c[0], c[1], 0.12 * min(half))          # full width just above the ground, then the sole's edge rolls under
    pts = [top, A, lerp(A, K, 0.4), lerp(A, K, 0.78), K, calf, lerp(K, F, 0.55), ankle, foot, c]
    P = tube(pts, list(sizes[:8]) + [half, (half[0] * 0.93, half[1] * 0.93)], sides=28, per_seg=6, turn_span=TURN_SPAN)
    P.v = [(x, y, max(z, 0.0)) for x, y, z in P.v]      # a flat sole: the last rings fold a little below the ground
    return ease_knee(P, K, A, F, 28, max(sizes[4]))


def joints(key):
    rig = json.load(open(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), encoding="utf-8"))
    return {j["name"]: tuple(float(v) for v in j["position_cm"]) for j in rig["joints"]}


def build_limbs(key):
    J = joints(key)
    parts = {}
    hind, fore = FEET[key]
    for side, sgn in (("l", -1), ("r", 1)):
        g = lambda n: J[f"{n}_{side}"] if f"{n}_{side}" in J else (J[f"{n}_l"][0], -J[f"{n}_l"][1], J[f"{n}_l"][2])
        A, K, F, B, E = g("thigh"), g("calf"), g("foot"), g("ball"), g("toe_end")
        sizes = HIND[key]
        if hind in PILLAR:
            c, half, feet = pillar_foot(hind, F, B, E, sizes[-1], sgn)
            parts.update(digits("leg", side, [pillar_leg(A, K, F, c, half, sizes, A[2])] + feet))
        else:
            feet = foot(hind, F, B, E, sizes[-1], sgn)
            k = FOOT_SCALE.get(key, 1.0)
            for P in feet:
                P.v = [(B[0] + (x - B[0]) * k, B[1] + (y - B[1]) * k, z * k) for x, y, z in P.v]
            parts.update(digits("leg", side, [leg_tube(A, K, F, B, sizes, A[2], sgn)] + feet))
        if fore:
            A, K, F, B = g("upperarm"), g("lowerarm"), g("hand"), g("finger_end")
            sizes = FORE[key]
            # The front foot's ball is the wrist end of the hand; the fingers run on to finger_end. The
            # sauropod hand is a column of fingers, so its ball is under the finger ends.
            Bh = (B[0], B[1], 0.0) if fore == "sauropod_manus" else lerp(F, B, 0.45)
            E = add(B, (1.0, 0.0, 0.0)) if fore == "sauropod_manus" else B
            c, half, feet = pillar_foot(fore, F, Bh, E, sizes[-1], sgn)
            parts.update(digits("arm", side, [pillar_leg(A, K, F, c, half, sizes, A[2])] + feet))
    return parts


# ---------------------------------------------------------------- OBJ in/out

def read_obj(path):
    """[(name, header comment lines before it, vertices, faces with indices local to the object)]."""
    objs, header, verts = [], [], []
    cur = None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("o "):
            cur = [line[2:].strip(), [], []]
            objs.append(cur)
        elif line.startswith("v "):
            v = tuple(float(x) for x in line.split()[1:4])
            verts.append(v)
            cur[1].append(len(verts) - 1)
        elif line.startswith("f "):
            cur[2].append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
        elif cur is None:
            header.append(line)
    out = []
    for name, vi, faces in objs:
        lo = min(vi) if vi else 0
        out.append((name, [verts[i] for i in vi], [[i - lo for i in f] for f in faces]))
    return header, out


def write_obj(path, header, objs):
    lines = list(header)
    base = 1
    for i, (name, verts, faces) in enumerate(objs):
        stem = name.rsplit("_", 1)[0] if name.rsplit("_", 1)[-1].isdigit() else name
        lines.append(f"o {stem}_{i}")
        lines += [f"v {v[0]:.2f} {v[1]:.2f} {v[2]:.2f}" for v in verts]
        lines += ["f " + " ".join(str(k + base) for k in f) for f in faces]
        base += len(verts)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def pieces_to_obj(pieces):
    verts, faces = [], []
    for P in pieces:
        b = len(verts)
        verts += [to_obj(p) for p in P.v]
        faces += [[a + b, c + b, d + b] for a, c, d in P.t]
    return verts, faces


def slices(verts, step=2.5):
    """Horizontal slices of one limb (Unreal axes in, OBJ verts): Z, centre X, centre lateral, front-back, side-side."""
    rows = []
    zs = [v[1] for v in verts]
    z = min(zs) + step / 2
    while z < max(zs):
        band = [v for v in verts if abs(v[1] - z) <= step / 2]
        if band:
            xs, ys = [v[0] for v in band], [v[2] for v in band]
            rows.append((0.5 * (min(xs) + max(xs)), z, 0.5 * (min(ys) + max(ys)), max(xs) - min(xs), max(ys) - min(ys)))
        z += step
    return rows


def main(argv):
    keys = argv or list(FEET)
    for key in keys:
        obj_path = os.path.join(ROOT, "body_shapes", f"{key}_body.obj")
        header, objs = read_obj(obj_path)
        limbs = build_limbs(key)
        new, done = [], set()
        for name, verts, faces in objs:
            stem = name.rsplit("_", 1)[0]
            if stem in limbs:
                verts, faces = pieces_to_obj(limbs[stem])
                done.add(stem)
                vol = sum(P.volume_obj() for P in limbs[stem]) / 1e6
                print(f"  {key} {stem}: {len(verts)} verts, {vol:.3f} m3, {len(limbs[stem])} pieces, "
                      f"{'outward' if min(P.volume_obj() for P in limbs[stem]) > 0 else 'INVERTED'}")
            new.append((name, verts, faces))
        missing = {s for s in limbs if s.count("_") == 1} - done
        if missing:
            raise SystemExit(f"{key}: no {sorted(missing)} object in {obj_path}")
        new += [(s, *pieces_to_obj(limbs[s])) for s in limbs if s not in done]      # the toes and claws, the first time
        if not any("leg_builder" in h for h in header):
            header.append("# Legs and feet rebuilt from the rig joints by body_tools/leg_builder.py.")
        write_obj(obj_path, header, new)

        # Keep the sections table in step: replace the limb rows with slices of the new limbs (left side).
        csv_path = os.path.join(ROOT, "body_shapes", f"{key}_sections.csv")
        rows = list(csv.reader(open(csv_path, encoding="utf-8")))
        head, body = rows[0], rows[1:]
        kinds = {"leg"} | ({"arm"} if FEET[key][1] else set())
        body = [r for r in body if r[0] not in kinds]
        for kind in sorted(kinds):
            verts = pieces_to_obj([P for s in limbs if s.startswith(kind + "_") and s.endswith("_l") for P in limbs[s]])[0]
            for x, z, y, fb, ss in slices(verts):
                body.append([kind, f"{x:.1f}", f"{z:.1f}", f"{y:.1f}", "", "", "", f"{fb:.1f}", f"{ss:.1f}"])
        with open(csv_path, "w", encoding="utf-8", newline="") as f:
            csv.writer(f, lineterminator="\n").writerows([head] + body)
        print(f"wrote {obj_path}")


if __name__ == "__main__":
    main(sys.argv[1:])
