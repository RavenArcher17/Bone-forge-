exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(load_skeleton("ptera.py"))
S = 130
C = lambda pts, n=6: curve_pts(pts, n)

def wing_shape(s):
    """Wing membrane from the body out to the wing tip and back to the leg (top view, x sideways, y forward)."""
    return C([(0.10 * s, 0.22), (0.42 * s, 0.28), (1.0 * s, 0.34), (1.62 * s, 0.34), (2.15 * s, 0.26), (2.55 * s, 0.14), (2.86 * s, 0.02), (3.14 * s, -0.12),
              (2.70 * s, -0.24), (2.0 * s, -0.40), (1.2 * s, -0.48), (0.55 * s, -0.50), (0.27 * s, -0.56), (0.10 * s, -0.30)], 6)

def life_top(OX, OY):
    wings = [wing_shape(1), wing_shape(-1)]
    body = C(mirror([(0.0, 0.32), (0.08, 0.28), (0.13, 0.12), (0.12, -0.12), (0.06, -0.30), (0.0, -0.34)]), 6)
    neck = C(mirror([(0.0, 0.66), (0.045, 0.62), (0.06, 0.30), (0.0, 0.26)]), 4)
    head = C(mirror([(0.0, 1.80), (0.014, 1.66), (0.04, 1.25), (0.07, 0.85), (0.075, 0.66), (0.0, 0.30)]), 6)
    pro = [C([(0.10 * s, 0.24), (0.38 * s, 0.36), (0.72 * s, 0.48), (1.0 * s, 0.36), (0.42 * s, 0.30)], 4) for s in (1, -1)]   # small front membrane
    parts = [(w, "closed", INK, 2.6) for w in wings] + [(p, "closed", INK, 2.0) for p in pro] + [(body, "closed", INK, 2.6), (neck, "closed", INK, 2.4), (head, "closed", INK, 2.6)]
    def tex():
        feather_tex([body, neck], OX, OY, 0.03, 0.02, 265, exclude=[head], seed=51)
    render_parts(parts, OX, OY, top, tex)
    # membrane stretch lines (fibres) radiating toward the wing tip
    for s in (1, -1):
        for k in range(7):
            t = (k + 1) / 8
            x0, y0 = (0.4 + 2.4 * t) * s, 0.34 - 0.36 * t ** 1.4
            fold([(x0, y0), (x0 - 0.25 * s * (1 - t), y0 - 0.55 * (1 - t * 0.6))], OX, OY, sw=0.9, stroke="#aaa")
        ellipse(0.05 * s, 0.76, 0.02, 0.03, OX, OY, sw=1.4)
    text(OX - 3.2 * S, OY - 1.95 * S, "TOP VIEW (wings spread)", 22)

def life_side(OX, OY):
    far_leg = C([(0.50, 0.80), (0.70, 0.46), (0.60, 0.08), (0.66, 0.02), (0.66, 0.0), (0.52, 0.0), (0.52, 0.08), (0.58, 0.44), (0.40, 0.74)], 5)
    far_arm = C([(1.06, 1.06), (0.94, 0.62), (1.16, 0.44), (1.34, 0.02), (1.24, 0.02), (1.06, 0.40), (0.86, 0.60), (0.92, 1.06)], 5)
    body = C([(0.26, 0.86), (0.45, 0.94), (0.78, 1.10), (1.02, 1.16), (1.12, 1.28), (1.17, 1.44), (1.25, 1.52), (1.30, 1.40), (1.22, 1.22),
              (1.14, 1.04), (0.98, 0.88), (0.70, 0.76), (0.42, 0.70), (0.22, 0.74), (0.18, 0.80)], 6)
    folded_wing = C([(1.30, 0.06), (1.10, 0.56), (0.86, 0.98), (0.58, 1.26), (0.30, 1.43), (0.24, 1.37), (0.48, 1.18), (0.76, 0.90), (0.98, 0.50), (1.20, 0.04)], 5)
    near_leg = C([(0.48, 0.80), (0.66, 0.46), (0.54, 0.08), (0.60, 0.02), (0.60, 0.0), (0.46, 0.0), (0.46, 0.08), (0.52, 0.44), (0.36, 0.74)], 5)
    near_arm = C([(1.04, 1.04), (0.92, 0.62), (1.14, 0.44), (1.32, 0.02), (1.22, 0.02), (1.04, 0.40), (0.84, 0.60), (0.90, 1.04)], 5)
    head = C([(1.18, 1.46), (1.12, 1.62), (0.90, 1.82), (0.55, 1.98), (0.62, 1.93), (1.00, 1.72), (1.30, 1.62), (1.70, 1.50), (2.10, 1.38),
              (2.40, 1.30), (2.10, 1.32), (1.70, 1.40), (1.35, 1.43), (1.18, 1.46)], 5)
    parts = [(far_leg, "open", "#8f8f8f", 2.0), (far_arm, "closed", "#8f8f8f", 2.0), (body, "closed", INK, 2.6), (folded_wing, "closed", INK, 2.2),
             (near_leg, "open", INK, 2.4), (near_arm, "closed", INK, 2.4), (head, "closed", INK, 2.6)]
    def tex():
        feather_tex([body], OX, OY, 0.03, 0.02, 200, exclude=[head, folded_wing, near_arm], seed=52)
    render_parts(parts, OX, OY, side, tex)
    ground(OX, OY, 0.0, 2.6)
    ellipse(1.28, 1.55, 0.03, 0.03, OX, OY, sw=1.8); ellipse(1.285, 1.55, 0.012, 0.012, OX, OY, fill=INK, sw=1)
    fold(C([(1.35, 1.43), (1.85, 1.36), (2.36, 1.30)], 4), OX, OY, sw=1.4, stroke=INK)
    for k in range(4):
        digit((1.28, 0.05), -10 + k * 8, [0.04, 0.03], 0.012, OX, OY, droop=-10, clawL=0.035, curl=50)
    text(OX - 60, OY - 2.15 * S, "SIDE VIEW (standing, wings folded)", 22)

W, H = 1500, 1000
life_top(450, 400)
S = 190
life_side(560, 950)
text(40, 50, "PTERANODON LONGICEPS · LIFE REFERENCE", 28, "700")
text(40, 78, "Body over the skeleton · short fuzzy body covering, bare skin-and-fibre wing membranes · wingspan about 6.2 m", 16, "400", "start", "#555")
text(W - 40, 985, "Faint grey = skeleton inside the body", 15, "400", "end", "#777")
save("ptera_life.svg", W, H)
print("ok")
