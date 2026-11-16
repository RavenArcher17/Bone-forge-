"""Fix inside-out faces in a body OBJ.

The v1 body volumes have their end caps (neck, tail tip, back of the skull, limb ends, tail club)
wound the opposite way to the faces around them, so those caps face into the body: in the engine
they're culled or lit from inside, and the surface looks inverted there. This re-orients every
triangle of a part to agree with its neighbours (each shared edge runs opposite ways in its two
triangles), then turns the whole part outward (positive signed volume). Open sheets that are
meant to be open (Pteranodon membranes) are only made consistent, not turned.

    python fix_winding.py ../body_shapes/trex_body.obj [more.obj ...]
A copy of each original is kept as <name>_v1.obj (once). Vertices and their order are unchanged.
"""
import os
import shutil
import sys

SHEETS = ("membrane", "wing_feathers", "tail_fan")


def orient(tris):
    edges = {}
    for i, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            edges.setdefault(frozenset(e), []).append(i)
    tris = list(tris)
    done = [False] * len(tris)
    flipped = 0
    for seed in range(len(tris)):
        if done[seed]:
            continue
        done[seed] = True
        stack = [seed]
        while stack:
            i = stack.pop()
            a, b, c = tris[i]
            for u, v in ((a, b), (b, c), (c, a)):
                for j in edges[frozenset((u, v))]:
                    if done[j]:
                        continue
                    t = tris[j]
                    if any((t[k], t[(k + 1) % 3]) == (u, v) for k in range(3)):
                        tris[j] = (t[0], t[2], t[1])
                        flipped += 1
                    done[j] = True
                    stack.append(j)
    return tris, flipped


def volume(tris, verts):
    s = 0.0
    for a, b, c in tris:
        A, B, C = verts[a - 1], verts[b - 1], verts[c - 1]
        s += A[0] * (B[1] * C[2] - B[2] * C[1]) - A[1] * (B[0] * C[2] - B[2] * C[0]) + A[2] * (B[0] * C[1] - B[1] * C[0])
    return s / 6.0


def fix(path):
    backup = path[:-4] + "_v1.obj"
    if not os.path.exists(backup):
        shutil.copyfile(path, backup)
    lines = open(path, encoding="utf-8").read().splitlines()
    verts = [tuple(float(x) for x in l.split()[1:4]) for l in lines if l.startswith("v ")]
    sections, cur = [], None
    for i, l in enumerate(lines):
        if l.startswith("o "):
            cur = [l.split()[1], []]
            sections.append(cur)
        elif l.startswith("f ") and cur:
            cur[1].append(i)
    report = []
    replace = {}
    for name, idx in sections:
        tris = []
        for i in idx:
            f = [int(x.split("/")[0]) for x in lines[i].split()[1:]]
            tris += [(f[0], f[k], f[k + 1]) for k in range(1, len(f) - 1)]
        before = list(tris)
        tris, _ = orient(tris)
        if not name.startswith(SHEETS) and volume(tris, verts) < 0:
            tris = [(a, c, b) for a, b, c in tris]
        changed = sum(1 for x, y in zip(before, tris) if x != y)
        if changed:
            report.append(f"{name}: {changed} faces turned")
        replace[idx[0]] = [f"f {a} {b} {c}" for a, b, c in tris]
        for i in idx[1:]:
            replace[i] = []
    out = []
    for i, l in enumerate(lines):
        out += replace.get(i, [l])
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(os.path.basename(path), "|", "; ".join(report) or "already consistent")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        fix(p)
