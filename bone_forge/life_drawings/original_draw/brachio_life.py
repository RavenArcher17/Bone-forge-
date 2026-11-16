exec(open("lib.py").read())
exec(open("life_lib.py").read())
exec(load_skeleton("brachio.py"))
S = 52
C = lambda pts, n=6: curve_pts(pts, n)

def life_side(OX, OY):
    far_hind = C([(7.25, 4.2), (7.40, 2.5), (7.30, 1.40), (7.25, 0.55), (7.45, 0.15), (7.60, 0.0), (6.55, 0.0), (6.55, 0.55), (6.50, 1.40), (6.40, 2.5), (6.1, 4.2)], 5)
    far_front = C([(11.40, 5.2), (11.05, 2.80), (11.25, 1.30), (11.35, 0.30), (11.45, 0.0), (10.55, 0.0), (10.60, 0.30), (10.50, 1.30), (10.30, 2.80), (10.40, 5.2)], 5)
    body = C([(-0.15, 2.33), (2.5, 3.78), (5.0, 5.22), (6.6, 5.92), (8.5, 6.15), (10.6, 6.72), (11.4, 7.15), (12.6, 8.45), (13.8, 10.25), (14.9, 11.80),
              (15.8, 12.85), (16.3, 12.85), (16.4, 12.0), (15.7, 11.25), (14.7, 9.75), (13.5, 8.0), (12.4, 6.25), (11.7, 4.9), (11.2, 4.1),
              (10.2, 3.25), (8.5, 2.55), (7.0, 2.75), (5.4, 3.40), (3.5, 3.05), (1.5, 2.45), (-0.15, 2.10)], 6)
    near_hind = C([(7.10, 4.3), (7.28, 2.5), (7.18, 1.40), (7.15, 0.55), (7.40, 0.15), (7.55, 0.0), (6.40, 0.0), (6.40, 0.55), (6.35, 1.40), (6.25, 2.5), (5.95, 4.3)], 5)
    near_front = C([(11.48, 5.3), (11.12, 2.80), (11.32, 1.30), (11.42, 0.30), (11.52, 0.0), (10.62, 0.0), (10.67, 0.30), (10.57, 1.30), (10.37, 2.80), (10.30, 5.3)], 5)
    head = C([(16.0, 12.30), (16.1, 12.85), (16.45, 13.25), (16.75, 13.05), (16.95, 12.65), (17.25, 12.30), (17.40, 12.05), (17.30, 11.85),
              (16.95, 11.82), (16.5, 11.88), (16.15, 11.98), (16.0, 12.30)], 6)
    parts = [(far_hind, "open", "#8f8f8f", 2.0), (far_front, "open", "#8f8f8f", 2.0), (body, "closed", INK, 2.6),
             (near_hind, "open", INK, 2.6), (near_front, "open", INK, 2.6), (head, "closed", INK, 2.8)]
    def tex():
        scales([body, near_hind, near_front], OX, OY, 0.75, 0.13, exclude=[head], seed=41, big=lambda x, y: 1.0 + 0.3 * (5.5 < x < 11 and y > 4.5))
        scales([head], OX, OY, 0.28, 0.05, seed=42)
    render_parts(parts, OX, OY, side, tex)
    ground(OX, OY, -0.3, 18.0)
    ellipse(16.40, 12.40, 0.13, 0.12, OX, OY, sw=1.8); ellipse(16.42, 12.40, 0.05, 0.05, OX, OY, fill=INK, sw=1)
    ellipse(16.68, 12.85, 0.10, 0.07, OX, OY, rot=-20, sw=1.5)
    fold(C([(16.3, 11.96), (16.8, 11.90), (17.35, 11.98)], 4), OX, OY, sw=1.6, stroke=INK)
    for k in range(9):   # neck folds, kept inside the neck
        a = 12.2 + k * 0.45; y = 7.6 + (a - 11.4) * 1.45
        p1, p2 = (a - 0.05, y + 0.25), (a + 0.25, y - 0.05)
        if inside(p1, body) and inside(p2, body):
            fold([p1, p2], OX, OY, sw=1.0, stroke="#999")
    fold(C([(10.35, 4.6), (10.7, 4.2), (10.9, 3.6)], 4), OX, OY)
    fold(C([(6.1, 4.6), (6.6, 4.25), (7.2, 4.3)], 4), OX, OY)
    claw((11.20, 0.40), 170, 0.32, 0.15, OX, OY, curl=-30)
    for x in (7.25, 7.40, 7.55):
        claw((x, 0.10), -15, 0.28, 0.14, OX, OY, curl=30)
    text(OX, 140, "SIDE VIEW", 22)

def life_front(OX, OY):
    body = C(mirror([(0.0, 6.6), (1.10, 6.25), (1.52, 5.2), (1.45, 3.9), (1.10, 3.25), (0.0, 3.05)]), 6)
    neck = C(mirror([(0.0, 12.0), (0.32, 11.8), (0.40, 9.5), (0.52, 7.5), (0.74, 6.25), (0.0, 6.0)]), 6)
    legs = [C([(0.70 * s, 5.2), (0.80 * s, 3.6), (0.75 * s, 2.0), (0.72 * s, 0.6), (0.68 * s, 0.0), (1.32 * s, 0.0), (1.28 * s, 0.6),
               (1.30 * s, 2.0), (1.38 * s, 3.6), (1.483 * s, 5.2)], 5) for s in (1, -1)]
    head = C(mirror([(0.0, 13.15), (0.28, 13.0), (0.34, 12.5), (0.26, 12.05), (0.0, 11.9)]), 6)
    parts = [(body, "closed", INK, 2.6)] + [(l, "open", INK, 2.6) for l in legs] + [(neck, "open", INK, 2.6), (head, "closed", INK, 2.8)]
    def tex():
        scales([body] + legs + [neck], OX, OY, 0.55, 0.10, exclude=[head], seed=43)
        scales([head], OX, OY, 0.22, 0.04, seed=44)
    render_parts(parts, OX, OY, front, tex)
    ground(OX, OY, -2.0, 2.0)
    for s in (1, -1):
        ellipse(0.20 * s, 12.48, 0.07, 0.08, OX, OY, sw=1.8); ellipse(0.20 * s, 12.48, 0.03, 0.03, OX, OY, fill=INK, sw=1)
        ellipse(0.10 * s, 12.85, 0.06, 0.08, OX, OY, sw=1.3)
    text(OX, OY + 32, "FRONT VIEW", 22, "600", "middle")

W, H = 1500, 830
life_side(40, 780)
life_front(1240, 780)
text(50, 50, "BRACHIOSAURUS ALTITHORAX · LIFE REFERENCE", 28, "700")
text(50, 78, "Body over the skeleton · scaly skin, larger scales on the back · about 22 m long, head about 13 m up", 16, "400", "start", "#555")
text(W - 40, 70, "Faint grey = skeleton inside the body", 15, "400", "end", "#777")
save("brachio_life.svg", W, H)
print("ok")
