import unreal

rot = unreal.Rotator(roll=90, pitch=0, yaw=0)
for name in ("leg_l_2", "leg_r_3", "head_1", "body_0"):
    m = unreal.EditorAssetLibrary.load_asset(f"/Game/BoneForge/Trex/ObjCheck/{name}")
    b = m.get_bounding_box()
    c = (b.min + b.max) * 0.5
    f = unreal.MathLibrary.greater_greater_vector_rotator(c, rot)
    unreal.log(f"[Probe] {name}: centre {c.x:.0f} {c.y:.0f} {c.z:.0f} -> Roll 90: {f.x:.0f} {f.y:.0f} {f.z:.0f}  (min z {b.min.z:.0f}, min y {b.min.y:.0f}, max y {b.max.y:.0f})")
