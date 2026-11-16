Bone Forge rig files: one JSON file per dinosaur, matching the Joints and Motion tab of the design doc.

Each joint has:
  name, parent      - shared names across all species so body sections can be swapped
  position_cm       - [X, Y, Z] in Unreal axes: X forward, Y right, Z up; origin on the ground under the hip sockets
  section           - which swappable body section (Bone Data tab) the joint belongs to
  limits_deg        - {bend, side, twist}: [min, max] degrees from the neutral standing pose, or null (no motion / free root)
  basis, source     - where the limit comes from: study, study-based, or estimate
  role, limit_note  - plain-language notes

Angle signs: bend + raises neck/head/tail, swings hip/shoulder forward, bends knee/elbow/ankle/toes more, opens the jaw.
side + = left/right for neck/head/tail, away from the body for hip/shoulder. twist = roll around the bone.
Limits are maximums; keep everyday animation to about two thirds of them.

*_joints.png - the joint maps: numbered joints over the skeleton reference drawing.

body_tilt - whole-body nose-down / nose-up tilt at the hips with the feet planted (degrees, max and everyday),
            and tail_clear = the nose-up tilt at which a straight tail touches the ground (lift the tail past it).
            Two-legged animals seesaw over the hips; four-legged ones only tilt as far as their legs can shorten/lengthen.

mass_kg, balance_point_cm ([X, Y, Z] Unreal axes), collision_capsules (one per bone, for the physics asset)
and gaits (walk and fastest gait: speed, stride length, stride time) come from the Weight and Physics and Movement tabs.

flight_pose_cm (Pteranodon only) - joint positions with the wings spread for flight; the wing membrane stretches between
the arm and wing finger, the body side, hip and ankle.

flight_muscle_kg, flight_check (Pteranodon and the splices) - the flight check from splice/splice.py: weight, wing
area, wing loading against the 245 N/m^2 limit, flight muscle against 15% of the weight, the verdict (flies, glides,
grounded) and a plain-words summary the testing ground shows.

Splices (raptorwings, ankylowings ...) are made by splice/splice.py from two of the rigs above: see splice/README.txt.
Their joints carry "lineage" (the species each came from) and the file a "splice" block (base, donor, mass shares).

tissue (on joints) - the flesh around a bone, the body's muscle anchors: stations [t along the bone, centre across,
centre up, half-width, half-height] in cm and the bone's frame, measured from the hand-built body by
body_tools/grow_body.py, which grows bodies from them (tissue_note at the end of the file says the same).
leg_check - whether the walking legs' muscle holds the body up (walks / stands / buckles), from splice/splice.py.
mount_from_cm (spliced limb joints) - the base's own socket, when a swapped limb was mounted out on the skin.
