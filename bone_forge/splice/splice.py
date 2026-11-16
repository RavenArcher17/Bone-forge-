"""Bone Forge splicing: one species' body section (head, neck, tail, legs or arms) swapped onto another, its body
grown around the mixed skeleton, and the flight and leg checks.

    python splice/splice.py                 # every recipe below
    python splice/splice.py raptorwings     # just one

A spliced creature is written out like another species, so the rest of the pipeline (bone_forge_gltf.py, the
Unreal import, the testing ground) treats it like any other:
  rig_joints/<key>_rig.json      the base's joints with the donor's section in place of its own: the donor's joint
                                 names (shared by every rig), limits, collision capsules and tissue profiles, each
                                 joint tagged with the species it came from; mass, balance point, flight pose and the
                                 flight and leg checks redone
  body_shapes/<key>_features.obj the bone parts: the base's (heads, feet, horns, armour...) without its section's,
                                 plus the donor's section's, each moved with its bones
  body_shapes/<key>_body.obj     those, and the flesh grown from every bone's tissue (body_tools/grow_body.py), so the
                                 donor's section joins the base's body the way an animal's own flesh does
  splice/<key>_report.txt        lineage, mass and the checks in plain words

A swapped part keeps its own size (a size mismatch is allowed and the checks show what it costs). Swapping legs
changes the hips' height: a two-legged animal is lifted or lowered, a four-legged one tilts about its shoulders so
its hips sit on the new legs (past 45° it stands up on them and carries its front limbs instead). Base joints that
hung off the swapped section (the head on a swapped neck) ride on the donor's section where the donor's own did.

The swapped section sits on the base's own sockets (a shoulder on a shoulder). A walking front limb moved onto a
two-legged animal can't reach the ground from its lower shoulders, so it is carried folded instead: the elbow,
wrist and wing knuckle fold as far as their limits let them and the shoulder swings the folded limb back along the
body (a search over those four bends). The joint limits move with the new rest pose, so the limb can still reach
every pose it could before, the flight pose included. On a four-legged animal the limb keeps walking: only its
shoulder turns, until the foot (or the wing knuckle) is as high off the ground as on its donor. Front limbs too long
for either (they would go through the ground) are stood on: the body tilts head-up about its hips, as far as 45°, and
is lifted the rest of the way, its hind feet then hanging (front_stance; the rig's hind_hang_cm). A tilt that would
put the tail through the ground lifts it from the root, and past 30° lays the rest along the ground (carry_tail).

Also refreshes the flight check of base rigs that have wings (the Pteranodon), with the same rule:
  wing loading = weight / wing area must stay under 245 N/m^2, and flight muscle must be at least 15% of the weight;
  passes the first but not the second = glides only (Weight and Physics tab, "Can it fly?").
Flight muscle (the downstroke muscle on the chest) belongs to the torso, so wings alone never bring it.
"""
import copy
import hashlib
import json
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "unreal"))
sys.path.insert(0, os.path.join(ROOT, "body_tools"))
import bone_forge_gltf as bfg  # noqa: E402  (joint frames, OBJ reading and starter skin weights)
import grow_body  # noqa: E402  (the flesh grown from the bones' tissue profiles)

RECIPES = {
    # Prototype 3: wings
    "raptorwings": dict(base="raptor", donor="ptera", section="Shoulder", name="Velociraptor with Pteranodon wings"),
    "ankylowings": dict(base="ankylo", donor="ptera", section="Shoulder", name="Ankylosaurus with Pteranodon wings"),
    # Catalog of six: Raven's first swaps (2026-11-04)
    "rextrikehead": dict(base="trex", donor="trike", section="Head", name="T. rex with a Triceratops head"),
    "raptorbrachioneck": dict(base="raptor", donor="brachio", section="Neck", name="Velociraptor with a Brachiosaurus neck"),
    "rexankylotail": dict(base="trex", donor="ankylo", section="Tail", name="T. rex with an Ankylosaurus tail"),
    "trikeraptorlegs": dict(base="trike", donor="raptor", section="Hip", name="Triceratops on Velociraptor legs"),
    # Limit tests
    "pterarexlegs": dict(base="ptera", donor="trex", section="Hip", name="Pteranodon on T. rex legs"),
    "raptorrexhead": dict(base="raptor", donor="trex", section="Head", name="Velociraptor with a T. rex head"),
}

G = 9.81
FOLD_MEMO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_fold_memo.json")
WING_LOADING_LIMIT = 245.0      # N/m^2, about the upper limit seen in flying birds (Quetzalcoatlus ~224)
FLIGHT_MUSCLE_MIN = 0.15        # share of body weight, as in birds that take off under their own power
# Downstroke (pectoral) muscle on the chest as a share of each species' own weight: game estimates. The
# Pteranodon's is bird-like; the others carry the shoulder muscles of an animal that walks, grabs or does neither.
FLIGHT_MUSCLE_SHARE = {"trex": 0.01, "trike": 0.02, "raptor": 0.04, "ankylo": 0.02, "brachio": 0.015, "ptera": 0.20}
WINGSPAN_M = {"ptera": 5.96}    # Witton 2008, with the wing area in the rig (1.99 m^2)
# Body parts (OBJ object names, as bone_forge_gltf.part_bones reads them) that belong to each section.
SECTION_PARTS = {"Shoulder": ("arm", "wing", "membrane"), "Hip": ("leg",), "Tail": ("tail",)}


# ---------------------------------------------------------------------------------------------- small helpers
def load(key):
    with open(os.path.join(ROOT, "rig_joints", f"{key}_rig.json"), encoding="utf-8") as f:
        return json.load(f)


def save_json(path, data):
    """Written whole or not at all (to a side file, then swapped in): two splice.py runs at once read a base rig while
    the other was rewriting it, and crashed on an empty file."""
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
        f.write("\n")
    for attempt in range(40):       # Windows won't replace a file another process (or OneDrive's sync) has open
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            if attempt == 39:
                os.remove(tmp)
                raise
            time.sleep(0.05 * (attempt + 1))


def rotation(axis, angle):
    x, y, z = bfg.v_norm(axis)
    c, s = math.cos(angle), math.sin(angle)
    C = 1 - c
    return ((c + x * x * C, x * y * C - z * s, x * z * C + y * s),
            (y * x * C + z * s, c + y * y * C, y * z * C - x * s),
            (z * x * C - y * s, z * y * C + x * s, c + z * z * C))


def subtree(joints, name):
    out = [name]
    for c in joints[name].children:
        out += subtree(joints, c)
    return out


def section_part(name, section):
    pieces = name.split("_")
    base = "_".join(pieces[:-1]) if len(pieces) > 1 and pieces[-1].isdigit() else name
    return grow_body.named(base, SECTION_PARTS.get(section, ()))


# ---------------------------------------------------------------------------------------------- folding a limb
class Fold:
    """A limb re-posed by turns at its joints, {joint: (bend, side)} in rig degrees (bend + swings forward / bends
    more, side + away from the body), done the way the game turns a joint: bend about its bone frame's Y, side about
    its Z (bone_forge_gltf.bone_frames), carried down the chain."""

    def __init__(self, joints, frames, names):
        self.joints, self.frames, self.names = joints, frames, set(names)

    def apply(self, turns):
        P = {n: self.joints[n].pos for n in self.names}
        R = {n: bfg.IDENTITY for n in self.names}
        for name, (bend, side) in turns.items():
            def col(k):
                return bfg.m_vec(R[name], tuple(self.frames[name][i][k] for i in range(3)))
            for axis, angle in ((col(1), -bfg.bend_pitch_sign(name) * bend),
                                (col(2), (-1 if name.endswith("_l") else 1) * side)):
                if not angle:
                    continue
                M = rotation(axis, math.radians(angle))
                pivot = P[name]
                for d in subtree(self.joints, name):
                    if d != name:
                        P[d] = bfg.v_add(pivot, bfg.m_vec(M, bfg.v_sub(P[d], pivot)))
                    R[d] = bfg.m_mul(M, R[d])
        return P, R


class BodyWidth:
    """How far out from the midline a body reaches, per 10 cm cell of (x, z): a limb's joints are kept outside it so
    the swapped limb doesn't sink into the flank (or the Ankylosaurus' armour). only: just these parts (the body wall
    itself, for mounting a part on the skin)."""

    def __init__(self, obj_path, skip_section, only=None):
        verts, parts = bfg.load_obj(obj_path)
        self.cells = {}
        for name, tris in parts:
            if section_part(name, skip_section) or name.startswith(("eye", "teeth", "tongue")):
                continue
            if only and not name.startswith(only):
                continue
            for i in {i for t in tris for i in t}:
                x, y, z = bfg.m_vec(bfg.UE_FROM_OBJ, verts[i])
                c = (int(x // 10), int(z // 10))
                self.cells[c] = max(self.cells.get(c, 0.0), abs(y))

    @classmethod
    def from_rig(cls, rig_path, skip_section, trunk_only=False):
        """The same from the bones' tissue, for a rig with no body yet (a step of compose.py)."""
        self = cls.__new__(cls)
        self.cells = {}
        rig, J, topo = bfg.load_rig(rig_path)
        tissue = {j["name"]: j["tissue"] for j in rig["joints"] if j.get("tissue")}
        for ch, prof, zs, Yv in grow_body.chain_sweeps(J, bfg.bone_frames(J, topo), tissue):
            if ch.name != "trunk" and (trunk_only or J[ch.name].section == skip_section):
                continue
            for x, y, z in grow_body.section_outline(ch, prof, zs, Yv):
                c = (int(x // 10), int(z // 10))
                self.cells[c] = max(self.cells.get(c, 0.0), abs(y))
        return self

    def moved(self, mv):
        """The same outline after the body's own move (a tilt or lift onto new legs): a limb placed on the moved body
        was kept clear of where it had been (Brachiosaurus arms under a lifted Velociraptor turned 25° away from it)."""
        if mv.R == bfg.IDENTITY and not any(mv.t):
            return self
        out = BodyWidth.__new__(BodyWidth)
        out.cells = {}
        for (cx, cz), w in self.cells.items():
            x, _, z = mv((cx * 10.0 + 5.0, 0.0, cz * 10.0 + 5.0))
            c = (int(x // 10), int(z // 10))
            out.cells[c] = max(out.cells.get(c, 0.0), w)
        return out

    def at(self, x, z, reach=1):
        cx, cz = int(x // 10), int(z // 10)
        span = range(-reach, reach + 1)
        return max((self.cells.get((cx + i, cz + k), 0.0) for i in span for k in span), default=0.0)

    def widest(self, x):
        """The body's half-width at x seen from above: its widest at any height."""
        cx = int(x // 10)
        return max((w for (i, _), w in self.cells.items() if i == cx), default=0.0)


def mount_out(skin, donor_skin, socket, donor_socket):
    """How far (cm) to move a swapped part's root out from the base's socket: to the base's widest at that point
    (seen from above), as deep under the skin as it sat under its donor's (at least 2 cm), never further in than
    the socket itself. Out there a wing flaps clear of a barrel-shaped body, and its membrane meets the flank along
    the body's outline instead of passing through it."""
    donor_depth = max(donor_skin.at(donor_socket[0], donor_socket[2], 0) - abs(donor_socket[1]), 2.0)
    wall = skin.widest(socket[0])
    return max(0.0, wall - donor_depth - abs(socket[1])) if wall else 0.0


def bend_chain(joints, top):
    """The limb's main line from its socket down: joints that bend, at most four (shoulder, elbow, wrist, knuckle)."""
    chain, j = [], top
    while j and len(chain) < 4:
        lim = joints[j].info.get("limits_deg") or {}
        b = lim.get("bend") or [0, 0]
        if b[1] - b[0] < 15:          # the wing finger's own joints barely move
            break
        chain.append(j)
        j = bfg.main_child(joints[j])
    return chain


def frange(lo, hi, step):
    n = int(round((hi - lo) / step))
    return [lo + (hi - lo) * i / max(n, 1) for i in range(n + 1)]


def far_end(joints, top):
    """The limb's tip: the end of its longest run of bones (the wing tip, not the knuckle that sticks out)."""
    def run(n):
        best = (0.0, n)
        for c in joints[n].children:
            length, end = run(c)
            length += bfg.v_len(bfg.v_sub(joints[c].pos, joints[n].pos))
            best = max(best, (length, end))
        return best
    return run(top)[1]


def clearance_penalty(P, off, socket, width, spread=None):
    """Joints inside the body's width, on the far side of the midline, or flung out sideways: more than 25 cm past the
    socket, or past how far the limb splays out on its donor (spread, cm per joint: a Brachiosaurus elbow stands 45 cm
    out from its shoulder, and held to 25 cm the arm was turned 25° to tuck it in)."""
    sign = 1.0 if socket[1] > 0 else -1.0
    pen = 0.0
    for n, p in P.items():
        x, y, z = (a + o for a, o in zip(p, off))
        out = sign * y
        pen += 0.5 * max(0.0, width.at(x, z) + 3.0 - out) ** 2
        pen += max(0.0, out - (abs(socket[1]) + 25.0 + max((spread or {}).get(n, 0.0), 0.0))) ** 2
    return pen


def carried_fold(donor_j, frames, side_names, chain, socket, base_j, width):
    """Bends that tuck the limb along a two-legged body: above the ground and below the head, inside the body's
    length and just outside its flank, folded close to the shoulder's height, with the tip trailing back."""
    fold = Fold(donor_j, frames, side_names)
    off = bfg.v_sub(socket, donor_j[chain[0]].pos)
    tip = far_end(donor_j, chain[0])
    xs = [j.pos[0] for j in base_j.values()]
    head = base_j["head"].pos[2] if "head" in base_j else socket[2] * 1.7
    snout = base_j["head_end"].pos[0] if "head_end" in base_j else max(xs)
    x_lo, x_hi = min(xs) - 5.0, snout - 20.0
    z_lo, z_hi, z_want = 0.25 * socket[2], head - 2.0, 1.1 * socket[2]
    lims = [donor_j[n].info["limits_deg"]["bend"] for n in chain]
    grids = [frange(lo, hi, st) for (lo, hi), st in zip(lims, (5.0, 8.0, 10.0, 10.0))]
    best = None

    def walk(level, bends):
        nonlocal best
        if level == len(chain):
            P, _ = fold.apply({n: (b, 0.0) for n, b in zip(chain, bends)})
            pen = clearance_penalty(P, off, socket, width)
            for p in P.values():
                x, z = p[0] + off[0], p[2] + off[2]
                pen += max(0, z_lo - z) ** 2 + max(0, z - z_hi) ** 2 + max(0, x - x_hi) ** 2 + max(0, x_lo - x) ** 2
                pen += 0.02 * (z - z_want) ** 2
            pen += 0.005 * max(0.0, P[tip][0] + off[0] - (socket[0] - 40.0)) ** 2
            if best is None or pen < best[0]:
                best = (pen, list(bends))
            return
        for b in grids[level]:
            walk(level + 1, bends + [b])

    walk(0, [])
    return {n: (b, 0.0) for n, b in zip(chain, best[1])}


def walking_fold(donor_j, frames, side_names, chain, socket, width, want=None):
    """Only the shoulder turns (within its limits): the limb's lowest joint ends as high above the ground as on
    its donor (or want, cm), so the foot (or wing knuckle) still meets it, and the folded limb clears the body's side."""
    fold = Fold(donor_j, frames, side_names)
    off = bfg.v_sub(socket, donor_j[chain[0]].pos)
    low = min(side_names, key=lambda n: donor_j[n].pos[2])
    want = donor_j[low].pos[2] if want is None else want
    lim = donor_j[chain[0]].info["limits_deg"]
    sign = 1.0 if socket[1] > 0 else -1.0
    spread = {n: sign * (donor_j[n].pos[1] - donor_j[chain[0]].pos[1]) for n in side_names}    # its own splay
    best = None
    for b in frange(max(lim["bend"][0], -30.0), min(lim["bend"][1], 30.0), 1.0):
        for sd in frange(max(lim["side"][0], 0.0), min(lim["side"][1], 40.0), 2.0):
            P, _ = fold.apply({chain[0]: (b, sd)})
            err = 40.0 * abs(P[low][2] + off[2] - want) + clearance_penalty(P, off, socket, width, spread)
            err += 0.05 * (abs(b) + abs(sd))
            if best is None or err < best[0]:
                best = (err, b, sd)
    return {chain[0]: (best[1], best[2])}


# ---------------------------------------------------------------------------------------------- the flight check
def flight_check(mass_kg, wing_area_m2, muscle_kg, span_m=None, heavy_note=""):
    if not wing_area_m2:
        return {"verdict": "no wings", "summary": "No wings."}
    weight = mass_kg * G
    loading = weight / wing_area_m2
    share = muscle_kg / mass_kg
    out = {"weight_n": round(weight, 1), "wing_area_m2": wing_area_m2, "wing_loading_n_m2": round(loading, 1),
           "wing_loading_limit": WING_LOADING_LIMIT, "flight_muscle_kg": round(muscle_kg, 2),
           "flight_muscle_share": round(share, 3), "flight_muscle_min": FLIGHT_MUSCLE_MIN}
    need_area = weight / WING_LOADING_LIMIT
    if span_m:
        out["wingspan_m"] = span_m
        out["wingspan_needed_m"] = round(span_m * math.sqrt(need_area / wing_area_m2), 1)
    if loading > WING_LOADING_LIMIT:
        out["verdict"] = "grounded"
        times = loading / WING_LOADING_LIMIT
        span = f"; it would need wings {out['wingspan_needed_m']:,.0f} m across" if span_m else ""
        out["summary"] = (f"Can't fly: wing loading {loading:,.0f} N/m² is {times:,.1f} times the {WING_LOADING_LIMIT:.0f} "
                          f"limit. {heavy_note or 'The body is too heavy for these wings'}{span}.")
    elif share < FLIGHT_MUSCLE_MIN:
        out["verdict"] = "glides"
        out["summary"] = (f"Glides only: wing loading {loading:,.0f} N/m² is under the {WING_LOADING_LIMIT:.0f} limit, but "
                          f"flight muscle is {100 * share:.0f}% of its weight (needs {100 * FLIGHT_MUSCLE_MIN:.0f}%), so it "
                          f"can't flap upward. It launches from a height or a running jump and sinks slowly.")
    else:
        out["verdict"] = "flies"
        out["summary"] = (f"Flies: wing loading {loading:,.0f} N/m² (limit {WING_LOADING_LIMIT:.0f}), flight muscle "
                          f"{100 * share:.0f}% of its weight (needs {100 * FLIGHT_MUSCLE_MIN:.0f}%).")
    return out

# ---------------------------------------------------------------------------------------------- the leg check
MUSCLE_STRESS = 30.0     # N per cm² of muscle cross-section: about 300 kPa, what vertebrate muscle pulls (game value)
MUSCLE_SHARE = 0.7       # of a thigh's (or upper arm's) cross-section; the rest is bone, fat and skin
WALK_MARGIN = 2.0        # walking loads each leg to about twice its standing share


def leg_check(rig):
    """Whether the walking legs' muscle can hold the body up: each pair's muscle (its first bone's thickest flesh,
    from the tissue profile) against the share of the weight it carries (front and back split by where the centre of
    mass sits between hips and shoulders). Walks with twice the muscle its share needs, stands with less, buckles
    with less than its share."""
    J = {j["name"]: j for j in rig["joints"]}
    weight = rig["mass_kg"]["total"] * G
    quad = rig["body_tilt"]["kind"] == "quadruped"
    hangs = bool(rig.get("hind_hang_cm"))         # its hind feet hang off the ground: the front legs carry it all
    pairs = ([] if hangs else [("hind", "thigh")]) + ([("front", "upperarm")] if quad else [])
    extra = rig.get("extra_pair") if rig.get("extra_pair", {}).get("walks") else None
    if extra:
        pairs.append(("extra", extra["upper"]))

    def capacity(name):
        t = J.get(name, {}).get("tissue")
        if not t:
            return None
        return max(math.pi * s[3] * s[4] for s in t) * MUSCLE_SHARE * MUSCLE_STRESS

    x = rig["balance_point_cm"][0]
    xh = J["thigh_l"]["position_cm"][0] if "thigh_l" in J else 0.0
    front = 0.0
    if quad and "upperarm_l" in J:
        xs = J["upperarm_l"]["position_cm"][0]
        front = 1.0 if hangs else min(max((x - xh) / max(xs - xh, 1e-6), 0.0), 1.0)
    out = {"weight_n": round(weight, 1), "muscle_stress_n_cm2": MUSCLE_STRESS, "walk_margin": WALK_MARGIN, "pairs": {}}
    worst = None
    for label, bone in pairs:
        caps = [capacity(f"{bone}_{s}") for s in ("l", "r")]
        if None in caps:
            continue
        share = front if label == "front" else 1.0 - front
        if extra:
            # Three pairs (or a biped's two), the body rigid on equal springs: each carries its even share, shifted
            # by where it stands against the centre of mass.
            at = [J[f"{b}_l"]["position_cm"][0] for _, b in pairs]
            mean = sum(at) / len(at)
            spread = sum((a - mean) ** 2 for a in at)
            here = J[f"{bone}_l"]["position_cm"][0]
            share = max(1.0 / len(at) + (x - mean) * (here - mean) / max(spread, 1e-6), 0.0)
        load_n = share * weight
        ratio = sum(caps) / max(load_n, 1e-6)
        out["pairs"][label] = {"muscle_n": round(sum(caps), 1), "load_n": round(load_n, 1), "ratio": round(ratio, 2)}
        if load_n > 0 and (worst is None or ratio < worst[1]):
            worst = (label, ratio, load_n)
    if worst is None:
        out.update(verdict="walks", summary="Legs: no walking legs measured.")
        return out
    label, ratio, load_n = worst
    legs = "extra legs" if label == "extra" else "hind legs" if label == "hind" and (quad or extra) else "front legs" if label == "front" else "legs"
    if ratio >= WALK_MARGIN:
        out["verdict"] = "walks"
        out["summary"] = f"Legs hold it: the {legs} have {ratio:.1f} times the muscle their share of the weight needs."
    elif ratio >= 1.0:
        out["verdict"] = "stands"
        out["summary"] = (f"Stands but can't walk: the {legs} have {math.floor(ratio * 10.0) / 10.0:.1f} times the muscle their share of the weight "
                          f"needs, and walking needs {WALK_MARGIN:.0f}.")
    else:
        out["verdict"] = "buckles"
        out["summary"] = (f"Legs buckle: the {legs} have {ratio:.2f} times the muscle to hold up the {load_n / 1000:,.1f} kN "
                          f"resting on them.")
    return out


# ---------------------------------------------------------------------------------------------- the gaits
# The six have their gaits from the design doc (Movement tab). A splice's are worked out from the species it was built
# on by dynamic similarity (animals of one shape move alike at the same Froude number, v^2 / g h): speed with the
# square root of leg length and stride in proportion; then its legs' strength for their load against that species'
# (the leg check's ratio). A gait with a moment in the air needs strong legs, and weaker ones fall back to a walk.
RUN_MIN_RATIO = 4.0      # running or bounding: the T. rex (4.6) just runs, the Ankylosaurus (3.8) ambles
TROT_MIN_RATIO = 2.5     # trotting: the Triceratops (2.7) trots, the Brachiosaurus (2.2) ambles
AMBLE_FROUDE = 0.5       # a fast walk or amble stays under this Froude number (elephants' fastest amble is about 0.5)
FAST_GAIN_MAX = 1.3      # stronger legs than the species' own speed its fastest gait up by at most this much


def _leg_height(rig, lc):
    """The walking legs' height (cm): the hips' (and shoulders') height above the ground, their geometric mean."""
    J = {j["name"]: j for j in rig["joints"]}
    bones = {"hind": "thigh", "front": "upperarm", "extra": (rig.get("extra_pair") or {}).get("upper")}
    hs = [J[f"{bones[p]}_l"]["position_cm"][2] for p in lc.get("pairs", {}) if bones.get(p) and f"{bones[p]}_l" in J]
    hs = [h for h in hs if h > 1.0]
    return math.exp(sum(math.log(h) for h in hs) / len(hs)) if hs else None


def splice_gaits(rig, root_key):
    """A splice's walk and fastest gait from its own legs, against the species it was built on (see above)."""
    import copy
    base = load(root_key)
    out = copy.deepcopy(base["gaits"])
    lb, lc = base.get("leg_check") or leg_check(base), rig.get("leg_check") or leg_check(rig)
    hb, hs = _leg_height(base, lb), _leg_height(rig, lc)
    note = {"base": root_key}
    if lc["verdict"] != "walks" or not hb or not hs:
        note["summary"] = f"Gaits: the {base['creature']}'s, unused (its legs can't walk)."
        return out, note
    rb = min(p["ratio"] for p in lb["pairs"].values())
    rs = min(p["ratio"] for p in lc["pairs"].values())
    k = hs / hb
    strength = math.sqrt(max(rs - 1.0, 0.01) / max(rb - 1.0, 0.01))
    walk_f, fast_f = min(1.0, strength), min(FAST_GAIN_MAX, max(0.3, strength))
    w, f = out["walk"], out["fast"]
    w["stride_m"] = round(w["stride_m"] * k, 2)
    w["speed_m_s"] = round(w["speed_m_s"] * math.sqrt(k) * walk_f, 2)
    f["stride_m"] = round(f["stride_m"] * k, 2)
    f["speed_m_s"] = f["speed_m_s"] * math.sqrt(k) * fast_f
    name = f["name"]
    aerial = name.startswith("run") or "bound" in name or "gallop" in name
    slowed = ""
    if (aerial and rs < RUN_MIN_RATIO) or ("trot" in name and rs < TROT_MIN_RATIO):
        f["name"] = "fast walk" if aerial and rig["body_tilt"]["kind"] != "quadruped" else "fast walk (amble)"
        cap = math.sqrt(AMBLE_FROUDE * G * hs / 100.0)
        f["speed_m_s"] = min(f["speed_m_s"], cap)
        f["stride_m"] = round(min(f["stride_m"], w["stride_m"] * 1.6), 2)
        slowed = (f" Its legs are too weak to {('run' if aerial else 'trot')} ({rs:.1f} times their load; that takes "
                  f"{RUN_MIN_RATIO if aerial else TROT_MIN_RATIO:.1f}), so its fastest is a {f['name']}.")
    f["speed_m_s"] = round(max(f["speed_m_s"], 1.3 * w["speed_m_s"]), 2)
    for g in (w, f):
        g["stride_time_s"] = round(g["stride_m"] / max(g["speed_m_s"], 0.01), 2)
    note.update(leg_height_cm=round(hs, 1), base_leg_height_cm=round(hb, 1), ratio=round(rs, 2), base_ratio=round(rb, 2),
                summary=(f"Gaits from its legs: {hs / 100:.1f} m high against the {base['creature']}'s {hb / 100:.1f} m, "
                         f"{rs:.1f} times as strong as their load against {rb:.1f}: it walks at {w['speed_m_s']:.1f} m/s "
                         f"({w['stride_m']:.1f} m strides) and its {f['name']} reaches {f['speed_m_s']:.1f} m/s.{slowed}"))
    return out, note


# ---------------------------------------------------------------------------------------------- the climb check
# How well each species' feet grip the testing ground's ramps (a friction coefficient, game values): hooked claws dig
# in, padded feet hold fairly well, hoof-like toes slip soonest. Feet slip on a slope steeper than atan(grip).
GRIP = {"trex": 0.9, "raptor": 1.0, "ptera": 1.0, "brachio": 0.8, "trike": 0.75, "ankylo": 0.75}
FEET = {"trex": "clawed toes", "raptor": "hooked claws", "ptera": "clawed toes", "brachio": "padded feet",
        "trike": "hoof-like toes", "ankylo": "hoof-like toes"}
CLIMB_PUSH = 1.5         # each step up a slope pushes the body up as well as holding it: the legs work as if carrying
                         # (1 + CLIMB_PUSH x the slope's rise per metre) times their share, plus walking's own load
CRAWL = 0.25             # the slowest it keeps climbing, as a share of its walking speed
LEAN_IN = 0.5            # how much of the uphill weight shift stays: it leans into the slope (head and neck forward)


def climb_check(rig, root_key):
    """How steep a slope it can walk up, and how much it slows. Two limits here, the lower wins: grip (its feet slip
    on a slope steeper than atan(grip), the grip of each pair's feet weighted by the weight on them) and strength
    (each pair carries its share of the weight, shifted onto the downhill legs as the body tilts, and must push it up
    the slope too; with the muscle that leaves it slows down, since walking slower loads the legs less, down to a
    crawl, and steeper than that it can't climb). The third, balance, the creature finds itself in the game: the first
    time it appears, an unseen copy of it stands on slopes with the game's own stance and balance code, and it never
    walks up one steeper than it can stand still on without tipping over (ABFDino::MeasureStanding)."""
    lc = rig.get("leg_check") or leg_check(rig)
    out = {"grip_by_species": GRIP, "climb_push": CLIMB_PUSH, "crawl_share": CRAWL, "lean_in": LEAN_IN}
    if lc["verdict"] != "walks":
        out.update(verdict="cant", max_deg=0.0, grip_deg=0.0, strength_deg=0.0, speed_share=[1.0],
                   summary="Can't climb: it can't walk.")
        return out
    J = {j["name"]: j for j in rig["joints"]}
    by_name = {load(k)["creature"]: k for k in GRIP}
    weight = rig["mass_kg"]["total"] * G
    quad = rig["body_tilt"]["kind"] == "quadruped"
    extra = rig.get("extra_pair") if rig.get("extra_pair", {}).get("walks") else None
    bones = {"hind": "thigh", "front": "upperarm", "extra": extra["upper"] if extra else None}
    pairs = [p for p in lc["pairs"] if bones.get(p) and f"{bones[p]}_l" in J]
    x, _, h = rig["balance_point_cm"]
    at = {p: J[f"{bones[p]}_l"]["position_cm"][0] for p in pairs}

    def shares(deg):
        """Each pair's share of the weight on a slope (nose up): the centre of mass, h above the feet, falls h tan(a)
        further back over them, less what leaning into the slope takes back."""
        xc = x - LEAN_IN * h * math.tan(math.radians(deg))
        if len(pairs) == 1:
            return {pairs[0]: 1.0}
        if len(pairs) == 2:
            lo, hi = sorted(pairs, key=lambda p: at[p])
            f = min(max((xc - at[lo]) / max(at[hi] - at[lo], 1e-6), 0.0), 1.0)
            return {hi: f, lo: 1.0 - f}
        mean = sum(at.values()) / len(at)
        spread = sum((a - mean) ** 2 for a in at.values())
        s = {p: max(1.0 / len(at) + (xc - mean) * (at[p] - mean) / max(spread, 1e-6), 0.0) for p in pairs}
        t = sum(s.values())
        return {p: v / t for p, v in s.items()}

    def foot(p):
        j = J[f"{bones[p]}_l"]
        k = by_name.get(j.get("lineage"), root_key) if j.get("lineage") else root_key
        return k if k in GRIP else root_key if root_key in GRIP else "trex"

    flat = shares(0.0)
    grip = sum(GRIP[foot(p)] * flat[p] for p in pairs)
    grip_deg = math.degrees(math.atan(grip))
    muscle = {p: lc["pairs"][p]["muscle_n"] for p in pairs}

    def pace(deg):
        """The share of its walking speed it keeps on this slope, and the pair that limits it."""
        sh = shares(deg)
        best = (1.0, None)
        for p in pairs:
            if sh[p] <= 0.0:
                continue
            spare = muscle[p] / (sh[p] * weight) - 1.0 - CLIMB_PUSH * math.sin(math.radians(deg))
            s = spare / (WALK_MARGIN - 1.0)
            if s < best[0]:
                best = (s, p)
        return best
    strength_deg, limiter = 89.0, None
    for d10 in range(0, 891):
        s, p = pace(d10 / 10.0)
        if s < CRAWL:
            strength_deg, limiter = d10 / 10.0 - 0.1, p
            break
    top = max(min(grip_deg, strength_deg), 0.0)
    table = [round(max(min(pace(d)[0], 1.0), CRAWL), 3) for d in range(0, int(top) + 5, 5)]
    at_top = max(min(pace(top)[0], 1.0), CRAWL)
    out.update(max_deg=round(top, 1), grip=round(grip, 2), grip_deg=round(grip_deg, 1), strength_deg=round(strength_deg, 1),
               speed_share=table, speed_step_deg=5)
    if strength_deg < 2.0:
        out.update(verdict="cant", why="its legs have no muscle to spare for pushing it uphill",
                   summary="Can't climb: its legs have no muscle to spare for pushing it uphill.")
        return out
    feet = sorted({FEET[foot(p)] for p in pairs})
    feet = " and ".join(feet)
    if limiter and strength_deg <= grip_deg:
        legs = "legs" if len(pairs) == 1 else {"hind": "hind legs", "front": "front legs", "extra": "extra legs"}[limiter]
        why = (f"steeper than that its {legs} can't push its {rig['mass_kg']['total'] / 1000:,.1f} t up "
               f"(its {feet} would grip to {grip_deg:.0f} degrees)")
        out["verdict"] = "strength"
    else:
        why = f"steeper than that its {feet} slip"
        out["verdict"] = "grip"
    half = table[min(len(table) - 1, int(top * 0.5 / 5.0 + 0.5))]
    at_half = int(top * 0.5 / 5.0 + 0.5) * 5
    slows = (f" It keeps full speed all the way up." if at_top >= 0.999 else
             f" It slows as the slope steepens: {100 * half:.0f}% of its walking speed at {at_half} degrees, "
             f"{100 * at_top:.0f}% at its limit.")
    out["why"] = why.replace("steeper than that ", "")
    out["summary"] = f"Climbs slopes up to {top:.0f} degrees: {why}.{slows}"
    return out


# ---------------------------------------------------------------------------------------------- the swim check
# Whole-body density (kg/m^3), game values. The air-sac dinosaurs (theropods, sauropods) and the pterosaur are light
# for their size, as the buoyancy studies found (Henderson 2004: sauropods floated readily, high in the water); the
# ornithischians have lungs but no air sacs; the Ankylosaurus' armour makes it just denser than water.
SWIM_DENSITY = {"trex": 880.0, "raptor": 880.0, "brachio": 800.0, "ptera": 650.0, "trike": 970.0, "ankylo": 1040.0}
FLESH_DENSITY = 1030.0         # flesh and bone without air
WATER_DENSITY = 1000.0
# Where each animal carries its air: the lungs in the trunk, and the air sacs in the neck of those that have them.
AIRY = {"trex": ("Core", "Neck"), "raptor": ("Core", "Neck"), "brachio": ("Core", "Neck"), "ptera": ("Core", "Neck"),
        "trike": ("Core",), "ankylo": ("Core",)}
PADDLE_SHARE = 0.0025          # paddling thrust, as a share of the walking legs' full muscle pull (game value)
SWIM_DRAG = 0.6                # drag coefficient of a body swimming at the surface (its bow wave included)
TIPPY_COST = 0.8               # a tippy swimmer paddles partly to stay upright: it keeps this share of its speed
AIR_HIGH = 0.4                 # how high in its flesh the air sits, as a share of the flesh's half-height


def _air_per_kg(key):
    """Air (m^3) per kg of a species' airy flesh, so the whole animal comes to its SWIM_DENSITY."""
    rig = load(key)
    J = {j["name"]: j for j in rig["joints"]}
    total = airy = 0.0
    for c in rig["collision_capsules"]:
        m = c["mass_kg"] * (2 if "(l, r)" in c["bone"] else 1)
        total += m
        if J.get(c["from_joint"], {}).get("section") in AIRY[key]:
            airy += m
    return max(total / SWIM_DENSITY[key] - total / FLESH_DENSITY, 0.0) / max(airy, 1e-9)


def _swim_bodies(rig, root_key):
    """The body as small spheres (centre cm, radius cm, volume m^3) and its masses (centre cm, kg): each collision
    capsule a row of spheres holding its flesh, and its share of its species' air if it carries any."""
    J = {j["name"]: j for j in rig["joints"]}
    by_name = {load(k)["creature"]: k for k in SWIM_DENSITY}
    air = {k: _air_per_kg(k) for k in SWIM_DENSITY}
    spheres, masses = [], []
    for c in rig["collision_capsules"]:
        for sd in (["_l", "_r"] if "(l, r)" in c["bone"] else [""]):
            a = c["from_joint"][:-2] + sd if sd else c["from_joint"]
            b = c["to_joint"][:-2] + sd if sd else c["to_joint"]
            if a not in J or b not in J:
                continue
            A, B = J[a]["position_cm"], J[b]["position_cm"]
            j = J[a]
            key = by_name.get(j.get("lineage"), root_key) if j.get("lineage") else root_key
            key = key if key in SWIM_DENSITY else root_key
            m = c["mass_kg"]
            n = max(1, int(math.ceil(max(c["length_cm"], 1.0) / max(c["radius_cm"], 1.0))))
            # The spine's capsules run along the back: their flesh hangs below it (the tissue profile's centre), and the
            # air (lungs, air sacs) sits in the upper part of that flesh.
            drop, half = 0.0, c["radius_cm"]
            if j.get("section") in ("Core", "Neck", "Tail") and j.get("tissue"):
                drop = sum(t[2] for t in j["tissue"]) / len(j["tissue"])
                half = sum(t[4] for t in j["tissue"]) / len(j["tissue"])
            for vol, up in ((m / FLESH_DENSITY, drop), (air[key] * m if j.get("section") in AIRY[key] else 0.0, drop + AIR_HIGH * half)):
                if vol <= 0.0:
                    continue
                r = (3.0 * vol / n / (4.0 * math.pi)) ** (1.0 / 3.0) * 100.0
                for i in range(n):
                    t = (i + 0.5) / n
                    p = [A[k] + t * (B[k] - A[k]) for k in range(3)]
                    p[2] += up
                    spheres.append((p, r, vol / n, j.get("section")))
            masses.append(([0.5 * (A[k] + B[k]) for k in range(2)] + [0.5 * (A[2] + B[2]) + drop], m, [0.5 * (A[k] + B[k]) for k in range(3)]))
    return spheres, masses


def _cap_share(d, r):
    """The share of a sphere of radius r under water when the water stands d above its centre."""
    h = max(0.0, min(2.0 * r, d + r))
    return h * h * (3.0 * r - h) / (4.0 * r ** 3)


def swim_check(rig, root_key):
    """Whether it floats, which way up, and whether it can breathe. The body (as spheres) is turned nose up or down
    and rolled, and sunk until the water it displaces weighs what it does; it settles where its centre of buoyancy
    sits straight above its centre of mass, the way that's stable. Then: is that upright, and do its nostrils clear
    the water with the neck raised as far as it goes?"""
    spheres, masses = _swim_bodies(rig, root_key)
    M = sum(m for _, m, _ in masses)
    com = [sum(p[k] * m for p, m, _ in masses) / M for k in range(3)]
    com_game = [sum(q[k] * m for _, m, q in masses) / M for k in range(3)]     # the capsules' centres, as ABFDino has it
    V = sum(v for _, _, v, _ in spheres)
    density = M / max(V, 1e-9)
    out = {"density_kg_m3": round(density, 1), "water_kg_m3": WATER_DENSITY}
    J = {j["name"]: j for j in rig["joints"]}
    if density >= WATER_DENSITY:
        out.update(verdict="sinks", summary=(f"Sinks: {density:,.0f} kg per cubic metre, denser than water. It walks along the "
                                             f"bottom, and in water deeper than its head it would drown."), speed_ms=0.0)
        return out
    rel = [([p[k] - com[k] for k in range(3)], r, v) for p, r, v, _ in spheres]

    def turned(p, pitch, roll):
        x, y, z = p
        cp, sp, cr, sr = math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
        x, z = x * cp - z * sp, x * sp + z * cp          # nose up
        y, z = y * cr - z * sr, y * sr + z * cr          # right side up
        return x, y, z

    def settle(pitch, roll):
        P = [(turned(p, pitch, roll), r, v) for p, r, v in rel]
        lo, hi = min(q[2] - r for q, r, _ in P), max(q[2] + r for q, r, _ in P)
        for _ in range(50):                               # the water line where the displaced water weighs M
            h = 0.5 * (lo + hi)
            if WATER_DENSITY * sum(v * _cap_share(h - q[2], r) for q, r, v in P) < M:
                lo = h
            else:
                hi = h
        B = [(q, WATER_DENSITY * v * _cap_share(h - q[2], r)) for q, r, v in P]
        return h, sum(b * q[0] for q, b in B) / M, sum(b * q[1] for q, b in B) / M

    def stable_root(f, lo, hi, step):
        """Where the righting turn f changes from turning up to turning down (stable), nearest to level."""
        best, prev, xs = None, None, []
        x = lo
        while x <= hi + 1e-9:
            xs.append(x)
            x += step
        vals = [f(math.radians(a)) for a in xs]
        for a0, a1, v0, v1 in zip(xs, xs[1:], vals, vals[1:]):
            if v0 > 0 >= v1:
                a = a0 + (a1 - a0) * v0 / (v0 - v1)
                if best is None or abs(a) < abs(best):
                    best = a
        return best
    pitch = stable_root(lambda a: settle(a, 0.0)[1], -80.0, 80.0, 2.0)
    pitch = 0.0 if pitch is None else pitch
    roll = 0.0
    if settle(math.radians(pitch), math.radians(4.0))[2] > 0.0:      # a small roll grows: it is top-heavy
        found = stable_root(lambda a: settle(math.radians(pitch), a)[2], 4.0, 180.0, 4.0)
        roll = found if found is not None else 90.0
    # Left to itself a top-heavy floater rolls over (Henderson's sauropods were "tipsy"); a live animal paddles to stay
    # upright, so that's a harder swim, not a failure: the rest is worked out upright.
    passive = roll
    tippy = abs(passive) > 15.0
    if tippy:
        roll = 0.0
    h, _, _ = settle(math.radians(pitch), math.radians(roll))

    # The nostrils: the snout tip, with the neck raised up to two thirds of its range (at most 60 degrees). The raise
    # is shared out the way the testing ground poses it (FBFPoser): over the neck's joints and the head, each by its
    # upward range (at most 90% of it), each bending everything beyond it.
    tip = "head_end" if "head_end" in J else "head" if "head" in J else None
    nose = J[tip]["position_cm"] if tip else com
    chain, n = [], J[tip].get("parent") if tip else None
    while n in J and (J[n].get("section") == "Neck" or n == "head"):
        chain.insert(0, n)
        n = J[n].get("parent")
    if tip == "head":
        chain.append("head")
    ups = [max((J[n].get("limits_deg") or {}).get("bend", [0.0, 0.0])[1], 0.0) for n in chain]
    total_up = sum(ups)
    lift = math.radians(min(0.66 * total_up, 60.0))

    def nostrils(up):
        pts = [list(J[n]["position_cm"]) for n in chain] + [list(nose)]
        for i, u in enumerate(ups):
            a = math.radians(min(math.degrees(up) * u / total_up, 0.9 * u)) if total_up > 0.0 else 0.0
            if not a:
                continue
            pv = pts[i]
            for p in pts[i + 1:]:
                dx, dz = p[0] - pv[0], p[2] - pv[2]
                p[0], p[2] = pv[0] + dx * math.cos(a) - dz * math.sin(a), pv[2] + dx * math.sin(a) + dz * math.cos(a)
        p = pts[-1]
        return turned([p[k] - com[k] for k in range(3)], math.radians(pitch), math.radians(roll))[2] - h
    clear = [nostrils(0.0), nostrils(lift)]
    # How far it raises its neck to keep its nostrils 30 cm clear (the testing ground poses it so; it bobs as it paddles).
    raise_deg = 0.0
    if clear[0] < 30.0:
        lo, hi = 0.0, lift
        for _ in range(30):
            mid = 0.5 * (lo + hi)
            lo, hi = (mid, hi) if nostrils(mid) < 30.0 else (lo, mid)
        raise_deg = math.degrees(hi)
    out["neck_raise_deg"] = round(raise_deg, 1)
    back = max(turned([p[k] - com[k] for k in range(3)], math.radians(pitch), math.radians(roll))[2] + r for p, r, _, sec in spheres if sec == "Core") - h
    # Speed: the legs' paddling thrust against the water's drag on the front of the body, the trunk's widest section
    # as far as it's under water. Thrust grows with leg muscle and drag with the body's section, both with size
    # squared, so a big swimmer is no faster than a small one unless its legs are stronger for its bulk.
    lc = rig.get("leg_check") or leg_check(rig)
    thrust = PADDLE_SHARE * sum(p["muscle_n"] for p in lc.get("pairs", {}).values())
    widest = (0.0, None, None)
    for j in rig["joints"]:
        if j.get("section") == "Core" and j.get("tissue"):
            for t in j["tissue"]:
                if math.pi * t[3] * t[4] > widest[0]:
                    widest = (math.pi * t[3] * t[4], j, t)
    area = 0.0
    if widest[1]:
        _, j, t = widest
        c = list(j["position_cm"])
        c[2] += t[2]
        under = h - turned([c[k] - com[k] for k in range(3)], math.radians(pitch), math.radians(roll))[2]
        u = min(max(under / max(t[4], 1e-6), -1.0), 1.0)
        area = widest[0] / 1e4 * (math.pi - math.acos(u) + u * math.sqrt(1.0 - u * u)) / math.pi
    speed = math.sqrt(2.0 * thrust / (WATER_DENSITY * SWIM_DRAG * max(area, 1e-4)))
    if tippy:
        speed *= TIPPY_COST
    speed = round(min(speed, 3.0), 2)
    out.update(thrust_n=round(thrust, 1), frontal_area_m2=round(area, 3))
    # The game floats its own centre of mass (the capsules' middles, higher up the back): its depth, at this attitude.
    game_depth = h - turned([com_game[k] - com[k] for k in range(3)], math.radians(pitch), math.radians(roll))[2]
    out.update(pitch_deg=round(pitch, 1), roll_deg=round(roll, 1), com_depth_cm=round(game_depth, 1), back_out_cm=round(back, 1),
               nostril_clear_cm=round(clear[0], 1), nostril_clear_raised_cm=round(clear[1], 1), speed_ms=speed)
    tilt = "level" if abs(pitch) < 8 else f"{abs(pitch):.0f} degrees {'nose-up' if pitch > 0 else 'nose-down'}"
    out.update(tippy=tippy, passive_roll_deg=round(passive, 1))
    tippy = (f", but it's tippy: left to itself it would roll {abs(passive):.0f} degrees onto its side, so it paddles to stay "
             f"upright, which costs it a fifth of its speed") if tippy else ""
    area_s = f"{area:.1f}" if area >= 0.95 else f"{area:.2f}"
    if clear[1] < 0.0:
        out.update(verdict="head_under", summary=(f"Floats {tilt}, but its nostrils stay {-clear[1]:.0f} cm under water even with "
                                                  f"its neck raised: it can't breathe swimming."))
    else:
        out.update(verdict="swims", summary=(f"Swims: floats {tilt} with its back {back:.0f} cm out of the water, holds its "
                                             f"nostrils {clear[1]:.0f} cm clear and paddles at about {speed:.1f} m/s (its legs "
                                             f"push {thrust:,.0f} N against the drag of {area_s} m² of body under water){tippy}."))
    return out


def refresh_base_checks():
    """The flight check (where there are wings) and the leg check on the six catalog rigs."""
    for key, share in FLIGHT_MUSCLE_SHARE.items():
        path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
        rig = load(key)
        total = rig["mass_kg"]["total"]
        if rig.get("wing_area_m2"):
            rig["flight_muscle_kg"] = round(share * total, 2)
            rig["flight_check"] = flight_check(total, rig["wing_area_m2"], share * total, WINGSPAN_M.get(key))
        rig["leg_check"] = leg_check(rig)
        rig["climb_check"] = climb_check(rig, key)
        rig["swim_check"] = swim_check(rig, key)
        save_json(path, rig)
        print(f"{key}: {rig['leg_check']['summary']}" + (f"  {rig['flight_check']['summary']}" if rig.get("flight_check") else ""))


# ---------------------------------------------------------------------------------------------- the splice
# Bone-part meshes (grow_body.py keeps them; the flesh is grown) by the section whose bones they ride.
FEATURE_SECTIONS = [("Head", ("head", "jaw", "teeth", "tongue", "eye", "eyelid", "nostril", "horn", "frill", "crest", "brow")),
                    ("Hip", ("leg", "leg2")), ("Shoulder", ("arm", "wing", "membrane", "arm2", "wing2")), ("Tail", ("tail",))]


def part_base(name):
    pieces = name.split("_")
    return "_".join(pieces[:-1]) if len(pieces) > 1 and pieces[-1].isdigit() else name


def feature_section(name):
    b = part_base(name)
    for sec, prefixes in FEATURE_SECTIONS:
        if grow_body.named(b, prefixes):      # whole words: "armor" plates aren't an "arm"
            return sec
    return "Core"


class Move:
    """A rigid move of part of the body: p -> R (p - pivot) + pivot + t."""

    def __init__(self, R=bfg.IDENTITY, pivot=(0.0, 0.0, 0.0), t=(0.0, 0.0, 0.0)):
        self.R, self.pivot, self.t = R, pivot, t

    def __call__(self, p):
        return bfg.v_add(bfg.v_add(bfg.m_vec(self.R, bfg.v_sub(p, self.pivot)), self.pivot), self.t)

    def then(self, t):
        return Move(self.R, self.pivot, bfg.v_add(self.t, t))


TILT_MAX_DEG = 45.0     # the most a body tilts onto new legs (head-up about its hips, or tail-down about its shoulders)
UNDER_SLACK_CM = 3.0    # a placed front limb whose lowest joint sits further under the ground stands on it instead
LIMB_GAP_CM = 1.0       # each side's front limb flesh stops at least this far short of the midline


def front_stance(base_j, rise):
    """Moves that raise the shoulders by rise (cm) onto front legs: the body tilts about its hips, the hind legs
    staying where they stand, as far as TILT_MAX_DEG; past that (raising only) it is lifted whole the rest of the way,
    so its hind feet hang that far off the ground and it stands on its front legs alone. Returns (moves, phi, hang,
    reached): phi the tilt (radians, + head-up), hang the lift (cm)."""
    hx0, hz0 = base_j["thigh_l"].pos[0], base_j["thigh_l"].pos[2]
    sx, sz = base_j["upperarm_l"].pos[0] - hx0, base_j["upperarm_l"].pos[2] - hz0       # shoulders, from the hips
    lever = max(math.hypot(sx, sz), 1e-6)
    t = (sz + rise) / lever
    phi = math.asin(max(min(t, 1.0), -1.0)) - math.atan2(sz, sx)
    limit = math.radians(TILT_MAX_DEG)
    reached = abs(t) <= 1.0 and abs(phi) <= limit
    phi = max(min(phi, limit), -limit)
    got = lever * math.sin(math.atan2(sz, sx) + phi) - sz          # how far the tilt alone raised the shoulders
    hang = max(0.0, rise - got) if not reached and rise > 0 else 0.0
    tilt = Move(rotation((0.0, -1.0, 0.0), phi), (hx0, 0.0, hz0))
    moves = {}
    for n in base_j:
        if base_j[n].parent is None:
            moves[n] = Move()                                      # the root stays on the ground under the hips
        elif base_j[n].section == "Hip":
            moves[n] = Move(t=(0.0, 0.0, hang))
        else:
            moves[n] = tilt.then((0.0, 0.0, hang))
    return moves, phi, hang, reached or hang > 0


def turned_about(m, R, pivot):
    """The move m, then turned by R about pivot (still one Move)."""
    t = bfg.v_add(bfg.v_sub(bfg.m_vec(R, bfg.v_sub(bfg.v_add(m.pivot, m.t), pivot)), m.pivot), pivot)
    return Move(bfg.m_mul(R, m.R), m.pivot, t)


TAIL_LIFT_MAX_DEG = 30.0   # the most a carried tail lifts at its root; beyond that it rests along the ground
TAIL_ROOT_SHARE = 0.3      # the lift is a gentle curve over this share of the tail's joints, from its root
TAIL_CLEAR_CM = 2.0        # a carried tail's flesh at least this far off the ground (or as high as it stood)


def carry_tail(base_j, frames, moves, notes):
    """A body tilted onto new legs swings its tail with it, and head-up (or tail-down) that can put the tail through the
    ground: an animal carries its tail rather than drag it, so it lifts, as one piece from the root (spread over its
    first joints so the root curves gently; a lift shared along the whole tail curled it like a scorpion's), only as
    far as it takes to bring the flesh's underside clear of the ground. A tail lifted past its joints' own range (a
    Triceratops' bends 20° up in all) is held up anyway: each turned joint's limits turn with its new rest pose,
    widened to keep it inside them (as a folded limb's are). Tails a tilt keeps clear of the ground stay as they are.
    Returns (moves, {joint: its turn in the rig's bend degrees})."""
    tail = {n for n in base_j if base_j[n].section == "Tail"}
    if not tail:
        return moves, {}
    root = next(n for n in base_j if n in tail and base_j[n].parent not in tail)
    names = subtree(base_j, root)
    under = {n: max((max(s[3], s[4]) for s in base_j[n].info.get("tissue") or []), default=0.0) for n in names}

    def low(mv):            # the flesh's underside: each joint less its flesh's half-depth
        return min(mv[n](base_j[n].pos)[2] - under[n] for n in names)

    if low(moves) >= -UNDER_SLACK_CM:
        return moves, {}
    target = min(min(base_j[n].pos[2] - under[n] for n in names), TAIL_CLEAR_CM)
    tip = far_end(base_j, root)
    chain, j = [], root
    while j in tail:
        chain.append(j)
        j = bfg.main_child(base_j[j])
    lift = chain[:max(2, round(TAIL_ROOT_SHARE * len(chain)))]

    def turned(a):          # the tail up by a degrees in all, a / len(lift) at each of its first joints
        out = dict(moves)
        for k in lift:
            R, pivot = rotation((0.0, -1.0, 0.0), -math.radians(a / len(lift))), out[k](base_j[k].pos)
            for n in subtree(base_j, k):
                out[n] = turned_about(out[n], R, pivot)
        return out

    def least(f, hi):       # the smallest angle in [0, hi] that f(angle) clears the target at (hi if none does)
        if f(hi) < target:
            return hi
        lo = 0.0
        for _ in range(40):
            mid = 0.5 * (lo + hi)
            lo, hi = (mid, hi) if f(mid) < target else (lo, mid)
        return hi

    a = least(lambda x: low(turned(x)), TAIL_LIFT_MAX_DEG)
    out = turned(a)
    deg = {k: a / len(lift) for k in lift}
    # A root too low to carry it clear (a Triceratops on Velociraptor legs): the rest of the tail lies along the ground,
    # each joint turning up just enough to keep the next off it.
    rests = low(out) < target
    if rests:
        for k, c in zip(chain, chain[1:]):
            def bent(x, k=k):
                R, pivot = rotation((0.0, -1.0, 0.0), -math.radians(x)), out[k](base_j[k].pos)
                return {**out, **{n: turned_about(out[n], R, pivot) for n in subtree(base_j, k)}}
            b = least(lambda x: bent(x)[c](base_j[c].pos)[2] - under[c], 90.0)
            if b > 0.05:
                out = bent(b)
                deg[k] = deg.get(k, 0.0) + b
    if max(deg.values(), default=0.0) * len(deg) < 0.5:
        return moves, {}
    turns, room = {}, 0.0
    for k, d in deg.items():    # in the rig's bend degrees: which sign lifts the tail at this joint
        P, _ = Fold(base_j, frames, subtree(base_j, k)).apply({k: (10.0, 0.0)})
        sign = 1.0 if P[tip][2] > base_j[tip].pos[2] else -1.0
        turns[k] = round(sign * d, 1)
        lim = (base_j[k].info.get("limits_deg") or {}).get("bend") or [0.0, 0.0]
        room += max(lim[1] if sign > 0 else -lim[0], 0.0)
    total = sum(deg.values())
    notes.append(f"its tail lifts {a:.0f}° from the root to keep off the ground"
                 + (" and the rest of it lies along the ground" if rests else "")
                 + (f" ({total:.0f}° of bend where its joints have {room:.0f}°: held so)" if total > room + 0.5 else "")
                 + ("" if low(out) >= -UNDER_SLACK_CM else ", and still drags"))
    return out, turns


def stance_note(phi, rise, hang, reached, why="so its shoulders sit on the new front legs"):
    deg = math.degrees(phi)
    note = f"the body tilts {abs(deg):.0f}° {'head-up' if deg > 0 else 'head-down'} about its hips {why} ({rise:+.0f} cm)"
    if hang > 0:
        return note + (f", as far as it goes, and is lifted the other {hang:.0f} cm: its hind feet hang {hang:.0f} cm "
                       f"off the ground, so it stands on its front legs alone")
    return note + ("" if reached else ", as far as it can: the feet don't reach the ground")


def body_moves(base_j, section, donor_j, notes, base, donor):
    """How the base's own joints move: a leg swap changes the hips' height. A biped is lifted or lowered whole; a
    quadruped tilts about its shoulders so the hips sit on the new legs, its front legs staying upright; past 45° of
    tilt it stands up on the new legs instead and carries its front limbs (it becomes a biped). New walking front legs
    on a quadruped tilt it about its hips the same way, as far as 45°, and longer ones lift it the rest of the way
    (front_stance; wings keep their own rule: walking_fold). Returns (moves, new kind, hind feet's hang in cm)."""
    moves = {n: Move() for n in base_j}
    if (section == "Shoulder" and base["body_tilt"]["kind"] == "quadruped" and donor["body_tilt"]["kind"] == "quadruped"
            and not donor.get("wing_area_m2") and "upperarm_l" in base_j and "thigh_l" in base_j and "upperarm_l" in donor_j):
        dz = donor_j["upperarm_l"].pos[2] - base_j["upperarm_l"].pos[2]
        moves, phi, hang, reached = front_stance(base_j, dz)
        notes.append(stance_note(phi, dz, hang, reached))
        return moves, None, hang
    if section != "Hip" or "thigh_l" not in donor_j:
        return moves, None, 0.0
    dz = donor_j["thigh_l"].pos[2] - base_j["thigh_l"].pos[2]
    lift = Move(t=(0.0, 0.0, dz))
    body = [n for n in base_j if base_j[n].parent is not None]       # the root stays on the ground under the hips
    if base["body_tilt"]["kind"] == "quadruped" and "upperarm_l" in base_j:
        sx, sz = base_j["upperarm_l"].pos[0], base_j["upperarm_l"].pos[2]
        hx, hz = base_j["thigh_l"].pos[0] - sx, base_j["thigh_l"].pos[2] - sz      # hips, from the shoulders
        lever = math.hypot(hx, hz)
        t = (hz + dz) / max(lever, 1e-6)
        if abs(t) <= 1.0:
            # Turn in the side plane about the shoulders by phi, (x, z) -> (x cos - z sin, x sin + z cos), until the
            # hips (behind the shoulders) sit dz higher: phi = (pi - asin t) - atan2(hz, hx).
            phi = (math.pi - math.asin(t)) - math.atan2(hz, hx)
            phi = math.atan2(math.sin(phi), math.cos(phi))
            if abs(math.degrees(phi)) <= 45.0:
                tilt = Move(rotation((0.0, -1.0, 0.0), phi), (sx, 0.0, sz))
                for n in body:
                    if base_j[n].section != "Shoulder":
                        moves[n] = tilt
                for n in body:                                # the front legs ride along upright with their socket
                    if base_j[n].section == "Shoulder":
                        top = n
                        while base_j[base_j[top].parent].section == "Shoulder":
                            top = base_j[top].parent
                        moves[n] = Move(t=bfg.v_sub(tilt(base_j[top].pos), base_j[top].pos))
                deg = math.degrees(phi)
                notes.append(f"the body tilts {abs(deg):.0f}° {'tail-down' if deg > 0 else 'tail-up'} about its shoulders "
                             f"so its hips sit on the new legs ({dz:+.0f} cm), the front legs staying upright")
                return moves, None, 0.0
        notes.append(f"its hips would move {dz:+.0f} cm, past what tilting allows: it stands up on the new legs and "
                     f"carries its front limbs (now two-legged)")
        return {n: (lift if n in body else Move()) for n in base_j}, "biped", 0.0
    notes.append(f"the body sits {abs(dz):.0f} cm {'higher' if dz > 0 else 'lower'} on the new legs")
    return {n: (lift if n in body else Move()) for n in base_j}, None, 0.0


def load_folds():
    if os.path.exists(FOLD_MEMO):
        with open(FOLD_MEMO, encoding="utf-8") as f:
            return json.load(f)
    return {}


def body_of(key, rig):
    """The hand-built or grown body behind a rig (a resized part made by compose.py keeps its species'); None for one
    of compose.py's steps, which has no body yet."""
    path = os.path.join(ROOT, "body_shapes", f"{rig.get('part_of', key)}_body.obj")
    return path if os.path.exists(path) else None


def splice(key, recipe, mode="full"):
    """mode: "full" grows the body; "part" writes the rig and bone parts (a step of compose.py); "rig" just the rig
    (compose.py's live readout)."""
    base_key, donor_key, section = recipe["base"], recipe["donor"], recipe["section"]
    base, donor = load(base_key), load(donor_key)
    bpath = os.path.join(ROOT, "rig_joints", f"{base_key}_rig.json")
    dpath = os.path.join(ROOT, "rig_joints", f"{donor_key}_rig.json")
    _, base_j, base_topo = bfg.load_rig(bpath)
    _, donor_j, donor_topo = bfg.load_rig(dpath)
    frames = bfg.bone_frames(donor_j, donor_topo)
    notes = []

    # 1. The base's own joints: a leg swap lifts, lowers or tilts the body.
    moves, new_kind, hang = body_moves(base_j, section, donor_j, notes, base, donor)
    biped = (new_kind or base["body_tilt"]["kind"]) != "quadruped"
    if section == "Shoulder":     # only limbs on the flank need the body's outline
        base_obj = body_of(base_key, base)
        if base_obj:
            width = BodyWidth(base_obj, section)
            skin = BodyWidth(base_obj, section, only=("body", "neck"))
        else:                     # a step of compose.py: its outline from the bones' tissue
            width = BodyWidth.from_rig(bpath, section)
            skin = BodyWidth.from_rig(bpath, section, trunk_only=True)
        donor_skin = BodyWidth(body_of(donor_key, donor), section, only=("body", "neck"))

    # 2. The donor's section on the base's (moved) sockets.
    taken = [n for n in donor_topo if donor_j[n].section == section]
    tops = [n for n in taken if donor_j[n].parent not in taken]
    # How far in toward the midline each of the donor's limbs' flesh reaches, from its socket, past the first quarter
    # of the limb (at the socket a thigh's flesh blends into the body past the midline): its outline as grown from its
    # tissue, so the gap below is the flesh's own (from the stations' radii it pushed a T. rex's own legs out 25 cm).
    limb_outline = {}
    if section in ("Hip", "Shoulder"):
        np = grow_body.np
        dtissue = {n: j.info["tissue"] for n, j in donor_j.items() if j.info.get("tissue")}
        for ch, prof, zs, Yv in grow_body.chain_sweeps(donor_j, frames, dtissue):
            if ch.name in tops:
                O = grow_body.section_outline(ch, prof, zs, Yv)
                s = np.repeat(ch.s / max(ch.s[-1], 1e-9), len(O) // len(ch.s))
                limb_outline[ch.name] = O[s > 0.25] - np.array(donor_j[ch.name].pos)     # from its socket
        if section == "Hip":
            base_obj = body_of(base_key, base)
            trunk = BodyWidth(base_obj, section, only=("body", "neck")) if base_obj else \
                BodyWidth.from_rig(bpath, section, trunk_only=True)

    # Where a walking foot rests: on the ground as the base's own feet do (a resized part's own feet can sit under its
    # own ground, and a walking limb aimed there went through it)
    ground_at = max(min(base_j[n].pos[2] for n in base_j if base_j[n].section == "Hip"), 0.0) if "thigh_l" in base_j else 0.0

    def gap_push(top, socket, body):
        """How far out a limb (as it stands on its donor) must go for its flesh to stop LIMB_GAP_CM short of the
        midline, 0 if it does already: limbs far thicker than the body is wide (Brachiosaurus arms on a Velociraptor,
        T. rex legs on a Pteranodon) crossed it and grew into one column with the other side's. Only its flesh outside
        the body counts (a T. rex's own thighs meet inside its belly, and were pushed out 22 cm)."""
        if top not in limb_outline:
            return 0.0
        P = limb_outline[top] + grow_body.np.array(socket)
        outside = [abs(y) > body.at(x, z, reach=0) for x, y, z in P]
        if not any(outside):
            return 0.0
        sign = 1.0 if socket[1] > 0 else -1.0
        return max(0.0, LIMB_GAP_CM - float((sign * P[outside, 1]).min()))

    def place_section(moves, walk_at=None):
        """Each side's limb on its (moved) socket. walk_at: it walks, its lowest joint that high (cm)."""
        pos, place, mounted, said = {}, {}, {}, []
        for top in tops:
            if top not in base_j:
                raise ValueError(f"{key}: the base has no socket '{top}' for the donor's {section} section")
            side_names = subtree(donor_j, top)
            socket = moves[top](base_j[top].pos)
            bends = {}
            if section == "Hip":
                # Legs far thicker than the hips are wide (T. rex legs on a Pteranodon) grew into one column the same way:
                # mounted out until each side's flesh clears the midline, the flesh grown from the old socket out to it.
                sign = 1.0 if socket[1] > 0 else -1.0
                gap = gap_push(top, socket, trunk.moved(moves[base_j[top].parent]) if base_j[top].parent in moves else trunk)
                if gap > 0.5:
                    mounted[top] = socket
                    socket = (socket[0], socket[1] + sign * gap, socket[2])
                    said.append(f"{top}: mounted {gap:.0f} cm out from the {recipe.get('root_name', base['creature'])}'s socket, "
                                f"clear of the other side's (its flesh is wider than the hips)")
            if section == "Shoulder":
                # Mounted on the skin, at the widest of the body there (a wing flaps clear of a barrel body); the grown
                # flesh runs from the old socket out to it, like a shoulder girdle reaching the body wall.
                out = mount_out(skin, donor_skin, base_j[top].pos, donor_j[top].pos)
                sign = 1.0 if socket[1] > 0 else -1.0
                # Out of the other side's way before it is posed (posed first, a limb hanging inside a narrow chest was
                # turned as far as its shoulder goes to get clear of the body)
                gap = gap_push(top, (socket[0], socket[1] + sign * out, socket[2]),
                               skin.moved(moves[base_j[top].parent]) if base_j[top].parent in moves else skin)
                if out + gap > 0.5:
                    mounted[top] = socket
                    socket = (socket[0], socket[1] + sign * (out + gap), socket[2])
                    said.append(f"{top}: mounted {out + gap:.0f} cm out from the {recipe.get('root_name', base['creature'])}'s socket, "
                                + (f"clear of the other side's (its flesh is wider than the body)" if gap > 0.5 else "at the widest of its body"))
                out += gap
                chain = bend_chain(donor_j, top)
                body_w = width.moved(moves[base_j[top].parent]) if base_j[top].parent in moves else width
                if walk_at is None and biped and donor["body_tilt"]["kind"] == "quadruped":
                    # The search takes a while: remembered, so the editor's readout stays live.
                    memo = json.dumps([top, socket, [(n, donor_j[n].pos) for n in side_names], [(n, j.pos) for n, j in base_j.items()]])
                    memo = hashlib.md5(memo.encode()).hexdigest()
                    folds = load_folds()
                    if memo in folds:
                        bends = {n: tuple(v) for n, v in folds[memo].items()}
                    else:
                        bends = carried_fold(donor_j, frames, side_names, chain, socket, base_j, body_w)
                        folds[memo] = bends
                        save_json(FOLD_MEMO, folds)
                    how = "carried folded along the body"
                else:
                    aim = walk_at
                    if aim is None and min(donor_j[n].pos[2] for n in side_names) < 0.0:
                        aim = ground_at
                    bends = walking_fold(donor_j, frames, side_names, chain, socket, body_w, aim)
                    how = "walking, shoulder turned to put the foot on the ground"
                turned = ", ".join(f"{n} bend {b:+.0f}°" + (f" side {sd:+.0f}°" if sd else "")
                                   for n, (b, sd) in bends.items() if round(b) or round(sd))
                said.append(f"{top}: {how}; " + (f"turned {turned}" if turned else "as it stands on its donor"))
            P, R = Fold(donor_j, frames, side_names).apply(bends)
            off = bfg.v_sub(socket, donor_j[top].pos)
            for n in side_names:
                pos[n] = tuple(round(c + o, 1) for c, o in zip(P[n], off))
            place[top] = (bends, off, P, R)
        return pos, place, mounted, said

    pos, place, mounted, said = place_section(moves)
    # Front limbs too long for the body (a Brachiosaurus arm on a Velociraptor's low shoulders, longer than a tilt can
    # make room for, or folded and still through the ground): it stands on them. Placed walking on its own sockets,
    # the limb's lowest joint tells how far the shoulders must rise; front_stance tilts the body head-up about its hips
    # and lifts it the rest of the way (its hind feet then hang off the ground).
    if section == "Shoulder" and "thigh_l" in base_j and "upperarm_l" in base_j and pos \
            and min(p[2] for p in pos.values()) < -UNDER_SLACK_CM:
        low_d = min(donor_j[n].pos[2] for n in taken)
        feet = [base_j[n].pos[2] for n in base_j if base_j[n].section == "Hip"]
        rest = low_d if donor["body_tilt"]["kind"] == "quadruped" and low_d >= 0.0 else max(min(feet), 0.0)
        # (the limb as it stands on its donor, unturned: measured turned as far as its shoulder goes, the body came up
        # short and the arm stayed turned 25° to reach the ground, its hand tipped 19 cm into it)
        rise = rest - min(base_j[t].pos[2] + min(donor_j[n].pos[2] for n in subtree(donor_j, t)) - donor_j[t].pos[2]
                          for t in tops)
        moves, phi, hang, reached = front_stance(base_j, rise)
        pos, place, mounted, said = place_section(moves, rest)
        notes[:] = [n for n in notes if not n.startswith("the body tilts")]
        notes.append(stance_note(phi, rise, hang, reached, "so the front limbs, too long to carry, stand on the ground"
                                 if biped else "so the front feet stand on the ground"))
        new_kind, biped = "quadruped", False
    notes += said
    tail_turns = {}
    if section != "Tail":       # a tilt swings the base's own tail with the body
        moves, tail_turns = carry_tail(base_j, bfg.bone_frames(base_j, base_topo), moves, notes)

    # 3. Base joints hanging off the swapped section (the head on a swapped neck) go where the donor's own were.
    base_sec = {n for n in base_j if base_j[n].section == section}
    reparent = {}
    for n in base_j:
        if n in base_sec or base_j[n].parent not in base_sec:
            continue
        if n not in donor_j or donor_j[n].parent not in pos:
            raise ValueError(f"{key}: nothing on the donor's {section} section for the base's '{n}' to hang from")
        top = next(t for t in tops if donor_j[n].parent in subtree(donor_j, t))
        new = bfg.v_add(donor_j[n].pos, place[top][1])
        delta = bfg.v_sub(new, moves[n](base_j[n].pos))
        for m in subtree(base_j, n):
            moves[m] = moves[m].then(delta)
        reparent[n] = donor_j[n].parent
        notes.append(f"its {n} rides on the {donor['creature']}'s {section.lower()}, {bfg.v_len(delta):.0f} cm from where it was")

    # 4. The joint list: the base's (moved) without its section, the donor's section where the base's started.
    donor_name = donor["creature"]
    joints = []
    for j in base["joints"]:
        if j["name"] in base_sec:
            continue
        j = copy.deepcopy(j)
        j["position_cm"] = [round(c, 1) for c in moves[j["name"]](tuple(j["position_cm"]))]
        if j["name"] in reparent:
            j["parent"] = reparent[j["name"]]
        turn = tail_turns.get(j["name"])
        if turn and j.get("limits_deg"):         # a carried tail's rest pose turned: its limits with it, rest inside
            lo, hi = j["limits_deg"]["bend"]
            j["limits_deg"]["bend"] = [round(min(lo - turn, 0.0), 1), round(max(hi - turn, 0.0), 1)]
            j["splice_note"] = f"rest pose turned bend {turn:+.0f}° to carry the tail, so its limits turned with it"
        joints.append(j)
    first = next(i for i, j in enumerate(base["joints"]) if j["name"] in base_sec)
    insert = sum(1 for j in base["joints"][:first] if j["name"] not in base_sec)
    bends_all = {}
    for bends, _, _, _ in place.values():
        bends_all.update(bends)
    new = []
    for j in donor["joints"]:
        if j["name"] not in taken:
            continue
        j = copy.deepcopy(j)
        j["position_cm"] = list(pos[j["name"]])
        # (a bone swapped in on its own, compose.py's Advanced mode, keeps the species it came from)
        j["lineage"] = j.get("lineage") or donor_name
        if j["name"] in tops:      # on the base's socket (a Brachiosaurus' head has a neck_06 behind it; this neck may not)
            j["parent"] = base_j[j["name"]].parent
        if j["name"] in mounted:
            j["mount_from_cm"] = [round(c, 1) for c in mounted[j["name"]]]
        turn = bends_all.get(j["name"])
        if turn and any(turn) and j.get("limits_deg"):
            for axis, a in zip(("bend", "side"), turn):
                lo, hi = j["limits_deg"][axis]
                j["limits_deg"][axis] = [round(lo - a, 1), round(hi - a, 1)]
            j["splice_note"] = (f"rest pose turned bend {turn[0]:+.0f}°, side {turn[1]:+.0f}° from the {donor_name}'s, "
                                f"so its limits turned with it")
        new.append(j)
    joints[insert:insert] = new
    jpos = {j["name"]: tuple(j["position_cm"]) for j in joints}

    # 5. Mass and capsules: the base's section out, the donor's in; the balance point from the capsules as they sit.
    out_caps = [c for c in base["collision_capsules"] if c["from_joint"] in base_sec]
    in_caps = [copy.deepcopy(c) for c in donor["collision_capsules"] if c["from_joint"] in taken]
    caps = [c for c in base["collision_capsules"] if c not in out_caps]
    first_cap = next((i for i, c in enumerate(base["collision_capsules"]) if c in out_caps), len(caps))
    caps[first_cap:first_cap] = in_caps
    # The capsules carry the weight in the game, so the swap moves their mass. The section's own figure comes with the
    # donor's section, and so does the flesh at its root that its capsules carry but the breakdown counts as Core (a
    # tail's thick base): the total stays the capsules' sum.
    cap_kg = lambda cs: sum(c["mass_kg"] * (2 if "(l, r)" in c["bone"] else 1) for c in cs)
    by_section = dict(base["mass_kg"]["by_section"])
    sec_key = next(k for k in by_section if k.startswith(section))
    dsec_key = next(k for k in donor["mass_kg"]["by_section"] if k.startswith(section))
    core_key = next(k for k in by_section if k.startswith("Core"))
    root_out = cap_kg(out_caps) - by_section[sec_key]
    root_in = cap_kg(in_caps) - donor["mass_kg"]["by_section"][dsec_key]
    by_section[sec_key] = donor["mass_kg"]["by_section"][dsec_key]
    by_section[core_key] = round(by_section[core_key] - root_out + root_in, 2)
    total = round(sum(by_section.values()), 2)
    acc, msum = [0.0, 0.0, 0.0], 0.0
    for c in caps:
        sides = ["_l", "_r"] if "(l, r)" in c["bone"] else [""]
        for sd in sides:
            a = c["from_joint"][:-2] + sd if sd else c["from_joint"]
            b = c["to_joint"][:-2] + sd if sd else c["to_joint"]
            if a in jpos and b in jpos:
                mid = [0.5 * (p + q) for p, q in zip(jpos[a], jpos[b])]
                acc = [s_ + c["mass_kg"] * m for s_, m in zip(acc, mid)]
                msum += c["mass_kg"]
    balance = [round(a / max(msum, 1e-9), 1) for a in acc]
    lineage = {base["creature"]: round(total - cap_kg(in_caps), 2), donor_name: round(cap_kg(in_caps), 2)}

    rig = copy.deepcopy(base)
    rig["creature"] = recipe["name"]
    rig["splice"] = {"base": base_key, "donor": donor_key, "section": section,
                     "swapped": f"{section} section" + (", both sides" if len(tops) > 1 else ""),
                     "lineage_kg": lineage, "lineage_share": {k: round(v / total, 3) for k, v in lineage.items()},
                     "wings": bool(section == "Shoulder" and donor.get("wing_area_m2")),
                     "notes": notes, "made_by": "splice/splice.py (body grown by body_tools/grow_body.py)"}
    if new_kind:
        rig["body_tilt"]["kind"] = new_kind
    if hang > 0.5:      # it stands on its front legs alone: the leg check, balance and the game leave the hind ones out
        rig["hind_hang_cm"] = round(hang, 1)
    rig["joints"] = joints
    rig["mass_kg"] = {"total": total, "by_section": by_section}
    rig["balance_point_cm"] = balance
    rig["collision_capsules"] = caps
    for k in ("flight_pose_cm", "flight_rule", "wing_area_m2", "flight_check", "flight_muscle_kg", "membrane_hip_offset_cm"):
        rig.pop(k, None)

    # 6. Flight: wings bring their area and spread pose; flight muscle stays with the torso.
    wings_from = donor if section == "Shoulder" and donor.get("wing_area_m2") else base if base.get("wing_area_m2") else None
    if wings_from is not None:
        flight = {}
        if wings_from is donor:
            fp = donor["flight_pose_cm"]
            for top in tops:
                shift = bfg.v_sub(jpos[top], tuple(fp[top]))
                for n in subtree(donor_j, top):
                    if n in fp:
                        flight[n] = [round(c + s_, 1) for c, s_ in zip(fp[n], shift)]
        else:   # the base's own wings: its flight pose, without the swapped section's joints
            flight = {n: v for n, v in base["flight_pose_cm"].items() if n in jpos and n not in base_sec}
        if biped and "thigh_l" in jpos:   # the legs trail back in flight, two thirds of the way to the hip's limit
            rig_tmp = copy.deepcopy(rig)
            tmp_path = os.path.join(HERE, f"_{key}_tmp_rig.json")
            save_json(tmp_path, rig_tmp)
            _, tj, ttopo = bfg.load_rig(tmp_path)
            os.remove(tmp_path)
            tfr = bfg.bone_frames(tj, ttopo)
            for side in ("_l", "_r"):
                thigh = "thigh" + side
                lo = tj[thigh].info["limits_deg"]["bend"][0]
                P2, _ = Fold(tj, tfr, subtree(tj, thigh)).apply({thigh: (0.66 * lo, 0.0)})
                for n in subtree(tj, thigh):
                    flight[n] = [round(c, 1) for c in P2[n]]
        rig["flight_pose_cm"] = flight
        rig["flight_rule"] = wings_from.get("flight_rule")
        rig["wing_area_m2"] = wings_from["wing_area_m2"]
        thigh = base_j.get("thigh_l")
        if wings_from is donor and thigh:
            rig["membrane_hip_offset_cm"] = [0.0, -round(max(0.0, skin.widest(thigh.pos[0]) - 2.0 - abs(thigh.pos[1])), 1), 0.0]
    # Flight muscle stays with the torso (a step of compose.py already carries its base's).
    muscle = base["flight_muscle_kg"] if "splice" in base else FLIGHT_MUSCLE_SHARE[base_key] * base["mass_kg"]["total"]
    rig["flight_muscle_kg"] = round(muscle, 2)
    if wings_from is not None:
        heavy = ""
        names = [n for k_ in (recipe.get("root", base_key), donor.get("part_of", donor_key))
                 for n, _ in bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{k_}_features.obj"))[1]]
        if any(n.startswith("armor") for n in names):
            heavy = "Armour and solid bone add too much mass for these wings"
        span = WINGSPAN_M.get(donor.get("part_of", donor_key) if wings_from is donor else recipe.get("root", base_key))
        rig["flight_check"] = flight_check(total, rig["wing_area_m2"], muscle, span, heavy)
    rig["leg_check"] = leg_check(rig)
    rig["gaits"], rig["gait_check"] = splice_gaits(rig, recipe.get("root", base_key))
    rig["climb_check"] = climb_check(rig, recipe.get("root", base_key))
    rig["swim_check"] = swim_check(rig, recipe.get("root", base_key))
    rig_path = os.path.join(ROOT, "rig_joints", f"{key}_rig.json")
    save_json(rig_path, rig)

    # 7. The body: the base's bone parts (moved with their bones) without its section's, the donor's section parts
    # placed like its bones; then the flesh is grown around the whole skeleton.
    if mode == "rig":
        return rig
    feats = write_features(key, base_key, donor_key, section, base_j, donor_j, frames, moves, place, tops)
    if mode == "full":
        grow_body.grow(rig_path, feats, os.path.join(ROOT, "body_shapes", f"{key}_body.obj"))
        report(key, recipe, rig, notes)
    return rig


def write_features(key, base_key, donor_key, section, base_j, donor_j, frames, moves, place, tops):
    bv, bparts = bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{base_key}_features.obj"))
    dv, dparts = bfg.load_obj(os.path.join(ROOT, "body_shapes", f"{donor_key}_features.obj"))
    with open(os.path.join(ROOT, "body_shapes", f"{base_key}_features.obj"), encoding="utf-8") as f:
        header = [ln.rstrip("\n") for ln in f if ln.startswith("#")]
    to_ue = lambda v: bfg.m_vec(bfg.UE_FROM_OBJ, v)
    from_ue = bfg.m_inv(bfg.UE_FROM_OBJ)
    live_b = [n for n in base_j if base_j[n].deforming and base_j[n].section != section]
    out = []
    for name, tris in bparts:
        if feature_section(name) == section:
            continue
        idx = sorted({i for t in tris for i in t})
        P = [to_ue(bv[i]) for i in idx]
        cen = tuple(sum(p[k] for p in P) / len(P) for k in range(3))
        ride = min(live_b, key=lambda n: bfg.v_len(bfg.v_sub(base_j[n].pos, cen)))   # the bone it rides (rigidly)
        mv = moves[ride]
        out.append((name, {i: bfg.m_vec(from_ue, mv(p)) for i, p in zip(idx, P)}, tris, None))
    for name, tris in dparts:
        if feature_section(name) != section or part_base(name).startswith("membrane"):
            continue
        idx = sorted({i for t in tris for i in t})
        P = [to_ue(dv[i]) for i in idx]
        cen = tuple(sum(p[k] for p in P) / len(P) for k in range(3))
        top = tops[0] if len(tops) == 1 else next(t for t in tops if t.endswith("_l" if cen[1] < 0 else "_r"))
        bends, off, Pnew, R = place[top]
        if bends:     # re-posed like the limb's bones (blended by starter skin weights)
            own, rigid = bfg.part_bones(name, cen, donor_j)
            weights = bfg.skin_part(P, own, donor_j, rigid)
            moved = {}
            for i, p, w in zip(idx, P, weights):
                acc = [0.0, 0.0, 0.0]
                for b, wt in w.items():
                    q = bfg.v_add(Pnew[b], bfg.m_vec(R[b], bfg.v_sub(p, donor_j[b].pos))) if b in R else p
                    acc = [a + wt * c for a, c in zip(acc, q)]
                moved[i] = bfg.m_vec(from_ue, bfg.v_add(tuple(acc), off))
        else:
            moved = {i: bfg.m_vec(from_ue, bfg.v_add(p, off)) for i, p in zip(idx, P)}
        out.append((name, moved, tris, donor_key))
    lines = header + [f"# Bone parts for the splice {key}: the {base_key}'s, with the {donor_key}'s {section} section parts."]
    count = 0
    for name, verts, tris, origin in out:
        used = sorted({i for t in tris for i in t})
        remap = {}
        if origin:
            lines.append(f"# lineage {name}: {origin}")
        lines.append(f"o {name}")
        for i in used:
            count += 1
            remap[i] = count
            v = verts[i]
            lines.append(f"v {v[0]:.2f} {v[1]:.2f} {v[2]:.2f}")
        for t in tris:
            lines.append("f " + " ".join(str(remap[i]) for i in t))
    path = os.path.join(ROOT, "body_shapes", f"{key}_features.obj")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return path


def report(key, recipe, rig, notes):
    sp = rig["splice"]
    lines = [f"{rig['creature']}  ({key})", "",
             f"Base {recipe['base']}, {recipe['section']} section from {recipe['donor']}.",
             "Lineage by mass: " + ", ".join(f"{k} {100 * v:.{0 if v >= 0.01 else 2}f}%" for k, v in sp["lineage_share"].items()),
             f"Mass {rig['mass_kg']['total']:,.2f} kg; balance point {rig['balance_point_cm']} cm", ""]
    lines += notes + [""]
    lc = rig["leg_check"]
    lines += [f"Leg check: {lc['verdict']}", lc["summary"]]
    for label, p in lc["pairs"].items():
        lines.append(f"  {label}: muscle {p['muscle_n'] / 1000:,.1f} kN for {p['load_n'] / 1000:,.1f} kN of weight (x{p['ratio']})")
    fc = rig.get("flight_check")
    if fc:
        lines += ["", f"Flight check: {fc['verdict']}", fc["summary"]]
        if "wing_loading_n_m2" in fc:
            lines.append(f"  weight {fc['weight_n']:,.0f} N / wing area {fc['wing_area_m2']} m² = {fc['wing_loading_n_m2']:,.0f} N/m² "
                         f"(limit {fc['wing_loading_limit']:.0f}); flight muscle {fc['flight_muscle_kg']:,.2f} kg = "
                         f"{100 * fc['flight_muscle_share']:.1f}% (needs {100 * fc['flight_muscle_min']:.0f}%)")
    text = "\n".join(lines) + "\n"
    with open(os.path.join(HERE, f"{key}_report.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(text)


def main(argv):
    refresh_base_checks()
    for key in (argv or list(RECIPES)):
        splice(key, RECIPES[key])


if __name__ == "__main__":
    main(sys.argv[1:])
