exec(open("lib.py").read())
S = 70   # px per metre (T. rex is ~12.3 m long)

def resample(pts, step):
    outp = [pts[0]]; acc = 0.0
    for i in range(1, len(pts)):
        (x1, y1), (x2, y2) = pts[i - 1], pts[i]
        d = math.hypot(x2 - x1, y2 - y1)
        while acc + d >= step:
            t = (step - acc) / d
            x1, y1 = x1 + (x2 - x1) * t, y1 + (y2 - y1) * t
            outp.append((x1, y1)); d = math.hypot(x2 - x1, y2 - y1); acc = 0.0
        acc += d
    return outp

def vert_chain(spine, ox, oy, spine_h, chev):
    """Vertebrae along a curve: centrum ovals, neural spines on top, chevrons under the tail."""
    for i in range(len(spine) - 1):
        (x1, y1), (x2, y2) = spine[i], spine[i + 1]
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        rot = math.degrees(math.atan2(y2 - y1, x2 - x1))
        sh = spine_h(mx)
        if sh > 0:
            poly([(mx - 0.035, my + 0.06), (mx + 0.035, my + 0.06), (mx + 0.015, my + 0.06 + sh), (mx - 0.05, my + 0.06 + sh)], ox, oy, sw=1.5)
        ch = chev(mx)
        if ch > 0:
            poly([(mx - 0.025, my - 0.06), (mx + 0.025, my - 0.06), (mx - 0.04, my - 0.06 - ch), (mx - 0.075, my - 0.06 - ch)], ox, oy, sw=1.4)
        size = 0.05 + 0.06 * min(1, mx / 5.5)
        ellipse(mx, my, size * 1.1, size * 0.85, ox, oy, rot=rot, sw=1.7)

def hind_leg(ox, oy, stroke, hip, knee, ankle, mt_end, toe_ang):
    bone(hip, knee, 0.30, ox, oy, stroke)          # femur
    bone(knee, ankle, 0.24, ox, oy, stroke)        # tibia
    bone((knee[0] + 0.06, knee[1] - 0.05), (ankle[0] + 0.05, ankle[1] + 0.05), 0.09, ox, oy, stroke)  # fibula
    bone(ankle, mt_end, 0.17, ox, oy, stroke)      # metatarsus (long, bird-like)
    ellipse(ankle[0], ankle[1], 0.13, 0.09, ox, oy, rot=-30, stroke=stroke, sw=1.8)
    # small dewclaw (toe I) up the back of the foot
    mx, my = ankle[0] * 0.55 + mt_end[0] * 0.45, ankle[1] * 0.55 + mt_end[1] * 0.45
    digit((mx, my), toe_ang - 40, [0.08], 0.05, ox, oy, stroke, droop=-10, clawL=0.08)
    for k, (dy, segs) in enumerate([(0.05, [0.17, 0.13]), (0.0, [0.20, 0.15, 0.12]), (-0.04, [0.16, 0.12, 0.10, 0.08])]):
        digit((mt_end[0], mt_end[1] + dy), toe_ang - k * 2, segs, 0.085 - k * 0.006, ox, oy, stroke, droop=-4, clawL=0.15, curl=25)


def scaled(start, ax, ay, ox, oy, k):
    """Wrap everything drawn since index `start` in a scale around metre point (ax, ay), keeping line widths."""
    X, Y = P(ax, ay, ox, oy)
    items = [e.replace("<path ", '<path vector-effect="non-scaling-stroke" ').replace("<ellipse ", '<ellipse vector-effect="non-scaling-stroke" ') for e in out[start:]]
    del out[start:]
    out.append(f'<g transform="translate({X:.1f},{Y:.1f}) scale({k}) translate({-X:.1f},{-Y:.1f})">' + "".join(items) + "</g>")

HEAD = 1.25

# ---------------- SIDE VIEW ----------------
def side(ox, oy):
    out.append(f'<line x1="{ox-20}" y1="{oy}" x2="{ox+12.6*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    # far leg, mid-stride, grey
    hind_leg(ox, oy, FAR, (6.0, 3.0), (6.55, 1.95), (6.45, 0.80), (6.85, 0.12), -4)
    # far arm
    bone((8.55, 2.50), (8.68, 2.13), 0.08, ox, oy, FAR)
    bone((8.68, 2.13), (8.92, 2.06), 0.06, ox, oy, FAR)

    spine = curve_pts([(0.0, 1.70), (1.5, 2.20), (3.2, 2.75), (4.8, 3.10), (6.0, 3.25), (7.2, 3.25), (8.3, 3.05),
                       (8.9, 3.10), (9.35, 3.40), (9.75, 3.55), (10.0, 3.45)], 12)
    spine = resample(spine, 0.21)
    # gastralia (belly ribs) under the gut
    for gx in [6.75 + i * 0.16 for i in range(10)]:
        gy = 1.72 + 0.30 * abs(gx - 7.45) ** 1.5
        rib([(gx - 0.18, gy + 0.08), (gx, gy - 0.02), (gx + 0.18, gy + 0.06)], 0.05, 0.03, ox, oy, sw=1.3)
    # ribs
    for rx in [6.55 + i * 0.24 for i in range(9)]:
        top = 3.15 - 0.05 * abs(rx - 7.4)
        depth = 1.45 - 0.45 * abs(rx - 7.5) / 1.2
        rib([(rx, top), (rx + 0.20, top - depth * 0.45), (rx + 0.12, top - depth)], 0.10, 0.03, ox, oy)
    vert_chain(spine, ox, oy,
               lambda x: 0.45 if 5.6 < x < 8.6 else (0.12 + 0.30 * x / 5.6 if x <= 5.6 else 0.10),
               lambda x: (0.12 + 0.35 * x / 5.6) if 0.5 < x < 5.6 else 0)
    # pelvis
    poly(curve_pts([(4.95, 3.40), (5.6, 3.80), (6.6, 3.85), (7.15, 3.55), (6.6, 3.25), (6.0, 3.15), (5.3, 3.20), (4.95, 3.40)], 5), ox, oy)
    ellipse(6.0, 3.0, 0.16, 0.16, ox, oy, sw=2.0)
    poly(curve_pts([(6.15, 2.95), (6.42, 2.30), (6.58, 1.80), (6.75, 1.62), (7.05, 1.55), (7.10, 1.45), (6.85, 1.38), (6.35, 1.40), (6.18, 1.48), (6.30, 1.62), (6.40, 1.80), (6.28, 2.35), (6.0, 2.92)], 5), ox, oy)   # pubis with its "boot"
    poly(curve_pts([(5.85, 2.90), (5.55, 2.50), (5.25, 2.15), (5.15, 2.10), (5.25, 2.25), (5.55, 2.65), (5.95, 3.0)], 4), ox, oy)  # ischium
    # shoulder girdle: long strap-like scapula + coracoid
    poly(curve_pts([(8.55, 2.65), (8.35, 2.95), (8.05, 3.30), (7.95, 3.35), (8.0, 3.20), (8.30, 2.85), (8.45, 2.55)], 4), ox, oy)
    ellipse(8.62, 2.52, 0.12, 0.14, ox, oy)
    # tiny near arm, two clawed fingers
    bone((8.58, 2.50), (8.72, 2.12), 0.09, ox, oy)
    bone((8.72, 2.12), (8.98, 2.05), 0.07, ox, oy)
    digit((8.98, 2.05), -20, [0.06, 0.05], 0.04, ox, oy, droop=-15, clawL=0.08)
    digit((8.98, 2.08), 5, [0.07, 0.05], 0.045, ox, oy, droop=-15, clawL=0.09)
    # near leg
    hind_leg(ox, oy, INK, (6.0, 3.0), (6.38, 1.85), (6.02, 0.76), (6.38, 0.12), 0)
    h0 = len(out)
    # skull
    sk = curve_pts([(9.85, 3.55), (9.95, 3.95), (10.35, 4.00), (10.9, 3.75), (11.35, 3.45), (11.55, 3.20), (11.50, 3.02),
                    (11.0, 3.02), (10.45, 3.05), (10.05, 3.08), (9.85, 3.30), (9.85, 3.55)], 6)
    poly(sk, ox, oy, sw=2.6)
    jaw = curve_pts([(9.95, 3.15), (10.5, 2.95), (11.1, 2.82), (11.45, 2.85), (11.42, 2.95), (11.0, 2.98), (10.45, 3.04), (10.0, 3.22)], 6)
    poly(jaw, ox, oy, sw=2.2)
    for tx in [10.55 + i * 0.09 for i in range(10)]:   # upper teeth
        poly([(tx - 0.025, 3.04), (tx + 0.025, 3.04), (tx + 0.005, 2.92)], ox, oy, sw=1.2)
    ellipse(10.25, 3.62, 0.13, 0.16, ox, oy, rot=10, sw=1.8)            # eye socket
    ellipse(10.75, 3.38, 0.28, 0.15, ox, oy, rot=-12, sw=1.8)           # antorbital opening
    ellipse(10.0, 3.55, 0.08, 0.16, ox, oy, sw=1.6)                     # temporal opening
    ellipse(11.32, 3.25, 0.08, 0.05, ox, oy, rot=-30, sw=1.6)           # nostril
    scaled(h0, 9.92, 3.32, ox, oy, HEAD)
    text(ox, oy - 4.3 * S, "SIDE VIEW", 22)

# ---------------- TOP VIEW ----------------
def top(ox, oy):
    # ribs and belly ribs first, backbone over them
    for rx in [6.55 + i * 0.24 for i in range(9)]:
        span = 0.55 + 0.20 * (1 - abs(rx - 7.6) / 1.2)
        for s in (1, -1):
            rib([(rx, 0.05 * s), (rx + 0.08, 0.6 * span * s), (rx + 0.18, span * s)], 0.09, 0.03, ox, oy, sw=1.6)
    spine_x = [i * 0.17 for i in range(0, 59)]
    for x in spine_x:
        w = 0.05 + 0.06 * min(1, x / 5.5)
        if x < 5.5 and x > 0.6:
            for s in (1, -1):
                poly([(x - 0.03, w * s), (x + 0.03, w * s), (x + 0.01, (w + 0.10 * min(1, x / 3)) * s), (x - 0.02, (w + 0.10 * min(1, x / 3)) * s)], ox, oy, sw=1.2)
        ellipse(x, 0, 0.07, w, ox, oy, sw=1.5)
    # neck
    for x in [8.95 + i * 0.17 for i in range(6)]:
        ellipse(x, 0, 0.07, 0.10, ox, oy, sw=1.5)
    # ilia (long, narrow, close to midline)
    for s in (1, -1):
        poly(curve_pts([(4.95, 0.10 * s), (5.6, 0.32 * s), (6.6, 0.38 * s), (7.15, 0.18 * s), (6.6, 0.12 * s), (5.6, 0.10 * s)], 5), ox, oy)
    # legs: knees and feet outside the hips, toes forward
    for s in (1, -1):
        ellipse(6.38, 0.62 * s, 0.17, 0.13, ox, oy)
        for k, d in enumerate((-0.10, 0.0, 0.10)):
            digit((6.45, (0.62 + d) * s), (-12 + k * 12) * s, [0.17, 0.13, 0.10], 0.08, ox, oy, droop=0, clawL=0.14, curl=0)
    # tiny arms
    for s in (1, -1):
        bone((8.55, 0.35 * s), (8.85, 0.50 * s), 0.08, ox, oy)
        digit((8.85, 0.50 * s), 10 * s, [0.06, 0.05], 0.04, ox, oy, droop=0, clawL=0.08, curl=0)
    h0 = len(out)
    # skull from above: wide at the back, narrow snout
    sk = curve_pts([(9.9, 0.0), (9.95, 0.42), (10.3, 0.50), (10.8, 0.36), (11.3, 0.20), (11.55, 0.0),
                    (11.3, -0.20), (10.8, -0.36), (10.3, -0.50), (9.95, -0.42), (9.9, 0.0)], 6)
    poly(sk, ox, oy, sw=2.6)
    for s in (1, -1):
        ellipse(10.25, 0.33 * s, 0.12, 0.08, ox, oy, sw=1.6)
        ellipse(10.05, 0.18 * s, 0.10, 0.07, ox, oy, sw=1.4)
    scaled(h0, 9.9, 0.0, ox, oy, HEAD)
    text(ox, oy - 1.3 * S, "TOP VIEW", 22)

# ---------------- FRONT VIEW ----------------
def front(ox, oy):
    out.append(f'<line x1="{ox-1.4*S}" y1="{oy}" x2="{ox+1.4*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    poly(curve_pts([(0.0, 3.30), (0.55, 3.10), (0.70, 2.60), (0.55, 2.05), (0.0, 1.85), (-0.55, 2.05), (-0.70, 2.60), (-0.55, 3.10), (0.0, 3.30)], 6), ox, oy, stroke=FAR, sw=1.8)
    for s in (1, -1):
        bone((0.40 * s, 3.0), (0.55 * s, 1.9), 0.30, ox, oy)
        bone((0.55 * s, 1.9), (0.42 * s, 0.78), 0.24, ox, oy)
        bone((0.42 * s, 0.78), (0.45 * s, 0.14), 0.17, ox, oy)
        for d in (-0.10, 0.0, 0.10):
            claw(((0.45 + d * 0.8) * s, 0.14), -90 + d * 120 * s, 0.13, 0.08, ox, oy, curl=0)
        # tiny arms
        bone((0.42 * s, 2.50), (0.52 * s, 2.15), 0.08, ox, oy)
        claw((0.52 * s, 2.15), -90, 0.10, 0.05, ox, oy, curl=0)
    h0 = len(out)
    # head on: deep, wide-cheeked skull
    head = curve_pts([(0.0, 4.02), (0.38, 3.90), (0.48, 3.50), (0.36, 3.05), (0.18, 2.86), (0.0, 2.82),
                      (-0.18, 2.86), (-0.36, 3.05), (-0.48, 3.50), (-0.38, 3.90), (0.0, 4.02)], 6)
    poly(head, ox, oy, sw=2.6)
    for s in (1, -1):
        ellipse(0.33 * s, 3.62, 0.09, 0.14, ox, oy, rot=-20 * s, sw=1.8)   # eye sockets, angled
        ellipse(0.40 * s, 3.25, 0.06, 0.12, ox, oy, rot=10 * s, sw=1.4)    # cheek opening
        ellipse(0.10 * s, 3.08, 0.035, 0.05, ox, oy, sw=1.4)              # nostrils
    poly([(-0.06, 3.95), (0.06, 3.95), (0.04, 3.40), (-0.04, 3.40)], ox, oy, sw=1.3)  # nasal ridge
    for tx in (-0.26, -0.16, -0.06, 0.06, 0.16, 0.26):
        poly([(tx - 0.03, 2.98), (tx + 0.03, 2.98), (tx, 2.86)], ox, oy, sw=1.2)
    scaled(h0, 0.0, 3.0, ox, oy, HEAD)
    text(ox, oy + 32, "FRONT VIEW", 22, "600", "middle")

def feet_detail():
    global S
    keep = S; S = 280
    text(1060, 520, "FOOT DETAIL (from above, 4x scale)", 22)
    gx, gy = 1180, 840
    # metatarsals fanning up into three big clawed toes; small dewclaw I and splint V
    for x0, ml, segs, w, lab in [(-0.08, 0.38, [0.17, 0.13], 0.075, "II"), (0.0, 0.42, [0.20, 0.15, 0.12], 0.085, "III"), (0.08, 0.38, [0.16, 0.12, 0.10, 0.08], 0.075, "IV")]:
        x1 = x0 * 1.6
        bone((x0, 0.0), (x1, ml), w, gx, gy)
        ang = 90 - x0 * 260
        digit((x1, ml), ang, segs, w, gx, gy, droop=0, clawL=0.16, curl=0)
    bone((-0.11, 0.05), (-0.14, 0.18), 0.04, gx, gy)
    claw((-0.14, 0.18), 120, 0.08, 0.05, gx, gy, curl=0)
    bone((0.10, 0.02), (0.12, 0.14), 0.03, gx, gy)
    ellipse(0.0, -0.01, 0.13, 0.05, gx, gy, sw=1.8)
    # hand: two clawed fingers + a tiny third
    hx = 1400
    bone((0.0, 0.0), (-0.03, 0.10), 0.04, hx, gy); digit((-0.03, 0.10), 100, [0.06, 0.05], 0.035, hx, gy, droop=0, clawL=0.07, curl=0)
    bone((0.04, 0.0), (0.05, 0.11), 0.04, hx, gy); digit((0.05, 0.11), 85, [0.07, 0.06], 0.038, hx, gy, droop=0, clawL=0.08, curl=0)
    bone((0.08, 0.0), (0.10, 0.06), 0.02, hx, gy)
    ellipse(0.04, -0.01, 0.07, 0.03, hx, gy, sw=1.6)
    S = keep
    text(gx, gy + 28, "Hind foot: 3 big toes, small dewclaw", 14, "600", "middle", "#333")
    text(hx + 10, gy + 28, "Hand: 2 fingers", 14, "600", "middle", "#333")

W, H = 1500, 900
side(60, 410)
front(1330, 410)
top(60, 690)
feet_detail()
scalebar(60, 860, 4)
sheet(W, H, "TYRANNOSAURUS REX", "Skeletal reference · about 12.5 m long · built from Reference Library picks")
text(W - 40, 470, "Grey = far-side bones", 15, "400", "end", "#777")
save("trex.svg", W, H)
print("ok")
