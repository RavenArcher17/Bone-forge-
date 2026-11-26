"""Bone Forge skins in Unreal: imports the scale tiles and builds the skin materials (run in the testing-ground
editor after the imports, like bone_forge_physics.py; C:/Dev/BoneForgeTest/run_bf_materials.py runs it).

  /Game/BoneForge/Textures/T_<tile>_H     the tiles' height maps (textures/tiles), linear, greyscale
  /Game/BoneForge/Materials/M_BoneForgeSkin
      The models have no UVs, so the tiles are projected from three sides onto the body's standing pose (the
      pre-skinned position, so the scales stay put on the skin as it moves). The vertex colour from
      bone_forge_gltf.py picks each region's tile and colour (bone_forge_skin.py: R belly, G pattern, B accent,
      A region 0 body / 0.5 feet and hands / 1 face). The scales are lifted out of the surface by bump-mapping their height
      in screen space, which needs no tangents (there are none without UVs), and darkened in their grooves.
  /Game/BoneForge/Materials/M_BoneForgeMembrane   two-sided flat colour for the Pteranodon's flight membrane
  /Game/BoneForge/Materials/M_BoneForgeEye        glossy eyes: iris and pupil from the vertex paint
  /Game/BoneForge/<Sp>/MI_<Sp>_Skin, MI_<Sp>_Eye   per species: colour scheme, tiles, scale size, iris colour; set on
      the skin slot (and the blink's eyelid sets) and the eye slot.
"""
import json
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bone_forge_skin as skin  # noqa: E402

TILES = os.path.normpath(os.path.join(HERE, "..", "textures", "tiles"))
TEX = "/Game/BoneForge/Textures"
MATS = "/Game/BoneForge/Materials"
RIGS = os.path.normpath(os.path.join(HERE, "..", "rig_joints"))


def folder_of(key):
    """The species' content folder under /Game/BoneForge (bone_forge_unreal.py capitalises the key)."""
    return key[:1].upper() + key[1:]


def splice_schemes():
    """{splice key: its base species} for every spliced rig (splice/splice.py) without its own skin entry."""
    out = {}
    for f in sorted(os.listdir(RIGS)):
        if f.endswith("_rig.json") and f[:-9] not in skin.SKIN:
            with open(os.path.join(RIGS, f), encoding="utf-8") as fh:
                base = json.load(fh).get("splice", {}).get("base")
            if base in skin.SKIN:
                out[f[:-9]] = base
    return out
at = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
problems = []


def log(msg):
    unreal.log(f"[BoneForge] materials {msg}")


def import_tiles():
    tasks = []
    for t in skin.TILES:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(TILES, f"{t}_height.png")
        task.destination_path = TEX
        task.destination_name = f"T_{t}_H"
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    at.import_asset_tasks(tasks)
    out = {}
    for t in skin.TILES:
        path = f"{TEX}/T_{t}_H"
        tex = eal.load_asset(path)
        if tex is None:
            problems.append(f"tile {t} did not import")
            continue
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        eal.save_asset(path)
        out[t] = tex
    return out


HEIGHT_HLSL = r"""
float3 n = abs(normalize(N));
n = pow(n, 4.0);
n /= (n.x + n.y + n.z);
float2 uyz = P.yz * S, uxz = P.xz * S, uxy = P.xy * S;
float hb = dot(float3(Texture2DSample(TBack, TBackSampler, uyz).r, Texture2DSample(TBack, TBackSampler, uxz).r, Texture2DSample(TBack, TBackSampler, uxy).r), n);
float hl = dot(float3(Texture2DSample(TBelly, TBellySampler, uyz).r, Texture2DSample(TBelly, TBellySampler, uxz).r, Texture2DSample(TBelly, TBellySampler, uxy).r), n);
float hf = dot(float3(Texture2DSample(TFace, TFaceSampler, uyz).r, Texture2DSample(TFace, TFaceSampler, uxz).r, Texture2DSample(TFace, TFaceSampler, uxy).r), n);
float ht = dot(float3(Texture2DSample(TFeet, TFeetSampler, uyz).r, Texture2DSample(TFeet, TFeetSampler, uxz).r, Texture2DSample(TFeet, TFeetSampler, uxy).r), n);
float h = lerp(hb, hl, VC.r);
h = lerp(h, ht, saturate(VA * 2.0));          // feet and hands (0.5), faded in from the body at the ankles
h = lerp(h, hf, saturate(VA * 2.0 - 1.0));    // the face (1)
return h;
"""

# Bump mapping from the height in screen space (Mikkelsen 2010): needs only the surface normal, no tangents.
BUMP_HLSL = r"""
float3 Pw = GetTranslatedWorldPosition(Parameters);
float3 Nw = normalize(Parameters.TangentToWorld[2]);
float hw = H * Depth;
float3 dpdx = ddx(Pw);
float3 dpdy = ddy(Pw);
float dhdx = ddx(hw);
float dhdy = ddy(hw);
float3 r1 = cross(dpdy, Nw);
float3 r2 = cross(Nw, dpdx);
float det = dot(dpdx, r1);
float3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
float3 bumped = normalize(abs(det) * Nw - grad);
return (abs(det) > 1e-12) ? bumped : Nw;
"""

COLOUR_HLSL = r"""
float3 c = lerp(Back, Belly, VC.r);
c = lerp(c, Pattern, VC.g);
c = lerp(c, Accent, VC.b);
c = c * lerp(1.0 - Groove, 1.0, H) * (1.0 - 0.8 * Fold);
c *= lerp(1.0, 0.6, Wet);          // wet skin is darker
return lerp(c, BloodCol * lerp(0.55, 1.0, H), Blood);
"""


# Blood (the testing ground's ABFDino sets Blood0..3, each frame it changes): up to four spots on the skin, xyz in the
# mesh's own standing pose (cm), w the spot's radius (0: none). Spot 0 is the muzzle after feeding at a kill, the rest
# are wounds. The edge is broken up by the scales' height, so it soaks into the grooves first.
BLOOD_HLSL = r"""
float m = 0.0;
float4 K[4] = { K0, K1, K2, K3 };
for (int i = 0; i < 4; ++i)
{
    if (K[i].w > 0.5)
    {
        float d = distance(P, K[i].xyz) / K[i].w;
        m = max(m, saturate((1.0 - d) * 2.5 + (0.5 - H) * 0.9));
    }
}
return m;
"""


ROUGH_HLSL = r"""
return lerp(lerp(R, 0.28, Wet), 0.18, B * W);
"""


JIGGLE_HLSL = r"""
float L = max(X1 - X0, 1.0);
float F = 0.25 * L;
float W = saturate((P.x - X0) / F) * saturate((X1 - P.x) / F);
return normalize(D) * (J * VC * W);
"""


# Skin sliding over the hips and shoulders. The geometry turns with a limb only over the narrow band its skin weights
# blend in, but a real skin slides as one sheet: near the joint the limb's skin lags it and the body's is dragged along.
# So the scale tiles are read from where that sheet would have taken them: turned about the joint (the mesh's own Y,
# the limbs' swing axis) by the limb's swing (ABFDino sets SlideA = joint and swing in radians, SlideB = the limb's
# rest direction and length, per walking limb, each frame) times (s - g): s the limb's own skin, g the sheet's slide,
# a wide band either side of the joint. Zero far up the body and far down the limb.
SLIDE_HLSL = r"""
float4 A[6] = { A0, A1, A2, A3, A4, A5 };
float4 B[6] = { B0, B1, B2, B3, B4, B5 };
float3 Q = P;
for (int i = 0; i < 6; i++)
{
    float R = B[i].w;
    if (R <= 0.0) continue;
    float3 d = P - A[i].xyz;
    float t = dot(d, B[i].xyz) / R;
    float r = length(d - B[i].xyz * dot(d, B[i].xyz)) / R;
    float phi = A[i].w * (smoothstep(-0.12, 0.12, t) - smoothstep(-0.8, 0.8, t)) * saturate(1.5 - 1.5 * r);
    float c = cos(phi), s = sin(phi);
    Q += float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z) - d;
}
return Q;
"""

# Skin folding on the inside of a bend (lab plan item 8, 2026-11-12). A knee or elbow bent past its rest closes on one
# side, where real skin bunches into folds instead of stretching smooth. ABFDino sets, for each walking limb's middle
# joint each frame: FoldA = the joint (mesh space, rest) and how far it is bent past its rest (0..1, 1 at 60 degrees),
# FoldB = the side the bend closes on (rest space) and the fold zone's half-length, FoldC = the lower bone's rest
# direction. Near the joint, on that side, ridges run across the limb a third of the zone apart: Mode 0 gives the
# valleys' shade (the colour darkens there), Mode 1 how far (cm) the skin there stands out along its normal.
FOLD_HLSL = r"""
float4 A[6] = { A0, A1, A2, A3, A4, A5 };
float4 B[6] = { B0, B1, B2, B3, B4, B5 };
float4 C[6] = { C0, C1, C2, C3, C4, C5 };
float shade = 0.0, out_cm = 0.0;
for (int i = 0; i < 6; i++)
{
    float R = B[i].w;
    if (R <= 0.0 || A[i].w <= 0.0) continue;
    float3 d = P - A[i].xyz;
    float s = dot(d, C[i].xyz);
    float3 rad = d - C[i].xyz * s;
    float rl = length(rad);
    float side = dot(rad / max(rl, 0.001), B[i].xyz);
    float zone = saturate(1.0 - abs(s) / R) * saturate(2.0 - rl / R);
    float c = saturate(1.6 * A[i].w) * zone * smoothstep(-0.3, 0.5, side);
    float ridge = 0.5 + 0.5 * cos(6.2831853 * s / (0.25 * R));
    shade = max(shade, c * (1.0 - ridge));
    out_cm += c * ridge * 0.15 * R;
}
return Mode < 0.5 ? shade : out_cm;
"""

# The raptor's folded wing feathers (bone_forge_gltf.feather_uv: U root 0 to tip 1, V along the wing; 0 elsewhere):
# each bends by its share along it squared, by the wing's sway (SwayL/R: where the tips lag to, world cm, a spring
# ABFDino drives from the hand's movement) and flutters across its own plane (NL/NR, world) at its own phase.
FEATHER_HLSL = r"""
float u = UV.x;
if (u <= 0.0) return float3(0.0, 0.0, 0.0);
float w = u * u;
bool left = P.y < 0.0;
float3 sway = left ? SwayL : SwayR;
float3 n = left ? NL : NR;
float wave = sin(6.2831853 * (FU - 0.6 * u + 3.7 * UV.y)) + 0.5 * sin(6.2831853 * (1.7 * FU + 5.3 * UV.y));
return w * (sway + n * (Lift + Flutter * wave / 1.5));
"""

# Nostrils flare (the design doc's Movement tab: slightly on each breath in, more when sniffing). They're painted all
# pupil and no iris (bone_forge_skin.paint), so G (1 - R) picks them out of the eye material; their rims grow along the
# head's side (HeadRight, world, from ABFDino each frame) by Flare cm, not out of it.
NOSTRIL_HLSL = r"""
float m = VC.g * (1.0 - VC.r);
float3 n = normalize(N);
float3 h = normalize(HR);
return (n - h * dot(n, h)) * (Flare * m);
"""


def custom(mat, code, out_type, inputs, x, y, desc):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", out_type)
    node.set_editor_property("description", desc)
    ins = []
    for name in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    node.set_editor_property("inputs", ins)
    return node


def param(mat, cls, name, x, y, value=None):
    node = mel.create_material_expression(mat, cls, x, y)
    node.set_editor_property("parameter_name", name)
    if value is not None:
        node.set_editor_property("default_value" if cls is not unreal.MaterialExpressionTextureObjectParameter else "texture", value)
    return node


def connect(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        problems.append(f"could not connect {a.get_name()}:{a_out} -> {b.get_name()}:{b_in}")


def fresh_material(name):
    path = f"{MATS}/{name}"
    if eal.does_asset_exist(path):
        mat = eal.load_asset(path)
        mel.delete_all_material_expressions(mat)
        return mat
    return at.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())


def build_skin_master(tiles):
    mat = fresh_material("M_BoneForgeSkin")
    mat.set_editor_property("tangent_space_normal", False)       # the bump node returns a world-space normal
    mat.set_editor_property("used_with_skeletal_mesh", True)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -1400, 0)
    pos = mel.create_material_expression(mat, unreal.MaterialExpressionPreSkinnedPosition, -1600, -300)
    nrm = mel.create_material_expression(mat, unreal.MaterialExpressionPreSkinnedNormal, -1600, -150)
    vip = mel.create_material_expression(mat, unreal.MaterialExpressionVertexInterpolator, -1400, -300)
    vin = mel.create_material_expression(mat, unreal.MaterialExpressionVertexInterpolator, -1400, -150)
    names = [f"A{i}" for i in range(6)] + [f"B{i}" for i in range(6)]
    slide = custom(mat, SLIDE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P"] + names, -1500, -600, "Skin slide")
    connect(pos, "", slide, "P")
    for i in range(6):
        connect(param(mat, unreal.MaterialExpressionVectorParameter, f"SlideA{i}", -1900, -900 + 120 * i,
                      unreal.LinearColor(0, 0, 0, 0)), "RGBA", slide, f"A{i}")
        connect(param(mat, unreal.MaterialExpressionVectorParameter, f"SlideB{i}", -2100, -900 + 120 * i,
                      unreal.LinearColor(0, 0, 0, 0)), "RGBA", slide, f"B{i}")
    connect(slide, "", vip, "VS")
    connect(nrm, "", vin, "VS")
    tile_cm = param(mat, unreal.MaterialExpressionScalarParameter, "TileCm", -1600, 150, 40.0)
    inv = mel.create_material_expression(mat, unreal.MaterialExpressionDivide, -1400, 150)
    inv.set_editor_property("const_a", 1.0)
    connect(tile_cm, "", inv, "B")
    default = tiles.get("trex_pebble")
    tex = {k: param(mat, unreal.MaterialExpressionTextureObjectParameter, f"{k}Tile", -1600, 300 + 150 * i, default)
           for i, k in enumerate(("Back", "Belly", "Face", "Feet"))}
    height = custom(mat, HEIGHT_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                    ["P", "N", "S", "VC", "VA", "TBack", "TBelly", "TFace", "TFeet"], -1100, 0, "Skin height")
    connect(vip, "", height, "P")
    connect(vin, "", height, "N")
    connect(inv, "", height, "S")
    connect(vc, "", height, "VC")         # the default output is RGB only; the region is in alpha
    connect(vc, "A", height, "VA")
    for k, node in tex.items():
        connect(node, "", height, f"T{k}")
    depth = param(mat, unreal.MaterialExpressionScalarParameter, "BumpCm", -1100, 250, 1.0)
    bump = custom(mat, BUMP_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["H", "Depth"], -800, 150, "Bump")
    connect(height, "", bump, "H")
    connect(depth, "", bump, "Depth")
    cols = {k: param(mat, unreal.MaterialExpressionVectorParameter, f"{k}Colour", -1100, -600 + 120 * i)
            for i, k in enumerate(("Back", "Belly", "Pattern", "Accent"))}
    groove = param(mat, unreal.MaterialExpressionScalarParameter, "Groove", -1100, -120, 0.35)
    # Skin folds (FOLD_HLSL): the same function twice, the valleys' shade (to the pixel, through an interpolator) and the
    # ridges' height (the vertex offset, along its skinned normal).
    fold_names = [f"{g}{i}" for g in "ABC" for i in range(6)]
    fold_shade = custom(mat, FOLD_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["P", "Mode"] + fold_names, -1500, -1400, "Fold shade")
    fold_out = custom(mat, FOLD_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["P", "Mode"] + fold_names, -1500, -1800, "Fold ridges")
    for node, mode in ((fold_shade, 0.0), (fold_out, 1.0)):
        k = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -1700, -1400 if mode == 0.0 else -1800)
        k.set_editor_property("r", mode)
        connect(pos, "", node, "P")
        connect(k, "", node, "Mode")
    for g, label in (("A", "FoldA"), ("B", "FoldB"), ("C", "FoldC")):
        for i in range(6):
            p = param(mat, unreal.MaterialExpressionVectorParameter, f"{label}{i}", -2300, -2400 + 120 * ("ABC".index(g) * 6 + i),
                      unreal.LinearColor(0, 0, 0, 0))
            connect(p, "RGBA", fold_shade, f"{g}{i}")
            connect(p, "RGBA", fold_out, f"{g}{i}")
    vif = mel.create_material_expression(mat, unreal.MaterialExpressionVertexInterpolator, -1200, -1400)
    connect(fold_shade, "", vif, "VS")
    blood = custom(mat, BLOOD_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["P", "H", "K0", "K1", "K2", "K3"], -1100, -900, "Blood")
    connect(vip, "", blood, "P")
    connect(height, "", blood, "H")
    for i in range(4):
        connect(param(mat, unreal.MaterialExpressionVectorParameter, f"Blood{i}", -1400, -1000 + 110 * i,
                      unreal.LinearColor(0, 0, 0, 0)), "RGBA", blood, f"K{i}")
    blood_col = param(mat, unreal.MaterialExpressionVectorParameter, "BloodColour", -1100, -1100, unreal.LinearColor(0.11, 0.006, 0.005, 1))
    blood_wet = param(mat, unreal.MaterialExpressionScalarParameter, "BloodWet", -1100, -1200, 1.0)
    colour = custom(mat, COLOUR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                    ["VC", "Back", "Belly", "Pattern", "Accent", "H", "Groove", "Fold", "Blood", "BloodCol", "Wet"], -800, -300, "Skin colour")
    wet = param(mat, unreal.MaterialExpressionScalarParameter, "Wet", -1100, -1300, 0.0)
    connect(wet, "", colour, "Wet")
    connect(blood, "", colour, "Blood")
    connect(blood_col, "", colour, "BloodCol")
    connect(vc, "", colour, "VC")
    for k, node in cols.items():
        connect(node, "", colour, k)
    connect(height, "", colour, "H")
    connect(groove, "", colour, "Groove")
    connect(vif, "", colour, "Fold")
    rough_p = param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", -1000, 0, 0.72)
    rough = custom(mat, ROUGH_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["R", "B", "W", "Wet"], -800, 0, "Wet blood")
    connect(wet, "", rough, "Wet")
    connect(rough_p, "", rough, "R")
    connect(blood, "", rough, "B")
    connect(blood_wet, "", rough, "W")
    # The belly's jiggle (the testing ground's ABFDino sets Jiggle, cm, each frame): the belly (vertex colour R) between
    # the hips and the shoulders (BellyX0..X1 along the mesh's X, faded in and out over a quarter of that) moves down
    # by it, in the mesh's own down.
    jig = param(mat, unreal.MaterialExpressionScalarParameter, "Jiggle", -1100, 500, 0.0)
    x0 = param(mat, unreal.MaterialExpressionScalarParameter, "BellyX0", -1100, 620, 0.0)
    x1 = param(mat, unreal.MaterialExpressionScalarParameter, "BellyX1", -1100, 740, 0.0)
    down = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -1100, 860)
    down.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, -1.0, 0.0))
    to_world = mel.create_material_expression(mat, unreal.MaterialExpressionTransform, -900, 860)
    to_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    to_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(down, "", to_world, "")
    wpo = custom(mat, JIGGLE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "VC", "J", "X0", "X1", "D"], -700, 600, "Belly jiggle")
    connect(pos, "", wpo, "P")
    connect(vc, "R", wpo, "VC")
    connect(jig, "", wpo, "J")
    connect(x0, "", wpo, "X0")
    connect(x1, "", wpo, "X1")
    connect(to_world, "", wpo, "D")
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 1000)
    feath = custom(mat, FEATHER_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   ["UV", "P", "SwayL", "SwayR", "NL", "NR", "Lift", "Flutter", "FU"], -700, 1000, "Feathers")
    connect(uv, "", feath, "UV")
    connect(pos, "", feath, "P")
    for i, (name, kind) in enumerate((("SwayL", "v"), ("SwayR", "v"), ("NL", "v"), ("NR", "v"),
                                      ("Lift", "s"), ("Flutter", "s"), ("FU", "s"))):
        cls = unreal.MaterialExpressionVectorParameter if kind == "v" else unreal.MaterialExpressionScalarParameter
        pname = {"NL": "FeatherNL", "NR": "FeatherNR", "Lift": "FeatherLift", "Flutter": "FeatherFlutter", "FU": "FeatherU"}.get(name, f"Feather{name}")
        node = param(mat, cls, pname, -1000, 1100 + 110 * i, unreal.LinearColor(0, 0, 0, 0) if kind == "v" else 0.0)
        connect(node, "", feath, name)
    both = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -450, 800)
    connect(wpo, "", both, "A")
    connect(feath, "", both, "B")
    # The folds' ridges stand out along the skinned normal.
    nws = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, -1800)
    ridges = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -700, -1800)
    connect(fold_out, "", ridges, "A")
    connect(nws, "", ridges, "B")
    all3 = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -300, 800)
    connect(both, "", all3, "A")
    connect(ridges, "", all3, "B")
    mel.connect_material_property(all3, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(bump, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    return mat


# The island's ground (BFWorld, the World and AI tab): its biome colours come painted on the ground's points (sRGB
# bytes, so squared back to linear here), given grain by two of the tiles laid flat across the land: pebbles a couple of
# metres across and broad patches tens of metres across, so open ground doesn't read as flat paint.
GROUND_HLSL = r"""
// (2026-11-16, Raven: "terrain polish ... more realistic": noise grain instead of a scale tile.)
// (2026-11-22, Raven: "make the land look good and real". From the ground tour: bare rock read as pale smooth plaster with
// contour stripes (a sine banded it by height), grass as one flat lawn green, sand near white from the air with a hard
// blob edge to the grass. Now: 3D noise (no stretching down steep faces), rock darker and varied in tone (warm brown-grey
// to cool blue-grey) with thin cracks, staining down steep faces, lichen and moss on its upward faces and no regular
// bands; grass in broad olive, lush and dry patches with bare soil showing; sand darker with a wet band at the sea; the
// edges between grass, sand and rock ragged with noise. The bumps themselves are in GROUND_NORMAL_HLSL.)
struct BFG
{
    float H3(float3 p)
    {
        p = frac(p * 0.3183099 + 0.1) * 17.0;
        return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
    }
    float N3(float3 x)
    {
        float3 i = floor(x), f = frac(x);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(H3(i), H3(i + float3(1, 0, 0)), f.x), lerp(H3(i + float3(0, 1, 0)), H3(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(H3(i + float3(0, 0, 1)), H3(i + float3(1, 0, 1)), f.x), lerp(H3(i + float3(0, 1, 1)), H3(i + float3(1, 1, 1)), f.x), f.y), f.z) * 2.0 - 1.0;
    }
};
BFG G;
float3 c = pow(saturate(VC), 2.2);
float3 p = P / 100.0;                          // metres
float3 nn = normalize(N);
float up = saturate(nn.z);
float n0 = G.N3(p * 3.3), n1 = G.N3(p * 0.9 + 17.0), n2 = G.N3(p * 0.3 + 31.0), n3 = G.N3(p * 0.09 + 5.0), n4 = G.N3(p * 0.035 + 11.0);
// (2026-11-22: broad patches 100-330 m across too: at 11-28 m alone the plains from the air were an even leopard print of
// pale spots)
float n5 = G.N3(p * 0.008 + 23.0), n6 = G.N3(p * 0.003 + 37.0);
float broad = n6 * 0.45 + n5 * 0.35 + n4 * 0.12 + n3 * 0.08;
// which ground: from the painted colour, the edges ragged with noise
float edge = n1 * 0.35 + n0 * 0.2;
float green = saturate((c.g - max(c.r, c.b)) * 25.0 + edge * 0.6);          // (2026-11-25: at 12 a plain was half "other ground", its paint shown bright as it was)
float sand = saturate((c.r - c.b) * 6.0 + edge * 0.5) * (1.0 - green);
float rock = saturate(max((0.84 - up) / 0.2 + edge * 0.6, saturate(1.0 - abs(c.r - c.b) * 8.0) * (1.0 - green) * 0.8));
float mott = 1.0 + 0.05 * n0 + 0.06 * n1 + 0.06 * n2 + 0.05 * n3;
float3 o3 = c * mott * (0.9 + 0.2 * (0.5 + 0.5 * broad));
// grass: clumps, broad dry olive and lush dark patches, a little soil showing
float clump = n1 * 0.6 + n0 * 0.4;
float3 g = c * mott * (1.0 + 0.14 * clump);
g = lerp(g, g * float3(1.3, 1.1, 0.64), 0.45 * smoothstep(0.0, 0.55, broad));
g = lerp(g, g * float3(0.74, 0.87, 0.8), 0.4 * smoothstep(0.0, 0.55, -broad));
g = lerp(g, g * float3(1.15, 1.05, 0.75), 0.3 * smoothstep(0.2, 0.8, n2));
float gl = dot(g, float3(0.3, 0.59, 0.11));
g = lerp(float3(gl, gl, gl) * float3(1.04, 1.0, 0.92), g, 0.74);          // (0.82: from the ground a meadow read as a lawn)
// (2026-11-25: still a lawn from the ground: darker, and patches of dark fern and dry olive a few metres to 30 across)
float mid = n2 * 0.6 + n3 * 0.4;
g = lerp(g, g * float3(0.72, 0.84, 0.7), 0.35 * smoothstep(0.05, 0.5, mid));
g = lerp(g, g * float3(1.22, 1.04, 0.62), 0.35 * smoothstep(0.05, 0.5, -mid));
g *= 0.88 * float3(1.04, 0.9, 0.8);          // (an olive cast: the fern floor, not a lawn)
float soil = smoothstep(0.38, 0.85, n0 * 0.55 + n1 * 0.45) * 0.55;
// fallen fronds and litter, a browner cast in patches a few metres across
g = lerp(g, g * float3(1.18, 1.02, 0.72), 0.3 * smoothstep(0.15, 0.7, n2 * 0.6 + n1 * 0.4));
g = lerp(g, float3(0.075, 0.058, 0.038), soil);
// sand: darker than paint (it read near white from the air), faint ripples, wet and dark just above the sea
float rip = sin((p.x * 0.8 + p.y * 0.6) * 9.0 + n2 * 3.0);
float3 s3 = c * 0.72 * (1.0 + 0.035 * rip + 0.05 * n1 + 0.06 * n3);
float wet = (1.0 - smoothstep(0.25, 1.1, p.z + 0.25 * n1)) * step(-3.0, p.z);
s3 = lerp(s3, s3 * float3(0.55, 0.55, 0.6), wet);
// rock: darker, varied in tone, cracked, stained down steep faces, lichen and moss on what faces up
// (steep faces are rock whatever was painted there: a sea bluff took the beach's tan, a steep meadow its green)
float3 rk = lerp(c, float3(0.165, 0.16, 0.152), 0.7 * saturate((0.84 - up) / 0.2)) * 0.66;
rk *= lerp(float3(1.1, 1.0, 0.86), float3(0.9, 0.97, 1.06), 0.5 + 0.5 * n3);
rk *= 0.72 + 0.4 * (0.5 + 0.5 * n2) + 0.1 * n1;
float cr = smoothstep(0.97, 0.997, 1.0 - abs(G.N3(p * float3(0.55, 0.55, 1.4) + 3.0)));
float cr2 = smoothstep(0.975, 0.998, 1.0 - abs(G.N3(p * 2.1 + 41.0)));
// (2026-11-25: at one width everywhere they read as contour lines drawn on a map: now only in stretches, fading in and out)
float crm = smoothstep(0.05, 0.45, G.N3(p * 0.22 + 61.0)) * (0.6 + 0.4 * n0);
rk *= 1.0 - (0.3 * cr + 0.15 * cr2) * crm;
float st = smoothstep(0.45, 0.95, abs(G.N3(float3(p.x * 1.3, p.y * 1.3, p.z * 0.07) + 7.0))) * saturate((0.9 - up) * 2.0);
rk *= 1.0 - 0.22 * st * saturate((0.8 - up) * 3.0);          // (2026-11-25: on open slopes it read as soot: only on the steep faces, and lighter)
float strata = smoothstep(0.6, 0.95, G.N3(float3(p.x * 0.05, p.y * 0.05, p.z * 0.7) + 19.0));
rk *= 1.0 - 0.15 * strata;
float li = smoothstep(0.5, 0.85, n1 * 0.5 + n0 * 0.5) * saturate(up * 1.5 - 0.2);
rk = lerp(rk, rk * float3(1.25, 1.2, 0.85) + float3(0.02, 0.02, 0.0), 0.45 * li);
float moss = smoothstep(0.6, 0.85, up) * smoothstep(-0.05, 0.45, n3 * 0.6 + n2 * 0.4);
// (2026-11-25: dark near-black moss on the open tops read as soot from above: olive, thinner, and less of it high up)
moss *= 1.0 - 0.7 * smoothstep(60.0, 130.0, p.z);
rk = lerp(rk, float3(0.07, 0.085, 0.04) * (1.0 + 0.3 * n0), 0.42 * moss);
o3 = lerp(o3, g, green);
o3 = lerp(o3, s3, sand);
o3 = lerp(o3, rk, rock);
return o3;
"""

# The ground's small relief (2026-11-22): a world-space normal bent by 3D noise, bumps a fifth of a metre to two across on
# bare rock (knobs, ledges, cracks catching the low sun), much gentler on grass and sand, so the 4 m grid of points
# doesn't read as smooth cloth.
GROUND_NORMAL_HLSL = r"""
struct BFG
{
    float H3(float3 p)
    {
        p = frac(p * 0.3183099 + 0.1) * 17.0;
        return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
    }
    float N3(float3 x)
    {
        float3 i = floor(x), f = frac(x);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(H3(i), H3(i + float3(1, 0, 0)), f.x), lerp(H3(i + float3(0, 1, 0)), H3(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(H3(i + float3(0, 0, 1)), H3(i + float3(1, 0, 1)), f.x), lerp(H3(i + float3(0, 1, 1)), H3(i + float3(1, 1, 1)), f.x), f.y), f.z) * 2.0 - 1.0;
    }
    float Hgt(float3 q, float r)
    {
        float hr = 0.22 * N3(q * 0.5 + 3.0) + 0.1 * N3(q * 1.5 + 9.0) + 0.035 * N3(q * 4.5 + 21.0) - 0.03 * abs(N3(q * float3(0.55, 0.55, 1.4) + 3.0));
        float hg = 0.05 * N3(q * 0.7 + 5.0) + 0.015 * N3(q * 2.5 + 13.0);
        return lerp(hg, hr, r);
    }
};
BFG G;
float3 c = pow(saturate(VC), 2.2);
float3 p = P / 100.0;
float3 nn = normalize(N);
float up = saturate(nn.z);
float green = saturate((c.g - max(c.r, c.b)) * 12.0);
float r = saturate(max((0.84 - up) / 0.2, saturate(1.0 - abs(c.r - c.b) * 8.0) * (1.0 - green) * 0.8));
float e = 0.04;
float h0 = G.Hgt(p, r);
float3 gr = float3(G.Hgt(p + float3(e, 0, 0), r) - h0, G.Hgt(p + float3(0, e, 0), r) - h0, G.Hgt(p + float3(0, 0, e), r) - h0) / e;
gr -= dot(gr, nn) * nn;
// (fades out far off, where it would only shimmer)
float fade = 1.0 - smoothstep(150.0, 600.0, D / 100.0);
return normalize(nn - gr * 0.35 * fade);
"""


def build_terrain_master(tiles):
    mat = fresh_material("M_BoneForgeTerrain")
    # (two-sided, 2026-11-15: where a cave's rock folds over itself in the big caverns a triangle's back faces you, and
    # one-sided it showed as a hole to the sky)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("tangent_space_normal", False)       # GROUND_NORMAL_HLSL returns a world-space normal
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 0)
    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, -200)
    fine = param(mat, unreal.MaterialExpressionTextureObjectParameter, "FineTile", -900, 200, tiles.get("trex_pebble"))
    broad = param(mat, unreal.MaterialExpressionTextureObjectParameter, "BroadTile", -900, 400, tiles.get("sauropod_mixed"))
    ground = custom(mat, GROUND_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "VC", "TFine", "TBroad", "N"], -500, 0, "Ground")
    nws = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, 600)
    connect(nws, "", ground, "N")
    connect(wp, "", ground, "P")
    connect(vc, "", ground, "VC")
    connect(fine, "", ground, "TFine")
    connect(broad, "", ground, "TBroad")
    dist = mel.create_material_expression(mat, unreal.MaterialExpressionPixelDepth, -900, 800)
    bump = custom(mat, GROUND_NORMAL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "VC", "N", "D"], -500, 500, "Ground relief")
    connect(wp, "", bump, "P")
    connect(vc, "", bump, "VC")
    connect(nws, "", bump, "N")
    connect(dist, "", bump, "D")
    rough = param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", -500, 250, 0.97)
    spec = param(mat, unreal.MaterialExpressionScalarParameter, "Specular", -500, 350, 0.2)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(ground, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(bump, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    log("terrain material built")
    return mat


def build_membrane_master():
    mat = fresh_material("M_BoneForgeMembrane")
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    c = param(mat, unreal.MaterialExpressionVectorParameter, "Color", -400, 0, unreal.LinearColor(0.42, 0.3, 0.22, 1))
    r = param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", -400, 200, 0.6)
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    return mat


def build_water():
    """The testing ground's pool: see-through blue-green water (BFGameMode loads it by path)."""
    mat = fresh_material("M_BoneForgeWater")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    c = param(mat, unreal.MaterialExpressionVectorParameter, "Color", -400, 0, unreal.LinearColor(0.03, 0.16, 0.2, 1))
    o = param(mat, unreal.MaterialExpressionScalarParameter, "Opacity", -400, 150, 0.55)
    r = param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", -400, 300, 0.05)
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(o, "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    return mat


# Marks and puffs (the testing ground's ABFEffects): footprints pressed in the ground, and soft puffs of dust and spray.
# Both are drawn as instanced engine shapes, each instance's colour (and the puff's opacity) in its custom data:
# 0..2 the colour, 3 the roughness (marks) or opacity (puffs).
PUFF_HLSL = r"""
float facing = saturate(dot(normalize(N), normalize(V)));
return A * facing * facing;
"""


# A waterfall's curtain (ABFWorld::BuildFalls, 2026-11-15): white water streaming down the strip (UV: x across, y down,
# 2 m a unit), in ropes of different speeds, thinning to see-through between them; lit a little from within so it reads
# in the shade of a ravine.
FALL_HLSL = r"""
// (2026-11-15: bands across it read as a blind. Water falls in ropes.)
// (2026-11-25, Raven: "as natural as it can be": the ropes were a regular sine, 11 even stripes like corrugated plastic,
// with straight edges and see-through nowhere. Now noise ropes 0.3-1.5 m across falling at 4-9 m/s, glassy and whole at
// the lip, breaking into white ropes with gaps lower down, thick white at the foot; its edges ragged and shifting.
// VC.r is its width in 10 cm, VC.g how far down it is.)
struct FN
{
    float H(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
    float N(float2 p)
    {
        float2 i = floor(p), f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(H(i), H(i + float2(1, 0)), f.x), lerp(H(i + float2(0, 1)), H(i + float2(1, 1)), f.x), f.y);
    }
};
FN F;
float wm = max(VC.r * 25.5, 2.0);
float down = VC.g;
float xm = UV.x * wm, ym = UV.y * 2.0;
float r1 = F.N(float2(xm * 1.4, ym * 0.12 - T * 1.1));
float r2 = F.N(float2(xm * 3.3 + 11.0, ym * 0.3 - T * 2.3));
float r3 = F.N(float2(xm * 8.0 + 5.0, ym * 0.9 - T * 3.6));
float foam = r1 * 0.5 + r2 * 0.32 + r3 * 0.18;
float glassy = 0.3 + 0.3 * r1 + 0.12 * r3;          // (at 0.5 and white, the level run over the lip read from above as a white card)
float ropes = smoothstep(0.28, 0.7, foam);
float a = lerp(glassy, ropes, smoothstep(0.04, 0.4, down));
a = lerp(a, saturate(0.55 + 0.6 * foam), smoothstep(0.75, 1.0, down));
float e = min(UV.x, 1.0 - UV.x) * wm;
float rag = 0.25 + 0.9 * F.N(float2(ym * 0.45 - T * 1.7, UV.x > 0.5 ? 3.0 : 17.0));
a *= smoothstep(0.0, rag, e);
return saturate(a);
"""


def build_fall():
    m = fresh_material("M_BoneForgeFall")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    t = mel.create_material_expression(m, unreal.MaterialExpressionTime, -900, 120)
    w = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -900, 240)
    w.set_editor_property("r", 1.0)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 360)
    op = custom(m, FALL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "T", "VC"], -500, 100, "Streaming")
    connect(uv, "", op, "UV")
    connect(t, "", op, "T")
    connect(vc, "", op, "VC")
    col = custom(m, "return lerp(float3(0.2, 0.3, 0.3), float3(0.9, 0.93, 0.94), smoothstep(0.35, 0.9, A));", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["A"], -250, -100, "White water")
    connect(op, "", col, "A")
    em = custom(m, "return C * 0.25;", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C"], -100, 200, "Glow")
    connect(col, "", em, "C")
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    eal.save_asset(m.get_path_name())
    log("M_BoneForgeFall built")


def build_fx():
    # (ABFEffects loads these two in its constructor, so its class default holds them: once they exist, remaking them
    # in the editor trips an assertion (a rooted object). Remade only when asked: BF_FX_REBUILD=1, or missing.)
    if not os.environ.get("BF_FX_REBUILD") and all(eal.does_asset_exist(f"{MATS}/{n}") for n in ("M_BoneForgeMark", "M_BoneForgePuff")):
        log("marks and puffs: kept as they are")
        return
    def cd(mat, i, x, y):
        n = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, x, y)
        n.set_editor_property("data_index", i)
        return n

    mark = fresh_material("M_BoneForgeMark")
    mark.set_editor_property("used_with_instanced_static_meshes", True)
    rgb = custom(mark, "return float3(R, G, B);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["R", "G", "B"], -300, 0, "Colour")
    for i, k in enumerate("RGB"):
        connect(cd(mark, i, -600, -100 + 120 * i), "", rgb, k)
    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(cd(mark, 3, -600, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mark)
    eal.save_asset(mark.get_path_name())

    puff = fresh_material("M_BoneForgePuff")
    puff.set_editor_property("used_with_instanced_static_meshes", True)
    puff.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    puff.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    rgb2 = custom(puff, "return float3(R, G, B);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["R", "G", "B"], -300, 0, "Colour")
    for i, k in enumerate("RGB"):
        connect(cd(puff, i, -600, -100 + 120 * i), "", rgb2, k)
    op = custom(puff, PUFF_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["N", "V", "A"], -300, 300, "Soft edge")
    connect(mel.create_material_expression(puff, unreal.MaterialExpressionVertexNormalWS, -600, 300), "", op, "N")
    connect(mel.create_material_expression(puff, unreal.MaterialExpressionCameraVectorWS, -600, 420), "", op, "V")
    connect(cd(puff, 3, -600, 540), "", op, "A")
    mel.connect_material_property(rgb2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(puff)
    eal.save_asset(puff.get_path_name())


# The island's water (World and AI tab): single-layer water, so it refracts what's under it, takes the colour of its
# depth (the absorption and scattering coefficients are per centimetre), reflects the sky and shows foam. ABFWorld
# draws a map of it (Info: R the sea's depth /40 m, G a sea point's distance from the shore /127.5 m, BA the fresh
# water's flow, -3..3 m/s) over the island's grid (Cell cm a point, Count points a side, Origin its corner).
# The sea: six long Gerstner waves round the wind (from the south-west, toward 35 deg) lift the surface (WPO), calm
# where it's shallow and gone far from the camera; rollers run in over the last 45 m to the beach. Every water: twelve
# short ripples round the wind make the surface's normal, carried down the rivers by the current (two staggered copies
# faded in turn, so they don't stretch). Foam where the water thins against the ground, on the rollers and in rapids.
# ABFWorld::SeaWaveM works the same waves out in C++ (things float on them): keep the two in step.
SEA_COMMON = r"""
struct BFW
{
    float Hash(float2 p)
    {
        float3 p3 = frac(float3(p.xyx) * 0.1031);
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.x + p3.y) * p3.z);
    }
    float Noise(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), f.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
    }
    float3 Swell(float2 p, float t, float amp, out float3 n)
    {
        float dirs[6] = { 35.0, 10.0, 58.0, 25.0, 80.0, -5.0 };
        float lens[6] = { 48.0, 33.0, 23.0, 15.5, 9.7, 6.3 };
        float3 o = float3(0, 0, 0);
        float3 s = float3(0, 0, 0);
        for (int i = 0; i < 6; i++)
        {
            float a = radians(dirs[i]);
            float2 d = float2(cos(a), sin(a));
            float k = 6.2831853 / lens[i];
            float w = sqrt(9.81 * k);
            float A = amp * lens[i] / 115.0;
            float ph = k * dot(d, p) - w * t + i * 1.7;
            float c = cos(ph);
            float sn = sin(ph);
            o.xy += 1.8 * A * d * c;
            o.z += A * sn;
            s.xy += d * k * A * c;
            s.z += 1.8 * k * A * sn;
        }
        n = float3(-s.x, -s.y, 1.0 - s.z);
        return o;
    }
    float SeaAmp(float depthM, float distM)
    {
        return smoothstep(0.5, 9.0, depthM) * (1.0 - smoothstep(700.0, 1500.0, distM));
    }
    float2 Ripples(float2 p, float t, float dist, float sp, float gust)
    {
        float2 g = float2(0, 0);
        p += float2(Noise(p * 0.21), Noise(p * 0.21 + 5.2)) * 2.4 + float2(Noise(p * 0.9 + 1.7), Noise(p * 0.9 + 9.1)) * 0.5;
        float L = 3.6;
        for (int i = 0; i < 12; i++)
        {
            float a = radians(35.0) + (frac(i * 0.618034) - 0.5) * 2.6;
            float2 d = float2(cos(a), sin(a));
            float k = 6.2831853 / L;
            float w = sqrt(9.81 * k + 0.000074 * k * k * k);
            float fade = saturate(1.5 - dist / (L * 45.0));
            float A = L * 0.010 * (gust + sp * 0.8) * fade;
            float ph = k * dot(d, p) - w * t + i * 2.3;
            g += d * k * A * cos(ph);
            L *= 0.79;
        }
        return g;
    }
};
BFW F;
float2 uv = (P.xy / Cell + 0.5) / Count;
float4 inf = Texture2DSampleLevel(Info, InfoSampler, uv, 0);
float depth = inf.r * 40.0;
float shore = inf.g * 127.5;
float2 flow = (inf.ba * 2.0 - 1.0) * 3.0;
float2 pm = P.xy / 100.0;
float dist = length(D) / 100.0;
"""

SEA_WPO_HLSL = SEA_COMMON + r"""
float3 n;
float3 o = F.Swell(pm, T, Waves * F.SeaAmp(depth, dist), n);
float band = frac(shore / 13.0 + T * 0.16);
o.z += Waves * pow(saturate(sin(band * 6.2831853)), 3.0) * 0.22 * smoothstep(1.0, 6.0, shore) * (1.0 - smoothstep(20.0, 45.0, shore)) * step(0.02, inf.r);
return o * 100.0;
"""

SEA_NORMAL_HLSL = SEA_COMMON + r"""
float3 nb;
F.Swell(pm, T, Waves * F.SeaAmp(depth, dist), nb);
float2 grad = -nb.xy / max(nb.z, 0.2);
float sp = length(flow);
float gust = (0.55 + 0.9 * F.Noise(pm * 0.04 + float2(T * 0.03, T * 0.02))) * lerp(0.5, 1.0, Waves);
float cyc = 1.6;
float ph0 = frac(T / cyc);
float ph1 = frac(T / cyc + 0.5);
float2 g0 = F.Ripples(pm - flow * ph0 * cyc, T, dist, sp, gust);
float2 g1 = F.Ripples(pm - flow * ph1 * cyc, T, dist, sp, gust);
grad += lerp(g0, g1, abs(1.0 - 2.0 * ph0));
return normalize(float3(-grad, 1.0));
"""

SEA_FOAM_HLSL = SEA_COMMON + r"""
float sp = length(flow);
float cyc = 1.6;
float ph0 = frac(T / cyc);
float ph1 = frac(T / cyc + 0.5);
float2 q0 = pm - flow * ph0 * cyc;
float2 q1 = pm - flow * ph1 * cyc;
float n0 = 0.6 * F.Noise(q0 * 1.3) + 0.4 * F.Noise(q0 * 4.3 + 7.0);
float n1 = 0.6 * F.Noise(q1 * 1.3 + 3.1) + 0.4 * F.Noise(q1 * 4.3 + 11.0);
float pat = lerp(n0, n1, abs(1.0 - 2.0 * ph0));
float edge = (1.0 - saturate(Thick / 45.0)) * (0.45 + 0.55 * Waves);
float band = frac(shore / 13.0 + T * 0.16);
float crest = smoothstep(0.14, 0.24, band) * (1.0 - smoothstep(0.26, 0.4, band));
float surf = Waves * crest * smoothstep(0.5, 4.0, shore) * (1.0 - smoothstep(8.0, 34.0, shore)) * step(0.02, inf.r);
float rapid = smoothstep(1.3, 2.6, sp);
float f = max(max(edge * 1.25, surf), rapid * 0.85);
return saturate((f - (1.0 - pat) * 1.05) * 2.2) * (1.0 - smoothstep(600.0, 2000.0, dist));
"""


def connect_any(a, a_out, b, names):
    for n in names:
        if mel.connect_material_expressions(a, a_out, b, n):
            return
    problems.append(f"could not connect {a.get_name()} -> {b.get_name()} as any of {names}")


def build_sea():
    """The island's sea, lakes and rivers (ABFWorld makes two instances: the sea with waves, fresh water without)."""
    # Made afresh each time: clearing a material's nodes leaves its single-layer-water output behind (two won't compile).
    if eal.does_asset_exist(f"{MATS}/M_BoneForgeSea"):
        eal.delete_asset(f"{MATS}/M_BoneForgeSea")
    mat = fresh_material("M_BoneForgeSea")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("tangent_space_normal", False)
    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1800, 0)
    wp.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    origin = param(mat, unreal.MaterialExpressionVectorParameter, "Origin", -1800, 150, unreal.LinearColor(0, 0, 0, 0))
    om = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1600, 150)
    om.set_editor_property("r", True)
    om.set_editor_property("g", True)
    om.set_editor_property("b", True)
    connect(origin, "", om, "")
    p = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, -1400, 0)
    connect(wp, "", p, "A")
    connect(om, "", p, "B")
    cam = mel.create_material_expression(mat, unreal.MaterialExpressionCameraPositionWS, -1800, 300)
    d = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, -1400, 200)
    connect(wp, "", d, "A")
    connect(cam, "", d, "B")
    t = mel.create_material_expression(mat, unreal.MaterialExpressionTime, -1400, 350)
    info = param(mat, unreal.MaterialExpressionTextureObjectParameter, "Info", -1800, 450,
                 unreal.load_asset("/Engine/EngineResources/Black.Black"))
    cell = param(mat, unreal.MaterialExpressionScalarParameter, "Cell", -1400, 500, 400.0)
    count = param(mat, unreal.MaterialExpressionScalarParameter, "Count", -1400, 600, 1025.0)
    waves = param(mat, unreal.MaterialExpressionScalarParameter, "Waves", -1400, 700, 1.0)
    swd = mel.create_material_expression(mat, unreal.MaterialExpressionSceneDepthWithoutWater, -1400, 850)
    pd = mel.create_material_expression(mat, unreal.MaterialExpressionPixelDepth, -1400, 950)
    thick = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, -1200, 900)
    connect(swd, "", thick, "A")
    connect(pd, "", thick, "B")
    common = ["P", "D", "T", "Info", "Cell", "Count", "Waves"]
    nodes = {
        "wpo": custom(mat, SEA_WPO_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, common, -900, -200, "Waves"),
        "normal": custom(mat, SEA_NORMAL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, common, -900, 100, "Ripples"),
        "foam": custom(mat, SEA_FOAM_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, common + ["Thick"], -900, 400, "Foam"),
    }
    for n in nodes.values():
        connect(p, "", n, "P")
        connect(d, "", n, "D")
        connect(t, "", n, "T")
        connect(info, "", n, "Info")
        connect(cell, "", n, "Cell")
        connect(count, "", n, "Count")
        connect(waves, "", n, "Waves")
    connect(thick, "", nodes["foam"], "Thick")
    foam_c = param(mat, unreal.MaterialExpressionVectorParameter, "FoamColor", -900, 600, unreal.LinearColor(0.8, 0.84, 0.86, 1))
    base = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 500)
    connect(foam_c, "", base, "A")
    connect(nodes["foam"], "", base, "B")
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -500, 650)
    r0 = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 650)
    r0.set_editor_property("r", 0.03)
    r1 = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 720)
    r1.set_editor_property("r", 0.6)
    connect(r0, "", rough, "A")
    connect(r1, "", rough, "B")
    connect(nodes["foam"], "", rough, "Alpha")
    mel.connect_material_property(nodes["wpo"], "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.connect_material_property(nodes["normal"], "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(nodes["foam"], "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    out = mel.create_material_expression(mat, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, -200, 900)
    scat = param(mat, unreal.MaterialExpressionVectorParameter, "Scattering", -600, 850, unreal.LinearColor(0.0001, 0.00035, 0.0004, 1))
    absn = param(mat, unreal.MaterialExpressionVectorParameter, "Absorption", -600, 1000, unreal.LinearColor(0.0045, 0.0009, 0.0006, 1))
    for pin_src, names in ((scat, ["ScatteringCoefficients", "Scattering Coefficients"]),
                           (absn, ["AbsorptionCoefficients", "Absorption Coefficients"])):
        m = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -400, 900)
        m.set_editor_property("r", True)
        m.set_editor_property("g", True)
        m.set_editor_property("b", True)
        connect(pin_src, "", m, "")
        connect_any(m, "", out, names)
    pg = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 1100)
    pg.set_editor_property("r", 0.2)
    connect_any(pg, "", out, ["PhaseG", "Phase G"])
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    log("sea material built")
    return mat


# The island's plants (BFPlants.cpp builds them in the game): colour in the vertex colour (sRGB), A how far that point
# sways in the wind; UV0.x 1 on leaves (they let light through: two-sided foliage); UV1.xy + UV2.x where a leaf
# point shrinks to when it's eaten (its frond's base, local cm). Per instance: custom data 0 how much of it has been
# eaten (0 whole, 1 stripped), and the instance's random number for its shade and its sway's timing. The wind blows
# toward 35 deg, as the sea's waves run, in gusts that travel across the island.
PLANT_WIND_HLSL = r"""
float3 wind = float3(0.819, 0.574, 0.0);
float gust = 0.55 + 0.45 * sin(dot(WP.xy, wind.xy) / 2200.0 - T * 1.1) * (0.6 + 0.4 * sin(T * 0.23 + WP.x / 9000.0));
float sway = VA * (gust + 0.3 * sin(T * 1.7 + R * 6.2832));
float flutter = Leaf * VA * sin(T * 7.0 + R * 17.0 + dot(WP.xy, float2(0.031, 0.027)));
return wind * sway * Sway + float3(0.0, 0.0, 1.0) * flutter * 3.0;
"""

PLANT_COLOUR_HLSL = r"""
float3 c = pow(saturate(VC.rgb), 2.2);
c *= 0.86 + 0.28 * R;
c *= lerp(float3(1.0, 1.0, 1.0), float3(1.1, 1.02, 0.8), Leaf * frac(R * 7.31) * 0.8);
c = lerp(c, c * float3(1.15, 1.0, 0.72), Leaf * E * 0.6);
return c;
"""


# Plants pushed aside (Raven's list, 32): the testing ground's ABFEffects puts up to twelve animals near the camera in
# MPC_BoneForgePush (Push0..11: where its feet are, xyz, and how far round it it pushes, w cm). A stem or frond within
# that leans away from it and is pressed down, more toward its tip (its height over the plant's own base).
PUSH_HLSL = r"""
// (Raven, 2026-11-15: plants smeared where animals walked. Each leaf was pushed away from the foot on its own, so a
// plant tore apart into a stretched triangle. Now the whole plant leans over from its base, away from the feet near
// it (by where its base is, not each leaf), as a bend: a leaf h up goes over h*sin(a) and down h*(1-cos(a)), keeping
// its length. At most ~55 deg, less for taller plants; nothing past 4.5 m up (trees stand).)
float h = max(WP.z - OP.z, 0.0);
float4 K[12] = { K0, K1, K2, K3, K4, K5, K6, K7, K8, K9, K10, K11 };
float2 lean = 0;
for (int i = 0; i < 12; ++i)
{
    if (K[i].w > 1.0 && abs(OP.z - K[i].z) < K[i].w)
    {
        float2 d = OP.xy - K[i].xy;
        float L = length(d);
        float s = saturate(1.0 - L / K[i].w);
        s = s * s * (3.0 - 2.0 * s);
        lean += (L > 1.0 ? d / L : float2(1.0, 0.0)) * s;
    }
}
float m = length(lean);
float3 off = 0;
if (m > 0.001)
{
    float a = min(m, 1.0) * 0.95 * saturate(1.5 - h / 300.0);
    float2 dir = lean / m;
    off.xy = dir * h * sin(a);
    off.z = h * (cos(a) - 1.0);
}
return off;
"""


def build_push_collection():
    path = f"{MATS}/MPC_BoneForgePush"
    if eal.does_asset_exist(path):
        mpc = eal.load_asset(path)
    else:
        mpc = at.create_asset("MPC_BoneForgePush", MATS, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    params = []
    for i in range(12):
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", f"Push{i}")
        p.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))
        params.append(p)
    mpc.set_editor_property("vector_parameters", params)
    eal.save_asset(path)
    return mpc


def build_plant_master():
    mpc = build_push_collection()
    mat = fresh_material("M_BoneForgePlant")
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("dithered_lod_transition", True)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -1400, 0)
    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, -200)
    t = mel.create_material_expression(mat, unreal.MaterialExpressionTime, -1400, -300)
    rnd = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceRandom, -1400, 200)
    eaten = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -1400, 300)
    eaten.set_editor_property("data_index", 0)
    uvs = []
    for i in range(3):
        u = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 400 + 100 * i)
        u.set_editor_property("coordinate_index", i)
        uvs.append(u)
    leaf = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1200, 400)
    leaf.set_editor_property("r", True)
    leaf.set_editor_property("g", False)     # a new mask has R and G on
    connect(uvs[0], "", leaf, "")
    dz = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1200, 600)
    dz.set_editor_property("r", True)
    dz.set_editor_property("g", False)     # a new mask has R and G on
    connect(uvs[2], "", dz, "")
    delta = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -1000, 500)
    connect(uvs[1], "", delta, "A")
    connect(dz, "", delta, "B")
    sway = param(mat, unreal.MaterialExpressionScalarParameter, "Sway", -1200, -100, 45.0)
    wind = custom(mat, PLANT_WIND_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["WP", "VA", "T", "R", "Leaf", "Sway"], -800, -200, "Wind")
    connect(wp, "", wind, "WP")
    connect(vc, "A", wind, "VA")             # the sway is in the alpha (the default output is RGB only)
    connect(t, "", wind, "T")
    connect(rnd, "", wind, "R")
    connect(leaf, "", wind, "Leaf")
    connect(sway, "", wind, "Sway")
    # Eaten: leaf points pulled toward their frond's base, in the plant's own frame, then turned into the world's.
    shrink = custom(mat, "return D * E * Leaf * 0.85;", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["D", "E", "Leaf"], -800, 400, "Eaten")
    connect(delta, "", shrink, "D")
    connect(eaten, "", shrink, "E")
    connect(leaf, "", shrink, "Leaf")
    tw = mel.create_material_expression(mat, unreal.MaterialExpressionTransform, -600, 400)
    tw.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    tw.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(shrink, "", tw, "")
    wpo0 = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -500, 0)
    connect(wind, "", wpo0, "A")
    connect(tw, "", wpo0, "B")
    push = custom(mat, PUSH_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["WP", "OP"] + [f"K{i}" for i in range(12)], -800, -700, "Pushed aside")
    connect(wp, "", push, "WP")
    connect(mel.create_material_expression(mat, unreal.MaterialExpressionObjectPositionWS, -1400, -700), "", push, "OP")
    for i in range(12):
        k = mel.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, -1100, -1400 + 60 * i)
        k.set_editor_property("collection", mpc)
        k.set_editor_property("parameter_name", f"Push{i}")
        connect(k, "", push, f"K{i}")
    wpo = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -400, 0)
    connect(wpo0, "", wpo, "A")
    connect(push, "", wpo, "B")
    colour = custom(mat, PLANT_COLOUR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["VC", "R", "Leaf", "E"], -800, 100, "Colour")
    connect(vc, "", colour, "VC")
    connect(rnd, "", colour, "R")
    connect(leaf, "", colour, "Leaf")
    connect(eaten, "", colour, "E")
    sss = custom(mat, "return C * Leaf * float3(0.75, 0.95, 0.45);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C", "Leaf"], -500, 250, "Light through leaves")
    connect(colour, "", sss, "C")
    connect(leaf, "", sss, "Leaf")
    rough = custom(mat, "return lerp(0.88, 0.62, Leaf);", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["Leaf"], -500, 350, "Roughness")
    connect(leaf, "", rough, "Leaf")
    fill = param(mat, unreal.MaterialExpressionScalarParameter, "Fill", -700, 550, 0.15)
    if hasattr(unreal, "MaterialExpressionEyeAdaptationInverse"):
        ea = mel.create_material_expression(mat, unreal.MaterialExpressionEyeAdaptationInverse, -700, 650)
        glow = custom(mat, "return C * Fill * lerp(1.0, 0.35, Leaf) * EA;", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      ["C", "Fill", "Leaf", "EA"], -500, 550, "Fill light")
    else:
        ea = mel.create_material_expression(mat, unreal.MaterialExpressionEyeAdaptation, -700, 650)
        glow = custom(mat, "return C * Fill * lerp(1.0, 0.35, Leaf) / max(EA, 1e-4);", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      ["C", "Fill", "Leaf", "EA"], -500, 550, "Fill light")
    connect(colour, "", glow, "C")
    connect(fill, "", glow, "Fill")
    connect(leaf, "", glow, "Leaf")
    connect(ea, "", glow, "EA")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    log("plant material built")
    return mat


def build_eye_master():
    """Glossy eye: iris and pupil from the vertex paint (bone_forge_skin.paint: R iris, G pupil); nostrils are
    painted all pupil, so they read as dark openings."""
    mat = fresh_material("M_BoneForgeEye")
    mat.set_editor_property("used_with_skeletal_mesh", True)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 0)
    sclera = param(mat, unreal.MaterialExpressionVectorParameter, "Sclera", -900, -300, unreal.LinearColor(0.03, 0.02, 0.012, 1))
    iris = param(mat, unreal.MaterialExpressionVectorParameter, "Iris", -900, -150, unreal.LinearColor(0.5, 0.3, 0.05, 1))
    pupil = param(mat, unreal.MaterialExpressionVectorParameter, "Pupil", -900, 200, unreal.LinearColor(0.002, 0.002, 0.002, 1))
    a = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -600, -200)
    connect(sclera, "", a, "A")
    connect(iris, "", a, "B")
    connect(vc, "R", a, "Alpha")
    b = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -400, 0)
    connect(a, "", b, "A")
    connect(pupil, "", b, "B")
    connect(vc, "G", b, "Alpha")
    rough = param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", -400, 200, 0.06)
    flare = param(mat, unreal.MaterialExpressionScalarParameter, "NostrilFlare", -900, 400, 0.0)
    side = param(mat, unreal.MaterialExpressionVectorParameter, "HeadRight", -900, 520, unreal.LinearColor(0, 1, 0, 0))
    nws = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, 640)
    nose = custom(mat, NOSTRIL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["VC", "N", "HR", "Flare"], -500, 450, "Nostril flare")
    connect(vc, "", nose, "VC")
    connect(nws, "", nose, "N")
    connect(side, "", nose, "HR")
    connect(flare, "", nose, "Flare")
    mel.connect_material_property(nose, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.connect_material_property(b, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    return mat


def linear(h):
    r, g, b = skin.hex_rgb(h)
    return unreal.LinearColor(r ** 2.2, g ** 2.2, b ** 2.2, 1.0)


def species_instance(key, master, eye_master, tiles, scheme=None):
    s = skin.SKIN[scheme or key]
    folder = f"/Game/BoneForge/{folder_of(key)}"
    name = f"MI_{folder_of(key)}_Skin"
    path = f"{folder}/{name}"
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else at.create_asset(
        name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for k, h in (("Back", s["back"]), ("Belly", s["belly"]), ("Pattern", s["pattern"]), ("Accent", s["accent"])):
        mel.set_material_instance_vector_parameter_value(mi, f"{k}Colour", linear(h))
    for k, t in zip(("Back", "Belly", "Face", "Feet"), s["tiles"]):
        mel.set_material_instance_texture_parameter_value(mi, f"{k}Tile", tiles[t])
    mel.set_material_instance_scalar_parameter_value(mi, "TileCm", s["tile_cm"])
    mel.set_material_instance_scalar_parameter_value(mi, "BumpCm", s["bump"])
    mel.update_material_instance(mi)
    eal.save_asset(path)
    eye_name = f"MI_{folder_of(key)}_Eye"
    eye_path = f"{folder}/{eye_name}"
    eye = eal.load_asset(eye_path) if eal.does_asset_exist(eye_path) else at.create_asset(
        eye_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(eye, eye_master)
    mel.set_material_instance_vector_parameter_value(eye, "Iris", linear(s["iris"]))
    mel.update_material_instance(eye)
    eal.save_asset(eye_path)
    # Toes and fingers: the skin material (its scales) in the toes' keratin colour (bone_forge_skin.scute_rgb).
    scute_name = f"MI_{folder_of(key)}_Scute"
    scute_path = f"{folder}/{scute_name}"
    scute = eal.load_asset(scute_path) if eal.does_asset_exist(scute_path) else at.create_asset(
        scute_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(scute, master)
    r, g, b = skin.scute_rgb(s)
    toe = unreal.LinearColor(r ** 2.2, g ** 2.2, b ** 2.2, 1.0)
    for k in ("Back", "Belly", "Pattern", "Accent"):
        mel.set_material_instance_vector_parameter_value(scute, f"{k}Colour", toe)
    for k, t in zip(("Back", "Belly", "Face", "Feet"), s["tiles"]):
        mel.set_material_instance_texture_parameter_value(scute, f"{k}Tile", tiles[t])
    mel.set_material_instance_scalar_parameter_value(scute, "TileCm", s["tile_cm"])
    mel.set_material_instance_scalar_parameter_value(scute, "BumpCm", s["bump"])
    mel.update_material_instance(scute)
    eal.save_asset(scute_path)
    # Put them on the skeletal mesh's slots: the skin also on the blink's eyelid sets (shown only mid-blink).
    mesh_path = f"{folder}/{key}_skeletal/SkeletalMeshes/{key}_skeletal"
    mesh = eal.load_asset(mesh_path)
    if mesh is None:
        problems.append(f"{key}: no skeletal mesh at {mesh_path}")
        return
    mats = list(mesh.get_editor_property("materials"))
    hit = False
    for i, m in enumerate(mats):
        slot = str(m.get_editor_property("material_slot_name"))
        if slot.endswith(("_Skin", "_LidHalf", "_LidClosed")):
            m.set_editor_property("material_interface", mi)
            mats[i] = m
            hit = True
        elif slot.endswith("_Eye"):
            m.set_editor_property("material_interface", eye)
            mats[i] = m
        elif slot.endswith("_Scute"):
            m.set_editor_property("material_interface", scute)
            mats[i] = m
    if not hit:
        problems.append(f"{key}: no skin slot in {[str(m.get_editor_property('material_slot_name')) for m in mats]}")
        return
    mesh.set_editor_property("materials", mats)
    eal.save_asset(mesh_path)
    log(f"{key}: MI_{folder_of(key)}_Skin on {mesh_path} (tiles {', '.join(s['tiles'])}; {s['tile_cm']} cm a tile)")


def main():
    build_fx()
    if os.environ.get("BF_FX_ONLY"):
        for p in problems:
            unreal.log_error(f"[BoneForge] materials problem: {p}")
        log(f"done (marks and puffs only), {len(problems)} problem(s)")
        return
    tiles = import_tiles()
    build_terrain_master(tiles)
    build_sea()
    build_plant_master()
    build_fall()
    if os.environ.get("BF_TERRAIN_ONLY") or os.environ.get("BF_WORLD_ONLY"):   # just the island's (skins left as they are)
        for p in problems:
            unreal.log_error(f"[BoneForge] materials problem: {p}")
        log(f"done (the world's only), {len(problems)} problem(s)")
        return
    master = build_skin_master(tiles)
    build_membrane_master()
    build_water()
    eye_master = build_eye_master()
    for key in skin.SKIN:
        species_instance(key, master, eye_master, tiles)
    for key, base in splice_schemes().items():     # splices wear their base species' skin
        species_instance(key, master, eye_master, tiles, scheme=base)
    for p in problems:
        unreal.log_error(f"[BoneForge] materials problem: {p}")
    log(f"done, {len(problems)} problem(s)")


main()
