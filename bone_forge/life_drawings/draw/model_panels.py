# Close-up panels drawn from the 3D model (body_tools/leg_builder.py), in the close-up sheets' style.
# Loaded with exec() after lib.py, life_lib.py (and mouth_lib.py), like the other helpers; S is the panel's scale.
import model_limbs as ml
import numpy as np


def _width_at(pts, y):
    xs = [x0 + (x1 - x0) * (y - y0) / (y1 - y0) for (x0, y0), (x1, y1) in zip(pts, pts[1:]) if (y0 - y) * (y1 - y) <= 0 and y0 != y1]
    return (max(xs) - min(xs)) if len(xs) >= 2 else 0.0


def _fit(box, ext):
    """Scale and origin that fit metre extents (x0, x1, y0, y1) into a pixel box (left, top, right, bottom)."""
    global S
    L, T, R, B = box
    x0, x1, y0, y1 = ext
    S = min((R - L) / (x1 - x0), (B - T) / (y1 - y0))
    return L + ((R - L) - (x1 - x0) * S) / 2 - x0 * S, B + y0 * S


def model_side(key, part, OX, OY, cut, seed, claw_w=0.5, curl=6, hooves=False, toe_pads=3, sw=2.6, sickle=False, box=None):
    """Lower leg (or arm) and foot from the side: outline, grey bones, scale dots, claws or hooves, toe pads.
    box = (left, top, right, bottom) in pixels fits the panel into it and sets S. Returns the panel data
    (ml.side_panel) with the origin used in "OX", "OY", for the caller's labels."""
    fp = ml.side_panel(key, part, cut)
    leg = fp["outline"]
    if box:
        claw_pts = [(b[0] + math.cos(math.radians(a)) * L, b[1] + math.sin(math.radians(a)) * L) for b, a, L, w in fp["claws"]]
        OX, OY = _fit(box, ml.extent([leg, claw_pts or leg]))
    fp["OX"], fp["OY"] = OX, OY
    fill_shape(leg, OX, OY)
    (bx, by), (cx, cy) = fp["toe"]
    ax, ay = fp["ankle"]
    w = _width_at(leg, 0.6 * cut)
    top = cut - 0.06 * cut
    if ay < cut:
        bone((ax - 0.02 * w, top), (ax, ay), 0.22 * w, OX, OY, GHOST)                # end of the shin
        bone((ax, ay), (bx, by), 0.16 * w, OX, OY, GHOST)                            # foot bones
    else:
        bone((bx, top), (bx, by), 0.2 * w, OX, OY, GHOST)                            # column of hand bones
    if (cx, cy) != (bx, by):
        toe = [(bx + (cx - bx) * t, by + (cy - by) * t) for t in (0.0, 0.38, 0.7, 0.96)]
        for (p0, p1), k in zip(zip(toe, toe[1:]), (0.13, 0.11, 0.09)):
            bone(p0, p1, k * w, OX, OY, GHOST)
    A = ml._ref(key, part)[0]
    hv = ml.hoof_shapes(key, part, lambda V: np.c_[(V[:, 0] - A[0]) / 100, V[:, 2] / 100], lambda V: V[:, 1]) if hooves else []
    scales([leg], OX, OY, 15 / S, 3 / S, seed=seed, exclude=hv)                    # dots about 15 px apart
    visible(open_edge(leg), [], OX, OY, sw=sw, closed=False)
    for base, ang, L, wd in fp["claws"]:
        if sickle and base[1] > 0.3 * ay:      # the raptor's sickle claw, held up: drawn hooked
            claw(base, ang + 50, L * 1.2, wd * claw_w, OX, OY, curl=150)
        else:
            claw(base, ang, L, wd * claw_w, OX, OY, curl=curl)
    for r in ml.hoof_lines(hv, leg, 1.5 / S):      # where each hoof meets the skin; the outline draws the rest
        outline(r, OX, OY, sw=1.8, close=False)
    if toe_pads and (cx, cy) != (bx, by):
        for k in range(toe_pads):
            x = bx + (cx - bx) * (0.3 + 0.5 * k / max(1, toe_pads - 1))
            d = 0.06 * abs(cx - bx)
            poly(C([(x - d, 5 / S), (x, -2.5 / S), (x + d, 5 / S)], 4), OX, OY, sw=1.4, fill="none", close=False)
    return fp


def model_below(key, part, OX, OY, seed, claw_w=0.5, hoofed=(), pads=(3,), sw=2.6, box=None):
    """Foot (or hand) from below, toes up the page: heel pad over the toes, toe pads, claws or hooves.
    hoofed: indexes of toes that end in hooves; pads: pads per toe (last value repeats). box as model_side."""
    fb = ml.below_panel(key, part)
    heel, toes = fb["heel"], fb["toes"]
    if box:
        claw_pts = [(b[0] + math.cos(math.radians(a)) * L, b[1] + math.sin(math.radians(a)) * L) for b, a, L, w in fb["claws"]]
        OX, OY = _fit(box, ml.extent([heel] + [t[0] for t in toes] + [claw_pts or heel]))
    fb["OX"], fb["OY"] = OX, OY
    for t_, ang, L in toes:
        fill_shape(t_, OX, OY)
    fill_shape(heel, OX, OY)
    Bj = ml._ref(key, part)[1]
    proj = lambda V: np.c_[(V[:, 1] - Bj[1]) / 100, (V[:, 0] - Bj[0]) / 100]
    hv = [(i, ml.outline_of([t[-1]], proj, smooth=3)) for i, t in enumerate(ml.limb(key, part)["toes"]) if i in hoofed and len(t) > 1]
    scales([heel] + [t[0] for t in toes], OX, OY, 15 / S, 3 / S, seed=seed, exclude=[h for _, h in hv])
    mid = np.mean([a for _, a, _ in toes])
    rank = lambda j: (abs(toes[j][1] - mid), j)
    over = {i: [toes[j][0] for j in range(len(toes)) if rank(j) < rank(i)] for i in range(len(toes))}   # middle toes on top
    for i, (t_, ang, L) in enumerate(toes):
        visible(t_, [heel] + over[i], OX, OY, sw=sw)
        a = math.radians(ang)
        ux, uy = math.cos(a), math.sin(a)
        wd = max(-uy * x + ux * y for x, y in t_) - min(-uy * x + ux * y for x, y in t_)    # toe width across its axis
        t0 = next((t / 50 for t in range(50) if not inside((ux * t / 50 * L, uy * t / 50 * L), heel)), 0.5)
        n = pads[min(i, len(pads) - 1)]
        end = 0.7 if i in hoofed else 0.86
        for k in range(n):
            t = t0 + 0.06 + (end - t0 - 0.06) * k / max(1, n - 1)
            if t < end + 0.01:
                ellipse(ux * t * L, uy * t * L, 0.18 * wd, 0.26 * wd, OX, OY, rot=ang, sw=1.3)
    visible(heel, [], OX, OY, sw=sw)
    for i, h in hv:      # each hoof from below: the edge where it meets the toe's skin
        for r in ml.hoof_lines([h], toes[i][0], 1.5 / S, hide=[heel] + over[i]):
            outline(r, OX, OY, sw=1.8, close=False)
        c = np.mean(h, axis=0)       # and the hoof wall's inner edge round its front, where the sole begins
        rim = [tuple(c + 0.72 * (np.array(p) - c)) for p in h if np.hypot(*p) > np.hypot(*c)]
        rim = [p for p in rim if not any(inside(p, o) for o in [heel] + over[i])]
        if len(rim) > 2:
            k = max(range(len(rim)), key=lambda j: math.dist(rim[j - 1], rim[j]))    # the ring's seam, if it is inside the arc
            outline(rim[k:] + rim[:k] if k else rim, OX, OY, stroke="#555", sw=1.3, close=False)
    for r in fb["raised"]:     # toes held off the ground: dashed
        out.append(f'<path d="{path_d(r, OX, OY, close=True)}" fill="none" stroke="#777" stroke-width="1.6" stroke-dasharray="5 4"/>')
    for base, ang, L, wd in fb["claws"]:
        claw(base, ang, L, wd * claw_w, OX, OY, curl=0)
    return fb


def model_front(key, part, OX, OY, cut, seed, sw=2.6, box=None):
    """Hand (or foot) from the front, the wrist (ankle) at x = 0: outline cut open at `cut`, grey bones, scale
    dots, hoof edges. box as model_side. Returns {"outline", "OX", "OY"}."""
    L = ml.limb(key, part)
    A, _ = ml._ref(key, part)
    proj = lambda V: np.c_[(V[:, 1] - A[1]) / 100, V[:, 2] / 100]
    leg = ml.open_at(ml.outline_of(ml.body_pieces(L), proj, soft=ml.SOFT.get(key, 0.0)), cut)
    if box:
        OX, OY = _fit(box, ml.extent([leg]))
    fill_shape(leg, OX, OY)
    w = _width_at(leg, 0.6 * cut)
    tubes = [np.array(t[0].v, float) for t in L["toes"]]     # each toe's tube: its last two vertices are the end caps
    ball = np.mean([proj(T[-2:-1])[0] for T in tubes], axis=0)
    bone((0.0, cut - 0.06 * cut), (0.0, A[2] / 100), 0.22 * w, OX, OY, GHOST)           # forearm (shin) bones
    bone((0.0, A[2] / 100), tuple(ball), 0.18 * w, OX, OY, GHOST)                     # palm (metatarsals)
    for T in tubes:
        b, t = proj(T[-2:])
        rv = (T[:, 2].max() - T[:, 2].min()) / 200
        bone(tuple(b), tuple(b + (t - b) * 0.9), 0.35 * rv, OX, OY, GHOST)           # finger (toe) bones
    hv = ml.hoof_shapes(key, part, proj, lambda V: V[:, 0])
    scales([leg], OX, OY, 15 / S, 3 / S, seed=seed, exclude=hv)
    visible(open_edge(leg), [], OX, OY, sw=sw, closed=False)
    for r in ml.hoof_lines(hv, leg, 1.5 / S):
        outline(r, OX, OY, sw=1.8, close=False)
    return {"outline": leg, "OX": OX, "OY": OY}
