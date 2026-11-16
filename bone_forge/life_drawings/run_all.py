"""Re-run the Bone Forge life drawings and close-up sheets.

Written for the hand-over; the drawing scripts in draw/ are unchanged.

    python -X utf8 run_all.py              all six species
    python -X utf8 run_all.py trex raptor  only these (compose.py still needs all 12 SVGs; see README.txt)

Runs each script inside draw/ in Python UTF-8 mode, then compose.py, then renders
<sp>_life_full.png and <sp>_mouth.png at 2x into out/ with rsvg-convert (if it is on PATH).
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DRAW = os.path.join(HERE, "draw")
OUT = os.path.join(HERE, "out")
ALL = ["trike", "trex", "raptor", "ankylo", "brachio", "ptera"]


def run(script):
    print("running", script)
    subprocess.run([sys.executable, "-X", "utf8", script], cwd=DRAW, check=True, stdout=subprocess.DEVNULL)


species = sys.argv[1:] or ALL
for sp in species:
    if sp not in ALL:
        sys.exit(f"unknown species {sp!r}; use one of {', '.join(ALL)}")
    run(f"{sp}_life.py")
    run(f"{sp}_mouth.py")

missing = [f for sp in ALL for f in (f"{sp}_life.svg", f"{sp}_mouth.svg") if not os.path.exists(os.path.join(DRAW, f))]
if missing:
    print("compose.py skipped: it builds all six species and these SVGs are missing:", ", ".join(missing))
else:
    run("compose.py")

rsvg = shutil.which("rsvg-convert")
if not rsvg:
    print("rsvg-convert not found: the SVGs are in draw/. See README.txt section 3 to render PNGs.")
    sys.exit(0)
os.makedirs(OUT, exist_ok=True)
for sp in species:
    for name in (f"{sp}_life_full", f"{sp}_mouth"):
        svg = os.path.join(DRAW, name + ".svg")
        if os.path.exists(svg):
            subprocess.run([rsvg, "-z", "2", svg, "-o", os.path.join(OUT, name + ".png")], check=True)
            print("wrote", os.path.join(OUT, name + ".png"))
