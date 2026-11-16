"""Rebuild every Bone Forge life drawing and mouth/feet sheet.

Run from THIS folder (the scripts load lib.py etc. by relative path):
    python build_all.py

Writes into this folder:
    <name>.svg              skeleton reference sheet (needed only as a by-product; the life scripts re-use its drawing code)
    <name>_life.svg         LIFE REFERENCE sheet: side view over the grey skeleton + front view
    <name>_mouth.svg        close-up sheet: mouth open (side, front), tooth, hand, foot side, foot from below
    <name>_life_full.svg    life sheet + "MOUTH OPEN" and "HANDS AND FEET" strips cropped from the mouth sheet (compose.py)
    <name>_life_full_sheet.png and <name>_mouth_sheet.png at 2x (the images used in the design doc)
"""
import os, shutil, subprocess, sys

NAMES = ["trex", "trike", "raptor", "ankylo", "brachio", "ptera"]
HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)

def run(script):
    print("python", script)
    subprocess.run([sys.executable, script], check=True)

def to_png(svg, png, zoom=2):
    """Render an SVG at 2x. Tries rsvg-convert, then Inkscape, then the cairosvg Python package."""
    if shutil.which("rsvg-convert"):
        subprocess.run(["rsvg-convert", "-z", str(zoom), svg, "-o", png], check=True); return
    ink = shutil.which("inkscape") or next((p for p in [r"C:\Program Files\Inkscape\bin\inkscape.exe"] if os.path.exists(p)), None)
    if ink:
        subprocess.run([ink, svg, "--export-type=png", f"--export-filename={png}", f"--export-dpi={96 * zoom}"], check=True); return
    try:
        import cairosvg
        cairosvg.svg2png(url=svg, write_to=png, scale=zoom); return
    except ImportError:
        pass
    print("  no SVG renderer found (install rsvg-convert, Inkscape or `pip install cairosvg`); SVG kept:", svg)

for n in NAMES:
    run(f"{n}.py")          # skeleton sheet (also defines the ghost-skeleton drawing the life sheet re-uses)
    run(f"{n}_life.py")
    run(f"{n}_mouth.py")
run("compose.py")
for n in NAMES:
    to_png(f"{n}_life_full.svg", f"{n}_life_full_sheet.png")
    to_png(f"{n}_mouth.svg", f"{n}_mouth_sheet.png")
print("done")
