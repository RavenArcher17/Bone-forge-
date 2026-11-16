exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(open("mouth_lib.py").read())

def mouth_side(OX, OY):
    global S
    S = 360
    hinge = (0.0, 0.0); OPEN = -12
    upper = C([(-0.58, 0.36), (-0.40, 0.31), (-0.12, 0.19), (0.05, 0.15), (0.18, 0.12), (0.5, 0.07), (0.9, 0.03), (1.26, 0.0),
               (0.9, -0.004), (0.5, -0.008), (0.15, -0.01), (0.05, 0.0), (-0.06, 0.04), (-0.22, 0.13), (-0.48, 0.29)], 6)
    lower_c = C([(-0.04, 0.03), (0.04, -0.05), (0.3, -0.05), (0.8, -0.03), (1.19, -0.006), (1.18, -0.001), (0.8, -0.012), (0.3, -0.015), (0.06, -0.01)], 6)
    lower = rot(lower_c, *hinge, OPEN)
    gum_up = C([(0.06, -0.008), (0.30, -0.010), (0.55, -0.008)], 5)
    gum_lo = rot(C([(0.07, -0.012), (0.30, -0.015), (0.55, -0.013)], 5), *hinge, OPEN)
    cavity = gum_up + gum_lo[::-1]
    fill_shape(lower, OX, OY); fill_shape(cavity, OX, OY, MOUTH)
    tongue = rot(C([(0.07, -0.012), (0.22, -0.002), (0.34, -0.006), (0.30, -0.016), (0.10, -0.018)], 5), *hinge, OPEN * 0.85)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.6)
    visible(lower, [cavity], OX, OY, sw=2.6)
    fill_shape(upper, OX, OY)
    feather_tex([C([(-0.10, 0.17), (0.08, 0.15), (0.08, 0.02), (-0.08, 0.03)], 3)], OX, OY, 0.02, 0.014, 200, seed=111)
    outline(upper, OX, OY, sw=2.8)
    fold(C([(0.12, 0.10), (0.6, 0.045), (1.15, 0.006)], 5), OX, OY, sw=1.0, stroke="#999")       # beak surface line
    eye(0.06, 0.085, 0.022, OX, OY)
    title(40, 125, "A · BEAK OPEN, SIDE VIEW")
    tx, ty = rot([(0.20, -0.01)], *hinge, OPEN * 0.85)[0]
    lead(OX, OY, tx, ty, 0.15, -0.36, "small tongue")
    lead(OX, OY, 0.80, -0.05, 0.85, -0.30, "no teeth: a long, pointed beak", "for catching fish")
    lead(OX, OY, -0.40, 0.31, -0.45, 0.50, "crest (long in males, short in females)")
    scalebar_at(OX, OY, 0.70, 0.30, 0.5, 0.1, "50 cm")

def mouth_front(OX, OY):
    global S
    S = 1100
    head = C(mirror([(0.0, 0.10), (0.028, 0.085), (0.036, 0.045), (0.030, 0.0), (0.018, -0.040), (0.006, -0.075), (0.0, -0.080)]), 6)
    gap = C(mirror([(0.0, -0.010), (0.012, -0.016), (0.014, -0.030), (0.008, -0.052), (0.0, -0.060)]), 5)
    crest = [(-0.004, 0.10), (0.004, 0.10), (0.003, 0.19), (-0.003, 0.19)]
    fill_shape(crest, OX, OY); outline(crest, OX, OY, sw=2.0)
    fill_shape(head, OX, OY)
    feather_tex([head], OX, OY, 0.006, 0.005, 260, seed=112, exclude=[gap])
    fill_shape(gap, OX, OY, MOUTH); outline(gap, OX, OY, sw=1.6)
    outline(head, OX, OY, sw=2.8)
    for s in (1, -1):
        eye(0.026 * s, 0.050, 0.006, OX, OY)
    title(OX - 0.13 * S, 125, "B · BEAK OPEN, FRONT")
    label(OX, OY + 0.11 * S, "very narrow beak; the crest is edge-on", "middle")

def beak_tip(OX, OY):
    global S
    S = 1300
    up = C([(-0.12, 0.030), (0.0, 0.016), (0.10, 0.004), (0.15, 0.0), (0.10, -0.002), (0.0, -0.004), (-0.12, -0.006)], 6)
    lo = rot(C([(-0.12, -0.012), (0.0, -0.010), (0.10, -0.006), (0.14, -0.004), (0.10, -0.012), (0.0, -0.022), (-0.12, -0.032)], 6), -0.12, -0.012, -6)
    for sh in (up, lo):
        fill_shape(sh, OX, OY); outline(sh, OX, OY, sw=2.4)
    fold(C([(-0.11, 0.012), (0.0, 0.006), (0.12, 0.001)], 4), OX, OY, sw=1.0, stroke="#999")
    title(OX - 0.13 * S, 125, "C · BEAK TIP (NO TEETH)")
    label(OX - 0.12 * S, OY + 0.06 * S, "smooth, pointed tips on both jaws;")
    label(OX - 0.12 * S, OY + 0.06 * S + 16, "fish bones and scales have been")
    label(OX - 0.12 * S, OY + 0.06 * S + 32, "found with Pteranodon fossils")

def hand(OX, OY):
    global S
    S = 800
    wrist = C([(-0.10, 0.20), (-0.04, 0.20), (0.02, 0.06), (0.02, 0.0), (-0.06, 0.0), (-0.12, 0.10)], 5)
    wingf = C([(-0.03, 0.18), (0.02, 0.24), (0.10, 0.30), (0.12, 0.28), (0.05, 0.20), (0.0, 0.14)], 5)
    fingers = [C([(0.0, 0.03), (0.05, 0.018), (0.075, 0.008), (0.072, 0.0), (0.04, 0.004), (0.0, 0.012)], 4),
               C([(0.0, 0.015), (0.06, 0.006), (0.085, 0.0), (0.08, -0.004), (0.045, -0.002), (0.0, 0.0)], 4),
               C([(-0.01, 0.006), (0.04, -0.006), (0.065, -0.012), (0.06, -0.016), (0.03, -0.012), (-0.01, -0.004)], 4)]
    for sh in [wingf, wrist] + fingers:
        fill_shape(sh, OX, OY)
    ghost_bones([((-0.07, 0.19), (-0.02, 0.02), 0.022), ((-0.02, 0.18), (0.10, 0.29), 0.016)], OX, OY)
    feather_tex([wrist], OX, OY, 0.008, 0.006, 250, seed=113)
    visible(open_edge(wingf), [wrist], OX, OY, sw=2.2, closed=False)
    visible(open_edge(wrist), fingers, OX, OY, sw=2.4, closed=False)
    for i, f in enumerate(fingers):
        visible(f, fingers[i + 1:], OX, OY, sw=2.0)
    for (x, y, a) in [(0.072, 0.004, -20), (0.083, -0.002, -25), (0.062, -0.014, -35)]:
        claw((x, y), a, 0.022, 0.008, OX, OY, curl=60)
    fold(C([(0.11, 0.28), (0.15, 0.20), (0.15, 0.12)], 4), OX, OY, sw=1.3, stroke="#999")       # folded membrane edge
    ground(OX, OY, -0.15, 0.25)
    title(40, 790, "D · HAND ON THE GROUND")
    label(OX + 0.14 * S, OY - 0.26 * S, "wing finger folds up and back")
    label(OX + 0.14 * S, OY - 0.07 * S, "3 small clawed fingers")
    label(OX + 0.14 * S, OY - 0.07 * S + 16, "turned outward when walking")

def foot_side(OX, OY):
    global S
    S = 1700
    leg = C([(-0.03, 0.20), (-0.03, 0.10), (-0.04, 0.03), (-0.02, 0.0), (0.06, 0.0), (0.10, 0.002), (0.11, 0.008), (0.06, 0.014), (0.02, 0.03), (0.01, 0.10), (0.01, 0.20)], 6)
    fill_shape(leg, OX, OY)
    ghost_bones([((-0.01, 0.19), (-0.01, 0.03), 0.010), ((-0.01, 0.02), (0.05, 0.007), 0.007), ((0.05, 0.007), (0.095, 0.004), 0.006)], OX, OY)
    scales([leg], OX, OY, 0.006, 0.001, seed=114)
    visible(open_edge(leg), [], OX, OY, sw=2.4, closed=False)
    claw((0.105, 0.005), -10, 0.016, 0.006, OX, OY, curl=40)
    ground(OX, OY, -0.08, 0.16)
    title(470, 790, "E · FOOT, SIDE")
    label(OX + 0.04 * S, OY - 0.12 * S, "flat-footed: the whole sole")
    label(OX + 0.04 * S, OY - 0.12 * S + 16, "touches the ground")

def foot_below(OX, OY):
    global S
    S = 2600
    heel = C(mirror([(0.0, 0.016), (0.018, 0.013), (0.020, -0.012), (0.012, -0.022), (0.0, -0.024)]), 6)
    footprint(OX, OY, [(112, 0.055, 0.0075, 2), (98, 0.062, 0.008, 2), (84, 0.062, 0.008, 2), (70, 0.055, 0.0075, 2)], heel, 0.004, end="claw", tex_seed=115, end_len=0.012)
    title(OX - 0.08 * S, 790, "F · FOOT FROM BELOW")
    label(OX, OY + 0.042 * S, "4 slender clawed toes on a long sole", "middle")

W, H = 1600, 1240
mouth_side(330, 450)
mouth_front(1060, 430)
beak_tip(1400, 420)
hand(150, 1140)
foot_side(640, 1130)
foot_below(1280, 1030)
sheet_frame(W, H, "PTERANODON LONGICEPS · BEAK, HAND AND FOOT",
            "Close-up life reference · long toothless beak, 3 small clawed fingers plus the wing finger, 4-toed flat feet")
save("ptera_mouth.svg", W, H)
print("ok")
