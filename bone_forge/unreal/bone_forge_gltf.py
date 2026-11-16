"""Bone Forge rig importer, part 1: rig JSON + body OBJ -> skinned glTF binary (.glb).

Unreal only creates a Skeleton asset when it imports a skinned mesh, so this
script bakes the rig into one. Every joint in the rig JSON becomes a bone, and
the body-volume OBJ is bound to those bones with simple starter weights.
Unreal's glTF importer then creates the Skeletal Mesh, Skeleton and Physics Asset.

Standard-library Python 3.9+ only, so it runs from a normal shell and inside
Unreal's built-in Python (bone_forge_unreal.py calls it).

    python bone_forge_gltf.py RIG_JSON BODY_OBJ OUT_GLB

Axes
  Rig JSON / Unreal: cm, X forward, Y right, Z up (left-handed).
  Body OBJ:          cm, X forward, Y up, Z right (right-handed), i.e. Unreal (X, Z, Y).
  glTF:              metres, right-handed, Y up.
  Unreal's glTF importer is expected to map glTF (x, y, z) m to Unreal (x, z, y) * 100 cm,
  a swap of Y and Z. That is UE_FROM_GLTF below. bone_forge_unreal.py measures what
  the importer really did and rebuilds with a corrected matrix if it differs.

Bone orientation
  Each bone's local X axis points at its main child joint, and its local Y axis stays
  as close as possible to the creature's right (+Y). So for every bone:
    bend = rotation about local Y, side = about local Z, twist = about local X (the bone).
  End joints (head_end, toe_end_l, ...) copy their parent's orientation.

Skin weights (starter weights for checking the rig, not final weights)
  Each OBJ part (body, head, leg_l, ...) only uses the bones of its own body section(s),
  plus the bone it hangs from (pelvis for a leg, chest for an arm, the last neck bone for
  the head). Frill, horns and crest stay rigid on the head. Within that set a bone's weight
  falls off with the vertex's distance to the bone (1/d^8), then the weights are averaged
  with each vertex's neighbours a few times (never across the mouth line, which stays a
  clean split between skull and jaw); at most 4 bones per vertex, as the Look and Model Spec asks.
  Joints with no motion limits (root and the *_end markers) get no weight.
"""

import array
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bone_forge_skin as skin  # noqa: E402

# glTF metres -> Unreal cm, swapping Y and Z (rows of a 3x3 matrix).
UE_FROM_GLTF = ((100.0, 0.0, 0.0),
                (0.0, 0.0, 100.0),
                (0.0, 100.0, 0.0))

# Body OBJ (x fwd, y up, z right) -> Unreal (X fwd, Y right, Z up).
UE_FROM_OBJ = ((1.0, 0.0, 0.0),
               (0.0, 0.0, 1.0),
               (0.0, 1.0, 0.0))

# Skin weight = (1 / distance to the bone) ** FALLOFF_POWER, normalised. Higher = sharper joints.
FALLOFF_POWER = 8
FALLOFF_EPS_CM = 1.0
MAX_INFLUENCES = 4      # Look and Model Spec: max 4 bone influences per vertex
MIN_WEIGHT = 0.01       # smaller weights are dropped and the rest renormalised
SMOOTH_PASSES = 4       # neighbour-averaging passes over the weights (not on the head: keeps the mouth line sharp)

MATERIAL_COLOURS = {"Skin": (0.42, 0.40, 0.33, 1.0), "Membrane": (0.55, 0.45, 0.38, 1.0),
                    "Teeth": (0.85, 0.80, 0.66, 1.0), "Mouth": (0.62, 0.28, 0.28, 1.0),
                    "Keratin": (0.80, 0.74, 0.60, 1.0), "Scute": (0.80, 0.74, 0.60, 1.0), "Eye": (0.10, 0.08, 0.05, 1.0),
                    "LidHalf": (0.42, 0.40, 0.33, 1.0), "LidClosed": (0.42, 0.40, 0.33, 1.0)}


class _JointMap(dict):
    """The joints dict, plus flags about the body being skinned (has_jaw_part)."""

IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


# ---------------------------------------------------------------- vector maths
def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def v_len(a):
    return math.sqrt(v_dot(a, a))


def v_norm(a):
    n = v_len(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-12 else (0.0, 0.0, 0.0)


def m_vec(m, v):
    return (v_dot(m[0], v), v_dot(m[1], v), v_dot(m[2], v))


def m_mul(a, b):
    return tuple(tuple(sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)) for r in range(3))


def m_t(m):
    return tuple(tuple(m[c][r] for c in range(3)) for r in range(3))


def m_det(m):
    return v_dot(m[0], v_cross(m[1], m[2]))


def m_inv(m):
    d = m_det(m)
    if abs(d) < 1e-12:
        raise ValueError("axis matrix is not invertible")
    c0, c1, c2 = v_cross(m[1], m[2]), v_cross(m[2], m[0]), v_cross(m[0], m[1])
    return tuple(tuple(col[r] / d for col in (c0, c1, c2)) for r in range(3))


def m_from_cols(x, y, z):
    return ((x[0], y[0], z[0]), (x[1], y[1], z[1]), (x[2], y[2], z[2]))


def m_orthonormal_part(m):
    """Strip a uniform scale from a signed-permutation-like matrix."""
    s = abs(m_det(m)) ** (1.0 / 3.0)
    return tuple(tuple(c / s for c in row) for row in m)


def quat_from_matrix(m):
    """Unit quaternion (x, y, z, w) of a proper rotation matrix."""
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
    n = math.sqrt(sum(c * c for c in q))
    q = tuple(c / n for c in q)
    return q if q[3] >= 0 else tuple(-c for c in q)


# ---------------------------------------------------------------- rig
class Joint:
    __slots__ = ("name", "parent", "pos", "deforming", "children", "section", "info")


def load_rig(path):
    """Return (rig dict, {name: Joint}, names parents-first)."""
    with open(path, encoding="utf-8") as f:
        rig = json.load(f)
    joints, order = {}, []
    for j in rig["joints"]:
        name = j["name"]
        if name in joints:
            raise ValueError(f"{path}: joint '{name}' appears twice")
        jt = Joint()
        jt.name, jt.parent = name, j.get("parent")
        jt.pos = tuple(float(c) for c in j["position_cm"])
        jt.deforming = bool(j.get("limits_deg"))  # root and *_end markers have no limits
        jt.section = j.get("section", "-")
        jt.children, jt.info = [], j
        joints[name] = jt
        order.append(name)
    roots = [n for n in order if joints[n].parent is None]
    if len(roots) != 1:
        raise ValueError(f"{path}: expected one root joint, found {roots}")
    for n in order:
        p = joints[n].parent
        if p is not None:
            if p not in joints:
                raise ValueError(f"{path}: joint '{n}' has unknown parent '{p}'")
            joints[p].children.append(n)
    topo, stack = [], [roots[0]]
    while stack:  # depth-first, keeping the file's child order
        n = stack.pop()
        topo.append(n)
        stack.extend(reversed(joints[n].children))
    if len(topo) != len(order):
        raise ValueError(f"{path}: joints not connected to the root: {sorted(set(order) - set(topo))}")
    return rig, joints, topo


def main_child(j):
    """The child a bone points at: '<name>_end' if present, else the first child in the file."""
    if j.name + "_end" in j.children:
        return j.name + "_end"
    return j.children[0] if j.children else None


def frame_from_x(x):
    """Rotation (columns = local X, Y, Z) with X along the bone and Y as close to +Y (right) as possible."""
    right, up = (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
    if abs(v_dot(x, right)) < 0.95:
        y = v_norm(v_sub(right, v_scale(x, v_dot(x, right))))
        z = v_cross(x, y)
    else:  # bone points sideways: keep Z as close to up as possible instead
        z = v_norm(v_sub(up, v_scale(x, v_dot(x, up))))
        y = v_cross(z, x)
    return m_from_cols(x, y, z)


def is_stub(j, joints):
    """A leaf bone sitting almost on its parent joint (the raptor's sickle claw is 1 cm from ball_l).

    Its direction from the parent is meaningless, so it copies the parent's orientation
    and gets no skin weights; it stays in the skeleton for a claw mesh added later.
    """
    if not j.deforming or j.children or j.parent is None or joints[j.parent].parent is None:
        return False
    p = joints[j.parent]
    parent_len = v_len(v_sub(p.pos, joints[p.parent].pos))
    return v_len(v_sub(j.pos, p.pos)) < 0.25 * parent_len


def bend_pitch_sign(name):
    """Unreal local Pitch that matches the rig's '+bend' (same rule as source/body/pose.py).

    + Pitch tilts a bone's X axis toward its local Z (nose up / swing forward). '+bend' raises
    the tail, bends the knee, flexes the wrist and opens the jaw, which is the other way for
    the tail, calf, hand and jaw bones.
    """
    base = name[:-2] if name.endswith(("_l", "_r")) else name
    base = base[:-2] if base.endswith("_2") else base      # a third pair of limbs (splice/compose.py)
    return -1 if base.startswith("tail") or base in ("calf", "hand", "jaw") else 1


def side_yaw_sign(name, frame):
    """Unreal local Yaw that matches the rig's '+side', or None if the bone's axes don't allow it.

    + Yaw turns a bone's X axis toward its local Y (the creature's right).
    Limbs (_l/_r): '+side' moves the limb away from the body (rig README), so left -1, right +1.
    Midline bones: '+side' turns the nose to the creature's left and the tail tip to its right
    (source/body/pose.py), which flips with the bone's local Z (up for the neck, down for the tail).
    """
    if name.endswith("_l"):
        return -1
    if name.endswith("_r"):
        return 1
    z_up = frame[2][2]  # local Z . world Z
    return -1 if z_up > 0.7 else 1 if z_up < -0.7 else None


def bone_frames(joints, topo):
    """World rotation of every bone in Unreal axes."""
    frames = {}
    for n in topo:
        j = joints[n]
        if j.parent is None:
            frames[n] = IDENTITY
            continue
        mc = main_child(j)
        if mc is not None:
            x = v_norm(v_sub(joints[mc].pos, j.pos))
        elif j.deforming and not is_stub(j, joints):  # last tail bone, tail club: carry on the parent's direction
            x = v_norm(v_sub(j.pos, joints[j.parent].pos))
        else:
            x = (0.0, 0.0, 0.0)
        frames[n] = frame_from_x(x) if v_len(x) > 0.5 else frames[j.parent]
    return frames


# ---------------------------------------------------------------- body OBJ
def load_obj(path):
    """Return (vertices in OBJ axes, [(part name, [triangles])])."""
    verts, parts = [], []
    current = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "v":
                verts.append((float(p[1]), float(p[2]), float(p[3])))
            elif p[0] in ("o", "g"):
                current = (p[1] if len(p) > 1 else f"part_{len(parts)}", [])
                parts.append(current)
            elif p[0] == "f":
                if current is None:
                    current = ("body_0", [])
                    parts.append(current)
                idx = []
                for tok in p[1:]:
                    i = int(tok.split("/")[0])
                    idx.append(i - 1 if i > 0 else len(verts) + i)
                for k in range(1, len(idx) - 1):  # fan-triangulate quads and n-gons
                    current[1].append((idx[0], idx[k], idx[k + 1]))
    return verts, [pt for pt in parts if pt[1]]


def part_bones(part, centroid, joints, extra_head=False):
    """(bones, rigid): the deforming bones a body part may be weighted to, from its OBJ object name.

    rigid parts (frill, horns, crest) don't blend into neighbouring bones. extra_head marks a
    second 'head' part (the Pteranodon crest), which is rigid on the head bone.
    """
    pieces = part.split("_")
    base = "_".join(pieces[:-1]) if len(pieces) > 1 and pieces[-1].isdigit() else part
    if base.endswith("_l") or base.endswith("_r"):
        side = base[-2:]
    else:
        side = "_l" if centroid[1] < 0 else "_r"

    def pick(sections, sided=False):
        return [n for n, j in joints.items()
                if j.deforming and not is_stub(j, joints) and j.section in sections
                and (not sided or n.endswith(side))]

    rigid = False
    has_jaw_part = getattr(joints, "has_jaw_part", False)
    if base.startswith("body_grown"):   # grown flesh (body_tools/grow_body.py): trunk and limbs as one skin
        own = [n for n, j in joints.items() if j.deforming and not is_stub(j, joints) and n != "jaw"]
    elif base == "body" or base.startswith("armor"):
        own = pick({"Core", "Neck", "Tail"})
    elif base.startswith(("jaw", "tongue", "teeth_lower")):
        own, rigid = [n for n in ("jaw",) if n in joints and joints[n].deforming], True
    elif base.startswith("teeth"):
        own, rigid = [n for n in ("head",) if n in joints and joints[n].deforming], True
    elif base.startswith("head") and not extra_head:
        # With a separate jaw part the skull keeps none of the jaw's weight (the mouth opens, nothing stretches).
        own = [n for n in pick({"Head"}) if not (has_jaw_part and n == "jaw")]
    elif base.startswith("neck"):
        # parts_builder's neck_blend: from the neck into the back of the skull and the throat, never the jaw.
        own = pick({"Neck"}) + [n for n in pick({"Head"}) if n != "jaw"]
    elif base.startswith("leg"):
        own = [n for n in pick({"Hip"}, sided=True) if ("_2_" in n) == base.startswith("leg2")]
    elif base.startswith(("arm", "wing")):
        own = [n for n in pick({"Shoulder"}, sided=True) if ("_2_" in n) == base.startswith(("arm2", "wing2"))]
    elif base.startswith("membrane"):
        own = pick({"Shoulder", "Hip"}, sided=True) + pick({"Core"})
    elif base.startswith("tail"):
        own = pick({"Tail"})
    elif base.startswith(("frill", "horn", "crest", "eye", "brow", "nostril")) or (base.startswith("head") and extra_head):
        own, rigid = [n for n in ("head",) if n in joints and joints[n].deforming], True
    else:
        own = []
    return (own or [n for n, j in joints.items() if j.deforming and not is_stub(j, joints)]), rigid


def bone_segments(name, own, joints):
    """Line segments that stand for a bone: joint -> each child in the same part (or its main child)."""
    j = joints[name]
    mc = main_child(j)
    segs = []
    for c in j.children:
        cj = joints[c]
        if (c in own or not cj.deforming or c == mc) and v_len(v_sub(cj.pos, j.pos)) > 1e-6:
            segs.append((j.pos, cj.pos, c if cj.deforming else None))
    if not segs:  # leaf bone: extend half its parent's length past the joint
        end = v_add(j.pos, v_scale(v_sub(j.pos, joints[j.parent].pos), 0.5))
        segs.append((j.pos, end, None))
    return segs


def skin_part(points, own, joints, rigid=False):
    """Starter weights for one part: list of {bone: weight} per point (Unreal cm)."""
    cands = list(own)
    if not rigid:  # also blend into the bone each part hangs from and the bone it leads into
        for b in own:
            for e in (joints[b].parent, main_child(joints[b])):
                if e and e not in cands and joints[e].deforming and not is_stub(joints[e], joints):
                    cands.append(e)
    segs = []
    for b in cands:
        for a, e, _ in bone_segments(b, own, joints):
            ab = v_sub(e, a)
            segs.append((b, a, ab, v_dot(ab, ab)))
    out = []
    for p in points:
        dist = {}
        for b, a, ab, ab2 in segs:
            ap = v_sub(p, a)
            t = v_dot(ap, ab) / ab2
            t = 0.0 if t < 0 else 1.0 if t > 1 else t
            d = v_len(v_sub(ap, v_scale(ab, t)))
            if d < dist.get(b, float("inf")):
                dist[b] = d
        near = min(dist.values()) + FALLOFF_EPS_CM
        w = {b: (near / (d + FALLOFF_EPS_CM)) ** FALLOFF_POWER for b, d in dist.items()}
        out.append(_limit(w))
    return out


def grown_weights(used, points, chains_path, joints):
    """Starter weights for a grown body (body_tools/grow_body.py writes <body>_chains.json beside it): each skin
    vertex is weighted within the chain of flesh it grew from (the trunk to the back, hips, tail and neck; a leg to
    that leg), shared where chains blend at a fillet. Weighting the one skin to every bone at once put a belly
    vertex on the legs and its neighbour on the arms (the four strongest of many far, nearly equal bones)."""
    with open(chains_path, encoding="utf-8") as f:
        data = json.load(f)
    share = data["share"]
    per_chain = []
    for c, own in enumerate(data["bones"]):
        own = [b for b in own if b in joints and joints[b].deforming and not is_stub(joints[b], joints)]
        sel = [k for k, i in enumerate(used) if i < len(share) and any(cc == c for cc, _ in share[i])]
        per_chain.append(dict(zip(sel, skin_part([points[k] for k in sel], own, joints))) if sel and own else {})
    out = []
    for k, i in enumerate(used):
        acc = {}
        for c, s in (share[i] if i < len(share) else []):
            for b, v in per_chain[c].get(k, {}).items():
                acc[b] = acc.get(b, 0.0) + s * v
        out.append(_limit(acc) if acc else skin_part([points[k]], list(joints_deforming(joints)), joints)[0])
    return out


def joints_deforming(joints):
    return (n for n, j in joints.items() if j.deforming and not is_stub(j, joints) and n != "jaw")


def _limit(w):
    """Keep the strongest MAX_INFLUENCES weights, drop tiny ones, sum to 1."""
    top = sorted(w.items(), key=lambda kv: -kv[1])[:MAX_INFLUENCES]
    total = sum(v for _, v in top)
    top = [(b, v / total) for b, v in top if v / total >= MIN_WEIGHT]
    total = sum(v for _, v in top)
    return {b: v / total for b, v in top}


def open_edges(tris):
    """The edges of a mesh that only one face uses."""
    count = {}
    for t in tris:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            key = (min(a, b), max(a, b))
            count[key] = count.get(key, 0) + 1
    return [key for key, c in count.items() if c == 1]


def relax_from(wt, tris, fixed, depth=6, passes=12):
    """Ease weights out from the vertices in fixed, which keep theirs, over depth steps of the mesh (wt changed in place)."""
    nbrs = {}
    for t in tris:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            nbrs.setdefault(a, set()).add(b)
            nbrs.setdefault(b, set()).add(a)
    layer, seen = set(fixed), set(fixed)
    band = []
    for _ in range(depth):
        layer = {n for v in layer for n in nbrs.get(v, ())} - seen
        seen |= layer
        band.extend(layer)
    for _ in range(passes):
        new = {}
        for v in band:
            acc = {b: 0.5 * x for b, x in wt[v].items()}
            share = 0.5 / len(nbrs[v])
            for n in nbrs[v]:
                for b, x in wt[n].items():
                    acc[b] = acc.get(b, 0.0) + share * x
            new[v] = acc
        wt.update(new)
    for v in band:
        wt[v] = _limit(wt[v])


def feather_uv(parts, verts_ue, jpos):
    """Texture coordinates for the feathers of a folded feathered wing (parts_builder.wing_feathers: separate flat
    blobs along the forearm and hand), which the skin material bends and flutters (bone_forge_materials.py): U how
    far along its feather a vertex is, root 0 to tip 1, and V where that feather's root sits along the wing, elbow 0
    to finger tip 1. Every other vertex keeps (0, 0), which the material leaves alone. {OBJ vertex: (u, v)}"""
    out = {}
    for pname, tris in parts:
        if not pname.startswith("wing_feathers"):
            continue
        base = pname.rsplit("_", 1)[0] if pname.rsplit("_", 1)[-1].isdigit() else pname     # wing_feathers_l_21
        side = base.rsplit("_", 1)[-1]
        line =[jpos[n] for n in (f"lowerarm_{side}", f"hand_{side}", f"finger_end_{side}") if n in jpos]

        def along(p):
            """Distance from the arm's line, and how far along it (0..1 by length) the nearest point is."""
            best, at, run = 1e18, 0.0, 0.0
            total = sum(v_len(v_sub(b, a)) for a, b in zip(line, line[1:])) or 1.0
            for a, b in zip(line, line[1:]):
                ab = v_sub(b, a)
                L2 = max(v_dot(ab, ab), 1e-9)
                t = max(0.0, min(1.0, v_dot(v_sub(p, a), ab) / L2))
                d = v_len(v_sub(p, v_add(a, v_scale(ab, t))))
                if d < best:
                    best, at = d, (run + t * math.sqrt(L2)) / total
                run += math.sqrt(L2)
            return best, at

        if len(line) < 2:
            line = []
        root = {}

        def find(a):      # each feather is a closed blob of its own: its connected vertices
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
            pts = [verts_ue[i] for i in comp]
            c = v_scale(tuple(map(sum, zip(*pts))), 1.0 / len(pts))
            # Its length: the main axis of its points (power iteration on their spread).
            axis = (1.0, 0.3, -0.5)
            for _ in range(20):
                acc = (0.0, 0.0, 0.0)
                for p in pts:
                    d = v_sub(p, c)
                    acc = v_add(acc, v_scale(d, v_dot(d, axis)))
                axis = v_norm(acc) if v_len(acc) > 1e-12 else axis
            proj = [v_dot(v_sub(p, c), axis) for p in pts]
            lo, hi = min(proj), max(proj)
            # The root is the end nearer the arm (without arm joints, the upper front end: they trail back and down).
            e_lo, e_hi = v_add(c, v_scale(axis, lo)), v_add(c, v_scale(axis, hi))
            if line:
                flip = along(e_hi)[0] < along(e_lo)[0]
            else:
                flip = v_dot(e_hi, (1.0, 0.0, 0.5)) > v_dot(e_lo, (1.0, 0.0, 0.5))
            if flip:
                proj, lo, hi, e_lo = [-x for x in proj], -hi, -lo, e_hi
            span = max(hi - lo, 1e-6)
            v = along(e_lo)[1] if line else 0.0
            for i, x in zip(comp, proj):
                out[i] = ((x - lo) / span, v)
    return out


def share_seams(all_weights, verts_ue, parts, tol=2.0):
    """Skin and foot held together. A grown skin ends on its foot part's ring: two meshes that only touch, each with its
    own weights, so they pulled apart as a leg moved (Raven saw this, 2026-11-06). Each vertex on the skin's open edge
    takes its weights from the point of the foot's ring beside it, in proportion along that edge of the ring, so under
    any pose it stays on that edge; the skin eases back to its own weights over the next few rows.
    all_weights: {part: {vertex: {bone: weight}}}, changed in place."""
    tris_of = dict(parts)
    skin = next((p for p, _ in parts if p.startswith("body_grown")), None)
    if skin is None:
        return
    rings = [(p, e) for p, tris in parts if p.startswith(("leg_foot", "arm_hand", "leg2_foot", "arm2_hand")) for e in open_edges(tris)]
    if not rings:
        return
    fixed = set()
    for i in {v for e in open_edges(tris_of[skin]) for v in e}:
        q = verts_ue[i]
        best, best_d = None, tol
        for p, (a, b) in rings:
            A, ab = verts_ue[a], v_sub(verts_ue[b], verts_ue[a])
            t = max(0.0, min(1.0, v_dot(v_sub(q, A), ab) / max(v_dot(ab, ab), 1e-9)))
            d = v_len(v_sub(v_add(A, v_scale(ab, t)), q))
            if d < best_d:
                best, best_d = (p, a, b, t), d
        if best:
            p, a, b, t = best
            acc = {}
            for bone, x in all_weights[p][a].items():
                acc[bone] = acc.get(bone, 0.0) + (1.0 - t) * x
            for bone, x in all_weights[p][b].items():
                acc[bone] = acc.get(bone, 0.0) + t * x
            all_weights[skin][i] = _limit(acc)
            fixed.add(i)
    if fixed:
        relax_from(all_weights[skin], tris_of[skin], fixed)


def seam_normals(groups, parts, verts_ue, tol=2.0):
    """Shading across a grown skin's stitched seams. The skin's edge and its foot's (or hand's) ring touch, but each
    vertex's normal comes from its own mesh's faces, a few degrees apart (8-10 for the worst tenth), which drew a
    faint line round every ankle and wrist and a step in the belly paint (which follows the normal); and where the
    stitch bent the skin's last faces onto the ring they can fold (the T. rex's right ankle: two faces 70 degrees
    apart), so the edge's own normals can't be trusted. The skin's edge takes the foot's (or hand's) normal beside
    it, the next row of the skin halfway between its own and the edge's, and the foot keeps its own: the shading eases
    across two rows instead of stepping. groups: the material groups, normals changed in place."""
    tris_of = dict(parts)
    skin = next((p for p, _ in parts if p.startswith("body_grown")), None)
    if skin is None:
        return 0
    rings = [e for p, tris in parts if p.startswith(("leg_foot", "arm_hand", "leg2_foot", "arm2_hand")) for e in open_edges(tris)]
    edge = open_edges(tris_of[skin])
    if not rings or not edge:
        return 0
    where = {}            # OBJ vertex -> (group, index in it)
    for g in groups.values():
        for i, k in g["remap"].items():
            where.setdefault(i, (g, k))

    def normal(i):
        g, k = where[i]
        return g["n"][k]

    def across(q, segs):
        best, best_d = None, tol
        for a, b in segs:
            A, ab = verts_ue[a], v_sub(verts_ue[b], verts_ue[a])
            t = max(0.0, min(1.0, v_dot(v_sub(q, A), ab) / max(v_dot(ab, ab), 1e-9)))
            d = v_len(v_sub(v_add(A, v_scale(ab, t)), q))
            if d < best_d:
                best, best_d = (a, b, t), d
        return best

    new = {}
    for i in {v for e in edge for v in e}:
        if i not in where:
            continue
        hit = across(verts_ue[i], rings)
        if hit and hit[0] in where and hit[1] in where:
            a, b, t = hit
            new[i] = v_norm(v_add(v_scale(normal(a), 1.0 - t), v_scale(normal(b), t)))
    # The next row in: halfway between its own normal and its edge neighbours' new ones.
    nbr = {}
    for a, b, c in tris_of[skin]:
        for p, q in ((a, b), (b, c), (c, a)):
            if p in new and q not in new:
                nbr.setdefault(q, []).append(new[p])
            elif q in new and p not in new:
                nbr.setdefault(p, []).append(new[q])
    second = {}
    for i, ns in nbr.items():
        if i in where:
            edge_n = v_norm(tuple(sum(n[k] for n in ns) for k in range(3)))
            second[i] = v_norm(v_add(normal(i), edge_n))
    new.update(second)
    for i, n in new.items():
        g, k = where[i]
        g["n"][k] = n
    return len(new)


def smooth_weights(weights, used, tris, passes=SMOOTH_PASSES, split_bone=None):
    """Average each vertex's weights with its mesh neighbours' (half and half), `passes` times.

    With split_bone (the jaw), vertices only average with neighbours on the same side of
    that bone's border, so the mouth line stays a clean split.
    """
    slot = {v: k for k, v in enumerate(used)}
    side = [w.get(split_bone, 0.0) > 0.5 for w in weights] if split_bone else [False] * len(used)
    nbrs = [set() for _ in used]
    for a, b, c in tris:
        a, b, c = slot[a], slot[b], slot[c]
        for p, q in ((a, b), (b, c), (c, a)):
            if side[p] == side[q]:
                nbrs[p].add(q)
                nbrs[q].add(p)
    for _ in range(passes):
        new = []
        for k, w in enumerate(weights):
            acc = {b: 0.5 * v for b, v in w.items()}
            share = 0.5 / len(nbrs[k]) if nbrs[k] else 0.0
            for n in nbrs[k]:
                for b, v in weights[n].items():
                    acc[b] = acc.get(b, 0.0) + share * v
            if not nbrs[k]:
                acc = dict(w)
            new.append(acc)
        weights = new
    return [_limit(w) for w in weights]


# ---------------------------------------------------------------- glTF writer
class _Glb:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.bin += data
        self.views.append(view)
        return len(self.views) - 1

    def accessor(self, typecode, values, gl_type, kind, count, target=None, minmax=False):
        arr = array.array(typecode, values)
        if sys.byteorder != "little":
            arr.byteswap()
        acc = {"bufferView": self._view(arr.tobytes(), target), "componentType": gl_type,
               "count": count, "type": kind}
        if minmax:
            n = len(values) // count
            acc["min"] = [min(values[i::n]) for i in range(n)]
            acc["max"] = [max(values[i::n]) for i in range(n)]
        self.accessors.append(acc)
        return len(self.accessors) - 1


FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def blurred_head_normals(groups, rot_ue_from_g, nx=40, nt=48, blur_x=3.0, blur_t=0.8):
    """Normals for painting the skull and jaw, with the small shapes blurred away: each part's normals averaged over
    a grid along it (nx cells) and around it (nt cells, angle about the middle of each slice), then blurred, mostly
    along the head (blur_x cells) and only a little around it (blur_t), so the skull's long edges stay crisp. The brows, cheekbones and hollows sculpted into the heads (body_tools/face_sculpt.py) then shade as
    shape instead of turning into pale belly patches wherever their undersides face down, while the countershading
    still follows the head's big shape. Returns paint_normal(p_ue, part): a UE-axes normal, or None for other parts."""
    pts = {}
    for grp in groups.values():
        for p_ue, part, n in zip(grp["ue"], grp["part"], grp["n"]):
            if part.startswith(("head", "jaw")):
                pts.setdefault(part, []).append((p_ue, m_vec(rot_ue_from_g, n)))
    grids = {}
    for part, pn in pts.items():
        x0 = min(p[0] for p, _ in pn)
        w = max((max(p[0] for p, _ in pn) - x0) / nx, 1e-3)
        lo, hi = [math.inf] * nx, [-math.inf] * nx
        for p, _ in pn:
            k = min(nx - 1, int((p[0] - x0) / w))
            lo[k], hi[k] = min(lo[k], p[2]), max(hi[k], p[2])
        mid = [0.5 * (a + b) if a <= b else None for a, b in zip(lo, hi)]
        for k in range(nx):
            if mid[k] is None:
                mid[k] = next(mid[j] for j in sorted(range(nx), key=lambda j: abs(j - k)) if mid[j] is not None)

        def cell(p, x0=x0, w=w, mid=mid):
            fx = min(nx - 1.0, max(0.0, (p[0] - x0) / w - 0.5))
            k = min(nx - 2, int(fx))
            zc = mid[k] + (mid[k + 1] - mid[k]) * (fx - k)          # smooth along X (a stepped one shows on beaks)
            ft = (math.atan2(p[2] - zc, p[1]) / (2 * math.pi) % 1.0) * nt - 0.5
            return fx, ft
        acc = [[[0.0, 0.0, 0.0, 0.0] for _ in range(nt)] for _ in range(nx)]
        for p, n in pn:
            fx, ft = cell(p)
            a = acc[int(round(fx))][int(round(ft)) % nt]
            a[0] += n[0]
            a[1] += n[1]
            a[2] += n[2]
            a[3] += 1.0
        rt, rx = int(math.ceil(2 * blur_t)), int(math.ceil(2 * blur_x))
        kt = [math.exp(-0.5 * (d / blur_t) ** 2) for d in range(-rt, rt + 1)]
        kx = [math.exp(-0.5 * (d / blur_x) ** 2) for d in range(-rx, rx + 1)]
        tmp = [[[sum(kt[d + rt] * acc[i][(j + d) % nt][c] for d in range(-rt, rt + 1)) for c in range(4)]
                for j in range(nt)] for i in range(nx)]
        out = [[[sum(kx[d + rx] * tmp[i + d][j][c] for d in range(-rx, rx + 1) if 0 <= i + d < nx) for c in range(3)]
                for j in range(nt)] for i in range(nx)]
        grids[part] = (cell, out)

    def paint_normal(p_ue, part):
        if part not in grids:
            return None
        cell, g = grids[part]
        fx, ft = cell(p_ue)
        i0, j0 = int(math.floor(fx)), int(math.floor(ft))
        tx, tt = fx - i0, ft - j0
        n = [0.0, 0.0, 0.0]
        for i, wi in ((i0, 1 - tx), (min(i0 + 1, nx - 1), tx)):
            for j, wj in ((j0 % nt, 1 - tt), ((j0 + 1) % nt, tt)):
                for c in range(3):
                    n[c] += wi * wj * g[i][j][c]
        return v_norm(n) if v_len(n) > 1e-9 else None
    return paint_normal


def species_key(rig_path):
    base = os.path.basename(rig_path)
    return base[:-len("_rig.json")] if base.endswith("_rig.json") else os.path.splitext(base)[0]


def build_glb(rig_path, obj_path, out_path, ue_from_gltf=UE_FROM_GLTF, asset_name=None):
    """Write the skinned .glb and return a summary dict."""
    rig, joints, topo = load_rig(rig_path)
    key = species_key(rig_path)
    # A splice without its own skin entry wears its base species' (bone_forge_skin.py).
    skey = key if key in skin.SKIN else rig.get("splice", {}).get("base", key)
    asset_name = asset_name or "SK_" + key[:1].upper() + key[1:]

    g_from_ue = m_inv(ue_from_gltf)                        # Unreal cm -> glTF m
    rot_g_from_ue = m_orthonormal_part(g_from_ue)
    rot_ue_from_g = m_t(rot_g_from_ue)

    def to_g(p):
        return m_vec(g_from_ue, p)

    def rot_to_g(r):
        return m_mul(m_mul(rot_g_from_ue, r), rot_ue_from_g)

    # Bones
    frames = bone_frames(joints, topo)
    index = {n: i for i, n in enumerate(topo)}
    world_r = {n: rot_to_g(frames[n]) for n in topo}
    world_p = {n: to_g(joints[n].pos) for n in topo}
    nodes, ibm = [], []
    for n in topo:
        j = joints[n]
        if j.parent is None:
            local_t, local_r = world_p[n], world_r[n]
        else:
            pr_t = m_t(world_r[j.parent])
            local_t = m_vec(pr_t, v_sub(world_p[n], world_p[j.parent]))
            local_r = m_mul(pr_t, world_r[n])
        node = {"name": n,
                "translation": [round(c, 7) for c in local_t],
                "rotation": [round(c, 9) for c in quat_from_matrix(local_r)]}
        if j.children:
            node["children"] = [index[c] for c in j.children]
        extras = {k: j.info[k] for k in ("section", "role", "limits_deg", "basis", "source", "limit_note")
                  if j.info.get(k) not in (None, "")}
        extras["position_cm"] = list(j.pos)
        if j.deforming:
            lateral = abs(frames[n][1][1])  # local Y . world Y
            extras["unreal_bend"] = (f"Pitch x {bend_pitch_sign(n):+d}" if lateral > 0.7 else
                                     "bone points sideways: bend axis is local Y but not the creature's right")
            ys = side_yaw_sign(n, frames[n])
            extras["unreal_side"] = f"Yaw x {ys:+d}" if ys else "local Z (bone is steep: check the sign)"
            extras["unreal_twist"] = "Roll"
        node["extras"] = extras
        nodes.append(node)
        rt = m_t(world_r[n])
        tt = v_scale(m_vec(rt, world_p[n]), -1.0)
        inv = ((rt[0][0], rt[0][1], rt[0][2], tt[0]),
               (rt[1][0], rt[1][1], rt[1][2], tt[1]),
               (rt[2][0], rt[2][1], rt[2][2], tt[2]),
               (0.0, 0.0, 0.0, 1.0))
        ibm.extend(inv[r][c] for c in range(4) for r in range(4))  # column-major

    # Body: OBJ -> Unreal -> glTF; weights per part
    verts_obj, parts = load_obj(obj_path)
    verts_ue = [m_vec(UE_FROM_OBJ, v) for v in verts_obj]
    joints = _JointMap(joints)
    joints.has_jaw_part = any(p.startswith("jaw") for p, _ in parts)
    flip_winding = m_det(m_mul(g_from_ue, UE_FROM_OBJ)) < 0
    groups = {}  # material -> {"remap": {}, "pos": [], "w": [], "tris": [], "ue": [], "part": []}
    part_report = []
    seen_head = False
    jpos = {n: j.pos for n, j in joints.items()}
    part_stats = {}
    for pname, tris in parts:
        base = pname.rsplit("_", 1)[0] if pname.rsplit("_", 1)[-1].isdigit() else pname
        pts = [verts_ue[i] for i in {i for t in tris for i in t}]
        c = v_scale(tuple(map(sum, zip(*pts))), 1.0 / len(pts))
        part_stats[base] = (c, max(v_len(v_sub(q, c)) for q in pts))
    all_weights, part_own = {}, {}
    for pname, tris in parts:
        used = sorted({i for t in tris for i in t})
        pts = [verts_ue[i] for i in used]
        centroid = v_scale(tuple(map(sum, zip(*pts))), 1.0 / len(pts))
        is_head = pname.startswith("head")
        own, rigid = part_bones(pname, centroid, joints, extra_head=is_head and seen_head)
        seen_head |= is_head
        chains_path = obj_path[:-4] + "_chains.json"
        if pname.startswith("body_grown") and os.path.exists(chains_path):
            weights = grown_weights(used, pts, chains_path, joints)
        else:
            weights = skin_part(pts, own, joints, rigid)
        if not rigid:
            # A grown skin's faces are small and even, so its weights need more passes to spread as far.
            weights = smooth_weights(weights, used, tris, passes=SMOOTH_PASSES * 3 if pname.startswith("body_grown") else SMOOTH_PASSES,
                                     split_bone="jaw" if is_head else None)
        all_weights[pname], part_own[pname] = dict(zip(used, weights)), own
    share_seams(all_weights, verts_ue, parts)
    feathers = feather_uv(parts, verts_ue, jpos)
    for pname, tris in parts:
        own = part_own[pname]
        base = pname.rsplit("_", 1)[0] if pname.rsplit("_", 1)[-1].isdigit() else pname
        wt = all_weights[pname]
        for a, b, c in tris:       # material per triangle: a part can carry two (the Pteranodon's skin and beak)
            if skey in skin.SKIN:
                cen = v_scale(v_add(v_add(verts_ue[a], verts_ue[b]), verts_ue[c]), 1.0 / 3.0)
                mat = skin.material_for(skey, base, cen, jpos)
            else:
                mat = ("Membrane" if pname.startswith("membrane") else "Teeth" if pname.startswith("teeth")
                       else "Mouth" if pname.startswith("tongue") else "Skin")
            grp = groups.setdefault(mat, {"remap": {}, "pos": [], "w": [], "tris": [], "ue": [], "part": []})
            for i in (a, b, c):
                if i not in grp["remap"]:
                    grp["remap"][i] = len(grp["pos"])
                    grp["pos"].append(to_g(verts_ue[i]))
                    grp["w"].append(wt[i])
                    grp["ue"].append(verts_ue[i])
                    grp["part"].append(base)
            t = (grp["remap"][a], grp["remap"][c], grp["remap"][b]) if flip_winding else \
                (grp["remap"][a], grp["remap"][b], grp["remap"][c])
            grp["tris"].append(t)
        part_report.append({"part": pname, "triangles": len(tris), "bones": own})

    for grp in groups.values():
        pos = grp["pos"]
        normals = [[0.0, 0.0, 0.0] for _ in pos]
        for a, b, c in grp["tris"]:
            fn = v_cross(v_sub(pos[b], pos[a]), v_sub(pos[c], pos[a]))  # area-weighted
            for i in (a, b, c):
                nn = normals[i]
                nn[0] += fn[0]
                nn[1] += fn[1]
                nn[2] += fn[2]
        # Coincident vertices of one part (a cut's duplicates: grow_body's foot cut left some on the ring) share one
        # normal, or the cut shows as a crease.
        same = {}
        for k, (p, part) in enumerate(zip(pos, grp["part"])):
            same.setdefault((part, round(p[0] * 2000), round(p[1] * 2000), round(p[2] * 2000)), []).append(k)
        for ks in same.values():
            if len(ks) > 1:
                total = [sum(normals[k][c] for k in ks) for c in range(3)]
                for k in ks:
                    normals[k] = list(total)
        grp["n"] = [v_norm(n) if v_len(n) > 1e-20 else (0.0, 1.0, 0.0) for n in normals]
    seam_normals(groups, parts, verts_ue)
    paint_normal = blurred_head_normals(groups, rot_ue_from_g) if skey in skin.SKIN else None
    # A grown skin's chains (grow_body.py's _chains.json): each skin vertex's [chain, share] pairs, and what each chain
    # paints as: the trunk as the body, a limb as its leg or arm (thigh_2_l -> leg2_l).
    grown_share, chain_paint = [], []
    chains_path = obj_path[:-4] + "_chains.json"
    if os.path.exists(chains_path) and any(p.startswith("body_grown") for p, _ in parts):
        with open(chains_path, encoding="utf-8") as f:
            cdata = json.load(f)
        grown_share = cdata["share"]
        for own in cdata["bones"]:
            first = own[0] if own else ""
            for top, name in (("thigh", "leg"), ("upperarm", "arm")):
                if first.startswith(top):
                    pair = "2" if "_2_" in first else ""
                    chain_paint.append(f"{name}{pair}_{first[-1]}")
                    break
            else:
                chain_paint.append("body")

    glb = _Glb()
    primitives, materials = [], []
    max_influences, unweighted = 0, 0
    for mat, grp in groups.items():
        pos, tris, normals = grp["pos"], grp["tris"], grp["n"]
        jidx, wts = [], []
        for w in grp["w"]:
            items = sorted(w.items(), key=lambda kv: -kv[1])[:4]
            total = sum(v for _, v in items)
            if total <= 0:
                unweighted += 1
                items, total = [(topo[1] if len(topo) > 1 else topo[0], 1.0)], 1.0
            max_influences = max(max_influences, len(items))
            items += [(None, 0.0)] * (4 - len(items))
            jidx.extend(index[b] if b is not None else 0 for b, _ in items)
            wts.extend(v / total for _, v in items)
        n_v = len(pos)
        attrs = {
            "POSITION": glb.accessor("f", [c for p in pos for c in p], FLOAT, "VEC3", n_v, ARRAY_BUFFER, True),
            "NORMAL": glb.accessor("f", [c for p in normals for c in p], FLOAT, "VEC3", n_v, ARRAY_BUFFER),
            "JOINTS_0": glb.accessor("H", jidx, USHORT, "VEC4", n_v, ARRAY_BUFFER),
            "WEIGHTS_0": glb.accessor("f", wts, FLOAT, "VEC4", n_v, ARRAY_BUFFER),
        }
        # Feathers (feather_uv): root-to-tip and place along the wing; (0, 0) everywhere else.
        obj_of = [0] * n_v
        for i, k in grp["remap"].items():
            obj_of[k] = i
        attrs["TEXCOORD_0"] = glb.accessor("f", [c for i in obj_of for c in feathers.get(i, (0.0, 0.0))], FLOAT, "VEC2", n_v, ARRAY_BUFFER)
        if skey in skin.SKIN:     # where on the animal each vertex is, for the skin material (bone_forge_skin.py)
            cols = []
            # Wings on an animal that isn't a Pteranodon: their flesh takes the back's colour all round (folded at
            # rest, its underside would paint as a pale belly and show from above in flight).
            pair = "_2_" if rig.get("wing_pair") else None
            wing_bones = {n for n, j in joints.items() if j.section == "Shoulder" and (not pair or pair in n)} if rig.get("wing_area_m2") and skey != "ptera" else set()
            for k, (p_ue, nrm, part, wt) in enumerate(zip(grp["ue"], normals, grp["part"], grp["w"])):
                shown = part
                if wing_bones and part.startswith(("body_grown", "arm_hand", "arm2_hand")) and sum(v for b, v in wt.items() if b in wing_bones) > 0.5:
                    shown = "wing_flesh"
                n_paint = paint_normal(p_ue, part) or m_vec(rot_ue_from_g, nrm)
                mix = grown_share[obj_of[k]] if shown.startswith("body_grown") and obj_of[k] < len(grown_share) else None
                if mix:
                    # Grown skin is painted by the chains it grew from, blended by their shares (the trunk as the body,
                    # a leg as that leg...), so grown legs keep the hand-built legs' paint (paler below, the feet's
                    # scales fading in above the ankle) and nothing changes at a line where a leg leaves the body.
                    acc, tot = [0.0, 0.0, 0.0, 0.0], 0.0
                    for c, s in mix:
                        for q, val in enumerate(skin.paint(skey, chain_paint[c], p_ue, n_paint, jpos, part_stats)):
                            acc[q] += s * val
                        tot += s
                    cols.extend(v / tot for v in acc)
                else:
                    cols.extend(skin.paint(skey, shown, p_ue, n_paint, jpos, part_stats))
            attrs["COLOR_0"] = glb.accessor("f", cols, FLOAT, "VEC4", n_v, ARRAY_BUFFER)
        ind = glb.accessor("I", [i for t in tris for i in t], UINT, "SCALAR", 3 * len(tris), ELEMENT_ARRAY_BUFFER)
        materials.append({"name": f"M_{asset_name[3:] if asset_name.startswith('SK_') else asset_name}_{mat}",
                          "pbrMetallicRoughness": {"baseColorFactor": list(skin.base_colour(skey, mat, MATERIAL_COLOURS[mat])),
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.8},
                          "doubleSided": mat == "Membrane"})
        primitives.append({"attributes": attrs, "indices": ind, "material": len(materials) - 1, "mode": 4})

    ibm_acc = glb.accessor("f", ibm, FLOAT, "MAT4", len(topo))
    mesh_node = len(nodes)
    nodes.append({"name": asset_name, "mesh": 0, "skin": 0})
    gltf = {
        "asset": {"version": "2.0", "generator": "Bone Forge rig importer (bone_forge_gltf.py)",
                  "extras": {"creature": rig.get("creature", key), "species_key": key,
                             "rig_json": os.path.basename(rig_path), "body_obj": os.path.basename(obj_path),
                             "ue_from_gltf": [list(r) for r in ue_from_gltf],
                             "note": "Bone axes: X along the bone, Y toward the creature's right. "
                                     "bend = local Y, side = local Z, twist = local X. Starter skin weights."}},
        "scene": 0,
        "scenes": [{"name": asset_name, "nodes": [index[topo[0]], mesh_node]}],
        "nodes": nodes,
        "meshes": [{"name": asset_name, "primitives": primitives}],
        "materials": materials,
        "skins": [{"name": asset_name + "_Skeleton", "joints": list(range(len(topo))),
                   "inverseBindMatrices": ibm_acc, "skeleton": index[topo[0]]}],
        "accessors": glb.accessors,
        "bufferViews": glb.views,
        "buffers": [{"byteLength": len(glb.bin)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    binary = bytes(glb.bin) + b"\0" * (-len(glb.bin) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(binary), b"BIN\0") + binary)

    return {"glb": out_path, "asset_name": asset_name, "species": key, "bones": len(topo),
            "vertices": sum(len(g["pos"]) for g in groups.values()),
            "triangles": sum(len(g["tris"]) for g in groups.values()),
            "max_influences": max_influences, "unweighted_vertices": unweighted, "parts": part_report}


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rig_json")
    ap.add_argument("body_obj")
    ap.add_argument("out_glb")
    ap.add_argument("--name", help="asset name in Unreal (default SK_<Species>)")
    ap.add_argument("--ue-from-gltf", nargs=9, type=float, metavar="M",
                    help="row-major 3x3 map from glTF metres to Unreal cm (default: swap Y/Z, x100)")
    args = ap.parse_args(argv)
    m = UE_FROM_GLTF
    if args.ue_from_gltf:
        v = args.ue_from_gltf
        m = (tuple(v[0:3]), tuple(v[3:6]), tuple(v[6:9]))
    s = build_glb(args.rig_json, args.body_obj, args.out_glb, m, args.name)
    print(f"{s['glb']}: {s['asset_name']}, {s['bones']} bones, {s['vertices']} vertices, "
          f"{s['triangles']} triangles, max {s['max_influences']} bones per vertex")
    for p in s["parts"]:
        print(f"  {p['part']:18s} {p['triangles']:6d} tris -> {', '.join(p['bones'])}")


if __name__ == "__main__":
    main()
