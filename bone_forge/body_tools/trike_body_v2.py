"""Triceratops body volume, version 2: rebuilt to the life drawing (design doc, Life Drawings tab).

Fixes against version 1:
  - no pinch where the neck meets the skull (v1 went to 5 cm deep at the back of the skull, which
    left a seam and a caved-in skull top);
  - the frill is a solid shield rising up and back from the skull, with the bumps (epoccipitals)
    along its edge, instead of a thin upright plate;
  - the skull top is one convex surface from the frill to the beak, wide at the cheeks, narrow beak;
  - legs are pillars that start inside the body (no flaps at the hips and shoulders), on round pads.

Same conventions as the other body OBJs: cm, Y up, X forward, Z = the creature's right, origin on
the ground under the hip sockets; one OBJ object per part (body, head, leg_l, leg_r, arm_l, arm_r,
frill, horn_brow x2, horn_nose) so bone_forge_gltf.py weights each to its own bones.
Every closed piece is checked for outward winding (positive signed volume).

    python trike_body_v2.py [out.obj]     (default: ../body_shapes/trike_body.obj)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# ---------------------------------------------------------------- maths (Unreal axes: X fwd, Y right, Z up)

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    n = math.sqrt(dot(a, a)) or 1.0
    return mul(a, 1.0 / n)
def lerp(a, b, t): return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def pchip(xs, ys):
    """Monotone cubic through (xs, ys): smooth, no overshoot between stations."""
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0:
            m[i] = 0.0
        else:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])

    def f(x):
        x = min(max(x, xs[0]), xs[-1])
        i = max(0, min(n - 2, next((k for k in range(n - 1) if x <= xs[k + 1]), n - 2)))
        t = (x - xs[i]) / h[i]
        h00, h10 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t
        h01, h11 = -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
        return h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1]
    return f


def spow(v, p):
    return math.copysign(abs(v) ** p, v)


# ---------------------------------------------------------------- mesh pieces

class Piece:
    """One closed mesh: vertices (Unreal axes) and triangles."""
    def __init__(self):
        self.v, self.t = [], []

    def add_v(self, p):
        self.v.append(p)
        return len(self.v) - 1

    def quad(self, a, b, c, d):
        self.t += [(a, b, c), (a, c, d)]

    def volume_obj(self):
        """Signed volume in OBJ axes (right-handed, Y up); positive = faces wound outward."""
        vol = 0.0
        for a, b, c in self.t:
            pa, pb, pc = (to_obj(self.v[i]) for i in (a, b, c))
            vol += dot(pa, cross(pb, pc)) / 6.0
        return vol

    def make_outward(self):
        """Orient every triangle like its neighbours (shared edges run opposite ways), then turn the
        whole piece outward. Caps and sides built separately can't end up facing opposite ways."""
        edges = {}
        for i, (a, b, c) in enumerate(self.t):
            for e in ((a, b), (b, c), (c, a)):
                edges.setdefault(frozenset(e), []).append(i)
        done = [False] * len(self.t)
        for seed in range(len(self.t)):
            if done[seed]:
                continue
            done[seed] = True
            stack = [seed]
            while stack:
                i = stack.pop()
                a, b, c = self.t[i]
                for u, v in ((a, b), (b, c), (c, a)):
                    for j in edges[frozenset((u, v))]:
                        if done[j] or j == i:
                            continue
                        ta = self.t[j]
                        same_dir = any((ta[k], ta[(k + 1) % 3]) == (u, v) for k in range(3))
                        if same_dir:
                            self.t[j] = (ta[0], ta[2], ta[1])
                        done[j] = True
                        stack.append(j)
        if self.volume_obj() < 0:
            self.t = [(a, c, b) for a, b, c in self.t]
        return self


def to_obj(p):
    return (p[0], p[2], p[1])   # OBJ: X forward, Y up, Z = creature's right


def loft_x(stations, x0, x1, step, sides, exponent):
    """Vertical rings along X. stations: (X, top Z, bottom Z, half width). Ends closed with a point."""
    xs = [s[0] for s in stations]
    top, bot, hw = (pchip(xs, [s[i] for s in stations]) for i in (1, 2, 3))
    ex = pchip(xs, [exponent(s[0]) for s in stations])
    n = max(2, int(round((x1 - x0) / step)))
    P = Piece()
    rings = []
    for k in range(n + 1):
        x = x0 + (x1 - x0) * k / n
        tz, bz, w, e = top(x), bot(x), max(hw(x), 0.5), ex(x)
        c = 0.5 * (tz + bz)
        up, dn = max(tz - c, 0.5), max(c - bz, 0.5)
        ring = []
        for j in range(sides):
            a = 2 * math.pi * j / sides
            ca, sa = math.cos(a), math.sin(a)
            y = w * spow(ca, 2.0 / e)
            z = c + (up if sa > 0 else dn) * spow(sa, 2.0 / e)
            ring.append(P.add_v((x, y, z)))
        rings.append(ring)
    for k in range(n):
        r0, r1 = rings[k], rings[k + 1]
        for j in range(sides):
            j1 = (j + 1) % sides
            P.quad(r0[j], r1[j], r1[j1], r0[j1])
    for ring, x, sgn in ((rings[0], x0, -1), (rings[-1], x1, 1)):
        cy = sum(P.v[i][1] for i in ring) / sides
        cz = sum(P.v[i][2] for i in ring) / sides
        tip = P.add_v((x + sgn * 0.5, cy, cz))
        for j in range(sides):
            P.t.append((ring[j], ring[(j + 1) % sides], tip))
    return P.make_outward()


def catmull_path(pts, per_seg):
    """Smooth path through pts (Catmull-Rom), list of points."""
    out = []
    ext = [sub(mul(pts[0], 2), pts[1])] + pts + [sub(mul(pts[-1], 2), pts[-2])]
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        for s in range(per_seg):
            t = s / per_seg
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                                    + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in range(3)))
    out.append(pts[-1])
    return out


def tube(points, sizes, sides=20, per_seg=6, side_axis=(0, 1, 0), turn_span=1):
    """Tube along points; sizes = (half front-back, half side-to-side) per point. Ends capped.
    turn_span: how many samples either side set each ring's facing. 1 follows the path exactly; more turns the rings
    gradually through a sharp bend (a knee, an elbow), where rings that turn all at once fold into each other."""
    path = catmull_path(points, per_seg)
    sz = catmull_path([(a, b, 0.0) for a, b in sizes], per_seg)
    P = Piece()
    rings = []
    for k, p in enumerate(path):
        t = norm(sub(path[min(k + turn_span, len(path) - 1)], path[max(k - turn_span, 0)]))
        L = norm(sub(side_axis, mul(t, dot(side_axis, t))))
        F = norm(cross(L, t))
        if F[0] < 0:
            F = mul(F, -1)
        a, b = sz[k][0], sz[k][1]
        ring = []
        for j in range(sides):
            ang = 2 * math.pi * j / sides
            ring.append(P.add_v(add(p, add(mul(F, a * math.cos(ang)), mul(L, b * math.sin(ang))))))
        rings.append(ring)
    for k in range(len(rings) - 1):
        for j in range(sides):
            j1 = (j + 1) % sides
            P.quad(rings[k][j], rings[k + 1][j], rings[k + 1][j1], rings[k][j1])
    for ring, p in ((rings[0], path[0]), (rings[-1], path[-1])):
        c = P.add_v(p)
        for j in range(sides):
            P.t.append((ring[j], ring[(j + 1) % sides], c))
    return P.make_outward()


def blob(center, half, flat_bottom=None, lat=12, lon=20, exponent=2.4):
    """Superellipsoid; flat_bottom clamps Z (a foot sole)."""
    P = Piece()
    top = P.add_v((center[0], center[1], center[2] + half[2]))
    rows = []
    for i in range(1, lat):
        phi = -math.pi / 2 + math.pi * (lat - i) / lat
        row = []
        for j in range(lon):
            th = 2 * math.pi * j / lon
            cp, sp = math.cos(phi), math.sin(phi)
            x = center[0] + half[0] * spow(cp, 2 / exponent) * spow(math.cos(th), 2 / exponent)
            y = center[1] + half[1] * spow(cp, 2 / exponent) * spow(math.sin(th), 2 / exponent)
            z = center[2] + half[2] * spow(sp, 2 / exponent)
            if flat_bottom is not None:
                z = max(z, flat_bottom)
            row.append(P.add_v((x, y, z)))
        rows.append(row)
    bz = center[2] - half[2] if flat_bottom is None else max(center[2] - half[2], flat_bottom)
    bottom = P.add_v((center[0], center[1], bz))
    for j in range(lon):
        P.t.append((top, rows[0][j], rows[0][(j + 1) % lon]))
        P.t.append((bottom, rows[-1][(j + 1) % lon], rows[-1][j]))
    for i in range(len(rows) - 1):
        for j in range(lon):
            j1 = (j + 1) % lon
            P.quad(rows[i][j], rows[i + 1][j], rows[i + 1][j1], rows[i][j1])
    return P.make_outward()


def horn(base, tip, bend, r_base, sides=14, steps=14, r_tip=0.6, flat=1.0, width_dir=None):
    """Tapered horn along a quadratic curve; bend = offset of the curve's middle. r_tip: radius left at the tip (cm;
    keeps big horns from ending in a needle, but small teeth need it scaled down). flat: thickness as a share of the
    width, the width lying along width_dir (e.g. along the jaw: a blade, like a recurved raptor tooth)."""
    mid = add(lerp(base, tip, 0.5), bend)
    pts = [add(add(mul(base, (1 - u) ** 2), mul(mid, 2 * u * (1 - u))), mul(tip, u * u)) for u in (k / steps for k in range(steps + 1))]
    P = Piece()
    rings = []
    for k, p in enumerate(pts[:-1]):
        u = k / steps
        t = norm(sub(pts[k + 1], p))
        if width_dir is not None:          # the same width direction on every ring, so a blade doesn't twist
            a = norm(sub(width_dir, mul(t, dot(width_dir, t))))
        else:
            a = norm(cross(t, (0, 0, 1))) if abs(t[2]) < 0.95 else (1, 0, 0)
        b = cross(t, a)
        r = r_base * (1 - u) ** 0.85 + r_tip
        rings.append([P.add_v(add(p, add(mul(a, r * math.cos(2 * math.pi * j / sides)), mul(b, r * flat * math.sin(2 * math.pi * j / sides)))))
                      for j in range(sides)])
    for k in range(len(rings) - 1):
        for j in range(sides):
            j1 = (j + 1) % sides
            P.quad(rings[k][j], rings[k + 1][j], rings[k + 1][j1], rings[k][j1])
    tipv, basev = P.add_v(pts[-1]), P.add_v(base)
    for j in range(sides):
        P.t.append((rings[-1][j], rings[-1][(j + 1) % sides], tipv))
        P.t.append((rings[0][(j + 1) % sides], rings[0][j], basev))
    return P.make_outward()


def frill():
    """Solid shield rising up and back from the skull: a lens (thick middle, thin rim), cupped forward,
    with round epoccipital bumps along the upper rim."""
    base = (316.0, 0.0, 172.0)
    up = norm((-0.5, 0.0, 0.866))           # leans back 30 deg from vertical, over the neck
    side = (0.0, 1.0, 0.0)
    fwd = norm(cross(side, up))
    if fwd[0] < 0:
        fwd = mul(fwd, -1)
    a, b, t0 = 118.0, 92.0, 84.0              # half width, half height, centre height along `up`
    R, S = 10, 48

    def point(r, th, h):
        s, t = a * r * math.cos(th), t0 + b * r * math.sin(th)
        cup = 62.0 * (abs(s) / a) ** 1.6      # the sides wrap forward and down toward the cheeks
        return add(add(add(base, mul(side, s)), mul(up, t)), mul(fwd, cup + h))

    def thick(r):
        return 8.0 * math.sqrt(max(0.0, 1 - r * r))

    P = Piece()
    cf, cb = P.add_v(point(0, 0, thick(0))), P.add_v(point(0, 0, -thick(0)))
    front, back = [], []
    for k in range(1, R + 1):
        r = k / R
        rf = [P.add_v(point(r, 2 * math.pi * j / S, thick(r))) for j in range(S)]
        front.append(rf)
        back.append(rf if k == R else [P.add_v(point(r, 2 * math.pi * j / S, -thick(r))) for j in range(S)])
    for j in range(S):
        j1 = (j + 1) % S
        P.t.append((cf, front[0][j], front[0][j1]))
        P.t.append((cb, back[0][j1], back[0][j]))
    for k in range(R - 1):
        for j in range(S):
            j1 = (j + 1) % S
            P.quad(front[k][j], front[k + 1][j], front[k + 1][j1], front[k][j1])
            P.quad(back[k][j1], back[k + 1][j1], back[k + 1][j], back[k][j])
    pieces = [P.make_outward()]
    for j in range(S):
        th = 2 * math.pi * j / S
        if j % 3 == 0 and t0 + b * math.sin(th) > 80.0:     # upper rim only; the lower rim is inside the neck
            pieces.append(blob(point(1.0, th, 0.0), (7.0, 7.0, 7.0), lat=6, lon=8, exponent=2.0))
    return pieces


# ---------------------------------------------------------------- the Triceratops

# Body, tail and neck: (X, top Z, bottom Z, half width), cm. From the life drawing's side outline and the
# Body Shape tab, with the neck kept thick into the skull (it hides behind the frill in life).
BODY = [
    (-336, 93, 91, 1.5), (-328, 99, 93, 5), (-278, 136, 106, 12), (-226, 175, 118, 21), (-172, 208, 130, 32),
    (-117, 236, 140, 45), (-59, 260, 136, 62), (0, 276, 116, 92), (58, 281, 97, 114), (117, 279, 88, 127),
    (176, 263, 85, 124), (232, 238, 88, 110), (262, 222, 94, 88), (293, 210, 100, 72), (318, 204, 104, 62),
]
# Head: one convex skull from behind the frill base to the beak. Wide cheeks (the jugal flare under the
# eye), brow ridge, narrowing snout, hooked beak at the front.
HEAD = [
    (286, 206, 108, 62), (312, 210, 104, 64), (338, 198, 100, 58), (362, 182, 95, 46), (390, 160, 88, 34),
    (414, 140, 82, 24), (434, 120, 78, 15), (448, 104, 76, 8), (457, 92, 80, 2),
]


def build():
    parts = []
    parts.append(("body", [loft_x(BODY, -336, 318, 3.5, 36, lambda x: 2.0 if x < -60 else 2.4)]))
    # Head with an opening mouth: upper skull + hooked beak, lower jaw with its own beak, cheek teeth far back.
    from head_builder import mouth_parts, line
    mouth = line((334, 112), (446, 84))
    upper = [(286, 206, 108, 62), (312, 210, 104, 64), (334, 200, mouth(334) + 0.2, 59), (362, 182, mouth(362) + 0.2, 46),
             (390, 160, mouth(390) + 0.2, 34), (414, 140, mouth(414) + 0.2, 24), (434, 120, mouth(434) + 0.2, 15),
             (448, 104, mouth(448) + 0.2, 8), (457, 92, 81, 2)]
    lower = [(334, mouth(334) - 0.2, 94, 52), (360, mouth(360) - 0.2, 85, 42), (390, mouth(390) - 0.2, 78, 31),
             (414, mouth(414) - 0.2, 75, 21), (434, mouth(434) - 0.2, 74, 13), (445, mouth(445) - 0.2, 77, 5)]
    parts += mouth_parts(upper, lower, mouth, exp_up=2.5, exp_jaw=2.4,
                         teeth=dict(upper=(345, 392, 10, 3.0, 1.2), lower=(347, 390, 10, 2.6, 1.1), curve=0.0))
    for side, sgn in (("l", -1), ("r", 1)):
        y = lambda v: v * sgn
        leg = tube([(-4, y(58), 206), (-3, y(85), 178), (20, y(95), 137), (42, y(103), 96), (31, y(101), 66), (20, y(100), 38), (26, y(100), 18)],
                   [(48, 32), (52, 38), (46, 38), (40, 40), (32, 33), (28, 31), (31, 33)])
        parts.append((f"leg_{side}", [leg, blob((32, y(100), 14), (40, 36, 14), flat_bottom=0.0)]))
    for side, sgn in (("l", -1), ("r", 1)):
        y = lambda v: v * sgn
        arm = tube([(266, y(54), 162), (274, y(85), 128), (254, y(101), 102), (234, y(112), 76), (244, y(105), 50), (254, y(98), 24), (258, y(98), 12)],
                   [(36, 25), (36, 29), (30, 28), (27, 29), (24, 25), (21, 24), (24, 26)])
        parts.append((f"arm_{side}", [arm, blob((264, y(99), 10), (31, 30, 10), flat_bottom=0.0)]))
    parts.append(("frill", frill()))
    for sgn in (-1, 1):
        parts.append(("horn_brow", [horn((336, 30 * sgn, 190), (447, 64 * sgn, 290), (8, 4 * sgn, 6), 14.0)]))
    parts.append(("horn_nose", [horn((410, 0, 136), (420, 0, 186), (-4, 0, 0), 11.0)]))
    for sgn in (-1, 1):     # the epijugals: blunt cheek horns under the eyes, pointing out and down
        parts.append(("horn_jugal", [horn((343, 54 * sgn, 124), (339, 74 * sgn, 112), (0, 2 * sgn, -2), 7.0, sides=12, steps=8)]))
    from face_sculpt import sculpt
    return sculpt("trike", parts)      # the skull's anatomy: horn bosses, cheek points, nostril hollows...


def write_obj(parts, path):
    lines = ["# Triceratops body volume v2 (Bone Forge), rebuilt to the life drawing. Units cm. Y up, X forward, Z = creature's right.",
             "# Same origin as the rig files: on the ground under the hip sockets. Built by body_tools/trike_body_v2.py."]
    base = 1
    for i, (name, pieces) in enumerate(parts):
        lines.append(f"o {name}_{i}")
        for P in pieces:
            for p in P.v:
                o = to_obj(p)
                lines.append(f"v {o[0]:.2f} {o[1]:.2f} {o[2]:.2f}")
        for P in pieces:
            for a, b, c in P.t:
                lines.append(f"f {a + base} {b + base} {c + base}")
            base += len(P.v)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "body_shapes", "trike_body.obj")
    parts = build()
    total_v = 0
    for name, pieces in parts:
        vols = [P.volume_obj() for P in pieces]
        nv = sum(len(P.v) for P in pieces)
        total_v += nv
        print(f"  {name:10s} {nv:6d} verts  volume {sum(vols) / 1e6:7.3f} m3  {'outward' if min(vols) > 0 else 'INVERTED'}")
    write_obj(parts, out)
    print(f"wrote {os.path.abspath(out)} ({total_v} vertices)")


if __name__ == "__main__":
    main()
