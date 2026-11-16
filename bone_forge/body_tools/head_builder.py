"""Heads with a working mouth: an upper skull and a separate lower jaw (plus teeth and tongue) that
meet at the mouth line and hinge at the rig's jaw joint, so opening the jaw opens the mouth instead
of stretching one closed head volume.

Parts it writes (bone_forge_gltf.py weights them by name):
  head         the upper skull and snout (head bone, blending into the neck at the back)
  jaw          the lower jaw (rigid on the jaw bone)
  tongue       on the floor of the mouth (rigid on jaw)
  teeth_upper  rigid on head;  teeth_lower  rigid on jaw
Closed, each set of teeth sits inside the opposite jaw, so it only shows when the mouth opens.

    python head_builder.py [species ...]     (default: trex raptor ankylo brachio ptera)
Replaces the head part of ../body_shapes/<species>_body.obj. The file it starts from is kept as
<species>_body_v1_fixed.obj (v1 with its winding fixed); the untouched original is <species>_body_v1.obj. The Triceratops uses this from trike_body_v2.py.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from trike_body_v2 import Piece, add, blob, horn, mul, norm, pchip, spow, sub, to_obj  # noqa: E402
from face_sculpt import sculpt  # noqa: E402

ROOT = os.path.join(HERE, "..")


# ---------------------------------------------------------------- lofts with separate top / bottom shapes

def loft(stations, step, sides, exp_up, exp_down, x0=None, x1=None):
    """Vertical rings along X through (X, top Z, bottom Z, half width) stations; exp_up / exp_down shape
    the upper and lower halves of each ring (2 = ellipse, higher = boxier / flatter)."""
    xs = [s[0] for s in stations]
    x0 = xs[0] if x0 is None else x0
    x1 = xs[-1] if x1 is None else x1
    top, bot, hw = (pchip(xs, [s[i] for s in stations]) for i in (1, 2, 3))
    n = max(2, int(round((x1 - x0) / step)))
    P = Piece()
    rings = []
    for k in range(n + 1):
        x = x0 + (x1 - x0) * k / n
        tz, bz, w = top(x), bot(x), max(hw(x), 0.3)
        c = 0.5 * (tz + bz)
        up, dn = max(tz - c, 0.3), max(c - bz, 0.3)
        ring = []
        for j in range(sides):
            a = 2 * math.pi * j / sides
            ca, sa = math.cos(a), math.sin(a)
            e = exp_up if sa > 0 else exp_down
            ring.append(P.add_v((x, w * spow(ca, 2.0 / e), c + (up if sa > 0 else dn) * spow(sa, 2.0 / e))))
        rings.append(ring)
    for k in range(n):
        for j in range(sides):
            j1 = (j + 1) % sides
            P.quad(rings[k][j], rings[k + 1][j], rings[k + 1][j1], rings[k][j1])
    for ring, x, sgn in ((rings[0], x0, -1), (rings[-1], x1, 1)):
        cy = sum(P.v[i][1] for i in ring) / sides
        cz = sum(P.v[i][2] for i in ring) / sides
        tip = P.add_v((x + sgn * 0.2, cy, cz))
        for j in range(sides):
            P.t.append((ring[j], ring[(j + 1) % sides], tip))
    return P.make_outward()


def round_back(stations, span):
    """Two shrinking stations behind the first, so the back end is rounded instead of a flat cut."""
    x, t, b, w = stations[0]
    c = 0.5 * (t + b)
    extra = [(x - 0.07 * span, c + 0.3 * (t - c), c - 0.3 * (c - b), 0.3 * w),
             (x - 0.04 * span, c + 0.75 * (t - c), c - 0.75 * (c - b), 0.75 * w)]
    return extra + list(stations)


def plate(center, normal, along, half, lat=6, lon=10):
    """A flattened superellipsoid lying on a surface: half = (along, across, thickness)."""
    n = norm(normal)
    t = norm(sub(along, mul(n, sum(a * b for a, b in zip(along, n)))))
    s = (n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0])
    P = blob((0.0, 0.0, 0.0), half, lat=lat, lon=lon, exponent=2.2)
    P.v = [add(center, add(add(mul(t, v[0]), mul(s, v[1])), mul(n, v[2]))) for v in P.v]
    return P.make_outward()


def ankylo_armor():
    """Rows of keeled plates over the back, spikes along the flanks and the tail, from the body's sections."""
    rows = {}
    with open(os.path.join(ROOT, "body_shapes", "ankylo_sections.csv"), encoding="utf-8") as f:
        next(f)
        for line_ in f:
            p = line_.strip().split(",")
            if p[0] == "body":
                rows[float(p[1])] = (float(p[2]), float(p[4]), float(p[5]), float(p[6]))
    xs = sorted(rows)
    zc, up, hw = (pchip(xs, [rows[x][i] for x in xs]) for i in (0, 1, 3))

    def surface(x, theta):
        """Point and outward normal on the body at angle theta from the top (+ = the creature's right)."""
        st, ct = math.sin(theta), math.cos(theta)
        p = (x, hw(x) * st, zc(x) + up(x) * ct)
        n = norm((0.0, st / max(hw(x), 1.0), ct / max(up(x), 1.0)))
        return p, n

    pieces, spikes = [], []
    x = -60.0
    k = 0
    while x < 235.0:   # plates from the hips to the neck, rows staggered
        for deg in (0, 26, 52, 76):
            for s in ((1,) if deg == 0 else (-1, 1)):
                th = math.radians(deg * s + (6 if k % 2 else 0))
                p, n = surface(x, th)
                size = 1.0 - 0.35 * abs(x - 90) / 160
                pieces.append(plate(add(p, mul(n, 2.0)), n, (1, 0, 0), (11 * size, 8 * size, 3.2)))
        x += 24.0
        k += 1
    for x in range(-30, 231, 34):  # side spikes along the flanks
        for s in (-1, 1):
            p, n = surface(float(x), math.radians(88 * s))
            out = norm(add(n, (0.0, 0.0, 0.25)))
            spikes.append(horn(add(p, mul(n, -2.0)), add(p, mul(out, 17.0)), (0, 0, 2), 7.0, sides=10, steps=6))
    for x in range(-100, -235, -32):  # triangular spikes along the tail sides
        for s in (-1, 1):
            p, n = surface(float(x), math.radians(60 * s))
            out = norm(add(n, (0.0, 0.0, 0.4)))
            spikes.append(horn(add(p, mul(n, -2.0)), add(p, mul(out, 13.0)), (0, 0, 1), 6.0, sides=10, steps=6))
    return [("armor_plates", pieces), ("armor_spikes", spikes)]


def mouth_parts(upper, lower, mouth, exp_up=2.5, exp_jaw=2.3, step=None, teeth=None, tongue=True, sides=96):
    """upper / lower: station lists (X, top, bottom, half width). mouth(x) = mouth-line height.
    teeth: dict(upper=(x0, x1, count, length, radius), lower=(...), curve=0.15). 96 sides round, fine enough for
    face_sculpt.py's brows, sockets and lip lines."""
    span = upper[-1][0] - upper[0][0]
    step = step or max(span / 70.0, 0.3)
    upper, lower = round_back(upper, span), round_back(lower, span)
    parts = [("head", [loft(upper, step, sides, exp_up, 7.0)]),
             ("jaw", [loft(lower, step, sides, 7.0, exp_jaw)])]
    lx = [s[0] for s in lower]
    jaw_hw = pchip(lx, [s[3] for s in lower])
    jaw_bot = pchip(lx, [s[2] for s in lower])
    ux = [s[0] for s in upper]
    up_hw = pchip(ux, [s[3] for s in upper])
    up_top = pchip(ux, [s[1] for s in upper])
    if tongue:
        a, b = lower[0][0], lower[-1][0]
        x0, x1 = a + 0.12 * (b - a), a + 0.62 * (b - a)
        cx = 0.5 * (x0 + x1)
        depth = 0.5 * (mouth(cx) - jaw_bot(cx))
        parts.append(("tongue", [blob((cx, 0.0, mouth(cx) - depth * 0.35), ((x1 - x0) * 0.5, jaw_hw(cx) * 0.5, depth * 0.45), lat=10, lon=16)]))
    if teeth:
        # curve: how far the tip leans along the jaw (as a share of the length; negative = back toward the throat, the
        # way theropod teeth recurve); bend: where the curve's middle sits, as a share of curve (negative = a convex
        # front edge); tip: the radius left at the point as a share of the tooth's base radius (None = horn()'s
        # fixed 0.6 cm, too blunt for small teeth); flat: thickness across the jaw as a share of the width along it.
        curve, bend = teeth.get("curve", 0.15), teeth.get("bend", 0.4)
        tip_share, flat = teeth.get("tip"), teeth.get("flat", 1.0)
        for which, sign, hwf, frac in (("upper", -1, up_hw, 0.8), ("lower", 1, jaw_hw, 0.68)):
            spec = teeth.get(which)
            if not spec:
                continue
            x0, x1, count, length, radius = spec
            pieces = []
            for i in range(count):
                u = i / max(count - 1, 1)
                x = x0 + (x1 - x0) * u
                size = 0.65 + 0.35 * math.sin(math.pi * min(1.0, 0.25 + u))   # biggest a third of the way back
                L, r = length * size, radius * size
                # No longer than the jaw it closes into is deep, or it pokes out under the chin (the raptor's thin
                # lower jaw near the tip).
                room = mouth(x) - jaw_bot(x) if which == "upper" else up_top(x) - mouth(x)
                L = min(L, 0.5 * room)
                for side in (-1, 1):
                    y = side * hwf(x) * frac
                    base = (x, y, mouth(x) - sign * 0.6 * r)
                    tip = (x + curve * L, y, mouth(x) + sign * L)
                    r_tip = 0.6 if tip_share is None else tip_share * r
                    pieces.append(horn(base, tip, (bend * curve * L, 0.0, 0.0), r, sides=8, steps=6, r_tip=r_tip, flat=flat,
                                       width_dir=(1.0, 0.0, 0.0) if flat != 1.0 else None))
            parts.append((f"teeth_{which}", pieces))
    return parts


# ---------------------------------------------------------------- species

def rig(species):
    d = json.load(open(os.path.join(ROOT, "rig_joints", f"{species}_rig.json"), encoding="utf-8"))
    return {j["name"]: j["position_cm"] for j in d["joints"]}


def line(p0, p1):
    """Mouth line through two (X, Z) points."""
    return lambda x: p0[1] + (p1[1] - p0[1]) * (x - p0[0]) / (p1[0] - p0[0])


def from_v1(verts, x0, x1, n):
    """(X, top, bottom, half width) stations measured from an old head volume (Unreal axes)."""
    out = []
    for k in range(n + 1):
        x = x0 + (x1 - x0) * k / n
        band = max((x1 - x0) / n * 0.6, 0.5)
        sl = [v for v in verts if abs(v[0] - x) <= band]
        if len(sl) < 4:
            continue
        out.append((x, max(v[2] for v in sl), min(v[2] for v in sl), max(abs(v[1]) for v in sl)))
    return out


def smooth_stations(stations, half):
    """Moving average of measured (X, top, bottom, half width) stations over 2 * half + 1 of them."""
    out = []
    for i, (x, *_) in enumerate(stations):
        win = stations[max(0, i - half):i + half + 1]
        out.append((x, *(sum(w[k] for w in win) / len(win) for k in (1, 2, 3))))
    return out


def split(stations, mouth, hinge_x, gap=0.4, keep_back=True):
    """Cut measured head stations at the mouth line: (upper, lower)."""
    upper, lower = [], []
    for x, t, b, w in stations:
        m = mouth(x)
        if x < hinge_x:
            if keep_back:
                upper.append((x, t, b, w))
            continue
        m = min(max(m, b + 0.3 * (t - b)), t - 0.35 * (t - b))
        upper.append((x, t, m + gap * 0.5, w))
        lower.append((x, m - gap * 0.5, b, w * 0.93))
    return upper, lower


def species_parts(species, v1_head):
    J = rig(species)
    hinge = J["jaw"]
    if species == "trex":
        # Deep, boxy skull; broad behind the eyes (binocular vision), narrow snout. From the life drawing.
        mouth = line((400, 305), (585, 275))
        # The back of the skull starts narrower than the neck there (2026-11-03), so the skull grows out of the
        # swollen neck (NECK_SWELL) instead of sitting on it like a helmet with a rim.
        upper = [(362, 410, 322, 50), (380, 427, 318, 62), (396, 433, mouth(396) + 0.2, 70), (425, 420, mouth(425) + 0.2, 64),
                 (460, 400, mouth(460) + 0.2, 53), (500, 375, mouth(500) + 0.2, 42), (540, 352, mouth(540) + 0.2, 34),
                 (570, 337, mouth(570) + 0.2, 27), (590, 324, mouth(590) + 0.2, 18), (599, 306, mouth(599) + 2, 5)]
        lower = [(396, mouth(396) - 0.2, 266, 60), (430, mouth(430) - 0.2, 259, 54), (480, mouth(480) - 0.2, 256, 43),
                 (530, mouth(530) - 0.2, 257, 33), (565, mouth(565) - 0.2, 262, 25), (584, mouth(584) - 0.2, 267, 14),
                 (590, mouth(590) - 0.2, 271, 4)]
        return mouth_parts(upper, lower, mouth, exp_up=2.8, exp_jaw=2.6,
                           teeth=dict(upper=(418, 586, 12, 15.0, 3.4), lower=(424, 576, 11, 13.0, 3.0), curve=0.18))
    if species == "ankylo":
        # Wide, low, armoured head with a horn at each back corner and a broad beak. From the life drawing; from
        # above it narrows from the horns to a rounded snout, as the skull picture shows (2026-11-03).
        mouth = line((280, 104), (344, 100))
        upper = [(264, 157, 110, 46), (281, 166, mouth(281) + 0.2, 51), (300, 165, mouth(300) + 0.2, 47),
                 (320, 156, mouth(320) + 0.2, 39), (335, 143, mouth(335) + 0.2, 32), (344, 126, mouth(344) + 0.2, 26),
                 (349, 110, mouth(349) + 1, 18)]
        lower = [(281, mouth(281) - 0.2, 85, 46), (300, mouth(300) - 0.2, 83, 42), (320, mouth(320) - 0.2, 85, 35),
                 (336, mouth(336) - 0.2, 90, 29), (344, mouth(344) - 0.2, 95, 22), (348, mouth(348) - 0.2, 98, 11)]
        parts = mouth_parts(upper, lower, mouth, exp_up=3.2, exp_jaw=3.0,
                            teeth=dict(upper=(292, 330, 10, 2.2, 1.1), lower=(294, 328, 10, 2.0, 1.0), curve=0.0))
        for s in (-1, 1):
            parts.append(("horn_squamosal", [horn((272, 44 * s, 156), (255, 64 * s, 176), (0, 4 * s, 4), 9.0)]))
            parts.append(("horn_cheek", [horn((278, 47 * s, 108), (262, 68 * s, 97), (0, 3 * s, -2), 8.0)]))
        return parts + ankylo_armor()
    if species == "raptor":
        mouth = line((hinge[0], hinge[2]), (J["jaw_end"][0], J["jaw_end"][2] + 0.6))
        st = from_v1(v1_head, min(v[0] for v in v1_head) + 0.3, max(v[0] for v in v1_head) - 0.3, 30)
        st = smooth_stations(st, 2)      # the measured outline was ribbed (Raven's review renders, 2026-11-03)
        # The skull pictures show a long, low, flat-topped snout (skull height about a quarter of its length); the
        # v1 head was a tall dome. Lower the skull roof toward the mouth line, most at the snout.
        st = [(x, mouth(x) + (t - mouth(x)) * (0.82 - 0.12 * (x - st[0][0]) / (st[-1][0] - st[0][0])), b_, w)
              for x, t, b_, w in st]
        upper, lower = split(st, mouth, hinge[0], gap=0.2)
        # Blade teeth, recurved back toward the throat with a convex front edge, pointed (Raven 2026-11-03: they were
        # blunt cylinders that looked like cups, from horn()'s fixed 0.6 cm tip radius on 0.35 cm teeth).
        return mouth_parts(upper, lower, mouth, exp_up=2.3, exp_jaw=2.2, step=0.4,
                           teeth=dict(upper=(64, 85.0, 13, 1.4, 0.42), lower=(64.5, 84.0, 12, 1.2, 0.36), curve=-0.35,
                                      bend=-0.6, tip=0.05, flat=0.5))
    if species == "brachio":
        mouth = line((hinge[0], hinge[2]), (J["jaw_end"][0], J["jaw_end"][2] + 1))
        st = from_v1(v1_head, min(v[0] for v in v1_head) + 0.5, max(v[0] for v in v1_head) - 0.5, 34)
        st = smooth_stations(st, 2)      # the measured outline was ragged (Raven's review renders, 2026-11-03)
        upper, lower = split(st, mouth, hinge[0])
        return mouth_parts(upper, lower, mouth, exp_up=2.4, exp_jaw=2.3,
                           teeth=dict(upper=(1032, 1060, 7, 3.5, 1.3), lower=(1034, 1058, 6, 3.0, 1.2), curve=0.05))
    if species == "ptera":
        # Long, narrow, toothless beak; the crest stays its own part. No tongue: the lower beak is too thin to
        # hold one (it showed through underneath).
        mouth = line((hinge[0], hinge[2] - 1), (J["jaw_end"][0], J["jaw_end"][2] + 0.4))
        st = from_v1(v1_head, min(v[0] for v in v1_head) + 0.3, max(v[0] for v in v1_head) - 0.3, 40)
        upper, lower = split(st, mouth, hinge[0], gap=0.2)
        return mouth_parts(upper, lower, mouth, exp_up=2.2, exp_jaw=2.2, step=0.8, tongue=False)
    raise ValueError(species)


# ---------------------------------------------------------------- OBJ in / out

def read_obj(path):
    verts, parts, cur = [], [], None
    for line_ in open(path, encoding="utf-8"):
        p = line_.split()
        if not p:
            continue
        if p[0] == "v":
            verts.append((float(p[1]), float(p[2]), float(p[3])))
        elif p[0] == "o":
            cur = [p[1], []]
            parts.append(cur)
        elif p[0] == "f":
            f = [int(x.split("/")[0]) for x in p[1:]]
            cur[1] += [(f[0], f[k], f[k + 1]) for k in range(1, len(f) - 1)]
    return verts, parts


def write_obj(path, header, old_parts, verts, new_parts):
    """old_parts: [(name, tris over verts)] written as they are; new_parts: [(name, [Piece])]."""
    lines = list(header)
    n = 0
    k = 0
    for name, tris in old_parts:
        base = name.rsplit("_", 1)[0] if name.rsplit("_", 1)[-1].isdigit() else name
        used = sorted({i for t in tris for i in t})
        local = {g: n + i + 1 for i, g in enumerate(used)}
        lines.append(f"o {base}_{k}")
        lines += [f"v {verts[g - 1][0]:.2f} {verts[g - 1][1]:.2f} {verts[g - 1][2]:.2f}" for g in used]
        lines += [f"f {local[a]} {local[b]} {local[c]}" for a, b, c in tris]
        n += len(used)
        k += 1
    for name, pieces in new_parts:
        lines.append(f"o {name}_{k}")
        for P in pieces:
            lines += [f"v {o[0]:.2f} {o[1]:.2f} {o[2]:.2f}" for o in (to_obj(p) for p in P.v)]
        for P in pieces:
            lines += [f"f {a + n + 1} {b + n + 1} {c + n + 1}" for a, b, c in P.t]
            n += len(P.v)
        k += 1
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


# Stretches of the v1 body smoothed out (X from, X to, passes): the T. rex body had a sharp ridge and a step under
# the chest where the neck starts, which showed as a jagged ring (Raven's review renders, 2026-11-03).
BODY_SMOOTH = {"trex": (235, 310, 20)}

# The T. rex head sat on its thin neck like a helmet (Raven, 2026-11-03; Reference Library T. rex #9, #10). The body's
# own neck is swollen out to these sections (X, top Z, bottom Z, half width; OBJ X): into the jaw muscles behind
# the eyes and down into the throat to the lower jaw, ending just inside the skull so the skull carries on from it.
# Pushing the body out keeps one surface; a separate blend piece over it showed a collar and a lip where they crossed.
NECK_SWELL = {"trex": [(232, 383, 200, 62), (255, 380, 220, 62), (285, 381, 236, 64), (315, 389, 248, 66),
                       (340, 402, 258, 67), (360, 416, 263, 68), (378, 428, 266, 68.5), (392, 432, 267, 68),
                       (398, 430, 268, 66)]}


def swell_body(verts, tris, stations, exp_up=2.8, exp_dn=2.3, ramp=8.0, passes=6):
    """Push a part's vertices out to the stations in vertical X slices wherever they lie inside them (easing in from
    85% over `ramp` cm; the first station matches the chest), then smooth the stretch to take out the folds a
    slice-wise push leaves on a tilted neck."""
    xs = [st[0] for st in stations]
    top, bot, hw = (pchip(xs, [st[i] for st in stations]) for i in (1, 2, 3))
    used = sorted({i for t in tris for i in t})
    for i in used:
        x, y, z = verts[i - 1]
        if not xs[0] <= x <= xs[-1]:
            continue
        t, b_, w = top(x), bot(x), hw(x)
        c = 0.5 * (t + b_)
        r = math.hypot(y - c, z)
        if r < 1e-6:
            continue
        cy, cz = (y - c) / r, z / r
        a, e = ((t - c), exp_up) if cy > 0 else ((c - b_), exp_dn)
        rb = 1.0 / ((abs(cz) / w) ** e + (abs(cy) / a) ** e) ** (1.0 / e)
        k = min(1.0, (x - xs[0]) / ramp)
        rb *= 0.85 + 0.15 * (k * k * (3 - 2 * k))
        if r < rb:
            verts[i - 1] = (x, c + cy * rb, cz * rb)
    smooth_body(verts, tris, xs[0] - 10, xs[-1] + 5, passes)


def smooth_body(verts, tris, x0, x1, passes):
    """Laplacian smoothing of one part's vertices between x0 and x1 (OBJ X), easing in and out at the ends."""
    nb = {}
    for a, b, c in tris:
        for i, j in ((a, b), (b, c), (c, a)):
            nb.setdefault(i, set()).add(j)
            nb.setdefault(j, set()).add(i)
    ramp = 0.2 * (x1 - x0)
    for _ in range(passes):
        new = {}
        for i, ns in nb.items():
            x = verts[i - 1][0]
            if not x0 < x < x1:
                continue
            w = 0.5 * min(1.0, (x - x0) / ramp, (x1 - x) / ramp)
            avg = [sum(verts[j - 1][k] for j in ns) / len(ns) for k in range(3)]
            new[i] = tuple(verts[i - 1][k] + w * (avg[k] - verts[i - 1][k]) for k in range(3))
        for i, v in new.items():
            verts[i - 1] = v


def rebuild(species):
    path = os.path.join(ROOT, "body_shapes", f"{species}_body.obj")
    v1 = os.path.join(ROOT, "body_shapes", f"{species}_body_v1.obj")
    # Start from the winding-fixed file each time; its head part is replaced, everything else kept.
    src = os.path.join(ROOT, "body_shapes", f"{species}_body_v1_fixed.obj")
    if not os.path.exists(src):
        os.replace(path, src)
    verts, parts = read_obj(src)
    header = [l.rstrip("\n") for l in open(src, encoding="utf-8") if l.startswith("#")]
    header.append("# Head rebuilt with an opening mouth (upper skull, jaw, teeth, tongue) by body_tools/head_builder.py.")
    if species in BODY_SMOOTH:
        smooth_body(verts, next(t for n, t in parts if n.startswith("body")), *BODY_SMOOTH[species])
    if species in NECK_SWELL:
        swell_body(verts, next(t for n, t in parts if n.startswith("body")), NECK_SWELL[species])
    head = next(p for p in parts if p[0].startswith("head"))
    head_verts = [(verts[i - 1][0], verts[i - 1][2], verts[i - 1][1]) for i in sorted({i for t in head[1] for i in t})]
    keep = []
    for name, tris in parts:
        if name is head[0]:
            continue
        if name.startswith("head"):          # the Pteranodon's second head part is its crest
            name = "crest_" + name.split("_")[-1]
        keep.append((name, tris))
    new = sculpt(species, species_parts(species, head_verts))      # the skull's anatomy, face_sculpt.py
    write_obj(path, header, keep, verts, new)
    print(f"{species}: " + ", ".join(f"{n} ({sum(len(P.v) for P in ps)} v, {'outward' if all(P.volume_obj() > 0 for P in ps) else 'CHECK'})" for n, ps in new))


if __name__ == "__main__":
    for s in sys.argv[1:] or ["trex", "raptor", "ankylo", "brachio", "ptera"]:
        rebuild(s)
