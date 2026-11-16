#!/usr/bin/env python3
"""Create paired listening WAVs and RPM plots from a native/Crankwave comparison.

Requires numpy, scipy, matplotlib. Only resampling and one fixed gain per complete
clip are applied. Originals remain unchanged; no EQ, denoising or time warping.
"""
import argparse
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly


def listening(source, destination):
    rate, samples = wavfile.read(source)
    if samples.ndim != 1:
        raise ValueError("Expected mono recording")
    if samples.dtype.kind == "i":
        x = samples.astype(np.float64) / (2 ** (samples.dtype.itemsize * 8 - 1))
    elif samples.dtype.kind == "f":
        x = samples.astype(np.float64)
    else:
        raise ValueError("Unsupported PCM encoding")
    divisor = math.gcd(int(rate), 48000)
    x = resample_poly(x, 48000 // divisor, int(rate) // divisor)[:27 * 48000]
    rms = float(np.sqrt(np.mean(x[2*48000:23*48000] ** 2)))
    peak = float(np.max(np.abs(x)))
    gain = min(.15 / rms, .94 / peak) if rms and peak else 1
    wavfile.write(destination, 48000, np.rint(x * gain * 32767).astype(np.int16))
    return dict(source=str(source), output=destination.name, gain=gain, original_decoded_peak=peak,
                running_rms_before_gain=rms, processing="48 kHz resampling and one fixed gain; no timbre edits")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--comparison", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    reports = json.loads((a.comparison / "comparison.json").read_text())
    fig, axes = plt.subplots(len(reports), 1, figsize=(12, 8), sharex=True, constrained_layout=True)
    axes = np.atleast_1d(axes)
    clips = []
    for ax, report in zip(np.atleast_1d(axes), reports, strict=True):
        name = report["engine"]; root = a.comparison / name
        clips.append(dict(engine=name, origin="native engine-sim",
                          **listening(root / "native/listen.wav", a.out / (name + "-engine-sim.wav"))))
        clips.append(dict(engine=name, origin="Crankwave diagnostic import",
                          **listening(root / "crankwave/audio/master.engine.audition.wav", a.out / (name + "-crankwave.wav"))))
        es = [json.loads(line) for line in (root / "native/telemetry.jsonl").read_text().splitlines()][1:]
        cw = [json.loads(line) for line in (root / "crankwave/telemetry/engine-telemetry.v1.ndjson").read_text().splitlines()]
        cw = [r for r in cw if r["record_type"] == "block"]
        ax.plot([r["simulationSeconds"] for r in es], [r["rpm"] for r in es], label="engine-sim", color="#235abc", lw=1)
        ax.plot([r["endpoint_scenario_time_s"] for r in cw], [r["telemetry"]["engine"]["engine_speed_rpm"] for r in cw],
                label="Crankwave import", color="#dc671c", lw=1, alpha=.85)
        ax.set_title(name.replace("-", " ").title(), loc="left")
        ax.set_ylabel("RPM")
        ax.set_xlim(0, 27)
        ax.grid(alpha=.18)
        for boundary in [2, 7, 10, 14, 18, 23]:
            ax.axvline(boundary, color="#888", alpha=.25, lw=.6)
    axes[0].legend(loc="upper left", ncol=2)
    axes[-1].set_xlabel("Seconds · crank 0–2 · idle 2–7 · rev 7–10 · release 10–14 · full pedal 14–18 · release 18–23 · off 23–27")
    fig.suptitle("Identical 20 kHz control schedule · parameters imported without engine tuning\nBackend behavior is not yet equivalent", fontsize=13)
    fig.savefig(a.out / "rpm-comparison.png", dpi=160)
    plt.close(fig)
    (a.out / "listening-manifest.json").write_text(json.dumps(dict(
        clips=clips, status="Diagnostic comparison, not approved parity or a baked runtime carrier",
        recorded_starter_overlay=False, source_profiles="Both backends at 20 kHz physics; original authored tuning retained"), indent=2) + "\n")
    print(a.out)


if __name__ == "__main__":
    main()
