exec(open("lib.py").read())
exec(open("life_lib.py").read())
src = open("trike.py").read()
exec(src[src.index("def side("):src.index("# ---------------- TOP VIEW")])
exec(src[src.index("def front("):src.index("\ndef foot_top")])
S = 120
C = lambda pts, n=6: curve_pts(pts, n)
import model_limbs as ml
X0 = 3.365   # drawing x of the rig origin (the 3D body's tail tip, -336.5 cm, sits at 0.0 here)
sq = lambda pts: [(((6.55 + (x - 6.55) * SNOUT) if x > 6.55 else x) + HEAD_DX, y) for x, y in pts]

# ---------------- SIDE VIEW ----------------
def life_side(OX, OY):
    # Legs and feet traced from the 3D model (body_tools/leg_builder.py); the far legs a step ahead.
    far_hind = ml.side_outline("trike", "leg", X0 + 0.10, cut=1.80)
    far_front = ml.side_outline("trike", "arm", X0 + 0.07, cut=1.50)
    body = C([(0.0, 0.93), (0.5, 1.42), (1.0, 1.80), (1.6, 2.14), (2.2, 2.44), (2.8, 2.66), (3.4, 2.78), (4.0, 2.80), (4.6, 2.76), (5.2, 2.58), (5.7, 2.36), (6.1, 2.10),
              (6.35, 1.80), (6.40, 1.45), (6.25, 1.15), (5.85, 0.98), (5.3, 0.88), (4.7, 0.84), (4.1, 0.92), (3.6, 1.08), (3.1, 1.28), (2.6, 1.42), (2.1, 1.40),
              (1.6, 1.28), (1.0, 1.12), (0.5, 0.98), (0.0, 0.93)], 6)
    near_hind = ml.side_outline("trike", "leg", X0, cut=1.92)
    near_front = ml.side_outline("trike", "arm", X0, cut=1.55)
    head = sq(C([(6.0, 1.50), (5.88, 2.30), (6.0, 3.02), (6.36, 3.25), (6.65, 3.02), (6.86, 2.50), (7.12, 2.10), (7.58, 1.80), (8.08, 1.50), (8.52, 1.18), (8.72, 0.90),
                 (8.50, 0.74), (8.10, 0.82), (7.60, 0.80), (7.10, 0.90), (6.70, 1.12), (6.40, 1.30), (6.0, 1.50)], 6))
    for sh in (far_hind, far_front, body, near_hind, near_front, head):
        fill_shape(sh, OX, OY)
    g0 = len(out); side(OX, OY); ghost(g0)
    out.append(f'<line x1="{OX-20}" y1="{OY}" x2="{OX+8.3*S}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')
    hv = {part: ml.side_hoof_shapes("trike", part, X0) for part in ("leg", "arm")}
    scales([body, near_hind, near_front], OX, OY, 0.24, 0.040, exclude=[head] + hv["leg"] + hv["arm"], seed=3,
           big=lambda x, y: 1.0 + 0.6 * max(0, (y - 1.6)) + (0.4 if 2.5 < x < 5.5 and 1.5 < y < 2.4 else 0))
    scales([head], OX, OY, 0.17, 0.028, seed=5)
    # outlines, back to front, each hidden where a nearer part covers it
    visible(open_edge(far_hind), [body, near_hind, near_front, head], OX, OY, stroke="#8f8f8f", sw=2.2, closed=False)
    visible(open_edge(far_front), [body, near_hind, near_front, head], OX, OY, stroke="#8f8f8f", sw=2.2, closed=False)
    visible(body, [near_hind, near_front, head], OX, OY)
    visible(open_edge(near_hind), [near_front, head], OX, OY, closed=False)
    visible(open_edge(near_front), [head], OX, OY, closed=False)
    visible(head, [], OX, OY, sw=3.0)
    for (x, y) in head[3:30:3]:
        if y > 1.9:
            ellipse(x, y, 0.045, 0.045, OX, OY, sw=1.6)
    hk = len(out)
    poly([(7.0, 1.72), (7.25, 1.92), (8.40, 2.72), (8.52, 2.80), (8.36, 2.62), (7.30, 1.70)], OX, OY, sw=2.6)
    poly([(7.06, 1.74), (7.30, 1.88), (8.45, 2.82), (8.56, 2.90), (8.42, 2.70), (7.36, 1.72)], OX, OY, stroke="#8f8f8f", sw=2.0)
    poly([(7.92, 1.44), (8.08, 1.68), (8.20, 1.82), (8.18, 1.62), (8.13, 1.40)], OX, OY, sw=2.2)
    fold(C([(8.15, 0.85), (8.45, 0.88), (8.70, 0.92)], 4), OX, OY, sw=1.8, stroke=INK)
    fold(C([(6.80, 1.15), (7.40, 0.98), (8.10, 0.90)], 5), OX, OY, sw=1.6)
    ellipse(7.12, 1.55, 0.075, 0.07, OX, OY, sw=2.0); ellipse(7.13, 1.555, 0.03, 0.03, OX, OY, fill=INK, sw=1)
    ellipse(8.08, 1.14, 0.06, 0.04, OX, OY, rot=-30, sw=1.6)
    squash(hk, 6.55, OX, OY, SNOUT)
    shift(hk, HEAD_DX, OX, OY)
    fold(C([(5.85, 2.0), (6.0, 1.65), (6.02, 1.30)], 4), OX, OY)
    fold(C([(5.55, 1.62), (5.75, 1.35), (5.80, 1.05)], 4), OX, OY)
    fold(C([(3.05, 1.95), (3.40, 1.75), (3.75, 1.78)], 4), OX, OY)
    fold(C([(3.55, 0.88), (3.72, 0.82)], 3), OX, OY); fold(C([(5.62, 0.66), (5.85, 0.66)], 3), OX, OY)
    for part, leg in (("leg", near_hind), ("arm", near_front)):     # hooves on the near feet, from the model
        for r in ml.hoof_lines(hv[part], leg, 1.2 / S):
            outline(r, OX, OY, sw=1.6, close=False)
    text(OX, 150, "SIDE VIEW", 22)

# ---------------- FRONT VIEW ----------------
def mirror(half):
    """Build a symmetric outline from its right half (top to bottom, x >= 0)."""
    return half + [(-x, y) for x, y in reversed(half)]

def life_front(OX, OY):
    body = C(mirror([(0.0, 2.80), (0.80, 2.62), (1.22, 2.12), (1.28, 1.50), (1.08, 1.00), (0.62, 0.84), (0.0, 0.80)]), 6)
    hind = []
    for s in (1, -1):
        hind.append(ml.front_outline("trike", "leg", s, 1.40))
    front_legs = []
    for s in (1, -1):
        front_legs.append(ml.front_outline("trike", "arm", s, 1.78))
    frill = C(mirror([(0.0, 3.18), (0.70, 3.02), (1.12, 2.48), (1.12, 1.94), (0.74, 1.58), (0.0, 1.50)]), 6)
    face = C(mirror([(0.0, 2.0), (0.44, 1.88), (0.48, 1.40), (0.34, 1.02), (0.14, 0.78), (0.0, 0.73)]), 6)
    for sh in hind + [body] + front_legs + [frill, face]:
        fill_shape(sh, OX, OY)
    g0 = len(out); front(OX, OY); ghost(g0)
    out.append(f'<line x1="{OX-1.6*S}" y1="{OY}" x2="{OX+1.6*S}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')
    hv = [ml.front_hoof_shapes("trike", "arm", s) for s in (1, -1)]
    scales([body] + front_legs, OX, OY, 0.22, 0.038, exclude=[frill, face] + hv[0] + hv[1], seed=7)
    scales([frill], OX, OY, 0.17, 0.028, exclude=[face], seed=8)
    scales([face], OX, OY, 0.13, 0.024, seed=9)
    for h in hind:
        visible(open_edge(h), [body, frill, face] + front_legs, OX, OY, stroke="#8f8f8f", sw=2.2, closed=False)
    visible(body, front_legs + [frill, face], OX, OY)
    for fl in front_legs:
        visible(open_edge(fl), [frill, face], OX, OY, closed=False)
    visible(frill, [face], OX, OY, sw=3.0)
    for (x, y) in frill[2:-2:3]:
        if y > 1.95:
            ellipse(x, y, 0.045, 0.045, OX, OY, sw=1.6)
    visible(face, [], OX, OY, sw=3.0)
    for s in (1, -1):
        poly([(0.20 * s, 1.74), (0.36 * s, 1.80), (0.64 * s, 2.80), (0.60 * s, 2.86), (0.54 * s, 2.80)], OX, OY, sw=2.6)   # brow horns (keratin)
        ellipse(0.30 * s, 1.58, 0.07, 0.065, OX, OY, sw=2.0); ellipse(0.30 * s, 1.58, 0.028, 0.028, OX, OY, fill=INK, sw=1)
        ellipse(0.10 * s, 1.0, 0.035, 0.05, OX, OY, sw=1.5)                                                              # nostrils
        fold(C([(0.62 * s, 1.40), (0.60 * s, 1.10)], 3), OX, OY)                                                         # elbow crease
        for r in ml.hoof_lines(hv[0 if s > 0 else 1], front_legs[0 if s > 0 else 1], 1.2 / S):
            outline(r, OX, OY, sw=1.6, close=False)
    poly([(-0.06, 1.24), (0.06, 1.24), (0.0, 1.52)], OX, OY, sw=2.2)                                                     # nose horn
    fold(C([(-0.14, 0.86), (0.0, 0.80), (0.14, 0.86)], 4), OX, OY, sw=1.8, stroke=INK)                                   # beak
    text(OX, OY + 32, "FRONT VIEW", 22, "600", "middle")

W, H = 1500, 640
life_side(60, 560)
life_front(1270, 560)
text(60, 60, "TRICERATOPS HORRIDUS · LIFE REFERENCE", 28, "700")
text(60, 88, "Body over the skeleton · scaly skin with larger scales on the back · about 7.9 m long", 16, "400", "start", "#555")
text(W - 40, 625, "Faint grey = skeleton inside the body", 15, "400", "end", "#777")
save("trike_life.svg", W, H)
print("ok")
