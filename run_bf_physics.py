"""Runs bone_forge_physics.py from a path without spaces (the pythonscript commandlet splits on them)."""
import sys

script = r"C:\Users\raven\OneDrive\Desktop\Claude Root\bone_forge\unreal\bone_forge_physics.py"
sys.argv = [script]
with open(script, encoding="utf-8") as f:
    code = compile(f.read(), script, "exec")
exec(code, {"__name__": "__main__", "__file__": script})