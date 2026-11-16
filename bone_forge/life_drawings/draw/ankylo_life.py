exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(load_skeleton("ankylo.py"))
S = 155
import model_limbs as ml
X0 = 2.965   # drawing x of the rig origin (the 3D body's tail tip, -260.5 cm, sits at 0.36 here)
C = lambda pts, n=6: curve_pts(pts, n)

def scute(cx, cy, rx, ry, OX, OY, rot=0, spike=0.0):
    """Armor plate drawn in front: white oval, dark rim, centre keel, optional spike."""
    ellipse(cx, cy, rx, ry, OX, OY, rot=rot, sw=1.8)
    a = math.radians(rot); ux, uy = math.cos(a), math.sin(a)
    fold([(cx - ux * rx * 0.55, cy - uy * rx * 0.55), (cx + ux * rx * 0.55, cy + uy * rx * 0.55)], OX, OY, sw=1.1, stroke="#777")
    if spike:
        poly([(cx - rx * 0.6, cy + ry * 0.2), (cx + rx * 0.6, cy + ry * 0.2), (cx + rx * 0.1, cy + ry + spike)], OX, OY, sw=1.6)

def life_side(OX, OY):
    # Legs and feet traced from the 3D model (body_tools/leg_builder.py); the far legs a step ahead.
    far_hind = ml.side_outline("ankylo", "leg", X0 + 0.10, cut=1.30)
    far_front = ml.side_outline("ankylo", "arm", X0 + 0.08, cut=1.05)
    body = C([(0.36, 0.95), (1.0, 1.14), (1.9, 1.34), (2.6, 1.66), (3.4, 1.86), (4.2, 1.90), (4.9, 1.78), (5.4, 1.60), (5.75, 1.50),
              (5.95, 1.15), (5.85, 0.92), (5.3, 0.62), (4.6, 0.48), (3.8, 0.48), (3.1, 0.62), (2.4, 0.82), (1.6, 0.86), (0.9, 0.80), (0.36, 0.71)], 6)
    club = C([(0.46, 0.83), (0.38, 1.00), (0.18, 1.04), (-0.03, 0.88), (-0.01, 0.66), (0.18, 0.58), (0.40, 0.63)], 6)
    near_hind = ml.side_outline("ankylo", "leg", X0, cut=1.30)
    near_front = ml.side_outline("ankylo", "arm", X0, cut=1.05)
    head = C([(5.66, 1.06), (5.64, 1.40), (5.95, 1.52), (6.30, 1.42), (6.52, 1.22), (6.52, 1.03), (6.30, 0.92), (5.95, 0.90), (5.66, 1.06)], 6)
    parts = [(far_hind, "open", "#8f8f8f", 2.2), (far_front, "open", "#8f8f8f", 2.2), (body, "closed", INK, 2.8), (club, "closed", INK, 2.8),
             (near_hind, "open", INK, 2.8), (near_front, "open", INK, 2.8), (head, "closed", INK, 3.0)]
    hv = {part: ml.side_hoof_shapes("ankylo", part, X0) for part in ("leg", "arm")}
    def tex():
        scales([body, near_hind, near_front], OX, OY, 0.16, 0.03, exclude=[head] + hv["leg"] + hv["arm"], seed=31)
        scales([head, club], OX, OY, 0.10, 0.02, seed=32)
    render_parts(parts, OX, OY, side, tex)
    ground(OX, OY, -0.2, 6.8)
    # armor on top of the skin: rows of keeled plates, neck half-rings, side spikes
    top_edge = [p for p in body if p[1] > 1.0 and 0.3 < p[0] < 5.9]
    def top_y(x):
        best = min(top_edge, key=lambda p: abs(p[0] - x))
        return best[1]
    for row, (dy, k, sp) in enumerate([(-0.07, 1.0, 0.0), (-0.36, 0.9, 0.0), (-0.62, 0.75, 0.12)]):
        for i in range(12):
            x = 2.35 + i * 0.27 + (0.13 if row % 2 else 0)
            if x > 5.25:
                continue
            yb = top_y(x) + dy
            if row == 2 and not (2.5 < x < 5.1):
                continue
            scute(x, yb, 0.11 * k, 0.06 * k, OX, OY, rot=-6 * (x - 3.8), spike=sp)
    for x, y in [(5.42, 1.52), (5.66, 1.42)]:
        poly(C([(x - 0.13, y - 0.13), (x - 0.07, y + 0.07), (x + 0.07, y + 0.07), (x + 0.13, y - 0.13)], 5), OX, OY, sw=2.2, fill="none", close=False)
    for i, x in enumerate([0.6 + j * 0.32 for j in range(6)]):
        y = top_y(x) - 0.02
        poly([(x - 0.07, y), (x + 0.07, y), (x - 0.02, y + 0.14)], OX, OY, sw=1.6)
    # head detail: horns, eye, beak, cheek armor
    poly([(5.75, 1.36), (5.60, 1.48), (5.58, 1.40), (5.72, 1.28)], OX, OY, sw=1.8)
    poly([(5.78, 1.02), (5.62, 0.94), (5.66, 0.88), (5.82, 0.96)], OX, OY, sw=1.8)
    ellipse(5.98, 1.18, 0.05, 0.045, OX, OY, sw=2.0); ellipse(5.99, 1.18, 0.02, 0.02, OX, OY, fill=INK, sw=1)
    fold(C([(5.85, 0.98), (6.15, 0.96), (6.48, 1.02)], 4), OX, OY, sw=1.6, stroke=INK)
    ellipse(6.38, 1.12, 0.035, 0.025, OX, OY, sw=1.4)
    for x in (6.0, 6.15, 6.3):
        ellipse(x, 1.36, 0.06, 0.035, OX, OY, sw=1.4)
    # toenails
    for part, leg in (("leg", near_hind), ("arm", near_front)):     # hooves on the near feet, from the model
        for r in ml.hoof_lines(hv[part], leg, 1.2 / S):
            outline(r, OX, OY, sw=1.5, close=False)
    text(OX, 150, "SIDE VIEW", 22)

def life_front(OX, OY):
    body = C(mirror([(0.0, 1.70), (0.80, 1.58), (1.10, 1.20), (1.02, 0.75), (0.70, 0.55), (0.0, 0.50)]), 6)
    legs = [ml.front_outline("ankylo", "arm", s, 1.0) for s in (1, -1)]
    head = C(mirror([(0.0, 1.38), (0.36, 1.34), (0.42, 1.12), (0.32, 0.95), (0.0, 0.88)]), 6)
    parts = [(body, "closed", INK, 2.8)] + [(l, "open", INK, 2.8) for l in legs] + [(head, "closed", INK, 3.0)]
    hv = [ml.front_hoof_shapes("ankylo", "arm", s) for s in (1, -1)]
    def tex():
        scales([body] + legs, OX, OY, 0.13, 0.025, exclude=[head] + hv[0] + hv[1], seed=33)
        scales([head], OX, OY, 0.09, 0.018, seed=34)
    render_parts(parts, OX, OY, front, tex)
    ground(OX, OY, -1.4, 1.4)
    for t_ in range(9):   # armor plates along the top arc, side spikes sticking out
        a = math.radians(18 + t_ * 18)
        x, y = math.cos(a) * 0.95, 1.08 + math.sin(a) * 0.58
        scute(x, y, 0.08, 0.05, OX, OY, rot=math.degrees(a) - 90)
    for s in (1, -1):
        poly([(1.05 * s, 1.18), (1.05 * s, 1.05), (1.25 * s, 1.10)], OX, OY, sw=1.6)
        poly([(0.36 * s, 1.34), (0.50 * s, 1.44), (0.42 * s, 1.24)], OX, OY, sw=1.8)
        poly([(0.40 * s, 1.0), (0.52 * s, 0.94), (0.36 * s, 0.94)], OX, OY, sw=1.8)
        ellipse(0.22 * s, 1.16, 0.045, 0.04, OX, OY, sw=2.0); ellipse(0.22 * s, 1.16, 0.018, 0.018, OX, OY, fill=INK, sw=1)
        ellipse(0.07 * s, 0.97, 0.025, 0.02, OX, OY, sw=1.3)
        for r in ml.hoof_lines(hv[0 if s > 0 else 1], legs[0 if s > 0 else 1], 1.2 / S):
            outline(r, OX, OY, sw=1.5, close=False)
    fold(C([(-0.18, 0.92), (0.0, 0.89), (0.18, 0.92)], 4), OX, OY, sw=1.8, stroke=INK)
    text(OX, OY + 32, "FRONT VIEW", 22, "600", "middle")

W, H = 1560, 560
life_side(50, 470)
S = 165
life_front(1340, 470)
text(50, 60, "ANKYLOSAURUS MAGNIVENTRIS · LIFE REFERENCE", 28, "700")
text(50, 88, "Body over the skeleton · small scales covered by rows of keeled armor plates, side spikes and a bony tail club · about 7 m long", 16, "400", "start", "#555")
text(W - 40, 548, "Faint grey = skeleton inside the body", 15, "400", "end", "#777")
save("ankylo_life.svg", W, H)
print("ok")
