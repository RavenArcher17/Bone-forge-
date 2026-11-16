exec(open("lib.py").read())
S = 330   # px per metre (Velociraptor is ~2 m long)

def hind_leg(ox, oy, stroke, hip, knee, ankle, mt_end):
    bone(hip, knee, 0.040, ox, oy, stroke)                     # femur
    bone(knee, ankle, 0.032, ox, oy, stroke)                   # tibia
    bone((knee[0] + 0.008, knee[1] - 0.01), (ankle[0] + 0.006, ankle[1] + 0.01), 0.012, ox, oy, stroke)  # fibula
    bone(ankle, mt_end, 0.024, ox, oy, stroke)                 # metatarsus
    ellipse(ankle[0], ankle[1], 0.018, 0.013, ox, oy, rot=-30, stroke=stroke, sw=1.8)
    # toe II: held up off the ground, with the big sickle claw
    digit((mt_end[0] + 0.004, mt_end[1] + 0.006), 40, [0.022, 0.018], 0.012, ox, oy, stroke, droop=-25, clawL=0.065, curl=120)
    # toes III and IV on the ground
    for k, (dy, segs) in enumerate([(0.0, [0.026, 0.020, 0.016]), (-0.004, [0.022, 0.016, 0.013, 0.010])]):
        digit((mt_end[0], mt_end[1] + dy), -8 - k * 3, segs, 0.011, ox, oy, stroke, droop=-3, clawL=0.022, curl=40)
    # toe I dewclaw
    mx, my = ankle[0] * 0.3 + mt_end[0] * 0.7, ankle[1] * 0.3 + mt_end[1] * 0.7
    digit((mx, my), -150, [0.012], 0.007, ox, oy, stroke, droop=0, clawL=0.014, curl=40)

def arm(ox, oy, stroke, sh, el, wr, hand_ang):
    bone(sh, el, 0.020, ox, oy, stroke)       # humerus
    bone(el, wr, 0.015, ox, oy, stroke)       # radius + ulna
    bone((el[0] + 0.004, el[1] - 0.006), (wr[0] + 0.002, wr[1] - 0.006), 0.008, ox, oy, stroke)
    for k, (segs, cl) in enumerate([([0.040, 0.020], 0.030), ([0.050, 0.030, 0.022], 0.034), ([0.040, 0.020, 0.016, 0.012], 0.026)]):
        digit(wr, hand_ang - k * 10, segs, 0.009, ox, oy, stroke, droop=-12, clawL=cl, curl=70)

# ---------------- SIDE VIEW ----------------
def side(ox, oy):
    out.append(f'<line x1="{ox-20}" y1="{oy}" x2="{ox+2.05*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    hind_leg(ox, oy, FAR, (1.0, 0.50), (1.14, 0.36), (1.08, 0.14), (1.16, 0.03))
    arm(ox, oy, FAR, (1.37, 0.49), (1.31, 0.37), (1.44, 0.33), -40)
    spine = curve_pts([(0.0, 0.56), (0.5, 0.575), (0.95, 0.585), (1.15, 0.60), (1.38, 0.58), (1.46, 0.63), (1.52, 0.72), (1.57, 0.80), (1.62, 0.83)], 12)
    spine = resample(spine, 0.034)
    # belly ribs + ribs
    for gx in [1.10 + i * 0.03 for i in range(9)]:
        gy = 0.36 + 0.4 * abs(gx - 1.22) ** 1.6
        rib([(gx - 0.03, gy + 0.012), (gx, gy - 0.002), (gx + 0.03, gy + 0.01)], 0.008, 0.005, ox, oy, sw=1.2)
    for rx in [1.06 + i * 0.04 for i in range(8)]:
        top = 0.585
        depth = 0.21 - 0.07 * abs(rx - 1.22) / 0.16
        rib([(rx, top), (rx + 0.03, top - depth * 0.45), (rx + 0.018, top - depth)], 0.014, 0.005, ox, oy)
    # tail: rod-like bony extensions stiffen it, so long thin lines run along it
    for k in range(4):
        y = 0.583 + (0.007 if k % 2 == 0 else -0.007) * (1 + k // 2)
        poly([(0.10, y - 0.002), (0.90, y)], ox, oy, sw=0.8, fill="none", close=False, stroke="#888")
    vert_chain(spine, ox, oy,
               lambda x: 0.035 if 0.95 < x < 1.40 else (0.012 + 0.012 * x / 0.95 if x <= 0.95 else 0.012),
               lambda x: (0.010 + 0.025 * x / 0.95) if 0.15 < x < 0.95 else 0, k=0.16, tail=0.95)
    # pelvis: ilium, backward-pointing pubis, short ischium
    poly(curve_pts([(0.84, 0.60), (0.92, 0.66), (1.06, 0.665), (1.15, 0.62), (1.06, 0.585), (0.95, 0.575), (0.84, 0.60)], 5), ox, oy)
    ellipse(1.0, 0.505, 0.022, 0.022, ox, oy, sw=1.8)
    poly(curve_pts([(1.02, 0.49), (0.98, 0.40), (0.94, 0.31), (0.92, 0.29), (0.89, 0.29), (0.91, 0.32), (0.95, 0.41), (0.98, 0.50)], 4), ox, oy)  # pubis
    poly(curve_pts([(0.97, 0.50), (0.92, 0.45), (0.87, 0.42), (0.86, 0.43), (0.90, 0.47), (0.95, 0.52)], 4), ox, oy)  # ischium
    # shoulder girdle
    poly(curve_pts([(1.38, 0.50), (1.34, 0.55), (1.28, 0.61), (1.26, 0.61), (1.29, 0.57), (1.35, 0.49)], 4), ox, oy)
    ellipse(1.385, 0.49, 0.016, 0.02, ox, oy)
    # near limbs
    arm(ox, oy, INK, (1.385, 0.49), (1.32, 0.38), (1.46, 0.34), -35)
    hind_leg(ox, oy, INK, (1.0, 0.505), (1.085, 0.35), (1.02, 0.135), (1.09, 0.028))
    # skull: long, low, slightly upturned snout
    h0 = len(out)
    sk = curve_pts([(1.595, 0.86), (1.62, 0.905), (1.70, 0.905), (1.79, 0.875), (1.86, 0.835), (1.875, 0.81), (1.86, 0.795),
                    (1.76, 0.80), (1.67, 0.81), (1.61, 0.82), (1.595, 0.86)], 6)
    poly(sk, ox, oy, sw=2.4)
    poly(curve_pts([(1.61, 0.82), (1.70, 0.79), (1.80, 0.775), (1.865, 0.785), (1.86, 0.795), (1.76, 0.800), (1.66, 0.812)], 6), ox, oy, sw=2.0)
    for tx in [1.70 + i * 0.014 for i in range(11)]:
        poly([(tx - 0.004, 0.80), (tx + 0.004, 0.80), (tx + 0.001, 0.788)], ox, oy, sw=1.0)
    ellipse(1.645, 0.868, 0.019, 0.019, ox, oy, sw=1.7)                    # eye socket
    ellipse(1.72, 0.85, 0.035, 0.016, ox, oy, rot=-8, sw=1.6)              # antorbital opening
    ellipse(1.612, 0.85, 0.010, 0.020, ox, oy, sw=1.4)                     # temporal opening
    ellipse(1.84, 0.825, 0.010, 0.006, ox, oy, rot=-20, sw=1.3)            # nostril
    text(ox, 110, "SIDE VIEW", 22)

# ---------------- TOP VIEW ----------------
def top(ox, oy):
    for rx in [1.06 + i * 0.04 for i in range(8)]:
        span = 0.09 + 0.03 * (1 - abs(rx - 1.22) / 0.16)
        for s in (1, -1):
            rib([(rx, 0.008 * s), (rx + 0.012, 0.6 * span * s), (rx + 0.028, span * s)], 0.012, 0.005, ox, oy, sw=1.4)
    for x in [i * 0.034 for i in range(0, 42)]:
        w = 0.008 + 0.010 * min(1, x / 0.95)
        ellipse(x, 0, 0.014, w, ox, oy, sw=1.3)
    for x in [1.42 + i * 0.034 for i in range(6)]:
        ellipse(x, 0, 0.014, 0.013, ox, oy, sw=1.3)
    for s in (1, -1):
        poly(curve_pts([(0.84, 0.015 * s), (0.92, 0.045 * s), (1.06, 0.05 * s), (1.15, 0.025 * s), (1.06, 0.015 * s)], 5), ox, oy)
        # legs: knee and foot just outside the hips
        ellipse(1.085, 0.075 * s, 0.022, 0.016, ox, oy)
        for k, d in enumerate((-0.012, 0.0, 0.012)):
            if k == 0:
                digit((1.10, (0.075 + d) * s), (-10) * s, [0.020, 0.016], 0.010, ox, oy, droop=0, clawL=0.05, curl=0)  # sickle-claw toe
            else:
                digit((1.10, (0.075 + d) * s), (-4 + k * 10) * s, [0.026, 0.020, 0.016], 0.011, ox, oy, droop=0, clawL=0.022, curl=0)
        # folded arms tucked along the body
        bone((1.385, 0.045 * s), (1.33, 0.085 * s), 0.018, ox, oy)
        bone((1.33, 0.085 * s), (1.46, 0.09 * s), 0.014, ox, oy)
        for k in range(3):
            digit((1.46, 0.09 * s), (-6 + k * 8) * s, [0.04, 0.025], 0.008, ox, oy, droop=0, clawL=0.028, curl=0)
    # skull from above: narrow and long
    sk = curve_pts([(1.595, 0.0), (1.60, 0.040), (1.66, 0.048), (1.75, 0.030), (1.84, 0.018), (1.875, 0.0),
                    (1.84, -0.018), (1.75, -0.030), (1.66, -0.048), (1.60, -0.040), (1.595, 0.0)], 6)
    poly(sk, ox, oy, sw=2.4)
    for s in (1, -1):
        ellipse(1.645, 0.032 * s, 0.016, 0.010, ox, oy, sw=1.4)
    text(ox, oy - 0.25 * S, "TOP VIEW", 22)

# ---------------- FRONT VIEW ----------------
def front(ox, oy):
    out.append(f'<line x1="{ox-0.25*S}" y1="{oy}" x2="{ox+0.25*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    poly(curve_pts([(0.0, 0.60), (0.09, 0.57), (0.12, 0.48), (0.09, 0.40), (0.0, 0.37), (-0.09, 0.40), (-0.12, 0.48), (-0.09, 0.57), (0.0, 0.60)], 6), ox, oy, stroke=FAR, sw=1.8)
    for s in (1, -1):
        bone((0.05 * s, 0.50), (0.08 * s, 0.35), 0.04, ox, oy)
        bone((0.08 * s, 0.35), (0.065 * s, 0.135), 0.032, ox, oy)
        bone((0.065 * s, 0.135), (0.07 * s, 0.025), 0.024, ox, oy)
        for d in (-0.010, 0.010):
            claw(((0.07 + d) * s, 0.028), -90, 0.022, 0.012, ox, oy, curl=0)
        claw((0.072 * s, 0.07), 90, 0.03, 0.012, ox, oy, curl=0)   # sickle claw held up
        # folded arms in front of the chest
        bone((0.07 * s, 0.49), (0.10 * s, 0.38), 0.02, ox, oy)
        bone((0.10 * s, 0.38), (0.06 * s, 0.35), 0.015, ox, oy)
        claw((0.06 * s, 0.35), -90, 0.035, 0.012, ox, oy, curl=0)
    for k in range(6):   # neck vertebrae seen from the front
        ellipse(0.0, 0.61 + k * 0.034, 0.016 + 0.002 * (5 - k), 0.014, ox, oy, sw=1.4)
    head = curve_pts([(0.0, 0.91), (0.045, 0.90), (0.05, 0.86), (0.035, 0.81), (0.0, 0.785), (-0.035, 0.81), (-0.05, 0.86), (-0.045, 0.90), (0.0, 0.91)], 6)
    poly(head, ox, oy, sw=2.2)
    for s in (1, -1):
        ellipse(0.035 * s, 0.872, 0.012, 0.016, ox, oy, sw=1.5)
    text(ox, oy + 32, "FRONT VIEW", 22, "600", "middle")

def feet_detail():
    global S
    keep = S; S = 1100
    text(1000, 520, "FOOT DETAIL (side, 3x scale)", 22)
    gy = 850
    ox = 1060 - 1.02 * S
    out.append(f'<line x1="1000" y1="{gy}" x2="1260" y2="{gy}" stroke="#bbb" stroke-width="1.5"/>')
    bone((1.05, 0.25), (1.02, 0.135), 0.032, ox, gy)
    bone((1.02, 0.135), (1.09, 0.028), 0.024, ox, gy)
    ellipse(1.02, 0.135, 0.018, 0.013, ox, gy, rot=-30, sw=1.8)
    digit((1.094, 0.034), 40, [0.022, 0.018], 0.012, ox, gy, droop=-25, clawL=0.065, curl=120)
    for k, (dy, segs) in enumerate([(0.0, [0.026, 0.020, 0.016]), (-0.004, [0.022, 0.016, 0.013, 0.010])]):
        digit((1.09, 0.028 + dy), -8 - k * 3, segs, 0.011, ox, gy, droop=-3, clawL=0.022, curl=40)
    hx = 1310 - 1.40 * S; hy = gy - 150 + 0.36 * S
    bone((1.40, 0.36), (1.46, 0.34), 0.015, hx, hy)
    for k, (segs, cl) in enumerate([([0.040, 0.020], 0.030), ([0.050, 0.030, 0.022], 0.034), ([0.040, 0.020, 0.016, 0.012], 0.026)]):
        digit((1.46, 0.34), -35 - k * 12, segs, 0.009, hx, hy, droop=-12, clawL=cl, curl=70)
    S = keep
    text(1130, gy + 28, "Hind foot: sickle claw on toe II", 14, "600", "middle", "#333")
    text(1370, gy + 28, "Hand: 3 clawed fingers", 14, "600", "middle", "#333")

W, H = 1500, 920
side(60, 400)
front(1330, 400)
top(60, 700)
feet_detail()
scalebar(60, 880, 0.5, 0.1)
sheet(W, H, "VELOCIRAPTOR MONGOLIENSIS", "Skeletal reference · about 2 m long · built from Reference Library picks")
text(W - 40, 480, "Grey = far-side bones", 15, "400", "end", "#777")
save("raptor.svg", W, H)
print("ok")
