import re
def inner(path):
    s = open(path).read()
    m = re.match(r'<svg[^>]*width="(\d+)" height="(\d+)"[^>]*>(.*)</svg>\s*$', s, re.S)
    return int(m.group(1)), int(m.group(2)), m.group(3)
def T(x, y, s, size, weight="600", fill="#111", anchor="start"):
    return f'<text x="{x}" y="{y}" font-family="Helvetica, Arial, sans-serif" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
# (life file, mouth file, x where the tooth panel starts on the mouth sheet, mouth heading)
jobs = [("trike", 1194, "MOUTH OPEN"), ("trex", 1230, "MOUTH OPEN"), ("raptor", 1254, "MOUTH OPEN"),
        ("ankylo", 1260, "MOUTH OPEN"), ("brachio", 1304, "MOUTH OPEN"), ("ptera", 1231, "BEAK OPEN")]
for name, cx, head in jobs:
    Wl, Hl, life = inner(f"{name}_life.svg")
    Wm, Hm, mouth = inner(f"{name}_mouth.svg")
    pad = 40
    aw, ah = cx - 20, 610            # mouth side + front panels, below their titles
    s1 = min(1.0, (Wl - 2 * pad) / aw)
    bw, bh = 1600, Hm - 28 - 800     # hand and foot row
    s2 = (Wl - 2 * pad) / bw
    y1 = Hl + 30
    y2 = y1 + 40 + ah * s1 + 30
    H = int(y2 + 40 + bh * s2 + 30)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{Wl}" height="{H}" viewBox="0 0 {Wl} {H}">',
             '<rect width="100%" height="100%" fill="#fff"/>', life,
             f'<line x1="{pad}" y1="{Hl + 8}" x2="{Wl - pad}" y2="{Hl + 8}" stroke="#ddd" stroke-width="1.5"/>',
             T(pad, y1 + 20, head + " · the living animal's jaws and teeth", 22),
             f'<svg x="{pad}" y="{y1 + 40}" width="{aw * s1:.0f}" height="{ah * s1:.0f}" viewBox="0 140 {aw} {ah}">{mouth}</svg>',
             T(pad, y2 + 20, "HANDS AND FEET · claws, pads and toes", 22),
             f'<svg x="{pad}" y="{y2 + 40:.0f}" width="{bw * s2:.0f}" height="{bh * s2:.0f}" viewBox="0 800 {bw} {bh}">{mouth}</svg>',
             '</svg>']
    open(f"{name}_life_full.svg", "w").write("".join(parts))
    print(name, Wl, H)
