"""Parts the first body volumes were missing or got wrong, from Raven's review of the clay renders (2026-11-03):

  neck        T. rex, Brachiosaurus: the head sat on the end of a thin neck like a helmet (T. rex) or floated past a
              slanted cut (Brachiosaurus). The T. rex body's own neck is swollen out into the jaw muscles and the
              throat by head_builder.py (NECK_SWELL), the way the life pictures show (Reference Library T. rex #9,
              #10); the Brachiosaurus gets a neck_blend piece from the neck over its cut end into the skull.
  arm_l/arm_r T. rex, Velociraptor: the arms ended in a rounded nub. Now an upper arm, forearm and a hand with
              clawed fingers: two big claws on the T. rex (and the splint of a third), three long curved claws on
              the raptor (T. rex #9, #13; the Velociraptor life pictures).
  tail_club   Ankylosaurus: was a ball. Now the bony club seen from above in Reference Library Ankylosaurus #7: two
              big rounded lobes either side of the tail's end, two small ones at the tip, flattened top to bottom.

  wing_feathers_l/_r  Velociraptor: folded wings of separate feathers on the new arms (the v1 ones were planks).
  arm_fingers, leg_toes  Pteranodon: three small clawed fingers at the knuckle it walks on, four clawed toes.
  eye, eyelid (+ _half, _closed), nostril  every species' face: eyeballs (painted iris and pupil), the open lids and
              two more sets the game shows in turn to blink, and nostrils, placed on the head mesh (FACE) after
              face_sculpt.py has sculpted the brows, sockets and the rest of the skull into it.

    python parts_builder.py [species ...]      (default: all six)

Rewrites ../body_shapes/<species>_body.obj in place, replacing these parts (or adding neck_blend). Run it after
head_builder.py and leg_builder.py; it touches nothing else. bone_forge_gltf.py weights neck_blend to the neck and
head bones, the arms to the shoulder chain, the club to the tail.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
from trike_body_v2 import Piece, add, blob, cross, lerp, mul, norm, sub, tube  # noqa: E402
from head_builder import loft  # noqa: E402
import leg_builder as lb  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(HERE), "unreal"))
import bone_forge_skin as skin  # noqa: E402

ROOT = os.path.dirname(HERE)

# (X, top Z, bottom Z, half width) in cm. Each starts just inside the neck and ends inside the skull; between, it
# swells out over the jaw muscles and down to the throat.
NECK_BLEND = {
    # The Brachiosaurus neck rises steeply, so these vertical sections are tall. Inside the neck at first, then a
    # little proud of it (no flicker where two surfaces meet), up over the cut end of the neck and into the crest.
    "brachio": [(872, 1232, 1073, 27), (895, 1268, 1100, 32.5), (920, 1291, 1129, 33), (940, 1300, 1152, 33),
                (960, 1306, 1170, 32.5), (975, 1314, 1180, 31.5), (992, 1318, 1188, 30)],
}


# Arms: joint names from the rig, sizes as leg_builder (half front-back, half side-side), the fingers as
# (spread deg, length, radius, claw length, claw curl), and whether the hand hangs palm-in (the raptor folds its
# hands like a bird's).
ARMS = {
    "trex": dict(sizes=[(17, 13), (15, 12), (12, 10), (9, 8.5), (7, 6.5), (6, 5.5)],
                 fingers=[(-14, 9.0, 3.2, 9.5, 0.55), (8, 12.0, 3.4, 10.5, 0.55)], splint=(26, 3.0, 1.6)),
    "raptor": dict(sizes=[(2.6, 2.0), (2.3, 1.8), (1.8, 1.5), (1.3, 1.1), (1.0, 0.85), (0.8, 0.7)],
                   fingers=[(-20, 3.6, 0.45, 2.4, 0.7), (0, 6.4, 0.5, 2.9, 0.7), (16, 5.0, 0.4, 2.3, 0.7)], splint=None),
}


def claw(base, d, length, r_base, curl, down=(0.0, 0.0, -1.0), sides=10, steps=12):
    """A curved, pointed claw from base along d, hooking toward `down` by curl (0 straight, 1 a quarter circle)."""
    d = norm(d)
    n = norm(sub(down, mul(d, sum(a * b for a, b in zip(down, d)))))     # the side it curls toward
    pts = []
    for k in range(steps + 1):
        u = k / steps
        a = curl * u * math.pi / 2
        pts.append(add(base, add(mul(d, length * math.sin(a) / (curl * math.pi / 2) if curl else length * u),
                                 mul(n, length * (1 - math.cos(a)) / (curl * math.pi / 2) if curl else 0.0))))
    P = Piece()
    rings = []
    for k, p in enumerate(pts[:-1]):
        t = norm(sub(pts[k + 1], p))
        a = norm(cross(t, n))
        b = cross(t, a)
        r = r_base * (1 - k / steps) ** 0.9 + 0.06 * r_base
        rings.append([P.add_v(add(p, add(mul(a, r * math.cos(2 * math.pi * j / sides)), mul(b, r * 0.85 * math.sin(2 * math.pi * j / sides)))))
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


def turn_h(d, deg):
    """d turned about the vertical by deg (positive toward +Y)."""
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return (d[0] * c - d[1] * s, d[0] * s + d[1] * c, d[2])


def arm(key, side, sgn):
    J = lb.joints(key)
    g = lambda n: J[f"{n}_{side}"] if f"{n}_{side}" in J else (J[f"{n}_l"][0], -J[f"{n}_l"][1], J[f"{n}_l"][2])
    S, E, W, F = g("upperarm"), g("lowerarm"), g("hand"), g("finger_end")
    spec = ARMS[key]
    sz = spec["sizes"]
    inside = (S[0] - 0.1 * (E[2] - S[2]), S[1] * 0.6, S[2] + 0.12 * abs(E[2] - S[2]))   # the shoulder, inside the body
    pts = [inside, S, lerp(S, E, 0.5), E, lerp(E, W, 0.55), W]
    pieces = [tube(pts, sz, sides=18, per_seg=5)]
    h = norm(sub(F, W))
    palm_end = add(W, mul(h, 0.45 * math.dist(W, F)))
    pieces.append(tube([W, palm_end], [sz[-1], (sz[-1][0] * 0.9, sz[-1][1] * 1.15)], sides=14, per_seg=3))
    down = (0.0, 0.0, -1.0)
    for deg, L, r, cl, curl in spec["fingers"]:
        d = norm(turn_h(h, sgn * deg))
        base = add(palm_end, (0.0, sgn * math.radians(deg) * r * 1.4, 0.0))
        # Each finger bends down a little at its middle knuckle, then the claw hooks on down.
        mid = add(add(base, mul(d, 0.55 * L)), (0.0, 0.0, -0.12 * L))
        tip = add(add(base, mul(d, L)), (0.0, 0.0, -0.35 * L))
        pieces.append(tube([base, mid, tip], [(r, r), (r * 0.9, r * 0.85), (r * 0.75, r * 0.7)], sides=12, per_seg=4))
        pieces[-1].kind = "toe"
        cd = norm(add(sub(tip, mid), (0.0, 0.0, -0.2 * L)))
        pieces.append(claw(tip, cd, cl, r * 0.8, curl, down))
        pieces[-1].kind = "claw"
    if spec.get("splint"):
        deg, L, r = spec["splint"]
        d = norm(turn_h(h, sgn * deg))
        base = add(W, mul(h, 0.3 * math.dist(W, F)))
        pieces.append(tube([base, add(add(base, mul(d, L)), (0.0, 0.0, -0.3 * L))], [(r, r), (r * 0.7, r * 0.7)], sides=10, per_seg=2))
        pieces[-1].kind = "toe"
    return pieces


def tail_club(key):
    """The Ankylosaurus club: lobes of fused bony plates around the last tail bone (Unreal axes, cm)."""
    J = lb.joints(key)
    c = J["tail_club"]
    x, z = c[0], c[2] + 1.0
    pieces = [blob((x + 9, 0.0, z + 1), (26, 12, 15), lat=10, lon=18, exponent=2.3)]          # the core the tail runs into
    for s in (-1, 1):
        pieces.append(blob((x + 2, s * 17, z), (24, 18, 14), lat=10, lon=18, exponent=2.3))    # the two big side lobes
        pieces.append(blob((x - 19, s * 8, z - 1), (11, 10, 11), lat=8, lon=14, exponent=2.3))  # the two small end lobes
    return pieces


def flat_blob(center, along, normal, half, lat=6, lon=12, exponent=2.2):
    """A thin superellipsoid lying in the plane through `along` with the given `normal`: half = (length, width,
    thickness) halves. For feathers."""
    a = norm(along)
    c = norm(sub(normal, mul(a, sum(x * y for x, y in zip(normal, a)))))
    w = cross(c, a)
    P = blob((0.0, 0.0, 0.0), half, lat=lat, lon=lon, exponent=exponent)
    P.v = [add(center, add(mul(a, x), add(mul(w, y), mul(c, z)))) for x, y, z in P.v]
    return P.make_outward()


def wing_feathers(key, side, sgn):
    """The Velociraptor's folded wing: secondaries along the forearm and longer primaries along the hand, trailing
    back and down beside the body like a bird's folded wing (Reference Library Velociraptor #2). The v1 feathers
    were two flat planks."""
    J = lb.joints(key)
    g = lambda n: J[f"{n}_{side}"] if f"{n}_{side}" in J else (J[f"{n}_l"][0], -J[f"{n}_l"][1], J[f"{n}_l"][2])
    E, W, F = g("lowerarm"), g("hand"), g("finger_end")
    d = norm((-1.0, sgn * 0.3, -0.5))
    pieces = []
    rows = [(E, W, 6, (6.0, 8.5)), (W, F, 5, (11.0, 9.0))]      # (from, to, feathers, length at start and end)
    for A, B, n, (l0, l1) in rows:
        arm = norm(sub(B, A))
        normal = cross(arm, d)
        for k in range(n):
            t = (k + 0.5) / n
            root = add(lerp(A, B, t), (0.0, sgn * 0.4, -0.6))
            L = l0 + (l1 - l0) * t
            pieces.append(flat_blob(add(root, mul(d, 0.5 * L)), d, normal, (0.5 * L, 0.11 * L + 0.6, 0.12)))
    return pieces


def ptera_digits(key, side, sgn):
    """Pteranodon hands and feet, which ended in a plain tube and a paddle: three small clawed fingers at the
    knuckle the folded wing stands on, turned out and back along the ground, and four slender clawed toes."""
    J = lb.joints(key)
    g = lambda n: J[f"{n}_{side}"] if f"{n}_{side}" in J else (J[f"{n}_l"][0], -J[f"{n}_l"][1], J[f"{n}_l"][2])
    hand, foot = [], []
    K = g("wingfinger_01")
    for deg, L in ((60, 7.0), (95, 8.0), (130, 6.5)):              # out to the side and back, as the prints show
        a = math.radians(deg)
        d = norm((math.cos(a), sgn * math.sin(a), -0.15))
        base = (K[0], K[1], max(K[2], 2.0))
        tip = add(base, mul(d, L))
        tip = (tip[0], tip[1], 1.0)
        hand.append(tube([base, lerp(base, tip, 0.5), tip], [(1.1, 1.1), (0.9, 0.9), (0.7, 0.7)], sides=10, per_seg=3))
        hand.append(claw(tip, d, 2.2, 0.6, 0.6))
        hand[-2].kind, hand[-1].kind = "toe", "claw"
    B, E = g("ball"), g("toe_end")
    fwd = norm((E[0] - B[0], E[1] - B[1], 0.0))
    for deg, k in ((-18, 0.85), (-6, 1.0), (6, 1.0), (18, 0.85)):
        d = norm(turn_h(fwd, sgn * deg))
        L = 1.3 * math.dist(B, E) * k
        base = (B[0], B[1], max(B[2], 1.2))
        tip = add(base, mul(d, L))
        tip = (tip[0], tip[1], 0.8)
        foot.append(tube([base, lerp(base, tip, 0.5), tip], [(1.0, 1.0), (0.8, 0.8), (0.6, 0.6)], sides=10, per_seg=3))
        foot.append(claw(tip, d, 1.8, 0.5, 0.5))
        foot[-2].kind, foot[-1].kind = "toe", "claw"
    return hand, foot


# Faces (Raven 2026-11-03: eyes, eyelids that blink, face contour details). Along the head mesh (0 back .. 1 snout tip)
# and up its side (0 mouth line .. 1 top): the eye, its radius (cm) and the nostril (along, up, size cm). The eyes look
# a little forward (bone_forge_skin.SKIN eye_fwd). Brows, sockets, cheekbones and the rest of the skull's shape are
# sculpted into the head itself (face_sculpt.py), around these same eye places.
FACE = {
    # From the skull pictures in the design doc (Reference Library, Mouths and Feet tabs), as shares of the skull:
    # the eyeball fills about 80% of the eye socket. T. rex: socket ~8% of skull length, upper back third.
    "trex": dict(eye=(0.29, 0.74), r=7.5, nostril=(0.93, 0.60, 5.0)),
    # Triceratops: a small round socket just under and behind the brow horn's base (anchor: offset in cm from it).
    "trike": dict(eye=(0.45, 0.55), r=5.5, anchor=("horn_brow", -4.0, -14.0), nostril=(0.82, 0.42, 6.5)),
    # Velociraptor: a big socket (~17% of skull length) a quarter of the way from the back, a little above halfway.
    "raptor": dict(eye=(0.27, 0.60), r=2.4, nostril=(0.88, 0.58, 0.9)),
    # Ankylosaurus: a round socket ~12% of skull length, just behind the middle, halfway up.
    "ankylo": dict(eye=(0.44, 0.52), r=4.5, nostril=(0.97, 0.45, 2.6)),
    # Brachiosaurus (the Giraffatitan skull): a large socket a third of the way from the back, high, behind the arch.
    "brachio": dict(eye=(0.34, 0.63), r=6.0, nostril=(0.45, 0.84, 4.0)),
    # Pteranodon: a small socket at the very back of the long beak.
    "ptera": dict(eye=(0.12, 0.62), r=2.4, nostril=None),
}
# Eyelids: how far the upper and lower lid edges reach toward the eye's axis (degrees from it, up is +), open,
# half-closed and closed. The game shows the half and closed sets in turn to blink (BFDino).
LIDS = {"eyelid": (40.0, -42.0), "eyelid_half": (8.0, -30.0), "eyelid_closed": (-16.0, -20.0)}


def head_surface(head, x, z, span):
    """How far out the head's skin is (|Y|) at (x, z): the outermost skin of the vertices nearest that spot (Unreal
    axes). Nearest, not the widest nearby, so an eye sits down in its sculpted socket under the brow."""
    best = sorted(((v[0] - x) ** 2 + (v[2] - z) ** 2, v[1]) for v in head if v[1] > 0)[:6]
    if not best:
        return max(abs(v[1]) for v in head)
    return sum(y for _, y in best) / len(best)


def head_heights(head, x, span):
    near = [v[2] for v in head if abs(v[0] - x) < 0.02 * span] or [v[2] for v in head]
    return min(near), max(near)


def shell(c, f, u, sd, r_in, r_out, a0, a1, b0=-75.0, b1=75.0, na=10, nb=14):
    """A closed lid: a thick spherical shell round centre c between angles a0..a1 from the eye's axis f toward up u
    (degrees), and b0..b1 round toward the side sd."""
    P = Piece()

    def pt(r, a, b):
        a, b = math.radians(a), math.radians(b)
        d = add(mul(add(mul(f, math.cos(a)), mul(u, math.sin(a))), math.cos(b)), mul(sd, math.sin(b)))
        return add(c, mul(d, r))
    grid = {}
    for side, r in (("o", r_out), ("i", r_in)):
        for i in range(na + 1):
            for j in range(nb + 1):
                grid[(side, i, j)] = P.add_v(pt(r, a0 + (a1 - a0) * i / na, b0 + (b1 - b0) * j / nb))
    for i in range(na):
        for j in range(nb):
            P.quad(grid[("o", i, j)], grid[("o", i + 1, j)], grid[("o", i + 1, j + 1)], grid[("o", i, j + 1)])
            P.quad(grid[("i", i, j + 1)], grid[("i", i + 1, j + 1)], grid[("i", i + 1, j)], grid[("i", i, j)])
    edges = [[(i, 0) for i in range(na + 1)], [(na, j) for j in range(nb + 1)],
             [(i, nb) for i in range(na, -1, -1)], [(0, j) for j in range(nb, -1, -1)]]
    for e in edges:      # the rims, closing the shell
        for (i0, j0), (i1, j1) in zip(e, e[1:]):
            P.quad(grid[("o", i0, j0)], grid[("i", i0, j0)], grid[("i", i1, j1)], grid[("o", i1, j1)])
    return P.make_outward()


def face(key, head, objs=None):
    """Eyes, the three eyelid sets, brow ridges and nostrils, from the head mesh's vertices (Unreal axes)."""
    sp = FACE[key]
    fwd = math.radians(skin.SKIN[key]["eye_fwd"])
    x0, x1 = min(v[0] for v in head), max(v[0] for v in head)
    span = x1 - x0
    parts = {}
    for side, sgn in (("l", -1), ("r", 1)):
        at, up = sp["eye"]
        x = x0 + at * span
        zb, zt = head_heights(head, x, span)
        z = zb + up * (zt - zb)
        r = sp["r"]
        if sp.get("anchor") and objs:      # under a horn's base: the base is the horn piece's last vertex
            name, dx, dz = sp["anchor"]
            bases = [(v[-1][0], v[-1][2], v[-1][1]) for n, v, _ in objs if n.rsplit("_", 1)[0] == name]
            mine = [q for q in bases if q[1] * sgn > 0]
            if mine:
                x, z = mine[0][0] + dx, mine[0][2] + dz
        f = norm((math.sin(fwd), sgn * math.cos(fwd), 0.1))
        u = norm(sub((0.0, 0.0, 1.0), mul(f, f[2])))
        sd = cross(f, u)
        y = sgn * head_surface(head, x, z, span)
        c = add((x, y, z), mul(f, -0.45 * r))
        parts[f"eye_{side}"] = [blob(c, (r, r, r), lat=36, lon=56, exponent=2.0)]      # fine enough for a round pupil
        for name, (up_edge, low_edge) in LIDS.items():
            parts[f"{name}_{side}"] = [shell(c, f, u, sd, 1.03 * r, 1.17 * r, up_edge, 150.0),
                                       shell(c, f, u, sd, 1.03 * r, 1.15 * r, -150.0, low_edge)]
        if sp["nostril"]:
            at_n, up_n, size = sp["nostril"]
            nx = x0 + at_n * span
            nzb, nzt = head_heights(head, nx, span)
            nz = nzb + up_n * (nzt - nzb)
            ny = sgn * (head_surface(head, nx, nz, span) - 0.15 * size)
            parts[f"nostril_{side}"] = [blob((nx, ny, nz), (0.6 * size, 0.3 * size, 0.38 * size), lat=8, lon=14, exponent=2.0)]
    return parts


def build(key, head=None, objs=None):
    parts = {}
    if head and key in FACE:
        parts.update(face(key, head, objs))
    if key in NECK_BLEND:
        parts["neck_blend"] = [loft(NECK_BLEND[key], 2.5, 36, 2.8, 2.3)]      # boxy on top, like the skulls
    if key in ARMS:
        for side, sgn in (("l", -1), ("r", 1)):
            parts.update(lb.digits("arm", side, arm(key, side, sgn)))
    if key == "ankylo":
        parts["tail_club"] = tail_club(key)
    if key == "raptor":
        for side, sgn in (("l", -1), ("r", 1)):
            parts[f"wing_feathers_{side}"] = wing_feathers(key, side, sgn)
    if key == "ptera":
        for side, sgn in (("l", -1), ("r", 1)):
            hand, foot = ptera_digits(key, side, sgn)
            parts.update(lb.digits("arm", side, hand))
            parts.update(lb.digits("leg", side, foot))
    return parts


FACE_STEMS = ("eye_", "eyelid", "brow", "nostril")
# Parts an earlier build had that these replace (the raptor's v1 feather planks, the T. rex blend piece).
DROP = {"raptor": {"wing_feathers"}, "trex": {"neck_blend"}}


def main(argv):
    for key in argv or ["trex", "trike", "raptor", "brachio", "ankylo", "ptera"]:
        path = os.path.join(ROOT, "body_shapes", f"{key}_body.obj")
        header, objs = lb.read_obj(path)
        head = next(([(v[0], v[2], v[1]) for v in verts] for name, verts, _ in objs if name.rsplit("_", 1)[0] == "head"), None)
        parts = build(key, head, objs)
        out, done = [], set()
        for name, verts, faces in objs:
            stem = name.rsplit("_", 1)[0] if name.rsplit("_", 1)[-1].isdigit() else name
            if stem in DROP.get(key, ()) or (stem.startswith(FACE_STEMS) and stem not in parts):
                continue        # replaced, or a face part this build no longer makes
            if stem in parts:
                verts, faces = lb.pieces_to_obj(parts[stem])
                done.add(stem)
            out.append((name, verts, faces))
        for stem in parts:
            if stem not in done:
                out.append((stem, *lb.pieces_to_obj(parts[stem])))
        if not any("parts_builder" in h for h in header):
            header.append("# Neck blend, grasping hands and the tail club from body_tools/parts_builder.py.")
        lb.write_obj(path, header, out)
        print(key + ": " + ", ".join(f"{s} ({sum(len(P.v) for P in ps)} v, "
                                     f"{'outward' if all(P.volume_obj() > 0 for P in ps) else 'CHECK'})" for s, ps in parts.items()))


if __name__ == "__main__":
    main(sys.argv[1:])
