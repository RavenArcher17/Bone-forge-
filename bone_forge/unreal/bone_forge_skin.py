"""Bone Forge skins: each species' colour scheme, skin tiles, and where on the body each one goes.

The models have no UVs, so the skin is painted in two layers that need none:
  - bone_forge_gltf.py paints every vertex (COLOR_0) with where it is on the animal: how much belly (R), how much
    pattern (G: the T. rex's saddles, the raptor's bars, the Triceratops' mottling...), how much accent (B: a warm
    face, the Triceratops frill rim, the Pteranodon crest), and which skin region it is in (A: 0 body, 0.5 feet and
    hands, 1 face; the feet fade in from the body at the ankle, so nothing between them may sit at a third value), from the colour schemes in body_shapes/colour_schemes.png and the Skin Textures tab of the design doc.
  - the Unreal material (bone_forge_materials.py) projects the seamless scale tiles (textures/tiles, from the
    design doc) onto the body from its standing pose, picks each region's tile by the vertex colour, and lifts the
    scales out of the surface with their height map.

Standard-library Python, used by bone_forge_gltf.py (any Python) and bone_forge_materials.py (Unreal's Python).
"""
import math


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


# Colours from body_shapes/colour_schemes.png (back, belly, pattern, accent), plus keratin for horns, spikes and the
# Pteranodon beak. Tiles: (back, belly, face, feet) from textures/tiles/README.txt and the Skin Textures tab.
# tile_cm: how much body one tile covers (bigger animals, bigger scales); bump: scale height in cm.
SKIN = {
    "trex": dict(back="#5b5137", belly="#cdbf9b", pattern="#3c3625", accent="#8a5a35", keratin="#3a3226",
                 tiles=("trex_pebble", "trex_pebble", "trike_frill_polygons", "theropod_foot"), tile_cm=40.0, bump=1.2,
                 pattern_kind="saddles", accent_kind="face", eye_fwd=28.0, iris="#c99a2e"),
    "trike": dict(back="#6e6250", belly="#c9bba0", pattern="#544a3c", accent="#a2562f", keratin="#e3d8bd",
                  tiles=("trike_cone_scales", "trike_cone_scales", "trike_frill_polygons", "trike_frill_polygons"),
                  tile_cm=45.0, bump=1.4, pattern_kind="mottle", accent_kind="frill_rim", eye_fwd=15.0, iris="#6b4a2a"),
    "raptor": dict(back="#3a2f28", belly="#b9a58a", pattern="#e8dfcf", accent="#3a2f28", keratin="#2c2620",
                   tiles=("raptor_feathers", "raptor_feathers", "trike_frill_polygons", "theropod_foot"), tile_cm=9.0,
                   bump=0.15, pattern_kind="bars", accent_kind=None, eye_fwd=25.0, iris="#d8a12a"),
    "ankylo": dict(back="#7a4a32", belly="#c7a98a", pattern="#4f3324", accent="#4f3324", keratin="#3b2a20",
                   tiles=("ankylo_armor", "trex_pebble", "ankylo_armor", "trex_pebble"), tile_cm=35.0, bump=1.6,
                   pattern_kind="armour", accent_kind=None, eye_fwd=12.0, iris="#5a3d24"),
    "brachio": dict(back="#6d7363", belly="#bfc0ae", pattern="#575c4f", accent="#575c4f", keratin="#3d3a33",
                    tiles=("sauropod_mixed", "sauropod_mixed", "trike_frill_polygons", "trex_pebble"), tile_cm=70.0,
                    bump=1.8, pattern_kind="mottle_neck", accent_kind=None, eye_fwd=12.0, iris="#4b3a26"),
    "ptera": dict(back="#4a4540", belly="#e2ded6", pattern="#4a4540", accent="#b5452a", keratin="#d8c79a",
                  tiles=("ptera_fuzz", "ptera_fuzz", "ptera_fuzz", "theropod_foot"), tile_cm=12.0, bump=0.15,
                  pattern_kind="dark_wings", accent_kind="crest", membrane_tile="ptera_membrane", eye_fwd=10.0, iris="#8a3a1c"),
}
# Spliced creatures (splice/splice.py) wear their base species' skin. wings: Pteranodon wings in place of the arms,
# which take the body's scales (their small fingers the feet tile) instead of a hand's.
SCUTE_TOWARDS_BELLY = 0.35
SKIN["raptorwings"] = dict(SKIN["raptor"], base="raptor", wings=True)
SKIN["ankylowings"] = dict(SKIN["ankylo"], base="ankylo", wings=True)
# theropod_foot (body_tools/foot_tile_blender.py) replaced foot_scutes on the T. rex, raptor and Pteranodon feet: its
# regular brick rows looked man-made laid across the toes (Raven, 2026-11-03).
TILES = ["trex_pebble", "trike_cone_scales", "trike_frill_polygons", "ankylo_armor", "sauropod_mixed",
         "foot_scutes", "theropod_foot", "raptor_feathers", "ptera_fuzz", "ptera_membrane"]


def scute_rgb(s):
    """The toes' colour (gamma 0 to 1): the species' keratin, a little way towards its pale belly. Toes and claws in one
    dark colour ran together into a black mass on the T. rex; this way the claws stand out at their ends."""
    k, b = hex_rgb(s["keratin"]), hex_rgb(s["belly"])
    return tuple(a + SCUTE_TOWARDS_BELLY * (c - a) for a, c in zip(k, b))


def base_colour(key, mat, default):
    """The glTF base colour for a material slot: the species' keratin and membrane colours (linear RGBA); the skin
    slot gets its colours from the skin material instead (bone_forge_materials.py)."""
    s = SKIN.get(key)
    if s and mat in ("Keratin", "Scute", "Membrane", "Skin"):
        r, g, b = scute_rgb(s) if mat == "Scute" else hex_rgb(s["keratin"] if mat == "Keratin" else s["back"])
        return (r ** 2.2, g ** 2.2, b ** 2.2, 1.0)
    return default


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def _hash(i, j, k):
    n = (i * 73856093) ^ (j * 19349663) ^ (k * 83492791)
    n = (n << 13) ^ n
    return ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 0x7fffffff


def noise(x, y, z):
    """Smooth value noise in [0, 1], one cell per unit."""
    i, j, k = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - i, y - j, z - k
    ux, uy, uz = (f * f * (3 - 2 * f) for f in (fx, fy, fz))
    v = 0.0
    for di in (0, 1):
        for dj in (0, 1):
            for dk in (0, 1):
                w = (ux if di else 1 - ux) * (uy if dj else 1 - uy) * (uz if dk else 1 - uz)
                v += w * _hash(i + di, j + dj, k + dk)
    return v


def material_for(key, base, centroid, joints):
    """Material for a triangle of part `base` with this centroid (Unreal cm): Skin, Keratin, Membrane, Teeth, Mouth,
    Eye (eyeballs and nostrils), or the blink's eyelid sets LidHalf / LidClosed (hidden until the game blinks)."""
    if base.startswith(("eye_", "nostril")):
        return "Eye"
    if base.startswith("eyelid_half"):
        return "LidHalf"
    if base.startswith("eyelid_closed"):
        return "LidClosed"
    if base.startswith("membrane"):
        return "Membrane"
    if base.startswith("teeth"):
        return "Teeth"
    if base.startswith("tongue"):
        return "Mouth"
    if base.startswith(("horn", "armor_spikes", "leg_claws", "arm_claws", "leg2_claws", "arm2_claws")):
        return "Keratin"
    if base.startswith(("leg_toes", "arm_fingers", "leg2_toes", "arm2_fingers")):
        return "Scute"       # keratin's colour, keeping the feet's scales (flat colour looked like plastic on a whole toe)
    if key == "ptera" and base.startswith(("head", "jaw")):
        h, e = joints["head"], joints["head_end"]
        if centroid[0] > h[0] + 0.3 * (e[0] - h[0]):      # the long toothless beak
            return "Keratin"
    return "Skin"


def paint(key, base, p, n, joints, part_stats):
    """Vertex colour (R belly, G pattern, B accent, A region) for a vertex of part `base` at p with normal n (Unreal
    cm, X forward, Y right, Z up). joints: rig joint positions; part_stats: {base: (centroid, radius)}."""
    s = SKIN[key]
    wings = s.get("wings", False)
    key = s.get("base", key)          # a splice is painted by its base species' rules
    x, y, z = p
    nz = n[2]
    if base.startswith(("eye_", "nostril")):      # Eye material: R iris, G pupil
        if base.startswith("nostril"):
            return (0.0, 1.0, 0.0, 0.5)
        c, r = part_stats[base]
        f = math.radians(s.get("eye_fwd", 15.0))
        ax = (math.sin(f), (1 if c[1] > 0 else -1) * math.cos(f), 0.1)
        L = math.sqrt(sum(a * a for a in ax))
        d = [p[i] - c[i] for i in range(3)]
        dl = math.sqrt(sum(a * a for a in d)) or 1.0
        cosang = sum(d[i] * ax[i] for i in range(3)) / (dl * L)
        return (smooth(0.62, 0.72, cosang), smooth(0.93, 0.96, cosang), 0.0, 0.5)
    hip = joints.get("pelvis", (0, 0, 100))[2]
    # Region: face (1), feet and hands (0.5) or body (0). The feet fade in from the leg over a wide band above the
    # ankle, so the leg's scales give way to the foot's gradually instead of at a line.
    region = 0.0
    if base.startswith(("head", "jaw", "crest", "frill", "horn", "eyelid", "brow")):
        region = 1.0
    elif base.startswith(("leg_toes", "arm_fingers", "leg2_toes", "arm2_fingers")):
        region = 0.5
    elif base.startswith(("leg", "arm")):
        side = ("_2" if base.startswith(("leg2", "arm2")) else "") + ("_l" if y < 0 else "_r")   # a third pair's own bones
        if base.startswith("leg"):
            ankle = joints.get("foot" + side, (0, 0, 0.1 * hip))[2]
            region = 0.5 * (1.0 - smooth(ankle * 0.95, ankle * 1.6, z))
        elif wings:
            region = 0.0
        elif key in ("trex", "raptor"):          # hands: the fingers and claws beyond the wrist
            w, e = joints["hand" + side], joints["lowerarm" + side]
            dw = math.dist(p, w)
            de = math.dist(p, e)
            region = 0.5 * smooth(1.0, 0.55, dw / max(de, 1e-3))
        elif key == "ptera":
            region = 0.5 * (1.0 - smooth(8.0, 16.0, z))
        else:
            wrist = joints.get("hand" + side, (0, 0, 0.1 * hip))[2]
            region = 0.5 * (1.0 - smooth(wrist * 0.95, wrist * 1.6, z))
    # Belly: countershading from the normal (pale where the skin faces down), paler lower legs.
    belly = 0.0 if base == "wing_flesh" else smooth(0.15, -0.55, nz)
    if (base.startswith("leg") or (base.startswith("arm") and not wings)) and key != "ptera":
        belly = max(belly, 0.6 * smooth(0.55, 0.12, z / max(hip, 1.0)))
    if key == "ptera":       # a pale seabird body under a dark back
        belly = max(belly, 0.85 * smooth(0.55, 0.2, nz)) if not base.startswith(("arm", "wing")) else belly
    # Pattern.
    kind = s["pattern_kind"]
    pat = 0.0
    top = smooth(-0.2, 0.3, nz)
    if kind == "saddles" and base.startswith(("body", "neck", "head")):
        pat = smooth(0.25, 0.6, math.sin(2 * math.pi * x / 115.0)) * top
    elif kind in ("mottle", "mottle_neck") and base.startswith(("body", "neck", "leg", "arm", "frill")):
        cell = 45.0 if kind == "mottle" else 90.0
        pat = 0.8 * smooth(0.52, 0.68, noise(x / cell, y / cell, z / cell)) * (1 - belly)
        if kind == "mottle_neck" and base.startswith(("body", "neck")):
            pat = max(pat, 0.7 * smooth(480.0, 620.0, x) * top)      # the darker top of the neck
    elif kind == "bars":
        if base.startswith("wing_feathers"):
            pat = smooth(0.3, 0.7, math.sin(2 * math.pi * (x - z) / 4.0))
        elif base.startswith(("body", "tail")) and x < -20.0:
            pat = smooth(0.4, 0.75, math.sin(2 * math.pi * x / 9.0)) * top
    elif kind == "armour" and base.startswith(("armor", "tail_club")):
        pat = 1.0
    elif kind == "dark_wings" and base.startswith(("arm", "wing")):
        pat = 1.0
    # Accent.
    acc = 0.0
    ak = s["accent_kind"]
    if ak == "face" and region == 1.0:
        acc = 0.35 * (1 - belly) * (1 - 0.6 * top)       # a warm tint on the cheeks and jaw, less on top
    elif ak == "frill_rim" and base.startswith("frill"):
        c, r = part_stats[base]
        acc = smooth(0.62, 0.85, math.dist(p, c) / max(r, 1e-3))
    elif ak == "crest" and base.startswith("crest"):
        acc = 1.0
    return (belly, pat, acc, region)
