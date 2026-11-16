exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(load_skeleton("trex.py"))
S = 85
C = lambda pts, n=6: curve_pts(pts, n)
import model_limbs as ml
X0 = 5.956   # drawing x of the rig origin (the 3D body's tail tip, -603.6 cm, sits at -0.08 here)
hs = lambda pts, cx, cy: [(cx + (x - cx) * HEAD, cy + (y - cy) * HEAD) for x, y in pts]   # same head enlargement as the skeleton

def life_side(OX, OY):
    # Legs and feet traced from the 3D model (body_tools/leg_builder.py); the far leg a step ahead.
    far_leg = ml.side_outline("trex", "leg", X0 + 0.35, cut=3.1)
    far_arm = C([(8.62, 2.62), (8.72, 2.22), (8.98, 2.12), (9.06, 2.02), (8.92, 1.98), (8.62, 2.05), (8.48, 2.30), (8.44, 2.62)], 4)
    body = C([(-0.08, 1.71), (1.5, 2.62), (3.2, 3.42), (4.8, 3.95), (6.0, 4.18), (7.2, 4.14), (8.3, 3.86), (9.0, 3.66), (9.5, 3.88), (9.85, 4.0),
              (10.1, 3.4), (10.0, 2.95), (9.3, 2.72), (8.9, 2.52), (8.75, 2.25), (8.45, 1.92), (7.8, 1.60), (7.0, 1.62), (6.6, 1.90), (5.6, 2.30),
              (4.8, 2.50), (3.2, 2.30), (1.5, 1.88), (-0.08, 1.71)], 6)
    near_leg = ml.side_outline("trex", "leg", X0, cut=3.25)
    near_arm = C([(8.72, 2.66), (8.82, 2.26), (9.06, 2.16), (9.16, 2.08), (9.12, 1.98), (8.94, 2.0), (8.66, 2.04), (8.54, 2.28), (8.48, 2.66)], 4)
    head = hs(C([(9.75, 3.32), (9.80, 3.68), (9.95, 4.08), (10.35, 4.10), (10.9, 3.86), (11.4, 3.52), (11.62, 3.22), (11.6, 3.0),
                 (11.52, 2.86), (11.1, 2.76), (10.5, 2.88), (10.0, 3.05), (9.75, 3.32)], 6), 9.92, 3.32)
    parts = [(far_leg, "open", "#8f8f8f", 2.2), (far_arm, "open", "#8f8f8f", 2.0), (body, "closed", INK, 2.8),
             (near_leg, "open", INK, 2.8), (near_arm, "open", INK, 2.4), (head, "closed", INK, 3.0)]
    def tex():
        scales([body, near_leg, near_arm], OX, OY, 0.26, 0.045, exclude=[head], seed=11,
               big=lambda x, y: 1.0 + 0.5 * max(0, y - 2.8))
        scales([head], OX, OY, 0.15, 0.028, seed=12)
    render_parts(parts, OX, OY, side, tex)
    ground(OX, OY, -0.2, 12.3)
    # face: lips over the teeth, eye, nostril, brow ridge
    fold(hs(C([(10.05, 3.04), (10.6, 2.95), (11.2, 2.92), (11.58, 2.98)], 5), 9.92, 3.32), OX, OY, sw=1.8, stroke=INK)
    ex, ey = hs([(10.25, 3.62)], 9.92, 3.32)[0]
    ellipse(ex, ey, 0.075, 0.06, OX, OY, sw=2.0); ellipse(ex + 0.01, ey, 0.03, 0.03, OX, OY, fill=INK, sw=1)
    fold(hs(C([(10.12, 3.78), (10.28, 3.82), (10.42, 3.76)], 3), 9.92, 3.32), OX, OY, sw=2.0, stroke=INK)
    nx, ny = hs([(11.32, 3.25)], 9.92, 3.32)[0]
    ellipse(nx, ny, 0.06, 0.035, OX, OY, rot=-20, sw=1.6)
    # skin folds and claws
    fold(C([(9.0, 3.3), (9.25, 3.05), (9.45, 2.85)], 4), OX, OY)
    fold(C([(5.95, 3.4), (6.3, 3.15), (6.7, 3.2)], 4), OX, OY)
    fold(C([(6.25, 1.25), (6.45, 1.25)], 3), OX, OY)
    for base, ang, L, w in ml.side_claws("trex", "leg", X0):
        claw(base, ang, L, w * 0.6, OX, OY, curl=6)
    text(OX, 150, "SIDE VIEW", 22)

def life_front(OX, OY):
    body = C(mirror([(0.0, 3.95), (0.55, 3.80), (0.78, 3.2), (0.74, 2.4), (0.52, 1.85), (0.0, 1.70)]), 6)
    legs = [ml.front_outline("trex", "leg", s, (3.6, 1.9, 0.45)) for s in (1, -1)]
    arms = [C([(0.40 * s, 2.55), (0.46 * s, 2.15), (0.52 * s, 2.03), (0.60 * s, 2.06), (0.60 * s, 2.2), (0.54 * s, 2.55)], 4) for s in (1, -1)]
    head = [(x * HEAD, 3.0 + (y - 3.0) * HEAD) for x, y in C(mirror([(0.0, 4.10), (0.42, 3.98), (0.52, 3.52), (0.40, 3.02), (0.20, 2.80), (0.0, 2.76)]), 6)]
    parts = [(body, "closed", INK, 2.8)] + [(l, "open", INK, 2.8) for l in legs] + [(a, "open", INK, 2.2) for a in arms] + [(head, "closed", INK, 3.0)]
    def tex():
        scales([body] + legs, OX, OY, 0.22, 0.04, exclude=[head] + arms, seed=13, big=lambda x, y: 1.0 + 0.5 * max(0, y - 2.8))
        scales([head], OX, OY, 0.14, 0.026, seed=14)
    render_parts(parts, OX, OY, front, tex)
    ground(OX, OY, -1.1, 1.1)
    for s in (1, -1):
        ellipse(0.33 * s * HEAD, 3.0 + 0.62 * HEAD, 0.06, 0.05, OX, OY, sw=2.0); ellipse(0.33 * s * HEAD, 3.0 + 0.62 * HEAD, 0.025, 0.025, OX, OY, fill=INK, sw=1)
        ellipse(0.10 * s * HEAD, 3.0 + 0.08 * HEAD, 0.035, 0.03, OX, OY, sw=1.5)
        for base, ang, L, w in ml.front_claws("trex", "leg", s):
            claw(base, ang, L * 0.8, w * 0.45, OX, OY, curl=0)
    fold(C([(-0.32, 2.95), (0.0, 2.88), (0.32, 2.95)], 4), OX, OY, sw=1.8, stroke=INK)
    text(OX, OY + 32, "FRONT VIEW", 22, "600", "middle")

W, H = 1500, 640
life_side(50, 560)
life_front(1340, 560)
text(50, 60, "TYRANNOSAURUS REX · LIFE REFERENCE", 28, "700")
text(50, 88, "Body over the skeleton · mostly scaly skin, lips covering the teeth · about 12.5 m long", 16, "400", "start", "#555")
text(W - 40, 625, "Faint grey = skeleton inside the body", 15, "400", "end", "#777")
save("trex_life.svg", W, H)
print("ok")
