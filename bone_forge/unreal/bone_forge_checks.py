"""Bone Forge rig importer, part 2: checks used by bone_forge_unreal.py (standard-library Python).

These work out what Unreal actually did on import, from what can be measured in the editor:
  - the bounding box of the static mesh made from the body OBJ;
  - the bone positions and axes of the skeletal mesh made from the .glb.
Each is compared with every axis-aligned rotation or mirror (48 of them) and a uniform scale,
so a result reads like "faces -X, lying on its side, 1 unit = 1 cm" instead of just "wrong".
"""

import itertools
import math

import bone_forge_gltf as bfg

IDENTITY = bfg.IDENTITY
AXIS_NAMES = {(1, 0, 0): "+X", (-1, 0, 0): "-X", (0, 1, 0): "+Y", (0, -1, 0): "-Y", (0, 0, 1): "+Z", (0, 0, -1): "-Z"}


def signed_permutations():
    """The 48 axis-aligned rotations and mirrors, identity first."""
    out = []
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1.0, -1.0), repeat=3):
            m = [[0.0] * 3 for _ in range(3)]
            for r in range(3):
                m[r][perm[r]] = signs[r]
            out.append(tuple(tuple(row) for row in m))
    out.sort(key=lambda m: m != IDENTITY)
    return out


def axis_name(v):
    return AXIS_NAMES.get(tuple(int(round(c)) for c in v), str(tuple(round(c, 3) for c in v)))


def describe(s):
    """Where the creature's forward, right and up ended up after the axis change s."""
    fwd, right, up = (bfg.m_vec(s, a) for a in ((1, 0, 0), (0, 1, 0), (0, 0, 1)))
    mirror = " (mirrored: left and right swapped)" if bfg.m_det(s) < 0 else ""
    return f"faces {axis_name(fwd)}, up is {axis_name(up)}, right side toward {axis_name(right)}{mirror}"


def is_identity(s, k=1.0, tol=0.01):
    return s == IDENTITY and abs(k - 1.0) < tol


# ---------------------------------------------------------------- OBJ static mesh
def obj_expected_bounds(obj_path):
    """Bounding box (min, max) the body OBJ should have in Unreal, in cm."""
    verts, parts = bfg.load_obj(obj_path)
    used = {i for _, tris in parts for t in tris for i in t}
    pts = [bfg.m_vec(bfg.UE_FROM_OBJ, verts[i]) for i in used]
    return tuple(min(p[i] for p in pts) for i in range(3)), tuple(max(p[i] for p in pts) for i in range(3))


def match_bounds(exp_min, exp_max, got_min, got_max):
    """Rank every axis change s and scale k by how well k * s * expected-box explains the imported box.

    Returns [(worst corner error in cm, s, k)], best first.
    """
    corners = [(x, y, z) for x in (exp_min[0], exp_max[0]) for y in (exp_min[1], exp_max[1])
               for z in (exp_min[2], exp_max[2])]
    got = list(got_min) + list(got_max)
    out = []
    for s in signed_permutations():
        tc = [bfg.m_vec(s, c) for c in corners]
        box = [min(c[i] for c in tc) for i in range(3)] + [max(c[i] for c in tc) for i in range(3)]
        den = sum(a * a for a in box)
        k = sum(a * b for a, b in zip(box, got)) / den if den else 1.0
        out.append((max(abs(k * a - b) for a, b in zip(box, got)), s, k))
    out.sort(key=lambda r: r[0])
    return out


def obj_report(obj_path, got_min, got_max, tol_cm=1.0):
    """(ok, lines) for the static mesh imported from the body OBJ."""
    exp_min, exp_max = obj_expected_bounds(obj_path)
    size = [b - a for a, b in zip(got_min, got_max)]
    lines = [f"Imported size: {size[0]:.0f} x {size[1]:.0f} x {size[2]:.0f} cm (X x Y x Z); "
             f"tallest axis {max(size) / 100:.2f} m, height on Z {size[2] / 100:.2f} m"]
    ranked = match_bounds(exp_min, exp_max, got_min, got_max)
    err, s, k = ranked[0]
    if err > tol_cm * max(1.0, k):
        lines.append(f"Bounds don't match the OBJ under any axis change (best fit off by {err:.1f} cm). "
                     "The importer may have moved the pivot or merged/dropped parts.")
        return False, lines
    ties = [r for r in ranked if r[0] <= err + tol_cm]
    if any(is_identity(t[1], t[2]) for t in ties):
        lines.append("Matches the rig: upright (Z up), facing +X, origin under the hips, 1 OBJ unit = 1 cm. "
                     "No rotation or scale needed.")
        if len(ties) > 1:
            lines.append("(A box can't show a left/right mirror; the skeleton check below covers that.)")
        return True, lines
    err, s, k = best_obj_fit(ranked, tol_cm)
    lines.append(f"Imported {describe(s)}, scale {k:g} (1 OBJ unit = {k:g} cm). "
                 "(A box can't show a left/right mirror, so it may also be mirrored.)")
    return False, lines


def best_obj_fit(ranked, tol_cm=1.0):
    """Among fits as good as the best, prefer a plain rotation: the body is left/right symmetric, so a
    mirror and a rotation give the same box, and only a rotation can be undone in the import options."""
    ties = [r for r in ranked if r[0] <= ranked[0][0] + tol_cm]
    return next((r for r in ties if bfg.m_det(r[1]) > 0), ranked[0])


# ---------------------------------------------------------------- skeleton
def fit_points(expected, got):
    """Best axis change s and scale k with got ~= k * s * expected over the shared names.

    Returns (rms error in cm, s, k).
    """
    names = [n for n in expected if n in got]
    best = None
    for s in signed_permutations():
        se = [bfg.m_vec(s, expected[n]) for n in names]
        g = [got[n] for n in names]
        den = sum(bfg.v_dot(a, a) for a in se)
        k = sum(bfg.v_dot(a, b) for a, b in zip(se, g)) / den if den else 1.0
        rms = math.sqrt(sum(bfg.v_dot(d, d) for d in (bfg.v_sub(bfg.v_scale(a, k), b) for a, b in zip(se, g)))
                        / max(1, len(names)))
        if best is None or rms < best[0] - 1e-9:
            best = (rms, s, k)
    return best


def corrected_map(ue_from_gltf, s, k):
    """The glTF->Unreal map the importer really used, if a build with ue_from_gltf came in as k * s * expected."""
    k = round(k, 5)  # float32 positions in the .glb make the fitted scale 0.99999999 rather than 1
    return tuple(tuple(round(k * c, 6) for c in row) for row in bfg.m_mul(s, ue_from_gltf))


def check_skeleton(rig_path, bones, pos_tol_cm=0.1, axis_tol=0.9999):
    """Compare imported bones with the rig JSON.

    bones: [{"name", "parent" (or None), "pos": component-space cm, "x_axis": unit vector}]
    Returns (ok, lines, fit) where fit = (rms cm, s, k) from fit_points.
    """
    rig, joints, topo = bfg.load_rig(rig_path)
    got = {b["name"]: b for b in bones}
    lines, ok = [], True
    missing = [n for n in topo if n not in got]
    extra = [b["name"] for b in bones if b["name"] not in joints]
    if missing:
        ok = False
        lines.append(f"MISSING bones: {', '.join(missing)}")
    if extra:
        lines.append(f"Unreal added bones that aren't in the rig: {', '.join(extra)}")
    wrong_parent = [f"{n} (parent {got[n]['parent']}, rig says {joints[n].parent})" for n in topo
                    if n in got and got[n]["parent"] != joints[n].parent
                    and not (joints[n].parent is None and got[n]["parent"] in extra)]
    if wrong_parent:
        ok = False
        lines.append("Wrong parent: " + "; ".join(wrong_parent))

    fit = fit_points({n: joints[n].pos for n in topo}, {n: b["pos"] for n, b in got.items()})
    errs = {n: bfg.v_len(bfg.v_sub(got[n]["pos"], joints[n].pos)) for n in topo if n in got}
    worst = max(errs, key=errs.get) if errs else None
    if worst is not None and errs[worst] > pos_tol_cm:
        ok = False
        rms, s, k = fit
        lines.append(f"Bone positions are off (worst: {worst}, {errs[worst]:.2f} cm).")
        if rms < 0.5 and not is_identity(s, k):
            lines.append(f"They match the rig after an axis change: the skeleton came in {describe(s)}, "
                         f"scale {k:g}. The importer's glTF axis map differs from the one assumed.")
    else:
        lines.append(f"All {len(errs)} bone positions match the rig JSON (worst {errs[worst]:.3f} cm, {worst}).")

    bad_axes = []
    for n in topo:
        j = joints[n]
        mc = bfg.main_child(j)
        if n not in got or j.parent is None or mc is None:
            continue
        want = bfg.v_norm(bfg.v_sub(joints[mc].pos, j.pos))
        if bfg.v_dot(bfg.v_norm(got[n]["x_axis"]), want) < axis_tol:
            bad_axes.append(n)
    if bad_axes:
        ok = False
        lines.append(f"Bone X axes don't point at the child joint: {', '.join(bad_axes)}")
    else:
        lines.append("Every bone's X axis points at its child joint (bend = Pitch, side = Yaw, twist = Roll).")
    return ok, lines, fit


# ---------------------------------------------------------------- quaternions (Unreal FQuat order x, y, z, w)
def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def q_rotate(q, v):
    x, y, z, w = q
    u = (x, y, z)
    t = bfg.v_scale(bfg.v_cross(u, v), 2.0)
    return bfg.v_add(bfg.v_add(v, bfg.v_scale(t, w)), bfg.v_cross(u, t))


def compose_bones(names, parents, local):
    """Component-space bones from Unreal's local reference pose.

    names: bone names in Unreal's order (parents first); parents: {name: parent or None};
    local: {name: (quat xyzw, translation, scale)}. Returns the list check_skeleton takes.
    """
    world, out = {}, []
    for n in names:
        q, t, sc = local[n]
        p = parents[n]
        if p is None or p not in world:
            wq, wt, ws = q, t, sc
        else:
            pq, pt, ps = world[p]
            wq = q_mul(pq, q)
            wt = bfg.v_add(pt, q_rotate(pq, (t[0] * ps[0], t[1] * ps[1], t[2] * ps[2])))
            ws = (sc[0] * ps[0], sc[1] * ps[1], sc[2] * ps[2])
        world[n] = (wq, wt, ws)
        out.append({"name": n, "parent": p, "pos": wt, "x_axis": q_rotate(wq, (1.0, 0.0, 0.0))})
    return out
