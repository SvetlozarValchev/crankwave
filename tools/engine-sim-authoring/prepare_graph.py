#!/usr/bin/env python3
"""Build a read-only graph adapter against an existing native engine-sim build.

Staged headers gain friend declarations only: no layout, code or parameter changes.
The generated adapter links the same native interpreter/physics libraries as the
audition renderer. No modifications are made to the engine-sim source checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    source, build, out = (v.resolve() for v in [a.source, a.build, a.out])
    cache = (build / "CMakeCache.txt").read_text()
    home = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.MULTILINE)
    if not home or Path(home[1]).resolve() != source:
        p.error("Native build belongs to a different source tree")
    # Make stale libraries a build error instead of linking changed declarations
    # against a previous class layout. Only build artifacts are modified.
    subprocess.run(["cmake", "--build", str(build), "--target", "engine-sim-script-interpreter"], check=True)
    out.mkdir(parents=True, exist_ok=False)
    for folder in ["include", "scripting/include"]:
        shutil.copytree(source / folder, out / folder)
    (out / "dependencies").symlink_to(source / "dependencies", target_is_directory=True)
    classes = {"engine": "Engine", "function": "Function", "camshaft": "Camshaft",
               "cylinder_head": "CylinderHead", "intake": "Intake", "exhaust_system": "ExhaustSystem",
               "fuel": "Fuel", "ignition_module": "IgnitionModule", "direct_throttle_linkage": "DirectThrottleLinkage"}
    patches = {}
    for file, name in classes.items():
        path = out / "include" / (file + ".h")
        before = path.read_text()
        after, count = re.subn(r"(class " + name + r"\b[^;{]*\{)",
                              r"\1\n    friend struct CrankwaveResolvedGraph;", before)
        if count != 1:
            raise RuntimeError(f"Unexpected native header declaration: {path}")
        path.write_text(after)
        patches[str(path.relative_to(out))] = {
            "source_sha256": hashlib.sha256(before.encode()).hexdigest(),
            "staged_sha256": hashlib.sha256(after.encode()).hexdigest(),
        }
    libraries = [build / "libengine-sim-script-interpreter.a",
                 build / "dependencies/submodules/piranha/libpiranha.a", build / "libengine-sim.a",
                 build / "dependencies/submodules/simple-2d-constraint-solver/libsimple-2d-constraint-solver.a"]
    cpp = Path(__file__).with_name("resolved_graph.cpp")
    command = ["c++", "-std=c++17", "-O2", "-DNDEBUG", "-DATG_ENGINE_SIM_PIRANHA_ENABLED",
               "-I", str(out / "include"), "-I", str(out / "scripting/include"),
               "-I", str(source / "dependencies/submodules"), str(cpp),
               *map(str, libraries), "-o", str(out / "resolved-graph")]
    subprocess.run(command, check=True)
    manifest = {"headers": patches, "cpp_sha256": hashlib.sha256(cpp.read_bytes()).hexdigest(),
                "linked_libraries": {str(v): hashlib.sha256(v.read_bytes()).hexdigest() for v in libraries},
                "binary_sha256": hashlib.sha256((out / "resolved-graph").read_bytes()).hexdigest()}
    (out / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(out / "resolved-graph")


if __name__ == "__main__":
    main()
