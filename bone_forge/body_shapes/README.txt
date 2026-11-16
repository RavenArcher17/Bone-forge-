Bone Forge body shapes and reference sheets.
*_body.obj       - base body volume per dinosaur, in cm, Y up, facing +X, origin on the ground under the hip sockets
                   (same origin as the rig JSON files). A starting volume to sculpt over, not a finished game mesh.
                   OBJ files are Y-up; Unreal and Blender convert to Z-up on import. Pteranodon's wing membranes are
                   open one-sided sheets (shown folded); make them two-sided in the material.
*_sections.csv   - every cross-section: body/head along the backbone (reach up/down from the backbone, half-width),
                   limbs as horizontal slices (front-to-back and side-to-side thickness), all in cm.
*_turnaround.png - side, front, back, top and 3/4 views at one scale.
*_poses.png      - key poses posed with the rig.
colour_schemes.png - default colour schemes.
Legs and feet (all but the Pteranodon's) are rebuilt from the rig joints by ../body_tools/leg_builder.py
(2026-11-03): deep thighs, calf muscles, and the feet from the design doc's Mouths and Feet tab. The leg and
arm rows of *_sections.csv are slices of those new limbs.
Later 2026-11-03, after Raven's review of the clay renders (body_shapes/views/<species>_sheet.png, made by
../body_tools/views_blender.py and compose_views.py; the Pteranodon flying by flight_blender.py):
  - leg_builder.py: Triceratops, Ankylosaurus and Brachiosaurus stand on pillar feet with nailed toes.
  - parts_builder.py (run after head_builder.py and leg_builder.py): neck_blend joins the neck into the head
    (Brachiosaurus; the T. rex neck is swollen into its skull by head_builder.py instead); real arms and clawed hands for the T. rex (2 fingers + a splint) and the
    Velociraptor (3 fingers); the Ankylosaurus tail club is two big lobes and two small ones, flattened.
Build order for a body: (v1 / trike_body_v2.py) -> head_builder.py -> leg_builder.py -> parts_builder.py
(head_builder.py starts again from <species>_body_v1_fixed.obj, so leg_builder.py and parts_builder.py always follow it).
Faces (2026-11-03): ../body_tools/face_sculpt.py sculpts each skull's anatomy into the head and jaw (brows, eye
sockets, cheekbones, jaw muscles, the hollow in front of the eye, nostril hollows, lip lines, horn bosses, the
Ankylosaurus' head plates, the Brachiosaurus' nasal arch); head_builder.py and trike_body_v2.py run it. Then
parts_builder.py FACE adds eyeballs, nostrils and three eyelid sets per eye (open, half, closed; the testing
ground blinks by switching them). Eye sizes and places, the Velociraptor's long low
skull and the Ankylosaurus' skull (triangular from above) follow the reference library's skull pictures; see
../unreal/README.md, "Skin, eyes and blink". Skin previews: views/skin/<species>_skin_sheet.png
(../body_tools/skin_preview_blender.py, then compose_views.py).
