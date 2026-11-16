exec(open("lib.py").read())
S = 110   # px per metre (wingspan ~6.2 m)
MEM = "#8a8a8a"

def dashed(pts, ox, oy):
    d = " ".join(("M" if i == 0 else "L") + f"{P(x, y, ox, oy)[0]:.1f},{P(x, y, ox, oy)[1]:.1f}" for i, (x, y) in enumerate(pts))
    out.append(f'<path d="{d}" fill="none" stroke="{MEM}" stroke-width="1.3" stroke-dasharray="6 5"/>')

# ---------------- TOP VIEW (flight) ----------------
def top(ox, oy):
    """x = sideways (m), y = forward (m); head points up the page."""
    for s in (1, -1):
        # wing membrane outline (soft tissue, dashed)
        dashed(curve_pts([(0.42 * s, 0.30), (1.6 * s, 0.32), (3.1 * s, -0.12), (2.0 * s, -0.42), (0.9 * s, -0.45), (0.28 * s, -0.42)], 8), ox, oy)
        sh = (0.13 * s, 0.18)
        el = (0.42 * s, 0.24); wr = (1.0 * s, 0.30); mc = (1.62 * s, 0.30)
        bone(sh, el, 0.07, ox, oy)                        # humerus
        bone(el, wr, 0.06, ox, oy)                        # radius + ulna
        bone((el[0], el[1] - 0.02), (wr[0], wr[1] - 0.02), 0.025, ox, oy)
        bone((wr[0] - 0.02 * s, wr[1] + 0.02), (0.72 * s, 0.48), 0.02, ox, oy)   # pteroid bone
        bone(wr, mc, 0.05, ox, oy)                        # long wing metacarpal
        for k in range(3):                                 # three small clawed fingers
            digit((mc[0] - 0.02 * s, mc[1] + 0.02), 75 + (10 + k * 8) * s, [0.06, 0.04], 0.014, ox, oy, droop=0, clawL=0.03, curl=0)
        # wing finger: four long phalanges sweeping back to the tip
        pts = [mc, (2.15 * s, 0.22), (2.55 * s, 0.10), (2.86 * s, -0.02), (3.1 * s, -0.12)]
        for i in range(4):
            bone(pts[i], pts[i + 1], 0.045 - i * 0.008, ox, oy)
        # hind legs trailing under the membrane
        bone((0.08 * s, -0.22), (0.20 * s, -0.36), 0.04, ox, oy)
        bone((0.20 * s, -0.36), (0.26 * s, -0.55), 0.032, ox, oy)
        for k in range(4):
            digit((0.26 * s, -0.55), -90 + (-12 + k * 8) * s, [0.04, 0.03], 0.01, ox, oy, droop=0, clawL=0.025, curl=0)
    # body: short torso, ribs, tiny tail
    for ry in [0.16 - i * 0.07 for i in range(5)]:
        for s in (1, -1):
            rib([(0.02 * s, ry), (0.08 * s, ry - 0.01), (0.13 * s, ry - 0.05)], 0.02, 0.008, ox, oy, sw=1.4)
    for y in [0.20 - i * 0.055 for i in range(11)]:
        ellipse(0, y, 0.022, 0.018, ox, oy, sw=1.3)
    for s in (1, -1):
        poly(curve_pts([(0.02 * s, -0.12), (0.10 * s, -0.16), (0.11 * s, -0.28), (0.03 * s, -0.30)], 4), ox, oy)  # pelvis
        poly(curve_pts([(0.03 * s, 0.24), (0.13 * s, 0.20), (0.15 * s, 0.13), (0.05 * s, 0.14)], 4), ox, oy)    # shoulder
    for y in [0.27 + i * 0.06 for i in range(6)]:         # neck
        ellipse(0, y, 0.03, 0.028, ox, oy, sw=1.3)
    # skull: long toothless beak forward, crest pointing back over the neck
    sk = curve_pts([(0.0, 0.62), (0.07, 0.66), (0.065, 0.85), (0.035, 1.25), (0.012, 1.65), (0.0, 1.78), (-0.012, 1.65), (-0.035, 1.25), (-0.065, 0.85), (-0.07, 0.66), (0.0, 0.62)], 6)
    poly(sk, ox, oy, sw=2.2)
    for s in (1, -1):
        ellipse(0.05 * s, 0.76, 0.02, 0.03, ox, oy, sw=1.3)
    poly([(-0.025, 0.66), (0.025, 0.66), (0.012, 0.32), (-0.012, 0.32)], ox, oy, sw=1.6)  # crest seen from above
    text(ox - 3.2 * S, oy - 1.55 * S, "TOP VIEW (wings spread)", 22)

# ---------------- FRONT VIEW (flight) ----------------
def front(ox, oy):
    """x = sideways, y = up; seen head-on in flight."""
    for s in (1, -1):
        pts = [(0.10 * s, 0.0), (0.40 * s, 0.06), (1.0 * s, 0.16), (1.62 * s, 0.22), (2.15 * s, 0.24), (2.55 * s, 0.22), (2.86 * s, 0.17), (3.1 * s, 0.10)]
        ws = [0.07, 0.06, 0.05, 0.045, 0.037, 0.03, 0.022]
        dashed([(0.12 * s, -0.06), (1.6 * s, 0.13), (3.1 * s, 0.08)], ox, oy)
        for i in range(len(pts) - 1):
            bone(pts[i], pts[i + 1], ws[i], ox, oy)
        bone((0.06 * s, -0.10), (0.10 * s, -0.32), 0.035, ox, oy)   # legs hanging back and down
        bone((0.10 * s, -0.32), (0.11 * s, -0.48), 0.028, ox, oy)
    poly(curve_pts([(0.0, 0.10), (0.10, 0.06), (0.11, -0.08), (0.0, -0.14), (-0.11, -0.08), (-0.10, 0.06), (0.0, 0.10)], 6), ox, oy, stroke=FAR, sw=1.6)
    head = curve_pts([(0.0, 0.42), (0.07, 0.36), (0.07, 0.22), (0.0, 0.16), (-0.07, 0.22), (-0.07, 0.36), (0.0, 0.42)], 6)
    poly(head, ox, oy, sw=2.2)
    poly([(-0.015, 0.42), (0.015, 0.42), (0.008, 0.66), (-0.008, 0.66)], ox, oy, sw=1.6)   # crest, edge-on
    for s in (1, -1):
        ellipse(0.045 * s, 0.30, 0.018, 0.025, ox, oy, sw=1.3)
    text(ox - 3.2 * S, oy - 0.9 * S, "FRONT VIEW (in flight)", 22)

# ---------------- SIDE VIEW (standing, wings folded) ----------------
def side(ox, oy):
    out.append(f'<line x1="{ox-10}" y1="{oy}" x2="{ox+2.6*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    # far limbs (grey)
    bone((0.45, 0.72), (0.66, 0.46), 0.045, ox, oy, FAR); bone((0.66, 0.46), (0.55, 0.08), 0.035, ox, oy, FAR)
    bone((1.00, 1.00), (0.92, 0.66), 0.06, ox, oy, FAR); bone((0.92, 0.66), (1.12, 0.48), 0.05, ox, oy, FAR)
    bone((1.12, 0.48), (1.30, 0.02), 0.045, ox, oy, FAR)
    # torso: short, with ribs
    for rx in [0.62 + i * 0.07 for i in range(6)]:
        rib([(rx, 1.0), (rx + 0.04, 0.90), (rx + 0.03, 0.76)], 0.02, 0.008, ox, oy, sw=1.4)
    spine = resample(curve_pts([(0.20, 0.70), (0.45, 0.82), (0.75, 1.00), (1.00, 1.08), (1.10, 1.22), (1.16, 1.38), (1.22, 1.50)], 10), 0.06)
    vert_chain(spine, ox, oy, lambda x: 0.03 if 0.45 < x < 1.0 else 0.0, lambda x: 0.0, k=0.35, tail=0.5)
    poly(curve_pts([(0.30, 0.80), (0.42, 0.86), (0.62, 0.90), (0.58, 0.82), (0.40, 0.76)], 4), ox, oy)  # pelvis
    ellipse(0.45, 0.74, 0.03, 0.03, ox, oy, sw=1.5)
    poly(curve_pts([(1.02, 1.04), (0.95, 1.10), (0.85, 1.12), (0.88, 1.06), (0.98, 1.00)], 4), ox, oy)  # shoulder
    # near hind leg: long and slim, flat-footed
    bone((0.45, 0.74), (0.62, 0.45), 0.05, ox, oy); bone((0.62, 0.45), (0.50, 0.09), 0.04, ox, oy)
    bone((0.50, 0.09), (0.60, 0.02), 0.03, ox, oy)
    for k in range(4):
        digit((0.60, 0.02 + 0.004 * k), -5 - k * 3, [0.04, 0.03], 0.012, ox, oy, droop=-2, clawL=0.03, curl=40)
    # near arm: folded wing, walking on the hand
    bone((1.00, 1.00), (0.90, 0.65), 0.07, ox, oy)         # humerus
    bone((0.90, 0.65), (1.10, 0.47), 0.055, ox, oy)        # forearm
    bone((1.10, 0.47), (1.28, 0.03), 0.05, ox, oy)         # wing metacarpal down to the ground
    for k in range(3):
        digit((1.28, 0.05), -10 + k * 8, [0.04, 0.03], 0.012, ox, oy, droop=-10, clawL=0.035, curl=50)
    # wing finger folded back up along the body
    pts = [(1.28, 0.06), (1.05, 0.55), (0.82, 0.95), (0.55, 1.22), (0.30, 1.38)]
    for i in range(4):
        bone(pts[i], pts[i + 1], 0.045 - i * 0.008, ox, oy)
    # skull: long pointed beak and a long backward crest
    h0 = len(out)
    sk = curve_pts([(1.18, 1.48), (1.12, 1.62), (0.90, 1.82), (0.55, 1.98), (0.62, 1.93), (1.00, 1.72), (1.30, 1.62),
                    (1.70, 1.50), (2.10, 1.38), (2.40, 1.30), (2.10, 1.32), (1.70, 1.40), (1.35, 1.44), (1.18, 1.48)], 5)
    poly(sk, ox, oy, sw=2.2)
    poly(curve_pts([(1.30, 1.44), (1.75, 1.36), (2.15, 1.30), (2.38, 1.29)], 5), ox, oy, sw=1.4, fill="none", close=False)
    ellipse(1.28, 1.55, 0.035, 0.035, ox, oy, sw=1.4)
    ellipse(1.50, 1.50, 0.09, 0.025, ox, oy, rot=-12, sw=1.3)
    text(ox, oy - 2.2 * S, "SIDE VIEW (standing)", 22)

def feet_detail():
    global S
    keep = S; S = 600
    text(1000, 640, "HAND AND FOOT DETAIL (5.5x scale)", 22)
    gy = 920
    # hand from above: 3 small clawed fingers + the huge wing finger knuckle
    hx = 1110
    bone((0.0, 0.0), (0.0, 0.16), 0.05, hx, gy)                       # end of the wing metacarpal
    for k in range(3):
        digit((-0.01 - k * 0.012, 0.14), 110 + k * 14, [0.06, 0.04], 0.013, hx, gy, droop=0, clawL=0.035, curl=0)
    bone((0.01, 0.16), (0.12, 0.26), 0.045, hx, gy)                    # start of the wing finger
    text(hx + int(0.17 * S), gy - int(0.22 * S), "wing finger", 13, "600", "start", "#555")
    # foot from above: 4 clawed toes and a short fifth
    fx = 1360
    ellipse(0.0, 0.0, 0.05, 0.015, fx, gy, sw=1.5)
    for k in range(4):
        x0 = -0.03 + k * 0.02
        bone((x0, 0.0), (x0 * 1.4, 0.10), 0.014, fx, gy)
        digit((x0 * 1.4, 0.10), 90 - x0 * 600, [0.035, 0.025], 0.012, fx, gy, droop=0, clawL=0.03, curl=0)
    bone((0.05, 0.0), (0.075, 0.05), 0.01, fx, gy)
    S = keep
    text(hx, gy + 28, "Hand: 3 small clawed fingers", 14, "600", "middle", "#333")
    text(fx, gy + 28, "Foot: 4 clawed toes", 14, "600", "middle", "#333")

W, H = 1500, 970
top(420, 360)
front(420, 650)
side(1040, 470)
feet_detail()
scalebar(60, 930, 2, 0.5)
sheet(W, H, "PTERANODON LONGICEPS", "Skeletal reference · wingspan about 6.2 m · built from Reference Library picks")
text(W - 40, 525, "Grey = far-side bones · dashed = wing membrane", 15, "400", "end", "#777")
save("ptera.svg", W, H)
print("ok")
