exec(open("lib.py").read())
S = 105   # px per metre (Ankylosaurus is ~7 m long)
ARM = "#6b6b6b"   # armor plates drawn in outline over the bones

def plate(cx, cy, rx, ry, ox, oy, rot=0, keel=True):
    ellipse(cx, cy, rx, ry, ox, oy, rot=rot, stroke=ARM, sw=1.4, fill="none")
    if keel:
        X1, Y1 = P(cx - rx * 0.6, cy, ox, oy); X2, Y2 = P(cx + rx * 0.6, cy, ox, oy)
        out.append(f'<line x1="{X1:.1f}" y1="{Y1:.1f}" x2="{X2:.1f}" y2="{Y2:.1f}" stroke="{ARM}" stroke-width="1"/>')

def side_foot_hooves(ankle, toes, ox, oy, stroke=INK):
    ax, ay = ankle
    for (mx, my), ang, segs, w in toes:
        bone((ax, ay - 0.02), (mx, my), w * 1.15, ox, oy, stroke)
    for (mx, my), ang, segs, w in toes:
        digit((mx, my), ang, segs, w, ox, oy, stroke, droop=-6, end="hoof")
    ellipse(ax, ay, 0.07, 0.05, ox, oy, rot=-15, stroke=stroke, sw=1.8)

# ---------------- SIDE VIEW ----------------
def side(ox, oy):
    out.append(f'<line x1="{ox-20}" y1="{oy}" x2="{ox+7.1*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    # far limbs
    bone((3.0, 1.15), (3.3, 0.64), 0.20, ox, oy, FAR); bone((3.3, 0.64), (3.22, 0.24), 0.16, ox, oy, FAR)
    side_foot_hooves((3.22, 0.22), [((3.36, 0.07), -10, [0.07, 0.05], 0.06), ((3.42, 0.06), -8, [0.08, 0.05], 0.065)], ox, oy, FAR)
    bone((5.05, 0.95), (4.98, 0.56), 0.17, ox, oy, FAR); bone((4.98, 0.56), (5.12, 0.22), 0.13, ox, oy, FAR)
    side_foot_hooves((5.12, 0.20), [((5.22, 0.07), -10, [0.05, 0.04], 0.05), ((5.28, 0.06), -8, [0.06, 0.04], 0.05)], ox, oy, FAR)
    # ribs: very deep and wide body
    for rx in [3.35 + i * 0.2 for i in range(9)]:
        top = 1.42 - 0.03 * abs(rx - 4.1)
        depth = 0.95 - 0.30 * abs(rx - 4.1) / 0.9
        rib([(rx, top), (rx + 0.16, top - depth * 0.45), (rx + 0.08, top - depth)], 0.09, 0.03, ox, oy)
    spine = curve_pts([(0.45, 0.80), (1.4, 0.95), (2.4, 1.15), (3.0, 1.30), (4.0, 1.42), (5.0, 1.33), (5.4, 1.25), (5.8, 1.15)], 12)
    spine = resample(spine, 0.15)
    vert_chain(spine, ox, oy, lambda x: 0.16 if 2.8 < x < 5.1 else (0.06 if x > 1.8 else 0.0),
               lambda x: 0.10 if 1.5 < x < 2.9 else 0.0, k=0.75, tail=2.8)
    # stiffened back half of the tail: bundles of bony tendons
    for k in range(3):
        poly(curve_pts([(0.45, 0.80 + 0.03 * k - 0.03), (1.2, 0.92 + 0.03 * k - 0.03), (1.9, 1.04 + 0.03 * k - 0.03)], 6), ox, oy, sw=0.9, fill="none", close=False, stroke="#888")
    # tail club (bony knob)
    poly(curve_pts([(0.45, 0.80), (0.38, 0.95), (0.18, 0.98), (0.0, 0.85), (0.02, 0.68), (0.20, 0.62), (0.40, 0.66), (0.45, 0.80)], 6), ox, oy, sw=2.4)
    ellipse(0.22, 0.80, 0.10, 0.07, ox, oy, sw=1.4)
    # pelvis: long low ilium
    poly(curve_pts([(2.25, 1.38), (2.8, 1.55), (3.6, 1.58), (4.15, 1.50), (3.6, 1.38), (2.9, 1.34), (2.25, 1.38)], 5), ox, oy)
    ellipse(3.0, 1.15, 0.12, 0.12, ox, oy, sw=1.8)
    poly(curve_pts([(2.92, 1.10), (2.7, 0.92), (2.55, 0.86), (2.58, 0.95), (2.80, 1.13)], 4), ox, oy)   # ischium
    # shoulder girdle
    poly(curve_pts([(5.05, 1.00), (4.85, 1.22), (4.60, 1.35), (4.55, 1.28), (4.80, 1.12), (4.98, 0.92)], 4), ox, oy)
    ellipse(5.06, 0.95, 0.10, 0.12, ox, oy)
    # near limbs: short and thick
    bone((5.06, 0.95), (4.92, 0.55), 0.19, ox, oy); bone((4.92, 0.55), (5.04, 0.21), 0.15, ox, oy)
    side_foot_hooves((5.04, 0.19), [((5.10, 0.08), -14, [0.04, 0.03], 0.045), ((5.16, 0.07), -10, [0.05, 0.04], 0.05), ((5.22, 0.06), -6, [0.06, 0.04], 0.05)], ox, oy)
    bone((3.0, 1.15), (3.18, 0.62), 0.22, ox, oy); bone((3.18, 0.62), (3.08, 0.23), 0.17, ox, oy)
    side_foot_hooves((3.08, 0.21), [((3.18, 0.08), -10, [0.07, 0.05], 0.065), ((3.25, 0.07), -8, [0.08, 0.05], 0.07), ((3.32, 0.06), -6, [0.08, 0.06], 0.07)], ox, oy)
    # skull: short, deep, armored, with horns at the back corners
    sk = curve_pts([(5.75, 1.05), (5.78, 1.32), (6.00, 1.38), (6.30, 1.30), (6.45, 1.15), (6.42, 1.02), (6.25, 0.98), (5.95, 0.97), (5.75, 1.05)], 6)
    poly(sk, ox, oy, sw=2.6)
    poly([(5.80, 1.30), (5.70, 1.38), (5.66, 1.33), (5.76, 1.24)], ox, oy, sw=1.6)    # rear horn
    poly([(5.82, 1.08), (5.70, 1.00), (5.72, 0.95), (5.85, 1.00)], ox, oy, sw=1.6)    # cheek horn
    poly(curve_pts([(5.80, 1.0), (6.1, 0.93), (6.38, 0.95), (6.40, 1.0)], 5), ox, oy, sw=1.6, fill="none", close=False)
    ellipse(5.98, 1.17, 0.06, 0.05, ox, oy, sw=1.6)
    ellipse(6.36, 1.12, 0.035, 0.03, ox, oy, sw=1.3)
    # armor plates (osteoderms)
    for x in [2.5 + i * 0.25 for i in range(12)]:
        yb = 1.55 + 0.10 * math.sin((x - 2.5) / 2.9 * math.pi)
        plate(x, yb, 0.10, 0.05, ox, oy)
        if 2.8 < x < 5.0:
            plate(x + 0.12, yb - 0.32, 0.09, 0.05, ox, oy)
    for x, y in [(5.25, 1.42), (5.5, 1.36)]:           # neck half-rings
        poly(curve_pts([(x - 0.12, y - 0.12), (x - 0.06, y + 0.06), (x + 0.06, y + 0.06), (x + 0.12, y - 0.12)], 5), ox, oy, stroke=ARM, sw=1.6, fill="none", close=False)
    for x in [1.9 + i * 0.22 for i in range(3)]:
        plate(x, 1.18 + (x - 1.9) * 0.18, 0.08, 0.045, ox, oy)
    for x in [0.6 + i * 0.3 for i in range(4)]:         # tail side spikes
        poly([(x - 0.06, 0.86 + x * 0.12), (x + 0.06, 0.86 + x * 0.12), (x, 0.98 + x * 0.12)], ox, oy, stroke=ARM, sw=1.3, fill="none")
    text(ox, 110, "SIDE VIEW", 22)

# ---------------- TOP VIEW ----------------
def top(ox, oy):
    for rx in [3.35 + i * 0.2 for i in range(9)]:
        span = 0.70 + 0.25 * (1 - abs(rx - 4.1) / 0.9)
        for s in (1, -1):
            rib([(rx, 0.06 * s), (rx + 0.08, 0.6 * span * s), (rx + 0.20, span * s)], 0.08, 0.03, ox, oy, sw=1.5)
    for x in [0.45 + i * 0.15 for i in range(36)]:
        w = 0.05 + 0.05 * min(1, x / 2.8)
        ellipse(x, 0, 0.065, w, ox, oy, sw=1.4)
    for s in (1, -1):   # very wide flaring ilia
        poly(curve_pts([(2.25, 0.08 * s), (2.6, 0.55 * s), (3.2, 0.82 * s), (3.9, 0.70 * s), (4.15, 0.25 * s), (3.4, 0.15 * s), (2.6, 0.10 * s)], 5), ox, oy)
        # legs just showing at the sides
        ellipse(3.18, 0.98 * s, 0.13, 0.11, ox, oy)
        for k, d in enumerate((-0.08, 0.0, 0.08)):
            digit((3.25, (0.98 + d) * s), (-12 + k * 12) * s, [0.10, 0.07], 0.07, ox, oy, droop=0, end="hoof")
        ellipse(4.95, 0.92 * s, 0.11, 0.10, ox, oy)
        for k, d in enumerate((-0.10, -0.05, 0.0, 0.05, 0.10)):
            digit((5.02, (0.92 + d) * s), (-20 + k * 10) * s, [0.06, 0.04], 0.045, ox, oy, droop=0, end="hoof")
    poly(curve_pts([(0.45, 0.0), (0.38, 0.28), (0.18, 0.30), (0.0, 0.12), (0.0, -0.12), (0.18, -0.30), (0.38, -0.28), (0.45, 0.0)], 6), ox, oy, sw=2.4)  # club
    # skull from above: wide triangle with corner horns
    sk = curve_pts([(5.75, 0.0), (5.75, 0.36), (5.95, 0.40), (6.25, 0.28), (6.45, 0.10), (6.48, 0.0), (6.45, -0.10), (6.25, -0.28), (5.95, -0.40), (5.75, -0.36), (5.75, 0.0)], 6)
    poly(sk, ox, oy, sw=2.6)
    for s in (1, -1):
        poly([(5.78, 0.36 * s), (5.66, 0.46 * s), (5.82, 0.42 * s)], ox, oy, sw=1.5)
        ellipse(5.98, 0.30 * s, 0.05, 0.04, ox, oy, sw=1.4)
    # armor grid across the back (outline)
    for x in [2.4 + i * 0.24 for i in range(13)]:
        half = 0.20 + 0.75 * math.sin((x - 2.4) / 3.0 * math.pi) ** 0.6
        n = max(1, int(half / 0.18))
        for j in range(-n, n + 1):
            plate(x, j * half / n * 0.95, 0.09, 0.06, ox, oy, keel=False)
    text(ox, oy - 1.2 * S, "TOP VIEW", 22)

# ---------------- FRONT VIEW ----------------
def front(ox, oy):
    out.append(f'<line x1="{ox-1.25*S}" y1="{oy}" x2="{ox+1.25*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    poly(curve_pts([(0.0, 1.50), (0.75, 1.40), (1.00, 1.00), (0.80, 0.55), (0.0, 0.45), (-0.80, 0.55), (-1.00, 1.00), (-0.75, 1.40), (0.0, 1.50)], 6), ox, oy, stroke=FAR, sw=1.8)
    for s in (1, -1):
        bone((0.62 * s, 0.95), (0.86 * s, 0.56), 0.19, ox, oy)       # sprawled upper arm
        bone((0.86 * s, 0.56), (0.80 * s, 0.20), 0.15, ox, oy)
        ellipse(0.80 * s, 0.17, 0.10, 0.05, ox, oy, sw=1.6)
        for d in (-0.09, -0.045, 0.0, 0.045, 0.09):
            bone(((0.80 + d * 0.5) * s, 0.15), ((0.80 + d * 1.3) * s, 0.07), 0.04, ox, oy)
            hoof(((0.80 + d * 1.3) * s, 0.07), -90, 0.06, 0.05, ox, oy)
    # armor along the top edge
    for t in range(11):
        a = math.radians(15 + t * 15)
        plate(math.cos(a) * 0.95, 0.95 + math.sin(a) * 0.62, 0.07, 0.05, ox, oy, rot=math.degrees(a) - 90, keel=False)
    head = curve_pts([(0.0, 1.30), (0.33, 1.28), (0.38, 1.10), (0.28, 0.95), (0.0, 0.90), (-0.28, 0.95), (-0.38, 1.10), (-0.33, 1.28), (0.0, 1.30)], 6)
    poly(head, ox, oy, sw=2.4)
    for s in (1, -1):
        poly([(0.33 * s, 1.28), (0.46 * s, 1.36), (0.38 * s, 1.20)], ox, oy, sw=1.5)
        poly([(0.37 * s, 1.02), (0.47 * s, 0.96), (0.33 * s, 0.96)], ox, oy, sw=1.5)
        ellipse(0.22 * s, 1.15, 0.05, 0.045, ox, oy, sw=1.5)
        ellipse(0.07 * s, 0.98, 0.03, 0.025, ox, oy, sw=1.3)
    text(ox, oy + 32, "FRONT VIEW", 22, "600", "middle")

def feet_detail():
    global S
    keep = S; S = 380
    text(1000, 560, "FOOT DETAIL (from above, 3.5x scale)", 22)
    foot_top(1110, 880, [(-0.09, 0.16, [0.07, 0.05], 0.065, True, "II"), (0.0, 0.18, [0.08, 0.06], 0.07, True, "III"), (0.09, 0.15, [0.07, 0.05], 0.065, True, "IV")], 0)
    foot_top(1360, 880, [(-0.12, 0.10, [0.04, 0.03], 0.045, True, "I"), (-0.06, 0.12, [0.05, 0.04], 0.05, True, "II"), (0.0, 0.13, [0.05, 0.04], 0.05, True, "III"),
                         (0.06, 0.12, [0.05, 0.04], 0.048, True, "IV"), (0.12, 0.10, [0.04, 0.03], 0.042, True, "V")], 0)
    S = keep
    text(1110, 925, "Hind foot: 3 hoofed toes", 14, "600", "middle", "#333")
    text(1360, 925, "Hand: 5 short hoofed fingers", 14, "600", "middle", "#333")

W, H = 1500, 950
side(60, 400)
front(1330, 400)
top(60, 750)
feet_detail()
scalebar(60, 905, 2, 0.5)
sheet(W, H, "ANKYLOSAURUS MAGNIVENTRIS", "Skeletal reference · about 7 m long · built from Reference Library picks")
text(W - 40, 475, "Grey = far-side bones · dark outlines = armor plates", 15, "400", "end", "#777")
save("ankylo.svg", W, H)
print("ok")
