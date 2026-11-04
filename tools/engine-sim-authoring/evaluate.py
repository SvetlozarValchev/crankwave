#!/usr/bin/env python3
"""Independently score completed trials; a successful render is not a good engine."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess
import sys

TARGETS = {"inline-four": (850, 6500), "crossplane-v8": (750, 5500), "uneven-v-twin": (950, 5200)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=list(TARGETS), action="append")
    args = parser.parse_args()
    names = args.case or list(TARGETS)
    for name in names:
        if not (args.runs / name / "run.json").exists():
            parser.error(f"Trial still running or missing: {name}")
    args.out.mkdir(parents=True, exist_ok=False)
    summaries = []
    for name in names:
        idle, limiter = TARGETS[name]
        root = args.runs.resolve() / name
        workspace = root / "workspace"
        run = json.loads((root / "run.json").read_text())
        attempts = sorted(workspace.glob("audition-*/report.json"))
        if not attempts:
            summaries.append(dict(case=name, run=run, accepted_behavior=False, reason="No native audition"))
            continue
        # The last completed audition is scored even if the agent claimed success
        # earlier. It must be exactly the submitted final source, with no edits.
        selected = attempts[-1]
        report = json.loads(selected.read_text())
        source_hash = hashlib.sha256((workspace / "main.mr").read_bytes()).hexdigest()
        stages = {stage["name"]: stage for stage in report.get("stages", [])}
        telemetry = [json.loads(line) for line in (selected.parent / "telemetry.jsonl").read_text().splitlines()]
        running = [row for row in telemetry if row.get("kind") == "sample"
                   and 3 <= row["simulationSeconds"] < 23]
        target = args.out.resolve() / name
        target.mkdir()
        events = []
        probe_exit = None
        if report.get("native_exit") == 0 and source_hash == report["source_sha256"]:
            command = [
                "bwrap", "--die-with-parent", "--unshare-net", "--unshare-pid",
                "--ro-bind", "/usr", "/usr", "--symlink", "usr/bin", "/bin",
                "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
                "--ro-bind", "/etc", "/etc", "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
                "--ro-bind", str(args.toolkit.resolve()), "/toolkit",
                "--ro-bind", str(workspace), "/workspace",
                "--bind", str(target), "/verification", "--chdir", "/workspace",
                "/toolkit/limiter-probe", "/workspace/main.mr",
                f"/workspace/{selected.parent.name}/controls.txt", "/verification/limiter-events.jsonl",
            ]
            result = subprocess.run(command, text=True, capture_output=True, timeout=300)
            probe_exit = result.returncode
            (target / "probe.log").write_text(result.stdout + result.stderr)
            if result.returncode == 0:
                events = [json.loads(line) for line in (target / "limiter-events.jsonl").read_text().splitlines()]
        counts = collections.Counter(event["kind"] for event in events)
        checks = dict(final_source_matches_audition=source_hash == report["source_sha256"],
                      native_simulation=report.get("native_exit") == 0,
                      no_stall_after_start=bool(running) and min(abs(row["rpm"]) for row in running) > 200,
                      idle_within_15_percent=abs(stages.get("idle", {}).get("tail_median_rpm", 0) - idle) <= 0.15 * idle,
                      revs_to_limiter_region=stages.get("full-throttle", {}).get("peak_rpm", 0) >= limiter * 0.95,
                      returns_toward_idle=0.5*idle <= stages.get("coast", {}).get("final_rpm", 0) <= 1.35*idle,
                      stops_on_key_off=stages.get("shutdown", {}).get("final_rpm", 1e9) < 30,
                      audible_nonzero=report.get("audio", {}).get("rms", 0) > 1e-5,
                      no_pcm_clipping=report.get("audio", {}).get("clipped_samples", 1) == 0,
                      actual_limiter_cut_and_recovery=counts["cut_begin"] > 0 and counts["cut_end"] == counts["cut_begin"])
        observable = []
        usage = None
        commands = 0
        for line in (root / "events.jsonl").read_text().splitlines():
            event = json.loads(line)
            item = event.get("item", {})
            if event["type"] == "item.completed" and item.get("type") == "agent_message":
                observable.append(item.get("text", ""))
            if event["type"] == "item.completed" and item.get("type") == "command_execution":
                commands += 1
            if event["type"] == "turn.completed":
                usage = event.get("usage")
        (target / "observable-statements.json").write_text(json.dumps(observable, indent=2) + "\n")
        summary = dict(case=name, run=run, accepted_behavior=all(checks.values()), subjective_acceptance="pending user listening",
                       checks=checks, full_auditions=len(attempts), compile_checks=len(list(workspace.glob("check-*/report.json"))),
                       source_sha256=source_hash, audio=str(selected.parent / "listen.wav"),
                       first_audition=json.loads(attempts[0].read_text()), final_audition=report,
                       limiter_events=dict(counts), probe_exit=probe_exit, completed_tool_commands=commands, usage=usage)
        (target / "evaluation.json").write_text(json.dumps(summary, indent=2) + "\n")
        summaries.append(summary)
        print(name, checks, flush=True)
    (args.out / "evaluation.json").write_text(json.dumps(summaries, indent=2) + "\n")
    return 0 if all(summary["accepted_behavior"] for summary in summaries) else 1


if __name__ == "__main__":
    sys.exit(main())
