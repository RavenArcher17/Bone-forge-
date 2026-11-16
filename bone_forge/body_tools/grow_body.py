"""Bone Forge grown bodies (catalog of six): the flesh is grown from the skeleton instead of sculpted.

Each bone carries a tissue profile, its "muscle anchors": cross-sections of the flesh around it at stations along
the bone (centre and half-sizes across and up, in the bone's own frame). Bones run in chains: the trunk from the
tail tip through the hips, back and neck to the head, and each limb from the hip or shoulder to the ankle or wrist.
Along a chain the flesh is one tube whose cross-section flows from station to station through the joints, so a
neck from one species tapers into a chest from another; the chains blend where they meet (hips, shoulders). The
body is that smooth union, meshed as one skin. Detailed parts stay as bone-part meshes riding their bones: heads,
jaws, teeth, eyes, horns, frills, crests, armour, tail clubs, feathers, wing membranes, and the feet and hands
(everything from the ankle or wrist down).

    python grow_body.py measure [species ...]   measure the hand-built bodies into the rigs' joints ("tissue")
    python grow_body.py grow <rig.json> <features.obj> <out.obj>
                                                 grow a body from a rig's tissue; features.obj holds the bone parts
    python grow_body.py [species ...]           all three for the six: body_shapes/<sp>_features.obj, _grown.obj

The profiles are measured from the hand-built bodies (body_shapes/<sp>_body.obj), so the catalog keeps their
proportions; a grown body is judged against its hand-built one before anything uses it.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "unreal"))
import bone_forge_gltf as bfg  # noqa: E402

SPECIES = ["trex", "trike", "raptor", "ankylo", "brachio", "ptera"]
GROWN_PARTS = ("body", "neck", "arm", "leg")        # soft tissue in the hand-built bodies ...
NOT_GROWN = ("arm_fingers", "leg_toes", "arm_claws", "leg_claws")     # ... except these small bone parts
STATIONS_PER_LENGTH = 60                            # stations along the body's length (more on a longer chain)
BEND_SOFTEN = 0.3                                   # the blur round a knee or elbow, as a share of the limb's half-width there
LIMB_VOXELS = 1.8                                   # voxels at least across the thinnest limb's half-thickness
MAX_CELLS = 24e6                                  # the voxel grid's most cells (a far-oversized splice's voxels grow)
FOOT_CUT = 0.55                                     # feet are cut from the hand-built legs this far down the shin
                                                    # (below a hanging thigh: an Ankylosaurus' overhangs mid-shin)


# ---------------------------------------------------------------------------------------------- skeleton
def part_base(name):
    pieces = name.split("_")
    return "_".join(pieces[:-1]) if len(pieces) > 1 and pieces[-1].isdigit() else name


def named(base, words):
    """Whether a part's name is one of words or starts with one as a whole word ("arm_l" is an arm, "armor" isn't)."""
    return any(base == w or base.startswith(w + "_") for w in words)


def is_grown(name):
    b = part_base(name)
    return named(b, GROWN_PARTS) and not named(b, NOT_GROWN)


def subtree(J, n):
    out = [n]
    for c in J[n].children:
        out += subtree(J, c)
    return out


def distal_bones(J):
    """Bones whose flesh stays a bone-part mesh: each foot and hand and everything below them, and a tail club (a
    Pteranodon's front limb is a wing: its hand bone is grown, the wing finger isn't)."""
    # (the wing-finger test is for hands alone: hand_2_l's finger is wingfinger_01_2_l. It used to catch the feet
    # too, "foot_l"[4:] being "_l" as well, so the Pteranodon's feet grew as capped stumps under its toes.)
    starts = [n for n in J if n.startswith(("foot_", "hand_")) and n.endswith(("_l", "_r"))
              and not (n.startswith("hand_") and "wingfinger_01" + n[4:] in J)]
    starts += [n for n in J if n.startswith("wingfinger_01_")]
    starts += [n for n in J if n == "tail_club"]
    out = set()
    for s in starts:
        out.update(subtree(J, s))
    return out


def bone_axes(frames, b):
    F = frames[b]
    return tuple(np.array([F[0][k], F[1][k], F[2][k]]) for k in range(3))   # X, Y, Z


class Chain:
    """A run of bones the flesh flows along. The axis is a smooth curve through the joints (a cubic through each
    joint along the average of its two bones' directions), cut into short pieces, so a bend (a neck rising out of the
    chest) stays round instead of folding; each piece belongs to the bone whose stretch it is on (whose tissue and
    frame it uses)."""

    def __init__(self, name, joints, owners, part_prefixes, step):
        self.name, self.parts = name, part_prefixes
        J = [np.array(p, dtype=float) for p in joints]
        dirs = []
        for i in range(len(J)):
            a = J[i] - J[i - 1] if i > 0 else J[1] - J[0]
            b = J[i + 1] - J[i] if i < len(J) - 1 else J[i] - J[i - 1]
            a, b = a / max(np.linalg.norm(a), 1e-9), b / max(np.linalg.norm(b), 1e-9)
            d = a + b
            dirs.append(d / max(np.linalg.norm(d), 1e-9))
        pts, own = [J[0]], []
        for i in range(len(J) - 1):
            L = float(np.linalg.norm(J[i + 1] - J[i]))
            n = max(2, int(np.ceil(L / step)))
            for k in range(1, n + 1):
                t = k / n
                h00, h10, h01, h11 = 2 * t**3 - 3 * t**2 + 1, t**3 - 2 * t**2 + t, -2 * t**3 + 3 * t**2, t**3 - t**2
                pts.append(h00 * J[i] + h10 * L * dirs[i] + h01 * J[i + 1] + h11 * L * dirs[i + 1])
                own.append(owners[i])
        self.points, self.owners = np.array(pts), own
        seg = self.points[1:] - self.points[:-1]
        self.L = np.maximum(np.linalg.norm(seg, axis=1), 1e-6)
        self.D = seg / self.L[:, None]
        self.s = np.concatenate([[0.0], np.cumsum(self.L)])
        T = np.concatenate([self.D[:1], self.D[:-1] + self.D[1:], self.D[-1:]])
        self.T = T / np.maximum(np.linalg.norm(T, axis=1), 1e-9)[:, None]    # the curve's direction at its points

    def tangent(self, i, f):
        """The curve's direction at fraction f along piece i, turning smoothly from piece to piece (a piece's own
        direction jumps at every corner, and an off-centre cross-section would step there: rings)."""
        t = (1 - f) * self.T[i] + f * self.T[i + 1]
        return t / np.maximum(np.linalg.norm(t, axis=-1), 1e-9)[..., None]

    def at(self, s):
        """At distances s along the curve: (piece, distance along it, point, smooth direction)."""
        j = np.clip(np.searchsorted(self.s, s, side="right") - 1, 0, len(self.L) - 1)
        u = s - self.s[j]
        return j, u, self.points[j] + self.D[j] * u[:, None], self.tangent(j, np.clip(u / self.L[j], 0.0, 1.0)[:, None])

    def foot(self, P, s):
        """Where points P sit along the smooth curve: the s near their nearest-piece s at which P - curve(s) is square
        to the curve's direction. Off the outside of a bend the nearest point on the pieces sticks at each corner
        (a whole wedge of flesh reads the same s, so a taper pauses there: a ring at every piece); this moves on
        smoothly round the bend."""
        reach = 2.0 * float(self.L.max())
        a, b = np.maximum(s - reach, 0.0), np.minimum(s + reach, self.s[-1])
        for _ in range(16):
            m = 0.5 * (a + b)
            _, _, C, T = self.at(m)
            ahead = np.einsum("nk,nk->n", P - C, T) > 0
            a, b = np.where(ahead, m, a), np.where(ahead, b, m)
        return 0.5 * (a + b)

    def segments(self):
        for i in range(len(self.L)):
            yield i, self.points[i], self.D[i], float(self.L[i]), self.owners[i]

    def project(self, P):
        """For points P (N x 3): (piece index, distance along it, distance from the axis) of the nearest axis point;
        the chain's two ends run on past their last points."""
        P = np.atleast_2d(P)
        if len(P) > 4000:                     # in batches, to keep the N x pieces arrays small
            parts = [self.project(P[k:k + 4000]) for k in range(0, len(P), 4000)]
            return tuple(np.concatenate([p_[j] for p_ in parts]) for j in range(3))
        A = self.points[:-1]
        U = np.einsum("nmk,mk->nm", P[:, None, :] - A[None, :, :], self.D)
        lo = np.zeros_like(U)
        hi = np.broadcast_to(self.L, U.shape).copy()
        lo[:, 0], hi[:, -1] = -np.inf, np.inf
        Uc = np.clip(U, lo, hi)
        Q = A[None, :, :] + self.D[None, :, :] * Uc[..., None]
        dist = np.linalg.norm(P[:, None, :] - Q, axis=-1)
        i = np.argmin(dist, axis=1)
        r = np.arange(len(P))
        return i, Uc[r, i], dist[r, i]


def chains(J, distal, step):
    """The trunk (tail tip to the head joint) and every limb (hip or shoulder to the ankle or wrist, and a little
    way on into the foot or hand, so the flesh closes over its top)."""
    out = []
    grown = lambda n: n in J and J[n].deforming and n not in distal and J[n].section != "Head"
    if "pelvis" in J:
        fwd, owners_f, cur = [J["pelvis"].pos], [], "pelvis"
        while True:
            mc = bfg.main_child(J[cur])
            if mc is None:
                break
            fwd.append(J[mc].pos)
            owners_f.append(cur)
            if not grown(mc) or J[mc].section in ("Hip", "Shoulder"):
                break
            cur = mc
        tail = []
        cur = next((c for c in J["pelvis"].children if J[c].section == "Tail" and grown(c)), None)
        while cur and grown(cur):
            tail.append(cur)
            cur = bfg.main_child(J[cur])
        pts, owners = [], []
        if tail:
            last = tail[-1]
            prev = J[tail[-2]].pos if len(tail) > 1 else J["pelvis"].pos
            nxt = bfg.main_child(J[last])
            end = J[nxt].pos if nxt else bfg.v_add(J[last].pos, bfg.v_scale(bfg.v_sub(J[last].pos, prev), 0.5))
            pts = [end] + [J[n].pos for n in reversed(tail)]
            owners = list(reversed(tail)) + ["pelvis"]          # the tail_01 -> hips link belongs to the pelvis
        out.append(Chain("trunk", pts + fwd, owners + owners_f, ("body", "neck"), step))
    for top in [n for n in J if J[n].section in ("Hip", "Shoulder") and J[n].parent and J[J[n].parent].section == "Core"]:
        pts, owners, cur = [J[top].pos], [], top
        mount = J[top].info.get("mount_from_cm")
        if mount:     # a spliced limb mounted out on the skin: its flesh starts back at the old socket, inside
            pts, owners = [tuple(mount), J[top].pos], [top]
        while cur and grown(cur):
            mc = bfg.main_child(J[cur])
            if mc is None:
                break
            pts.append(J[mc].pos)
            owners.append(cur)
            cur = mc
        if owners:
            nxt = bfg.main_child(J[cur]) if cur else None
            into = None
            if nxt:
                a = np.subtract(J[cur].pos, pts[-2])
                b = np.subtract(J[nxt].pos, J[cur].pos)
                # On into the foot or hand by 40% of its first bone, so the flesh can take the foot's own shape at
                # its top and close inside it (fit_foot); not where that bone folds back up (a Pteranodon's wing
                # finger from its knuckle), or the curve would loop.
                if float(np.dot(a, b)) > 0.0:
                    into = bfg.v_add(J[cur].pos, bfg.v_scale(bfg.v_sub(J[nxt].pos, J[cur].pos), 0.4))
            if into:
                pts.append(into)
                owners.append(owners[-1])
            side = top[-2:]
            pair = "2" if "_2_" in top else ""      # a third pair: leg2_l, its foot part leg2_foot_l
            ch = Chain(top, pts, owners, (("leg" if J[top].section == "Hip" else "arm") + pair + side,), step)
            ch.into = into is not None
            if cur and owners:   # the plane the foot part is cut at (features): FOOT_CUT down the shin, square to it
                a = np.subtract(J[cur].pos, J[owners[-1]].pos)
                ch.plane = (np.array(J[owners[-1]].pos) + FOOT_CUT * a, a / max(np.linalg.norm(a), 1e-9))
            if ch.into:          # where the ankle or wrist is along it (the flesh tapers inside the foot past it)
                i, u, _ = ch.project(np.array(J[cur].pos)[None])
                ch.end_s = float(ch.s[i[0]] + u[0])
            out.append(ch)
    return out


def nearest_bones(J, bones, P):
    """For points P (N x 3), the index into bones of each point's nearest bone line."""
    A, E = [], []
    for n in bones:
        a = np.array(J[n].pos)
        mc = bfg.main_child(J[n])
        A.append(a)
        E.append(np.array(J[mc].pos) if mc else a + 0.5 * (a - np.array(J[J[n].parent].pos)))
    A, E = np.array(A), np.array(E)
    AB = E - A
    t = np.einsum("nmk,mk->nm", P[:, None, :] - A[None], AB) / np.maximum((AB * AB).sum(1), 1e-9)
    t = np.clip(t, 0.0, 1.0)
    d = np.linalg.norm(P[:, None, :] - (A[None] + AB[None] * t[..., None]), axis=-1)
    return np.argmin(d, axis=1)


def seg_frame(axes, d):
    """Across and up for a segment running along d, from its owner bone's frame (Y kept square to the segment)."""
    X, Y, Z = axes
    y = Y - d * float(np.dot(Y, d))
    y = y / max(np.linalg.norm(y), 1e-9)
    z = np.cross(d, y)
    return y, (z if np.dot(z, Z) >= 0 else -z)


def chain_frames(ch, frames):
    """Each piece's across and up (seg_frame), turned over where a bone's own frame is flipped against the bone before
    it (a tail bone's frame points back and down, the pelvis' forward and up), so they agree along the whole chain;
    and the signs that turn each bone's own across and up into the chain's ({bone: (sy, sz)}). A tissue station is
    stored in its bone's own frame; mixing two frames that disagree would flip its centre's offset at the joint."""
    ys, zs, sy, sz = [], [], [1.0], [1.0]
    for i in range(len(ch.L)):
        y, z = seg_frame(bone_axes(frames, ch.owners[i]), ch.D[i])
        if i:
            sy.append(sy[-1] * (1.0 if np.dot(y, ys[-1]) >= 0 else -1.0))
            sz.append(sz[-1] * (1.0 if np.dot(z, zs[-1]) >= 0 else -1.0))
        ys.append(y)
        zs.append(z)
    sy, sz = np.array(sy), np.array(sz)
    signs = {}
    for i, b in enumerate(ch.owners):
        signs.setdefault(b, (sy[i], sz[i]))
    return np.array(ys) * sy[:, None], np.array(zs) * sz[:, None], signs


# ---------------------------------------------------------------------------------------------- measure
# Superellipse exponents a section's halves may take: 2 is an ellipse, more is squarer. Never under 2: below it the
# curve turns infinitely sharp where it crosses its axes, which grew a keel along the Triceratops' underside.
SQUARE_MIN, SQUARE_MAX = 2.0, 4.0
SMOOTH_AFTER = int(os.environ.get("BF_SMOOTH_AFTER", "10"))    # Taubin passes after the skin's last projection


def squareness(v, w, v0, v1, w0, w1):
    """How square a cross-section is, its upper and lower halves apart: the superellipse exponent (|x|^n + |y|^n = 1,
    2 an ellipse, more a box) whose corner (45 degrees) reaches as far as the flesh does there. The hand-built trunks
    are ellipses above but broad and square below the flanks (up to 18% past the ellipse at the belly's corners), so
    a body grown from ellipses came out slimmer there."""
    cv, cw, hv, hw = 0.5 * (v0 + v1), 0.5 * (w0 + w1), max(0.5 * (v1 - v0), 1e-3), max(0.5 * (w1 - w0), 1e-3)
    a, b = np.abs(v - cv) / hv, (w - cw) / hw
    ang = np.degrees(np.arctan2(b, a))              # folded across: 45 is an upper corner, -45 a lower one

    def reach(k, x):
        return float(np.percentile(x[k], 95)) if k.sum() >= 6 else None
    # The side's and the top's (or bottom's) reach and the corner's, all by the same measure (the outer flesh in a
    # narrow wedge, short of the very outermost): the corner against the extents cut at 1% grew tails and necks too
    # square (5% past the hand-built's upper corners), and the 98th percentile caught the hips' and shoulders' bumps.
    # (A low-poly body can have no vertices straight above its middle, the Ankylosaurus' and Triceratops' trunks: that
    # side's reach is then its extent, 1, as before, rather than no squareness at all.)
    side = reach(np.abs(ang) < 15.0, a) or 1.0
    out = []
    for at, sgn in ((45.0, 1.0), (-45.0, -1.0)):
        top = reach(np.abs(ang - 2.0 * at) < 15.0, sgn * b) or 1.0
        A, B = a / side, sgn * b / top
        k = np.abs(np.degrees(np.arctan2(B, A)) - 45.0) < 10.0
        corner = reach(k & (B > 0), np.hypot(A, B))
        if corner is None:
            out.append(2.0)
            continue
        q = corner / np.sqrt(2.0)                    # the corner's reach as a share of the box's
        n = -1.0 / np.log2(np.clip(q, 0.5, 0.97))
        out.append(float(np.clip(n, SQUARE_MIN, SQUARE_MAX)))
    return out[0], out[1]


def measure(key):
    """Tissue profiles for every grown bone of a species, from its hand-built body; written into its rig JSON."""
    rig_path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
    rig, J, topo = bfg.load_rig(rig_path)
    frames = bfg.bone_frames(J, topo)
    verts, parts = bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{key}_body.obj"))
    distal = distal_bones(J)
    length = max(j.pos[0] for j in J.values()) - min(j.pos[0] for j in J.values())
    spacing = length / STATIONS_PER_LENGTH
    live = [n for n in topo if J[n].deforming and not bfg.is_stub(J[n], J)]

    out = {}
    for ch in chains(J, distal, spacing / 2):
        idx = sorted({i for name, tris in parts if is_grown(name) and named(part_base(name), ch.parts)
                      for t in tris for i in t})
        if not idx:
            continue
        P = np.array([bfg.m_vec(bfg.UE_FROM_OBJ, verts[i]) for i in idx])
        # Feet, hands and the tail club stay bone parts: leave out the flesh nearest them, nearest of all the bones
        # (a Triceratops' belly, 1.2 m under its spine, is nearer its hind feet than its back; weighed against the
        # trunk's bones alone it went as foot flesh and the grown belly caved in).
        keep = ~np.isin(np.array(live)[nearest_bones(J, live, P)], list(distal))
        P = P[keep]
        si, u, _ = ch.project(P)
        ys, zs, signs = chain_frames(ch, frames)
        rows = []
        for k in np.unique(si):
            m = si == k
            Q = P[m] - (ch.points[k] + ch.D[k] * u[m][:, None])
            rows.append(np.stack([ch.s[k] + u[m], Q @ ys[k], Q @ zs[k]], axis=1))
        S = np.concatenate(rows)
        s_lo, s_hi = np.percentile(S[:, 0], 0.1), np.percentile(S[:, 0], 99.9)
        nb = max(2, int(round((s_hi - s_lo) / spacing)))
        edges = np.linspace(s_lo, s_hi, nb + 1)
        for k in range(nb):
            sel = S[(S[:, 0] >= edges[k]) & (S[:, 0] <= edges[k + 1])]
            if len(sel) < 8:
                continue
            v0, v1 = np.percentile(sel[:, 1], [1, 99])
            w0, w1 = np.percentile(sel[:, 2], [1, 99])
            n_up, n_low = squareness(sel[:, 1], sel[:, 2], v0, v1, w0, w1)
            sc = 0.5 * (edges[k] + edges[k + 1])
            # The station belongs to the bone owning that stretch of the chain, at its place along that bone.
            si_ = int(np.clip(np.searchsorted(ch.s, sc) - 1, 0, len(ch.owners) - 1))
            owner = ch.owners[si_]
            pos = ch.points[si_] + ch.D[si_] * (sc - ch.s[si_])
            X = bone_axes(frames, owner)[0]
            mc = bfg.main_child(J[owner])
            L = bfg.v_len(bfg.v_sub(J[mc].pos, J[owner].pos)) if mc else 1.0
            t = float(np.dot(pos - np.array(J[owner].pos), X)) / max(L, 1e-6)
            sy, sz = signs[owner]            # from the chain's frame back into the bone's own
            # (up is the bone frame's up only when sz is +1; flipped, the upper and lower halves swap)
            nu, nl = (n_up, n_low) if sz > 0 else (n_low, n_up)
            out.setdefault(owner, []).append([round(t, 3), round(sy * 0.5 * (v0 + v1), 1), round(sz * 0.5 * (w0 + w1), 1),
                                              round(max(0.5 * (v1 - v0), 0.3), 1), round(max(0.5 * (w1 - w0), 0.3), 1),
                                              round(nu, 2), round(nl, 2)])
    for b in out:
        out[b].sort()

    with open(rig_path, encoding="utf-8") as f:
        data = json.load(f)
    for j in data["joints"]:
        if j["name"] in out:
            j["tissue"] = out[j["name"]]
        else:
            j.pop("tissue", None)
    data["tissue_note"] = ("tissue: the flesh around a bone, the body's muscle anchors: stations [t along the bone "
                           "(0 its joint, 1 the next), centre across (cm, + right), centre up, half-width, half-height], "
                           "in the bone's frame; the flesh flows from station to station along each chain of bones "
                           "(body_tools/grow_body.py measures them from the hand-built body and grows bodies from them). "
                           "Feet, hands, heads and other bone parts keep their own meshes.")
    with open(rig_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(f"{key}: tissue for {len(out)} bones, {sum(len(s) for s in out.values())} stations")
    return out


# ---------------------------------------------------------------------------------------------- grow
def chain_profile(ch, J, frames, tissue, signs):
    """The chain's stations in order along it: arrays s, across, up, half-width, half-height, across and up in the
    chain's frame (signs from chain_frames). A station whose section is under 40% of its neighbours' (one cut where
    only part of the flesh reached it) is dropped."""
    rows = []
    for b in dict.fromkeys(ch.owners):
        if b not in tissue:
            continue
        X = bone_axes(frames, b)[0]
        mc = bfg.main_child(J[b])
        L = bfg.v_len(bfg.v_sub(J[mc].pos, J[b].pos)) if mc else 1.0
        for st in tissue[b]:
            t, cy, cz, ry, rz = st[:5]
            nu, nl = (st[5], st[6]) if len(st) >= 7 else (2.0, 2.0)      # older stations: ellipses
            if signs[b][1] < 0:
                nu, nl = nl, nu                     # the bone's up is the chain's down here
            si, u, _ = ch.project(np.array(J[b].pos) + X * (t * L))
            rows.append((float(ch.s[si[0]] + u[0]), signs[b][0] * cy, signs[b][1] * cz, ry, rz, nu, nl))
    if not rows:
        return None
    R = np.array(sorted(rows))
    area = R[:, 3] * R[:, 4]
    keep = np.ones(len(R), dtype=bool)
    for i in range(len(R)):
        nb = np.concatenate([area[max(0, i - 2):i], area[i + 1:i + 3]])
        if len(nb) and area[i] < 0.4 * np.median(nb):
            keep[i] = False
    R = R[keep]
    # Smooth the sizes along the chain (a few stations' worth), so measuring noise doesn't ring the flesh and a
    # big chest hands over to a thin neck gradually. Each station takes the value of a straight line fitted through
    # its neighbours (weighted by nearness), not their average: at a chain's end the neighbours are all on one side,
    # and an average would carry a thick thigh down to the ankle (a fat lower leg stepping into the foot, a cuff).
    # The fit is read off at eight points per station, not just at the stations: the flesh is interpolated in
    # straight lines between them, and a taper whose slope turns at every station shades as a ring at each one.
    if len(R) > 3:
        span = (R[-1, 0] - R[0, 0]) / (len(R) - 1)
        sigma = 1.2 * span
        Sf = np.linspace(R[0, 0], R[-1, 0], 8 * (len(R) - 1) + 1)
        X = R[:, 0][None, :] - Sf[:, None]
        W = np.exp(-0.5 * (X / sigma) ** 2)
        sw, sx, sxx = W.sum(1), (W * X).sum(1), (W * X * X).sum(1)
        det = sw * sxx - sx * sx
        F = np.empty((len(Sf), R.shape[1]))
        F[:, 0] = Sf
        for c in range(1, R.shape[1]):
            sy, sxy = W @ R[:, c], (W * X) @ R[:, c]
            F[:, c] = np.where(det > 1e-9 * sw * sw, (sxx * sy - sx * sxy) / np.maximum(det, 1e-12), sy / sw)
        F[:, 3:5] = np.maximum(F[:, 3:5], 0.3)
        F[:, 5:7] = np.clip(F[:, 5:7], SQUARE_MIN, SQUARE_MAX)
        R = F
    # The trunk's first stations sit short of the tail's tip (a pointed tail has too little flesh at its end to
    # measure), and the sections hold their size past the last one: the tail ran on as a rod and closed in a knob.
    # Carry the tail's taper on to the tip instead, to a tenth of its last measured size.
    if ch.name == "trunk" and R[0, 0] > ch.s[0] + 1.0 and len(R) > 4:
        near = R[:, 0] < R[0, 0] + 0.1 * (R[-1, 0] - R[0, 0])
        if near.sum() >= 3:
            gap = R[0, 0] - ch.s[0]
            Sx = np.linspace(ch.s[0], R[0, 0], max(3, int(np.ceil(gap / max(R[1, 0] - R[0, 0], 1e-3)))), endpoint=False)
            tip = []
            for c in (3, 4):
                slope = np.polyfit(R[near, 0], R[near, c], 1)[0]
                tip.append(np.clip(R[0, c] - max(slope, 0.0) * (R[0, 0] - Sx), max(0.1 * R[0, c], 0.3), R[0, c]))
            add = np.column_stack([Sx, np.full_like(Sx, R[0, 1]), np.full_like(Sx, R[0, 2]), tip[0], tip[1],
                                   np.full_like(Sx, R[0, 5]), np.full_like(Sx, R[0, 6])])
            R = np.vstack([add, R])
    ch.square = (R[:, 0], R[:, 5], R[:, 6])
    return R[:, 0], R[:, 1], R[:, 2], R[:, 3], R[:, 4]


def section_coords(ch, prof, zs, Yv, P):
    """Where points P sit on a chain's sweep: (s, along, across, up). s is the distance along the curve of each
    point's foot on it, across and up are measured from the curve in the cross-section's frame there; along is how
    far past the chain's start or end the point lies (0 between them)."""
    S = prof[0]
    K = len(ch.L)
    A, Dd, Ls, s0 = ch.points[:-1], ch.D, ch.L, ch.s[:-1]
    lo_u0 = min(0.0, S[0] - s0[0])                    # the curve runs on to the first and last stations
    hi_uK = max(Ls[-1], S[-1] - s0[-1])
    U = np.einsum("nmk,mk->nm", P[:, None, :] - A[None], Dd)
    lo_c, hi_c = np.zeros(K), Ls.copy()
    lo_c[0], hi_c[-1] = -np.inf, np.inf
    Uc = np.clip(U, lo_c, hi_c)
    i = np.argmin(np.linalg.norm(P[:, None, :] - (A[None] + Dd[None] * Uc[..., None]), axis=-1), axis=1)
    u = U[np.arange(len(P)), i]
    before, past = (i == 0) & (u < 0), (i == K - 1) & (u > Ls[-1])
    sv = s0[i] + np.clip(u, 0.0, Ls[i])
    mid = ~(before | past)
    sv[mid] = ch.foot(P[mid], sv[mid])
    j, uc, _, t_ = ch.at(sv)
    j = np.where(before, 0, np.where(past, K - 1, j))
    uc = np.where(before, np.clip(u, lo_u0, 0.0), np.where(past, np.clip(u, Ls[-1], hi_uK), uc))
    du = np.where(before | past, u - uc, 0.0)
    f = np.clip(uc / Ls[j], 0.0, 1.0)[:, None]
    y = (1 - f) * Yv[j] + f * Yv[j + 1]
    y -= t_ * np.einsum("nk,nk->n", y, t_)[:, None]
    y /= np.maximum(np.linalg.norm(y, axis=1), 1e-9)[:, None]
    z = np.cross(t_, y)
    z *= np.sign(np.einsum("nk,nk->n", z, zs[j]) + 1e-9)[:, None]
    q = P - (A[j] + Dd[j] * uc[:, None])
    return s0[j] + uc, du, np.einsum("nk,nk->n", q, y), np.einsum("nk,nk->n", q, z)


def sections_at(ch, prof, sv):
    """The cross-section (centre across, centre up, half-width, half-height) at distances sv along a chain."""
    S, CY, CZ, RY, RZ = prof
    cy, cz = np.interp(sv, S, CY), np.interp(sv, S, CZ)
    ry, rz = np.interp(sv, S, RY), np.interp(sv, S, RZ)
    if getattr(ch, "into", False) and not getattr(ch, "fitted", False):
        # Past the ankle or wrist the flesh runs on inside the foot or hand, narrowing to 65% by its end, so its
        # end stays inside the foot instead of standing out round the ankle in a cuff.
        taper = 1.0 - 0.35 * np.clip((sv - ch.end_s) / max(ch.s[-1] - ch.end_s, 1e-6), 0.0, 1.0)
        ry, rz = ry * taper, rz * taper
    return cy, cz, ry, rz


def sweep_field(ch, prof, zs, Yv, P):
    """One chain's flesh as a signed distance (cm, negative inside) at points P: each point is measured against the
    cross-section at its foot on the chain's curve, in the frame there; past the chain's two ends the flesh closes
    round (flat at a limb's lower end, which sits inside the foot or hand)."""
    sv, du, v, w = section_coords(ch, prof, zs, Yv, P)
    cy, cz, ry, rz = sections_at(ch, prof, sv)
    dv, dw = v - cy, w - cz
    past = du > 0
    cap = np.minimum(ry, rz) * np.where(past & getattr(ch, "into", False), 0.3, 1.0)
    sq = getattr(ch, "square", None)
    if sq is None:
        k0 = np.sqrt((du / cap) ** 2 + (dv / ry) ** 2 + (dw / rz) ** 2)
        k1 = np.sqrt((du / (cap * cap)) ** 2 + (dv / (ry * ry)) ** 2 + (dw / (rz * rz)) ** 2)
        return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)
    # Each half of the section a superellipse (squareness, measure): q its size measure in the section, k with the
    # caps past the ends, and the field (k - 1) over its gradient: a distance near the surface. With both halves at
    # 2 this is the ellipse's field above.
    n = np.where(dw >= 0.0, np.interp(sv, sq[0], sq[1]), np.interp(sv, sq[0], sq[2]))
    a, b = np.abs(dv) / ry, np.abs(dw) / rz
    q = np.maximum((a ** n + b ** n) ** (1.0 / n), 1e-6)
    k = np.sqrt(q * q + (du / cap) ** 2)
    gq = q ** (1.0 - n)
    ga, gb = a ** (n - 1.0) * gq / ry, b ** (n - 1.0) * gq / rz          # dq/d(across), dq/d(up)
    grad = np.sqrt((q / np.maximum(k, 1e-9)) ** 2 * (ga * ga + gb * gb) + (du / (cap * cap * np.maximum(k, 1e-9))) ** 2)
    return (k - 1.0) / np.maximum(grad, 1e-9)


def section_outline(ch, prof, zs, Yv, around=24):
    """Points round a chain's flesh at each point of its curve: its outline without growing it."""
    t = np.linspace(0.0, 2.0 * np.pi, around, endpoint=False)
    cy, cz, ry, rz = sections_at(ch, prof, ch.s)
    out = []
    for i, p in enumerate(ch.points):
        d, y = ch.T[i], Yv[i] - ch.T[i] * float(np.dot(Yv[i], ch.T[i]))
        y = y / max(np.linalg.norm(y), 1e-9)
        z = np.cross(d, y)
        z = z if np.dot(z, zs[min(i, len(zs) - 1)]) >= 0 else -z
        out.append(p + np.outer(cy[i] + ry[i] * np.cos(t), y) + np.outer(cz[i] + rz[i] * np.sin(t), z))
    return np.concatenate(out)


def chain_sweeps(J, frames, tissue, feet=None):
    """Every chain of flesh ready to sweep: (chain, profile, up per piece, across at the curve's points), the
    trunk first. feet: the foot and hand parts' vertices by limb ("leg_l": N x 3), which the limbs' ends are
    fitted to (fit_foot)."""
    length = max(max(j.pos[k] for j in J.values()) - min(j.pos[k] for j in J.values()) for k in range(3))   # its biggest size
    sweeps = []
    for ch in chains(J, distal_bones(J), length / STATIONS_PER_LENGTH):
        ys, zs, signs = chain_frames(ch, frames)
        prof = chain_profile(ch, J, frames, tissue, signs)
        if prof is None:
            continue
        Yv = np.concatenate([ys[:1], ys[:-1] + ys[1:], ys[-1:]])      # frames at the curve's points, averaged
        Yv /= np.maximum(np.linalg.norm(Yv, axis=1), 1e-9)[:, None]
        if feet is not None and getattr(ch, "into", False) and len(feet.get(ch.parts[0], ())):
            prof = fit_foot(ch, prof, zs, Yv, feet[ch.parts[0]])
        sweeps.append((ch, prof, zs, Yv))
    sweeps.sort(key=lambda w: w[0].name != "trunk")
    return sweeps


def fit_foot(ch, prof, zs, Yv, P):
    """A limb's flesh meets its foot (or hand) at the foot part's ring: the hand-built lower leg's top, cut flat across
    the shin (features). Over one radius above the ring the flesh's section eases into the ring's own (centre and
    size), so stitch_feet has little left to bend. The ring is the foot's vertices on the cut plane: measured along the chain (which bends
    towards the foot near the ankle) a flat ring can lean, so it is covered along its whole span. Matched only at the ring: a
    foot followed further down brings its pad and toes into the leg (a skirt, a lobe). P: the foot part's vertices.
    Sets ch.cut = (the ring along the chain, where it reaches lowest; radius)."""
    sv, du, v, w = section_coords(ch, prof, zs, Yv, P)
    s = sv + np.maximum(du, 0.0)                     # past the curve's end, on along its last direction
    S, CY, CZ, RY, RZ = prof
    if getattr(ch, "plane", None) is None:
        return prof
    # The ring: the foot's vertices on the cut plane. Measured along the chain (which bends towards the foot near
    # the ankle) a flat ring can lean; the flesh covers it from where it starts along the chain to where it ends.
    c, n = ch.plane
    h = (P - c) @ n
    m = np.abs(h) < max(1.5, 3.0 * float(np.median(np.sort(np.abs(h))[:max(8, len(h) // 50)])))
    if m.sum() < 10:
        return prof
    s_top, s_low = float(np.percentile(s[m], 2)), float(np.percentile(s[m], 98))
    r = 0.5 * float(np.interp(s_top, S, RY) + np.interp(s_top, S, RZ))
    v0, v1 = np.percentile(v[m], [3, 97])
    w0, w1 = np.percentile(w[m], [3, 97])
    edge = np.array([0.5 * (v0 + v1), 0.5 * (w0 + w1), 0.5 * (v1 - v0), 0.5 * (w1 - w0)])
    cover = edge.copy()             # the ring itself (stitch_feet lands the skin on its exact shape)
    a0, a1 = s_top - r, s_top
    Sb = np.linspace(a0, a1, 24)
    t = (Sb - a0) / (a1 - a0)
    t = t * t * (3 - 2 * t)
    B = np.stack([Sb] + [(1 - t) * np.interp(Sb, S, X) + t * cover[k] for k, X in enumerate((CY, CZ, RY, RZ))], axis=1)
    # Below the ring the flesh draws in and ends inside the hand-built lower leg, well short of the foot (the skin
    # past the ring is cut away by stitch_feet; the slopes just above it are measured from this).
    hold = s_low + 0.1 * r
    Sc = hold + r * np.array([0.0, 0.4, 0.8, 1.1, 1.3])
    size = np.array([1.0, 0.97, 0.88, 0.6, 0.05])
    C = np.stack([Sc, np.full(5, edge[0]), np.full(5, edge[1]), edge[2] * size, edge[3] * size], axis=1)
    above = S < a0
    R = np.concatenate([np.stack([S[above], CY[above], CZ[above], RY[above], RZ[above]], axis=1), B, C])
    ch.fitted, ch.cut = True, (s_low, r)
    # The flesh inside the foot must never hide the foot itself: where a foot's first bone runs forward along the
    # ground (a Triceratops' hand) the flesh running on along it reached the toes and swallowed them. Draw the inner
    # end in and back until next to none of the foot below the covered band lies inside it.
    # Only the tip is drawn in (gently: the section just below the ring stays, or the flesh ends in a lip there).
    below = s > hold + 0.4 * r
    for _ in range(6):
        f = sweep_field(ch, tuple(R.T), zs, Yv, P[below]) if below.any() else np.zeros(0)
        if not len(f) or (f < -0.5).mean() <= 0.01:
            break
        tip = R[:, 0] > hold + 0.2 * r
        R[tip, 3:] *= 0.9
        R[tip, 0] = hold + 0.2 * r + (R[tip, 0] - hold - 0.2 * r) * 0.8
    return R[:, 0], R[:, 1], R[:, 2], R[:, 3], R[:, 4]


def fillet(prof):
    """How far a limb's flesh blends into the trunk's (cm): 40% of the limb's thinnest flesh near its root."""
    S, CY, CZ, RY, RZ = prof
    return 0.4 * float(np.minimum(RY, RZ)[: max(2, len(RY) // 3)].min())


def blur3(A, sigma):
    """A Gaussian blur of a 3D array (sigma in cells), each axis in turn; the edges keep their own values."""
    r = int(np.ceil(3.0 * sigma))
    if r < 1:
        return A
    kernel = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    for axis in range(3):
        pad = [(0, 0)] * 3
        pad[axis] = (r, r)
        B = np.pad(A, pad, mode="edge")
        out = np.zeros_like(A, dtype=np.float64)
        for k, w in enumerate(kernel):
            sl = [slice(None)] * 3
            sl[axis] = slice(k, k + A.shape[axis])
            out += w * B[tuple(sl)]
        A = out
    return A


def soften_bends(C, xs, voxel, J, ch, prof):
    """Round one limb's flesh at its knee or elbow, before it joins the rest of the body (on the finished body the blur
    would web two thighs together). Along a bent chain the inside of the bend is tighter than the limb is thick, so the
    flesh above and below meet in a hard fold with a shelf where the thicker bone overhangs, and bending the joint
    closes that into a ledge. Blurred over a share of the limb's width, fading out by two half-widths from the joint
    and to nothing at the foot's ring, which the skin has to meet exactly."""
    C = C.astype(np.float64)
    for b in list(dict.fromkeys(ch.owners))[1:]:
        c = np.array(J[b].pos, dtype=float)
        si, u, _ = ch.project(c[None])
        _, _, ry, rz = sections_at(ch, prof, np.array([float(ch.s[si[0]] + u[0])]))
        r = float(min(ry[0], rz[0]))
        sigma = BEND_SOFTEN * r / voxel
        if sigma < 0.7:
            continue
        i0 = [int(np.searchsorted(xs[k], c[k] - 2.5 * r)) for k in range(3)]
        i1 = [int(np.searchsorted(xs[k], c[k] + 2.5 * r)) for k in range(3)]
        sl = tuple(slice(i0[k], i1[k]) for k in range(3))
        sub = C[sl]
        if min(sub.shape) < 3:
            continue
        X, Y, Z = (xs[0][i0[0]:i1[0]][:, None, None], xs[1][i0[1]:i1[1]][None, :, None], xs[2][i0[2]:i1[2]][None, None, :])
        t = np.clip((np.sqrt((X - c[0]) ** 2 + (Y - c[1]) ** 2 + (Z - c[2]) ** 2) - r) / r, 0.0, 1.0)
        m = 1.0 - t * t * (3 - 2 * t)
        plane = getattr(ch, "plane", None)
        if plane is not None:
            c0, n = plane
            gap = float(np.dot(c0 - c, n))     # from the joint down to the ring
            if gap > 1e-3:
                h = (X - c0[0]) * n[0] + (Y - c0[1]) * n[1] + (Z - c0[2]) * n[2]
                f = np.clip(-h / gap, 0.0, 1.0)
                m = m * f * f * (3 - 2 * f)
        C[sl] = (1.0 - m) * sub + m * blur3(sub, sigma)
    return C


def grow_sdf(J, frames, tissue, voxel, feet=None):
    """Signed distance (cm, negative inside) of the grown flesh on a voxel grid. Each chain is one sweep
    (sweep_field): every point is measured against the cross-section at its foot on the chain's curve (flowing
    between stations, its frame turning smoothly along the curve), so there are no seams or rings along it. Chains
    join the body so far by a smooth minimum, which makes the fillet where a limb meets the trunk."""
    sweeps = chain_sweeps(J, frames, tissue, feet)
    lo = np.min([ch.points.min(axis=0) - chain_reach(prof) for ch, prof, *_ in sweeps], axis=0)
    hi = np.max([ch.points.max(axis=0) + chain_reach(prof) for ch, prof, *_ in sweeps], axis=0)
    # A part far too big for the animal (a Brachiosaurus upper arm on a raptor) spread the grid far past the body at the
    # body's own small voxels: 66 million cells, 9 minutes to grow. Past MAX_CELLS the voxels grow to fit (the six
    # grow on 6 to 16 million, the Triceratops the most, so they never do).
    cells = float(np.prod((hi - lo) / voxel))
    if cells > MAX_CELLS:
        voxel *= (cells / MAX_CELLS) ** (1.0 / 3.0)
    shape = np.ceil((hi - lo) / voxel).astype(int) + 1
    return field_on(J, sweeps, lo, shape, voxel, voxel), lo, sweeps, voxel


def chain_reach(prof):
    """How far a chain's flesh can reach from its curve (cm), with room for the blend."""
    S, CY, CZ, RY, RZ = prof
    return 1.3 * max(RY.max(), RZ.max()) + max(abs(CY).max(), abs(CZ).max())


def field_on(J, sweeps, lo, shape, voxel, blend_voxel):
    """The grown flesh's signed distance on one grid (origin lo, shape cells of voxel cm): every chain that reaches into
    it, blended in the same order as on the whole body's grid. blend_voxel: the voxel the fillets are widened for (the
    whole body's), so a finer grid round a small limb samples the same field."""
    D = np.full(shape, 1e6, dtype=np.float32)
    for ch, prof, zs, Yv in sweeps:
        r = chain_reach(prof)
        i0 = np.maximum(np.floor((ch.points.min(axis=0) - r - lo) / voxel).astype(int), 0)
        i1 = np.minimum(np.ceil((ch.points.max(axis=0) + r - lo) / voxel).astype(int) + 1, shape)
        if np.any(i1 <= i0):
            continue                          # it doesn't reach this grid
        xs = [lo[k] + voxel * np.arange(i0[k], i1[k]) for k in range(3)]
        G = np.stack(np.meshgrid(*xs, indexing="ij"), axis=-1).reshape(-1, 3)
        out = np.empty(len(G), dtype=np.float32)
        for c0 in range(0, len(G), 20000):
            out[c0:c0 + 20000] = sweep_field(ch, prof, zs, Yv, G[c0:c0 + 20000])
        C = out.reshape(i1 - i0)
        if ch.name != "trunk":
            C = soften_bends(C, xs, voxel, J, ch, prof)
        sl = tuple(slice(i0[k], i1[k]) for k in range(3))
        # At least four voxels: a tighter blend met the trunk in a crease too sharp for the grid, which meshed as a
        # stepped line (the Triceratops' shoulders, 8 cm on 2.6 cm voxels); but no more than half as much again as its
        # own, or a small limb on big voxels webs into the body (the T. rex's arms would have gone from 5.5 to 16 cm).
        k = fillet(prof)
        if ch.name != "trunk":
            k = max(k, min(4.0 * blend_voxel, 1.5 * k))
        sub = D[sl]
        h = np.clip(0.5 + 0.5 * (sub - C) / max(k, 1e-3), 0.0, 1.0)   # polynomial smooth minimum
        D[sl] = sub * (1 - h) + C * h - k * h * (1 - h)
    return D


def buried(ch, P, sweeps):
    """How far outside every other chain's flesh the points P are (cm; negative: inside the body's flesh)."""
    return np.min([sweep_field(o, prof, zs, Yv, P) for o, prof, zs, Yv in sweeps if o is not ch], axis=0)


FINE_PAD = 4.0          # a small limb's own grid reaches this many of the body's voxels past its flesh (where it joins)
FINE_CELLS = 8e6        # the most cells a small limb's own grid has


def fine_regions(sweeps, voxel, thin):
    """Boxes round limbs too thin for the body's voxels (under LIMB_VOXELS of them across their half-thickness, with 2%
    to spare: only where the cell cap or a given voxel kept the body's grid coarser than grow() asks for; thin: each
    limb's half-thickness as grow() measured it): [(lo, hi, fine voxel)], boxes that overlap made one. Each reaches
    FINE_PAD body voxels past the limb's flesh, so its edge crosses the body where the body's own voxels draw it well."""
    boxes = []
    for ch, prof, zs, Yv in sweeps:
        if ch.name == "trunk":
            continue
        r = thin.get(ch.name, float(np.median(np.minimum(prof[3], prof[4]))))
        if r >= 0.98 * LIMB_VOXELS * voxel:
            continue
        _, _, ry, rz = sections_at(ch, prof, ch.s)
        if np.all(buried(ch, ch.points, sweeps) < -np.maximum(ry, rz)):
            continue                          # its flesh is inside the body's all along: nothing of it to draw finer
        m = chain_reach(prof) + FINE_PAD * voxel
        boxes.append((ch.points.min(axis=0) - m, ch.points.max(axis=0) + m, max(r / LIMB_VOXELS, 0.4)))
    merged = True
    while merged:
        merged = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a, b = boxes[i], boxes[j]
                if np.all(a[0] < b[1]) and np.all(b[0] < a[1]):
                    boxes[i] = (np.minimum(a[0], b[0]), np.maximum(a[1], b[1]), min(a[2], b[2]))
                    del boxes[j]
                    merged = True
                    break
            if merged:
                break
    out = []
    for lo, hi, vf in boxes:
        cells = float(np.prod((hi - lo) / vf + 4.0 * voxel / vf))
        if cells > FINE_CELLS:
            vf *= (cells / FINE_CELLS) ** (1.0 / 3.0)
        out.append((lo, hi, vf))
    return out


def boundary_loops(T):
    """The mesh's open edges as closed loops of vertex indices, each in its faces' winding."""
    T = np.asarray(T)
    E = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]])
    have = set(map(tuple, E.tolist()))
    nxt = {}
    for a, b in E.tolist():
        if (b, a) not in have:
            nxt.setdefault(a, []).append(b)
    loops = []
    while nxt:
        start = next(iter(nxt))
        loop, a = [start], start
        while True:
            b = nxt[a].pop()
            if not nxt[a]:
                del nxt[a]
            if b == start:
                break
            loop.append(b)
            a = b
            if a not in nxt:
                break                          # an open run (a vertex touching the edge twice): left as it is
        loops.append(loop)
    return loops


def zipper(V, outer, inner):
    """Triangles closing the band between a loop of the body's mesh (outer) and one of a small limb's patch (inner),
    both in their own faces' winding (so the patch's runs the other way round): along both, always closing the
    shorter diagonal, wound to match both sides."""
    A = list(outer)
    B = list(inner)[::-1]
    j0 = int(np.argmin(np.linalg.norm(V[B] - V[A[0]], axis=1)))
    B = B[j0:] + B[:j0]
    n, m = len(A), len(B)
    i = j = 0
    tris = []
    while i < n or j < m:
        a0, a1 = A[i % n], A[(i + 1) % n]
        b0, b1 = B[j % m], B[(j + 1) % m]
        if j >= m or (i < n and np.linalg.norm(V[a1] - V[b0]) <= np.linalg.norm(V[a0] - V[b1])):
            tris.append((a1, a0, b0))
            i += 1
        else:
            tris.append((b0, b1, a0))
            j += 1
    return tris


def patch_in(V, T, label, Vf, Tf, ri, blo, bhi):
    """The body's mesh with a small limb's finer patch in place of its own inside the box (blo, bhi): the body's faces
    with any corner in the box out, the patch's with every corner in it kept, and the band between their edges closed
    (zipper). Returns (V, T, label): label is -1 for the body's vertices, ri for this patch's. None if their edges
    didn't pair up (the body's own mesh is kept then)."""
    def inside(P):
        return np.all((P > blo) & (P < bhi), axis=1)
    Tc = T[~inside(V)[T].any(axis=1)]
    Tp = Tf[inside(Vf)[Tf].all(axis=1)]
    used_c, used_f = np.unique(Tc), np.unique(Tp)
    mc = -np.ones(len(V), dtype=np.int64)
    mc[used_c] = np.arange(len(used_c))
    mf = -np.ones(len(Vf), dtype=np.int64)
    mf[used_f] = np.arange(len(used_f)) + len(used_c)
    V2 = np.concatenate([V[used_c], Vf[used_f]])
    L2 = np.concatenate([label[used_c], np.full(len(used_f), ri)])
    T2 = np.concatenate([mc[Tc], mf[Tp]])
    loops = boundary_loops(T2)
    outer = [l for l in loops if L2[l[0]] != ri]
    inner = [l for l in loops if L2[l[0]] == ri]
    if len(outer) != len(inner) or not outer:
        print(f"  small limb patch {ri}: {len(outer)} body edge loops against {len(inner)} of the patch; not used")
        return None
    band, free = [], list(range(len(outer)))
    for li in sorted(inner, key=len, reverse=True):
        P = V2[li]
        k = min(free, key=lambda o: float(np.mean(np.min(np.linalg.norm(V2[outer[o]][:, None] - P[None], axis=2), axis=0))))
        free.remove(k)
        band += zipper(V2, outer[k], li)
    T2 = np.concatenate([T2, np.array(band, dtype=np.int64)])
    if boundary_loops(T2):
        print(f"  small limb patch {ri}: the band left open edges; not used")
        return None
    return V2, T2, L2


def chain_shares(J, frames, tissue, P, min_width=0.0, feet=None):
    """Which chain of flesh each point P (on a grown skin) belongs to: (bone lists, shares N x chains). A point on
    the trunk belongs to the trunk, one on a leg to that leg; across the fillet where they blend the share passes
    smoothly from one to the other. bone_forge_gltf skins grown skin by these, so the belly follows the hips and
    back the way a hand-built body does, never a leg on one vertex and an arm on the next. min_width: the
    narrowest blend (cm); the skin's own edges must fit inside it (a splice sized by a long neck has big voxels)."""
    sweeps = chain_sweeps(J, frames, tissue, feet)
    F, bones, width = [], [], []
    for ch, prof, zs, Yv in sweeps:
        F.append(np.concatenate([sweep_field(ch, prof, zs, Yv, P[c0:c0 + 20000]) for c0 in range(0, len(P), 20000)]))
        own = list(dict.fromkeys(ch.owners))
        nxt = bfg.main_child(J[own[-1]])
        if ch.name != "trunk" and nxt and J[nxt].deforming:
            own.append(nxt)                     # the flesh closing inside the foot or hand moves with it
        bones.append(own)
        width.append(fillet(prof) if ch.name != "trunk" else None)
    limb = [w for w in width if w]
    width = [max(w if w else (min(limb) if limb else 1.0), min_width) for w in width]
    F = np.array(F)
    share = np.exp(-(F - F.min(axis=0)) / np.array(width)[:, None])
    share /= share.sum(axis=0)
    return bones, share.T


def surface_nets(D, lo, voxel):
    """Mesh of the zero surface: a vertex in every cell the surface crosses (at the mean of its edge crossings),
    a quad across every grid edge it crosses. Returns (vertices, triangles); grow() fixes the winding."""
    inside = D < 0
    nx, ny, nz = D.shape
    corners = [(i, j, k) for i in (0, 1) for j in (0, 1) for k in (0, 1)]
    edges = [(a, b) for a in range(8) for b in range(a + 1, 8)
             if sum(abs(corners[a][d] - corners[b][d]) for d in range(3)) == 1]
    cv = [D[i:nx - 1 + i, j:ny - 1 + j, k:nz - 1 + k] for i, j, k in corners]
    acc = np.zeros((nx - 1, ny - 1, nz - 1, 3), dtype=np.float32)
    cnt = np.zeros((nx - 1, ny - 1, nz - 1), dtype=np.float32)
    for a, b in edges:
        va, vb = cv[a], cv[b]
        cross = (va < 0) != (vb < 0)
        t = np.where(cross, va / np.where(cross, va - vb, 1.0), 0.0)
        pa, pb = np.array(corners[a], dtype=np.float32), np.array(corners[b], dtype=np.float32)
        acc += np.where(cross[..., None], pa + (pb - pa) * t[..., None], 0.0)
        cnt += cross
    has = cnt > 0
    index = -np.ones(has.shape, dtype=np.int64)
    cells = np.argwhere(has)
    index[has] = np.arange(len(cells))
    verts = (cells + acc[has] / cnt[has][:, None]) * voxel + lo
    quads = []
    for axis in range(3):
        s0 = [slice(0, n) for n in D.shape]
        s1 = [slice(0, n) for n in D.shape]
        s0[axis] = slice(0, D.shape[axis] - 1)
        s1[axis] = slice(1, D.shape[axis])
        a_in, b_in = inside[tuple(s0)], inside[tuple(s1)]
        flip = a_in & ~b_in
        cross = flip | (~a_in & b_in)
        o1, o2 = [d for d in range(3) if d != axis]
        pts = np.argwhere(cross)
        f = flip[cross]
        keep = (pts[:, o1] > 0) & (pts[:, o2] > 0) & (pts[:, o1] < D.shape[o1] - 1) & (pts[:, o2] < D.shape[o2] - 1)
        pts, f = pts[keep], f[keep]

        def cell(d1, d2):
            c = pts.copy()
            c[:, o1] -= d1
            c[:, o2] -= d2
            return index[c[:, 0], c[:, 1], c[:, 2]]
        q = np.stack([cell(1, 1), cell(0, 1), cell(0, 0), cell(1, 0)], axis=1)
        q[~f] = q[~f][:, ::-1]
        if axis == 1:
            q = q[:, ::-1]
        quads.append(q)
    quads = np.concatenate(quads)
    quads = quads[(quads >= 0).all(axis=1)]
    return verts, np.concatenate([quads[:, [0, 1, 2]], quads[:, [0, 2, 3]]])


def smooth(verts, tris, passes=8, lam=0.5, mu=-0.53):
    """Taubin smoothing: takes the voxel steps off without shrinking the body."""
    n = len(verts)
    e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
    e = np.concatenate([e, e[:, ::-1]])
    rows, cols = e[:, 0], e[:, 1]
    deg = np.bincount(rows, minlength=n).astype(np.float64)
    V = verts.astype(np.float64)
    for _ in range(passes):
        for f in (lam, mu):
            S = np.zeros_like(V)
            np.add.at(S, rows, V[cols])
            V = V + f * (S / np.maximum(deg, 1)[:, None] - V)
    return V


def soften_creases(V, T, deg=15.0, passes=10, rounds=3):
    """The grown skin has no sharp edges of its own (all detail is in the bone parts), so any crease is the meshing's:
    where two chains' flesh meets in a tight valley (the Triceratops' upper arms meeting under its throat folded faces
    flat back on each other, and its shoulders showed a stepped line). Every vertex on an edge sharper than deg, and
    its neighbours, is relaxed; the skin's open edge (each foot's and hand's ring) and the row next to it hold still."""
    V = np.asarray(V, dtype=np.float64)
    T = np.asarray(T)
    E = np.sort(np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]]), axis=1)
    F = np.tile(np.arange(len(T)), 3)            # E lists every face's first edge, then every second, then every third
    order = np.lexsort((E[:, 1], E[:, 0]))
    E, F = E[order], F[order]
    same = np.all(E[1:] == E[:-1], axis=1)
    pair = np.where(same)[0]                     # edge k and k+1 are one edge, two faces
    lone = np.ones(len(E), dtype=bool)
    lone[pair] = lone[pair + 1] = False
    fixed = np.zeros(len(V), dtype=bool)
    fixed[E[lone].ravel()] = True                # the open edge ...
    ring = np.unique(T[np.isin(T, np.where(fixed)[0]).any(axis=1)])
    fixed[ring] = True                           # ... and the row beside it
    softened = 0
    for _ in range(rounds):
        fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
        fn /= np.maximum(np.linalg.norm(fn, axis=1), 1e-12)[:, None]
        cosang = np.einsum("ij,ij->i", fn[F[pair]], fn[F[pair + 1]])
        sharp = pair[cosang < np.cos(np.radians(deg))]
        if not len(sharp):
            break
        hot = np.zeros(len(V), dtype=bool)
        hot[E[sharp].ravel()] = True
        hot[np.unique(T[hot[T].any(axis=1)])] = True     # and their neighbours
        hot &= ~fixed
        idx = np.where(hot)[0]
        softened = max(softened, len(sharp))
        # Plain averaging (no Taubin back-step): a fold has to be pulled flat, and these patches are small.
        V = relax(V, T, idx, np.ones(len(idx)), passes=passes, mu=0.0)
    return V, softened


def relax(P, T, idx, weight, passes=10, lam=0.5, mu=-0.53):
    """Taubin smoothing of just the vertices idx (the rest hold still), each by its weight (0..1)."""
    P = np.asarray(P, dtype=np.float64).copy()
    n = len(P)
    e = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]])
    e = np.concatenate([e, e[:, ::-1]])
    sel = np.zeros(n, dtype=bool)
    sel[idx] = True
    e = e[sel[e[:, 0]]]                        # only the moving vertices' edges
    rows, cols = e[:, 0], e[:, 1]
    deg = np.bincount(rows, minlength=n).astype(np.float64)
    wv = np.zeros(n)
    wv[idx] = weight
    for _ in range(passes):
        for f in (lam, mu):
            S = np.zeros_like(P)
            np.add.at(S, rows, P[cols])
            avg = S / np.maximum(deg, 1)[:, None]
            m = deg > 0
            P[m] += (f * wv[m])[:, None] * (avg[m] - P[m])
    return P


def onto_surface(V, D, lo, voxel, passes=2, value=False):
    """Moves mesh vertices onto the zero surface of the field D (trilinear), along its gradient: a surface-nets
    vertex sits somewhere in its voxel, which shows as faint terracing on a gentle slope. value: just the field's
    value at V."""
    hi = np.array(D.shape) - 2

    def at(Q):
        g = (Q - lo) / voxel
        i = np.clip(np.floor(g).astype(int), 0, hi)
        f = np.clip(g - i, 0.0, 1.0)
        out = np.zeros(len(Q))
        for a in (0, 1):
            for b in (0, 1):
                for c in (0, 1):
                    w = (f[:, 0] if a else 1 - f[:, 0]) * (f[:, 1] if b else 1 - f[:, 1]) * (f[:, 2] if c else 1 - f[:, 2])
                    out += w * D[i[:, 0] + a, i[:, 1] + b, i[:, 2] + c]
        return out
    if value:
        return at(V)
    V = V.copy()
    for _ in range(passes):
        d = at(V)
        g = np.stack([(at(V + e) - at(V - e)) / voxel for e in np.eye(3) * (0.5 * voxel)], axis=1)
        step = d / np.maximum((g * g).sum(axis=1), 1e-6)
        V -= np.clip(step, -voxel, voxel)[:, None] * g
    return V


def cut_tube(V, T, c, n, reach, end, mine=None):
    """The grown skin without its stretch past the plane (c, along n) near the axis through c along n (within reach)
    or near the line on from c to the flesh's end (it runs on along the foot bone): the leg's flesh inside the foot.
    Faces crossing the plane are cut exactly at it, so the skin ends in a clean ring. New vertices are appended."""
    V = list(V)
    P = np.asarray(V)
    h = (P - c) @ n
    d = P - c
    e = np.asarray(end) - c
    te = np.clip(d @ e / max(float(e @ e), 1e-9), 0.0, 1.0)
    near = (np.linalg.norm(d - np.outer(h, n), axis=1) < reach) | (np.linalg.norm(d - np.outer(te, e), axis=1) < reach)
    if mine is not None:
        near = near & mine
    made, out = {}, []

    def on_edge(i, j):
        key = (min(i, j), max(i, j))
        if key not in made:
            t = h[i] / (h[i] - h[j])
            V.append(P[i] + t * (P[j] - P[i]))
            made[key] = len(V) - 1
        return made[key]
    for t in T:
        past = h[t] > 0
        if not past.any() or not near[t].any() or (mine is not None and not mine[t].all()):
            out.append(tuple(t))
            continue
        if past.all():
            continue
        poly = []
        for k in range(3):
            i, j = t[k], t[(k + 1) % 3]
            if h[i] <= 0:
                poly.append(i)
            if (h[i] <= 0) != (h[j] <= 0):
                poly.append(on_edge(i, j))
        for k in range(1, len(poly) - 1):
            out.append((poly[0], poly[k], poly[k + 1]))
    return np.array(V), np.array(out)


def slice_points(P, tris, c, n, offset):
    """Where a mesh's edges cross the plane offset along n from c."""
    h = (P - c) @ n - offset
    out = []
    for t in tris:
        for k in range(len(t)):
            i, j = t[k], t[(k + 1) % len(t)]
            if (h[i] > 0) != (h[j] > 0):
                out.append(P[i] + h[i] / (h[i] - h[j]) * (P[j] - P[i]))
    return np.array(out)


def polar(points, c, a, b, reach, bins=72):
    """A closed section's radius round c by angle (in the plane of a, b): (angle grid, radius), filled and smoothed.
    Only points within reach: a slice across the whole skin also crosses the belly and the other legs."""
    if not len(points):
        return None
    q = points - c
    ang = np.arctan2(q @ b, q @ a)
    rad = np.hypot(q @ a, q @ b)
    ang, rad = ang[rad < reach], rad[rad < reach]
    grid = np.linspace(-np.pi, np.pi, bins, endpoint=False)
    k = np.floor((ang + np.pi) / (2 * np.pi) * bins).astype(int) % bins
    R = np.full(bins, np.nan)
    for i in range(bins):
        if (k == i).any():
            R[i] = np.max(rad[k == i])
    ok = ~np.isnan(R)
    if ok.sum() < 6:
        return None
    R = np.interp(grid, np.concatenate([grid[ok] - 2 * np.pi, grid[ok], grid[ok] + 2 * np.pi]), np.tile(R[ok], 3))
    R = (np.roll(R, 1) + 2 * R + np.roll(R, -1)) / 4.0
    return grid, R


def at_angle(pol, ang):
    grid, R = pol
    return np.interp(ang, np.concatenate([grid - 2 * np.pi, grid, grid + 2 * np.pi]), np.tile(R, 3))


def stitch_feet(V, T, verts, parts, sweeps):
    """The grown leg ends exactly on its foot's ring, as one surface: the foot part (the hand-built lower leg, cut
    flat across the shin) is left as it is, the grown skin is cut at the same plane, and its last stretch (one
    radius) is bent round the leg, angle by angle, so that at the plane it lies on the foot's ring and leaves it at
    the foot's own slope. (Two surfaces meeting at a ring always showed a line, a band or a boot top where their
    widths or slopes differed.)"""
    UE = np.array([bfg.m_vec(bfg.UE_FROM_OBJ, v) for v in verts])
    chains = [w[0] for w in sweeps]

    def own_skin(ch, P, c, n, r):
        """Which of the points P are this limb's own: nearer its bones than any other chain's. On a narrow-hipped animal
        the next leg's shin is within reach of this leg's cut: cutting and bending it too tore a strip between the
        legs, and the other shin's width got into this foot's measurements (Raven saw the crack, 2026-11-06). The trunk
        counts too: a body tilted onto long front legs hangs its hind feet beside its tail, and their cuts bit holes in
        the tail (2026-11-08)."""
        out = np.ones(len(P), dtype=bool)
        h = (P - c) @ n
        close = np.where((np.abs(h) < 3.0 * r) & (np.linalg.norm(P - c - np.outer(h, n), axis=1) < 3.0 * r))[0]
        others = [o for o in chains if o is not ch]
        if len(close) and others:
            d = ch.project(P[close])[2]
            best = np.min([o.project(P[close])[2] for o in others], axis=0)
            out[close] = d <= best
        return out

    for ch, prof, zs, Yv in sweeps:
        plane = getattr(ch, "plane", None)
        if plane is None or not getattr(ch, "fitted", False):
            continue
        c0, n = plane
        if buried(ch, c0[None], sweeps)[0] < 0.0:
            # The ring lies inside the body's flesh (a Velociraptor's arms under a Brachiosaurus neck's base, their
            # wrists 4 cm in): there is no skin of this limb's own to end on it, and cutting there bit holes in the
            # neck. The skin stays whole and the hand pokes out of it like any bone part.
            continue
        want = ch.parts[0][:-2] + ("_foot" if ch.parts[0].startswith("leg") else "_hand") + ch.parts[0][-2:]
        tris = [t for name, ts in parts if part_base(name) == want for t in ts]
        if not tris:
            continue
        idx = np.array(sorted({i for t in tris for i in t}))
        ring = UE[idx][np.abs((UE[idx] - c0) @ n) < 0.05]
        if len(ring) < 8:
            continue                          # not cut flat (a Pteranodon's wing hand): left as it was
        c = ring.mean(axis=0)
        a = np.cross(n, [0.0, 0.0, 1.0]) if abs(n[2]) < 0.9 else np.cross(n, [1.0, 0.0, 0.0])
        a /= np.linalg.norm(a)
        b = np.cross(n, a)
        r = float(np.max(np.hypot((ring - c) @ a, (ring - c) @ b)))
        band, dl = 1.0 * r, 0.15 * r
        V, T = cut_tube(V, T, c, n, 1.6 * r, ch.points[-1], own_skin(ch, np.asarray(V), c, n, r))
        foot0 = polar(ring, c, a, b, 1.6 * r)
        foot1 = polar(slice_points(UE, tris, c, n, dl), c, a, b, 1.6 * r)
        P = np.asarray(V)
        h = (P - c) @ n
        q = P - c - np.outer(h, n)
        rad = np.hypot(q @ a, q @ b)
        on = np.zeros(len(P), dtype=bool)
        on[np.unique(T)] = True
        on &= own_skin(ch, P, c, n, r)
        g0 = polar(P[on & (np.abs(h) < 1e-3)], c, a, b, 1.6 * r)
        g1 = polar(slice_points(P, T[on[T].all(axis=1)], c, n, -dl), c, a, b, 1.6 * r)
        if foot0 is None or g0 is None:
            continue
        m = on & (h <= 1e-6) & (h > -band) & (rad < 1.6 * r)
        ang = np.arctan2(q[m] @ b, q[m] @ a)
        y = -h[m]                              # height above the plane
        shift = at_angle(foot0, ang) - at_angle(g0, ang)
        if foot1 is not None and g1 is not None:
            # the foot's slope at the ring (radius change per cm going up) against the grown leg's
            slope = (at_angle(foot0, ang) - at_angle(foot1, ang)) / dl - (at_angle(g1, ang) - at_angle(g0, ang)) / dl
            shift = shift + np.clip(slope, -1.5, 1.5) * y
        t = np.clip(y / band, 0.0, 1.0)
        w = 1.0 - t * t * (3 - 2 * t)
        # Fading out towards the reach: skin past it isn't bent, and a sudden stop would fold (behind an elbow).
        o = np.clip((rad[m] - 1.15 * r) / (0.45 * r), 0.0, 1.0)
        w = w * (1.0 - o * o * (3 - 2 * o))
        dirs = q[m] / np.maximum(rad[m], 1e-9)[:, None]
        P[m] += dirs * (shift * w)[:, None]
        # The bend above follows the ring through a smoothed outline, which rounds off a sharp tip (a thickened foot's
        # has one; the skin stopped 1.6 cm short of it, and the sky showed through). The skin's last row goes exactly
        # onto the ring, one of its vertices onto each of the ring's, and the rows just above ease back from there.
        h = (P - c) @ n
        q = P - c - np.outer(h, n)
        rad = np.hypot(q @ a, q @ b)
        edge = np.where(on & (np.abs(h) < 1e-3) & (rad < 1.6 * r))[0]
        rim = [e for e in bfg.open_edges(tris) if abs(float((UE[e[0]] - c) @ n)) < 0.05 and abs(float((UE[e[1]] - c) @ n)) < 0.05]
        if len(edge) >= 8 and rim:
            A, B = UE[[e[0] for e in rim]], UE[[e[1] for e in rim]]
            AB = B - A
            u = np.clip(np.einsum("nmk,mk->nm", P[edge][:, None, :] - A[None], AB) / np.maximum((AB * AB).sum(axis=1), 1e-9), 0.0, 1.0)
            F = A[None] + u[..., None] * AB[None]
            nearest = np.argmin(np.linalg.norm(P[edge][:, None, :] - F, axis=2), axis=1)
            target = F[np.arange(len(edge)), nearest]
            corners = UE[sorted({i for e in rim for i in e})]
            who = np.argmin(np.linalg.norm(corners[:, None, :] - P[edge][None], axis=2), axis=1)
            target[who] = corners
            move = target - P[edge]
            ang = np.arctan2(q @ b, q @ a)
            o = np.argsort(ang[edge])
            ea = ang[edge][o]
            full = 2 * np.pi
            near = np.where(on & (h <= 1e-6) & (h > -0.35 * r) & (rad < 1.6 * r))[0]
            t = np.clip(-h[near] / (0.35 * r), 0.0, 1.0)
            w = 1.0 - t * t * (3 - 2 * t)
            for k in range(3):
                P[near, k] += np.interp(ang[near], np.concatenate([ea - full, ea, ea + full]), np.tile(move[o, k], 3)) * w
            P[edge] = target                   # exactly, whatever the easing made of them
            # Bent angle by angle, from outlines measured in 72 steps, the band came out rippled (a mottled ring
            # above every grown ankle and wrist in close renders): relax it, most next to the ring and not at all by
            # the band's top, the ring's own row held where it is.
            band_i = np.where(on & (h < -1e-3) & (h > -1.2 * band) & (rad < 1.6 * r))[0]
            tt = np.clip(-h[band_i] / (1.2 * band), 0.0, 1.0)
            P = relax(P, T, band_i, 1.0 - tt * tt * (3 - 2 * tt), passes=12)
            P[edge] = target
        V = P
    used = np.unique(T)
    remap = -np.ones(len(V), dtype=np.int64)
    remap[used] = np.arange(len(used))
    return np.asarray(V)[used], remap[T]


def grow(rig_path, features_obj, out_obj, voxel=None):
    rig, J, topo = bfg.load_rig(rig_path)
    frames = bfg.bone_frames(J, topo)
    tissue = {j["name"]: j["tissue"] for j in rig["joints"] if j.get("tissue")}
    length = max(max(j.pos[k] for j in J.values()) - min(j.pos[k] for j in J.values()) for k in range(3))   # its biggest size
    # Each limb's half-thickness (cm, its median section), measured before it is fitted to its foot
    thin = {ch.name: float(np.median(np.minimum(prof[3], prof[4]))) for ch, prof, *_ in chain_sweeps(J, frames, tissue, None)
            if ch.name != "trunk"}
    if not voxel:
        voxel = max(length / 300.0, 0.4)
        # A splice made long by one part (a Brachiosaurus neck on a Velociraptor) grew its small limbs on voxels wider
        # than they are thick, and they came out lumpy with untidy seams: no coarser than LIMB_VOXELS across the
        # thinnest limb (the six have 1.9 or more, so they never change; MAX_CELLS still holds).
        if thin:
            voxel = max(min(voxel, min(thin.values()) / LIMB_VOXELS), 0.4)
    verts, parts = bfg.load_obj(features_obj)
    obj_from_ue = bfg.m_inv(bfg.UE_FROM_OBJ)
    feet = {}                                 # "leg_l": that limb's foot part vertices (Unreal cm)
    for name, tris in parts:
        b = part_base(name)
        for kind, limb in (("leg_foot", "leg"), ("arm_hand", "arm"), ("leg2_foot", "leg2"), ("arm2_hand", "arm2")):
            if named(b, (kind,)):
                idx = sorted({i for t in tris for i in t})
                feet.setdefault(limb + b[len(kind):len(kind) + 2], []).extend(bfg.m_vec(bfg.UE_FROM_OBJ, verts[i]) for i in idx)
    feet = {k: np.array(v) for k, v in feet.items()}
    D, lo, sweeps, voxel = grow_sdf(J, frames, tissue, voxel, feet)       # (coarser, for a far-oversized splice)
    V, T = surface_nets(D, lo, voxel)
    V = onto_surface(V, D, lo, voxel)
    # Limbs too thin for these voxels each grow on a finer grid of their own, meshed alone and set into the body's mesh
    # (patch_in); every vertex then follows its own grid's field.
    fields, fine_note = [], ""
    for blo, bhi, vf in fine_regions(sweeps, voxel, thin):
        pad = 2.0 * voxel
        glo = blo - pad
        Df = field_on(J, sweeps, glo, np.ceil((bhi + pad - glo) / vf).astype(int) + 1, vf, voxel)
        Vf, Tf = surface_nets(Df, glo, vf)
        Vf = onto_surface(Vf, Df, glo, vf)
        got = patch_in(V, T, np.full(len(V), -1) if not fields else label, Vf, Tf, len(fields), blo, bhi)
        if got:
            V, T, label = got
            fields.append((Df, glo, vf))
    if fields:
        fine_note = f"; small limbs on {', '.join(f'{f[2]:.2f}' for f in fields)} cm"

        def project(V, passes):
            V = V.copy()
            m = label < 0
            V[m] = onto_surface(V[m], D, lo, voxel, passes=passes)
            for ri, (Df, glo, vf) in enumerate(fields):
                m = label == ri
                V[m] = onto_surface(V[m], Df, glo, vf, passes=passes)
            return V
        V = smooth(V, T)
        V = project(V, 1)
    else:
        V = smooth(V, T)
        V = onto_surface(V, D, lo, voxel, passes=1)
    # The trilinear field's surface kinks a little at every voxel boundary, which shaded as faint bands round a
    # gentle slope (the T. rex's shins) and a stepped line along a crease (where a limb meets the trunk): smoothing
    # after the last projection takes them off. (The grown skin has no fine detail to lose: that is all in the bone parts.)
    V = smooth(V, T, passes=SMOOTH_AFTER)
    seated = seat_armour(verts, parts, D, lo, voxel)
    vol = np.einsum("ij,ij->i", V[T[:, 0]], np.cross(V[T[:, 1]], V[T[:, 2]])).sum() / 6.0
    if vol < 0:                               # faces must wind outward
        T = T[:, [0, 2, 1]]
    T = drop_pockets(V, T)
    V, T = stitch_feet(V, T, verts, parts, sweeps)
    V, creased = soften_creases(V, T)
    # Where the surface stretches (a big folded wing pulling past its neighbour), long faces would skin with a jump across them.
    V, T = weld(V, T)
    long_v = [tuple(float(c) for c in p) for p in V]
    long_t = subdivide(long_v, [tuple(int(i) for i in t) for t in T], 2.5 * voxel)
    V, T = np.array(long_v), np.array(long_t)
    with open(features_obj, encoding="utf-8") as f:
        header = [ln.rstrip("\n") for ln in f if ln.startswith("#")]
    lines = header + [f"# Body grown from the rig's tissue profiles by body_tools/grow_body.py ({voxel:.2f} cm "
                      f"voxels{fine_note}); bone parts kept as they were.", "o body_grown_0"]
    for v in V:
        o = bfg.m_vec(obj_from_ue, tuple(v))
        lines.append(f"v {o[0]:.2f} {o[1]:.2f} {o[2]:.2f}")
    for a, b, c in T:                         # OBJ axes mirror Unreal's: wind the other way round
        lines.append(f"f {a + 1} {c + 1} {b + 1}")
    count = len(V)
    for name, tris in parts:
        used = sorted({i for t in tris for i in t})
        remap = {}
        lines.append(f"o {name}")
        for i in used:
            count += 1
            remap[i] = count
            v = verts[i]
            lines.append(f"v {v[0]:.2f} {v[1]:.2f} {v[2]:.2f}")
        for t in tris:
            lines.append("f " + " ".join(str(remap[i]) for i in t))
    with open(out_obj, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    # Which chain of flesh each skin vertex grew from, for skinning (bone_forge_gltf.grown_weights): the skin's
    # vertices are the OBJ's first ones, in order.
    bones, share = chain_shares(J, frames, tissue, V, min_width=3.0 * voxel, feet=feet)
    with open(out_obj[:-4] + "_chains.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump({"note": "grow_body.py: per skin vertex (the OBJ's first vertices, in order), its [chain, share] "
                           "pairs; each chain's bones", "bones": bones,
                   "share": [[[c, round(float(v), 3)] for c, v in enumerate(row) if v >= 0.01] for row in share]},
                  f, separators=(",", ":"))
    print(f"grew {out_obj}: {len(V)} skin vertices, {len(T)} triangles, {len(parts)} bone parts; softened {creased} sharp edges"
          + (f"; seated {seated[0]} of {seated[1]} armour pieces on the skin" if seated[1] else ""))


ARMOUR = ("armor_plates", "armor_spikes", "armor")
ARMOUR_SUNK = (0.3, 0.75)      # how much of a plate (its depth through the skin) may be sunk in before it's moved ...
ARMOUR_SEAT = 0.45             # ... and how much it is sunk in once moved


def seat_armour(verts, parts, D, lo, voxel):
    """Armour pieces (an Ankylosaurus' plates and spikes) are bone parts kept where they sat on the hand-built body,
    so where the grown skin lies a little lower they stood off it, shadows under them, or a little higher, buried.
    Each piece (a connected bit of the armour parts) whose share sunk into the skin is outside ARMOUR_SUNK moves
    straight out from the skin or in until ARMOUR_SEAT of it is. verts (OBJ) changed in place; (moved, pieces)."""
    obj_from_ue = bfg.m_inv(bfg.UE_FROM_OBJ)
    moved = total = 0
    for name, tris in parts:
        if not named(part_base(name), ARMOUR):
            continue
        root = {}

        def find(a):
            root.setdefault(a, a)
            while root[a] != a:
                root[a] = root[root[a]]
                a = root[a]
            return a
        for a, b, c in tris:
            for x, y in ((a, b), (a, c)):
                rx, ry = find(x), find(y)
                if rx != ry:
                    root[rx] = ry
        comps = {}
        for i in {i for t in tris for i in t}:
            comps.setdefault(find(i), []).append(i)
        for comp in comps.values():
            total += 1
            P = np.array([bfg.m_vec(bfg.UE_FROM_OBJ, verts[i]) for i in comp])
            f = onto_surface(P, D, lo, voxel, value=True)
            depth = float(f.max() - f.min())
            if depth < 0.5 or f.max() > 1e5:
                continue
            sunk = -float(f.min()) / depth
            if ARMOUR_SUNK[0] <= sunk <= ARMOUR_SUNK[1]:
                continue
            c = P[np.argsort(f)[: max(3, len(P) // 4)]].mean(axis=0)          # round its deepest part
            h = 0.5 * voxel
            g = np.array([float(onto_surface(np.array([c + h * e]), D, lo, voxel, value=True)[0]
                                - onto_surface(np.array([c - h * e]), D, lo, voxel, value=True)[0]) for e in np.eye(3)])
            if np.linalg.norm(g) < 1e-6:
                continue
            g /= np.linalg.norm(g)                                              # out of the skin
            shift = float(f.min()) + ARMOUR_SEAT * depth                        # > 0: it stands off, sink it
            for i, p in zip(comp, P - g * shift):
                verts[i] = bfg.m_vec(obj_from_ue, tuple(p))
            moved += 1
    return moved, total


def close_cut(verts, tris, cut, normal):
    """Fill the open edges lying on a cut plane: a fan from the middle of each loop."""
    on = lambda i: abs(float(np.dot(np.array(bfg.m_vec(bfg.UE_FROM_OBJ, verts[i])) - cut, normal))) < 1e-2
    count = {}
    for t in tris:
        for k in range(len(t)):
            a, b = t[k], t[(k + 1) % len(t)]
            count[(a, b)] = count.get((a, b), 0) + 1
    # An edge used one way only, both ends on the plane: the cap runs it the other way.
    nxt = {}
    for (a, b) in count:
        if (b, a) not in count and on(a) and on(b):
            nxt.setdefault(b, []).append(a)
    out = list(tris)
    while nxt:
        start = next(iter(nxt))
        loop, cur = [start], start
        while cur in nxt:
            step = nxt[cur].pop()
            if not nxt[cur]:
                del nxt[cur]
            if step == start:
                break
            loop.append(step)
            cur = step
        if len(loop) < 3:
            continue
        mid = tuple(sum(verts[i][k] for i in loop) / len(loop) for k in range(3))
        verts.append(mid)
        m = len(verts) - 1
        out += [(loop[i], loop[(i + 1) % len(loop)], m) for i in range(len(loop))]
    return out


def drop_pockets(V, T):
    """Hollow pockets inside the flesh gone. Where a limb's flesh tapers inside itself the surface can close a little
    bubble in the leg, which nobody sees until the ankle cut opens it. A pocket's faces wind inward: its volume comes
    out the opposite way round to the body's."""
    V, T = np.asarray(V), np.asarray(T)
    parent = np.arange(len(V))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for a, b, c in T:
        ra, rb, rc = find(a), find(b), find(c)
        parent[rb] = ra
        parent[find(rc)] = ra
    roots = np.array([find(t[0]) for t in T])
    vol = np.einsum("ij,ij->i", V[T[:, 0]], np.cross(V[T[:, 1]], V[T[:, 2]]))
    ids, inv = np.unique(roots, return_inverse=True)
    per = np.zeros(len(ids))
    np.add.at(per, inv, vol)
    sign = np.sign(per[np.argmax(np.abs(per))])
    faces = np.bincount(inv)
    keep = (per * sign > 0) & (faces >= 0.002 * faces.max())     # and no loose slivers
    return T[keep[inv]]


def weld(V, T, eps=0.01):
    """Vertices closer than eps cm made one, and the faces that leaves with no area dropped. A cut through a vertex
    leaves such faces, and their undefined normals shade as a white tooth on smooth skin."""
    V, T = np.asarray(V), np.asarray(T)
    _, first, inv = np.unique(np.round(V / eps).astype(np.int64), axis=0, return_index=True, return_inverse=True)
    inv = inv.ravel()
    T = inv[T]
    V = V[first]
    A = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    keep = (T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 2] != T[:, 0]) & (np.linalg.norm(A, axis=1) > 1e-5)
    return V, T[keep]


def subdivide(verts, tris, longest):
    """Split every edge over longest at its middle until none is left (faces sharing an edge share its new vertex)."""
    tris = [tuple(t) for t in tris if len(t) == 3]
    for _ in range(8):
        made, out, split = {}, [], False

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in made:
                verts.append(tuple(0.5 * (verts[a][k] + verts[b][k]) for k in range(3)))
                made[key] = len(verts) - 1
            return made[key]
        far = lambda a, b: sum((verts[a][k] - verts[b][k]) ** 2 for k in range(3)) > longest * longest
        for a, b, c in tris:
            ab, bc, ca = far(a, b), far(b, c), far(c, a)
            n = ab + bc + ca
            if n == 0:
                out.append((a, b, c))
                continue
            split = True
            if n == 3:
                x, y, z = mid(a, b), mid(b, c), mid(c, a)
                out += [(a, x, z), (x, b, y), (z, y, c), (x, y, z)]
            elif n == 2:
                if not bc:              # turned so that a-b is the edge left whole
                    a, b, c = b, c, a
                elif not ca:
                    a, b, c = c, a, b
                x, y = mid(b, c), mid(c, a)
                out += [(c, y, x), (a, b, x), (a, x, y)]
            else:
                if bc:
                    a, b, c = b, c, a
                elif ca:
                    a, b, c = c, a, b
                x = mid(a, b)
                out += [(a, x, c), (x, b, c)]
        tris = out
        if not split:
            break
    return tris


def clip_below(verts, tris, cut, normal, axis_from, reach):
    """The faces of a mesh on the far side of a plane (along normal from cut), each face crossing the plane cut
    exactly at it, so the part's new edge is one clean flat ring (whole faces left a jagged edge, and every way of
    meeting a jagged edge showed as ragged). Near the cut, only flesh within reach of the axis through axis_from
    along normal (the shin, not a thigh beside it). New vertices are appended to verts (OBJ coordinates); one per cut edge, so
    neighbouring faces share it."""
    to_ue = lambda i: np.array(bfg.m_vec(bfg.UE_FROM_OBJ, verts[i]))
    h, near = {}, {}
    for t in tris:
        for i in t:
            if i not in h:
                p = to_ue(i)
                h[i] = float(np.dot(p - cut, normal))
                d = p - axis_from
                # The reach applies near the cut only: further down the foot runs forward into its toes.
                near[i] = float(np.linalg.norm(d - normal * np.dot(d, normal))) < reach or h[i] > 0.5 * reach
    made = {}

    def on_edge(i, j):
        key = (min(i, j), max(i, j))
        if key not in made:
            t = h[i] / (h[i] - h[j])
            verts.append(tuple(verts[i][k] + t * (verts[j][k] - verts[i][k]) for k in range(3)))
            made[key] = len(verts) - 1
        return made[key]
    out = []
    for t in tris:
        if not all(near[i] for i in t):
            continue
        inside = [h[i] >= 0 for i in t]
        if all(inside):
            out.append(t)
            continue
        if not any(inside):
            continue
        poly = []
        for k in range(len(t)):
            i, j = t[k], t[(k + 1) % len(t)]
            if h[i] >= 0:
                poly.append(i)
            if (h[i] >= 0) != (h[j] >= 0):
                poly.append(on_edge(i, j))
        for k in range(1, len(poly) - 1):
            out.append((poly[0], poly[k], poly[k + 1]))
    return out


def features(key, out_path):
    """The bone parts of a hand-built body: every part that isn't grown, plus each grown limb's foot or hand, cut
    from the hand-built leg by a flat plane across the shin (FOOT_CUT of the way from knee to ankle, square to the
    shin): the hand-built ankle and foot below it, a clean ring on a plain tube above it for the grown leg to meet.
    (Cut where the flesh is nearer the foot bone, the edge ran round the ankle at a slant and through pads and
    heels, and no grown leg met it cleanly.) A limb whose next bone folds back (a Pteranodon's wing finger) keeps
    that older cut: the faces whose corners all lie nearest bones from the wrist down."""
    rig, J, topo = bfg.load_rig(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"))
    src = os.path.join(ROOT, "body_shapes", f"{key}_body.obj")
    verts, parts = bfg.load_obj(src)
    with open(src, encoding="utf-8") as f:
        header = [ln.rstrip("\n") for ln in f if ln.startswith("#")]
    distal = distal_bones(J)
    live = [n for n in topo if J[n].deforming and not bfg.is_stub(J[n], J)]
    keep = []
    for name, tris in parts:
        if not is_grown(name):
            keep.append((name, tris))
            continue
        base = part_base(name)
        if not named(base, ("arm", "leg")):
            continue
        side = base[-2:]
        sec = "Hip" if named(base, ("leg",)) else "Shoulder"
        bones = [n for n in live if J[n].section == sec and n.endswith(side)]
        top = next((n for n in J if J[n].section == sec and n.endswith(side) and J[n].parent
                    and J[J[n].parent].section == "Core"), None)
        shin, ankle = top, None
        while shin and J[shin].deforming and shin not in distal:
            mc = bfg.main_child(J[shin])
            if mc is None or mc in distal or not J[mc].deforming:
                ankle = mc
                break
            shin = mc
        nxt = bfg.main_child(J[ankle]) if ankle else None
        if shin and ankle and nxt and np.dot(np.subtract(J[ankle].pos, J[shin].pos),
                                             np.subtract(J[nxt].pos, J[ankle].pos)) > 0:
            k, a = np.array(J[shin].pos), np.subtract(J[ankle].pos, J[shin].pos)
            sub = clip_below(verts, tris, k + FOOT_CUT * a, a / np.linalg.norm(a), k, 0.75 * float(np.linalg.norm(a)))
            if sub:
                keep.append((("leg_foot" if sec == "Hip" else "arm_hand") + side + "_" + name.split("_")[-1], sub))
            continue
        if shin and ankle and nxt:
            # A wing: the finger folds back up along the hand. Cut exactly on the plane halfway between the two bones
            # (the nearest-bone test below, by whole faces, left a sawtooth edge), and keep the finger's side.
            k = np.array(J[ankle].pos)
            u = np.subtract(J[shin].pos, k)
            v = np.subtract(J[nxt].pos, k)
            n = v / np.linalg.norm(v) - u / np.linalg.norm(u)
            n = n / np.linalg.norm(n)
            sub = clip_below(verts, tris, k, n, k, 1e9)
            sub = [t for t in sub if float(np.dot(np.array(bfg.m_vec(bfg.UE_FROM_OBJ, verts[t[0]])) - k, v)) > -0.5 * float(np.dot(v, v))]
            if sub:
                # Lengthwise, the cut leaves half a tube: closed, or the spread wing shows it hollow. Finely meshed, as
                # its faces were long ones along the finger, and the wing bends across them.
                sub = close_cut(verts, sub, k, n)
                sub = subdivide(verts, sub, 0.04 * float(np.linalg.norm(v)))
                keep.append((("leg_foot" if sec == "Hip" else "arm_hand") + side + "_" + name.split("_")[-1], sub))
            continue
        on = {}
        for t in tris:
            for i in t:
                if i in on:
                    continue
                p = np.array(bfg.m_vec(bfg.UE_FROM_OBJ, verts[i]))
                best, bd = None, float("inf")
                for n in bones:
                    a = np.array(J[n].pos)
                    mc = bfg.main_child(J[n])
                    e = np.array(J[mc].pos) if mc else a + 0.5 * (a - np.array(J[J[n].parent].pos))
                    ab = e - a
                    tt = min(max(float(np.dot(p - a, ab) / max(np.dot(ab, ab), 1e-9)), 0.0), 1.0)
                    dd = float(np.linalg.norm(p - a - ab * tt))
                    if dd < bd:
                        best, bd = n, dd
                on[i] = best in distal
        sub = [t for t in tris if all(on[i] for i in t)]
        if sub:
            keep.append((("leg_foot" if sec == "Hip" else "arm_hand") + side + "_" + name.split("_")[-1], sub))
    lines = header[:]
    count = 0
    for name, tris in keep:
        used = sorted({i for t in tris for i in t})
        remap = {}
        lines.append(f"o {name}")
        for i in used:
            count += 1
            remap[i] = count
            v = verts[i]
            lines.append(f"v {v[0]:.2f} {v[1]:.2f} {v[2]:.2f}")
        for t in tris:
            lines.append("f " + " ".join(str(remap[i]) for i in t))
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return out_path


def main(argv):
    if argv and argv[0] == "measure":
        for key in argv[1:] or SPECIES:
            measure(key)
        return
    if argv and argv[0] == "grow":
        grow(argv[1], argv[2], argv[3])
        return
    for key in argv or SPECIES:
        measure(key)
        feats = features(key, os.path.join(ROOT, "body_shapes", f"{key}_features.obj"))
        grow(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), feats,
             os.path.join(ROOT, "body_shapes", f"{key}_grown.obj"))


if __name__ == "__main__":
    main(sys.argv[1:])
