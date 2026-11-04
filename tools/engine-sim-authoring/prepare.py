#!/usr/bin/env python3
"""Prepare the real engine-sim API/compiler toolkit without copying engines.

The source must be the local engine-sim fork with the native web C ABI and exact
limiter event support. This does not claim compatibility with an arbitrary
upstream checkout. The source repository is read-only throughout the build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    args = parser.parse_args()
    source, build, toolkit = (p.resolve() for p in [args.source, args.build, args.toolkit])
    if toolkit.exists():
        parser.error("toolkit destination must be new")
    subprocess.run([
        "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DENGINE_SIM_BUILD_APP=OFF",
        "-DENGINE_SIM_BUILD_TESTS=OFF", "-DENGINE_SIM_BUILD_BAKER=OFF",
        "-DDISCORD_ENABLED=OFF", "-DDTV=OFF", "-DPIRANHA_ENABLED=ON",
        "-DENGINE_SIM_NATURAL_GAS_RESONANCE=OFF",
    ], check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "engine-sim-web-parity-native",
                    "--parallel", "6"], check=True)
    toolkit.mkdir(parents=True)
    shutil.copytree(source / "es", toolkit / "es")
    implementation = toolkit / 'native-api'
    implementation.mkdir()
    for name in ['intake.cpp', 'fuel.cpp', 'combustion_chamber.cpp', 'camshaft.cpp',
                 'cylinder_bank.cpp', 'crankshaft.cpp', 'ignition_module.cpp',
                 'direct_throttle_linkage.cpp']:
        shutil.copy2(source / 'src' / name, implementation / name)
    shutil.copy2(build / "engine-sim-web-parity-native", toolkit / "engine-sim-native")
    shutil.copy2(Path(__file__).with_name("audition.py"), toolkit / "audition.py")
    subprocess.run([
        "c++", "-std=c++17", "-O2", "-DNDEBUG", "-I", str(source / "include"),
        str(Path(__file__).with_name("limiter_probe.cpp")),
        str(build / "libengine-sim-web-core.a"), str(build / "libengine-sim-script-interpreter.a"),
        str(build / "dependencies/submodules/piranha/libpiranha.a"),
        str(build / "libengine-sim.a"),
        str(build / "dependencies/submodules/simple-2d-constraint-solver/libsimple-2d-constraint-solver.a"),
        "-o", str(toolkit / "limiter-probe"),
    ], check=True)
    def git(*command):
        return subprocess.check_output(["git", "-C", str(source), *command], text=True).strip()
    manifest = {
        "origin": "local modified engine-sim fork, not pristine upstream",
        "source_head": git("rev-parse", "HEAD"),
        "source_status": git("status", "--short"),
        "source_diff_sha256": hashlib.sha256(git("diff", "HEAD").encode()).hexdigest(),
        "submodules": git("submodule", "status", "--recursive"),
        "toolkit_files": {str(p.relative_to(toolkit)): sha(p) for p in sorted(toolkit.rglob("*")) if p.is_file()},
    }
    (toolkit / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(toolkit)


if __name__ == "__main__":
    main()
