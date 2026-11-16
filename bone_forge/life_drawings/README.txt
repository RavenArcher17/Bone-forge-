BONE FORGE - LIFE DRAWINGS AND CLOSE-UP SHEETS: HOW TO RE-RUN THEM
=================================================================

Intended location: C:\Users\raven\OneDrive\Desktop\Claude Root\bone_forge\life_drawings\

What's in this folder
  draw\        the 22 Python scripts, byte-for-byte as they were in bone_forge_source.zip (draw/ folder)
  published\   the 12 PNGs currently in the design doc, downloaded from it, for comparison
  run_all.py   re-runs everything (written for this hand-over; it only calls the scripts in draw\)
  README.txt   this file

A cloud session wrote the drawing scripts. A later cloud session wrote this README: it read the
scripts, re-ran them, and compared the result with the doc's images (see section 5).


1. WHICH SCRIPT MAKES WHICH SHEET
---------------------------------
All six species share one set of helper files. Each species has three scripts:

  <sp>.py          skeleton sheet (side/top/front skeleton). The life sheets do NOT run this whole
                   file. They borrow its side() and front() drawing functions to draw the faint grey
                   skeleton inside the body.
  <sp>_life.py     LIFE REFERENCE, top half: side view (body over the faint grey skeleton) + front
                   view (Pteranodon: top view in flight + side view standing). Writes <sp>_life.svg.
  <sp>_mouth.py    close-up sheet: mouth side + front, tooth, hand, foot side, foot from below
                   (Brachiosaurus: footprints instead). Writes <sp>_mouth.svg.
                   -> this IS the "Mouths and Feet" tab image.
  compose.py       builds the full LIFE REFERENCE sheet (the "Life Drawings" tab image) from the two:
                   <sp>_life.svg on top, then two crops of <sp>_mouth.svg: its mouth row
                   ("MOUTH OPEN") and its hand-and-foot row ("HANDS AND FEET").
                   Writes <sp>_life_full.svg.

  So the hands-and-feet close-ups on the life sheets come from <sp>_mouth.py. Redraw a foot there
  and both tabs change.

  <sp> = trex, trike, raptor, ankylo, brachio, ptera

Helper modules. Each script pulls these in with exec(open("lib.py").read()), so they share one
namespace:
  lib.py         drawing basics: P() metre->pixel, poly, bone, claw, hoof, digit, ellipse,
                 curve_pts (Catmull-Rom), text, save (writes the SVG). lib.py's top comment says
                 "Triceratops"; it was split out of trike.py.
  life_lib.py    life-drawing helpers: ghost() greys out the skeleton, render_parts() fills shapes and
                 hides the hidden lines, scales() scatters the skin-scale dots, feather_tex(),
                 fold(), mirror(), ground(), load_skeleton(), visible().
  mouth_lib.py   close-up helpers: banana/peg teeth, lead lines and labels, footprint(),
                 sheet_frame(), hoof_cap(). trex_mouth.py does NOT use mouth_lib.py; it has its own
                 copies of rot/banana/label.
  trike_life.py  doesn't use load_skeleton(). It cuts two slices out of trike.py's source instead:
                 "def side(" up to "# ---------------- TOP VIEW", and "def front(" up to "def foot_top".


2. INPUTS
---------
No data files. The scripts read only each other's source (lib.py, life_lib.py, mouth_lib.py,
<sp>.py) and, in compose.py, the SVGs the other scripts wrote.

  Outlines, bone and joint positions: typed into the scripts by hand as metre coordinates.
      They were judged by eye from the images in the doc's Reference Library and Reference Sheets
      tabs; the skeleton sheets say "built from Reference Library picks". There's no other record
      of how each number was picked.

  Scale-dot (skin) pattern: generated, not stored. life_lib.py scales() lays a staggered grid at
      `step` spacing, jitters each dot by up to 20% of a step with random.Random(seed), and keeps the
      dots that fall inside the body shapes. The seeds are fixed in each call (e.g. seed=11 and 12 in
      trex_life.py), so it comes out the same every run. Make a leg fatter and the dots fill the new
      shape automatically. feather_tex() works the same way for feathers.

  Fonts: none bundled. The SVGs ask for "Helvetica, Arial, sans-serif"; the renderer substitutes
      whatever it has (Arial on Windows). Only the lettering depends on it.

  Head size: the life sheets reuse the skeleton script's HEAD enlargement (trex.py HEAD = 1.25);
      the hs() lambda in trex_life.py applies it to the life outline too.


3. HOW TO RUN
-------------
Language: Python 3.7 or newer. Standard library only (math, random, re). No pip installs.
Tested on 2026-11-03 with Python 3.13 on Linux: every script runs without errors.

Run the scripts FROM INSIDE draw\ (they open "lib.py" etc. by relative path).

ON WINDOWS, USE UTF-8 MODE (python -X utf8). The titles contain "·". Without UTF-8 mode, Windows
Python writes it as a lone byte the SVG reader rejects. rsvg-convert then stops with
"Input is not proper UTF-8". This was checked by simulating Windows' default file encoding.

  Everything at once (from the life_drawings folder):
      python -X utf8 run_all.py
      python -X utf8 run_all.py trex          (one species; see the compose note below)

  By hand, one species (from inside draw\):
      python -X utf8 trex_life.py            -> draw\trex_life.svg
      python -X utf8 trex_mouth.py           -> draw\trex_mouth.svg
      python -X utf8 compose.py              -> draw\<sp>_life_full.svg for ALL six species
                                               (it needs all 12 SVGs; edit its `jobs` list to do fewer)
  The skeleton sheet itself (not needed for the life sheets):
      python -X utf8 trex.py                 -> draw\trex.svg

  SVG -> PNG: the doc's PNGs are exactly 2x the SVG size, rendered with rsvg-convert (librsvg):
      rsvg-convert -z 2 trex_life_full.svg -o trex_life_full.png     (Life Drawings tab)
      rsvg-convert -z 2 trex_mouth.svg     -o trex_mouth.png         (Mouths and Feet tab)
  Sizes: <sp>_life_full 1500 wide (Ankylosaurus 1560) and 1721-2145 tall; <sp>_mouth 1600 x 1220-1240.
  Both double at -z 2.
  On Windows, rsvg-convert comes with MSYS2 (pacman -S mingw-w64-ucrt-x86_64-librsvg) or
  standalone builds. Inkscape also works, with slightly different anti-aliasing:
      inkscape trex_life_full.svg --export-type=png --export-dpi=192 --export-filename=trex_life_full.png
  run_all.py uses rsvg-convert if it's on PATH and writes the PNGs to life_drawings\out\. Without
  it, it leaves the SVGs.

  Where files go: each script writes its SVG into the folder it runs in (draw\). The original PNG
  file names aren't recorded; the doc stores them as uploaded images. published\ uses
  <sp>_life_full.png and <sp>_mouth.png.

  The "-X utf8" isn't needed on macOS/Linux.


4. WHERE THE BODY, LEG AND FOOT SHAPES ARE, AND THEIR UNITS
-----------------------------------------------------------
Units: every shape is in METRES. lib.py P(x, y, OX, OY) = (OX + x*S, OY - y*S) pixels, where
  S  = pixels per metre (a global, set per sheet or per panel), and
  OX, OY = the pixel position of the metre origin (OY is the ground line).
Side views: x = metres forward from the tail tip, y = metres above the ground.
Front views: x = metres from the midline, y = metres above the ground. Most front shapes are built
  from their right half with mirror(); legs and arms are listed once with a factor s = +1/-1 for
  the two sides.
Shapes are lists of control points smoothed by C(pts, n) = curve_pts (Catmull-Rom, n samples per
span). Line widths are in pixels and don't scale with S.

render_parts() takes `parts` from back to front:
  - "open" parts (legs, arms) leave their top edge open so they blend into the body;
  - far-side limbs are grey (#8f8f8f), near-side limbs are INK (#111);
  - each part hides the outlines of the parts drawn before it.

LIFE SHEETS (<sp>_life.py), line numbers as in draw\:
  trex_life.py     S = 85 px/m. life_side() line 8:
                     far_leg 9, far_arm 10, body 11, near_leg 14, near_arm 16, head 17,
                     parts list 19, toe claws 38-39 (claw(...) at x 6.70/6.82/6.94).
                   life_front() line 42: body 43, legs 44-45, arms 46, head 47, foot claws 57-58.
                   Sheet layout 62-68 (W, H = 1500, 640; side origin 50,560; front origin 1340,560).
  trike_life.py    S = 120. life_side() line 11: far_hind 12, far_front 13, body 14, near_hind 17,
                   near_front 18, head 19. life_front() line 63: body 64, hind 65, front_legs 68,
                   frill 71, face 72. Layout 101-103.
  raptor_life.py   S = 480. life_side() line 7: far_leg 8, far_wing 9, body 10, tail_fan 13,
                   near_leg 14, near_wing 15, near_arm 16, head 17, parts 19, toe claws 46.
                   life_front() line 49: body 50, legs 51, wings 53, head 54, claws 64. Layout 67-69.
  ankylo_life.py   S = 155 for the side view, 165 for the front view (reset at line 93).
                   life_side() line 15: far_hind 16, far_front 17, body 18, club 20, near_hind 21,
                   near_front 22, head 23, parts 24. life_front() line 65: body 66, legs 67, head 69.
                   Layout 91-95.
  brachio_life.py  S = 52. life_side() line 7: far_hind 8, far_front 9, body 10, near_hind 13,
                   near_front 14, head 15, parts 17, claws 34 and 36. life_front() line 39: body 40,
                   neck 41, legs 42, head 44. Layout 56-58.
  ptera_life.py    S = 130 for the top view, 190 for the side view (reset at line 55).
                   life_top() line 12: wings 13 (wing_shape() line 7), body 14, neck 15, head 16.
                   life_side() line 31: far_leg 32, far_arm 33, body 34, folded_wing 36, near_leg 37,
                   near_arm 38, head 39, parts 41. Layout 53-56.

THE FAINT GREY SKELETON inside each life view is drawn live by <sp>.py's side() / front(), then
greyed out by ghost(). For the ghost to sit inside heavier legs, edit the leg bones there too:
  trex.py     hind_leg() line 31; side() calls it at line 57 (far leg) and 91 (near leg) with
              (hip, knee, ankle, foot end) points; front() line 149.
  raptor.py   hind_leg() line 4; called at lines 29 and 58; arm() line 19; front() line 107.
  trike.py    side_foot() line 68; side() line 105 (feet at 110, 113, 151, 156); front() line 228.
  ankylo.py   side_foot_hooves() line 11; side() line 20; front() line 110.
  brachio.py  column_foot() line 4; side() line 16 (feet at 20 and 54); front() line 93.
  ptera.py    side() line 72 (near hind leg ~86, folded arm ~91); front() line 53.
Each skeleton script sets its own S (trex 70, trike 90, raptor 330, ankylo 105, brachio 38,
ptera 110). The life script overrides it right after loading, so the skeleton is drawn at the
life sheet's S.

CLOSE-UP SHEETS (<sp>_mouth.py). Each panel is a function with its own origin (OX, OY) and its
own S, set with "global S" as its first lines:
  trex_mouth.py    mouth_side 26 (S 380), mouth_front 83 (380), tooth 114 (1250), hand 149 (900),
                   foot_side 172 (420), foot_below 193 (420). Layout 223-231.
  trike_mouth.py   mouth_side 5 (205), mouth_front 52 (170), tooth 80 (2600), hand 99 (560),
                   foot_side 121 (420), foot_below 137 (420). Layout 145-152.
  raptor_mouth.py  mouth_side 5 (2500), mouth_front 42 (1900), tooth 66 (12000), hand 92 (1450),
                   foot_side 119 (1250), foot_below 139 (2600). Layout 151-158.
  ankylo_mouth.py  mouth_side 22 (620), mouth_front 62 (520), tooth 85 (10000), hand 105 (600),
                   foot_side 125 (520), foot_below 141 (560). Layout 149-156.
  brachio_mouth.py mouth_side 5 (440), mouth_front 42 (460), tooth 66 (3200), hand 83 (330),
                   foot_side 98 (360), prints 114 (380). Layout 136-143.
  ptera_mouth.py   mouth_side 5 (360), mouth_front 32 (1100), beak_tip 48 (1300), hand 61 (800),
                   foot_side 86 (1700), foot_below 100 (2600). Layout 108-115.
  In these panels x/y are metres from the panel origin (not from the tail tip). The bones inside
  are drawn in GHOST grey with bone() calls in the same function.

compose.py crops by FIXED pixel windows of <sp>_mouth.svg:
  - the mouth row: viewBox "0 140 <cx-20> 610", where cx comes from the `jobs` list (line 9);
  - the hand-and-foot row: viewBox "0 800 1600 <H-28-800>".
If a redraw moves panels on the close-up sheet, update those numbers or parts will be cut off.

Downstream (outside this folder): source\body\capture.py reads <sp>_life.py and records the
outlines it passes to render_parts(). It writes ..\body\<sp>_parts.pkl, which builds the 3D body
OBJs. Run it from inside draw\: python -X utf8 ..\body\capture.py trex (Triceratops:
capture_trike.py). So heavier legs in the life scripts also change the 3D bodies if that pipeline
is re-run. The rig's joint positions (source\rig\rigdata.py) were read off the skeleton drawings
too.


5. HAND EDITS AFTER THE SCRIPTS RAN
-----------------------------------
None found. On 2026-11-03 every sheet was re-run from these scripts, rendered at 2x with
rsvg-convert, and compared pixel by pixel with the image in the doc, with the lettering masked out:
  - 11 of 12 sheets: the drawing is identical. Only the lettering differs, from font substitution.
  - Triceratops life sheet: identical except the faint grey toe bones of the side-view hind foot.
    They sit a few pixels differently, so that PNG was rendered from a slightly earlier version of
    trike.py's foot bones. The outline, legs, feet, claws and scales match exactly.
So the doc's images are straight script output: no touch-ups to repeat. (There is no record from the
session that drew them; this conclusion comes only from the comparison.)


6. CHANGES ON THIS MACHINE (2026-11-03)
---------------------------------------
The legs and feet were redrawn to match the rebuilt 3D legs (body_tools/leg_builder.py: heavier legs, species
feet from the doc's Mouths and Feet tab, T. rex feet 1.5x the first draft). Instead of hand-typed outlines,
the scripts now trace every leg, foot, claw and hoof from the model, so the drawings and the model can't drift
apart:

  draw\model_limbs.py   builds the model's limb pieces (via leg_builder) and traces outlines in a drawing's
                        metres: side_outline / front_outline (opened at the top where the limb meets the body),
                        side_claws / front_claws, side_hoof_shapes / front_hoof_shapes + hoof_lines, side_panel / below_panel.
                        Side views map rig x (cm) to drawing x = x/100 + X0, where X0 puts the 3D body's tail
                        tip on the drawing's tail tip (set at the top of each <sp>_life.py); height = z/100.
  draw\model_panels.py  close-up panels from the model in the sheets' style: model_side (lower leg and foot,
                        bones, scale dots, claws or hooves) and model_below (foot from below, toe pads, hooves,
                        dashed raised toes). box=(left, top, right, bottom) fits a panel into its place.

Changed scripts: trex/raptor/trike/ankylo/brachio _life.py (legs, claws, hooves) and _mouth.py (foot and
hand panels; the T. rex close-up sheet is 1340 tall to fit the bigger feet). The Pteranodon scripts and the
mouths, teeth, hands drawn from the front, and the Brachiosaurus prints are unchanged.
original_draw\ keeps the hand-over's scripts as they were.

No rsvg-convert here: render_png.py renders the SVGs at 2x with headless Chrome or Edge:
    cd draw && python -X utf8 trex_life.py && python -X utf8 trex_mouth.py && python -X utf8 compose.py
    cd .. && python -X utf8 render_png.py draw/trex_life_full.svg draw/trex_mouth.svg     -> out\

Later on 2026-11-03: the Triceratops and Ankylosaurus feet looked blocky. The tracing kept every step between
the model's leg tube sections and every face of its low-poly pad blobs, and the hooves were bounding boxes.
  - model_limbs.SOFT (metres, per species: trike 0.07, ankylo 0.05) softens those outlines: a closing on the
    raster fills notches narrower than about 2 x SOFT, then a Gaussian along the outline (sigma SOFT / 2).
    Species not in SOFT trace exactly as before.
  - Hooves are drawn as nail caps (the hoof blob's upper half with its sides dropped to the ground), and only
    the edges inside the leg's outline are drawn (hoof_lines); the outline draws the rest. Scale dots skip them.
  - model_panels.model_front draws a hand (or foot) from the front from the model; the Triceratops and
    Ankylosaurus "D · HAND FROM THE FRONT" panels use it instead of the hand-drawn palm with box hooves.
  - model_below hides each toe's lines behind the more central toes (the raptor's from-below panel changed by
    one line because of this) and, for hooves, draws the hoof edge and the inner rim of the hoof wall.
  - The pillar feet (leg_builder.nail_toe, 2026-11-03): model_limbs tags nail_toe's pieces as toes (the nail is a
    toe's last piece, as a hoof is), and a pillar foot's sole for the from-below panels is the bottom of the leg
    itself. SOFT now also covers the Brachiosaurus; its close-ups and life sheet draw the nails.

