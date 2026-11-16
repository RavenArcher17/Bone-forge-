BONE FORGE LIFE DRAWINGS AND MOUTHS-AND-FEET SHEETS: SOURCE
=====================================================================

Everything needed to rebuild the LIFE REFERENCE sheets (side view over the faint grey skeleton,
front view, "MOUTH OPEN" strip, "HANDS AND FEET" strip) and the separate "Mouths and Feet"
close-up sheets for T. rex, Triceratops, Velociraptor, Ankylosaurus, Brachiosaurus and Pteranodon.

Re-running build_all.py in a clean folder reproduced the design-doc PNGs pixel for pixel, with one
exception: the Triceratops life sheet. trike.py was fixed (4 hoofed hind toes) after that sheet was
made, so a rebuild shows the corrected toes in the grey ghost skeleton. The difference is only a
114 x 78 px patch on the hind foot.


1. FILES
--------
Helper modules (loaded with exec(open(...).read()), not import):
  lib.py         SVG primitives shared by everything: P() metre->pixel mapping, poly, bone, rib, hoof, toe,
                 side_foot, ellipse, curve_pts (Catmull-Rom), claw, digit, text, save, squash/shift/scaled.
                 Global SNOUT = 0.68 and HEAD_DX = -0.125 (Triceratops face shortening / head moved back).
  life_lib.py    Life-drawing tools: ghost() (greys the skeleton), fill_shape, outline, scales (scale dots),
                 feather_tex, fold, visible() (hidden-line removal), open_edge, mirror, render_parts, ground,
                 load_skeleton().
  mouth_lib.py   Close-up tools: rot, banana (curved teeth), peg (teeth), label, lead (leader lines),
                 scalebar_at, eye, ghost_bones, title, hoof_cap, footprint, sheet_frame.
                 (trex_mouth.py defines its own copies of rot/banana/label and does not load mouth_lib.py.)

Skeleton scripts:  trex.py trike.py raptor.py ankylo.py brachio.py ptera.py
  Each draws the skeleton reference sheet. The life scripts re-use their side()/front()/top() functions to
  draw the grey ghost skeleton under the body (load_skeleton() runs the file up to the line "W, H =").
  trike_life.py instead copies two slices of trike.py by text markers:
  "def side(" ... "# ---------------- TOP VIEW" and "def front(" ... "\ndef foot_top".
  Keep those markers if you edit trike.py.

Life scripts:   <name>_life.py   -> <name>_life.svg      (side view + front view; Pteranodon: top view + side view)
Mouth scripts:  <name>_mouth.py  -> <name>_mouth.svg     (the "Mouths and Feet" sheets; separate scripts)
compose.py      <name>_life.svg + <name>_mouth.svg -> <name>_life_full.svg
                (adds the MOUTH OPEN strip = mouth sheet panels A+B, and the HANDS AND FEET strip = the
                bottom row of the mouth sheet, cropped by viewBox; nothing is redrawn)
build_all.py    runs everything in order and renders the PNGs.
reference_output/  the SVGs and the 2x PNGs exactly as they are in the design doc, for comparison.


2. INPUTS
---------
No external data files, fonts or images are read. Every input is typed into the scripts:
  - Skeleton bone positions: typed in by hand (metres) in the skeleton scripts, drawn from the user's
    Reference Library picks (public-domain / CC-licensed skeletal photos and drawings on Wikimedia Commons;
    see the Reference Library tab of the design doc).
  - Body outlines, legs, feet, heads, frill, wings: typed in by hand in the life scripts as control
    points, smoothed with curve_pts() (Catmull-Rom). They were shaped by eye over the ghost skeleton and
    tuned through several rounds of feedback.
  - Mouth/teeth/claw/foot panels: typed in by hand in the mouth scripts. Tooth counts and claw shapes follow
    the Mouths and Feet tab of the doc.
  - Scale dots and feather strokes: generated, not hand placed. scales() and feather_tex() in life_lib.py
    scatter them on a jittered grid with Python's random module and a FIXED seed per call (seed=11, 12...),
    so they come out the same every run. If an outline changes, the dots inside it change with it.
    tiles() in ankylo_mouth.py and footprint(tex_seed=...) work the same way.
  - Fonts: none bundled. The SVG text asks for "Helvetica, Arial, sans-serif"; the renderer picks a
    system font (Arial on Windows; it was TeX Gyre Heros here), so text can be a pixel or two different.


3. HOW TO RUN
-------------
Language: Python 3 (built and tested with 3.13; nothing newer than 3.8 is needed).
Libraries: only the standard library (math, random, re, subprocess, shutil).
PNG rendering needs ONE of: rsvg-convert (librsvg 2.58 was used; on Windows via MSYS2:
  pacman -S mingw-w64-ucrt-x86_64-librsvg), Inkscape 1.x, or `pip install cairosvg` (on Windows cairosvg
  also needs the Cairo DLLs, e.g. from the GTK runtime). The doc images were made with rsvg-convert.

  cd "C:\Users\raven\OneDrive\Desktop\Claude Root\bone_forge\life_drawings"
  python build_all.py

The scripts must run with this folder as the working directory (they open lib.py etc. by relative path).
Single sheet, by hand:
  python trex.py          (only needed if trex.py changed; life script re-reads its source anyway)
  python trex_life.py     -> trex_life.svg
  python trex_mouth.py    -> trex_mouth.svg
  python compose.py       -> all six <name>_life_full.svg (needs every _life.svg and _mouth.svg)
  rsvg-convert -z 2 trex_life_full.svg -o trex_life_full_sheet.png
  rsvg-convert -z 2 trex_mouth.svg -o trex_mouth_sheet.png
All output goes into the working folder. The design doc's "Life Drawings" tab shows
<name>_life_full_sheet.png; its "Mouths and Feet" tab shows <name>_mouth_sheet.png (both rendered at 2x).


4. WHERE THE SHAPES ARE AND WHAT UNITS THEY USE
-----------------------------------------------
Units: METRES. Side views: x = metres forward from the tail tip (x = 0), y = metres up from the ground (y = 0).
Front views: x = metres sideways from the centre line, y = height; mirror() builds the left half from the
right half (front outlines are written as their right half only). Pteranodon top view: x sideways,
y forward.
Pixels: lib.P(x, y, ox, oy) = (ox + x*S, oy - y*S). S is pixels per metre, a global set per script/view:
  trex_life S=85 (both views)   trike_life S=120         raptor_life S=480
  ankylo_life side S=155, front S=165    brachio_life S=52    ptera_life top S=130, side S=190
  Mouth sheets set S separately inside each panel function (e.g. trex foot_side, foot_below).
  Skeleton sheets: trex 70, trike 90, raptor 330, ankylo 105, brachio 38, ptera 110 (the ghost is
  redrawn at the life script's S, so it always matches the body).
Head adjustments applied in both skeleton and life drawings: T. rex HEAD = 1.25 (trex.py; skull scaled about
(9.92, 3.32)); Triceratops SNOUT = 0.68 and HEAD_DX = -0.125 (lib.py; trike_life.py uses the lambda sq()).

Body, legs and feet in the LIFE sheets (each is a C([...]) control-point list):
  trex_life.py     life_side() line 8:  far_leg l.9, far_arm l.10, body l.11, near_leg l.14, near_arm l.16,
                   head l.17; foot claws l.38-39. life_front() line 42: body l.43, legs l.44, arms l.46,
                   head l.47; toe claws l.57-58.
  trike_life.py    life_side() line 11: far_hind l.12, far_front l.13, body l.14, near_hind l.17,
                   near_front l.18, head l.19; hoof shapes l.52-55. life_front() line 63: body l.64,
                   hind legs l.65-67, front legs l.68-70, frill/face below; hooves near the end of life_front.
  raptor_life.py   life_side() line 7:  far_leg l.8, far_wing l.9, body l.10, tail_fan, near_leg l.14,
                   near_wing l.15, near_arm l.16, head l.17; toes/sickle claw l.43-46.
                   life_front() line 49: body l.50, legs l.51, wings l.53, head l.54; claws l.64.
  ankylo_life.py   life_side() line 15: far_hind l.16, far_front, body l.18, club, near_hind l.21,
                   near_front, head l.23; toenails l.58-62. life_front() line 65: body l.66, legs l.67,
                   head l.69.
  brachio_life.py  life_side() line 7:  far_hind l.8, far_front, body l.10, near_hind l.13, near_front,
                   head l.15; thumb claw l.34, toe claws l.35-36. life_front() line 39: body l.40,
                   legs l.42, neck, head l.44.
  ptera_life.py    life_top() line 12 (wing_shape() l.7 = membrane). life_side() line 31: far_leg l.32,
                   far_arm l.33, body l.34, folded_wing l.36, near_leg l.37, near_arm l.38, head l.39.
Legs are "open" shapes: their top edge isn't drawn, so they merge into the body. render_parts() draws
the parts back to front (far limbs grey, then body, near limbs, head) and visible() hides any line that
falls inside a part in front of it. The leg/foot outlines are what define the drawn legs; the ghost
skeleton underneath comes from the skeleton script (e.g. hind_leg() in trex.py / raptor.py, bone() calls
in side() of trike.py / ankylo.py / brachio.py / ptera.py). If the legs get much heavier, check the ghost
bones still sit inside them, and widen them in the skeleton script if you want the bones heavier too.

Hands and feet in the MOUTH sheets (these are what the HANDS AND FEET strip shows):
  trex_mouth.py    hand() l.149, foot_side() l.172, foot_below() l.193     (panels D, E, F)
  trike_mouth.py   hand() l.99,  foot_side() l.121, foot_below() l.137
  raptor_mouth.py  hand() l.92,  foot_side() l.119, foot_below() l.139
  ankylo_mouth.py  hand() l.105, foot_side() l.125, foot_below() l.141
  brachio_mouth.py hand() l.83,  foot_side() l.98,  prints() l.114  (hand + foot prints from below)
  ptera_mouth.py   hand() l.61,  foot_side() l.86,  foot_below() l.100
  Mouth panels: mouth_side(), mouth_front(), tooth() or beak_tip(). Panel positions are the calls under
  "W, H =" at the bottom of each file. footprint() in mouth_lib.py draws the from-below foot views.
compose.py crops by fixed numbers: the mouth strip is viewBox "0 140 {cx-20} 610", where cx is the x where the
tooth panel starts (the jobs list at the top of compose.py). The hands-and-feet strip is
viewBox "0 800 1600 {H-828}". If you move panels on a mouth sheet, update those numbers.


5. MANUAL STEPS AFTER THE SCRIPTS
---------------------------------
None. Every change was made in the scripts and re-run; no SVG or PNG was edited by hand. The only steps
after the scripts were the PNG renders at 2x (rsvg-convert -z 2) and uploading those PNGs to the design doc.
