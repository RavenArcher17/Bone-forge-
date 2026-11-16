exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(open("mouth_lib.py").read())
exec(open("model_panels.py").read())

def tiles(shape, OX, OY, step, seed, exclude=()):
    """Armor tiles: small polygons scattered over a shape."""
    rnd = random.Random(seed)
    xs = [p[0] for p in shape]; ys = [p[1] for p in shape]
    y = min(ys); row = 0
    while y < max(ys):
        x = min(xs) + (step / 2 if row % 2 else 0)
        while x < max(xs):
            cx, cy = x + rnd.uniform(-0.15, 0.15) * step, y + rnd.uniform(-0.15, 0.15) * step
            if inside((cx, cy), shape) and not any(inside((cx, cy), e) for e in exclude):
                r = step * 0.42
                pts = [(cx + math.cos(math.radians(a + rnd.uniform(-12, 12))) * r * rnd.uniform(0.8, 1.0),
                        cy + math.sin(math.radians(a + rnd.uniform(-12, 12))) * r * rnd.uniform(0.8, 1.0)) for a in range(0, 360, 60)]
                poly(pts + [pts[0]], OX, OY, stroke="#9a9a9a", sw=1.0, fill="none")
            x += step
        y += step * 0.87; row += 1

def mouth_side(OX, OY):
    global S
    S = 620
    hinge = (0.0, 0.0); OPEN = -16
    upper = C([(-0.03, 0.05), (-0.02, 0.24), (0.15, 0.31), (0.40, 0.29), (0.56, 0.22), (0.64, 0.10), (0.66, 0.01), (0.62, -0.02),
               (0.45, -0.02), (0.25, -0.01), (0.08, 0.0)], 6)
    lower_c = C([(-0.01, 0.02), (0.05, -0.11), (0.30, -0.13), (0.52, -0.09), (0.60, -0.04), (0.60, -0.01), (0.45, -0.02), (0.25, -0.02), (0.08, -0.01)], 6)
    lower = rot(lower_c, *hinge, OPEN)
    gum_up = C([(0.06, 0.0), (0.25, -0.012), (0.44, -0.015)], 5)
    gum_lo = rot(C([(0.07, -0.01), (0.25, -0.02), (0.44, -0.02)], 5), *hinge, OPEN)
    cavity = gum_up + gum_lo[::-1]
    fill_shape(lower, OX, OY); fill_shape(cavity, OX, OY, MOUTH)
    tongue = rot(C([(0.06, 0.0), (0.20, 0.02), (0.36, 0.015), (0.42, -0.005), (0.30, -0.02), (0.10, -0.015)], 6), *hinge, OPEN * 0.75)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.8)
    for i in range(14):
        x = 0.12 + i * 0.023
        bx, by = rot([(x, -0.02)], *hinge, OPEN)[0]
        peg((bx, by), 0.016, 0.014, 90 + OPEN, OX, OY, sw=1.1, tip=0.9)
    visible(lower, [cavity], OX, OY, sw=2.8)
    fill_shape(upper, OX, OY)
    scales([lower], OX, OY, 0.025, 0.004, seed=91, exclude=[cavity])
    tiles(upper, OX, OY, 0.055, 92)
    outline(upper, OX, OY, sw=3.0)
    for i in range(14):
        x = 0.12 + i * 0.023
        peg((x, -0.01), 0.016, 0.014, -90, OX, OY, sw=1.1, tip=0.9)
    fold(C([(0.47, -0.02), (0.60, -0.025), (0.665, 0.0)], 4), OX, OY, sw=2.2, stroke=INK)          # broad upper beak
    fold(rot(C([(0.44, -0.025), (0.55, -0.03), (0.60, -0.012)], 4), *hinge, OPEN), OX, OY, sw=2.2, stroke=INK)
    poly([(0.02, 0.27), (-0.09, 0.36), (-0.06, 0.28), (-0.01, 0.21)], OX, OY, sw=2.2)                # upper horn at the back corner
    poly([(0.0, 0.08), (-0.10, 0.0), (-0.04, -0.01), (0.03, 0.03)], OX, OY, sw=2.2)                 # cheek horn
    eye(0.16, 0.18, 0.026, OX, OY)
    ellipse(0.57, 0.13, 0.028, 0.018, OX, OY, rot=-30, sw=1.5)
    title(40, 125, "A · MOUTH OPEN, SIDE VIEW")
    tx, ty = rot([(0.22, 0.01)], *hinge, OPEN * 0.75)[0]
    lead(OX, OY, tx, ty, 0.05, -0.35, "thick tongue")
    lead(OX, OY, 0.33, -0.03, 0.55, -0.36, "rows of very small leaf-shaped teeth")
    lead(OX, OY, 0.66, -0.01, 0.72, 0.22, "broad beak for", "cropping low plants")
    lead(OX, OY, 0.35, 0.25, 0.40, 0.40, "bony armor tiles fused to the skull")
    scalebar_at(OX, OY, 0.62, -0.30, 0.2, 0.05, "20 cm")

def mouth_front(OX, OY):
    global S
    S = 520
    head = C(mirror([(0.0, 0.44), (0.30, 0.42), (0.40, 0.30), (0.40, 0.08), (0.32, -0.06), (0.18, -0.12), (0.0, -0.13)]), 6)
    cavity = C(mirror([(0.0, -0.02), (0.15, -0.03), (0.22, -0.06), (0.18, -0.10), (0.0, -0.11)]), 6)
    fill_shape(head, OX, OY)
    tiles(head, OX, OY, 0.05, 93, exclude=[cavity])
    fill_shape(cavity, OX, OY, MOUTH)
    tongue = C(mirror([(0.0, -0.05), (0.08, -0.06), (0.10, -0.085), (0.0, -0.10)]), 4)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.5)
    for k in range(-4, 5):
        peg((0.045 * k, -0.035 + 0.004 * abs(k)), 0.012, 0.012, -90, OX, OY, sw=1.0, tip=0.9)
    outline(cavity, OX, OY, sw=1.8)
    fold(C([(-0.22, -0.06), (0.0, -0.035), (0.22, -0.06)], 5), OX, OY, sw=2.2, stroke=INK)
    outline(head, OX, OY, sw=3.0)
    for s in (1, -1):
        poly([(0.30 * s, 0.42), (0.46 * s, 0.50), (0.40 * s, 0.32)], OX, OY, sw=2.0)
        poly([(0.39 * s, 0.10), (0.50 * s, 0.02), (0.36 * s, 0.02)], OX, OY, sw=2.0)
        eye(0.24 * s, 0.24, 0.024, OX, OY)
        ellipse(0.07 * s, 0.07, 0.018, 0.014, OX, OY, sw=1.3)
    title(OX - 0.50 * S, 125, "B · MOUTH OPEN, FRONT")
    label(OX, OY + 0.30 * S, "wide head, wide beak; horns at each corner", "middle")

def tooth(OX, OY):
    global S
    S = 10000
    crown = C([(-0.0050, 0.0), (-0.0058, 0.006), (-0.0040, 0.012), (0.0, 0.016), (0.0040, 0.012), (0.0058, 0.006), (0.0050, 0.0)], 8)
    root = C([(-0.0045, 0.0), (-0.0040, -0.010), (-0.0020, -0.016), (0.0, -0.017), (0.0020, -0.016), (0.0040, -0.010), (0.0045, 0.0)], 6)
    fill_shape(root, OX, OY, "#fafafa"); outline(root, OX, OY, sw=2.0, stroke="#888")
    fill_shape(crown, OX, OY); outline(crown, OX, OY, sw=2.4)
    edge = resample(crown, 0.0016)
    for (x, y) in edge[2:-2]:     # small denticles along both edges of the crown
        ellipse(x, y, 0.0005, 0.0005, OX, OY, sw=1.0)
    fold([(0.0, 0.002), (0.0, 0.013)], OX, OY, sw=1.0, stroke="#999")
    fold([(-0.005, 0.0), (0.005, 0.0)], OX, OY, sw=1.4, stroke="#888")
    title(OX - 0.012 * S, 125, "C · ONE TOOTH")
    lead(OX, OY, 0.004, 0.010, 0.009, 0.014, "leaf-shaped crown,")
    X, Y = P(0.009, 0.014, OX, OY); label(X + 4, Y + 20, "flattened side to side")
    lead(OX, OY, -0.0058, 0.005, -0.011, -0.002, "5 to 8 tiny")
    X, Y = P(-0.011, -0.002, OX, OY); label(X + 4, Y + 20, "serrations per edge")
    lead(OX, OY, 0.0, -0.010, 0.007, -0.012, "root, inside the jaw")
    label(OX - 0.012 * S, OY + 0.024 * S, "very small for the animal's size")

def hand(OX, OY):
    global S
    BOX = (70, 830, 380, 1030)                        # panel fitted from the 3D model
    fp = model_front("ankylo", "arm", OX, OY, 0.45, seed=94, box=BOX)
    OX, OY = fp["OX"], fp["OY"]
    out.append(f'<line x1="{BOX[0] - 15}" y1="{OY}" x2="{BOX[2] + 15}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')
    title(40, 790, "D · HAND FROM THE FRONT")
    label(70, 1064, "short, hoof-like; the front legs carried most")
    label(70, 1080, "of the weight (finger count is partly fiction)")

def foot_side(OX, OY):
    global S
    BOX = (480, 860, 790, 1150)                       # panel fitted from the 3D model
    fp = model_side("ankylo", "leg", OX, OY, 0.45, seed=96, hooves=True, toe_pads=0, box=BOX)
    OX, OY = fp["OX"], fp["OY"]
    out.append(f'<line x1="{BOX[0] - 15}" y1="{OY}" x2="{BOX[2] + 15}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')
    title(470, 790, "E · LOWER LEG AND FOOT, SIDE")
    label(802, 890, "short, thick leg on a heel pad;")
    label(802, 906, "probably 3 hoofed toes")

def foot_below(OX, OY):
    global S
    BOX = (1100, 830, 1480, 1150)                       # panel fitted from the 3D model
    model_below("ankylo", "leg", OX, OY, seed=97, hoofed=(0, 1, 2), pads=(0,), box=BOX)
    title(1080, 790, "F · FOOT FROM BELOW")
    label(1290, 1178, "3 hoofed toes on a wide pad", "middle")

W, H = 1600, 1240
mouth_side(190, 470)
mouth_front(1010, 440)
tooth(1380, 380)
hand(220, 920)
foot_side(640, 1150)
foot_below(1260, 1060)
sheet_frame(W, H, "ANKYLOSAURUS MAGNIVENTRIS · MOUTH, TEETH, HAND AND FOOT",
            "Close-up life reference · broad beak, about 140 tiny leaf-shaped teeth, hoof-like hands and feet")
save("ankylo_mouth.svg", W, H)
print("ok")
