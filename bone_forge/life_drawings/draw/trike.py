exec(open("lib.py").read())
"""Triceratops skeletal reference sheet, clean line art.
Coordinates in metres; side view x = from tail tip forward, y = height above ground."""
import math

S = 90          # px per metre
INK = "#111"
FAR = "#9a9a9a"  # far-side limbs
out = []

def P(x, y, ox, oy):
    return ox + x * S, oy - y * S

def poly(pts, ox, oy, stroke=INK, sw=2.2, fill="#fff", close=True):
    d = " ".join(("M" if i == 0 else "L") + f"{P(x, y, ox, oy)[0]:.1f},{P(x, y, ox, oy)[1]:.1f}" for i, (x, y) in enumerate(pts))
    out.append(f'<path d="{d}{" Z" if close else ""}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"/>')

def bone(a, b, w, ox, oy, stroke=INK, knob=None):
    """Long bone a->b, width w (m): slim shaft, flared rounded ends."""
    (x1, y1), (x2, y2) = a, b
    dx, dy = x2 - x1, y2 - y1
    L = math.hypot(dx, dy); ux, uy = dx / L, dy / L; nx, ny = -uy, ux
    h = w / 2
    f = lambda t: h * (0.62 + 0.48 * abs(2 * t - 1) ** 2.5)
    left, right = [], []
    for i in range(13):
        t = i / 12; cx, cy = x1 + dx * t, y1 + dy * t
        left.append((cx + nx * f(t), cy + ny * f(t))); right.append((cx - nx * f(t), cy - ny * f(t)))
    capB = [(x2 + (nx * math.cos(math.radians(a)) + ux * math.sin(math.radians(a))) * f(1), y2 + (ny * math.cos(math.radians(a)) + uy * math.sin(math.radians(a))) * f(1)) for a in range(0, 181, 20)]
    capA = [(x1 + (-nx * math.cos(math.radians(a)) - ux * math.sin(math.radians(a))) * f(0), y1 + (-ny * math.cos(math.radians(a)) - uy * math.sin(math.radians(a))) * f(0)) for a in range(0, 181, 20)]
    poly(left + capB + right[::-1] + capA, ox, oy, stroke=stroke)

def rib(ctrl, w0, w1, ox, oy, stroke=INK, sw=1.8):
    """Curved, tapering rib: wide head at the spine (w0), thin tip (w1), with a small head knob."""
    c = curve_pts(ctrl, 8)
    left, right = [], []
    n = len(c)
    for i, (x, y) in enumerate(c):
        a = c[max(i - 1, 0)]; b = c[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L, dx / L
        t = i / (n - 1)
        # slight bulge near the head, steady taper toward the tip
        w = (w0 * (1 - t) + w1 * t) * (1.0 + 0.35 * max(0, 1 - t / 0.15)) / 2
        left.append((x + nx * w, y + ny * w)); right.append((x - nx * w, y - ny * w))
    poly(left + right[::-1], ox, oy, stroke=stroke, sw=sw)

def hoof(base, ang, L, w, ox, oy, stroke=INK):
    """Blunt hoof-like claw pointing along ang (deg)."""
    a = math.radians(ang); ux, uy = math.cos(a), math.sin(a); nx, ny = -uy, ux
    bx, by = base
    pts = [(bx + nx * w / 2, by + ny * w / 2),
           (bx + ux * L * 0.75 + nx * w * 0.42, by + uy * L * 0.75 + ny * w * 0.42),
           (bx + ux * L, by + uy * L),
           (bx + ux * L * 0.8 - nx * w * 0.5, by + uy * L * 0.8 - ny * w * 0.5),
           (bx - nx * w / 2, by - ny * w / 2)]
    poly(pts, ox, oy, stroke=stroke, sw=1.5)

def toe(start, ang, segs, w, ox, oy, stroke=INK, droop=-6):
    """Chain of toe bones (phalanges) ending in a hoof."""
    x, y = start; a = ang
    for i, L in enumerate(segs):
        ex, ey = x + math.cos(math.radians(a)) * L, y + math.sin(math.radians(a)) * L
        bone((x, y), (ex, ey), w * (1 - 0.12 * i), ox, oy, stroke)
        x, y = ex, ey; a += droop
    hoof((x, y), a, w * 1.5, w * 1.05, ox, oy, stroke)

def side_foot(ankle, toes, ox, oy, stroke=INK, front=False):
    """Side view foot: ankle/wrist bones, long foot bones, toes. toes = list of (metatarsal end, toe angle, segs)."""
    ax, ay = ankle
    for (mx, my), ang, segs, w in toes:
        bone((ax + 0.02, ay - 0.04), (mx, my), w * 1.15, ox, oy, stroke)
    for (mx, my), ang, segs, w in toes:
        toe((mx, my), ang, segs, w, ox, oy, stroke)
    ellipse(ax, ay, 0.085, 0.06, ox, oy, rot=-20, stroke=stroke, sw=1.8)
    if not front:
        ellipse(ax + 0.03, ay - 0.06, 0.06, 0.04, ox, oy, stroke=stroke, sw=1.6)

def bumps(pts, every, r, ox, oy, keep=lambda p: True):
    for p in pts[::every]:
        if keep(p):
            ellipse(p[0], p[1], r, r, ox, oy, sw=1.5)

def ellipse(cx, cy, rx, ry, ox, oy, rot=0, stroke=INK, sw=2.2, fill="#fff"):
    X, Y = P(cx, cy, ox, oy)
    out.append(f'<ellipse cx="{X:.1f}" cy="{Y:.1f}" rx="{rx*S:.1f}" ry="{ry*S:.1f}" transform="rotate({-rot} {X:.1f} {Y:.1f})" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

def curve_pts(ctrl, n):
    """Catmull-Rom through control points, n samples."""
    pts = []
    c = [ctrl[0]] + ctrl + [ctrl[-1]]
    for i in range(1, len(c) - 2):
        p0, p1, p2, p3 = c[i - 1], c[i], c[i + 1], c[i + 2]
        for j in range(n):
            t = j / n
            t2, t3 = t * t, t * t * t
            pts.append(tuple(0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in range(2)))
    pts.append(ctrl[-1])
    return pts

def text(x, y, s, size=20, weight="600", anchor="start", fill=INK):
    out.append(f'<text x="{x}" y="{y}" font-family="Helvetica, Arial, sans-serif" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}" fill="{fill}">{s}</text>')

# ---------------- SIDE VIEW ----------------
def side(ox, oy):
    out.append(f'<line x1="{ox-20}" y1="{oy}" x2="{ox+9.4*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    # far-side limbs (grey)
    bone((3.25, 1.75), (3.75, 0.98), 0.24, ox, oy, FAR)
    bone((3.75, 0.98), (3.55, 0.36), 0.18, ox, oy, FAR)
    side_foot((3.55, 0.39), [((3.80, 0.12), -6, [0.12, 0.09], 0.08), ((3.88, 0.11), -8, [0.14, 0.10], 0.085)], ox, oy, FAR)
    bone((5.95, 1.30), (5.55, 0.78), 0.20, ox, oy, FAR)
    bone((5.55, 0.78), (5.75, 0.25), 0.15, ox, oy, FAR)
    side_foot((5.77, 0.25), [((5.95, 0.09), -8, [0.08, 0.06], 0.06), ((6.02, 0.085), -8, [0.09, 0.06], 0.065)], ox, oy, FAR, front=True)

    # spine line: tail tip -> hips -> back -> neck
    spine = curve_pts([(0.0, 0.95), (1.0, 1.55), (2.2, 2.05), (3.3, 2.25), (4.4, 2.35), (5.4, 2.15), (6.1, 1.85), (6.55, 1.70)], 8)
    # vertebrae as small blocks + neural spines
    for i in range(0, len(spine) - 1, 1):
        (x1, y1), (x2, y2) = spine[i], spine[i + 1]
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        tail = mx < 3.0
        hgt = 0.06 + 0.09 * min(1, mx / 3.0)
        ellipse(mx, my, 0.045 + 0.035 * min(1, mx / 3.0), hgt / 1.6, ox, oy, rot=math.degrees(math.atan2(y2 - y1, x2 - x1)))
        # neural spine
        sp = 0.10 + (0.25 if 3.0 < mx < 5.6 else (0.18 * mx / 3.0 if tail else 0.08))
        poly([(mx - 0.03, my + 0.05), (mx + 0.03, my + 0.05), (mx + 0.01, my + 0.05 + sp), (mx - 0.04, my + 0.05 + sp)], ox, oy, sw=1.6)
        # chevrons under tail
        if tail and mx > 0.4:
            ch = 0.10 + 0.22 * (mx / 3.0)
            poly([(mx - 0.02, my - 0.06), (mx + 0.02, my - 0.06), (mx - 0.03, my - 0.06 - ch), (mx - 0.06, my - 0.06 - ch)], ox, oy, sw=1.4)

    # ribs (behind pelvis/scapula)
    for i, rx in enumerate([3.9, 4.15, 4.4, 4.65, 4.9, 5.15, 5.4, 5.65]):
        top = 2.25 - 0.08 * abs(rx - 4.6)
        depth = 1.25 - 0.10 * abs(rx - 4.9) * 2
        rib([(rx, top), (rx + 0.14, top - depth * 0.45), (rx + 0.05, top - depth)], 0.10, 0.03, ox, oy)

    # pelvis: ilium, pubis, ischium
    poly(curve_pts([(2.55, 2.30), (3.2, 2.55), (4.1, 2.55), (4.55, 2.35), (4.1, 2.18), (3.5, 2.05), (3.2, 1.95), (2.85, 2.05), (2.55, 2.30)], 5), ox, oy)
    ellipse(3.28, 1.78, 0.13, 0.13, ox, oy, sw=2.0)  # acetabulum
    poly([(3.45, 1.85), (3.95, 1.70), (4.35, 1.45), (4.45, 1.30), (4.25, 1.35), (3.85, 1.55), (3.40, 1.68)], ox, oy)       # pubis
    poly(curve_pts([(3.10, 1.80), (2.75, 1.50), (2.35, 1.20), (2.20, 1.15), (2.30, 1.30), (2.70, 1.62), (3.05, 1.92)], 5), ox, oy)  # ischium

    # scapula + coracoid
    poly(curve_pts([(5.95, 1.40), (5.70, 1.80), (5.35, 2.15), (5.20, 2.20), (5.30, 2.05), (5.60, 1.65), (5.80, 1.30)], 5), ox, oy)
    ellipse(6.05, 1.30, 0.13, 0.16, ox, oy)

    # near hind limb
    bone((3.25, 1.78), (3.70, 0.96), 0.27, ox, oy)       # femur
    bone((3.70, 0.96), (3.48, 0.38), 0.20, ox, oy)       # tibia
    side_foot((3.47, 0.40), [((3.58, 0.22), -28, [0.10, 0.08], 0.065), ((3.64, 0.18), -20, [0.12, 0.09], 0.07), ((3.72, 0.15), -14, [0.15, 0.10], 0.08), ((3.82, 0.10), -6, [0.16, 0.11, 0.07], 0.09)], ox, oy)

    # near fore limb
    bone((6.02, 1.28), (5.62, 0.76), 0.23, ox, oy)       # humerus
    bone((5.62, 0.76), (5.82, 0.24), 0.17, ox, oy)       # radius/ulna
    side_foot((5.82, 0.25), [((5.86, 0.15), -30, [0.06], 0.05), ((5.93, 0.13), -20, [0.09, 0.06], 0.06), ((6.01, 0.105), -12, [0.10, 0.07], 0.068), ((6.09, 0.085), -6, [0.10, 0.07], 0.068)], ox, oy, front=True)  # hand

    hk = len(out)
    # skull: frill, face, beak
    skull = curve_pts([(6.05, 1.55), (5.95, 2.30), (6.05, 2.95), (6.35, 3.15), (6.60, 2.95), (6.80, 2.45),
                       (7.10, 2.05), (7.55, 1.75), (8.05, 1.45), (8.45, 1.15), (8.62, 0.90), (8.40, 0.78),
                       (8.05, 0.88), (7.60, 0.85), (7.10, 0.95), (6.75, 1.20), (6.45, 1.40), (6.05, 1.55)], 5)
    poly(skull, ox, oy, sw=2.6)
    bumps(skull[2:28], 3, 0.055, ox, oy)
    # frill opening (fenestra)
    ellipse(6.35, 2.45, 0.18, 0.30, ox, oy, rot=-15, sw=1.8)
    # eye, nostril, jaw opening
    ellipse(7.12, 1.55, 0.13, 0.11, ox, oy, sw=2.0)
    ellipse(8.05, 1.12, 0.12, 0.08, ox, oy, rot=-30, sw=1.8)
    ellipse(7.35, 1.18, 0.20, 0.10, ox, oy, rot=-10, sw=1.6)
    # lower jaw line
    poly(curve_pts([(6.80, 1.18), (7.4, 0.98), (8.0, 0.92), (8.40, 0.80)], 6), ox, oy, sw=1.8, fill="none", close=False)
    # horns: brow (two, one far) and nose
    poly([(7.05, 1.70), (7.25, 1.85), (8.20, 2.55), (8.30, 2.62), (8.18, 2.48), (7.32, 1.68)], ox, oy, stroke=FAR)
    poly([(7.00, 1.72), (7.22, 1.92), (8.15, 2.48), (8.26, 2.56), (8.13, 2.40), (7.28, 1.70)], ox, oy, sw=2.4)
    poly([(7.95, 1.42), (8.10, 1.62), (8.20, 1.70), (8.17, 1.55), (8.12, 1.38)], ox, oy, sw=2.0)

    squash(hk, 6.55, ox, oy, SNOUT)
    shift(hk, HEAD_DX, ox, oy)
    # labels + scale
    text(ox, oy - 3.45 * S, "SIDE VIEW", 22)

# ---------------- TOP VIEW ----------------
def top(ox, oy):
    # y here = lateral offset from midline (m); oy is the midline
    # ribs splaying wide
    for rx in [3.9, 4.15, 4.4, 4.65, 4.9, 5.15, 5.4, 5.65]:
        span = 0.85 + 0.25 * (1 - abs(rx - 4.8) / 1.0)
        for s in (1, -1):
            rib([(rx, 0.06 * s), (rx + 0.06, 0.6 * span * s), (rx + 0.16, span * s)], 0.09, 0.03, ox, oy, sw=1.6)
    spine = [(x, 0.0) for x in [i * 0.15 for i in range(0, 45)]]
    for i, (x, _) in enumerate(spine):
        w = 0.06 + (0.06 if x > 3.0 else 0.04 * x / 3.0)
        ellipse(x, 0, 0.07, w, ox, oy, sw=1.6)
        if x < 3.0 and x > 0.3:  # transverse processes on tail base
            if x > 2.0:
                poly([(x - 0.03, w), (x + 0.03, w), (x + 0.01, w + 0.14), (x - 0.02, w + 0.14)], ox, oy, sw=1.2)
                poly([(x - 0.03, -w), (x + 0.03, -w), (x + 0.01, -w - 0.14), (x - 0.02, -w - 0.14)], ox, oy, sw=1.2)
    # ilium plates
    for s in (1, -1):
        poly(curve_pts([(2.55, 0.15 * s), (3.0, 0.75 * s), (3.6, 1.05 * s), (4.3, 0.80 * s), (4.55, 0.30 * s), (4.0, 0.25 * s), (3.0, 0.15 * s)], 5), ox, oy)
    # limbs seen from above: knees/elbows and feet just outside the body
    for s in (1, -1):
        ellipse(3.62, 1.12 * s, 0.16, 0.13, ox, oy)
        for k, d in enumerate((-0.12, 0.0, 0.12)):
            toe((3.72, (1.12 + d) * s), (-14 + k * 14) * s, [0.16, 0.11, 0.08], 0.085, ox, oy, droop=0)
        ellipse(5.65, 1.12 * s, 0.14, 0.12, ox, oy)
        for k, d in enumerate((-0.13, -0.045, 0.045, 0.13)):
            toe((5.75, (1.12 + d) * s), (-20 + k * 13) * s, [0.13, 0.08], 0.07, ox, oy, droop=0)
    # scapulae
    for s in (1, -1):
        poly(curve_pts([(5.35, 0.30 * s), (5.65, 0.70 * s), (5.95, 0.92 * s), (6.02, 0.80 * s), (5.70, 0.48 * s), (5.35, 0.30 * s)], 5), ox, oy, sw=1.8)
    hk = len(out)
    # skull from above: frill fan + snout
    sk = curve_pts([(6.0, 0.0), (6.05, 0.70), (6.35, 1.05), (6.65, 0.85), (7.0, 0.42), (7.6, 0.32), (8.2, 0.18), (8.6, 0.0),
                    (8.2, -0.18), (7.6, -0.32), (7.0, -0.42), (6.65, -0.85), (6.35, -1.05), (6.05, -0.70), (6.0, 0.0)], 6)
    poly(sk, ox, oy, sw=2.6)
    for s in (1, -1):
        ellipse(6.45, 0.48 * s, 0.25, 0.16, ox, oy, sw=1.8)
        poly([(7.05, 0.32 * s), (7.25, 0.38 * s), (8.25, 0.55 * s), (8.30, 0.50 * s), (7.25, 0.24 * s)], ox, oy, sw=2.2)
        ellipse(7.1, 0.36 * s, 0.10, 0.06, ox, oy, sw=1.6)
    poly([(7.95, 0.06), (8.20, 0.0), (7.95, -0.06)], ox, oy, sw=1.8)
    squash(hk, 6.55, ox, oy, SNOUT)
    shift(hk, HEAD_DX, ox, oy)
    text(ox, oy - 1.6 * S, "TOP VIEW", 22)

# ---------------- FRONT VIEW ----------------
def front(ox, oy):
    # x = lateral offset (m), y = height; ox is the midline
    out.append(f'<line x1="{ox-1.9*S}" y1="{oy}" x2="{ox+1.9*S}" y2="{oy}" stroke="#bbb" stroke-width="1.5"/>')
    for s in (1, -1):
        # hind legs behind (grey)
        bone((0.85 * s, 1.75), (1.05 * s, 0.95), 0.27, ox, oy, FAR)
        bone((1.05 * s, 0.95), (1.0 * s, 0.38), 0.20, ox, oy, FAR)
        for d in (-0.11, 0.0, 0.11):
            bone(((1.0 + d * 0.5) * s, 0.36), ((1.0 + d * 1.5) * s, 0.13), 0.075, ox, oy, FAR)
            hoof(((1.0 + d * 1.5) * s, 0.13), -90, 0.12, 0.10, ox, oy, FAR)
    # rib cage cross-section (barrel)
    poly(curve_pts([(0.0, 2.3), (0.7, 2.15), (1.05, 1.7), (1.05, 1.25), (0.75, 1.0), (0.0, 0.95), (-0.75, 1.0), (-1.05, 1.25), (-1.05, 1.7), (-0.7, 2.15), (0.0, 2.3)], 6), ox, oy, stroke=FAR, sw=1.8)
    for s in (1, -1):
        # front legs, slightly sprawled at the elbow
        bone((0.85 * s, 1.30), (1.12 * s, 0.78), 0.23, ox, oy)
        bone((1.12 * s, 0.78), (0.98 * s, 0.25), 0.17, ox, oy)
        ellipse(0.98 * s, 0.25, 0.10, 0.05, ox, oy, sw=1.8)
        for d in (-0.15, -0.05, 0.05, 0.15):
            bone(((0.98 + d * 0.4) * s, 0.23), ((0.98 + d * 1.25) * s, 0.11), 0.065, ox, oy)
            hoof(((0.98 + d * 1.25) * s, 0.11), -90, 0.10, 0.08, ox, oy)
    # head on, frill as wide fan
    frill = curve_pts([(0.0, 3.10), (0.65, 2.95), (1.05, 2.45), (1.05, 1.95), (0.70, 1.62), (0.0, 1.55), (-0.70, 1.62), (-1.05, 1.95), (-1.05, 2.45), (-0.65, 2.95), (0.0, 3.10)], 6)
    poly(frill, ox, oy, sw=2.4)
    bumps(frill, 3, 0.05, ox, oy, keep=lambda p: p[1] > 1.9)
    for s in (1, -1):
        ellipse(0.48 * s, 2.45, 0.20, 0.28, ox, oy, sw=1.6)
    face = curve_pts([(0.0, 1.95), (0.38, 1.85), (0.42, 1.40), (0.30, 1.05), (0.12, 0.82), (0.0, 0.78), (-0.12, 0.82), (-0.30, 1.05), (-0.42, 1.40), (-0.38, 1.85), (0.0, 1.95)], 6)
    poly(face, ox, oy, sw=2.4)
    for s in (1, -1):
        ellipse(0.30 * s, 1.58, 0.07, 0.09, ox, oy, sw=1.8)
        poly([(0.22 * s, 1.72), (0.34 * s, 1.75), (0.55 * s, 2.55), (0.50 * s, 2.58)], ox, oy, sw=2.2)
    poly([(-0.05, 1.22), (0.05, 1.22), (0.02, 1.48), (-0.02, 1.48)], ox, oy, sw=1.8)
    text(ox - 1.9 * S, oy - 3.45 * S, "FRONT VIEW", 22)


def foot_top(cx, cy, digits, label_dy):
    """Foot seen from above, toes pointing up the page. digits: (x_off_m, metapodial_len, [phalanx lens], width, hoofed, label)."""
    # wrist/ankle bones as a row of small blocks
    for d in digits:
        ellipse(d[0], 0.0, d[3] * 0.85, 0.05, cx, cy, sw=1.6)
    for x0, ml, phal, w, hoofed, lab in digits:
        splay = x0 * 0.9
        x1, y1 = x0 * 1.25, ml
        bone((x0, 0.03), (x1, y1), w, cx, cy)
        x, y = x1, y1
        ang = 90 - math.degrees(splay)
        for i, L in enumerate(phal):
            ex, ey = x + math.cos(math.radians(ang)) * L, y + math.sin(math.radians(ang)) * L
            bone((x, y), (ex, ey), w * (0.95 - 0.1 * i), cx, cy)
            x, y = ex, ey
        if hoofed:
            hoof((x, y), ang, w * 1.6, w * 1.35, cx, cy)
        X, Y = P(x, y, cx, cy)
        text(int(X), int(Y - (w * 1.6 + 0.04) * S - 4), lab, 13, "600", "middle", "#555")

def feet_detail():
    global S
    keep = S; S = 300
    text(1000, 500, "FOOT DETAIL (from above, 3x scale)", 22)
    # hind foot: 4 toes, I-III with hooves, IV small
    foot_top(1120, 790, [(-0.13, 0.20, [0.07, 0.06], 0.065, True, "I"), (-0.045, 0.26, [0.08, 0.06, 0.05], 0.075, True, "II"),
                         (0.045, 0.27, [0.08, 0.06, 0.05, 0.04], 0.075, True, "III"), (0.13, 0.20, [0.06, 0.05], 0.058, True, "IV")], 0)
    # hand: 5 fingers, I-III with hooves, IV-V small
    foot_top(1360, 790, [(-0.14, 0.15, [0.06, 0.05], 0.06, True, "I"), (-0.07, 0.18, [0.06, 0.05, 0.04], 0.062, True, "II"),
                         (0.0, 0.19, [0.06, 0.05, 0.04], 0.062, True, "III"), (0.07, 0.16, [0.04, 0.03], 0.045, False, "IV"),
                         (0.13, 0.12, [0.03], 0.04, False, "V")], 0)
    S = keep
    text(1120, 830, "Hind foot", 15, "600", "middle", "#333")
    text(1360, 830, "Hand", 15, "600", "middle", "#333")

W, H = 1500, 880
side(70, 420)
front(1220, 420)
top(70, 680)
feet_detail()

# scale bar
sx, sy = 70, 830
for i in range(4):
    out.append(f'<rect x="{sx+i*S}" y="{sy}" width="{S}" height="10" fill="{INK if i%2==0 else "#fff"}" stroke="{INK}" stroke-width="1.5"/>')
text(sx, sy - 8, "0", 15, "400"); text(sx + 4 * S, sy - 8, "4 m", 15, "400", "middle")
text(W - 40, 50, "TRICERATOPS HORRIDUS", 30, "700", "end")
text(W - 40, 78, "Skeletal reference · about 8 m long · built from Reference Library picks", 16, "400", "end", "#555")
text(W - 40, 450, "Grey = far-side bones", 15, "400", "end", "#777")

svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="#fff"/>' + "".join(out) + "</svg>"
open("trike.svg", "w").write(svg)
print("ok")
