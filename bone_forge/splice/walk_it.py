"""The splicing editor's Walk It: waits for the testing ground to close, imports the creature, and starts the testing
ground again with it. (Unreal can't save assets while the game has them open.)

    python splice/walk_it.py <game pid> <UnrealEditor.exe> <project.uproject> <creature key>
"""
import ctypes
import os
import subprocess
import sys

SYNCHRONIZE = 0x00100000
INFINITE = 0xFFFFFFFF


def main(argv):
    pid, editor, project, key = int(argv[0]), argv[1], argv[2], argv[3]
    print(f"Importing {key}: the testing ground reopens by itself in about a minute.", flush=True)
    kernel = ctypes.windll.kernel32
    handle = kernel.OpenProcess(SYNCHRONIZE, False, pid)
    if handle:
        kernel.WaitForSingleObject(handle, INFINITE)
        kernel.CloseHandle(handle)
    folder = os.path.dirname(project)
    command = os.path.join(os.path.dirname(editor), "UnrealEditor-Cmd.exe")
    logs = os.path.join(folder, "Saved", "Splice")
    os.makedirs(logs, exist_ok=True)
    for label, script, env in (("the creature", "run_bf.py", {"BF_SPECIES": key}), ("its skin", "run_bf_materials.py", {})):
        print(f"  importing {label}...", flush=True)
        with open(os.path.join(logs, f"walk_it_{script}.log"), "w", encoding="utf-8") as log:
            # run_bf.py exits 1 with its usual "OBJ needs a fix" note; the game reports a mesh that really didn't import.
            subprocess.run([command, project, "-run=pythonscript", f"-script={os.path.join(folder, script)}", "-unattended",
                            "-nosplash", "-nullrhi"], env=dict(os.environ, **env), stdout=log, stderr=subprocess.STDOUT)
    print("  starting the testing ground...", flush=True)
    extra = argv[4:]        # the scripted check passes its own flags on
    subprocess.Popen([editor, project, "-game", "-windowed", "-ResX=1600", "-ResY=900", "-nosplash", f"-bfstart={key}"] + extra)


if __name__ == "__main__":
    main(sys.argv[1:])
