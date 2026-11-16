"""Leg and foot outlines traced from the 3D model, for the life drawings and close-up sheets.

The 3D legs and feet are built by body_tools/leg_builder.py from the rig joints (2026-11-03). This module builds
the same pieces and traces their outlines in a drawing's own units (metres), so a sheet shows exactly the legs
the model has. Claws come back separately (base, angle, length, width) so the sheets keep drawing them with
lib.py's claw().

Side views:  x = drawing metres (rig x / 100 + the sheet's offset), y = height above the ground in metres.
Front views: x = metres from the midline (s = +1 or -1 picks the side), y = height.
Close-ups:   any projection, through outline_of(pieces, project).

    import model_limbs as ml
    near_leg = ml.side_outline("trex", "leg", x0, cut=3.25)
    for base, ang, L, w in ml.side_claws("trex", "leg", x0): claw(base, ang, L, w, OX, OY, curl=25)
"""
import math
import os
import sys
from types import SimpleNamespace

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.normpath(os.path.join(HERE, "..", "..", "body_tools"))
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)
import leg_builder as lb  # noqa: E402

_cache = {}


def limb(key, part):
    """Pieces of the right-side limb (part "leg" or "arm"), tagged: {"leg": [tube], "toes": [[pieces], ...],
    "claws": [piece], "dewclaw": [pieces], "pads": [pieces]}. Rig axes, cm (X forward, Y right, Z up).
    A toe's last piece is its hoof or nail when it has one (leg_builder.toe and nail_toe both end that way)."""
    if (key, part) in _cache:
        return _cache[(key, part)]
    tags = {}
    real_toe, real_nail, real_horn, real_dew = lb.toe, lb.nail_toe, lb.horn, lb.dewclaw
    toe_no = [0]

    def tagged(real):
        def toe(*a, **k):
            out = real(*a, **k)
            for P in out:
                tags.setdefault(id(P), ("toe", toe_no[0]))
            toe_no[0] += 1
            return out
        return toe

    def horn(*a, **k):
        P = real_horn(*a, **k)
        tags[id(P)] = ("claw", None)
        return P

    def dewclaw(*a, **k):
        out = real_dew(*a, **k)
        for P in out:
            if tags.get(id(P), ("",))[0] == "claw":
                tags[id(P)] = ("claw", "dewclaw")
            tags.setdefault(id(P), ("dewclaw", None))
        return out

    lb.toe, lb.nail_toe, lb.horn, lb.dewclaw = tagged(real_toe), tagged(real_nail), horn, dewclaw
    try:
        parts = lb.build_limbs(key)
    finally:
        lb.toe, lb.nail_toe, lb.horn, lb.dewclaw = real_toe, real_nail, real_horn, real_dew
    pieces = parts[f"{part}_r"]
    res = {"leg": [pieces[0]], "toes": [], "claws": [], "dewclaw": [], "pads": []}
    for P in pieces[1:]:
        kind, n = tags.get(id(P), ("pad", None))
        if kind == "claw":
            res["claws"].append(P)
            if n == "dewclaw":
                res.setdefault("dewclaw_claws", []).append(P)
        elif kind == "toe":
            while len(res["toes"]) <= n:
                res["toes"].append([])
            res["toes"][n].append(P)
        elif kind == "dewclaw":
            res["dewclaw"].append(P)
        else:
            res["pads"].append(P)
    res["toes"] = [t for t in res["toes"] if t]
    # A pad that doesn't reach the ground is a raised toe (the raptor's toe II with the sickle claw).
    res["raised"] = [P for P in res["pads"] if min(v[2] for v in P.v) > 0.5]
    res["pads"] = [P for P in res["pads"] if P not in res["raised"]]
    _cache[(key, part)] = res
    return res


def body_pieces(L, claws=False):
    out = list(L["leg"]) + [p for t in L["toes"] for p in t] + L["dewclaw"] + L["pads"] + L.get("raised", [])
    return out + (L["claws"] if claws else [])


def claw_geom(P):
    """(base, tip, base radius) of a horn piece, cm. horn() adds the tip vertex then the base vertex last."""
    tip, base = np.array(P.v[-2]), np.array(P.v[-1])
    r = float(np.mean([np.linalg.norm(np.array(v) - base) for v in P.v[:6]]))
    return base, tip, r


# ------------------------------------------------------------------ outline tracing
def _raster(tris2d, res, pad=3):
    lo = tris2d.reshape(-1, 2).min(0) - pad * res
    hi = tris2d.reshape(-1, 2).max(0) + pad * res
    W, H = (np.ceil((hi - lo) / res).astype(int) + 1)
    g = np.zeros((H, W), bool)
    for t in tris2d:
        (x0, y0), (x1, y1), (x2, y2) = t
        c0, c1 = int((min(x0, x1, x2) - lo[0]) / res), int((max(x0, x1, x2) - lo[0]) / res) + 1
        r0, r1 = int((min(y0, y1, y2) - lo[1]) / res), int((max(y0, y1, y2) - lo[1]) / res) + 1
        xs = lo[0] + (np.arange(c0, c1 + 1) + 0.5) * res
        ys = lo[1] + (np.arange(r0, r1 + 1) + 0.5) * res
        X, Y = np.meshgrid(xs, ys)
        d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(d) < 1e-12:
            continue
        a = ((y1 - y2) * (X - x2) + (x2 - x1) * (Y - y2)) / d
        b = ((y2 - y0) * (X - x2) + (x0 - x2) * (Y - y2)) / d
        m = (a >= -1e-9) & (b >= -1e-9) & (a + b <= 1 + 1e-9)
        g[r0:r1 + 1, c0:c1 + 1] |= m[: max(0, min(H, r1 + 1) - r0), : max(0, min(W, c1 + 1) - c0)]
    return g, lo


DIRS = [(0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1)]


def _trace(g):
    """Outer boundary (Moore neighbour tracing) of the blob that holds the first filled cell."""
    rows, cols = np.nonzero(g)
    k = np.lexsort((cols, rows))[0]
    s = (int(rows[k]), int(cols[k]))
    cur, back, path = s, 0, [s]
    for _ in range(4 * g.size):
        for i in range(1, 9):
            d = (back + i) % 8
            n = (cur[0] + DIRS[d][0], cur[1] + DIRS[d][1])
            if g[n]:
                p = (cur[0] + DIRS[(back + i - 1) % 8][0], cur[1] + DIRS[(back + i - 1) % 8][1])
                back = DIRS.index((p[0] - n[0], p[1] - n[1]))
                cur = n
                break
        else:
            break
        if cur == s and back == 0:
            break
        path.append(cur)
    return path


def _rdp(pts, eps):
    """Ramer-Douglas-Peucker, iterative (outlines can have thousands of points)."""
    P = np.array(pts)
    keep = np.zeros(len(P), bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(P) - 1)]
    while stack:
        i0, i1 = stack.pop()
        if i1 - i0 < 2:
            continue
        a, ab = P[i0], P[i1] - P[i0]
        n = np.hypot(*ab)
        Q = P[i0 + 1:i1] - a
        d = np.abs(ab[0] * Q[:, 1] - ab[1] * Q[:, 0]) / n if n > 1e-12 else np.hypot(Q[:, 0], Q[:, 1])
        k = int(np.argmax(d))
        if d[k] > eps:
            m = i0 + 1 + k
            keep[m] = True
            stack += [(i0, m), (m, i1)]
    return [tuple(p) for p in P[keep]]


def _dilate(g, n):
    """n steps of 3x3 dilation, alternating the cross and the square: an octagon, close to a disc of radius n."""
    for i in range(n):
        h = g.copy()
        h[1:] |= g[:-1]; h[:-1] |= g[1:]; h[:, 1:] |= g[:, :-1]; h[:, :-1] |= g[:, 1:]
        if i % 2:
            h[1:, 1:] |= g[:-1, :-1]; h[1:, :-1] |= g[:-1, 1:]; h[:-1, 1:] |= g[1:, :-1]; h[:-1, :-1] |= g[1:, 1:]
        g = h
    return g


def _soften(pts, step, sigma):
    """Closed outline resampled every `step` and smoothed along its length (Gaussian, sigma in the same units)."""
    A = np.array(pts + [pts[0]])
    seg = np.hypot(*np.diff(A, axis=0).T)
    s = np.r_[0, np.cumsum(seg)]
    t = np.arange(0, s[-1], step)
    A = np.c_[np.interp(t, s, A[:, 0]), np.interp(t, s, A[:, 1])]
    k = sigma / step
    w = np.exp(-0.5 * (np.arange(-int(3 * k), int(3 * k) + 1) / k) ** 2)
    w /= w.sum()
    n = len(w) // 2
    W = np.r_[A[-n:], A, A[:n]] if n else A
    return np.c_[np.convolve(W[:, 0], w, "valid"), np.convolve(W[:, 1], w, "valid")]


def outline_of(pieces, project, res=None, smooth=2, soft=0.0):
    """Closed outline (clockwise, y up) of the pieces' shadow under project: (N,3) cm -> (N,2) units.
    soft (units) rounds the model's facets and the steps between its tube sections: notches narrower than
    about 2 x soft fill in, and the outline is smoothed over about soft / 2."""
    tris = []
    for P in pieces:
        V = project(np.array(P.v, float))
        tris.append(V[np.array(P.t)])
    tris = np.concatenate(tris)
    ext = tris.reshape(-1, 2).max(0) - tris.reshape(-1, 2).min(0)
    res = res or max(ext) / 700
    n = int(round(soft / res))
    g, lo = _raster(tris, res, pad=n + 3)
    if n:
        g = ~_dilate(~_dilate(g, n), n)      # closing
    path = _trace(g)
    pts = [(lo[0] + (c + 0.5) * res, lo[1] + (r + 0.5) * res) for r, c in path]
    # Smooth the pixel steps, then thin.
    A = np.array(pts)
    for _ in range(smooth):
        A = (np.roll(A, 1, 0) + 2 * A + np.roll(A, -1, 0)) / 4
    if n:
        A = _soften([tuple(p) for p in A], res, soft / 2)
    pts = [tuple(p) for p in A]
    pts = _rdp(pts + [pts[0]], res * 0.8)[:-1]
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))
    return pts[::-1] if area > 0 else pts      # clockwise


def open_at(pts, cut):
    """The outline cut open above y = cut, listed from the top of the front edge round to the top of the back
    edge (the drawing scripts' "open" parts: the top edge is where the limb joins the body). cut is a height,
    or a function (x, y) -> keep the point. A height cut ends both edges exactly at that height."""
    pts = [(x, max(y, 0.0)) for x, y in pts]          # nothing below the ground line
    keep = [cut(*p) if callable(cut) else p[1] <= cut for p in pts]
    n = len(pts)
    if all(keep):
        i = max(range(n), key=lambda k: pts[k][1])
        return pts[i:] + pts[:i]
    start = next(i for i in range(n) if keep[i] and not keep[i - 1])
    out = []
    i = start
    while keep[i % n] and len(out) < n:
        out.append(pts[i % n])
        i += 1
    if not callable(cut):
        def at_cut(p, q):      # where the segment p-q crosses y = cut
            t = (cut - p[1]) / (q[1] - p[1])
            return (p[0] + (q[0] - p[0]) * t, cut)
        out = [at_cut(pts[start], pts[start - 1])] + out + [at_cut(pts[(i - 1) % n], pts[i % n])]
    return out


# ------------------------------------------------------------------ the sheets' views
# Outline softening per species, metres (outline_of's soft). The hoofed feet are built from short tubes and
# low-poly pad blobs whose facets and section steps read as blocky once traced; the clawed feet are left sharp.
SOFT = {"trike": 0.07, "ankylo": 0.05, "brachio": 0.08}


def side_outline(key, part, x0, cut, which="all"):
    L = limb(key, part)
    pieces = body_pieces(L) if which == "all" else L[which]
    return open_at(outline_of(pieces, lambda V: np.c_[V[:, 0] / 100 + x0, V[:, 2] / 100], soft=SOFT.get(key, 0.0)), cut)


def front_outline(key, part, s, cut):
    """cut: height, or (outer height, inner height, x where inner starts) for thighs that reach the midline."""
    L = limb(key, part)
    pts = outline_of(body_pieces(L), lambda V: np.c_[s * V[:, 1] / 100, V[:, 2] / 100], soft=SOFT.get(key, 0.0))
    if s < 0:
        pts = pts[::-1]
    if isinstance(cut, tuple):
        outer, inner, xi = cut
        return open_at(pts, lambda x, y: y <= (outer if abs(x) >= xi else inner))
    return open_at(pts, cut)


def side_claws(key, part, x0):
    """(base, angle deg, length, width) per claw in a side view (the far claws hidden behind nearer ones too)."""
    out = []
    for P in limb(key, part)["claws"]:
        b, t, r = claw_geom(P)
        d = (t - b)[[0, 2]] / 100
        out.append(((b[0] / 100 + x0, b[2] / 100), math.degrees(math.atan2(d[1], d[0])), float(np.hypot(*d)), 2 * r / 100))
    return out


def front_claws(key, part, s):
    out = []
    for P in limb(key, part)["claws"]:
        b, t, r = claw_geom(P)
        full = np.linalg.norm(t - b) / 100
        d = np.array([s * (t - b)[1], (t - b)[2]]) / 100
        L = float(np.hypot(*d))
        if L < 0.45 * full:    # pointing at the viewer: draw it short and downward, as the sheets do
            out.append(((s * b[1] / 100, b[2] / 100), -90.0, 0.45 * full, 2 * r / 100))
        else:
            out.append(((s * b[1] / 100, b[2] / 100), math.degrees(math.atan2(d[1], d[0])), L, 2 * r / 100))
    return out


def joints(key):
    return lb.joints(key)


# ------------------------------------------------------------------ close-up panels
REF = {"leg": ("foot", "ball"), "arm": ("hand", "finger_end")}


def _ref(key, part):
    J = joints(key)
    a, b = REF[part]
    return np.array(J[a + "_r"]), np.array(J[b + "_r"])


def side_panel(key, part, cut):
    """Lower limb from the side, in metres with the ankle (wrist) at x = 0 and the ground at y = 0, cut open at
    height `cut`: {"outline", "claws": [(base, ang, L, w)], "toe": (ball, claw base) of the longest toe,
    "ankle": (x, y)}."""
    L = limb(key, part)
    A, B = _ref(key, part)
    proj = lambda V: np.c_[(V[:, 0] - A[0]) / 100, V[:, 2] / 100]
    out = {"outline": open_at(outline_of(body_pieces(L), proj, soft=SOFT.get(key, 0.0)), cut), "claws": [], "ankle": (0.0, A[2] / 100)}
    best = None
    for P in L["claws"]:
        b, t, r = claw_geom(P)
        d = (t - b)[[0, 2]] / 100
        c = ((b[0] - A[0]) / 100, b[2] / 100)
        out["claws"].append((c, math.degrees(math.atan2(d[1], d[0])), float(np.hypot(*d)), 2 * r / 100))
        if b[2] < 0.5 * A[2] and (best is None or b[0] > best[0]):
            best = (b[0], c)
    ball = ((B[0] - A[0]) / 100, B[2] / 100)
    out["toe"] = (ball, best[1] if best else ball)
    return out


def below_panel(key, part):
    """The foot from below, toes up the page, metres with the ball at the origin: {"heel": outline, "toes":
    [(outline, angle deg, length)], "claws": [(base, ang, L, w)]}. Only pieces on the ground (no dewclaw)."""
    L = limb(key, part)
    A, B = _ref(key, part)
    # x across the page (the animal's outside to the right), y = forward (up the page).
    proj = lambda V: np.c_[(V[:, 1] - B[1]) / 100, (V[:, 0] - B[0]) / 100]
    out = {"heel": None, "toes": [], "claws": [], "raised": [outline_of([P], proj) for P in L["raised"]]}
    pads = L["pads"]
    if not pads:      # a pillar foot has no pad: its sole is the bottom of the leg itself, below half the ankle height
        T = L["leg"][0]
        low = 0.5 * A[2]
        pads = [SimpleNamespace(v=T.v, t=[t for t in T.t if all(T.v[i][2] < low for i in t)])]
    soft = SOFT.get(key, 0.0)
    out["heel"] = outline_of(pads, proj, soft=soft)
    for t in L["toes"]:
        o = outline_of(t, proj, soft=soft / 2)
        tip = max(o, key=lambda p: p[0] ** 2 + p[1] ** 2)
        out["toes"].append((o, math.degrees(math.atan2(tip[1], tip[0])), math.hypot(*tip)))
    for P in L["claws"]:
        b, t, r = claw_geom(P)
        if P in L.get("dewclaw_claws", []) or b[2] > 0.3 * A[2]:   # claws held up off the ground don't show from below
            continue
        d = (t - b)[[1, 0]] / 100
        out["claws"].append((((b[1] - B[1]) / 100, (b[0] - B[0]) / 100), math.degrees(math.atan2(d[1], d[0])),
                             float(np.hypot(*d)) * 1.1, 2 * r / 100))
    return out


def extent(pts_lists):
    xs = [p[0] for pts in pts_lists for p in pts]
    ys = [p[1] for pts in pts_lists for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def _cap(H):
    """A hoof outline (y up, clockwise) as a nail cap: its upper half as traced, its sides straight down to the
    ground (y = 0), flaring a little. The model's hoof is a rounded blob; drawn whole it reads as a ball."""
    A = np.array(H)
    cy = (A[:, 1].min() + A[:, 1].max()) / 2
    up = A[:, 1] >= cy
    i = next(k for k in range(len(A)) if up[k] and not up[k - 1])
    A, up = np.roll(A, -i, 0), np.roll(up, -i)
    arc = A[:int(np.argmin(up))]
    (x0, _), (x1, _) = arc[0], arc[-1]
    f = 0.15 * cy * (1 if x1 > x0 else -1)
    return [tuple(p) for p in arc] + [(x1 + f, 0.0), (x0 - f, 0.0)]


def hoof_shapes(key, part, project, depth):
    """Each hoof (the blob on a hoofed toe's tip) as a nail cap under project (y up, the ground at y = 0), farthest
    first by depth: (N,3) cm -> (N,) larger = nearer the viewer."""
    out = []
    for t in limb(key, part)["toes"]:
        if len(t) < 2:          # a toe without its hoof piece (the small outer fingers)
            continue
        V = np.array(t[-1].v, float)
        out.append((float(depth(V).mean()), _cap(outline_of([t[-1]], project, smooth=3))))
    return [o for _, o in sorted(out, key=lambda e: e[0])]


def side_hoof_shapes(key, part, x0):
    return hoof_shapes(key, part, lambda V: np.c_[V[:, 0] / 100 + x0, V[:, 2] / 100], lambda V: V[:, 1])


def front_hoof_shapes(key, part, s):
    return hoof_shapes(key, part, lambda V: np.c_[s * V[:, 1] / 100, V[:, 2] / 100], lambda V: V[:, 0])


def _inside(pts, poly):
    """Even-odd point-in-polygon for many points at once."""
    P = np.asarray(pts, float)
    Q = np.asarray(poly, float)
    x, y = P[:, :1], P[:, 1:]
    x1, y1 = Q[:, 0], Q[:, 1]
    x2, y2 = np.roll(x1, -1), np.roll(y1, -1)
    cross = ((y1 > y) != (y2 > y)) & (x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1)
    return cross.sum(1) % 2 == 1


def _dist(pts, line):
    """Distance from each point to a polyline."""
    P = np.asarray(pts, float)[:, None, :]
    A = np.asarray(line[:-1], float)[None]
    D = np.asarray(line[1:], float)[None] - A
    t = np.clip(((P - A) * D).sum(2) / np.maximum((D * D).sum(2), 1e-18), 0, 1)
    return np.hypot(*(P - A - t[..., None] * D).transpose(2, 0, 1)).min(1)


def hoof_lines(shapes, silhouette, tol, step=None, hide=()):
    """The hooves' edges that show inside a limb's silhouette (where the hoof meets the skin of the toe), each
    hidden behind the nearer hooves and the `hide` shapes. The silhouette already draws their outer edges.
    shapes farthest first (hoof_shapes); silhouette a closed outline (an open_at outline is closed across its
    cut); tol: how near the silhouette an edge counts as on it. Returns polylines."""
    sil = list(silhouette) + [silhouette[0]]
    runs = []
    for i, H in enumerate(shapes):
        ring = np.array(list(H) + [H[0]])
        seg = np.hypot(*np.diff(ring, axis=0).T)
        s = np.r_[0, np.cumsum(seg)]
        t = np.arange(0, s[-1], step or tol / 2)
        D = np.c_[np.interp(t, s, ring[:, 0]), np.interp(t, s, ring[:, 1])]
        keep = _inside(D, sil) & (_dist(D, sil) > tol)
        for o in list(shapes[i + 1:]) + list(hide):
            keep &= ~_inside(D, o)
        own, cur = [], []
        for p, k in zip(D, keep):
            if k:
                cur.append(tuple(p))
            elif cur:
                own.append(cur)
                cur = []
        if cur:
            if own and keep[0]:      # the ring started part-way along a run: join its two ends
                own[0] = cur + own[0]
            else:
                own.append(cur)
        runs += own
    return [r for r in runs if len(r) > 2]
