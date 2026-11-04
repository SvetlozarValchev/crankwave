#!/usr/bin/env python3
"""Build a complete diagnostic responsive carrier from evaluated engine-sim MR.

Runs the existing native base baker (held, directional, lifecycle and recorded
starter), captures real limiter cuts, attaches them and verifies the final file.
Backend parity is not implied by a successful bake. No engine-specific retuning.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--script', type=Path)
    source.add_argument('--import-dir', type=Path)
    p.add_argument('--adapter', type=Path)
    p.add_argument('--library', type=Path)
    p.add_argument('--id')
    p.add_argument('--crankwave', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--diagnostic-loss-policy', action='store_true')
    a = p.parse_args()
    if not a.diagnostic_loss_policy:
        p.error('Native aggregate loss semantics are not yet representable; diagnostic builds require --diagnostic-loss-policy')
    if a.script and not all([a.adapter, a.library, a.id]):
        p.error('--script requires --adapter, --library and --id')
    root = a.out.resolve(); root.mkdir(parents=True, exist_ok=False)
    binary = str(a.crankwave.resolve())
    tools = Path(__file__).resolve().parent
    repo = tools.parent.parent
    report = dict(status='building', parity='unverified backend; diagnostic carrier', stages=[])

    def run(name, argv, timeout=1200):
        print(name, flush=True)
        with (root / (name + '.log')).open('w') as log:
            result = subprocess.run(list(map(str, argv)), cwd=repo, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        report['stages'].append(dict(stage=name, exit_code=result.returncode))
        write(root / 'build-report.json', report)
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}); see {root / (name + ".log")}')

    try:
        imported = root / 'import'
        if a.script:
            run('resolve-and-import', [sys.executable, tools/'export.py', '--script', a.script.resolve(), '--adapter', a.adapter.resolve(),
                '--library', a.library.resolve(), '--id', a.id, '--out', imported, '--diagnostic-loss-policy'])
        else:
            shutil.copytree(a.import_dir.resolve(), imported, ignore=shutil.ignore_patterns('native-work'))
        native = json.loads((imported / 'resolved-graph.json').read_text())
        engine = json.loads((imported / 'engine.json').read_text())
        imported_report = json.loads((imported / 'import-report.json').read_text())
        if (imported_report.get('engine_sha256') != digest(imported / 'engine.json') or
                imported_report.get('graph_sha256') != digest(imported / 'resolved-graph.json')):
            raise ValueError('Import differs from its exporter report; re-export MR instead of editing Crankwave JSON')
        if native['schema'] != 'crankwave/engine-sim-resolved-graph/v1':
            raise ValueError('Unsupported resolved graph schema')
        report['source_import_sha256'] = digest(imported / 'engine.json')
        # These two legacy bake entrypoints require literal RPM quantities.
        # 0.104719755 is the pinned native/CW unit conversion, not math.pi/30.
        conversions = []
        for name, value in [('redline', engine['engine']['limits']['redline']),
                            ('limiter', engine['engine']['ignition']['limiter']['activation_speed'])]:
            if value['unit'] == 'rpm':
                continue
            if value['unit'] != 'rad/s':
                raise ValueError('Unsupported angular unit for ' + name)
            original = value['value']
            value.update(value=original / .104719755, unit='rpm')
            if value['value'] * .104719755 != original:
                raise ValueError('RPM normalization would change binary64 value: ' + name)
            conversions.append(name)
        write(imported / 'engine.json', engine)
        report['rpm_normalizations_exact'] = conversions
        report['bake_engine_sha256'] = digest(imported / 'engine.json')
        report['source_graph_sha256'] = digest(imported / 'resolved-graph.json')
        report['rates'] = dict(base_bake_physics_hz=10000, limiter_capture_physics_hz=20000, presentation_hz=192000)
        run('base-bake', [binary, 'bake-crankwave', '--engine', imported/'engine.json', '--asset-root', imported,
                         '--output', root/'base.crankwave', '--result-format', 'json'])
        # Long WOT capture produces independent cut/recovery clips even on an
        # engine which only intermittently touches its limiter. No lowered limit.
        scenario = json.loads((imported / 'scenario.json').read_text())
        scenario['id'] = engine['engine']['identity']['id'] + '-limiter-capture'
        scenario['mode']['throttle_01']['points'] = [dict(time=dict(value=t,unit='s'),value=v) for t,v in [(0,0),(7,1),(77,0)]]
        scenario['events'][1]['time']['value'] = 82
        scenario['total_duration']['value'] = scenario['audible_duration']['value'] = 86
        for rate in ['physics','capture']:
            scenario['rates'][rate]['numerator'] = '20000'
        write(root/'limiter-scenario.json', scenario)
        run('limiter-render', [binary, 'render', '--engine', imported/'engine.json', '--scenario', root/'limiter-scenario.json',
            '--asset-root', imported, '--output-directory', root/'limiter-render', '--result-format', 'json'])
        run('attach-limiter', ['node', repo/'tools/bake-limiter-performance.mjs', root/'base.crankwave', imported/'engine.json',
            root/'limiter-render', root/'package', root/'candidate.crankwave', binary])
        run('verify-carrier', [binary, 'verify-crankwave', '--input', root/'candidate.crankwave', '--result-format', 'json'])
        manifest = json.loads((root/'package/runtime.json').read_text())
        required = ['lifecycle_package_path','shared_recorded_starter_package_path','limiter_package_path']
        for key in required:
            if not manifest.get(key) or not (root/'package'/manifest[key]).is_file():
                raise RuntimeError('Complete carrier is missing ' + key)
        run('runtime-lifecycle-test', ['node', repo/'web/tests/integration/responsive-carrier-lifecycle.integration.mjs', root/'candidate.crankwave'])
        (root/'candidate.crankwave').rename(root/'complete.crankwave')
        report.update(status='complete-diagnostic-carrier', complete_carrier_sha256=digest(root/'complete.crankwave'),
                      required_capabilities={key:manifest[key] for key in required}, authored_engine_tuning_changed=False)
        write(root/'build-report.json', report)
        print(root/'complete.crankwave')
    except Exception as error:
        report.update(status='failed', error=str(error))
        write(root/'build-report.json', report)
        raise


if __name__ == '__main__':
    main()
