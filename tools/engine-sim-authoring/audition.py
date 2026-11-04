#!/usr/bin/env python3
"""Fixed, observable native engine-sim authoring probe (no audio synthesis here)."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import time
import wave


def scenario(mode):
    if mode == "validate":
        return [("compile-and-step", 0.04, 0.0, 0, 0)]
    return [
        ("starter", 2, 0.0, 1, 1),
        ("idle", 5, 0.0, 1, 0),
        ("gentle-rev", 3, 0.12, 1, 0),
        ("rundown", 4, 0.0, 1, 0),
        ("full-throttle", 4, 1.0, 1, 0),
        ("coast", 5, 0.0, 1, 0),
        ("shutdown", 4, 0.0, 0, 0),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["validate", "audition"])
    parser.add_argument("script", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--native", type=Path, default=Path(__file__).with_name("engine-sim-native"))
    parser.add_argument("--frequency", type=int, default=10000)
    args = parser.parse_args()
    if args.frequency <= 0 or args.frequency % 50:
        parser.error("frequency must be a positive multiple of 50")
    script = args.script.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    # Keep every input attempt, including failed compilation, independently reviewable.
    source = script.read_bytes()
    (out / "submitted.mr").write_bytes(source)
    commands, stages = [], []
    elapsed = 0
    for name, seconds, throttle, ignition, starter in scenario(args.mode):
        stages.append(dict(name=name, begin=elapsed, end=elapsed + seconds))
        for _ in range(round(seconds * 50)):
            commands.append(f"{len(commands)} {args.frequency // 50} {throttle} {ignition} {starter} 0 0 0 0")
        elapsed += seconds
    controls = "\n".join(commands) + "\n"
    (out / "controls.txt").write_text(controls)
    started = time.monotonic()
    result = subprocess.run([
        str(args.native.resolve()), "--script", str(script), "--seed", "20261104",
        "--pcm", str(out / "native.pcm16"), "--telemetry", str(out / "telemetry.jsonl"),
    ], input=controls, text=True, capture_output=True, timeout=300)
    (out / "native.stdout.log").write_text(result.stdout)
    (out / "native.stderr.log").write_text(result.stderr)
    report = dict(mode=args.mode, source_sha256=hashlib.sha256(source).hexdigest(),
                  native_exit=result.returncode, wall_seconds=time.monotonic() - started,
                  scenario=stages, audio_origin="native engine-sim; not Crankwave",
                  limiter_detection="RPM only; no claim of ignition-cut event validation",
                  coast_kind="free-engine throttle release; not vehicle backdrive",
                  starter_kind="native cranking; no recorded starter overlay")
    report["acceptance"] = "Not evaluated here; use evaluate.py for behavioral acceptance and listen for sound quality"
    if result.returncode:
        report["simulation_succeeded"] = False
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print((result.stdout + result.stderr)[-18000:])
        print(json.dumps(report))
        return result.returncode
    rows = [json.loads(line) for line in (out / "telemetry.jsonl").read_text().splitlines()]
    metadata, samples = rows[0], rows[1:]
    if metadata["simulationFrequency"] != args.frequency:
        raise RuntimeError("Authored simulation frequency differs from the fixed test")
    report["metadata"] = metadata
    report["stages"] = []
    for stage in stages:
        selected = [row for row in samples
                    if stage["begin"] < row["simulationSeconds"] <= stage["end"] + 1e-6]
        rpm = [abs(row["rpm"]) for row in selected]
        tail = rpm[-min(len(rpm), 100):]
        report["stages"].append(dict(**stage, minimum_rpm=min(rpm), peak_rpm=max(rpm),
                                    final_rpm=rpm[-1], tail_median_rpm=statistics.median(tail)))
    pcm = array.array("h", (out / "native.pcm16").read_bytes())
    if sys.byteorder != "little":
        pcm.byteswap()
    peak = max(map(abs, pcm), default=0)
    report["audio"] = dict(frames=len(pcm), sample_rate=metadata["audioSampleRate"],
                           peak=peak / 32768, rms=math.sqrt(sum(x*x for x in pcm) / max(1, len(pcm))) / 32768,
                           clipped_samples=sum(abs(x) >= 32767 for x in pcm))
    # One fixed gain per complete listening file; raw PCM remains untouched.
    gain = min(16.0, 26000 / peak) if peak else 1
    report["audio"]["listening_gain"] = gain
    listening = array.array("h", (round(x * gain) for x in pcm))
    if sys.byteorder != "little":
        listening.byteswap()
    with wave.open(str(out / "listen.wav"), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(metadata["audioSampleRate"])
        wav.writeframes(listening.tobytes())
    report["simulation_succeeded"] = True
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
