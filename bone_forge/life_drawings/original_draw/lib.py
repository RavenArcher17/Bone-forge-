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
        if hoofed == "claw":
            claw((x, y), ang, w * 1.7, w * 0.9, cx, cy, curl=0)
        elif hoofed:
            hoof((x, y), ang, w * 1.6, w * 1.35, cx, cy)
        X, Y = P(x, y, cx, cy)
        text(int(X), int(Y - (w * 1.6 + 0.04) * S - 4), lab, 13, "600", "middle", "#555")


def claw(base, ang, L, w, ox, oy, stroke=INK, curl=35):
    """Sharp curved claw from base along ang (deg), curling downward by curl degrees."""
    bx, by = base
    top, bot = [], []
    for i in range(9):
        t = i / 8
        a = math.radians(ang - curl * t)
        # centre line arc
        cx = bx + sum(math.cos(math.radians(ang - curl * (j / 8))) * L / 8 for j in range(i))
        cy = by + sum(math.sin(math.radians(ang - curl * (j / 8))) * L / 8 for j in range(i))
        hw = w / 2 * (1 - t) ** 0.9
        nx, ny = -math.sin(a), math.cos(a)
        top.append((cx + nx * hw, cy + ny * hw)); bot.append((cx - nx * hw, cy - ny * hw))
    poly(top + bot[::-1], ox, oy, stroke=stroke, sw=1.6)

def digit(start, ang, segs, w, ox, oy, stroke=INK, droop=-6, end="claw", clawL=None, curl=35):
    x, y = start; a = ang
    for i, L in enumerate(segs):
        ex, ey = x + math.cos(math.radians(a)) * L, y + math.sin(math.radians(a)) * L
        bone((x, y), (ex, ey), w * (1 - 0.1 * i), ox, oy, stroke)
        x, y = ex, ey; a += droop
    if end == "claw":
        claw((x, y), a, clawL or w * 2.2, w * 1.0, ox, oy, stroke, curl)
    elif end == "hoof":
        hoof((x, y), a, w * 1.5, w * 1.05, ox, oy, stroke)

def sheet(W, H, title, sub):
    text(W - 40, 50, title, 30, "700", "end")
    text(W - 40, 78, sub, 16, "400", "end", "#555")

def scalebar(sx, sy, metres, step=1):
    n = int(metres / step)
    for i in range(n):
        out.append(f'<rect x="{sx+i*step*S}" y="{sy}" width="{step*S}" height="10" fill="{INK if i%2==0 else "#fff"}" stroke="{INK}" stroke-width="1.5"/>')
    text(sx, sy - 8, "0", 15, "400"); text(sx + metres * S, sy - 8, f"{metres} m", 15, "400", "middle")

def save(name, W, H):
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="#fff"/>' + "".join(out) + "</svg>"
    open(name, "w").write(svg)

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

def vert_chain(spine, ox, oy, spine_h, chev, k=1.0, tail=5.5):
    """Vertebrae along a curve: centrum ovals, neural spines on top, chevrons under the tail. k scales bone sizes."""
    for i in range(len(spine) - 1):
        (x1, y1), (x2, y2) = spine[i], spine[i + 1]
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        rot = math.degrees(math.atan2(y2 - y1, x2 - x1))
        sh = spine_h(mx)
        if sh > 0:
            poly([(mx - 0.035 * k, my + 0.06 * k), (mx + 0.035 * k, my + 0.06 * k), (mx + 0.015 * k, my + 0.06 * k + sh), (mx - 0.05 * k, my + 0.06 * k + sh)], ox, oy, sw=1.5)
        ch = chev(mx)
        if ch > 0:
            poly([(mx - 0.025 * k, my - 0.06 * k), (mx + 0.025 * k, my - 0.06 * k), (mx - 0.04 * k, my - 0.06 * k - ch), (mx - 0.075 * k, my - 0.06 * k - ch)], ox, oy, sw=1.4)
        size = (0.05 + 0.06 * min(1, mx / tail)) * k
        ellipse(mx, my, size * 1.1, size * 0.85, ox, oy, rot=rot, sw=1.7)

def scaled(start, ax, ay, ox, oy, k):
    """Wrap everything drawn since index `start` in a scale around metre point (ax, ay), keeping line widths."""
    X, Y = P(ax, ay, ox, oy)
    items = [e.replace("<path ", '<path vector-effect="non-scaling-stroke" ').replace("<ellipse ", '<ellipse vector-effect="non-scaling-stroke" ') for e in out[start:]]
    del out[start:]
    out.append(f'<g transform="translate({X:.1f},{Y:.1f}) scale({k}) translate({-X:.1f},{-Y:.1f})">' + "".join(items) + "</g>")


def squash(start, x0, ox, oy, kx):
    """Shorten everything drawn since `start` along x, keeping x0 (metres) fixed and line widths unchanged."""
    X0, _ = P(x0, 0, ox, oy)
    items = [e.replace("<path ", '<path vector-effect="non-scaling-stroke" ').replace("<ellipse ", '<ellipse vector-effect="non-scaling-stroke" ') for e in out[start:]]
    del out[start:]
    out.append(f'<g transform="translate({X0:.1f},0) scale({kx},1) translate({-X0:.1f},0)">' + "".join(items) + "</g>")

SNOUT = 0.68   # Triceratops face length factor (frill stays put)

HEAD_DX = -0.125   # Triceratops head moved back toward the shoulders (m)

def shift(start, dx, ox, oy):
    """Move everything drawn since `start` along x by dx metres."""
    items = out[start:]; del out[start:]
    out.append(f'<g transform="translate({dx * S:.1f},0)">' + "".join(items) + "</g>")
