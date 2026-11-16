"""Shared helpers for the mouth / tooth / hand / foot close-up sheets."""
C = lambda pts, n=6: curve_pts(pts, n)
MOUTH = "#eeeeee"

def rot(pts, cx, cy, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in pts]

def banana(base, L, w, ang, curve, OX, OY, sw=1.4):
    """Curved tooth from `base`, length L, base width w, pointing `ang` degrees, curving by `curve` degrees."""
    bx, by = base; top, bot = [], []
    x, y, a = bx, by, ang
    for i in range(9):
        t = i / 8; hw = w / 2 * (1 - t) ** 0.8
        nx, ny = -math.sin(math.radians(a)), math.cos(math.radians(a))
        top.append((x + nx * hw, y + ny * hw)); bot.append((x - nx * hw, y - ny * hw))
        x += math.cos(math.radians(a)) * L / 8; y += math.sin(math.radians(a)) * L / 8; a += curve / 8
    poly(top + bot[::-1], OX, OY, sw=sw)

def peg(base, L, w, ang, OX, OY, sw=1.3, tip=0.5):
    """Straight blunt tooth (spoon, leaf or battery tooth): rounded/pointed tip."""
    a = math.radians(ang); ux, uy = math.cos(a), math.sin(a); nx, ny = -uy, ux
    bx, by = base
    pts = [(bx + nx * w / 2, by + ny * w / 2), (bx + ux * L * (1 - tip * 0.4) + nx * w * 0.5, by + uy * L * (1 - tip * 0.4) + ny * w * 0.5),
           (bx + ux * L, by + uy * L), (bx + ux * L * (1 - tip * 0.4) - nx * w * 0.5, by + uy * L * (1 - tip * 0.4) - ny * w * 0.5),
           (bx - nx * w / 2, by - ny * w / 2)]
    poly(C(pts, 3), OX, OY, sw=sw)

def label(x, y, s, anchor="start"):
    text(x, y, s, 13, "400", anchor, "#555")

def lead(OX, OY, x1, y1, x2, y2, s, s2=None):
    fold([(x1, y1), (x2, y2)], OX, OY, sw=1.0, stroke="#999")
    X, Y = P(x2, y2, OX, OY); label(X + 4, Y + 4, s)
    if s2:
        label(X + 4, Y + 20, s2)

def scalebar_at(OX, OY, x, y, metres, step, txt):
    n = int(round(metres / step))
    for i in range(n):
        X, Y = P(x + i * step, y, OX, OY)
        out.append(f'<rect x="{X:.1f}" y="{Y:.1f}" width="{step * S:.1f}" height="8" fill="{INK if i % 2 == 0 else "#fff"}" stroke="{INK}" stroke-width="1.2"/>')
    X, Y = P(x, y, OX, OY); label(X, Y - 6, "0"); label(X + metres * S, Y - 6, txt, "middle")

def eye(x, y, r, OX, OY):
    ellipse(x, y, r, r * 0.85, OX, OY, sw=2.0); ellipse(x + r * 0.15, y, r * 0.4, r * 0.4, OX, OY, fill=INK, sw=1)

def ghost_bones(segs, OX, OY):
    for a, b, w in segs:
        bone(a, b, w, OX, OY, GHOST)

def title(x, y, s):
    text(x, y, s, 18)

def hoof_cap(x, y, w, h, OX, OY, ang=-90):
    """Blunt hoof seen from the side or front, pointing along ang."""
    hoof((x, y), ang, h, w, OX, OY)

def footprint(OX, OY, toes, heel, pad_r, end="claw", tex_seed=70, end_len=None):
    """Foot from below. toes: list of (angle_deg, length, width, n_pads). heel: half-outline for mirror() or full outline."""
    shapes = []
    for ang, L, w, n in toes:
        a = math.radians(ang); ux, uy = math.cos(a), math.sin(a); nx, ny = -uy, ux
        pts = [(ux * t * L + nx * w * (1 - 0.3 * t), uy * t * L + ny * w * (1 - 0.3 * t)) for t in [i / 6 for i in range(7)]]
        pts += [(ux * L * 1.03, uy * L * 1.03)]
        pts += [(ux * t * L - nx * w * (1 - 0.3 * t), uy * t * L - ny * w * (1 - 0.3 * t)) for t in [i / 6 for i in range(6, -1, -1)]]
        shapes.append((C(pts, 3), ang, L, w, n))
    for sh in shapes:
        fill_shape(sh[0], OX, OY)
    fill_shape(heel, OX, OY)
    scales([heel] + [s[0] for s in shapes], OX, OY, pad_r * 1.6, pad_r * 0.3, seed=tex_seed)
    for sh, ang, L, w, n in shapes:
        visible(sh, [heel], OX, OY, sw=2.6)
        a = math.radians(ang)
        for k in range(n):
            t = 0.40 + 0.52 * k / max(1, n - 1) if n > 1 else 0.7
            ellipse(math.cos(a) * t * L, math.sin(a) * t * L, w * 0.48, w * 0.36, OX, OY, rot=ang, sw=1.3)
        tipx, tipy = math.cos(a) * L, math.sin(a) * L
        if end == "claw":
            claw((tipx, tipy), ang, end_len or w * 1.4, w * 0.8, OX, OY, curl=0)
        elif end == "hoof":
            hoof((tipx * 0.97, tipy * 0.97), ang, end_len or w * 0.9, w * 1.5, OX, OY)
    visible(heel, [], OX, OY, sw=2.6)

def sheet_frame(W, H, name, sub):
    out.insert(0, '<rect width="100%" height="100%" fill="#fff"/>')
    text(40, 50, name, 28, "700")
    text(40, 78, sub, 16, "400", "start", "#555")
    text(W - 40, H - 15, "Faint grey = bones inside · grey fill = inside of the mouth", 15, "400", "end", "#777")
