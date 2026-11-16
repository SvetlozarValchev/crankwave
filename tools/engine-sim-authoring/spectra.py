#!/usr/bin/env python3
"""Plot identical-scale spectrograms of the level-matched listening pairs.

These compare a complete backend result, including different RPM trajectories.
They are diagnostic, not a matched-RPM acoustic error metric or listening score.
"""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile
from scipy.signal import spectrogram


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--review", type=Path, required=True)
    a = p.parse_args()
    manifest = json.loads((a.review / "listening-manifest.json").read_text())
    cases = list(dict.fromkeys(c["engine"] for c in manifest["clips"]))
    fig, axes = plt.subplots(len(cases), 2, figsize=(13, 9), squeeze=False,
                             sharex=True, sharey=True, constrained_layout=True)
    for row, case in enumerate(cases):
        for col, backend in enumerate(["engine-sim", "crankwave"]):
            rate, pcm = wavfile.read(a.review / f"{case}-{backend}.wav")
            assert rate == 48000 and pcm.dtype == np.int16
            frequency, time, power = spectrogram(pcm.astype(np.float64) / 32768, rate,
                                                nperseg=2048, noverlap=1536, detrend=False)
            selected = (frequency >= 40) & (frequency <= 8000)
            ax = axes[row, col]
            m = ax.pcolormesh(time, frequency[selected], 10 * np.log10(np.maximum(power[selected], 1e-14)),
                              vmin=-95, vmax=-25, cmap="magma", shading="auto", rasterized=True)
            ax.set_yscale("log"); ax.set_ylim(40, 8000); ax.set_xlim(0, 27)
            ax.set_yticks([50, 100, 200, 500, 1000, 2000, 4000, 8000],
                         ["50", "100", "200", "500", "1k", "2k", "4k", "8k"])
            ax.set_title(case.replace("-", " ").title() + " · " + backend, loc="left")
            if col == 0:
                ax.set_ylabel("Hz")
            if row == len(cases) - 1:
                ax.set_xlabel("Seconds")
    fig.colorbar(m, ax=axes, label="dBFS / Hz", shrink=.8)
    fig.suptitle("Engine-sim → Crankwave diagnostic imports · identical spectrogram scale\n"
                 "One fixed gain per clip; RPM trajectories differ; no EQ or time alignment", fontsize=13)
    fig.savefig(a.review / "spectrogram-comparison.png", dpi=160)
    plt.close(fig)
    print(a.review / "spectrogram-comparison.png")


if __name__ == "__main__":
    main()
