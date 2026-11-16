# Accepted idle and coasting baseline

The user accepted the version 10 audition as “sounds much better” and explicitly
requested a baseline commit before investigating the gargle during revving.
This checkpoint includes the smooth valve contacts, quieter idle, coasting pulse
balance and flow-driven coasting texture. Rev character remains open for tuning.
The earlier version 5 checkpoint remains documented in [BASELINE.md](BASELINE.md).

This baseline is named `baseline/diesel-flowing-coast`. Further rev tuning belongs
on `tuning/diesel-rev-character`.

| Item | SHA-256 |
|---|---|
| engine_definition | `69ff2578c04e08b4baa36fb54f0caa5e1b24598fb358184b43e62539918609f1` |
| runtime | `482b1f881919da70def27890d2e1326183d3b359dbcbc64cf6f0f10f39dd5283` |
| carrier | `e1e7240359ebe6eb95661d02b3559cfc14b04f40888034ba748955dcca5116cc` |
| idle_revs_coast_48k | `de34378696c3528b4e0ca7b697fa048b733f615d5089f11d142c05f186a7b5e5` |
| gear_change_48k | `98c47790228c14501fb5750a7143bd7466a7f5cf1433d1a22e307ddb9eee7883` |

The accepted carrier and listening WAVs remain in the ignored local directory
`artifacts/diesel-sound/ural-flowing-coast/`. `render.mjs`, `listening.py`,
`verification.json` and `band-measurements.json` there document reproduction.
The main 18-second audition has idle 0–4 s, acceleration 4–8 s, hold 8–9 s,
coast 9–15 s and final idle 15–18 s. The six-second gear-change audition has
coast at 2–3.5 s. No loudness normalization was applied.

Rebuild using [README.md](README.md), choosing new output paths. The carrier binds
the exact baker executable digest, so a new build may have a different package
hash. Downloaded reference audio is not required to bake or play the engine.

Validation: 142 runtime tests (36 diesel-specific), native definition/bake tests,
Ural native bake/package-to-JavaScript integration, both authoring schemas, and
the browser Worker/DSP path with its existing audio-device test double. Previous
version 9 WAVs replay byte-for-byte. The accepted idle and loaded sections are
sample-identical between versions 9 and 10; physical captures are unchanged.
User acceptance establishes a listening baseline, not a measured Ural match.
