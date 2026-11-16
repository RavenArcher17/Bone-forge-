"""Helpers for life drawings: a faint skeleton ghost inside a clean body outline with skin texture."""
import random, re

GHOST, GHOST_FAR = "#b9b9b9", "#dadada"

def ghost(start):
    """Recolour everything drawn since `start` into a faint skeleton; drop its text and ground lines."""
    items = []
    for e in out[start:]:
        if e.startswith("<text") or e.startswith("<line"):
            continue
        e = e.replace(f'stroke="{INK}"', f'stroke="{GHOST}"').replace(f'stroke="{FAR}"', f'stroke="{GHOST_FAR}"').replace('stroke="#888"', f'stroke="{GHOST_FAR}"').replace('stroke="#666"', f'stroke="{GHOST_FAR}"').replace('stroke="#6b6b6b"', f'stroke="{GHOST}"')
        e = re.sub(r'stroke-width="([\d.]+)"', lambda m: f'stroke-width="{float(m.group(1)) * 0.6:.2f}"', e)
        e = e.replace('fill="#fff"', 'fill="none"')
        items.append(e)
    del out[start:]
    out.extend(items)

def path_d(pts, ox, oy, close=True):
    return " ".join(("M" if i == 0 else "L") + f"{P(x, y, ox, oy)[0]:.1f},{P(x, y, ox, oy)[1]:.1f}" for i, (x, y) in enumerate(pts)) + (" Z" if close else "")

def fill_shape(pts, ox, oy, fill="#fff"):
    out.append(f'<path d="{path_d(pts, ox, oy)}" fill="{fill}" stroke="none"/>')

def outline(pts, ox, oy, stroke=INK, sw=2.8, close=True):
    out.append(f'<path d="{path_d(pts, ox, oy, close)}" fill="none" stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"/>')

def inside(pt, poly_pts):
    x, y = pt; c = False; n = len(poly_pts)
    for i in range(n):
        (x1, y1), (x2, y2) = poly_pts[i], poly_pts[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1:
            c = not c
    return c

def scales(shapes, ox, oy, step, r, stroke="#d2d2d2", seed=1, exclude=(), big=None):
    """Scatter small round scales inside any of `shapes`, skipping `exclude` shapes. big(x,y) -> size multiplier."""
    rnd = random.Random(seed)
    xs = [p[0] for s in shapes for p in s]; ys = [p[1] for s in shapes for p in s]
    y = min(ys)
    row = 0
    while y < max(ys):
        x = min(xs) + (step / 2 if row % 2 else 0)
        while x < max(xs):
            px, py = x + rnd.uniform(-0.2, 0.2) * step, y + rnd.uniform(-0.2, 0.2) * step
            if any(inside((px, py), s) for s in shapes) and not any(inside((px, py), s) for s in exclude):
                k = big(px, py) if big else 1.0
                X, Y = P(px, py, ox, oy)
                out.append(f'<ellipse cx="{X:.1f}" cy="{Y:.1f}" rx="{r * k * S:.1f}" ry="{r * k * S * 0.8:.1f}" fill="none" stroke="{stroke}" stroke-width="1"/>')
            x += step
        y += step * 0.85; row += 1

def feathers(line_pts, ox, oy, length, ang, every=1, stroke="#7a7a7a", sw=1.2):
    """Short feather strokes along a line of points, angled `ang` degrees from horizontal."""
    for (x, y) in line_pts[::every]:
        X1, Y1 = P(x, y, ox, oy)
        X2, Y2 = P(x + math.cos(math.radians(ang)) * length, y + math.sin(math.radians(ang)) * length, ox, oy)
        out.append(f'<line x1="{X1:.1f}" y1="{Y1:.1f}" x2="{X2:.1f}" y2="{Y2:.1f}" stroke="{stroke}" stroke-width="{sw}" stroke-linecap="round"/>')

def fold(pts, ox, oy, sw=1.3, stroke="#555"):
    outline(pts, ox, oy, stroke=stroke, sw=sw, close=False)

def leg_outline(pts, ox, oy, stroke=INK, sw=2.8):
    """Outline a leg shape but leave its top edge open so it blends into the body."""
    outline(pts[1:-1], ox, oy, stroke=stroke, sw=sw, close=False)

def visible(pts, occluders, ox, oy, stroke=INK, sw=2.8, closed=True, step=0.01):
    """Draw only the parts of an outline not covered by any occluder shape (hidden-line removal)."""
    line = list(pts) + ([pts[0]] if closed else [])
    dense = resample(line, step) + [line[-1]]
    runs, cur = [], []
    for p in dense:
        if any(inside(p, o) for o in occluders):
            if len(cur) > 1:
                runs.append(cur)
            cur = []
        else:
            cur.append(p)
    if len(cur) > 1:
        runs.append(cur)
    for r in runs:
        outline(r, ox, oy, stroke=stroke, sw=sw, close=False)

def open_edge(pts):
    """A part's outline without its top edge (where it joins the body)."""
    return pts[1:-1]

def mirror(half):
    """Symmetric outline from its right half, listed top to bottom with x >= 0."""
    return half + [(-x, y) for x, y in reversed(half)]

def render_parts(parts, OX, OY, ghost_draw, tex=None):
    """parts: back-to-front list of (pts, mode, stroke, sw); mode 'closed' or 'open' (open = no top edge)."""
    for p in parts:
        fill_shape(p[0], OX, OY)
    g0 = len(out); ghost_draw(OX, OY); ghost(g0)
    if tex:
        tex()
    for i, (pts, mode, stroke, sw) in enumerate(parts):
        occ = [q[0] for q in parts[i + 1:]]
        if mode == "open":
            visible(open_edge(pts), occ, OX, OY, stroke=stroke, sw=sw, closed=False)
        else:
            visible(pts, occ, OX, OY, stroke=stroke, sw=sw, closed=True)

def feather_tex(shapes, OX, OY, step, length, ang, stroke="#cfcfcf", seed=2, exclude=()):
    """Scatter short feather strokes inside shapes, angled `ang` degrees."""
    rnd = random.Random(seed)
    xs = [p[0] for s in shapes for p in s]; ys = [p[1] for s in shapes for p in s]
    y = min(ys); row = 0
    while y < max(ys):
        x = min(xs) + (step / 2 if row % 2 else 0)
        while x < max(xs):
            px, py = x + rnd.uniform(-0.25, 0.25) * step, y + rnd.uniform(-0.25, 0.25) * step
            if any(inside((px, py), s) for s in shapes) and not any(inside((px, py), s) for s in exclude):
                a = math.radians(ang + rnd.uniform(-12, 12))
                X1, Y1 = P(px, py, OX, OY); X2, Y2 = P(px + math.cos(a) * length, py + math.sin(a) * length, OX, OY)
                out.append(f'<line x1="{X1:.1f}" y1="{Y1:.1f}" x2="{X2:.1f}" y2="{Y2:.1f}" stroke="{stroke}" stroke-width="1.1" stroke-linecap="round"/>')
            x += step
        y += step * 0.8; row += 1

def ground(OX, OY, x0, x1):
    out.append(f'<line x1="{OX + x0 * S:.1f}" y1="{OY}" x2="{OX + x1 * S:.1f}" y2="{OY}" stroke="#bbb" stroke-width="1.5"/>')

def load_skeleton(script):
    """Load a skeleton script's drawing functions without running its sheet."""
    src = open(script).read()
    return src[:src.index("\nW, H =")]
