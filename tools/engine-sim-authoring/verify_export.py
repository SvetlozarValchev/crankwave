#!/usr/bin/env python3
"""Integration checks using the real MR compiler and the pilot I4 fixture.

Checks equivalence, sensitivity, deterministic output and fail-closed behavior.
No simulation, renderer stubs, source checkout edits or existing outputs reused.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixture", type=Path, required=True)
    p.add_argument("--adapter", type=Path, required=True)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    root = a.out.resolve(); root.mkdir(parents=True, exist_ok=False)
    adapter, library = a.adapter.resolve(), a.library.resolve()
    source = a.fixture.read_text()
    checks = []

    def check(name, passed, **evidence):
        checks.append(dict(name=name, passed=bool(passed), **evidence))

    def replace(old, new):
        if source.count(old) != 1:
            raise ValueError("Expected the unmodified pilot I4 fixture: " + old)
        return source.replace(old, new)

    def resolve(name, mr):
        work = root / name; work.mkdir()
        (work / "es").symlink_to(library, target_is_directory=True)
        script = work / "main.mr"; script.write_text(mr)
        graph = work / "graph.json"
        r = subprocess.run([str(adapter), str(script), str(graph)], cwd=work, capture_output=True, text=True, timeout=60)
        (work / "compiler.log").write_text(r.stdout + r.stderr)
        return r, graph

    baseline_r, baseline = resolve("baseline", source)
    if baseline_r.returncode:
        raise RuntimeError(baseline_r.stderr)
    native = json.loads(baseline.read_text())
    r, repeated = resolve("repeat", source)
    check("native graph byte determinism", r.returncode == 0 and baseline.read_bytes() == repeated.read_bytes(), sha256=sha(baseline))

    r, equivalent = resolve("expression", replace("label bore(86.0 * units.mm)", "label bore((43.0 + 43.0) * units.mm)"))
    check("native MR arithmetic equivalence", r.returncode == 0 and baseline.read_bytes() == equivalent.read_bytes())

    r, changed = resolve("changed-bore", replace("label bore(86.0 * units.mm)", "label bore(87.0 * units.mm)"))
    changed_graph = json.loads(changed.read_text()) if r.returncode == 0 else {}
    check("native MR parameter sensitivity", r.returncode == 0 and changed_graph["banks"][0]["bore_m"] != native["banks"][0]["bore_m"],
          original_bore=native["banks"][0]["bore_m"], changed_bore=changed_graph.get("banks", [{}])[0].get("bore_m"))

    for name, field in [("lbv", "lbv_multiplier: 1.2"), ("scheduled-two-stage", "auto_ignition_temperature: 700.0, premixed_burn_fraction: 0.2, premixed_flame_speed_multiplier: 2.5, diffusion_flame_speed_multiplier: 0.65")]:
        r, graph = resolve(name, replace("fuel: fuel(", "fuel: fuel(\n            " + field + ","))
        resolved = json.loads(graph.read_text()) if r.returncode == 0 else {}
        check(name + " resolved explicitly", r.returncode == 0 and
              (resolved['fuel']['lbv_multiplier'] == 1.2 if name == 'lbv' else
               resolved['fuel']['source_two_stage'] == dict(auto_ignition_temperature_k=700,
                   premixed_burn_fraction_01=.2, premixed_flame_speed_multiplier=2.5,
                   diffusion_flame_speed_multiplier=.65)), message=r.stderr.strip())
        if r.returncode == 0:
            import importlib.util
            spec = importlib.util.spec_from_file_location('mr_export', Path(__file__).with_name('export.py'))
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            target = graph.parent/'lowered'; target.mkdir()
            module.lower(resolved, target, 'extension-test', graph.parent, True)
            combustion = json.loads((target/'engine.json').read_text())['engine']['fuels'][0]['combustion']
            check(name + ' lowered without losing coefficients',
                  combustion.get('lbv_multiplier') == 1.2 if name == 'lbv' else
                  combustion.get('source_two_stage') == resolved['fuel']['source_two_stage'])

    r, graph = resolve('invalid-lbv', replace('fuel: fuel(', 'fuel: fuel(\n            lbv_multiplier: -1.0,'))
    check('invalid multiplier rejected', r.returncode != 0 and not graph.exists())

    r, graph = resolve("malformed", 'import "engine_sim.mr"\nthis is not valid MR !!!\n')
    check("malformed MR rejected", r.returncode != 0 and not graph.exists() and "MR compilation failed" in r.stderr)

    exporter = Path(__file__).with_name("export.py").resolve()
    def export(name, diagnostic):
        destination = root / name
        command = [sys.executable, str(exporter), "--script", str(a.fixture.resolve()), "--adapter", str(adapter),
                   "--library", str(library), "--id", "export-test-i4", "--out", str(destination)]
        if diagnostic:
            command.append("--diagnostic-loss-policy")
        r = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (root / (name + ".log")).write_text(r.stdout + r.stderr)
        return r, destination

    r, strict = export("strict", False)
    report = json.loads((strict / "import-report.json").read_text())
    check("strict import rejects absent native loss policy", r.returncode == 2 and not (strict / "engine.json").exists()
          and report["code"] == "native-aggregate-loss-policy-not-representable", exit_code=r.returncode)

    r, diagnostic = export("diagnostic", True)
    r2, diagnostic2 = export("diagnostic-repeat", True)
    if r.returncode or r2.returncode:
        raise RuntimeError(r.stderr + r2.stderr)
    for name in ["resolved-graph.json", "engine.json", "scenario.json", "accessory-configuration.json", "import-report.json"]:
        check(name + " deterministic", (diagnostic / name).read_bytes() == (diagnostic2 / name).read_bytes())
    imported = json.loads((diagnostic / "engine.json").read_text())
    source_irs = {sha((diagnostic / "native-work" / v["ir_path"]).resolve()) for v in native["exhausts"]}
    for asset in imported["presentation"]["assets"]:
        copied = diagnostic / asset["uri"]
        check("IR copied without byte changes", sha(copied) == asset["sha256"] and sha(copied) in source_irs, sha256=sha(copied))
    # Imported evidence must stay marked as diagnostic even after valid compile.
    report = json.loads((diagnostic / "import-report.json").read_text())
    check("no parity or carrier claim", report["parity_status"].startswith("unverified") and not list(diagnostic.glob("*.crankwave")))

    result = dict(passed=all(c["passed"] for c in checks), checks=checks, fixture_sha256=sha(a.fixture), adapter_sha256=sha(adapter))
    (root / "verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
