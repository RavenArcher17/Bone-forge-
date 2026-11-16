import unreal
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
ok = unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Maps/TestGround")
unreal.log("[BFMap] saved=%s exists=%s" % (ok, unreal.EditorAssetLibrary.does_asset_exist("/Game/Maps/TestGround")))