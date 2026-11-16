exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(load_skeleton("raptor.py"))
S = 480
C = lambda pts, n=6: curve_pts(pts, n)

def life_side(OX, OY):
    far_leg = C([(1.18, 0.58), (1.17, 0.45), (1.165, 0.36), (1.12, 0.24), (1.105, 0.14), (1.16, 0.06), (1.22, 0.02), (1.22, 0.0), (1.10, 0.0), (1.06, 0.04), (1.055, 0.14), (1.07, 0.25), (1.10, 0.33), (1.03, 0.42), (0.97, 0.55)], 5)
    far_wing = C([(1.40, 0.45), (1.52, 0.37), (1.56, 0.345), (1.47, 0.30), (1.33, 0.29), (1.28, 0.33), (1.33, 0.42)], 5)
    body = C([(-0.03, 0.578), (0.4, 0.64), (0.9, 0.675), (1.1, 0.70), (1.3, 0.675), (1.42, 0.68), (1.50, 0.78), (1.56, 0.87), (1.60, 0.91),
              (1.65, 0.79), (1.60, 0.70), (1.53, 0.60), (1.46, 0.48), (1.35, 0.37), (1.22, 0.33), (1.08, 0.37), (0.98, 0.45),
              (0.8, 0.50), (0.4, 0.52), (-0.03, 0.555)], 6)
    tail_fan = C([(0.70, 0.665), (0.40, 0.70), (0.14, 0.71), (0.0, 0.67), (-0.07, 0.585), (0.0, 0.50), (0.14, 0.46), (0.40, 0.47), (0.70, 0.50)], 6)
    near_leg = C([(1.12, 0.58), (1.12, 0.45), (1.10, 0.36), (1.06, 0.24), (1.045, 0.14), (1.09, 0.06), (1.15, 0.02), (1.15, 0.0), (1.04, 0.0), (1.00, 0.04), (0.995, 0.14), (1.01, 0.25), (1.04, 0.33), (0.97, 0.42), (0.90, 0.55)], 5)
    near_wing = C([(1.36, 0.42), (1.50, 0.355), (1.54, 0.33), (1.45, 0.285), (1.30, 0.275), (1.24, 0.32), (1.29, 0.40)], 5)
    near_arm = C([(1.41, 0.53), (1.37, 0.41), (1.47, 0.37), (1.53, 0.34), (1.52, 0.31), (1.44, 0.32), (1.31, 0.36), (1.33, 0.49)], 4)
    head = C([(1.58, 0.84), (1.585, 0.88), (1.61, 0.918), (1.70, 0.918), (1.79, 0.888), (1.865, 0.842), (1.888, 0.812), (1.872, 0.790),
              (1.80, 0.773), (1.70, 0.783), (1.62, 0.798), (1.58, 0.84)], 6)
    parts = [(far_leg, "open", "#8f8f8f", 2.0), (far_wing, "closed", "#8f8f8f", 1.8), (tail_fan, "closed", INK, 2.2), (body, "closed", INK, 2.8),
             (near_leg, "open", INK, 2.6), (near_wing, "closed", INK, 2.2), (near_arm, "open", INK, 2.2), (head, "closed", INK, 2.8)]
    def tex():
        feather_tex([body, near_leg], OX, OY, 0.035, 0.022, 200, exclude=[head, near_wing, near_arm], seed=21)
        feather_tex([tail_fan], OX, OY, 0.03, 0.03, 182, seed=22, exclude=[body])
        scales([head], OX, OY, 0.022, 0.004, seed=23)
    render_parts(parts, OX, OY, side, tex)
    ground(OX, OY, -0.05, 1.95)
    # wing feathers (long primaries) on the near arm
    for i in range(7):
        t = i / 6
        x0, y0 = 1.33 + 0.18 * t, 0.39 - 0.05 * t
        fold([(x0, y0), (x0 - 0.07 + 0.03 * t, y0 - 0.07 - 0.01 * t)], OX, OY, sw=1.2, stroke="#777")
    # feathered thigh ("trousers") edge and tail fan quills
    for x in (0.95, 1.0, 1.05, 1.10):
        fold([(x, 0.40), (x - 0.02, 0.35)], OX, OY, sw=1.1, stroke="#777")
    for x in [0.04 + i * 0.08 for i in range(8)]:
        w = 0.12 * (1 - abs(x - 0.18) / 0.7)
        fold([(x + 0.07, 0.585 + w * 0.5), (x, 0.585 + w)], OX, OY, sw=1.0, stroke="#888")
        fold([(x + 0.07, 0.585 - w * 0.5), (x, 0.585 - w)], OX, OY, sw=1.0, stroke="#888")
    # face
    fold(C([(1.63, 0.805), (1.75, 0.795), (1.875, 0.80)], 4), OX, OY, sw=1.6, stroke=INK)
    ellipse(1.648, 0.866, 0.012, 0.011, OX, OY, sw=1.8); ellipse(1.650, 0.866, 0.005, 0.005, OX, OY, fill=INK, sw=1)
    ellipse(1.845, 0.828, 0.007, 0.005, OX, OY, rot=-20, sw=1.3)
    # the raised sickle claw and toe claws, in front
    digit((1.094, 0.034), 40, [0.022, 0.018], 0.012, OX, OY, INK, droop=-25, clawL=0.065, curl=120)
    for dx in (0.0, 0.02):
        claw((1.14 + dx, 0.012), -15, 0.024, 0.01, OX, OY, curl=40)
    text(OX, 140, "SIDE VIEW", 22)

def life_front(OX, OY):
    body = C(mirror([(0.0, 0.80), (0.036, 0.79), (0.05, 0.68), (0.10, 0.64), (0.13, 0.52), (0.10, 0.40), (0.0, 0.36)]), 6)
    legs = [C([(0.04 * s, 0.45), (0.05 * s, 0.30), (0.055 * s, 0.14), (0.05 * s, 0.03), (0.045 * s, 0.0), (0.10 * s, 0.0), (0.095 * s, 0.03),
               (0.088 * s, 0.14), (0.105 * s, 0.30), (0.118 * s, 0.46)], 5) for s in (1, -1)]
    wings = [C([(0.06 * s, 0.54), (0.125 * s, 0.44), (0.13 * s, 0.34), (0.09 * s, 0.30), (0.05 * s, 0.33), (0.04 * s, 0.44)], 5) for s in (1, -1)]
    head = C(mirror([(0.0, 0.92), (0.05, 0.905), (0.058, 0.86), (0.042, 0.80), (0.0, 0.775)]), 6)
    parts = [(body, "closed", INK, 2.6)] + [(l, "open", INK, 2.4) for l in legs] + [(w, "closed", INK, 2.2) for w in wings] + [(head, "closed", INK, 2.6)]
    def tex():
        feather_tex([body] + legs, OX, OY, 0.025, 0.016, 260, exclude=wings + [head], seed=24)
        feather_tex(wings, OX, OY, 0.022, 0.03, 265, seed=25)
    render_parts(parts, OX, OY, front, tex)
    ground(OX, OY, -0.2, 0.2)
    for s in (1, -1):
        ellipse(0.036 * s, 0.872, 0.011, 0.012, OX, OY, sw=1.8); ellipse(0.036 * s, 0.872, 0.005, 0.005, OX, OY, fill=INK, sw=1)
        ellipse(0.012 * s, 0.80, 0.005, 0.006, OX, OY, sw=1.2)
        claw((0.072 * s, 0.07), 90, 0.03, 0.012, OX, OY, curl=0)
    text(OX, OY + 32, "FRONT VIEW", 22, "600", "middle")

W, H = 1500, 680
life_side(60, 600)
life_front(1300, 600)
text(60, 60, "VELOCIRAPTOR MONGOLIENSIS · LIFE REFERENCE", 28, "700")
text(60, 88, "Body over the skeleton · fully feathered with wing feathers on the arms and a feather fan on the tail · about 2 m long", 16, "400", "start", "#555")
text(W - 40, 665, "Faint grey = skeleton inside the body", 15, "400", "end", "#777")
save("raptor_life.svg", W, H)
print("ok")
