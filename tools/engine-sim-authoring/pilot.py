#!/usr/bin/env python3
"""Run independent engine-sim authoring pilots in restricted filesystem views.

Requires Linux bubblewrap, the Codex CLI, and a prepared toolkit containing
engine-sim-native, es/, and audition.py. No engine examples or project tree are
mounted. Network remains available for Codex inference; web search is disabled.
Uses the existing Codex authentication without copying or printing credentials.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time


CASES = {
    "inline-four": "Naturally aspirated 2.0 L inline-four, 86 mm bore and stroke, compression 10:1, firing order 1-3-4-2, idle near 850 rpm, ignition limiter at 6500 rpm. A restrained production road engine with a clear four-cylinder rev character.",
    "crossplane-v8": "Naturally aspirated 90-degree crossplane V8, 101.6 mm bore, 88.4 mm stroke (5.7 L), compression 9:1, firing order 1-8-4-3-6-5-7-2, idle near 750 rpm, ignition limiter at 5500 rpm. Audible uneven bank pulses, two separate exhausts, modest road cam rather than extreme drag-racing lope.",
    "uneven-v-twin": "Naturally aspirated 45-degree shared-crankpin V-twin, 88.8 mm bore, 108 mm stroke (1.34 L), compression 8.5:1, firing intervals 315/405 degrees, idle near 950 rpm, ignition limiter at 5200 rpm. Distinct uneven motorcycle rhythm, mechanically plausible mild road tune.",
}

PROMPT = """Create a complete engine-sim engine in /workspace/main.mr from scratch.
Brief: {brief}

This is an independent authoring experiment. No engine examples are supplied.
The real engine-sim standard API library is in /toolkit/es and available via
import \"engine_sim.mr\". Read API declarations as needed. Do not access network,
external examples, any other project, or auth/config files. Do not spawn agents.
Use engine-sim syntax, physical components, ignition wiring and timing, cam and
flow curves, real engine-sim impulse responses. Do not synthesize audio yourself.
Choose sensible acoustic settings and exhaust lengths for the requested engine.
Set simulation_frequency: 10000. Include run(engine: ..., vehicle: ...,
transmission: ...) so the native simulator can create the complete session.
The API library includes impulse_response_library; its WAVs are already present.

Save first.mr before your first validation. Compile and validate with:
python3 /toolkit/audition.py validate /workspace/main.mr --out /workspace/check-01
Each --out directory must be new. To run the fixed 27-second native audio test:
python3 /toolkit/audition.py audition /workspace/main.mr --out /workspace/audition-01
This physically simulates 2s cranking, 5s zero-pedal idle, 3s at 12% pedal, 4s
release, 4s full pedal (limiter), 5s release, 4s ignition-off. Do not change the
test, wrapper or standard library. Fix your definition if it fails to compile,
stall-free idle, freely rev and return toward idle. Observe RPM and nonzero audio.
Native PCM clipping must be zero; reducing the listening WAV gain does not repair
clipping already present in the native PCM. Correct the authored sound settings.
Use at most 12 compile checks and 6 full auditions. Do not pretend to have heard
audio if no audio listening tool exists. Save brief factual decisions and known
limitations in decisions.json, and identify the final audition directory in
result.json. End with a concise result and observed checks. No hidden reasoning
transcript is requested; edits, tool calls and measurements are the experiment.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolkit", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--model", default="gpt-6-astra")
    parser.add_argument("--effort", default="xhigh")
    parser.add_argument("--timeout", default=900, type=int)
    parser.add_argument("--case", choices=list(CASES), action="append")
    parser.add_argument("--repair-from", type=Path,
                        help="Repair an existing independent trial's main.mr in a fresh session")
    args = parser.parse_args()
    if args.repair_from and len(args.case or []) != 1:
        parser.error("--repair-from requires exactly one --case")
    toolkit = args.toolkit.resolve()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    codex = Path(shutil.which("codex")).resolve()
    release = codex.parent.parent
    node_root = Path(shutil.which("node")).resolve().parent.parent
    auth_home = Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex")))
    if not (auth_home / "auth.json").is_file():
        raise RuntimeError("Existing Codex file authentication is required")
    if not (release / "codex-path").is_dir():
        raise RuntimeError("Expected standalone Codex distribution")

    def run(case):
        root = output / case
        root.mkdir(exist_ok=False)
        workspace = root / "workspace"
        workspace.mkdir()
        (workspace / "es").symlink_to("/toolkit/es", target_is_directory=True)
        state = root / "state"
        state.mkdir()
        prompt = PROMPT.format(brief=CASES[case])
        if args.repair_from:
            shutil.copy2(args.repair_from, workspace / "main.mr")
            prompt = prompt.replace("from scratch.", "by repairing the supplied definition.")
            prompt += """\nIndependent acceptance found clipped samples in the prior native render.
This is a repair pass, with the prior authored engine supplied as main.mr. Keep its
geometry, firing order and target behavior. Resolve native PCM clipping and verify
the result with the unchanged full audition. Save the input as first.mr before
edits. Sound gain/IR corrections are allowed; offline waveform repairs are not.
"""
        (root / "prompt.txt").write_text(prompt)
        command = [
            "bwrap", "--die-with-parent", "--new-session", "--unshare-pid",
            "--ro-bind", "/usr", "/usr", "--symlink", "usr/bin", "/bin",
            "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
            "--ro-bind", "/etc", "/etc", "--proc", "/proc", "--dev", "/dev",
            "--ro-bind", str(Path("/etc/resolv.conf").resolve()), str(Path("/etc/resolv.conf").resolve()),
            "--tmpfs", "/tmp", "--tmpfs", "/home",
            "--dir", str(Path.home()), "--bind", str(state), str(auth_home),
            "--ro-bind", str(auth_home / "auth.json"), str(auth_home / "auth.json"),
            "--ro-bind", str(release), str(release),
            "--ro-bind", str(node_root), "/opt/node",
            "--ro-bind", str(toolkit), "/toolkit",
            "--bind", str(workspace), "/workspace", "--chdir", "/workspace",
            "--setenv", "PATH", f"/opt/node/bin:{release}/bin:{release}/codex-path:/usr/bin:/bin",
            str(codex), "exec", "--ephemeral", "--ignore-user-config", "--ignore-rules",
            "--skip-git-repo-check", "--dangerously-bypass-approvals-and-sandbox",
            "--json", "--color", "never", "-C", "/workspace",
            "-m", args.model, "-c", f'model_reasoning_effort="{args.effort}"',
            "-c", 'web_search="disabled"', "-c", "features.multi_agent=false",
            "-o", "/workspace/final.txt", "-",
        ]
        started = time.monotonic()
        print(f"{case}: starting independent {args.model} session", flush=True)
        with (root / "events.jsonl").open("w") as events, (root / "stderr.log").open("w") as errors:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=events, stderr=errors,
                                       text=True, start_new_session=True)
            timed_out = False
            try:
                process.communicate(prompt, timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=15)
        report = dict(case=case, model=args.model, effort=args.effort, exit_code=process.returncode,
                      timed_out=timed_out, wall_seconds=time.monotonic()-started,
                      isolation="fresh Codex session; restricted filesystem; no project or engine examples",
                      network="available for inference; web search disabled; no network use instructed")
        (root / "run.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report), flush=True)
        return report

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        reports = list(pool.map(run, args.case or CASES))
    (output / "runs.json").write_text(json.dumps(reports, indent=2) + "\n")


if __name__ == "__main__":
    main()
