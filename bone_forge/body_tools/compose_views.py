"""Lay out views_blender.py's four renders of each model as one sheet, with headless Chrome (no PIL here).

    python compose_views.py <views_dir> trex raptor ...     -> <views_dir>/<name>_sheet.png
    python compose_views.py <skin views_dir> faces          -> all six species' face close-ups on one sheet
"""
import os
import subprocess
import sys

TITLES = {"trex": "Tyrannosaurus rex", "raptor": "Velociraptor", "trike": "Triceratops", "ankylo": "Ankylosaurus",
          "brachio": "Brachiosaurus", "ptera": "Pteranodon"}
VIEWS = [("34", "3/4 from the front right"), ("side", "Right side"), ("front", "Front"), ("feet", "Feet, near side")]
# The Pteranodon's membranes are folded into thin strips standing; its sheet shows it flying too (flight_blender.py).
VIEWS_FOR = {"ptera": [("34", "Standing, 3/4 from the front right"), ("side", "Standing, right side (wings folded)"),
                       ("flight_34", "Flying, wings spread"), ("flight_below", "Flying, from below")]}
# Skin previews (skin_preview_blender.py, named <species>_skin_<view>.png).
SKIN_VIEWS = [("34", "3/4 from the front right"), ("side", "Right side"), ("head", "Head: eyes, brows, nostrils"),
              ("feet", "Feet")]
# The faces sheet: each species' side-on face close-up from skin_preview_blender.py (<species>_skin_face.png).
FACES = ["trex", "trike", "raptor", "ankylo", "brachio", "ptera"]
BROWSERS = [r"C:\Program Files\Google\Chrome\Application\chrome.exe",
            r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"]

views_dir = os.path.abspath(sys.argv[1])
browser = next(b for b in BROWSERS if os.path.exists(b))
W, H = 1200, 1030
for name in sys.argv[2:]:
    files = ([(f"{k}_skin_face", TITLES[k]) for k in FACES] if name == "faces" else
             [(f"{name}_{v}", cap) for v, cap in (SKIN_VIEWS if name.endswith("_skin") else VIEWS_FOR.get(name, VIEWS))])
    cells = "".join(f'<figure><img src="file:///{os.path.join(views_dir, f"{f}.png").replace(os.sep, "/")}">'
                    f"<figcaption>{cap}</figcaption></figure>" for f, cap in files)
    head = (("Faces", "Each skull's anatomy sculpted into the head (body_tools/face_sculpt.py): brows, eye sockets, "
             "cheekbones, jaw muscles, nostril hollows, lip lines") if name == "faces" else
            (f'{TITLES.get(name.replace("_skin", ""), name)} · {"skin" if name.endswith("_skin") else "3D model"}',
             "Skin preview: the colour scheme, scale tiles and face as the Unreal material draws them "
             "(body_tools/skin_preview_blender.py)" if name.endswith("_skin") else
             f"Clay render of bone_forge/body_shapes/{name}_body.obj (the model imported into the Unreal testing ground)"))
    height = H if len(files) <= 4 else H + 490 * ((len(files) - 3) // 2)
    page = os.path.join(views_dir, f"{name}_sheet.html")
    with open(page, "w", encoding="utf-8") as f:
        f.write(f"""<html><head><style>
body {{ margin: 0; background: #fff; font-family: Segoe UI, Arial, sans-serif; width: {W}px; }}
h1 {{ font-size: 28px; margin: 18px 24px 4px; }}
p {{ margin: 0 24px 10px; color: #555; font-size: 15px; }}
.grid {{ display: grid; grid-template-columns: 1fr 1fr; gap: 8px; padding: 0 12px; }}
figure {{ margin: 0; }} img {{ width: 100%; display: block; border-radius: 4px; }}
figcaption {{ font-size: 14px; color: #444; padding: 4px 2px 6px; }}
</style></head><body><h1>{head[0]}</h1>
<p>{head[1]}</p>
<div class="grid">{cells}</div></body></html>""")
    out = os.path.join(views_dir, f"{name}_sheet.png")
    subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=2",
                    f"--window-size={W},{height}", f"--screenshot={out}", "--allow-file-access-from-files",
                    "file:///" + page.replace(os.sep, "/")], check=True, capture_output=True)
    os.remove(page)
    print("wrote", out)
