exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(open("mouth_lib.py").read())

def mouth_side(OX, OY):
    global S
    S = 205
    hinge = (0.0, 0.0); OPEN = -20
    upper = C([(-0.42, 0.18), (-0.60, 0.75), (-0.45, 1.35), (-0.12, 1.60), (0.18, 1.40), (0.38, 1.00), (0.62, 0.70), (0.95, 0.50), (1.25, 0.32),
               (1.50, 0.15), (1.62, 0.0), (1.58, -0.10), (1.48, -0.05), (1.30, -0.02), (0.95, 0.0), (0.55, 0.03), (0.15, 0.05), (-0.15, 0.08)], 6)
    lower_c = C([(-0.08, 0.06), (0.05, -0.24), (0.55, -0.30), (1.05, -0.24), (1.40, -0.14), (1.53, -0.06), (1.50, -0.02), (1.30, -0.03), (0.90, -0.03), (0.45, -0.01), (0.05, 0.02)], 6)
    lower = rot(lower_c, *hinge, OPEN)
    gum_up = C([(0.05, 0.06), (0.50, 0.03), (1.0, 0.0)], 5)
    gum_lo = rot(C([(0.06, 0.02), (0.50, -0.01), (1.0, -0.03)], 5), *hinge, OPEN)
    cavity = gum_up + gum_lo[::-1]
    fill_shape(lower, OX, OY); fill_shape(cavity, OX, OY, MOUTH)
    tongue = rot(C([(0.10, 0.02), (0.40, 0.07), (0.72, 0.05), (0.85, 0.0), (0.60, -0.03), (0.20, -0.02)], 6), *hinge, OPEN * 0.8)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.8)
    # dental battery: tightly packed small teeth along the back of both jaws, forming one cutting edge
    for i in range(18):
        x = 0.20 + i * 0.042
        bx, by = rot([(x, -0.02)], *hinge, OPEN)[0]
        peg((bx, by), 0.05, 0.04, 90 + OPEN, OX, OY, sw=1.1)
    fold(rot([(0.18, 0.035), (0.96, 0.035)], *hinge, OPEN), OX, OY, sw=1.4, stroke=INK)
    visible(lower, [cavity], OX, OY, sw=2.8)
    fill_shape(upper, OX, OY)
    scales([upper], OX, OY, 0.08, 0.014, seed=71)
    scales([lower], OX, OY, 0.08, 0.014, seed=72, exclude=[cavity])
    outline(upper, OX, OY, sw=3.0)
    for i in range(18):
        x = 0.18 + i * 0.044
        peg((x, 0.045 - 0.04 * (x / 1.0)), 0.05, 0.04, -90, OX, OY, sw=1.1)
    fold([(0.16, 0.0), (0.98, -0.04)], OX, OY, sw=1.4, stroke=INK)
    # beaks: keratin edges, upper hooked over the lower
    fold(C([(1.20, 0.10), (1.45, 0.02), (1.58, -0.08)], 4), OX, OY, sw=2.0, stroke=INK)
    fold(rot(C([(1.15, -0.04), (1.40, -0.06), (1.52, -0.04)], 4), *hinge, OPEN), OX, OY, sw=2.0, stroke=INK)
    # horns (keratin over bone cores), frill knobs, eye, nostril
    poly([(0.50, 0.78), (0.62, 0.86), (1.12, 1.36), (1.18, 1.44), (1.13, 1.30), (0.68, 0.74)], OX, OY, sw=2.6)
    poly([(1.18, 0.30), (1.28, 0.54), (1.34, 0.62), (1.34, 0.50), (1.32, 0.26)], OX, OY, sw=2.2)
    for (x, y) in upper[1:22:3]:
        if y > 0.4 and x < 0.3:
            ellipse(x, y, 0.035, 0.035, OX, OY, sw=1.5)
    eye(0.55, 0.62, 0.05, OX, OY)
    ellipse(1.22, 0.16, 0.045, 0.03, OX, OY, rot=-30, sw=1.5)
    title(40, 125, "A · MOUTH OPEN, SIDE VIEW")
    tx, ty = rot([(0.45, 0.04)], *hinge, OPEN * 0.8)[0]
    lead(OX, OY, tx, ty, 0.10, -0.75, "tongue")
    lead(OX, OY, 0.60, -0.02, 0.85, -0.70, "dental battery: hundreds of small teeth", "packed into one slicing edge")
    lead(OX, OY, 1.56, -0.06, 1.75, -0.30, "toothless beak,", "upper hooks over lower")
    scalebar_at(OX, OY, -0.5, -0.95, 0.5, 0.1, "50 cm")

def mouth_front(OX, OY):
    global S
    S = 170
    frill = C(mirror([(0.0, 1.55), (0.75, 1.42), (1.10, 0.95), (1.05, 0.45), (0.65, 0.20), (0.0, 0.15)]), 6)
    face = C(mirror([(0.0, 0.62), (0.34, 0.55), (0.36, 0.10), (0.24, -0.35), (0.10, -0.62), (0.0, -0.66)]), 6)
    cavity = C(mirror([(0.0, -0.30), (0.10, -0.32), (0.13, -0.45), (0.08, -0.56), (0.0, -0.58)]), 5)
    fill_shape(frill, OX, OY); fill_shape(face, OX, OY)
    scales([frill], OX, OY, 0.10, 0.016, seed=73, exclude=[face])
    scales([face], OX, OY, 0.08, 0.013, seed=74, exclude=[cavity])
    visible(frill, [face], OX, OY, sw=3.0)
    for (x, y) in frill[2:-2:3]:
        if y > 0.45:
            ellipse(x, y, 0.035, 0.035, OX, OY, sw=1.5)
    fill_shape(cavity, OX, OY, MOUTH)
    tongue = C(mirror([(0.0, -0.44), (0.06, -0.46), (0.07, -0.52), (0.0, -0.55)]), 4)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.4)
    outline(cavity, OX, OY, sw=1.8)
    visible(face, [], OX, OY, sw=3.0)
    for s in (1, -1):
        poly([(0.18 * s, 0.40), (0.32 * s, 0.46), (0.56 * s, 1.22), (0.50 * s, 1.27), (0.44 * s, 1.18)], OX, OY, sw=2.4)
        eye(0.30 * s, 0.30, 0.045, OX, OY)
        ellipse(0.09 * s, -0.10, 0.025, 0.04, OX, OY, sw=1.3)
        fold([(0.11 * s, -0.36), (0.12 * s, -0.52)], OX, OY, sw=1.2, stroke="#888")     # tooth rows deep inside
    poly([(-0.06, -0.02), (0.06, -0.02), (0.0, 0.25)], OX, OY, sw=2.0)
    title(OX - 1.1 * S, 125, "B · MOUTH OPEN, FRONT")
    label(OX, OY + 0.95 * S, "narrow beak for plucking; tooth rows", "middle")
    label(OX, OY + 0.95 * S + 16, "sit far back on each side", "middle")

def tooth(OX, OY):
    global S
    S = 2600
    jaw = [(-0.03, 0.0), (0.03, 0.0), (0.03, -0.075), (-0.03, -0.075)]
    fill_shape(jaw, OX, OY, "#fafafa"); outline(jaw, OX, OY, sw=1.6, stroke="#888")
    # 3 stacked teeth in one column: the top one worn into a slanted cutting face
    for k, (y0, worn) in enumerate([(0.0, True), (-0.024, False), (-0.050, False)]):
        pts = [(-0.012, y0), (0.0, y0 - 0.018), (0.012, y0), (0.010, y0 + 0.016), (0.0, y0 + 0.024), (-0.010, y0 + 0.016)]
        if worn:
            pts = [(-0.012, y0), (0.0, y0 - 0.018), (0.012, y0), (0.010, y0 + 0.030), (-0.010, y0 + 0.012)]
        fill_shape(C(pts + [pts[0]], 3), OX, OY); outline(C(pts + [pts[0]], 3), OX, OY, sw=2.0 if k == 0 else 1.6, stroke=INK if k == 0 else "#666")
    fold([(-0.010, 0.012), (0.010, 0.030)], OX, OY, sw=2.4, stroke=INK)
    title(OX - 0.06 * S, 125, "C · ONE TOOTH COLUMN")
    lead(OX, OY, 0.008, 0.024, 0.035, 0.045, "worn cutting edge")
    lead(OX, OY, 0.0, -0.024, 0.035, -0.02, "replacement teeth")
    label(OX + 0.035 * S + 4, OY + 0.02 * S + 20, "stacked underneath")
    label(OX - 0.03 * S, OY + 0.105 * S, "36 to 40 columns per side")
    label(OX - 0.03 * S, OY + 0.105 * S + 16, "of each jaw, 3 to 5 teeth each")

def hand(OX, OY):
    global S
    S = 560
    palm = C(mirror([(0.0, 0.16), (0.17, 0.14), (0.20, 0.06), (0.16, -0.02), (0.0, -0.04)]), 6)
    fingers = []
    for x0, L, w in [(-0.14, 0.13, 0.055), (-0.07, 0.17, 0.06), (0.0, 0.18, 0.06), (0.07, 0.11, 0.045), (0.13, 0.07, 0.035)]:
        fingers.append(C([(x0 - w / 2, 0.02), (x0 - w / 2 * 0.9, -L), (x0, -L - w * 0.3), (x0 + w / 2 * 0.9, -L), (x0 + w / 2, 0.02)], 4))
    for f in fingers:
        fill_shape(f, OX, OY)
    fill_shape(palm, OX, OY)
    ghost_bones([((-0.12, 0.10), (-0.14, -0.09), 0.03), ((-0.05, 0.11), (-0.07, -0.13), 0.032), ((0.02, 0.11), (0.0, -0.14), 0.032),
                 ((0.08, 0.10), (0.07, -0.07), 0.026), ((0.13, 0.09), (0.13, -0.03), 0.02)], OX, OY)
    scales([palm] + fingers, OX, OY, 0.022, 0.004, seed=75)
    for f in fingers:
        visible(f, [palm], OX, OY, sw=2.4)
    visible(palm, [], OX, OY, sw=2.4)
    for x0, L, w in [(-0.14, 0.13, 0.055), (-0.07, 0.17, 0.06), (0.0, 0.18, 0.06)]:
        hoof((x0, -L + 0.01), -90, 0.045, w * 1.05, OX, OY)
    title(40, 790, "D · HAND FROM THE FRONT")
    label(OX - 0.2 * S, OY + 0.30 * S, "fingers I to III end in hooves and carry")
    label(OX - 0.2 * S, OY + 0.30 * S + 16, "the weight; IV and V are small")

def foot_side(OX, OY):
    global S
    S = 420
    leg = C([(-0.16, 0.60), (-0.15, 0.35), (-0.16, 0.18), (-0.12, 0.05), (0.0, 0.0), (0.40, 0.0), (0.44, 0.05), (0.36, 0.14), (0.24, 0.22), (0.17, 0.36), (0.17, 0.60)], 6)
    fill_shape(leg, OX, OY)
    ghost_bones([((0.0, 0.52), (-0.02, 0.28), 0.10), ((-0.02, 0.26), (0.15, 0.10), 0.065), ((0.15, 0.10), (0.32, 0.05), 0.05)], OX, OY)
    scales([leg], OX, OY, 0.045, 0.008, seed=76)
    visible(open_edge(leg), [], OX, OY, sw=2.6, closed=False)
    for x in (0.31, 0.37, 0.43):
        hoof((x, 0.03), -15, 0.05, 0.055, OX, OY)
    fold(C([(-0.10, 0.06), (0.10, -0.005), (0.28, 0.0)], 4), OX, OY, sw=1.4)        # heel pad underneath
    ground(OX, OY, -0.3, 0.7)
    title(470, 790, "E · LOWER LEG AND FOOT, SIDE")
    label(OX + 0.30 * S, OY - 0.45 * S, "thick pillar leg on a soft heel pad,")
    label(OX + 0.30 * S, OY - 0.45 * S + 16, "toes end in blunt hooves, like a rhino")

def foot_below(OX, OY):
    global S
    S = 420
    heel = C(mirror([(0.0, 0.10), (0.15, 0.07), (0.17, -0.05), (0.10, -0.14), (0.0, -0.15)]), 6)
    footprint(OX, OY, [(128, 0.30, 0.065, 2), (104, 0.36, 0.07, 2), (80, 0.36, 0.07, 2), (56, 0.29, 0.06, 2)], heel, 0.03, end="hoof", tex_seed=77, end_len=0.05)
    title(OX - 0.4 * S, 790, "F · FOOT FROM BELOW")
    label(OX, OY + 0.24 * S, "4 hoofed toes on a big rounded pad", "middle")

W, H = 1600, 1240
mouth_side(190, 520)
mouth_front(1000, 530)
tooth(1350, 400)
hand(220, 900)
foot_side(640, 1150)
foot_below(1250, 1050)
sheet_frame(W, H, "TRICERATOPS HORRIDUS · MOUTH, TEETH, HAND AND FOOT",
            "Close-up life reference · toothless beak, 432 to 800 teeth in dental batteries, hoofed hands and feet")
save("trike_mouth.svg", W, H)
print("ok")
