exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(open("mouth_lib.py").read())

def mouth_side(OX, OY):
    global S
    S = 2500
    hinge = (0.0, 0.0); OPEN = -28
    upper = C([(-0.012, 0.035), (-0.004, 0.068), (0.035, 0.082), (0.09, 0.072), (0.15, 0.054), (0.20, 0.034), (0.228, 0.017), (0.231, 0.004),
               (0.21, -0.003), (0.15, -0.005), (0.09, -0.004), (0.04, -0.001), (0.0, 0.010)], 6)
    lower_c = C([(-0.006, 0.010), (0.02, -0.028), (0.09, -0.032), (0.17, -0.024), (0.222, -0.012), (0.225, -0.004), (0.19, -0.006), (0.10, -0.007), (0.03, -0.004)], 6)
    lower = rot(lower_c, *hinge, OPEN)
    gum_up = C([(0.012, 0.006), (0.07, -0.003), (0.13, -0.005)], 5)
    gum_lo = rot(C([(0.014, -0.004), (0.07, -0.007), (0.13, -0.007)], 5), *hinge, OPEN)
    cavity = gum_up + gum_lo[::-1]
    fill_shape(lower, OX, OY); fill_shape(cavity, OX, OY, MOUTH)
    tongue = rot(C([(0.015, 0.0), (0.06, 0.006), (0.11, 0.004), (0.13, -0.002), (0.10, -0.006), (0.03, -0.006)], 6), *hinge, OPEN * 0.8)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.8)
    lsizes = [0.008, 0.010, 0.011, 0.012, 0.012, 0.012, 0.011, 0.011, 0.010, 0.010, 0.009, 0.008, 0.007, 0.006]
    for i, L in enumerate(lsizes):
        x = 0.218 - i * 0.0135
        bx, by = rot([(x, -0.006)], *hinge, OPEN)[0]
        banana((bx, by), L, L * 0.40, 90 + OPEN + 10, 30, OX, OY)
    visible(lower, [cavity], OX, OY, sw=2.6)
    fill_shape(upper, OX, OY)
    scales([upper], OX, OY, 0.008, 0.0014, seed=81)
    scales([lower], OX, OY, 0.008, 0.0014, seed=82, exclude=[cavity])
    outline(upper, OX, OY, sw=2.8)
    usizes = [0.007, 0.008, 0.008, 0.009] + [0.011, 0.013, 0.014, 0.014, 0.013, 0.012, 0.011, 0.010, 0.009, 0.008, 0.007]
    for i, L in enumerate(usizes):
        x = 0.224 - i * 0.0128
        banana((x, -0.004), L, L * 0.42, -90 - 14, -30, OX, OY)
    fold(C([(0.02, 0.001), (0.10, -0.006), (0.20, -0.005)], 5), OX, OY, sw=1.6, stroke=INK)
    eye(0.035, 0.050, 0.009, OX, OY)
    ellipse(0.21, 0.020, 0.006, 0.004, OX, OY, rot=-20, sw=1.4)
    title(40, 125, "A · MOUTH OPEN, SIDE VIEW")
    tx, ty = rot([(0.07, 0.003)], *hinge, OPEN * 0.8)[0]
    lead(OX, OY, tx, ty, 0.0, -0.10, "tongue")
    lead(OX, OY, 0.17, -0.012, 0.21, -0.065, "about 28 to 30 curved, serrated", "teeth per jaw; drawn with lips like T. rex")
    scalebar_at(OX, OY, 0.0, 0.11, 0.05, 0.01, "5 cm")

def mouth_front(OX, OY):
    global S
    S = 1900
    head = C(mirror([(0.0, 0.085), (0.045, 0.075), (0.052, 0.035), (0.042, -0.005), (0.030, -0.045), (0.014, -0.070), (0.0, -0.074)]), 6)
    cavity = C(mirror([(0.0, 0.004), (0.020, 0.002), (0.030, -0.010), (0.028, -0.035), (0.016, -0.052), (0.0, -0.056)]), 6)
    fill_shape(head, OX, OY)
    scales([head], OX, OY, 0.007, 0.0012, seed=83, exclude=[cavity])
    fill_shape(cavity, OX, OY, MOUTH)
    tongue = C(mirror([(0.0, -0.028), (0.011, -0.032), (0.015, -0.042), (0.009, -0.050), (0.0, -0.052)]), 6)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.6)
    for k in range(-4, 5):
        t = k / 4
        banana((0.026 * math.sin(t * 1.2), 0.003 - 0.010 * t * t), 0.010, 0.0045, -90 + 10 * t, 0, OX, OY)
    for k in range(-3, 4):
        t = k / 3
        banana((0.022 * math.sin(t * 1.2), -0.054 + 0.010 * t * t), 0.009, 0.0042, 90 - 12 * t, 0, OX, OY)
    outline(cavity, OX, OY, sw=1.6)
    outline(head, OX, OY, sw=2.8)
    for s in (1, -1):
        eye(0.040 * s, 0.050, 0.008, OX, OY)
        ellipse(0.010 * s, 0.020, 0.004, 0.003, OX, OY, sw=1.2)
    title(OX - 0.07 * S, 125, "B · MOUTH OPEN, FRONT")
    label(OX, OY + 0.11 * S, "narrow snout, teeth in a tight row", "middle")

def tooth(OX, OY):
    global S
    S = 12000
    L, w = 0.016, 0.0085
    left, right = [], []
    for i in range(13):
        t = i / 12; hw = w / 2 * (1 - t) ** 0.7
        cx = -0.006 * t ** 1.6; cy = L * t
        left.append((cx - hw, cy)); right.append((cx + hw, cy))
    crown = left + right[::-1]
    root = C([(-0.0035, 0.0), (-0.0032, -0.008), (-0.0018, -0.014), (0.0, -0.015), (0.0018, -0.014), (0.0032, -0.008), (0.0035, 0.0)], 6)
    fill_shape(root, OX, OY, "#fafafa"); outline(root, OX, OY, sw=2.0, stroke="#888")
    fill_shape(crown, OX, OY); outline(crown, OX, OY, sw=2.4)
    for i in range(1, 12):      # back edge: big serrations; front edge: small ones near the tip only
        t = i / 12; hw = w / 2 * (1 - t) ** 0.7; cx = -0.006 * t ** 1.6; cy = L * t
        ellipse(cx + hw, cy, 0.00022, 0.00032, OX, OY, sw=1.0)
        if t > 0.55:
            ellipse(cx - hw, cy, 0.00013, 0.0002, OX, OY, sw=0.9)
    fold([(-0.0038, 0.0), (0.0038, 0.0)], OX, OY, sw=1.4, stroke="#888")
    title(OX - 0.008 * S, 125, "C · ONE TOOTH")
    lead(OX, OY, 0.0035, 0.006, 0.0065, 0.004, "back edge:")
    X, Y = P(0.0065, 0.004, OX, OY); label(X + 4, Y + 20, "strong serrations")
    lead(OX, OY, -0.0045, 0.012, -0.010, 0.017, "front edge: smaller ones")
    lead(OX, OY, 0.0, -0.009, 0.006, -0.010, "root, inside the jaw")
    label(OX - 0.008 * S, OY + 0.024 * S, "curved blade for slicing; the tip points back")

def hand(OX, OY):
    global S
    S = 1450
    arm = C([(-0.12, 0.035), (0.0, 0.03), (0.03, 0.012), (0.02, -0.012), (-0.12, -0.012)], 4)
    fingers = [C([(0.02, 0.020), (0.08, 0.040), (0.13, 0.040), (0.135, 0.030), (0.08, 0.025), (0.025, 0.006)], 5),
               C([(0.025, 0.006), (0.10, 0.012), (0.17, 0.004), (0.172, -0.006), (0.10, -0.002), (0.025, -0.006)], 5),
               C([(0.025, -0.006), (0.08, -0.016), (0.13, -0.028), (0.128, -0.036), (0.08, -0.026), (0.02, -0.014)], 5)]
    # wing feathers fanning back from the hand and forearm (drawn first, behind)
    for k in range(9):
        x = -0.10 + k * 0.03
        fold(C([(x, -0.010), (x - 0.03, -0.07 - 0.005 * k), (x - 0.045, -0.09 - 0.006 * k)], 4), OX, OY, sw=1.3, stroke="#777")
        fold([(x - 0.03, -0.07 - 0.005 * k), (x - 0.02, -0.08 - 0.006 * k)], OX, OY, sw=0.8, stroke="#aaa")
    for sh in [arm] + fingers:
        fill_shape(sh, OX, OY)
    ghost_bones([((-0.11, 0.012), (0.02, 0.008), 0.012), ((0.025, 0.018), (0.12, 0.034), 0.006), ((0.025, 0.004), (0.16, 0.003), 0.007), ((0.025, -0.008), (0.12, -0.026), 0.006)], OX, OY)
    feather_tex([arm], OX, OY, 0.008, 0.007, 190, seed=84)
    scales(fingers, OX, OY, 0.008, 0.0013, seed=85)
    visible(open_edge(arm), fingers, OX, OY, sw=2.4, closed=False)
    for i, f in enumerate(fingers):
        visible(f, fingers[i + 1:], OX, OY, sw=2.2)
    claw((0.132, 0.035), -10, 0.028, 0.010, OX, OY, curl=90)
    claw((0.170, -0.001), -20, 0.032, 0.011, OX, OY, curl=90)
    claw((0.128, -0.032), -35, 0.026, 0.009, OX, OY, curl=90)
    title(40, 790, "D · HAND WITH WING FEATHERS")
    label(OX - 0.12 * S, OY + 0.16 * S, "3 long fingers with strongly curved claws; wing")
    label(OX - 0.12 * S, OY + 0.16 * S + 16, "feathers anchor on bumps (quill knobs) on the forearm")

def foot_side(OX, OY):
    global S
    S = 1250
    leg = C([(-0.012, 0.25), (-0.006, 0.18), (0.012, 0.09), (0.03, 0.03), (0.05, 0.004), (0.08, 0.0), (0.12, 0.004), (0.15, 0.0), (0.162, 0.008),
             (0.13, 0.016), (0.09, 0.02), (0.06, 0.04), (0.04, 0.10), (0.026, 0.25)], 6)
    fill_shape(leg, OX, OY)
    ghost_bones([((0.006, 0.24), (0.045, 0.025), 0.012), ((0.045, 0.02), (0.09, 0.012), 0.009), ((0.09, 0.012), (0.145, 0.008), 0.008)], OX, OY)
    scales([leg], OX, OY, 0.008, 0.0014, seed=86)
    visible(open_edge(leg), [], OX, OY, sw=2.4, closed=False)
    claw((0.158, 0.006), -8, 0.022, 0.009, OX, OY, curl=40)
    # toe II, held up, with the sickle claw
    toe2 = C([(0.045, 0.040), (0.07, 0.055), (0.085, 0.060), (0.088, 0.050), (0.065, 0.038), (0.048, 0.030)], 5)
    fill_shape(toe2, OX, OY); outline(toe2, OX, OY, sw=2.2)
    claw((0.086, 0.056), 60, 0.066, 0.016, OX, OY, curl=150)
    claw((0.012, 0.10), 215, 0.014, 0.006, OX, OY, curl=-40)
    ground(OX, OY, -0.05, 0.22)
    title(470, 790, "E · FOOT, SIDE")
    lead(OX, OY, 0.10, 0.09, 0.14, 0.14, "sickle claw on toe II, over 6.5 cm", "along its curve, held off the ground")
    label(OX + 0.06 * S, OY + 30, "walks on toes III and IV")

def foot_below(OX, OY):
    global S
    S = 2600
    heel = C(mirror([(0.0, 0.018), (0.016, 0.014), (0.019, 0.0), (0.012, -0.014), (0.0, -0.016)]), 6)
    footprint(OX, OY, [(98, 0.085, 0.012, 3), (78, 0.075, 0.011, 3)], heel, 0.004, end="claw", tex_seed=87, end_len=0.016)
    # toe II tucked up off the ground: dashed outline
    pts = [(-0.012, 0.012), (-0.022, 0.04), (-0.028, 0.06), (-0.020, 0.062), (-0.010, 0.035), (-0.004, 0.014)]
    d = path_d(C(pts, 4), OX, OY, close=True)
    out.append(f'<path d="{d}" fill="none" stroke="#777" stroke-width="1.6" stroke-dasharray="5 4"/>')
    title(OX - 0.06 * S, 790, "F · FOOT FROM BELOW")
    label(OX, OY + 0.028 * S, "2 toes touch the ground; dashed = raised toe II", "middle")

W, H = 1600, 1240
mouth_side(110, 470)
mouth_front(1010, 440)
tooth(1350, 430)
hand(260, 950)
foot_side(560, 1130)
foot_below(1250, 1080)
sheet_frame(W, H, "VELOCIRAPTOR MONGOLIENSIS · MOUTH, TEETH, HAND AND FOOT",
            "Close-up life reference · about 56 to 60 serrated teeth, feathered 3-fingered hands, sickle claw on toe II")
save("raptor_mouth.svg", W, H)
print("ok")
