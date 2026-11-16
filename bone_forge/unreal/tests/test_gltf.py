"""Checks the .glb files written by bone_forge_gltf.py, reading them back from the raw bytes.

    python tests/test_gltf.py            (from the unreal/ folder; builds the six species and every splice)

For each species it checks that:
  - every rig joint is a bone with the right parent;
  - bone positions, after the glTF -> Unreal axis map, match position_cm in the rig JSON;
  - each bone's X axis points at its main child joint and its Y axis toward the creature's right;
  - inverse bind matrices are the inverse of the bone transforms (so the mesh is not distorted);
  - skinning in the bind pose puts every vertex back where the OBJ has it;
  - weights sum to 1, use at most 4 bones, and never use root or an *_end marker;
  - every vertex of a left part is weighted to left bones only, and right to right.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import bone_forge_gltf as bf  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SPECIES = ["trex", "trike", "raptor", "ankylo", "brachio", "ptera"] + sorted(   # + every splice (splice/splice.py)
    f[:-9] for f in os.listdir(os.path.join(ROOT, "rig_joints"))
    if f.endswith("_rig.json") and not f.startswith("_") and "splice" in json.load(open(os.path.join(ROOT, "rig_joints", f), encoding="utf-8")))


def read_glb(path):
    data = open(path, "rb").read()
    magic, version, total = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2 and total == len(data)
    jlen, jtype = struct.unpack_from("<I4s", data, 12)
    gl = json.loads(data[20:20 + jlen])
    blen, btype = struct.unpack_from("<I4s", data, 20 + jlen)
    assert jtype == b"JSON" and btype == b"BIN\0"
    binary = data[28 + jlen:28 + jlen + blen]
    return gl, binary


def accessor(gl, binary, i):
    a = gl["accessors"][i]
    v = gl["bufferViews"][a["bufferView"]]
    fmt = {5126: "f", 5123: "H", 5125: "I", 5121: "B"}[a["componentType"]]
    n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    vals = struct.unpack_from(f"<{a['count'] * n}{fmt}", binary, v["byteOffset"] + a.get("byteOffset", 0))
    return [vals[k:k + n] for k in range(0, len(vals), n)]


def quat_to_matrix(q):
    x, y, z, w = q
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


def check_species(key, out_dir):
    rig_path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
    obj_path = os.path.join(ROOT, "body_shapes", f"{key}_body.obj")
    glb_path = os.path.join(out_dir, f"{key}_skeletal.glb")
    summary = bf.build_glb(rig_path, obj_path, glb_path)
    gl, binary = read_glb(glb_path)
    rig = json.load(open(rig_path, encoding="utf-8"))
    rig_j = {j["name"]: j for j in rig["joints"]}
    _, joints, topo = bf.load_rig(rig_path)
    problems = []

    # --- skeleton
    skin = gl["skins"][0]
    jnodes = skin["joints"]
    names = [gl["nodes"][i]["name"] for i in jnodes]
    if sorted(names) != sorted(rig_j):
        problems.append(f"bone names differ from the rig: {set(names) ^ set(rig_j)}")
    parent_of = {}
    for i, nd in enumerate(gl["nodes"]):
        for c in nd.get("children", []):
            parent_of[c] = i
    world = {}

    def world_of(i):
        if i in world:
            return world[i]
        nd = gl["nodes"][i]
        r = quat_to_matrix(nd.get("rotation", (0, 0, 0, 1)))
        t = tuple(nd.get("translation", (0, 0, 0)))
        if i in parent_of:
            pr, pt = world_of(parent_of[i])
            r, t = bf.m_mul(pr, r), bf.v_add(pt, bf.m_vec(pr, t))
        world[i] = (r, t)
        return world[i]

    ue_from_g = bf.UE_FROM_GLTF
    rot_ue_from_g = bf.m_orthonormal_part(ue_from_g)
    worst_pos, worst_axis = 0.0, 1.0
    ibm = accessor(gl, binary, skin["inverseBindMatrices"])
    worst_ibm = 0.0
    for k, i in enumerate(jnodes):
        name = gl["nodes"][i]["name"]
        par = gl["nodes"][parent_of[i]]["name"] if i in parent_of else None
        if par != rig_j[name]["parent"]:
            problems.append(f"{name}: parent {par}, rig says {rig_j[name]['parent']}")
        r, t = world_of(i)
        pos_ue = bf.m_vec(ue_from_g, t)
        worst_pos = max(worst_pos, bf.v_len(bf.v_sub(pos_ue, rig_j[name]["position_cm"])))
        r_ue = bf.m_mul(bf.m_mul(rot_ue_from_g, r), bf.m_t(rot_ue_from_g))
        x_ue = (r_ue[0][0], r_ue[1][0], r_ue[2][0])
        y_ue = (r_ue[0][1], r_ue[1][1], r_ue[2][1])
        mc = bf.main_child(joints[name])
        if mc is not None and par is not None:  # root keeps the world axes on purpose
            want = bf.v_norm(bf.v_sub(joints[mc].pos, joints[name].pos))
            worst_axis = min(worst_axis, bf.v_dot(x_ue, want))
        if abs(y_ue[1]) < 0.7 and joints[name].deforming:  # sprawling arms are fine up to 45 degrees
            problems.append(f"{name}: local Y is not toward the creature's right (Y.y = {y_ue[1]:.2f})")
        # IBM * world should be identity
        m = ibm[k]
        inv = [[m[c * 4 + rr] for c in range(4)] for rr in range(4)]
        w4 = [list(r[0]) + [t[0]], list(r[1]) + [t[1]], list(r[2]) + [t[2]], [0, 0, 0, 1]]
        prod = [[sum(inv[a][b] * w4[b][c] for b in range(4)) for c in range(4)] for a in range(4)]
        worst_ibm = max(worst_ibm, max(abs(prod[a][c] - (1 if a == c else 0)) for a in range(4) for c in range(4)))
    if worst_pos > 0.01:
        problems.append(f"bone position off by up to {worst_pos:.3f} cm")
    if worst_axis < 0.9999:
        problems.append(f"a bone's X axis misses its child joint (cos {worst_axis:.5f})")
    if worst_ibm > 1e-5:
        problems.append(f"inverse bind matrices off by {worst_ibm:.2e}")

    # --- skinning
    obj_v, _ = bf.load_obj(obj_path)
    obj_set = {tuple(round(c, 2) for c in v) for v in obj_v}
    bind = {}
    for k, i in enumerate(jnodes):
        r, t = world_of(i)
        m = ibm[k]
        inv_r = ((m[0], m[4], m[8]), (m[1], m[5], m[9]), (m[2], m[6], m[10]))
        inv_t = (m[12], m[13], m[14])
        bind[k] = (bf.m_mul(r, inv_r), bf.v_add(bf.m_vec(r, inv_t), t))
    end_or_root = {k for k, i in enumerate(jnodes)
                   if not joints[gl["nodes"][i]["name"]].deforming or bf.is_stub(joints[gl["nodes"][i]["name"]], joints)}
    worst_skin, worst_sum, max_inf, bad_bones = 0.0, 0.0, 0, 0
    tris, worst_jump, worst_at = 0, 0.0, ''
    for prim in gl["meshes"][0]["primitives"]:
        pos = accessor(gl, binary, prim["attributes"]["POSITION"])
        jj = accessor(gl, binary, prim["attributes"]["JOINTS_0"])
        ww = accessor(gl, binary, prim["attributes"]["WEIGHTS_0"])
        idx = [i[0] for i in accessor(gl, binary, prim["indices"])]
        tris += len(idx) // 3
        # weight jump across each edge: half the L1 distance between the two weight sets (0 = same, 1 = no overlap)
        wmap = [{j: w for j, w in zip(js, ws) if w > 0} for js, ws in zip(jj, ww)]
        jaw = next((k for k, i in enumerate(jnodes) if gl["nodes"][i]["name"] == "jaw"), None)
        valence = {}
        for i in idx:
            valence[i] = valence.get(i, 0) + 1
        for k in range(0, len(idx), 3):
            for a, b in ((idx[k], idx[k + 1]), (idx[k + 1], idx[k + 2]), (idx[k + 2], idx[k])):
                wa, wb = wmap[a], wmap[b]
                if (wa.get(jaw, 0) > 0.5) != (wb.get(jaw, 0) > 0.5):  # the mouth line is meant to be a clean split
                    continue
                if max(valence[a], valence[b]) > 12:  # centre of a fan-shaped end cap or plate: long edges
                    continue
                jump = sum(abs(wa.get(x, 0) - wb.get(x, 0)) for x in set(wa) | set(wb)) / 2
                if jump > worst_jump:
                    worst_jump = jump
                    names_ = [gl["nodes"][jnodes[j]]["name"] for j in set(wa) | set(wb)]
                    worst_at = (f"{[round(c * 100) for c in pos[a]]} -> {[round(c * 100) for c in pos[b]]} "
                                f"(glTF cm), bones {sorted(names_)}")
        for p, js, ws in zip(pos, jj, ww):
            worst_sum = max(worst_sum, abs(sum(ws) - 1))
            max_inf = max(max_inf, sum(1 for w in ws if w > 0))
            bad_bones += any(w > 0 and j in end_or_root for j, w in zip(js, ws))
            out = (0.0, 0.0, 0.0)
            for j, w in zip(js, ws):
                if w:
                    br, bt = bind[j]
                    out = bf.v_add(out, bf.v_scale(bf.v_add(bf.m_vec(br, p), bt), w))
            worst_skin = max(worst_skin, bf.v_len(bf.v_sub(out, p)))
        # back to OBJ cm: glTF (x, y, z) m is OBJ (x, y, z) cm / 100 with the default axis map
        missing = sum(1 for p in pos if tuple(round(c * 100, 2) for c in p) not in obj_set)
        if missing:
            problems.append(f"{missing} vertices are not at an OBJ vertex position")
    if worst_skin > 1e-5:
        problems.append(f"bind-pose skinning moves vertices by up to {worst_skin * 100:.4f} cm")
    if worst_sum > 1e-5:
        problems.append(f"weights sum off by {worst_sum:.2e}")
    if max_inf > 4:
        problems.append(f"{max_inf} bones on one vertex")
    if bad_bones:
        problems.append(f"{bad_bones} vertices weighted to root or an *_end marker")
    if worst_jump > 0.5:
        problems.append(f"skin weights jump by {worst_jump:.2f} across one edge (seam) at {worst_at}")
    summary["worst_jump"] = worst_jump

    # --- left parts only on left bones
    for part in summary["parts"]:
        side = "_l" if "_l_" in part["part"] + "_" else "_r" if "_r_" in part["part"] + "_" else None
        if side and part["part"].startswith(("leg", "arm", "membrane")):
            wrong = [b for b in part["bones"] if b.endswith("_r" if side == "_l" else "_l")]
            if wrong:
                problems.append(f"{part['part']} may use {wrong}")
    return summary, tris, problems


def main():
    out_dir = os.path.join(os.path.dirname(HERE), "out")
    failed = False
    for key in SPECIES:
        s, tris, problems = check_species(key, out_dir)
        status = "OK  " if not problems else "FAIL"
        failed |= bool(problems)
        print(f"{status} {key:8s} {s['bones']:3d} bones  {s['vertices']:6d} verts  {tris:6d} tris  "
              f"max {s['max_influences']} bones/vertex  worst edge weight jump {s['worst_jump']:.2f}")
        for p in problems:
            print("       -", p)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
