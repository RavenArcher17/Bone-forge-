DINO SKIN TEXTURE TILES
All tiles are 1024 x 1024, seamless (they repeat with no visible edge).

Each skin type has two files:
  *_height.png  grayscale height / bump map (white = raised)
  *_normal.png  tangent-space normal map, OpenGL style (green = up)

In Unreal: import the normal map, and in its texture settings tick "Flip Green Channel"
(Unreal uses DirectX-style normals). Import height maps with sRGB turned off.
Use the height map for displacement / parallax, or as a mask to darken grooves in the colour.

Tiles and where to use them:
  trex_pebble            T. rex body; Ankylosaurus legs and belly
  trike_cone_scales      Triceratops body and legs
  trike_frill_polygons   Triceratops frill and face; any face
  ankylo_armor           Ankylosaurus back, sides and tail
  sauropod_mixed         Brachiosaurus whole body
  foot_scutes            toes and feet of T. rex, Velociraptor, Pteranodon (replaced 2026-11-03, see below)
  theropod_foot          toes and feet of T. rex, Velociraptor, Pteranodon: rounded scales of mixed sizes

theropod_foot is not from the original tile set: body_tools/foot_tile_blender.py draws it. foot_scutes' regular
brick rows looked man-made on the T. rex feet (Raven, 2026-11-03): with no UVs the skin lays a tile across the
feet from fixed directions, so the rows could not follow the toes.
  raptor_feathers        Velociraptor body, arms and tail base
  ptera_fuzz             Pteranodon body and neck
  ptera_membrane         Pteranodon wings
