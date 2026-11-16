"""Bone Forge splicing editor, the back end: a creature from one base species and any number of section swaps and
resizes, chained through splice.py. The testing ground's Splice screen (BFSpliceStage.cpp) runs this.

    python splice/compose.py preview <recipe.json> <out.json>    the rig only, for the live readout (about a second)
    python splice/compose.py build <recipe.json> <out.json>      and the body grown, written out like any species

recipe: {"base": "trex", "sections": {"Head": {"donor": "trike", "scale": 1.0}, "Hip": {"scale": 1.2}, ...}}
Sections: Head, Neck, Tail, Hip (legs), Shoulder (arms). No donor = the base's own section (resized).
"scale": "fit" sizes the part to the base's own section (fit_scale): the same bones and shape, at the base's size.
"thick": 1.2 widens a part's flesh and leaves its joints where they are: its mass and muscle go with the square.
"move": 0.2 slides legs or arms along the body, forward by 20% of the trunk's length (negative: back), after any swaps.
"add": {"pair": {"part": "Hip" or "Shoulder", "donor": "trex", "at": 0.5, "scale": "fit", "thick": 1.0}, "club": {"scale": "fit"}}
puts on a third pair of limbs (named thigh_2_l and so on), "at" of the way from hips to chest, and an Ankylosaurus' tail club.
"size": 0.3 grows the whole creature at that size (a young one, a small kind): every length times it, every area times
its square, every mass times its cube (density the same); its legs, gaits, swimming and climbing are worked out again
for the smaller body, so a youngster's strength, speed and footing are its own.
Legs and pillar arms walk (sized to reach the ground); wings make it a flier if it has none; other arms are carried.

A resized section is a part like any other, one the size asked for (density the same, so its mass goes with the cube),
so splice.py's rules hold: legs change the hips' height, a head rides on a longer neck, a wing's area goes with the
square. Swaps run legs first, arms last (the arms mount on the body as the other swaps left it).
"""
import hashlib
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import splice as sp  # noqa: E402

bfg, ROOT = sp.bfg, sp.ROOT
ORDER = ["Hip", "Neck", "Head", "Tail", "Shoulder"]
SECTION_WORD = {"Hip": "legs", "Neck": "neck", "Head": "head", "Tail": "tail", "Shoulder": "arms"}
SHORT = {"trex": "T. rex", "trike": "Triceratops", "raptor": "Velociraptor", "ankylo": "Ankylosaurus",
         "brachio": "Brachiosaurus", "ptera": "Pteranodon"}
KEY_LIMIT = 48
MOVING = ("Hip", "Shoulder")    # the sections that slide along the body
MOVE_LIMITS = (-1.0, 1.0)       # of the trunk's length
SCALE_LIMITS = (0.03, 4.0)
SIZE_LIMITS = (0.1, 3.0)          # the whole creature
THICK_LIMITS = (0.5, 3.0)
MISMATCH = 3.0            # a part this many times heavier or lighter than the section it replaces is flagged
UNDERGROUND_CM = 10.0     # a joint further under the ground than this is flagged (a limb too long for the body)
TIP_SHARE = 0.02          # weight further outside the feet than this share of its length tips it (as ABFDino does)


def fit_scale(base, donor, section):
    """The size at which a donor's section fits the base's: walking legs by the height of their socket off the ground,
    anything else by how far the section reaches from its root."""
    def rig_of(key):
        rig = sp.load(key)
        J = {j["name"]: j for j in rig["joints"]}
        taken = [n for n, j in J.items() if j["section"] == section]
        tops = [n for n in taken if J[n]["parent"] not in taken]
        return rig, J, taken, tops

    b, bj, bt, btops = rig_of(base)
    d, dj, dt, dtops = rig_of(donor)
    if not btops or not dtops:
        return 1.0
    walking = section == "Hip" or (section == "Shoulder" and b["body_tilt"]["kind"] == "quadruped"
                                   and d["body_tilt"]["kind"] == "quadruped" and not d.get("wing_area_m2"))
    if walking:
        return bj[btops[0]]["position_cm"][2] / max(dj[dtops[0]]["position_cm"][2], 1e-6)

    def reach(J, taken, top):
        a = J[top]["position_cm"]
        return max(math.dist(a, J[n]["position_cm"]) for n in taken)
    return reach(bj, bt, btops[0]) / max(reach(dj, dt, dtops[0]), 1e-6)


def size_of(recipe):
    return max(min(float(recipe.get("size", 1.0)), SIZE_LIMITS[1]), SIZE_LIMITS[0])


def scale_rig(node, size, key=""):
    """Every length (..._cm, ..._m) times size, every area (..._m2) times its square, every mass (..._kg) times its cube."""
    power = 3 if key.endswith("_kg") else 2 if key.endswith("_m2") else 1 if key.endswith(("_cm", "_m")) else 0
    if key == "tissue" and isinstance(node, list):
        # the flesh's stations: [t along the bone, across cm, up cm, half-width cm, half-height cm, shape, shape]
        return [[st[0]] + [round(v * size, 3) for v in st[1:5]] + list(st[5:]) for st in node]
    if isinstance(node, dict):
        return {k: scale_rig(v, size, k if power == 0 else key) for k, v in node.items()}
    if isinstance(node, list):
        return [scale_rig(v, size, key) for v in node]
    if isinstance(node, (int, float)) and not isinstance(node, bool) and power:
        return round(node * size ** power, 4)
    return node


def checked(recipe):
    """The swaps to run, in order: (section, donor species, scale, thickness). Sections left as they were drop out."""
    steps = []
    for sec in ORDER:
        c = recipe["sections"].get(sec) or {}
        edits = bone_edits(recipe, sec)
        if not c and not edits:
            continue
        donor = c.get("donor") or recipe["base"]
        scale = c.get("scale", 1.0)
        if scale == "fit":
            scale = round(fit_scale(recipe["base"], donor, sec), 2)
        scale = max(min(float(scale), SCALE_LIMITS[1]), SCALE_LIMITS[0])
        thick = max(min(float(c.get("thick", 1.0)), THICK_LIMITS[1]), THICK_LIMITS[0])
        if donor == recipe["base"] and abs(scale - 1.0) < 1e-3 and abs(thick - 1.0) < 1e-3 and not edits:
            continue
        steps.append((sec, donor, scale, thick))
    return steps


def moves_of(recipe):
    """{section: how far it slides, as a share of the trunk}, for the sections that move at all."""
    out = {}
    for sec in MOVING:
        m = float(recipe["sections"].get(sec, {}).get("move", 0.0))
        m = max(min(m, MOVE_LIMITS[1]), MOVE_LIMITS[0])
        if abs(m) >= 1e-3:
            out[sec] = m
    return out


def pair_of(recipe):
    """The extra limb pair asked for, tidied; None if there isn't one."""
    p = (recipe.get("add") or {}).get("pair")
    if not p or not p.get("donor") or p.get("part") not in MOVING:
        return None
    scale = p.get("scale", "fit")
    return {"part": p["part"], "donor": p["donor"], "scale": scale if scale == "fit" else max(min(float(scale), SCALE_LIMITS[1]), SCALE_LIMITS[0]),
            "thick": max(min(float(p.get("thick", 1.0)), THICK_LIMITS[1]), THICK_LIMITS[0]),
            "at": max(min(float(p.get("at", 0.5)), 1.0), 0.0)}


def club_of(recipe):
    c = (recipe.get("add") or {}).get("club")
    if c is None or c is False:
        return None
    scale = c.get("scale", "fit") if isinstance(c, dict) else "fit"
    return {"scale": scale if scale == "fit" else max(min(float(scale), SCALE_LIMITS[1]), SCALE_LIMITS[0])}


def creature_name(recipe):
    bits = []
    for sec, donor, scale, thick in checked(recipe):
        if donor == recipe["base"] and abs(scale - 1.0) < 1e-3 and abs(thick - 1.0) < 1e-3:
            continue              # only single bones changed in it (named below)
        size = "" if abs(scale - 1.0) < 1e-3 else f" {100 * scale:.0f}%"
        size += "" if abs(thick - 1.0) < 1e-3 else f" ({100 * thick:.0f}% thick)"
        bits.append(f"{SHORT[donor] + ' ' if donor != recipe['base'] else ''}{SECTION_WORD[sec]}{size}")
    for sec, m in moves_of(recipe).items():
        bits.append(f"{SECTION_WORD[sec]} {100 * abs(m):.0f}% {'forward' if m > 0 else 'back'}")
    roles = {j["name"]: j.get("role") or bone_label(j["name"]) for j in sp.load(recipe["base"])["joints"]}
    for sec in ORDER:
        for name, e in bone_edits(recipe, sec).items():
            paired = name.endswith("_l")
            bit = (SHORT[e["donor"]] + " " if e["donor"] else "") + bone_label(name) + ("s" if paired else "")
            if e["donor"] and e["fit"]:
                bit += " (fitted)"
            if abs(e["scale"] - 1.0) >= 1e-3:
                bit += f" {100 * e['scale']:.0f}%"
            if abs(e["move"]) >= 1e-3:
                role = roles.get(name, "joint").replace(" socket", "")
                # minus slides it toward the bone above's root: up a leg or arm, in toward the body on a neck or tail
                way = ("lower" if e["move"] > 0 else "higher") if sec in ("Hip", "Shoulder") else \
                      ("moved out" if e["move"] > 0 else "moved in")
                bit += f" ({role}{'s' if paired else ''} {100 * abs(e['move']):.0f}% {way})"
            bits.append(bit)
    pair = pair_of(recipe)
    if pair:
        bits.append(f"extra {SHORT[pair['donor']]} {'wings' if sp.load(pair['donor']).get('wing_area_m2') and pair['part'] == 'Shoulder' else SECTION_WORD[pair['part']]}")
    if club_of(recipe):
        bits.append("a tail club")
    size = size_of(recipe)
    who = ("Young " if size < 0.6 else f"{100 * size:.0f}% ") + SHORT[recipe["base"]] if abs(size - 1.0) >= 1e-3 else SHORT[recipe["base"]]
    return who + (" with " + ", ".join(bits) if bits else "")


def creature_key(recipe):
    bits = [recipe["base"]]
    for sec, donor, scale, thick in checked(recipe):
        if donor == recipe["base"] and abs(scale - 1.0) < 1e-3 and abs(thick - 1.0) < 1e-3:
            continue
        bits.append(sec[:2].lower() + (donor if donor != recipe["base"] else "") + (
            "" if abs(scale - 1.0) < 1e-3 else str(round(100 * scale))) + (
            "" if abs(thick - 1.0) < 1e-3 else f"t{round(100 * thick)}"))
    for sec, m in moves_of(recipe).items():
        bits.append(f"m{sec[:2].lower()}{'f' if m > 0 else 'b'}{round(100 * abs(m))}")
    for sec in ORDER:
        for name, e in bone_edits(recipe, sec).items():
            code = name[:-2] if name.endswith(("_l", "_r")) else name
            bits.append("b" + code.replace("_", "") + (e["donor"] or "") + ("f" if e["donor"] and e["fit"] else "")
                        + ("" if abs(e["scale"] - 1.0) < 1e-3 else str(round(100 * e["scale"])))
                        + ("" if abs(e["move"]) < 1e-3 else f"m{round(100 * e['move'])}"))
    pair, club = pair_of(recipe), club_of(recipe)
    if pair:
        bits.append(f"x{pair['part'][:2].lower()}{pair['donor']}{round(100 * pair['at'])}" + (
            "" if pair["scale"] == "fit" else f"s{round(100 * pair['scale'])}") + (
            "" if abs(pair["thick"] - 1.0) < 1e-3 else f"t{round(100 * pair['thick'])}"))
    if club:
        bits.append("club" + ("" if club["scale"] == "fit" else str(round(100 * club["scale"]))))
    size = size_of(recipe)
    if abs(size - 1.0) >= 1e-3:
        bits.append(f"z{round(100 * size)}")
    key = "ed_" + "_".join(bits)
    # An imported mesh's path repeats the key three times and Windows paths stop at 260 characters.
    return key if len(key) <= KEY_LIMIT else f"ed_{recipe['base']}_{hashlib.md5(key.encode()).hexdigest()[:12]}"


def make_part(species, section, scale, thick, with_parts):
    """A species' rig (and bone parts) with one section resized and thickened: another donor to splice.py."""
    rig = sp.load(species)
    key = f"_ed_part{os.getpid()}_{species}_{section}_{round(100 * scale)}_{round(100 * thick)}"
    J = {j["name"]: j for j in rig["joints"]}
    old = {n: tuple(j["position_cm"]) for n, j in J.items()}
    taken = {n for n, j in J.items() if j["section"] == section}
    anchors = {}                      # a section's top joint -> the point it grows from
    for n in taken:
        if J[n]["parent"] not in taken:
            # Legs that walk stay on the ground: they grow from under their socket.
            anchors[n] = (old[n][0], old[n][1], 0.0) if walks_on(rig, section) else old[n]

    def top_of(n):
        while n not in anchors:
            n = J[n]["parent"]
        return n
    delta = {}
    for j in rig["joints"]:           # parents come first
        n = j["name"]
        if n in taken:
            a = anchors[top_of(n)]
            j["position_cm"] = [round(a[k] + scale * (old[n][k] - a[k]), 2) for k in range(3)]
            delta[n] = tuple(j["position_cm"][k] - old[n][k] for k in range(3))
            if j.get("tissue"):
                k_ = scale * thick
                # (a station's squareness, its 6th and 7th numbers when it has them, doesn't change with size)
                j["tissue"] = [[s[0], s[1] * k_, s[2] * k_, s[3] * k_, s[4] * k_] + list(s[5:]) for s in j["tissue"]]
        elif j.get("parent") in taken:
            # The joint the section's last bone ends at (the head on a neck) is that bone's end: it scales with the
            # section. Riding along unscaled, a Brachiosaurus neck fitted to a raptor (3%) kept its last bone's full
            # metre, and the head sat 1 m from the neck.
            a = anchors[top_of(j["parent"])]
            j["position_cm"] = [round(a[k] + scale * (old[n][k] - a[k]), 2) for k in range(3)]
            delta[n] = tuple(j["position_cm"][k] - old[n][k] for k in range(3))
        elif j.get("parent") in delta:   # what hangs off the section beyond that rides along
            delta[n] = delta[j["parent"]]
            j["position_cm"] = [round(old[n][k] + delta[n][k], 2) for k in range(3)]
    for c in rig["collision_capsules"]:
        if c["from_joint"] in taken:
            c["radius_cm"] = round(c["radius_cm"] * scale * thick, 2)
            c["length_cm"] = round(c["length_cm"] * scale, 2)
            c["mass_kg"] = round(c["mass_kg"] * scale ** 3 * thick ** 2, 3)
    by = rig["mass_kg"]["by_section"]
    k = next(k for k in by if k.startswith(section))
    change = by[k] * (scale ** 3 * thick ** 2 - 1.0)
    by[k] = round(by[k] + change, 2)
    rig["mass_kg"]["total"] = round(rig["mass_kg"]["total"] + change, 2)
    if section == "Shoulder" and rig.get("wing_area_m2"):
        rig["wing_area_m2"] = round(rig["wing_area_m2"] * scale ** 2, 3)
        fp = rig["flight_pose_cm"]
        was = {n: list(v) for n, v in fp.items()}
        for n in fp:
            if n in taken:
                t = was[top_of(n)]
                fp[n] = [round(t[i] + scale * (was[n][i] - t[i]), 2) for i in range(3)]
    rig["part_of"] = species
    sp.save_json(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), rig)
    # Always written: splice.py reads a donor's part names for the armour note.
    verts, parts = bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{species}_features.obj"))
    to_ue, from_ue = (lambda v: bfg.m_vec(bfg.UE_FROM_OBJ, v)), bfg.m_inv(bfg.UE_FROM_OBJ)
    lines, count = [], 0
    for name, tris in parts:
        used = sorted({i for t in tris for i in t})
        if with_parts and sp.feature_section(name) == section:
            P = [to_ue(verts[i]) for i in used]
            cy = sum(p[1] for p in P) / len(P)
            tops = list(anchors)
            a = anchors[tops[0] if len(tops) == 1 else next(t for t in tops if t.endswith("_l" if cy < 0 else "_r"))]
            side = "_l" if cy < 0 else "_r"
            top = tops[0] if len(tops) == 1 else next(t for t in tops if t.endswith(side))
            Q = [tuple(a[k] + scale * (p[k] - a[k]) for k in range(3)) for p in P]
            if abs(thick - 1.0) >= 1e-3:
                Q = thicken(Q, J, taken, top, side, thick, walks_on(rig, section))
            new = {i: bfg.m_vec(from_ue, q) for i, q in zip(used, Q)}
        else:
            new = {i: verts[i] for i in used}
        remap = {}
        lines.append(f"o {name}")
        for i in used:
            count += 1
            remap[i] = count
            lines.append("v {:.2f} {:.2f} {:.2f}".format(*new[i]))
        lines += ["f " + " ".join(str(remap[i]) for i in t) for t in tris]
    with open(os.path.join(ROOT, "body_shapes", f"{key}_features.obj"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return key


# Advanced mode: single bones inside a section (the design doc's swap levels: "open a section and swap or move single
# bones inside it"). recipe "bones": {"calf_l": {"donor": "trike", "scale": 1.0, "fit": true, "move": 0.0}, ...}, the
# left one of a pair standing for both. Only grown bones (flesh from their tissue) that end in a joint of their own:
# feet, hands and skulls are bone parts and swap as whole sections.
BONE_SCALE_LIMITS = (0.25, 3.0)
BONE_MOVE_LIMITS = (-0.4, 0.4)     # a joint slides this share of the bone above it
BONE_WORDS = {"thigh": "thigh", "calf": "shin", "upperarm": "upper arm", "lowerarm": "forearm", "hand": "hand",
              "neck": "neck bone", "tail": "tail bone"}
ADVANCED = ("Neck", "Tail", "Hip", "Shoulder")


def bone_label(name):
    """A bone's name as the editor shows it: calf_l -> shin, neck_02 -> neck bone 2."""
    base = name[:-2] if name.endswith(("_l", "_r")) else name
    kind, _, num = base.partition("_")
    if kind in ("neck", "tail") and num.isdigit():
        return f"{BONE_WORDS[kind]} {int(num)}"
    return BONE_WORDS.get(kind, base.replace("_", " "))


def mirror(name):
    return name[:-2] + "_r" if name.endswith("_l") else name[:-2] + "_l" if name.endswith("_r") else None


def first_children(rig):
    """{joint: its children in the rig's order}; a bone's first child is the joint it points at."""
    out = {}
    for j in rig["joints"]:
        out.setdefault(j.get("parent"), []).append(j["name"])
    return out


def editable_bones(rig, section):
    """A section's bones Advanced mode can swap, size and move: grown ones (with tissue and limits), not an added
    pair's, each ending in a joint of its own; the left one of each pair."""
    kids = first_children(rig)
    return [j["name"] for j in rig["joints"] if j["section"] == section and not j["name"].endswith("_r")
            and "_2_" not in j["name"] and j.get("tissue") and j.get("limits_deg") and kids.get(j["name"])]


SECTION_BONES = {"Neck": ("neck_",), "Tail": ("tail_",), "Hip": ("thigh", "calf"), "Shoulder": ("upperarm", "lowerarm", "hand")}


def bone_section(name):
    return next((sec for sec, starts in SECTION_BONES.items() if name.startswith(starts)), None)


def bone_edits(recipe, section):
    """The single-bone edits asked for in a section, tidied: {bone: {donor, scale, fit, move}}."""
    out = {}
    for name, e in sorted((recipe.get("bones") or {}).items()):
        if bone_section(name) != section or "_2_" in name or name.endswith("_r"):
            continue
        donor = e.get("donor") or None
        scale = max(min(float(e.get("scale", 1.0)), BONE_SCALE_LIMITS[1]), BONE_SCALE_LIMITS[0])
        move = max(min(float(e.get("move", 0.0)), BONE_MOVE_LIMITS[1]), BONE_MOVE_LIMITS[0])
        fit = bool(e.get("fit", False))
        if donor is None and abs(scale - 1.0) < 1e-3 and abs(move) < 1e-3:
            continue
        out[name] = {"donor": donor, "scale": scale, "fit": fit, "move": move}
    return out


def edit_bones(source, section, edits, with_parts):
    """A copy of source (a species or a resized part) with single bones of one section swapped, resized or moved: a donor
    for splice.py like make_part's. A swapped bone takes the donor's length (or, fitted, keeps its own), the donor's
    flesh (tissue) and motion limits, and its capsule's weight, at the size asked for (density the same: mass with the
    cube); the joints below it ride along, and walking legs are set back on the ground (their socket moves, and
    splice.py lifts or tilts the body to it). Move slides a joint along the bone above it (a knee up or down the thigh):
    the bones either side change length, the joints below stay. Returns (key, notes)."""
    rig = sp.load(source)
    key = f"_ed_bones{os.getpid()}_{source}_{section}"
    J = {j["name"]: j for j in rig["joints"]}
    kids = first_children(rig)
    order = [j["name"] for j in rig["joints"]]
    fp = rig.get("flight_pose_cm") or {}
    caps = {c["from_joint"]: c for c in rig["collision_capsules"]}
    by_key = next(k for k in rig["mass_kg"]["by_section"] if k.startswith(section))
    old = {n: tuple(J[n]["position_cm"]) for n in J}
    moved = {n: [0.0, 0.0, 0.0] for n in J}
    notes, change = [], 0.0

    def P(n):
        return J[n]["position_cm"]

    def sub(n):
        out, todo = [], [n]
        while todo:
            m = todo.pop()
            out.append(m)
            todo += kids.get(m, [])
        return out

    def shift(names, d, pose=None):
        for m in names:
            if pose is None:
                J[m]["position_cm"] = [round(P(m)[k] + d[k], 2) for k in range(3)]
                moved[m] = [moved[m][k] + d[k] for k in range(3)]
            elif m in pose:
                pose[m] = [round(pose[m][k] + d[k], 2) for k in range(3)]

    def unit(a, b):
        d = [b[k] - a[k] for k in range(3)]
        L = math.sqrt(sum(c * c for c in d))
        return [c / max(L, 1e-9) for c in d], L

    def capsule_of(n):
        c = caps.get(n)
        if c is None and n.endswith("_r"):
            return None, 0                            # a pair's capsule is the left one's, counted twice
        return c, (2 if c and "(l, r)" in c["bone"] else 1)

    def scale_capsule(n, length_ratio, flesh):
        nonlocal change
        c, times = capsule_of(n)
        if not c:
            return
        before = c["mass_kg"]
        c["length_cm"] = round(c["length_cm"] * length_ratio, 2)
        c["radius_cm"] = round(c["radius_cm"] * flesh, 2)
        c["mass_kg"] = round(c["mass_kg"] * length_ratio * flesh * flesh, 3)
        change += (c["mass_kg"] - before) * times

    span0 = None
    if section == "Shoulder" and rig.get("wing_area_m2"):
        span0 = sum(unit(old[n], old[kids[n][0]])[1] for n in J if J[n]["section"] == section and n.endswith("_l") and kids.get(n))
    want = {}
    for name, e in edits.items():
        want[name] = e
        if mirror(name) in J:
            want[mirror(name)] = e
    for n in [m for m in order if m in want]:     # parents first
        e = want[n]
        if J[n]["section"] != section or not kids.get(n) or not J[n].get("tissue"):
            if not n.endswith("_r"):
                notes.append(f"this {SECTION_WORD[section]} has no {bone_label(n)} of its own to change")
            continue
        c = kids[n][0]
        # Move: the joint slides along the bone above it; the bones either side stretch, those below stay.
        par = J[n]["parent"]
        if abs(e["move"]) >= 1e-3 and par in J and J[par]["section"] == section:
            u, Lp = unit(P(par), P(n))
            Lc0 = unit(P(n), P(c))[1]
            d = [e["move"] * Lp * u[k] for k in range(3)]
            shift([n], d)
            if fp and par in fp and n in fp:
                uf, Lpf = unit(fp[par], fp[n])
                shift([n], [e["move"] * Lpf * uf[k] for k in range(3)], fp)
            scale_capsule(par, unit(P(par), P(n))[1] / max(Lp, 1e-9), 1.0)
            scale_capsule(n, unit(P(n), P(c))[1] / max(Lc0, 1e-9), 1.0)
        # Swap and size: the bone's length, flesh, limits and weight.
        u, Lb = unit(P(n), P(c))
        donor = e["donor"] if e["donor"] and e["donor"] != rig.get("part_of", source) else None
        if donor:
            D = sp.load(donor)
            DJ = {j["name"]: j for j in D["joints"]}
            dk = first_children(D).get(n)
            if n not in DJ or not dk or not DJ[n].get("tissue"):
                notes.append(f"the {SHORT[donor]} has no {bone_label(n)} to give")
                donor = None
        # Size is the bone's length: its flesh keeps its girth (a single bone at 150% that also grew 50% thicker made a
        # bulge in the middle of a neck; the section's Thick is for girth), and its mass goes with its length. A
        # swapped bone brings the donor's girth, in proportion when it is fitted to this one's length.
        if donor:
            Ld = unit(DJ[n]["position_cm"], DJ[dk[0]]["position_cm"])[1]
            kf = Lb / max(Ld, 1e-9) if e["fit"] else 1.0
            L = (Lb if e["fit"] else Ld) * e["scale"]
            J[n]["tissue"] = [[s[0], s[1] * kf, s[2] * kf, s[3] * kf, s[4] * kf] + list(s[5:]) for s in DJ[n]["tissue"]]
            J[n]["limits_deg"] = DJ[n]["limits_deg"]
            J[n]["lineage"] = D["creature"]
            c_own, times = capsule_of(n)
            c_d = next((cc for cc in D["collision_capsules"] if cc["from_joint"] == n), None)
            if c_own and c_d:
                before = c_own["mass_kg"]
                c_own.update(radius_cm=round(c_d["radius_cm"] * kf, 2), length_cm=round(L, 2),
                             mass_kg=round(c_d["mass_kg"] * (L / max(Ld, 1e-9)) * kf * kf, 3))
                change += (c_own["mass_kg"] - before) * times
        else:
            L = Lb * e["scale"]
            if abs(L - Lb) >= 1e-3:
                scale_capsule(n, e["scale"], 1.0)
        if abs(L - Lb) >= 1e-3:
            shift(sub(c), [(L - Lb) * u[k] for k in range(3)])
            if fp and n in fp and c in fp:
                uf, Lbf = unit(fp[n], fp[c])
                shift([m for m in sub(c) if m in fp], [(L * Lbf / max(Lb, 1e-9) - Lbf) * uf[k] for k in range(3)], fp)
    # Walking legs back on the ground: the whole section up or down by how far its lowest joint left it.
    if walks_on(rig, section):
        mine = [n for n in J if J[n]["section"] == section and "_2_" not in n]
        lo0, lo1 = min(old[n][2] for n in mine), min(P(n)[2] for n in mine)
        if abs(lo1 - lo0) >= 0.5:
            shift(mine, [0.0, 0.0, lo0 - lo1])
            if fp:
                shift(mine, [0.0, 0.0, lo0 - lo1], fp)
            notes.append(f"its {SECTION_WORD[section]} stand {abs(lo0 - lo1):.0f} cm {'taller' if lo0 > lo1 else 'shorter'} at the socket")
    rig["mass_kg"]["by_section"][by_key] = round(rig["mass_kg"]["by_section"][by_key] + change, 2)
    rig["mass_kg"]["total"] = round(rig["mass_kg"]["total"] + change, 2)
    if span0:
        span1 = sum(unit(P(n), P(kids[n][0]))[1] for n in J if J[n]["section"] == section and n.endswith("_l") and kids.get(n))
        rig["wing_area_m2"] = round(rig["wing_area_m2"] * span1 / max(span0, 1e-9), 3)
    rig["part_of"] = rig.get("part_of", source)
    sp.save_json(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), rig)
    # Its bone parts (feet, the folded wing's feathers): each rides the joint nearest it.
    verts, parts = bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{source}_features.obj"))
    to_ue, from_ue = (lambda v: bfg.m_vec(bfg.UE_FROM_OBJ, v)), bfg.m_inv(bfg.UE_FROM_OBJ)
    mine = [n for n in J if J[n]["section"] == section]
    lines, count = [], 0
    for name, tris in parts:
        used = sorted({i for t in tris for i in t})
        d = None
        if with_parts and sp.feature_section(name) == section and mine:
            Q = [to_ue(verts[i]) for i in used]
            cen = [sum(q[k] for q in Q) / len(Q) for k in range(3)]
            near = min(mine, key=lambda n: math.dist(old[n], cen))
            d = bfg.m_vec(from_ue, tuple(moved[near]))
        lines.append(f"o {name}")
        remap = {}
        for i in used:
            count += 1
            remap[i] = count
            v = verts[i] if d is None else tuple(verts[i][k] + d[k] for k in range(3))
            lines.append("v {:.2f} {:.2f} {:.2f}".format(*v))
        lines += ["f " + " ".join(str(remap[i]) for i in t) for t in tris]
    with open(os.path.join(ROOT, "body_shapes", f"{key}_features.obj"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return key, notes


def walks_on(rig, section):
    return section == "Hip" or (section == "Shoulder" and rig["body_tilt"]["kind"] == "quadruped" and not rig.get("wing_area_m2"))


def thicken(Q, J, taken, top, side, thick, walks):
    """A part's points moved out from its bones, along them unchanged. On a leg: square to the shin down to the ankle
    (so the cut across the shin stays flat), then level from there, so the foot widens and stays on the ground. On
    anything else: square to the line from its root to the furthest of its joints."""
    def v(n):
        return tuple(J[n]["position_cm"])
    ankle = next((n for n in (f"foot{side}", f"hand{side}") if n in J and n in taken), None)
    if walks and ankle:
        a, b = v(J[ankle]["parent"]), v(ankle)
    else:
        a = v(top)
        b = max((v(n) for n in taken), key=lambda p: math.dist(a, p))
    ab = tuple(b[k] - a[k] for k in range(3))
    L2 = max(sum(c * c for c in ab), 1e-9)
    out = []
    for p in Q:
        t = sum((p[k] - a[k]) * ab[k] for k in range(3)) / L2
        if walks and ankle and t >= 1.0:          # the foot: wider, on the same ground
            out.append((b[0] + thick * (p[0] - b[0]), b[1] + thick * (p[1] - b[1]), p[2]))
        else:
            c = tuple(a[k] + t * ab[k] for k in range(3))
            out.append(tuple(c[k] + thick * (p[k] - c[k]) for k in range(3)))
    return out


def cleanup(made):
    for key in made:
        for path in (os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), os.path.join(ROOT, "body_shapes", f"{key}_features.obj")):
            if os.path.exists(path):
                os.remove(path)


def trunk_line(J):
    """The spine's joints from hips to chest, as (name, x, z)."""
    names = ["pelvis"] + sorted(n for n in J if n.startswith("spine_")) + ["chest"]
    return [(n, J[n]["position_cm"][0], J[n]["position_cm"][2]) for n in names if n in J]


def spine_z(line, x):
    if x <= line[0][1]:
        return line[0][2]
    for (_, x0, z0), (_, x1, z1) in zip(line, line[1:]):
        if x <= x1:
            return z0 + (z1 - z0) * (x - x0) / max(x1 - x0, 1e-6)
    return line[-1][2]


def apply_moves(rig, moves, notes, flags):
    """Slide limbs along the body: {section: (dx, dz)} as they moved. Walking limbs keep their height, so their feet
    stay on the ground; carried ones ride the spine up and down. The flesh starts on the spine at the new place (the
    grown body's mount), so a limb moved under a higher back still joins it, and the limb hangs from the spine bone
    nearest it, so it follows the body's bending from there."""
    J = {j["name"]: j for j in rig["joints"]}
    line = trunk_line(J)
    length = max(line[-1][1] - line[0][1], 1e-6)
    quad = rig["body_tilt"]["kind"] == "quadruped"
    shifts = {}
    for sec in MOVING:
        if sec not in moves:
            continue
        taken = [n for n in J if J[n]["section"] == sec]
        tops = [n for n in taken if J[J[n]["parent"]]["section"] == "Core"]
        if not tops:
            continue
        x0 = J[tops[0]]["position_cm"][0]
        lo, hi = line[0][1] - 0.15 * length, line[-1][1] + 0.15 * length
        other = next((n for n in J if n in ("upperarm_l", "thigh_l") and J[n]["section"] != sec), None)
        if quad and other:       # front and back legs keep their order
            ox = J[other]["position_cm"][0]
            if sec == "Hip":
                hi = min(hi, ox - 0.2 * length)
            else:
                lo = max(lo, ox + 0.2 * length)
        x1 = min(max(x0 + moves[sec] * length, lo), hi)
        if abs(x1 - x0 - moves[sec] * length) > 1.0:
            flags.append(f"The {SECTION_WORD[sec]} go no further than {abs(x1 - x0) / length:.0%} of the body's length that way.")
        dx = x1 - x0
        walking = sec == "Hip" or quad
        dz = 0.0 if walking else spine_z(line, x1) - spine_z(line, x0)
        carrier = min(line, key=lambda p: abs(p[1] - x1))[0]
        for n in taken:
            p = J[n]["position_cm"]
            J[n]["position_cm"] = [round(p[0] + dx, 1), p[1], round(p[2] + dz, 1)]
        for t in tops:
            old = J[t].get("mount_from_cm")
            y = old[1] if old else 0.5 * J[t]["position_cm"][1]
            J[t]["parent"] = carrier
            # Inside the trunk, just under the spine (under a long body the two are far apart).
            J[t]["mount_from_cm"] = [round(x1, 1), round(y, 1), round(spine_z(line, x1), 1)]
        # Parents first, with a limb after the spine and neck: a bone's first child is what it points at.
        rig["joints"] = [j for j in rig["joints"] if j["name"] not in taken] + [j for j in rig["joints"] if j["name"] in taken]
        fp = rig.get("flight_pose_cm")
        if fp:
            for n in taken:
                if n in fp:
                    fp[n] = [round(fp[n][0] + dx, 1), fp[n][1], round(fp[n][2] + dz, 1)]
        shifts[sec] = (dx, dz)
        notes.append(f"the {SECTION_WORD[sec]} sit {abs(dx):.0f} cm {'forward' if dx > 0 else 'back'}, hung from the {carrier.replace('_', ' ')}")
    return shifts


def balance_of(rig):
    """The balance point from the capsules as they sit (as in the splice)."""
    pos = {j["name"]: j["position_cm"] for j in rig["joints"]}
    acc, total = [0.0, 0.0, 0.0], 0.0
    for c in rig["collision_capsules"]:
        for sd in (["_l", "_r"] if "(l, r)" in c["bone"] else [""]):
            a = c["from_joint"][:-2] + sd if sd else c["from_joint"]
            b = c["to_joint"][:-2] + sd if sd else c["to_joint"]
            if a in pos and b in pos:
                acc = [s + c["mass_kg"] * 0.5 * (p + q) for s, p, q in zip(acc, pos[a], pos[b])]
                total += c["mass_kg"]
    return [round(a / max(total, 1e-9), 1) for a in acc]


def renamed(n):
    """A joint of the extra pair: thigh_l -> thigh_2_l (still a thigh and still on the left to every rule that asks)."""
    return n[:-2] + "_2" + n[-2:]


def add_pair(rig, pair, key, notes, flags, made, build):
    """A third pair of limbs on the body: joints, capsules, flight if they're the first wings. Returns the pair's bone
    parts [(name, {index: OBJ point}, tris, donor)] for the features file."""
    J = {j["name"]: j for j in rig["joints"]}
    line = trunk_line(J)
    length = max(line[-1][1] - line[0][1], 1e-6)
    x = line[0][1] + pair["at"] * length
    sec, donor_key = pair["part"], pair["donor"]
    donor = sp.load(donor_key)
    wings = sec == "Shoulder" and bool(donor.get("wing_area_m2"))
    walking = sec == "Hip" or (donor["body_tilt"]["kind"] == "quadruped" and not wings)
    tmp = f"_ed_tmp_{key}"
    tmp_path = os.path.join(ROOT, "rig_joints", f"{tmp}_rig.json")
    sp.save_json(tmp_path, rig)
    made.append(tmp)
    skin = sp.BodyWidth.from_rig(tmp_path, sec, trunk_only=True)
    # Under the spine as far as the animal's own limbs of that kind are, on its flank.
    ref = next((n for n in (("thigh_l", "upperarm_l") if sec == "Hip" or (walking and rig["body_tilt"]["kind"] != "quadruped")
                            else ("upperarm_l", "thigh_l")) if n in J), None)
    drop = spine_z(line, J[ref]["position_cm"][0]) - J[ref]["position_cm"][2] if ref else 0.0
    socket_z = max(spine_z(line, x) - drop, 1.0)
    y = float(max(abs(J[ref]["position_cm"][1]) if ref else 0.0, 0.8 * skin.widest(x)))
    rig_d = {j["name"]: j for j in donor["joints"]}
    top_d = next(n for n, j in rig_d.items() if j["section"] == sec and j["name"].endswith("_l") and rig_d[j["parent"]]["section"] != sec)
    if walking:
        fit = socket_z / max(rig_d[top_d]["position_cm"][2], 1e-6)
    else:
        dline = trunk_line(rig_d)
        fit = length / max(dline[-1][1] - dline[0][1], 1e-6)
    scale = fit if pair["scale"] == "fit" else pair["scale"]
    if walking and scale > fit * 1.02:
        flags.append(f"Those {SECTION_WORD[sec]} can't be longer than the body is high: held at {100 * fit:.0f}%.")
        scale = fit
    elif walking and scale < fit * 0.9:
        flags.append(f"Those {SECTION_WORD[sec]} hang clear of the ground: carried, not walked on.")
        walking = False
    scale = round(max(min(scale, SCALE_LIMITS[1]), SCALE_LIMITS[0]), 2)
    if walking:
        # Clear of the animal's own walking legs: a pair set so close that its flesh overlapped theirs grew into them,
        # an untidy seam between the two. It slides along the body to the nearest place its thigh just clears theirs.
        def reach(tissue, k=1.0):
            return k * max((max(s[3], s[4]) for s in tissue or []), default=0.0)
        r_new = reach(rig_d[top_d].get("tissue"), scale * pair["thick"])
        own = [(J[n]["position_cm"][0], reach(J[n].get("tissue"))) for n in ("thigh_l", "upperarm_l")
               if n in J and (n == "thigh_l" or rig["body_tilt"]["kind"] == "quadruped")]
        bad = [(xo - (r_new + ro + 2.0), xo + (r_new + ro + 2.0)) for xo, ro in own]
        lo_x, hi_x = line[0][1], line[-1][1]
        ok = [c for c in [x] + [e for b in bad for e in b] if lo_x <= c <= hi_x and not any(a < c < b for a, b in bad)]
        if ok and min(ok, key=lambda c: abs(c - x)) != x:
            x_new = min(ok, key=lambda c: abs(c - x))
            notes.append(f"the extra {SECTION_WORD[sec]} sit {abs(x_new - x):.0f} cm further "
                         f"{'forward' if x_new > x else 'back'}, clear of its own")
            x = x_new
            y = float(max(abs(J[ref]["position_cm"][1]) if ref else 0.0, 0.8 * skin.widest(x)))
        elif not ok:
            flags.append(f"The extra {SECTION_WORD[sec]} have no room between its own: their flesh grows into theirs.")
        socket_z = scale * rig_d[top_d]["position_cm"][2]    # exactly what the feet need
    part = donor_key
    if abs(scale - 1.0) >= 1e-3 or abs(pair["thick"] - 1.0) >= 1e-3:
        part = make_part(donor_key, sec, scale, pair["thick"], build)
        made.append(part)
    part_rig = sp.load(part)
    _, donor_j, donor_topo = bfg.load_rig(os.path.join(ROOT, "rig_joints", f"{part}_rig.json"))
    frames = bfg.bone_frames(donor_j, donor_topo)
    taken = [n for n in donor_topo if donor_j[n].section == sec]
    tops = [n for n in taken if donor_j[n].parent not in taken]
    carrier = min(line, key=lambda p: abs(p[1] - x))[0]
    width = None
    pos, turns, place, biped = {}, {}, {}, rig["body_tilt"]["kind"] != "quadruped"
    for top in tops:
        side = sp.subtree(donor_j, top)
        sign = 1.0 if top.endswith("_r") else -1.0
        socket = (x, sign * y, socket_z)
        bends = {}
        if sec == "Shoulder" and not walking and donor["body_tilt"]["kind"] == "quadruped":
            if width is None:
                width = sp.BodyWidth.from_rig(tmp_path, sec)
            chain = sp.bend_chain(donor_j, top)
            memo = sp.hashlib.md5(json.dumps(["pair", top, socket, [(n, donor_j[n].pos) for n in side],
                                              [(n, j.pos) for n, j in bfg.load_rig(tmp_path)[1].items()]]).encode()).hexdigest()
            folds = sp.load_folds()
            if memo in folds:
                bends = {n: tuple(v) for n, v in folds[memo].items()}
            else:
                bends = sp.carried_fold(donor_j, frames, side, chain, socket, bfg.load_rig(tmp_path)[1], width)
                folds[memo] = bends
                sp.save_json(sp.FOLD_MEMO, folds)
        P, Rt = sp.Fold(donor_j, frames, side).apply(bends)
        off = bfg.v_sub(socket, donor_j[top].pos)
        for n in side:
            pos[n] = tuple(round(c + o, 1) for c, o in zip(P[n], off))
        place[top] = (bends, off, P, Rt)
        turns.update(bends)
    new = []
    donor_name = donor["creature"]
    for j in part_rig["joints"]:
        n = j["name"]
        if n not in taken:
            continue
        j = json.loads(json.dumps(j))
        j["name"], j["position_cm"], j["lineage"] = renamed(n), list(pos[n]), donor_name
        if n in tops:
            j["parent"] = carrier
            j["mount_from_cm"] = [round(x, 1), round(0.5 * pos[n][1], 1), round(spine_z(line, x), 1)]
        else:
            j["parent"] = renamed(j["parent"])
        turn = turns.get(n)
        if turn and any(turn) and j.get("limits_deg"):
            for axis, a in zip(("bend", "side"), turn):
                lo, hi = j["limits_deg"][axis]
                j["limits_deg"][axis] = [round(lo - a, 1), round(hi - a, 1)]
        new.append(j)
    rig["joints"] += new
    kg = 0.0
    for c in part_rig["collision_capsules"]:
        if c["from_joint"] in taken:
            c = dict(c, bone=c["bone"].replace(" (l, r)", "_2 (l, r)"), from_joint=renamed(c["from_joint"]),
                     to_joint=renamed(c["to_joint"]) if c["to_joint"] in taken else c["to_joint"])
            rig["collision_capsules"].append(c)
            kg += c["mass_kg"] * (2 if "(l, r)" in c["bone"] else 1)
    by = rig["mass_kg"]["by_section"]
    by["Extra pair"] = round(by.get("Extra pair", 0.0) + kg, 2)
    rig["mass_kg"]["total"] = round(rig["mass_kg"]["total"] + kg, 2)
    low, mid = ("foot", "calf") if sec == "Hip" else ("hand", "lowerarm")
    rig["extra_pair"] = {"upper": "thigh_2" if sec == "Hip" else "upperarm_2", "mid": mid + "_2", "low": low + "_2", "walks": walking,
                         "phase": 0.125 if rig["body_tilt"]["kind"] == "quadruped" else 0.25, "scale": scale}
    notes.append(f"a third pair of {'wings' if wings else SECTION_WORD[sec]}, {100 * scale:.0f}% size, hung from the {carrier.replace('_', ' ')}: "
                 + ("they walk" if walking else "carried" if not wings else "folded at rest"))
    if wings:
        if rig.get("wing_area_m2"):
            flags.append("It already has wings: the second pair stays folded and adds no lift.")
        else:
            fp = part_rig["flight_pose_cm"]
            flight = {}
            for top in tops:
                shift = bfg.v_sub(pos[top], tuple(fp[top]))
                for n in sp.subtree(donor_j, top):
                    if n in fp:
                        flight[renamed(n)] = [round(c + s, 1) for c, s in zip(fp[n], shift)]
            rig["flight_pose_cm"] = flight
            rig["flight_rule"] = donor.get("flight_rule")
            rig["wing_area_m2"] = part_rig["wing_area_m2"]
            rig["wing_pair"] = "_2"
            thigh = J.get("thigh_l")
            if thigh:
                rig["membrane_hip_offset_cm"] = [0.0, -round(max(0.0, skin.widest(thigh["position_cm"][0]) - 2.0 - abs(thigh["position_cm"][1])), 1), 0.0]
    parts = []
    if build:
        verts, dparts = bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{part}_features.obj"))
        to_ue, from_ue = (lambda v: bfg.m_vec(bfg.UE_FROM_OBJ, v)), bfg.m_inv(bfg.UE_FROM_OBJ)
        for name, tris in dparts:
            if sp.feature_section(name) != sec or sp.part_base(name).startswith("membrane"):
                continue
            idx = sorted({i for t in tris for i in t})
            P = [to_ue(verts[i]) for i in idx]
            cen = tuple(sum(p[k] for p in P) / len(P) for k in range(3))
            top = next(t for t in tops if t.endswith("_l" if cen[1] < 0 else "_r"))
            bends, off, Pnew, Rt = place[top]
            if bends:
                own, rigid = bfg.part_bones(name, cen, donor_j)
                weights = bfg.skin_part(P, own, donor_j, rigid)
                moved = {}
                for i, p, w in zip(idx, P, weights):
                    acc = [0.0, 0.0, 0.0]
                    for b, wt in w.items():
                        q = bfg.v_add(Pnew[b], bfg.m_vec(Rt[b], bfg.v_sub(p, donor_j[b].pos))) if b in Rt else p
                        acc = [a + wt * c for a, c in zip(acc, q)]
                    moved[i] = bfg.m_vec(from_ue, bfg.v_add(tuple(acc), off))
            else:
                moved = {i: bfg.m_vec(from_ue, bfg.v_add(p, off)) for i, p in zip(idx, P)}
            if bends:     # folding stretches the faces across a joint; fine again, so the skin weights can follow
                order = sorted(moved)
                slot = {i: k for k, i in enumerate(order)}
                pts = [tuple(moved[i]) for i in order]
                fine = sp.grow_body.subdivide(pts, [tuple(slot[i] for i in t) for t in tris], 0.03 * length)
                moved, tris = dict(enumerate(pts)), fine
            first, rest = name.split("_", 1)
            parts.append((f"{first}2_{rest}", moved, tris, donor_key))
    return parts


def add_club(rig, club, flags, notes, build):
    """An Ankylosaurus' tail club on the last tail bone, carried on a tail_club joint with its own capsule. Bone-solid
    (1,100 kg/m^3, an ellipsoid the size of the part)."""
    J = {j["name"]: j for j in rig["joints"]}
    if "tail_club" in J:
        flags.append("Its tail already ends in a club.")
        return []
    tail = [j["name"] for j in rig["joints"] if j["section"] == "Tail" and j.get("limits_deg") and not j["name"].endswith("_end")]
    if not tail:
        flags.append("It has no tail to put a club on.")
        return []
    children = {}
    for j in rig["joints"]:
        children.setdefault(j["parent"], []).append(j["name"])
    last = next(n for n in tail if not any(c in tail for c in children.get(n, ())))
    source = sp.load("ankylo")
    sj = {j["name"]: j for j in source["joints"]}
    sline, line = trunk_line(sj), trunk_line(J)
    fit = (line[-1][1] - line[0][1]) / max(sline[-1][1] - sline[0][1], 1e-6)
    scale = round(fit if club["scale"] == "fit" else club["scale"], 2)
    stem, club_at = sj["tail_club"]["parent"], sj["tail_club"]["position_cm"]
    a0 = sj[stem]["position_cm"]
    d0 = bfg.v_norm(bfg.v_sub(club_at, a0))
    prev = J[J[last]["parent"]]["position_cm"]
    end = J[last]["position_cm"]
    d1 = bfg.v_norm(bfg.v_sub(end, prev))
    axis = bfg.v_cross(d0, d1)
    sin = bfg.v_len(axis)
    R = sp.rotation(bfg.v_norm(axis), math.atan2(sin, bfg.v_dot(d0, d1))) if sin > 1e-4 else bfg.IDENTITY
    place = lambda p: bfg.v_add(end, bfg.v_scale(bfg.m_vec(R, bfg.v_sub(p, a0)), scale))
    joint = json.loads(json.dumps(sj["tail_club"]))
    joint.update(parent=last, position_cm=[round(c, 1) for c in place(club_at)], lineage=source["creature"])
    rig["joints"].append(joint)
    verts, dparts = bfg.load_obj(os.path.join(ROOT, "body_shapes", "ankylo_features.obj"))
    to_ue, from_ue = (lambda v: bfg.m_vec(bfg.UE_FROM_OBJ, v)), bfg.m_inv(bfg.UE_FROM_OBJ)
    parts, used = [], []
    for name, tris in dparts:
        if not sp.part_base(name).startswith("tail_club"):
            continue
        idx = sorted({i for t in tris for i in t})
        used += [place(to_ue(verts[i])) for i in idx]
        parts.append((name, {i: bfg.m_vec(from_ue, place(to_ue(verts[i]))) for i in idx}, tris, "ankylo"))
    size = [max(p[k] for p in used) - min(p[k] for p in used) for k in range(3)]     # cm
    kg = 1100.0 * math.pi / 6.0 * size[0] * size[1] * size[2] * 1e-6
    rig["collision_capsules"].append({"bone": "tail_club", "from_joint": "tail_club", "to_joint": last,
                                      "radius_cm": round(0.5 * max(size[1], size[2]), 1), "length_cm": round(math.dist(joint["position_cm"], end), 1),
                                      "mass_kg": round(kg, 2)})
    by = rig["mass_kg"]["by_section"]
    tail_key = next(k for k in by if k.startswith("Tail"))
    by[tail_key] = round(by[tail_key] + kg, 2)
    rig["mass_kg"]["total"] = round(rig["mass_kg"]["total"] + kg, 2)
    rig["tail_club_scale"] = scale
    notes.append(f"a tail club, {100 * scale:.0f}% of an Ankylosaurus', {kg:,.1f} kg of solid bone on the {last.replace('_', ' ')}")
    return parts if build else []


def write_final_features(key, source, shifts, extra=(), size=1.0):
    """The last step's bone parts for the whole creature, those on a moved section slid with its bones, and any added."""
    to_obj = sp.bfg.m_inv(sp.bfg.UE_FROM_OBJ)
    out, shift, count = [], None, 0
    with open(os.path.join(ROOT, "body_shapes", f"{source}_features.obj"), encoding="utf-8") as f:
        for ln in f:
            ln = ln.rstrip("\n")
            if ln.startswith("o "):
                s = shifts.get(sp.feature_section(ln[2:].strip()))
                shift = bfg.m_vec(to_obj, (s[0], 0.0, s[1])) if s else None
            elif ln.startswith("v ") and (shift or abs(size - 1.0) >= 1e-3):
                v = [float(c) for c in ln.split()[1:4]]
                v = [a + b for a, b in zip(v, shift)] if shift else v
                ln = "v {:.2f} {:.2f} {:.2f}".format(*(a * size for a in v))
            count += ln.startswith("v ")
            out.append(ln)
    for name, new, tris, origin in extra:
        out += [f"# lineage {name}: {origin}", f"o {name}"]
        remap = {}
        for i in sorted(new):
            count += 1
            remap[i] = count
            out.append("v {:.2f} {:.2f} {:.2f}".format(*(a * size for a in new[i])))
        out += ["f " + " ".join(str(remap[i]) for i in t) for t in tris]
    path = os.path.join(ROOT, "body_shapes", f"{key}_features.obj")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    return path


def compose(recipe, build):
    base = recipe["base"]
    steps = checked(recipe)
    moves = moves_of(recipe)
    root = sp.load(base)
    key, name = creature_key(recipe), creature_name(recipe)
    pair, club = pair_of(recipe), club_of(recipe)
    size = size_of(recipe)
    if not steps and not moves and not pair and not club and abs(size - 1.0) < 1e-3:
        return key, name, root, []
    made, current, rig, flags, notes = [], base, json.loads(json.dumps(root)), [], []
    try:
        for i, (sec, donor, scale, thick) in enumerate(steps):
            part = donor
            if abs(scale - 1.0) >= 1e-3 or abs(thick - 1.0) >= 1e-3:
                part = make_part(donor, sec, scale, thick, build)
                made.append(part)
            edits = bone_edits(recipe, sec)
            if edits:                 # Advanced mode: single bones of this section, after its swap and resize
                part, said = edit_bones(part, sec, edits, build)
                made.append(part)
                notes += said
            ours = sp.load(part)["mass_kg"]["by_section"]
            theirs = root["mass_kg"]["by_section"]
            a = next(v for k, v in ours.items() if k.startswith(sec))
            b = next(v for k, v in theirs.items() if k.startswith(sec))
            if a > MISMATCH * b or b > MISMATCH * a:
                word = SECTION_WORD[sec]
                these, weighs = ("These", "weigh") if word.endswith("s") else ("This", "weighs")
                flags.append(f"Size mismatch: {these.lower()} {word} {weighs} {a:,.0f} kg where the {SHORT[base]}'s {weighs} {b:,.0f} kg.")
            step = f"_ed_step{os.getpid()}_{i}"
            made.append(step)
            rig = sp.splice(step, dict(base=current, donor=part, section=sec, name=name, root=base, root_name=SHORT[base]),
                            mode="part" if build else "rig")
            notes += [n for n in rig["splice"]["notes"] if "tilts 0°" not in n and "sits 0 cm" not in n]     # a fitted part moves nothing
            flags += [n[0].upper() + n[1:] for n in rig["splice"]["notes"] if "don't reach the ground" in n or "front legs alone" in n]
            current = step
        # The whole creature's record, in place of the last step's.
        rig["creature"] = name
        shifts = apply_moves(rig, moves, notes, flags)
        extra = add_pair(rig, pair, key, notes, flags, made, build) if pair else []
        if club:
            extra += add_club(rig, club, flags, notes, build)
        if shifts or pair or club:
            rig["balance_point_cm"] = balance_of(rig)
        if abs(size - 1.0) >= 1e-3:
            # The whole creature at its size; the checks below are worked out again for it.
            keep = {k: rig[k] for k in ("splice", "recipe") if k in rig}
            rig = scale_rig({k: v for k, v in rig.items() if k not in keep}, size)
            rig.update(keep)
            notes.append(f"Grown at {100 * size:.0f}% of its size: {rig['mass_kg']['total']:,.1f} kg.")
        rig["leg_check"] = sp.leg_check(rig)
        rig["gaits"], rig["gait_check"] = sp.splice_gaits(rig, base)
        rig["climb_check"] = sp.climb_check(rig, base)
        rig["swim_check"] = sp.swim_check(rig, base)
        # (worked out again whenever its weight or wings changed: an added pair or club, or the whole creature's size
        # (2026-11-15: at 123% size it still read the 100% wing loading, scaled as if it were an area))
        if rig.get("wing_area_m2") and (pair or club or abs(size - 1.0) >= 1e-3):
            muscle = rig.get("flight_muscle_kg") or sp.FLIGHT_MUSCLE_SHARE[base] * root["mass_kg"]["total"]
            rig["flight_muscle_kg"] = round(muscle, 2)
            names = [n for k_ in (base, pair["donor"] if pair else base) for n, _ in bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{k_}_features.obj"))[1]]
            heavy = "Armour and solid bone add too much mass for these wings" if any(n.startswith("armor") for n in names) else ""
            rig["flight_check"] = sp.flight_check(rig["mass_kg"]["total"], rig["wing_area_m2"], muscle,
                                                  sp.WINGSPAN_M.get(pair["donor"]) if pair and rig.get("wing_pair") and abs(rig["extra_pair"]["scale"] - 1.0) < 1e-3 else None, heavy)
        # A limb far too long for the body (a Brachiosaurus arm on a raptor's low shoulders) can neither fold clear nor
        # stand on its foot, and ends under the ground. Flagged: lifting or tilting the body onto it isn't done.
        low = {}
        for j in rig["joints"]:
            if j["position_cm"][2] < -UNDERGROUND_CM:
                low[j["section"]] = min(low.get(j["section"], 0.0), j["position_cm"][2])
        for sec, z in low.items():
            word = SECTION_WORD.get(sec, sec.lower())
            flags.append(f"Its {word} {'reach' if word.endswith('s') else 'reaches'} {-z:,.0f} cm into the ground: "
                         f"too long for its body, or tilted into it (try Fit, or a smaller size).")
        finish(key, name, rig, recipe, root, steps, notes, flags)
        if build:
            rig_path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
            sp.save_json(rig_path, rig)
            feats = write_final_features(key, current, shifts, extra, size)
            sp.grow_body.grow(rig_path, feats, os.path.join(ROOT, "body_shapes", f"{key}_body.obj"))
            sp.report(key, dict(base=base, donor=rig["splice"]["donor"], section=rig["splice"]["section"]), rig, notes)
    finally:
        cleanup(made)
    return key, name, rig, notes


def finish(key, name, rig, recipe, root, steps, notes, flags):
    base = recipe["base"]
    J = {j["name"]: j for j in rig["joints"]}
    kg = {}
    for c in rig["collision_capsules"]:
        who = J.get(c["from_joint"], {}).get("lineage", root["creature"])
        kg[who] = kg.get(who, 0.0) + c["mass_kg"] * (2 if "(l, r)" in c["bone"] else 1)
    total = rig["mass_kg"]["total"]
    rig["splice"] = dict(rig.get("splice", {}), base=base, donor=steps[-1][1] if steps else base,
                         section="several" if len(steps) > 1 else steps[0][0] if steps else "moved",
                         swapped=name, lineage_kg={k: round(v, 2) for k, v in kg.items()},
                         lineage_share={k: round(v / total, 3) for k, v in kg.items()}, notes=notes, flags=flags,
                         recipe=recipe, made_by="splice/compose.py (body grown by body_tools/grow_body.py)")


def readout(key, name, rig, notes):
    """What the Splice screen shows and draws."""
    pos = {j["name"]: j["position_cm"] for j in rig["joints"]}
    J = {j["name"]: j for j in rig["joints"]}
    splice = rig.get("splice", {})
    root_name = sp.load(splice["base"])["creature"] if splice else rig["creature"]
    caps = []
    for c in rig["collision_capsules"]:
        for sd in (["_l", "_r"] if "(l, r)" in c["bone"] else [""]):
            a = c["from_joint"][:-2] + sd if sd else c["from_joint"]
            b = c["to_joint"][:-2] + sd if sd else c["to_joint"]
            if a in pos and b in pos:
                caps.append({"a": pos[a], "b": pos[b], "r": c["radius_cm"], "from": J[a].get("lineage", root_name)})
    legs = rig.get("leg_check") or sp.leg_check(rig)
    # Whether the centre of mass sits over the feet (the testing ground measures this live on the animal too).
    x = rig["balance_point_cm"][0]
    extra_walks = rig.get("extra_pair", {}).get("walks")
    hangs = bool(rig.get("hind_hang_cm"))      # standing on its front legs alone: the hind feet are off the ground
    feet = [pos[n][0] for n in pos if (n.startswith(("foot_", "ball_", "toe_end_")) and not (hangs and "_2_" not in n)
                                       and ("_2_" not in n or extra_walks)) or (
        rig["body_tilt"]["kind"] == "quadruped" and n.startswith(("hand_", "finger_end_", "wingfinger_01_")) and ("_2_" not in n or extra_walks))
        or (extra_walks and n.startswith(("hand_2_", "finger_end_2_")))]
    margin = min(x - min(feet), max(feet) - x) if feet else 0.0
    # A two-legged animal steps its feet under its weight (the testing ground swings the legs as far as 90% of the
    # hip's range), so it only tips if that can't reach; one on its front legs alone the same with them.
    step = 0.0
    thigh = J.get("upperarm_l" if hangs else "thigh_l")
    if margin < 0.0 and (rig["body_tilt"]["kind"] != "quadruped" or hangs) and thigh and thigh.get("limits_deg"):
        lo, hi = thigh["limits_deg"]["bend"]
        swing = 0.9 * abs(hi if x > max(feet) else lo)
        step = thigh["position_cm"][2] * math.sin(math.radians(min(swing, 90.0)))
    # The testing ground tips an animal over when, standing still, its weight is more than TIP_SHARE of its length
    # outside its feet and stepping can't bring them under it (ABFDino::TickBalance): the same here.
    xs = [p[0] for p in pos.values()]
    slack = TIP_SHARE * (max(xs) - min(xs))
    if margin >= 5.0:
        balance = f"Balanced: the centre of mass sits {margin:.0f} cm inside the feet."
    elif margin >= 0.0:
        balance = f"Only just balanced: the centre of mass is {margin:.0f} cm inside the feet."
    elif step > 0.0 and margin + step >= -slack:
        balance = f"Balanced by stepping: it swings its feet {-margin:.0f} cm {'forward' if x > max(feet) else 'back'} to stand under its weight."
    elif margin >= -slack:
        balance = f"Only just balanced: the centre of mass is {-margin:.0f} cm past the edge of its feet, close enough to hold."
    else:
        balance = f"Tips over: the centre of mass is {-(margin + step):.0f} cm {'ahead of' if x > max(feet) else 'behind'} where its feet can reach."
    margin += step
    flight = rig.get("flight_check", {})
    recipe = splice.get("recipe")
    scales = {sec: scale for sec, _, scale, _ in checked(recipe)} if recipe else {}
    if rig.get("extra_pair"):
        scales["Pair"] = rig["extra_pair"]["scale"]
    if rig.get("tail_club_scale"):
        scales["Club"] = rig["tail_club_scale"]
    # Advanced mode's rows: each section's bones as the creature has them now, where each came from, and which species
    # have one to give.
    six = {k: sp.load(k) for k in SHORT}
    key_of = {r["creature"]: k for k, r in six.items()}
    kids = first_children(rig)
    bones = {}
    for sec in ADVANCED:
        rows = []
        for n in editable_bones(rig, sec):
            c = kids[n][0]
            rows.append({"bone": n, "label": bone_label(n), "paired": n.endswith("_l"), "role": J[n].get("role") or "",
                         "len_cm": round(math.dist(pos[n], pos[c]), 1),
                         "from": key_of.get(J[n].get("lineage") or root_name, splice.get("base", key)),
                         "movable": J.get(J[n].get("parent"), {}).get("section") == sec,
                         "donors": [k for k, r in six.items() if n in editable_bones(r, sec)]})
        bones[sec] = rows
    return {"scales": scales, "bones": bones,
            "key": key, "name": name, "mass_kg": rig["mass_kg"]["total"], "balance_cm": rig["balance_point_cm"],
            "kind": rig["body_tilt"]["kind"], "lineage": splice.get("lineage_share") or {root_name: 1.0},
            "legs": legs["summary"], "legs_verdict": legs["verdict"], "balance": balance, "balance_ok": margin >= 0.0,
            # the weakest pair's muscle against its share of the weight (the editor shows what a change did to it)
            "legs_ratio": min((p["ratio"] for p in legs.get("pairs", {}).values() if p.get("load_n", 0) > 0), default=0.0),
            "flight": flight.get("summary", ""), "flight_verdict": flight.get("verdict", ""),
            "swim": rig.get("swim_check", {}).get("summary", ""), "swim_verdict": rig.get("swim_check", {}).get("verdict", ""),
            "climb": rig.get("climb_check", {}).get("summary", ""), "climb_deg": rig.get("climb_check", {}).get("max_deg", 0.0),
            "gaits": rig.get("gait_check", {}).get("summary", ""),
            "notes": notes, "flags": splice.get("flags", []), "capsules": caps,
            "body": os.path.join(ROOT, "body_shapes", f"{key}_body.obj") if splice else ""}


def main(argv):
    mode, recipe_path, out_path = argv
    with open(recipe_path, encoding="utf-8") as f:
        recipe = json.load(f)
    try:
        out = readout(*compose(recipe, mode == "build"))
    except Exception as e:   # shown on the screen, like any other result
        out = {"error": f"{type(e).__name__}: {e}"}
    with open(out_path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False)
    os.replace(out_path + ".tmp", out_path)
    if "error" in out:
        print(out["error"])
        sys.exit(1)


if __name__ == "__main__":
    main(sys.argv[1:])
