# UAZ-469: 3:00 reference variant

Engine: `assets/engines/uaz469_reference_3m.mr`.
Standalone entry: `assets/uaz469_reference_3m_main.mr`.
The display name ends in `(3:00 Reference)`.

This separate variant targets [the supplied video at 3:00](https://www.youtube.com/watch?v=GDxEeeixRAY&t=180s).
The preserved `uaz469_cleanroom.mr`, its WAV/EQ, the default entry, and
Candidate 01 are unchanged. To select this experiment in the desktop app,
replace the engine import in `assets/main.mr` with
`import "engines/uaz469_reference_3m.mr"` and reload.

## Comparison and changes

Peaks around 73–74 Hz and their multiples imply approximately 2,200 rpm
for this four-stroke inline four. This is an audio inference, not tachometer
data. The comparison uses a 2,200 rpm held point after cranking and warmup;
the actual throttle/load in the recording is unknown. The physical engine
and its 764 rpm free idle remain the same as the preserved version.

The independent EQ matches the low firing harmonics and smoothed upper
bands in video seconds 180–184, then checks seconds 184–186. It restores
higher-harmonic body and broad metallic energy while keeping the softer
excitation: derivative gain 0.00012, noise 0.08, and jitter 0.010. Broad
synthetic resonances and 0.28 ms attack smoothing avoid the earlier strong
1,550 Hz ring. This remains an exhaust-driven acoustic approximation, with
no separate engine-block or chassis sound source.

The WAV is fully synthetic: reference samples, phase, and loops are not
embedded. Reference audio is kept outside the assets. The EQ contains only
frequency/gain measurements and subsequent voicing adjustments.

| Normalized comparison | Preserved engine | New variant |
| --- | ---: | ---: |
| Broad-band RMS difference, 20–4,000 Hz, 180–184 s | 11.63 dB | 0.85 dB |
| Broad-band RMS difference, 20–4,000 Hz, 184–186 s | 11.17 dB | 0.83 dB |
| Third-octave RMS difference, 25–4,000 Hz, 180–184 s | 13.00 dB | 5.01 dB |
| Third-octave RMS difference, 25–4,000 Hz, 184–186 s | 12.75 dB | 4.82 dB |

Each excerpt is normalized independently over 20–8,000 Hz. The 1.2–3.8 kHz
pulse-envelope variation is 0.30 versus 0.26 in the fitting excerpt, and
0.30 versus 0.22 in the check excerpt. The original engine was already close
on this envelope metric; the main improvement is the frequency balance.
There is still a substantial difference above 4 kHz: the 4–8 kHz band is
about -27 dB relative to total energy in this variant, versus -11 dB in the
recording. Narrow spectral detail, dynamic response, microphone coloration,
and background sources also differ. These are numerical comparisons, not
direct listening or a verified 1:1 perceptual match.

## Reproduce

The shared generator is unchanged. Generate this variant's WAV explicitly:

```sh
python3 tools/generate_uaz469_ir.py \
  --eq-profile assets/sound-library/uaz469/reference_3m/reference_eq.json \
  --output assets/sound-library/uaz469/reference_3m/uaz469_synthetic_exhaust.wav
```

The generator prints base IR volume 0.0276587908. This variant adds the
profile's 6 dB preview gain, giving `impulse_response.volume: 0.05518654`.
The 6,144-sample WAV regenerates byte-for-byte with the recorded generator.

Render the saved comparison schedule with the production native driver:

```sh
mkdir -p build-uaz469-reference3m-preview
build-web-native/engine-sim-web-parity-native \
  --script assets/uaz469_reference_3m_main.mr --seed 469 \
  --pcm build-uaz469-reference3m-preview/steady.pcm \
  --telemetry build-uaz469-reference3m-preview/steady.jsonl \
  < assets/sound-library/uaz469/reference_3m/comparison_schedule.txt
python3 tools/analyze_uaz469_reference.py \
  --reference build-uaz469-reference/reference-175-200.wav --reference-offset 175 \
  --simulation build-uaz469-reference3m-preview/steady.pcm \
  --window fit 180 184 12 16 --window check 184 186 16 18 \
  --output build-uaz469-reference3m-preview/analysis
```

The reference WAV must cover video seconds 175–200 at its original timing.
The simulator's comparison windows are 12–16 and 16–18 seconds. The saved
six-second listening preview is simulation seconds 12–18; it contains only
the generated engine sound.

Functional checks are separate from spectral comparison:

```sh
python3 tools/render_uaz469_preview.py \
  --script assets/uaz469_reference_3m_main.mr \
  --output build-uaz469-reference3m-preview
```

Cranking, idle, rev, recovery, shutdown, loaded torque, sustained overrun,
audio duration, and peak checks passed with seeds 469 and 470. The seed-469
free idle is 764 rpm, gentle-rev peak 2,215 rpm, and loaded output near
4,000 rpm 54.3 kW. All tested PCM peaks stayed below the 32,700 validation
threshold. The manifest records the comparison metrics, render results,
new-file hashes, and hashes of the preserved files.

The WebAssembly preload list includes the new entry, engine, and WAV.
Browser artifacts require rebuilding; the existing workspace's Emscripten
toolchain remains unavailable. Native loading and rendering were validated.
