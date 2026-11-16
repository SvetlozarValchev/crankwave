#!/usr/bin/env python3
"""Generate the UAZ-469 exhaust filter from a synthetic response and band EQ.

This is an artistic acoustic model, not a measured UAZ impulse response.
Broad pulse coloration and a short, ringing metallic tail suggest an exterior
listening position. Smoothed reference magnitude measurements guide the EQ;
no recorded samples or reference phase are included. Default generation needs
NumPy; --base-only uses just Python's standard library.
"""

import argparse
import json
import math
from pathlib import Path
import struct
import wave


def generate():
    rate = 44100
    count = 4096  # 93 ms, below the synthesizer's 10,000-tap limit.
    impulse = [0.0] * count
    impulse[0] = 0.85
    # Hz, decay time, strength. All values are original sound-design choices.
    for frequency, decay, strength in ((240, 0.0020, 0.045),
                                       (500, 0.0030, 0.090),
                                       (920, 0.0048, 0.060),
                                       (1550, 0.0050, 0.016),
                                       (2250, 0.0045, 0.009),
                                       (3150, 0.0040, 0.007)):
        # Derivative of an exponentially damped sine: each mode has zero
        # continuous-time DC response, so adding metallic detail does not
        # also build up a long bass drone. Broad low/mid modes avoid a
        # dominant tube note. The stronger, inharmonic upper modes give the
        # pulse a short metal ring without increasing broadband attack gain.
        # Keep these modes broad and modest: the earlier 11 ms / 0.034 mode
        # at 1,550 Hz produced an exaggerated narrow tone in the reference
        # comparison even after the broad-band EQ was close.
        omega = 2 * math.pi * frequency
        for i in range(count):
            time = i / rate
            impulse[i] += strength * math.exp(-time / decay) * (
                math.cos(omega * time) - math.sin(omega * time) / (omega * decay))
    for cutoff in (3800, 5600):
        alpha = 1 - math.exp(-2 * math.pi * cutoff / rate)
        state = 0.0
        for i, sample in enumerate(impulse):
            state += alpha * (sample - state)
            impulse[i] = state
    # Quiet, dense reflections above 650 Hz preserve a sense of space without
    # repeating whole bass pulses. A fixed integer generator makes this
    # synthesized IR reproducible without a sampled recording or random file.
    noise_state = 469
    low = high = 0.0
    low_alpha = 1 - math.exp(-2 * math.pi * 2800 / rate)
    high_alpha = 1 - math.exp(-2 * math.pi * 650 / rate)
    onset = round(0.012 * rate)
    for i in range(onset, count):
        noise_state = (1664525 * noise_state + 1013904223) & 0xffffffff
        sample = noise_state / 2147483648.0 - 1.0
        low += low_alpha * (sample - low)
        high += high_alpha * (low - high)
        time = (i - onset) / rate
        envelope = (1 - math.exp(-time / 0.004)) * math.exp(-time / 0.025)
        impulse[i] += 0.025 * envelope * (low - high)
    # Reduce the close-tailpipe bass emphasis while retaining the firing beat.
    alpha = 1 - math.exp(-2 * math.pi * 120 / rate)
    state = 0.0
    fade_samples = round(0.015 * rate)
    for i, sample in enumerate(impulse):
        state += alpha * (sample - state)
        impulse[i] = sample - state
        # Smooth the final 15 ms to zero.
        if i >= count - fade_samples:
            phase = (i - count + fade_samples) / (fade_samples - 1)
            impulse[i] *= 0.5 * (1 + math.cos(math.pi * phase))
    scale = 30000 / max(abs(sample) for sample in impulse)
    return rate, struct.pack(f"<{count}h", *(round(x * scale) for x in impulse))


def apply_equalization(rate, pcm, profile):
    """Apply measured band gains to our synthetic response, using NumPy.

    A minimum-phase realization keeps the response causal. Neither phase nor
    PCM from the reference recording enters this filter.
    """
    import numpy as np

    if profile["sample_rate"] != rate:
        raise ValueError("EQ profile has an incompatible sample rate")
    centers = np.asarray(profile["frequencies_hz"], dtype=float)
    gains = np.asarray(profile["gain_db"], dtype=float)
    if (centers.shape != gains.shape or centers.ndim != 1 or len(centers) < 2
            or not np.all(np.isfinite(centers)) or not np.all(np.isfinite(gains))
            or np.any(np.diff(centers) <= 0) or centers[0] <= 0
            or np.max(abs(gains)) > 36):
        raise ValueError("Invalid EQ profile")
    fft_size = 32768
    original = np.frombuffer(pcm, dtype="<i2").astype(float) / 32768
    frequency = np.fft.rfftfreq(fft_size, 1 / rate)
    gain_db = np.interp(np.log(np.maximum(frequency, centers[0])),
                        np.log(centers), gains)
    # Roll off the unmeasured top octave rather than amplifying codec hiss.
    gain_db -= 18 * np.maximum(0, np.log2(np.maximum(frequency, 8000) / 8000))
    magnitude = abs(np.fft.rfft(original, fft_size)) * 10 ** (gain_db / 20)
    cepstrum = np.fft.irfft(np.log(np.maximum(magnitude, 1e-10)), fft_size)
    causal = np.zeros(fft_size)
    causal[0] = cepstrum[0]
    causal[1:fft_size // 2] = 2 * cepstrum[1:fft_size // 2]
    causal[fft_size // 2] = cepstrum[fft_size // 2]
    response = np.fft.irfft(np.exp(np.fft.rfft(causal)), fft_size)[:6144].copy()
    # A short, positive, unit-sum kernel rounds the leading edge instead of
    # adding another resonant filter. It preserves the bass and firing cadence
    # while progressively reducing sharp upper harmonics. All taps are causal.
    smoothing_ms = float(profile.get("attack_smoothing_ms", 0))
    if not math.isfinite(smoothing_ms) or not 0 <= smoothing_ms <= 1:
        raise ValueError("Attack smoothing must be between 0 and 1 ms")
    if smoothing_ms > 0:
        taps = max(3, round(smoothing_ms * rate / 1000) | 1)
        kernel = np.hanning(taps)
        kernel /= kernel.sum()
        response = np.convolve(response, kernel)[:len(response)]
    response[-1024:] *= 0.5 * (1 + np.cos(np.linspace(0, math.pi, 1024)))
    peak = float(np.max(abs(response)))
    if not math.isfinite(peak) or peak <= 0:
        raise ValueError("EQ produced an invalid impulse response")
    compensation = peak / (30000 / 32768)
    output = np.rint(response * (30000 / peak)).astype("<i2").tobytes()
    return output, compensation


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=root / (
        "assets/sound-library/uaz469/uaz469_synthetic_exhaust.wav"))
    parser.add_argument("--eq-profile", type=Path, default=root / (
        "assets/sound-library/uaz469/reference_eq.json"))
    parser.add_argument("--base-only", action="store_true",
                        help="Generate the uncorrected synthetic response")
    args = parser.parse_args()
    rate, pcm = generate()
    if not args.base_only:
        profile = json.loads(args.eq_profile.read_text())
        pcm, compensation = apply_equalization(rate, pcm, profile)
        print(f"EQ normalization compensation: {compensation:.9g}")
        print(f"Suggested impulse_response.volume: {0.004 * compensation:.9g}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.output), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(pcm)
    print(args.output)


if __name__ == "__main__":
    main()
