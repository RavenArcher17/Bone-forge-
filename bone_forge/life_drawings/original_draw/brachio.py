exec(open("lib.py").read())
S = 38   # px per metre (Brachiosaurus is ~22 m long, head ~12.5 m up)

def column_foot(ankle, toes, ox, oy, stroke=INK, claw_toes=()):
    """Elephant-like sauropod foot from the side: short metapodials, stubby toes, claws on some."""
    ax, ay = ankle
    for i, ((mx, my), ang, w) in enumerate(toes):
        bone((ax, ay), (mx, my), w, ox, oy, stroke)
        if i in claw_toes:
            claw((mx, my), ang, w * 2.0, w * 0.9, ox, oy, stroke, curl=40)
        else:
            ellipse(mx + 0.05, my, w * 0.6, w * 0.4, ox, oy, stroke=stroke, sw=1.4)
    ellipse(ax, ay, 0.28, 0.16, ox, oy, rot=-10, stroke=stroke, sw=1.8)

# ---------------- SIDE VIEW ----------------
def side(ox, oy):
    out.append(f'<line x1="{ox-20}" y1="{oy}" x2="{ox+18.3*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    # far limbs
    bone((6.6, 3.9), (7.1, 2.0), 0.55, ox, oy, FAR); bone((7.1, 2.0), (6.95, 0.75), 0.42, ox, oy, FAR)
    column_foot((6.98, 0.55), [((7.35, 0.12), -20, 0.20), ((7.45, 0.10), -15, 0.18)], ox, oy, FAR, claw_toes=(0, 1))
    bone((10.9, 4.8), (10.6, 2.75), 0.48, ox, oy, FAR); bone((10.6, 2.75), (10.85, 1.25), 0.38, ox, oy, FAR)
    bone((10.85, 1.25), (10.95, 0.25), 0.30, ox, oy, FAR)
    # ribs: deep chest, highest at the shoulders
    for rx in [7.2 + i * 0.45 for i in range(9)]:
        top = 4.85 + 0.30 * (rx - 7.2) / 3.6
        depth = 2.6 - 0.9 * abs(rx - 9.6) / 2.4
        rib([(rx, top), (rx + 0.35, top - depth * 0.45), (rx + 0.20, top - depth)], 0.22, 0.07, ox, oy)
    spine = curve_pts([(0.0, 2.2), (2.5, 3.2), (5.0, 4.4), (6.6, 4.95), (8.5, 5.2), (10.6, 5.75), (11.4, 6.1),
                       (12.6, 7.6), (13.8, 9.4), (14.9, 11.0), (15.8, 12.1), (16.3, 12.4)], 12)
    spine = resample(spine, 0.42)
    vert_chain(spine, ox, oy, lambda x: 0.55 if 6.0 < x < 11.3 else (0.30 if x > 11.3 else 0.12 + 0.35 * x / 6.0),
               lambda x: 0.15 + 0.35 * x / 6.0 if 1.0 < x < 6.0 else 0.0, k=2.6, tail=6.0)
    # long neck ribs running along the neck
    for k in range(10):
        a = 11.6 + k * 0.45
        y = 6.2 + (a - 11.4) * 1.33
        poly([(a, y - 0.25), (a + 0.9, y + 0.95)], ox, oy, sw=1.0, fill="none", close=False, stroke="#888")
    # pelvis
    poly(curve_pts([(5.6, 5.15), (6.2, 5.75), (7.2, 5.85), (7.8, 5.45), (7.1, 5.05), (6.3, 4.95), (5.6, 5.15)], 5), ox, oy)
    ellipse(6.6, 3.95, 0.30, 0.30, ox, oy, sw=1.8)
    poly(curve_pts([(6.75, 3.85), (7.1, 3.2), (7.4, 2.75), (7.25, 2.68), (6.9, 3.15), (6.6, 3.75)], 4), ox, oy)  # pubis
    poly(curve_pts([(6.45, 3.85), (6.0, 3.30), (5.75, 3.0), (5.65, 3.08), (5.95, 3.45), (6.35, 4.0)], 4), ox, oy)  # ischium
    # shoulder blade
    poly(curve_pts([(11.0, 4.95), (10.6, 5.6), (10.0, 6.1), (9.85, 6.0), (10.3, 5.5), (10.8, 4.85)], 4), ox, oy)
    ellipse(11.0, 4.85, 0.32, 0.38, ox, oy)
    # near limbs: very long front legs (the "arm lizard")
    bone((11.0, 4.82), (10.7, 2.75), 0.52, ox, oy)            # humerus
    bone((10.7, 2.75), (10.95, 1.25), 0.40, ox, oy)           # radius + ulna
    bone((10.95, 1.25), (11.05, 0.25), 0.32, ox, oy)          # tall column of hand bones
    claw((11.20, 0.30), 160, 0.30, 0.15, ox, oy, curl=-30)   # single thumb claw
    poly([(10.80, 0.25), (11.30, 0.25), (11.35, 0.0), (10.75, 0.0)], ox, oy, sw=1.8)
    bone((6.6, 3.95), (7.0, 2.0), 0.58, ox, oy)               # femur
    bone((7.0, 2.0), (6.85, 0.75), 0.45, ox, oy)              # tibia
    column_foot((6.88, 0.55), [((7.15, 0.13), -25, 0.22), ((7.28, 0.11), -18, 0.20), ((7.40, 0.10), -12, 0.18), ((7.45, 0.08), -5, 0.13)], ox, oy, claw_toes=(0, 1, 2))
    # skull: arched nasal crest over the eyes, spoon-shaped teeth
    h0 = len(out)
    sk = curve_pts([(16.1, 12.25), (16.25, 12.75), (16.55, 13.05), (16.75, 12.85), (16.95, 12.45), (17.2, 12.15), (17.25, 11.95),
                    (17.0, 11.92), (16.6, 11.95), (16.25, 12.0), (16.1, 12.25)], 6)
    poly(sk, ox, oy, sw=2.4)
    poly(curve_pts([(16.25, 12.02), (16.7, 11.82), (17.1, 11.80), (17.22, 11.90)], 5), ox, oy, sw=1.6, fill="none", close=False)
    ellipse(16.38, 12.35, 0.11, 0.12, ox, oy, sw=1.5)
    ellipse(16.65, 12.75, 0.12, 0.18, ox, oy, rot=-20, sw=1.5)   # large nostril opening in the crest
    for tx in [16.85 + i * 0.07 for i in range(5)]:
        poly([(tx - 0.025, 11.95), (tx + 0.025, 11.95), (tx, 11.86)], ox, oy, sw=1.0)
    text(ox, 150, "SIDE VIEW", 22)

# ---------------- TOP VIEW ----------------
def top(ox, oy):
    for rx in [7.2 + i * 0.45 for i in range(9)]:
        span = 1.0 + 0.4 * (1 - abs(rx - 9.6) / 2.4)
        for s in (1, -1):
            rib([(rx, 0.15 * s), (rx + 0.2, 0.6 * span * s), (rx + 0.45, span * s)], 0.20, 0.07, ox, oy, sw=1.5)
    xs = [i * 0.42 for i in range(int(11.4 / 0.42))]
    for x in xs:
        ellipse(x, 0, 0.18, 0.12 + 0.15 * min(1, x / 6.0), ox, oy, sw=1.3)
    # neck stretched straight forward in this view
    for x in [11.4 + i * 0.5 for i in range(10)]:
        ellipse(x, 0, 0.22, 0.20 - 0.008 * (x - 11.4), ox, oy, sw=1.3)
    for s in (1, -1):
        poly(curve_pts([(5.6, 0.20 * s), (6.1, 0.80 * s), (6.9, 0.95 * s), (7.7, 0.55 * s), (7.0, 0.25 * s)], 5), ox, oy)
        ellipse(7.0, 1.05 * s, 0.32, 0.26, ox, oy)
        for k, d in enumerate((-0.18, -0.06, 0.06, 0.18)):
            digit((7.25, (1.05 + d) * s), (-14 + k * 9) * s, [0.18], 0.14, ox, oy, droop=0, end="claw" if k < 3 else None, clawL=0.25, curl=0)
        ellipse(10.75, 1.10 * s, 0.30, 0.25, ox, oy)
        poly(curve_pts([(10.9, 1.25 * s), (11.15, 1.15 * s), (11.15, 0.95 * s), (10.9, 0.88 * s)], 4), ox, oy, sw=1.6)
    sk = curve_pts([(16.1, 0.0), (16.15, 0.28), (16.5, 0.32), (16.9, 0.24), (17.25, 0.15), (17.3, 0.0), (17.25, -0.15), (16.9, -0.24), (16.5, -0.32), (16.15, -0.28), (16.1, 0.0)], 6)
    poly(sk, ox, oy, sw=2.4)
    for s in (1, -1):
        ellipse(16.4, 0.22 * s, 0.10, 0.07, ox, oy, sw=1.4)
    text(ox, oy - 1.6 * S, "TOP VIEW (neck straightened)", 22)

# ---------------- FRONT VIEW ----------------
def front(ox, oy):
    out.append(f'<line x1="{ox-2.2*S}" y1="{oy}" x2="{ox+2.2*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    poly(curve_pts([(0.0, 6.0), (1.20, 5.6), (1.50, 4.5), (1.20, 3.3), (0.0, 3.0), (-1.20, 3.3), (-1.50, 4.5), (-1.20, 5.6), (0.0, 6.0)], 6), ox, oy, stroke=FAR, sw=1.8)
    for s in (1, -1):
        bone((0.95 * s, 4.8), (1.05 * s, 2.75), 0.52, ox, oy)
        bone((1.05 * s, 2.75), (1.0 * s, 1.25), 0.40, ox, oy)
        bone((1.0 * s, 1.25), (1.0 * s, 0.25), 0.32, ox, oy)
        poly([(0.75 * s, 0.25), (1.25 * s, 0.25), (1.30 * s, 0.0), (0.70 * s, 0.0)], ox, oy, sw=1.8)
    for k in range(14):     # neck rising straight up toward the viewer
        ellipse(0.0, 6.2 + k * 0.42, 0.24 - k * 0.006, 0.17, ox, oy, sw=1.3)
    head = curve_pts([(0.0, 13.05), (0.24, 12.9), (0.30, 12.5), (0.22, 12.05), (0.0, 11.9), (-0.22, 12.05), (-0.30, 12.5), (-0.24, 12.9), (0.0, 13.05)], 6)
    poly(head, ox, oy, sw=2.2)
    for s in (1, -1):
        ellipse(0.18 * s, 12.45, 0.06, 0.08, ox, oy, sw=1.4)
        ellipse(0.10 * s, 12.80, 0.07, 0.10, ox, oy, sw=1.3)
    text(ox, oy + 32, "FRONT VIEW", 22, "600", "middle")

def feet_detail():
    global S
    keep = S; S = 130
    text(1000, 700, "FOOT DETAIL (from above, 3.5x scale)", 22)
    # hind foot: 5 toes, claws on I-III
    foot_top(1110, 990, [(-0.36, 0.30, [0.20], 0.17, "claw", "I"), (-0.18, 0.36, [0.18], 0.17, "claw", "II"), (0.0, 0.38, [0.16], 0.16, "claw", "III"),
                         (0.18, 0.34, [0.10], 0.14, False, "IV"), (0.34, 0.26, [], 0.11, False, "V")], 0)
    # hand: a tall horseshoe of bones with one thumb claw
    for k in range(5):
        a = math.radians(200 - k * 40)
        x, y = math.cos(a) * 0.32, 0.25 + math.sin(a) * 0.20
        bone((x, y - 0.05), (x, y + 0.45), 0.13, 1360, 990)
    claw((-0.30, 0.66), 125, 0.25, 0.13, 1360, 990, curl=0)
    S = keep
    text(1110, 1015, "Hind foot: 5 toes, claws on 3", 14, "600", "middle", "#333")
    text(1360, 1015, "Hand: column of bones, 1 thumb claw", 14, "600", "middle", "#333")

W, H = 1500, 1050
side(60, 610)
front(1330, 610)
top(60, 900)
feet_detail()
scalebar(60, 1015, 5, 1)
sheet(W, H, "BRACHIOSAURUS ALTITHORAX", "Skeletal reference · about 22 m long, head about 12.5 m up · built from Reference Library picks")
text(W - 40, 670, "Grey = far-side bones", 15, "400", "end", "#777")
save("brachio.svg", W, H)
print("ok")
