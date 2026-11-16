exec(open("lib.py").read())
exec(open("life_lib.py").read())
C = lambda pts, n=6: curve_pts(pts, n)
import model_limbs as ml
MOUTH = "#eeeeee"      # inside of the mouth
GUM = "#f7f7f7"

def rot(pts, cx, cy, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in pts]

def banana(base, L, w, ang, curve, OX, OY, sw=1.4):
    """A thick curved tooth: base point, length L, base width w, pointing `ang` degrees, curving by `curve` degrees."""
    bx, by = base; top, bot = [], []
    x, y, a = bx, by, ang
    for i in range(9):
        t = i / 8; hw = w / 2 * (1 - t) ** 0.8
        nx, ny = -math.sin(math.radians(a)), math.cos(math.radians(a))
        top.append((x + nx * hw, y + ny * hw)); bot.append((x - nx * hw, y - ny * hw))
        x += math.cos(math.radians(a)) * L / 8; y += math.sin(math.radians(a)) * L / 8; a += curve / 8
    poly(top + bot[::-1], OX, OY, sw=sw)

def label(x, y, s, anchor="start"):
    text(x, y, s, 13, "400", anchor, "#555")

# ---------------- A. OPEN MOUTH, SIDE VIEW ----------------
def mouth_side(OX, OY):
    global S
    S = 380
    hinge = (0.02, 0.0)
    upper = C([(-0.08, 0.30), (0.0, 0.56), (0.25, 0.70), (0.6, 0.63), (1.0, 0.46), (1.32, 0.28), (1.47, 0.12), (1.48, 0.0),
               (1.30, -0.03), (0.9, -0.04), (0.5, -0.02), (0.18, 0.02), (-0.04, 0.12)], 6)
    lower_closed = C([(-0.02, 0.05), (0.15, -0.30), (0.7, -0.30), (1.2, -0.22), (1.42, -0.12), (1.43, -0.03), (1.2, -0.04), (0.6, -0.05), (0.2, -0.03)], 6)
    OPEN = -30
    lower = rot(lower_closed, *hinge, OPEN)
    gum_up = C([(0.10, 0.03), (0.5, -0.02), (0.85, -0.04)], 6)
    gum_lo = rot(C([(0.12, -0.03), (0.5, -0.05), (0.85, -0.05)], 6), *hinge, OPEN)
    cavity = gum_up + gum_lo[::-1]
    neck = C([(-0.45, 0.62), (0.05, 0.62), (0.05, -0.30), (-0.45, -0.38)], 4)
    # neck stub, lower jaw, mouth cavity, tongue, lower teeth, then upper head and upper teeth
    fill_shape(lower, OX, OY)
    fill_shape(cavity, OX, OY, MOUTH)
    tongue = rot(C([(0.15, 0.0), (0.45, 0.06), (0.80, 0.04), (0.95, -0.02), (0.75, -0.05), (0.30, -0.05)], 6), *hinge, OPEN * 0.8)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.8)
    fold(rot(C([(0.25, -0.01), (0.55, 0.02), (0.82, 0.0)], 4), *hinge, OPEN * 0.8), OX, OY, sw=1.0, stroke="#999")
    for k in range(5):     # palate ridges on the roof of the mouth
        x = 0.35 + k * 0.17
        fold([(x, -0.01), (x + 0.05, 0.06)], OX, OY, sw=1.0, stroke="#aaa")
    # lower teeth: 13 per side, bigger toward the middle front
    lsizes = [0.07, 0.09, 0.11, 0.12, 0.13, 0.13, 0.12, 0.11, 0.10, 0.09, 0.08, 0.07, 0.06]
    for i, L in enumerate(lsizes):
        x = 1.36 - i * 0.090
        bx, by = rot([(x, -0.04)], *hinge, OPEN)[0]
        banana((bx, by), L, L * 0.42, 90 + OPEN + 8, 22, OX, OY)
    visible(lower, [cavity], OX, OY, sw=2.8)
    fill_shape(upper, OX, OY)
    scales([upper], OX, OY, 0.06, 0.010, seed=61)
    scales([lower], OX, OY, 0.06, 0.010, seed=62, exclude=[cavity])
    outline(upper, OX, OY, sw=3.0)
    # upper teeth: 4 small front (premaxillary) + 12 big side teeth, largest mid-row
    usizes = [0.06, 0.07, 0.07, 0.08] + [0.11, 0.14, 0.17, 0.18, 0.17, 0.15, 0.13, 0.11, 0.09, 0.08, 0.07, 0.06]
    for i, L in enumerate(usizes):
        x = 1.43 - i * 0.075
        y = -0.03 if i < 13 else 0.0
        banana((x, y), L, L * 0.45, -90 - 12, -24, OX, OY)
    fold(C([(0.20, 0.01), (0.6, -0.03), (1.0, -0.05), (1.40, -0.04)], 5), OX, OY, sw=1.8, stroke=INK)    # upper lip edge
    ellipse(0.20, 0.42, 0.05, 0.04, OX, OY, sw=2.0); ellipse(0.21, 0.42, 0.02, 0.02, OX, OY, fill=INK, sw=1)
    fold(C([(0.10, 0.50), (0.20, 0.53), (0.30, 0.50)], 3), OX, OY, sw=2.0, stroke=INK)
    ellipse(1.36, 0.18, 0.04, 0.025, OX, OY, rot=-20, sw=1.6)
    fold(C([(0.0, 0.10), (0.06, -0.02), (0.04, -0.15)], 3), OX, OY)       # skin fold at the jaw corner
    text(40, 125, "A · MOUTH OPEN, SIDE VIEW", 18)
    def lead(x1, y1, x2, y2, s):
        fold([(x1, y1), (x2, y2)], OX, OY, sw=1.0, stroke="#999"); X, Y = P(x2, y2, OX, OY); label(X + 4, Y + 4, s)
    lead(1.20, -0.05, 1.40, -0.24, "lips cover the")
    X, Y = P(1.40, -0.24, OX, OY); label(X + 4, Y + 20, "tooth bases")
    tx, ty = rot([(0.55, 0.02)], *hinge, OPEN * 0.8)[0]
    lead(tx, ty, 0.10, -0.62, "tongue on the floor of the mouth")
    # scale bar 50 cm
    for i in range(5):
        out.append(f'<rect x="{OX + i * 0.1 * S:.1f}" y="{OY + 0.75 * S:.1f}" width="{0.1 * S:.1f}" height="8" fill="{INK if i % 2 == 0 else "#fff"}" stroke="{INK}" stroke-width="1.2"/>')
    label(OX, OY + 0.75 * S - 6, "0"); label(OX + 0.5 * S, OY + 0.75 * S - 6, "50 cm", "middle")

# ---------------- B. MOUTH OPEN, FRONT VIEW ----------------
def mouth_front(OX, OY):
    global S
    S = 380
    head = C(mirror([(0.0, 0.70), (0.42, 0.60), (0.52, 0.25), (0.46, -0.10), (0.40, -0.45), (0.22, -0.62), (0.0, -0.66)]), 6)
    cavity = C(mirror([(0.0, 0.08), (0.20, 0.06), (0.34, -0.02), (0.36, -0.20), (0.30, -0.36), (0.16, -0.46), (0.0, -0.48)]), 6)
    fill_shape(head, OX, OY)
    scales([head], OX, OY, 0.06, 0.010, seed=63, exclude=[cavity])
    fill_shape(cavity, OX, OY, MOUTH)
    tongue = C(mirror([(0.0, -0.20), (0.12, -0.24), (0.18, -0.34), (0.12, -0.43), (0.0, -0.45)]), 6)
    fill_shape(tongue, OX, OY); outline(tongue, OX, OY, sw=1.8)
    fold([(0.0, -0.22), (0.0, -0.40)], OX, OY, sw=1.0, stroke="#999")
    # upper teeth around the top of the opening (front ones D-shaped and close together)
    for k in range(-5, 6):
        t = k / 5
        x = 0.33 * math.sin(t * 1.25); y = 0.06 - 0.10 * abs(t) ** 2
        L = 0.07 + 0.05 * abs(t)
        banana((x, y), L, 0.045, -90 + 12 * t, 0, OX, OY)
    for k in range(-4, 5):
        t = k / 4
        x = 0.28 * math.sin(t * 1.25); y = -0.46 + 0.10 * abs(t) ** 2
        L = 0.06 + 0.05 * abs(t)
        banana((x, y), L, 0.042, 90 - 14 * t, 0, OX, OY)
    outline(cavity, OX, OY, sw=1.8)
    outline(head, OX, OY, sw=3.0)
    for s in (1, -1):
        ellipse(0.36 * s, 0.38, 0.05, 0.04, OX, OY, sw=2.0); ellipse(0.36 * s, 0.38, 0.02, 0.02, OX, OY, fill=INK, sw=1)
        ellipse(0.10 * s, 0.20, 0.03, 0.02, OX, OY, sw=1.4)
    text(OX - 0.55 * S, 125, "B · MOUTH OPEN, FRONT", 18)
    label(OX, OY + 0.76 * S, "front teeth close together, D-shaped", "middle")

# ---------------- C. SINGLE TOOTH ----------------
def tooth(OX, OY):
    global S
    S = 1250
    # root (bottom, inside the jaw) and crown (top), crown curving back
    root = C([(-0.029, 0.0), (-0.026, -0.08), (-0.015, -0.15), (0.0, -0.17), (0.015, -0.15), (0.026, -0.08), (0.031, 0.0)], 6)
    crown = []
    L, w = 0.13, 0.062
    left, right = [], []
    for i in range(13):
        t = i / 12; hw = w / 2 * (1 - t) ** 0.6
        cx = -0.035 * t ** 2; cy = L * t
        left.append((cx - hw, cy)); right.append((cx + hw, cy))
    crown = left + right[::-1]
    fill_shape(root, OX, OY, "#fafafa"); outline(root, OX, OY, sw=2.0, stroke="#888")
    fill_shape(crown, OX, OY); outline(crown, OX, OY, sw=2.4)
    # serrations on both edges
    for i in range(1, 12):
        t = i / 12; hw = w / 2 * (1 - t) ** 0.6; cx = -0.035 * t ** 2; cy = L * t
        for s in (-1, 1):
            ellipse(cx + s * hw, cy, 0.0018, 0.0026, OX, OY, sw=1.0)
    fold([(-0.032, 0.0), (0.034, 0.0)], OX, OY, sw=1.4, stroke="#888")      # gum line
    # cross-sections
    ellipse(0.13, 0.09, 0.026, 0.017, OX, OY, sw=1.8)
    poly(C([(0.118, -0.03), (0.138, -0.03), (0.145, -0.045), (0.138, -0.06), (0.118, -0.06)], 4) + [(0.118, -0.03)], OX, OY, sw=1.8)
    text(OX - 0.08 * S, 125, "C · BIGGEST TOOTH", 18)
    def lead(x1, y1, x2, y2, s):
        fold([(x1, y1), (x2, y2)], OX, OY, sw=1.0, stroke="#999"); X, Y = P(x2, y2, OX, OY); label(X + 4, Y + 4, s)
    lead(0.01, 0.03, 0.045, 0.015, "crown, about 12 cm")
    lead(0.0, -0.11, 0.045, -0.12, "root, inside the jaw")
    lead(-0.03, 0.11, -0.06, 0.155, "serrated front and back edges")
    X, Y = P(0.13, 0.06, OX, OY); label(X, Y + 4, "side tooth: thick oval", "middle")
    X, Y = P(0.13, -0.075, OX, OY); label(X, Y + 4, "front tooth: D-shape", "middle")
    label(OX - 0.03 * S, OY + 0.20 * S, "about 30 cm in total")

# ---------------- D. HAND ----------------
def hand(OX, OY):
    global S
    S = 900
    arm = C([(-0.10, 0.10), (0.02, 0.09), (0.06, 0.05), (0.06, -0.02), (-0.10, -0.04)], 4)
    f1 = C([(0.04, 0.07), (0.12, 0.10), (0.20, 0.08), (0.22, 0.05), (0.14, 0.04), (0.05, 0.03)], 5)
    f2 = C([(0.04, 0.02), (0.13, 0.02), (0.23, -0.01), (0.25, -0.04), (0.15, -0.03), (0.05, -0.03)], 5)
    for sh in (arm, f1, f2):
        fill_shape(sh, OX, OY)
    for (a, b, w) in [((-0.08, 0.03), (0.05, 0.04), 0.035), ((0.05, 0.06), (0.12, 0.075), 0.016), ((0.12, 0.075), (0.19, 0.065), 0.013),
                      ((0.05, 0.0), (0.13, -0.005), 0.017), ((0.13, -0.005), (0.20, -0.02), 0.014), ((0.05, 0.02), (0.08, 0.005), 0.008)]:
        bone(a, b, w, OX, OY, GHOST)
    scales([arm, f1, f2], OX, OY, 0.012, 0.0025, seed=64)
    visible(open_edge(arm), [f1, f2], OX, OY, sw=2.4, closed=False)
    visible(f1, [f2], OX, OY, sw=2.4)
    visible(f2, [], OX, OY, sw=2.4)
    claw((0.205, 0.075), -15, 0.06, 0.022, OX, OY, curl=60)
    claw((0.23, -0.02), -25, 0.065, 0.024, OX, OY, curl=60)
    for x, y in [(0.10, 0.045), (0.17, 0.045), (0.10, -0.035), (0.18, -0.04)]:
        ellipse(x, y, 0.016, 0.008, OX, OY, sw=1.2)      # finger pads
    text(40, 790, "D · HAND (2 fingers)", 18)
    label(OX - 0.10 * S, OY + 0.12 * S, "hooked claws, pads under each finger joint")

# ---------------- E. FOOT, SIDE ----------------
# Feet traced from the 3D model (body_tools/leg_builder.py: heavier legs, feet 1.5x the first draft).
def foot_side(OX, OY):
    global S
    S = 300
    fp = ml.side_panel("trex", "leg", 0.95)
    leg = fp["outline"]
    fill_shape(leg, OX, OY)
    (bx, by), (cx, cy) = fp["toe"]
    ax, ay = fp["ankle"]
    bone((ax - 0.02, 0.86), (ax, ay), 0.08, OX, OY, GHOST)                    # end of the shin
    bone((ax, ay), (bx, by + 0.03), 0.065, OX, OY, GHOST)                     # pinched middle foot bone
    toe = [(bx + (cx - bx) * t, by + 0.03 + (cy - by - 0.03) * t) for t in (0.0, 0.38, 0.7, 0.96)]
    for (p0, p1), w in zip(zip(toe, toe[1:]), (0.055, 0.047, 0.04)):
        bone(p0, p1, w, OX, OY, GHOST)
    scales([leg], OX, OY, 0.05, 0.0095, seed=65)
    visible(open_edge(leg), [], OX, OY, sw=2.6, closed=False)
    for base, ang, L, w in fp["claws"]:
        claw(base, ang, L, w * 0.5, OX, OY, curl=6)
    for t in (0.3, 0.55, 0.8):                                                # toe pads on the underside
        x = bx + (cx - bx) * t
        poly(C([(x - 0.08, 0.016), (x, -0.008), (x + 0.08, 0.016)], 4), OX, OY, sw=1.4, fill="none", close=False)
    ground(OX, OY, ax - 0.45, cx + 0.5)
    text(470, 790, "E · LOWER LEG AND FOOT, SIDE", 18)
    label(OX + 0.32 * S, OY - 0.80 * S, "stands on its toes; the ankle is")
    label(OX + 0.32 * S, OY - 0.80 * S + 16, "high off the ground, like a bird")
    label(OX - 0.40 * S, OY + 34, "dewclaw off the ground at the back")

# ---------------- F. FOOT, FROM BELOW ----------------
def foot_below(OX, OY):
    global S
    S = 205
    fb = ml.below_panel("trex", "leg")
    heel, toes = fb["heel"], fb["toes"]
    for t_, ang, L in toes:
        fill_shape(t_, OX, OY)
    fill_shape(heel, OX, OY)
    scales([heel] + [t[0] for t in toes], OX, OY, 0.06, 0.0115, seed=66)
    for i, (t_, ang, L) in enumerate(toes):
        visible(t_, [heel], OX, OY, sw=2.6)
        a = math.radians(ang)
        t0 = next(t / 50 for t in range(50) if not inside((math.cos(a) * t / 50 * L, math.sin(a) * t / 50 * L), heel))
        n_pads = 3 if i == 1 else 2
        for k in range(n_pads):     # pads under the toe where it's clear of the heel pad
            t = t0 + 0.1 + (0.86 - t0 - 0.1) * k / max(1, n_pads - 1)
            ellipse(math.cos(a) * t * L, math.sin(a) * t * L, 0.08, 0.1, OX, OY, rot=ang, sw=1.3)
    visible(heel, [], OX, OY, sw=2.6)
    for base, ang, L, w in fb["claws"]:
        claw(base, ang, L, w * 0.5, OX, OY, curl=0)
    hy = min(p[1] for p in heel)
    text(OX - 0.55 * S, 790, "F · FOOT FROM BELOW", 18)
    label(OX, OY - hy * S + 26, "3 thick toes with pads, like the footprint (R5)", "middle")

out.append('<rect width="100%" height="100%" fill="#fff"/>')
W, H = 1600, 1340
mouth_side(130, 430)
mouth_front(1000, 430)
tooth(1330, 380)
hand(120, 930)
foot_side(560, 1240)
foot_below(1300, 1170)
text(40, 50, "TYRANNOSAURUS REX · MOUTH, TEETH, HAND AND FOOT", 28, "700")
text(40, 78, "Close-up life reference · about 58 teeth, lips over the teeth, 2-fingered hands, 3-toed feet with pads", 16, "400", "start", "#555")
text(W - 40, H - 15, "Faint grey = bones inside · grey fill = inside of the mouth", 15, "400", "end", "#777")
save("trex_mouth.svg", W, H)
print("ok")
