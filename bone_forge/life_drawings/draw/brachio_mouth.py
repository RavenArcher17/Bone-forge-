exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(open("mouth_lib.py").read())
exec(open("model_panels.py").read())

def mouth_side(OX, OY):
    global S
    S = 440
    hinge = (0.0, 0.0); OPEN = -22
    upper = C([(-0.04, 0.05), (-0.03, 0.28), (0.08, 0.48), (0.24, 0.62), (0.38, 0.57), (0.50, 0.38), (0.66, 0.22), (0.79, 0.11), (0.83, 0.02),
               (0.79, -0.02), (0.60, -0.015), (0.35, -0.01), (0.10, 0.0)], 6)
    lower_c = C([(-0.02, 0.02), (0.06, -0.10), (0.40, -0.12), (0.70, -0.08), (0.80, -0.035), (0.79, -0.01), (0.55, -0.015), (0.30, -0.015), (0.08, -0.01)], 6)
    lower = rot(lower_c, *hinge, OPEN)
    gum_up = C([(0.06, 0.0), (0.30, -0.01), (0.55, -0.015)], 5)
    gum_lo = rot(C([(0.07, -0.01), (0.30, -0.015), (0.55, -0.015)], 5), *hinge, OPEN)
    cavity = gum_up + gum_lo[::-1]
    fill_shape(lower, OX, OY); fill_shape(cavity, OX, OY, MOUTH)
    tongue = rot(C([(0.06, 0.0), (0.22, 0.03), (0.42, 0.025), (0.50, 0.0), (0.36, -0.02), (0.10, -0.015)], 6), *hinge, OPEN * 0.8)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.8)
    for i in range(12):          # lower teeth only at the front of the jaw
        x = 0.78 - i * 0.030
        bx, by = rot([(x, -0.015)], *hinge, OPEN)[0]
        peg((bx, by), 0.042, 0.018, 90 + OPEN - 8, OX, OY, sw=1.2, tip=0.7)
    visible(lower, [cavity], OX, OY, sw=2.8)
    fill_shape(upper, OX, OY)
    scales([upper], OX, OY, 0.03, 0.005, seed=101)
    scales([lower], OX, OY, 0.03, 0.005, seed=102, exclude=[cavity])
    outline(upper, OX, OY, sw=3.0)
    for i in range(14):
        x = 0.81 - i * 0.030
        peg((x, -0.015), 0.046, 0.019, -98, OX, OY, sw=1.2, tip=0.7)
    ellipse(0.27, 0.53, 0.07, 0.045, OX, OY, rot=-25, sw=1.6)     # big bony nostril opening on the arch
    eye(0.12, 0.30, 0.03, OX, OY)
    ellipse(0.74, 0.08, 0.022, 0.014, OX, OY, rot=-20, sw=1.3, fill="none", stroke="#999")
    title(40, 125, "A · MOUTH OPEN, SIDE VIEW")
    tx, ty = rot([(0.28, 0.015)], *hinge, OPEN * 0.8)[0]
    lead(OX, OY, tx, ty, 0.05, -0.40, "tongue")
    lead(OX, OY, 0.62, -0.06, 0.80, -0.36, "spoon-shaped teeth at the front only;", "it stripped leaves and swallowed whole")
    lead(OX, OY, 0.33, 0.53, 0.56, 0.52, "fleshy nostrils: on top of the arch or")
    X, Y = P(0.56, 0.52, OX, OY); label(X + 4, Y + 20, "nearer the snout tip (still debated)")
    scalebar_at(OX, OY, 0.70, 0.30, 0.2, 0.05, "20 cm")

def mouth_front(OX, OY):
    global S
    S = 460
    head = C(mirror([(0.0, 0.62), (0.16, 0.60), (0.24, 0.40), (0.30, 0.15), (0.30, -0.05), (0.22, -0.16), (0.0, -0.18)]), 6)
    cavity = C(mirror([(0.0, -0.01), (0.18, -0.02), (0.25, -0.07), (0.20, -0.14), (0.0, -0.15)]), 6)
    fill_shape(head, OX, OY)
    scales([head], OX, OY, 0.03, 0.005, seed=103, exclude=[cavity])
    fill_shape(cavity, OX, OY, MOUTH)
    tongue = C(mirror([(0.0, -0.07), (0.09, -0.08), (0.11, -0.11), (0.0, -0.13)]), 4)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.5)
    for k in range(-5, 6):
        t = k / 5
        peg((0.22 * math.sin(t * 1.3), -0.015 - 0.03 * t * t), 0.035, 0.018, -90, OX, OY, sw=1.1, tip=0.7)
    for k in range(-4, 5):
        t = k / 4
        peg((0.19 * math.sin(t * 1.3), -0.145 + 0.03 * t * t), 0.03, 0.016, 90, OX, OY, sw=1.1, tip=0.7)
    outline(cavity, OX, OY, sw=1.8)
    outline(head, OX, OY, sw=3.0)
    for s in (1, -1):
        eye(0.20 * s, 0.28, 0.026, OX, OY)
        ellipse(0.07 * s, 0.50, 0.04, 0.06, OX, OY, sw=1.4)
    title(OX - 0.45 * S, 125, "B · MOUTH OPEN, FRONT")
    label(OX, OY + 0.32 * S, "broad, rounded snout with a U-shaped row of teeth", "middle")

def tooth(OX, OY):
    global S
    S = 3200
    # front view (spoon) and side view (chisel) of the same tooth
    for dx, wfac, lab in [(-0.022, 1.0, "front"), (0.022, 0.45, "side")]:
        w = 0.016 * wfac
        crown = C([(dx - w / 2, 0.0), (dx - w * 0.62, 0.025), (dx - w * 0.45, 0.042), (dx, 0.050), (dx + w * 0.45, 0.042), (dx + w * 0.62, 0.025), (dx + w / 2, 0.0)], 6)
        root = C([(dx - w * 0.45, 0.0), (dx - w * 0.42, -0.035), (dx, -0.045), (dx + w * 0.42, -0.035), (dx + w * 0.45, 0.0)], 6)
        fill_shape(root, OX, OY, "#fafafa"); outline(root, OX, OY, sw=2.0, stroke="#888")
        fill_shape(crown, OX, OY); outline(crown, OX, OY, sw=2.4)
        fold([(dx - w * 0.5, 0.0), (dx + w * 0.5, 0.0)], OX, OY, sw=1.4, stroke="#888")
        X, Y = P(dx, -0.05, OX, OY); label(X, Y + 16, lab, "middle")
    fold(C([(-0.022, 0.006), (-0.022, 0.040)], 3), OX, OY, sw=1.0, stroke="#999")
    title(OX - 0.03 * S, 125, "C · ONE TOOTH")
    label(OX - 0.03 * S, OY + 0.075 * S, "spoon-shaped from the front,")
    label(OX - 0.03 * S, OY + 0.075 * S + 16, "chisel-like from the side")

def hand(OX, OY):
    global S
    BOX = (50, 830, 300, 1150)                       # panel fitted from the 3D model
    fp = model_side("brachio", "arm", OX, OY, 1.0, seed=104, claw_w=0.8, curl=-20, hooves=True, toe_pads=0, box=BOX)
    OX, OY = fp["OX"], fp["OY"]
    out.append(f'<line x1="{BOX[0] - 15}" y1="{OY}" x2="{BOX[2] + 15}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')
    title(40, 790, "D · HAND, SIDE")
    label(312, 860, "tall column of hand bones,")
    label(312, 876, "nails round the front;")
    label(312, 892, "one thumb claw")

def foot_side(OX, OY):
    global S
    BOX = (480, 830, 800, 1150)                       # panel fitted from the 3D model
    fp = model_side("brachio", "leg", OX, OY, 0.9, seed=105, claw_w=0.8, curl=25, hooves=True, toe_pads=0, box=BOX)
    OX, OY = fp["OX"], fp["OY"]
    out.append(f'<line x1="{BOX[0] - 15}" y1="{OY}" x2="{BOX[2] + 15}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')
    title(470, 790, "E · LOWER LEG AND FOOT, SIDE")
    label(812, 860, "elephant-like foot: toes")
    label(812, 876, "with nails, the 3 inner")
    label(812, 892, "ones longer and blunt")

def prints(OX, OY):
    global S
    S = 380
    # hand print: horseshoe (half-ring of hand bones), thumb claw mark
    hx = -0.35
    ring = [(hx + 0.20 * math.cos(math.radians(a)), 0.05 + 0.14 * math.sin(math.radians(a))) for a in range(0, 181, 15)]
    inner = [(hx + 0.10 * math.cos(math.radians(a)), 0.05 + 0.06 * math.sin(math.radians(a))) for a in range(180, -1, -15)]
    shoe = C(ring + inner, 2)
    fill_shape(shoe, OX, OY); scales([shoe], OX, OY, 0.03, 0.006, seed=106); outline(shoe, OX, OY, sw=2.6)
    claw((hx - 0.17, 0.15), 120, 0.08, 0.04, OX, OY, curl=0)
    X, Y = P(hx, -0.08, OX, OY); label(X, Y + 16, "hand: horseshoe shape", "middle")
    # foot print: big oval pad, 5 toes, claws on the inner 3
    heel = C(mirror([(0.0, 0.08), (0.15, 0.05), (0.17, -0.07), (0.10, -0.15), (0.0, -0.17)]), 6)
    import copy
    footprint(OX + 0.30 * S, OY, [(140, 0.22, 0.04, 1), (115, 0.25, 0.042, 1), (90, 0.25, 0.042, 1), (66, 0.21, 0.038, 1), (44, 0.17, 0.034, 1)],
              heel, 0.03, end=None, tex_seed=107)
    for ang, L in [(140, 0.22), (115, 0.25), (90, 0.25)]:
        a = math.radians(ang)
        claw((0.30 + math.cos(a) * L, math.sin(a) * L), ang, 0.07, 0.03, OX, OY, curl=0)
    X, Y = P(0.30, -0.22, OX, OY); label(X, Y + 16, "foot: claws on 3 toes", "middle")
    title(1050, 790, "F · HAND AND FOOT FROM BELOW")

W, H = 1600, 1240
mouth_side(170, 500)
mouth_front(1000, 450)
tooth(1400, 400)
hand(200, 1150)
foot_side(640, 1150)
prints(1270, 1020)
sheet_frame(W, H, "BRACHIOSAURUS ALTITHORAX · MOUTH, TEETH, HAND AND FOOT",
            "Close-up life reference · about 56 spoon-shaped teeth, no chewing, column-like hands with one thumb claw")
save("brachio_mouth.svg", W, H)
print("ok")
