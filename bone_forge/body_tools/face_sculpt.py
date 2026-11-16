"""The skull under the skin: facial anatomy sculpted into the heads (Raven 2026-11-03: "more depth and shape to the
dinosaurs' faces so they look less like just colored and textured shapes").

head_builder.py (and trike_body_v2.py for the Triceratops) lofts each upper skull and lower jaw as a smooth tube.
This pushes their surface in and out along its normals with soft brushes placed from the skull pictures (Reference
Library; the design doc's Mouths and Feet tab): brow bosses over sunken eye sockets, cheekbones, the jaw muscles
bulging behind the eyes, the hollow in front of the eye (the antorbital fossa), nostril hollows, a lip line along the
mouth, the rough nasal bones of the T. rex, the Triceratops' flaring cheek points and horn bosses, the Ankylosaurus'
head plates. It stays one surface, so there are no seams; the eyes and lids (parts_builder.py FACE) are placed
afterwards on the sculpted skull, so they sit in their sockets.

Positions: along the skull u (0 the back of the head part, 1 the snout tip) and up it v (0 the mouth line, 1 the top
of the skull; negative down the lower jaw, -1 its bottom edge), or in cm from the eye. Sizes and depths in cm,
mostly as shares of the skull length S or the eye radius r.
"""
import math


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def vertex_normals(P):
    """Area-weighted outward vertex normals (Unreal axes)."""
    n = [[0.0, 0.0, 0.0] for _ in P.v]
    for a, b, c in P.t:
        A, B, C = P.v[a], P.v[b], P.v[c]
        u = (B[0] - A[0], B[1] - A[1], B[2] - A[2])
        w = (C[0] - A[0], C[1] - A[1], C[2] - A[2])
        f = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        for i in (a, b, c):
            n[i][0] += f[0]
            n[i][1] += f[1]
            n[i][2] += f[2]
    out = []
    for q in n:
        L = math.sqrt(q[0] ** 2 + q[1] ** 2 + q[2] ** 2) or 1.0
        out.append((q[0] / L, q[1] / L, q[2] / L))
    # The pieces are wound outward in OBJ axes, which mirror Unreal's: check which way these point.
    c = [sum(v[k] for v in P.v) / len(P.v) for k in range(3)]
    if sum(sum((v[k] - c[k]) * m[k] for k in range(3)) for v, m in zip(P.v, out)) < 0:
        out = [(-a, -b, -c_) for a, b, c_ in out]
    return out


class Head:
    """The upper skull and lower jaw being sculpted, with brushes that add up before anything moves."""

    def __init__(self, head, jaw):
        self.P = [head] + ([jaw] if jaw is not None else [])
        self.N = [vertex_normals(P) for P in self.P]
        self.D = [[0.0] * len(P.v) for P in self.P]
        xs = [v[0] for v in head.v]
        self.x0, self.x1 = min(xs), max(xs)
        self.S = self.x1 - self.x0
        # Heights along the skull, measured as parts_builder.head_heights does: the upper skull's lowest and highest
        # points within 2% of the skull length (its lowest is the mouth line, or the bottom behind the jaw), and the
        # jaw's lowest.
        n = 100
        band = 0.02 * self.S
        self.gx = [self.x0 + self.S * k / n for k in range(n + 1)]
        self.lo, self.hi, self.jb = [], [], []
        for x in self.gx:
            up = [v[2] for v in head.v if abs(v[0] - x) < band] or [v[2] for v in head.v]
            dn = [v[2] for v in jaw.v if abs(v[0] - x) < band] if jaw is not None else []
            self.lo.append(min(up))
            self.hi.append(max(up))
            self.jb.append(min(dn) if dn else min(up))

    def _interp(self, arr, x):
        t = (x - self.x0) / self.S * (len(self.gx) - 1)
        k = max(0, min(len(self.gx) - 2, int(t)))
        f = max(0.0, min(1.0, t - k))
        return arr[k] + (arr[k + 1] - arr[k]) * f

    def at(self, u, v):
        """(x, z) of a point u along the skull and v up it."""
        x = self.x0 + u * self.S
        lo, hi, jb = (self._interp(a, x) for a in (self.lo, self.hi, self.jb))
        return x, (lo + v * (hi - lo) if v >= 0 else lo + v * (lo - jb))

    def height(self, u):
        """The skull's height from the mouth line to the top, u along it."""
        x = self.x0 + u * self.S
        return self._interp(self.hi, x) - self._interp(self.lo, x)

    def _skin(self, x, a, sgn, top):
        """The skin nearest (x, z) on one side (sgn) or nearest (x, y) seen from above (top): (point, normal)."""
        best = []
        for p, P in enumerate(self.P):
            N = self.N[p]
            for i, v in enumerate(P.v):
                n = N[i]
                if top:
                    if n[2] < 0.3:
                        continue
                    d = (v[0] - x) ** 2 + (v[1] - a) ** 2
                else:
                    if v[1] * sgn <= 0 or n[1] * sgn < 0.15:
                        continue
                    d = (v[0] - x) ** 2 + (v[2] - a) ** 2
                best.append((d, v, n))
        best.sort(key=lambda b: b[0])
        best = best[:6]
        c = tuple(sum(b[1][k] for b in best) / len(best) for k in range(3))
        n = [sum(b[2][k] for b in best) for k in range(3)]
        L = math.sqrt(sum(q * q for q in n)) or 1.0
        c = (x, c[1], a) if not top else (x, a, c[2])
        return c, tuple(q / L for q in n)

    def _push(self, c, facing, rx, rq, amp, sharp=False):
        for p, P in enumerate(self.P):
            N, D = self.N[p], self.D[p]
            for i, v in enumerate(P.v):
                dx = (v[0] - c[0]) / rx
                if dx * dx >= 1.0:
                    continue
                d2 = dx * dx + ((v[1] - c[1]) ** 2 + (v[2] - c[2]) ** 2) / (rq * rq)
                if d2 >= 1.0:
                    continue
                face = N[i][0] * facing[0] + N[i][1] * facing[1] + N[i][2] * facing[2]
                if face <= 0.15:        # only the skin facing the same way (not through a thin beak to the far side)
                    continue
                f = (1.0 - math.sqrt(d2)) ** 2 if sharp else (1.0 - d2) ** 3
                D[i] += amp * f * smooth(0.15, 0.55, face)

    def side(self, xz, rx, rz, amp, sharp=False):
        """A bump (amp > 0) or hollow centred on the side of the face at (x, z), both sides."""
        for sgn in (-1, 1):
            c, n = self._skin(xz[0], xz[1], sgn, False)
            self._push(c, n, rx, rz, amp, sharp)

    def top(self, x, y, rx, ry, amp):
        """A bump or hollow on top of the skull at (x, y); off the midline it is mirrored."""
        for s in ((1,) if abs(y) < 1e-6 else (-1, 1)):
            c, n = self._skin(x, s * y, 0, True)
            self._push(c, n, rx, ry, amp)

    def line(self, pts, r, amp, samples=16):
        """A ridge (amp > 0) or groove along the side of the face through (x, z) points, both sides; it fades in and
        out over its first and last sixth."""
        dense = []
        for k in range(samples + 1):
            t = k / samples * (len(pts) - 1)
            j = min(int(t), len(pts) - 2)
            f = t - j
            dense.append((pts[j][0] + (pts[j + 1][0] - pts[j][0]) * f, pts[j][1] + (pts[j + 1][1] - pts[j][1]) * f))
        for sgn in (-1, 1):
            skin = [self._skin(x, z, sgn, False) for x, z in dense]
            L = [0.0]
            for a, b in zip(skin, skin[1:]):
                L.append(L[-1] + math.dist(a[0], b[0]))
            total = L[-1] or 1.0
            lo = [min(s[0][k] for s in skin) - r for k in range(3)]
            hi = [max(s[0][k] for s in skin) + r for k in range(3)]
            for p, P in enumerate(self.P):
                N, D = self.N[p], self.D[p]
                for i, v in enumerate(P.v):
                    if any(v[k] < lo[k] or v[k] > hi[k] for k in range(3)):
                        continue
                    best = None
                    for s in range(len(skin) - 1):
                        a, b = skin[s][0], skin[s + 1][0]
                        ab = [b[k] - a[k] for k in range(3)]
                        ll = sum(q * q for q in ab) or 1e-9
                        t = max(0.0, min(1.0, sum((v[k] - a[k]) * ab[k] for k in range(3)) / ll))
                        d2 = sum((v[k] - a[k] - ab[k] * t) ** 2 for k in range(3)) / (r * r)
                        if best is None or d2 < best[0]:
                            best = (d2, s, t)
                    d2, s, t = best
                    if d2 >= 1.0:
                        continue
                    na, nb = skin[s][1], skin[s + 1][1]
                    facing = [na[k] + (nb[k] - na[k]) * t for k in range(3)]
                    face = sum(N[i][k] * facing[k] for k in range(3)) / (math.sqrt(sum(q * q for q in facing)) or 1.0)
                    if face <= 0.15:
                        continue
                    along = (L[s] + (L[s + 1] - L[s]) * t) / total
                    fade = smooth(0.0, 1 / 6, along) * smooth(1.0, 5 / 6, along)
                    D[i] += amp * (1.0 - d2) ** 3 * fade * smooth(0.15, 0.55, face)

    def finish(self):
        """Move the skin. A hollow never goes deeper than a third of the way to the midline (thin beaks)."""
        for p, P in enumerate(self.P):
            N, D = self.N[p], self.D[p]
            out = []
            for v, n, d in zip(P.v, N, D):
                if d < 0 and abs(n[1]) > 0.5:
                    d = max(d, -0.33 * abs(v[1]))
                out.append((v[0] + n[0] * d, v[1] + n[1] * d, v[2] + n[2] * d))
            P.v = out


# ---------------------------------------------------------------- the species
# Each gets the head, the skull length S, and its eye (x, z, radius r) as parts_builder.FACE places it.

def trex(H, ex, ez, r):
    """Deep skull with a small eye under a rough brow: the lacrimal horn in front of the eye and the postorbital boss
    behind it, a deep cheek (jugal) under it, the big jaw muscles swelling the back of the skull, a hollow in front
    of the eye where the antorbital opening is, rough paired nasal bones along the top of the snout."""
    S = H.S
    H.side((ex, ez), 2.0 * r, 1.7 * r, -0.5 * r)                                      # socket
    H.side((ex + 1.5 * r, ez + 1.15 * r), 1.5 * r, 1.0 * r, 1.4 * r)                  # lacrimal horn
    H.side((ex - 0.6 * r, ez + 1.3 * r), 1.7 * r, 1.0 * r, 1.3 * r)                   # postorbital boss
    H.line([(ex + 1.6 * r, ez + 1.05 * r), (ex - 0.9 * r, ez + 1.25 * r)], 0.8 * r, 0.7 * r)   # brow between them
    H.line([(ex + 1.4 * r, ez - 1.8 * r), H.at(0.18, 0.28), H.at(0.07, 0.22)], 0.05 * S, 0.03 * S)   # cheekbone
    H.side(H.at(0.12, 0.72), 0.07 * S, 0.10 * S, 0.03 * S)                            # jaw muscles behind the eye
    H.side(H.at(0.47, 0.50), 0.11 * S, 0.08 * S, -0.025 * S)                          # antorbital fossa
    H.side(H.at(0.61, 0.42), 0.04 * S, 0.035 * S, -0.01 * S)                          # maxillary fenestra
    for u in (0.52, 0.59, 0.66, 0.73, 0.80, 0.87):                                     # rough nasals, two rows
        H.top(H.x0 + u * S, 0.17 * H.height(u), 0.028 * S, 0.03 * S, 0.01 * S)
    H.side(H.at(0.93, 0.60), 0.035 * S, 0.03 * S, -0.012 * S)                         # nostril hollow
    H.line([H.at(u, 0.14) for u in (0.14, 0.4, 0.7, 0.96)], 0.02 * S, -0.01 * S)      # lip line
    H.line([H.at(u, 0.05) for u in (0.16, 0.4, 0.7, 0.95)], 0.02 * S, 0.008 * S)      # the lip over the teeth
    H.line([H.at(u, -0.14) for u in (0.24, 0.5, 0.75, 0.93)], 0.016 * S, -0.008 * S)  # lower lip line
    H.side(H.at(0.28, -0.45), 0.055 * S, 0.04 * S, -0.018 * S)                        # mandibular fenestra
    H.line([H.at(0.22, -0.2), H.at(0.40, -0.16)], 0.025 * S, 0.01 * S)                # jaw muscle ridge on the jaw


def trike(H, ex, ez, r):
    """A small eye under the brow horn; horn bosses; the cheek bone flaring out under the eye to the epijugal horn
    (its own part, trike_body_v2.py); a big nostril hollow; the beak's edge; the tooth row set in under a cheek ridge."""
    S = H.S
    H.side((ex, ez), 1.9 * r, 1.7 * r, -0.5 * r)                                      # socket
    H.top(336.0, 30.0, 0.08 * S, 0.08 * S, 0.045 * S)                                 # brow horn bosses
    H.top(410.0, 0.0, 0.07 * S, 0.06 * S, 0.035 * S)                                  # nose horn boss
    H.side((342.0, 123.0), 0.09 * S, 0.08 * S, 0.04 * S)                              # cheek under the epijugal
    H.line([(ex + 0.6 * r, ez - 1.6 * r), (340.0, 140.0), (342.0, 124.0)], 0.045 * S, 0.025 * S)   # cheekbone to it
    H.side((300.0, 160.0), 0.07 * S, 0.09 * S, 0.025 * S)                             # jaw muscles at the frill base
    H.side(H.at(0.80, 0.46), 0.065 * S, 0.055 * S, -0.03 * S)                         # nostril hollow
    H.line([H.at(0.89, 0.06), H.at(0.885, 0.5), H.at(0.87, 0.9)], 0.016 * S, -0.01 * S)    # edge of the beak
    H.line([H.at(0.87, -0.06), H.at(0.86, -0.5), H.at(0.85, -0.9)], 0.016 * S, -0.01 * S)  # edge of the lower beak
    H.line([H.at(u, 0.32) for u in (0.36, 0.55, 0.74)], 0.03 * S, 0.014 * S)          # cheek ridge over the teeth
    H.line([H.at(u, 0.12) for u in (0.36, 0.55, 0.76)], 0.022 * S, -0.014 * S)        # the tooth row set in


def raptor(H, ex, ez, r):
    """A long, low skull with a big eye: a light brow, the jugal bar under the eye, jaw muscles behind it, a long
    antorbital hollow and the smaller maxillary one in front, a nostril hollow near the tip."""
    S = H.S
    H.side((ex, ez), 1.9 * r, 1.7 * r, -0.4 * r)                                      # socket
    H.line([(ex + 1.2 * r, ez + 1.05 * r), (ex - 0.9 * r, ez + 1.1 * r)], 0.65 * r, 0.35 * r)  # brow
    H.line([(ex + 0.8 * r, ez - 1.5 * r), H.at(0.12, 0.25), H.at(0.05, 0.2)], 0.035 * S, 0.018 * S)  # jugal bar
    H.side(H.at(0.10, 0.62), 0.06 * S, 0.10 * S, 0.02 * S)                            # jaw muscles
    H.side(H.at(0.47, 0.50), 0.11 * S, 0.075 * S, -0.02 * S)                          # antorbital fossa
    H.side(H.at(0.63, 0.45), 0.035 * S, 0.03 * S, -0.01 * S)                          # maxillary fenestra
    H.side(H.at(0.88, 0.58), 0.035 * S, 0.03 * S, -0.012 * S)                         # nostril hollow
    H.line([H.at(u, 0.14) for u in (0.2, 0.5, 0.8, 0.96)], 0.018 * S, -0.007 * S)     # lip line
    H.line([H.at(u, -0.14) for u in (0.3, 0.6, 0.9)], 0.015 * S, -0.006 * S)          # lower lip line
    H.side(H.at(0.28, -0.45), 0.05 * S, 0.035 * S, -0.012 * S)                        # mandibular fenestra


def ankylo(H, ex, ez, r):
    """A low, wide, armoured head: heavy brow bosses over the eye, domed plates over the skull roof, the bases of
    the corner horns swelling out of the skull, forward nostrils in a deep hollow, a plate along the lower jaw."""
    S = H.S
    H.side((ex, ez), 1.7 * r, 1.5 * r, -0.55 * r)                                     # socket
    H.line([(ex + 1.4 * r, ez + 1.2 * r), (ex - 1.4 * r, ez + 1.35 * r)], 1.1 * r, 1.7 * r)   # brow boss
    for u, w in ((0.25, 0.5), (0.45, 0.5), (0.65, 0.45), (0.82, 0.35)):               # head plates, paired
        x = H.x0 + u * S
        hw = max(abs(v[1]) for v in H.P[0].v if abs(v[0] - x) < 0.02 * S)
        H.top(x, w * hw, 0.085 * S, 0.085 * S, 0.05 * S)
    for u in (0.35, 0.55, 0.74):                                                      # and down the middle
        H.top(H.x0 + u * S, 0.0, 0.08 * S, 0.07 * S, 0.04 * S)
    H.side((272.0, 152.0), 0.1 * S, 0.1 * S, 0.07 * S)                               # squamosal horn base
    H.side((278.0, 110.0), 0.1 * S, 0.09 * S, 0.07 * S)                               # cheek horn base
    H.side(H.at(0.93, 0.50), 0.06 * S, 0.06 * S, -0.045 * S)                         # nostril hollow
    H.line([H.at(0.9, 0.06), H.at(0.9, 0.8)], 0.025 * S, -0.016 * S)                   # edge of the beak
    H.line([H.at(u, 0.14) for u in (0.3, 0.6, 0.88)], 0.025 * S, -0.016 * S)           # lip line
    H.line([H.at(u, -0.55) for u in (0.42, 0.62, 0.84)], 0.045 * S, 0.03 * S)         # the plate on the lower jaw


def brachio(H, ex, ez, r):
    """The nasal arch over the eyes with the big nostril hollows either side of its ridge, a light brow, the hollow
    in front of the eye under the arch, jaw muscles behind the eye, a cheek line to the corner of the mouth."""
    S = H.S
    H.side((ex, ez), 1.8 * r, 1.6 * r, -0.55 * r)                                     # socket
    H.line([(ex + 1.1 * r, ez + 1.05 * r), (ex - 1.0 * r, ez + 1.15 * r)], 0.8 * r, 0.8 * r)  # brow
    H.side(H.at(0.47, 0.80), 0.1 * S, 0.07 * S, -0.06 * S)                            # narial fossa
    for u in (0.38, 0.47, 0.56):                                                       # the ridge between them
        H.top(H.x0 + u * S, 0.0, 0.07 * S, 0.035 * S, 0.03 * S)
    H.side(H.at(0.53, 0.42), 0.08 * S, 0.06 * S, -0.045 * S)                          # antorbital fossa
    H.side(H.at(0.12, 0.55), 0.07 * S, 0.10 * S, 0.045 * S)                           # jaw muscles
    H.line([(ex + 0.6 * r, ez - 1.5 * r), H.at(0.2, 0.25), H.at(0.1, 0.18)], 0.045 * S, 0.035 * S)  # cheekbone
    H.line([H.at(u, 0.14) for u in (0.28, 0.6, 0.95)], 0.022 * S, -0.016 * S)         # lip line
    H.line([H.at(u, -0.14) for u in (0.35, 0.65, 0.92)], 0.018 * S, -0.012 * S)       # lower lip line


def ptera(H, ex, ez, r):
    """A small skull at the back of the beak: a socket and a light brow, jaw muscles behind the eye, the shallow
    hollow of the big opening in front of it, the keratin beak's cutting edges."""
    S = H.S
    H.side((ex, ez), 1.8 * r, 1.6 * r, -0.4 * r)                                      # socket
    H.line([(ex + 1.0 * r, ez + 1.05 * r), (ex - 0.9 * r, ez + 1.1 * r)], 0.65 * r, 0.35 * r)  # brow
    H.side(H.at(0.04, 0.60), 0.03 * S, 0.4 * H.height(0.04), 0.01 * S)                # jaw muscles
    H.side(H.at(0.22, 0.50), 0.06 * S, 0.3 * H.height(0.22), -0.006 * S)              # nasoantorbital opening
    H.line([H.at(u, 0.10) for u in (0.3, 0.6, 0.95)], 0.007 * S, 0.0025 * S)          # cutting edge, upper
    H.line([H.at(u, -0.10) for u in (0.32, 0.6, 0.93)], 0.007 * S, 0.0025 * S)        # and lower


SPECIES = {"trex": trex, "trike": trike, "raptor": raptor, "ankylo": ankylo, "brachio": brachio, "ptera": ptera}


def eye(key, H, parts):
    """The eye's (x, z, radius) as parts_builder.face places it on the unsculpted skull."""
    from parts_builder import FACE
    sp = FACE[key]
    if sp.get("anchor"):      # under a horn's base: the base is the horn piece's last vertex
        name, dx, dz = sp["anchor"]
        base = next(P.v[-1] for n, ps in parts if n == name for P in ps if P.v[-1][1] > 0)
        return base[0] + dx, base[2] + dz, sp["r"]
    x, z = H.at(*sp["eye"])
    return x, z, sp["r"]


def sculpt(key, parts):
    """Sculpt the head and jaw in [(name, [Piece])] in place; returns parts."""
    head = next(ps[0] for n, ps in parts if n == "head")
    jaw = next((ps[0] for n, ps in parts if n == "jaw"), None)
    H = Head(head, jaw)
    SPECIES[key](H, *eye(key, H, parts))
    H.finish()
    return parts
