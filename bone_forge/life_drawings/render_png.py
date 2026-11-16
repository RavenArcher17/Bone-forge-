"""SVG -> PNG at 2x with headless Chrome or Edge, for Windows machines without rsvg-convert.

    python -X utf8 render_png.py draw/trex_life_full.svg [more.svg ...]   -> out/<name>.png

Matches the doc's PNGs: 2x the SVG's width and height, white background.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BROWSERS = [r"C:\Program Files\Google\Chrome\Application\chrome.exe",
            r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"]


def size(svg):
    head = open(svg, encoding="utf-8").read(2000)
    w = float(re.search(r'width="([\d.]+)', head).group(1))
    h = float(re.search(r'height="([\d.]+)', head).group(1))
    return int(round(w)), int(round(h))


def render(svg, out):
    w, h = size(svg)
    browser = next(b for b in BROWSERS if os.path.exists(b))
    # An HTML wrapper gives the page a white background and no margin around the SVG.
    page = out + ".html"
    with open(page, "w", encoding="utf-8") as f:
        f.write(f'<html><body style="margin:0;background:#fff"><img src="file:///{os.path.abspath(svg).replace(os.sep, "/")}" '
                f'width="{w}" height="{h}" style="display:block"></body></html>')
    subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=2",
                    f"--window-size={w},{h}", f"--screenshot={os.path.abspath(out)}", "--allow-file-access-from-files",
                    "file:///" + os.path.abspath(page).replace(os.sep, "/")], check=True, capture_output=True)
    os.remove(page)


if __name__ == "__main__":
    os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
    for svg in sys.argv[1:]:
        out = os.path.join(HERE, "out", os.path.splitext(os.path.basename(svg))[0] + ".png")
        render(svg, out)
        print("wrote", out)
