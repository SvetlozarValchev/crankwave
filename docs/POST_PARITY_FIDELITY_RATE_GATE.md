# Post-parity fidelity rate gate

Status: accepted by the user on 2026-08-03 as sounding good with no obvious audible
difference from the control.

## Decision

Production and cooker scenarios use one canonical rate contract:

- physics and capture: `20000/1 Hz`;
- source processing, acoustics, and delivery: `192000/1 Hz`;
- method quantum: 400 physics/capture frames to 3,840 delivery frames, exactly 20 ms.

This replaces the production 10 kHz scenario baseline. There is no dual production
path, legacy-rate selector, or compatibility mode. Frozen 10 kHz parity recordings
remain historical comparison evidence only.

The higher source clock halves the physical/capture time step and derives propagation
delays from that actual clock without replacing the accepted windowed-sinc
reconstruction. The gate does not claim that rate alone made an obvious audible
improvement; it accepts the finer foundation because the listening result remained
sound, deterministic, and inexpensive enough for the current cooker.

## Evidence

Both renders used the BMW M52TUB28 held-dyno pull/lift procedure with embedded Git head
`20258b9d012c03357cd69a792a0d194c82c0864a` and renderer source-closure SHA-256
`2678fd52b7d4fc971acbf9662e8e3ef7e2e0f1d334fff5cd9dbd08d89ec426c5`:

| Evidence | Historical 10 kHz control | Accepted 20 kHz candidate |
|---|---|---|
| Artifact root | `artifacts/listening/fidelity-rate-bmw-10khz-control-20258b9/` | `artifacts/listening/fidelity-rate-bmw-20khz-candidate-20258b9/` |
| Wall time on the development PC | `28.62 s` | `35.18 s` |
| Audition WAV SHA-256 | `0c3553463d07867cfe4382a97a48c5bad7ccf57af7cd0154843893e1ba3a658e` | `93723eeb8dd63e7c3111387b5a4fbc2245b14e9aaf1a2e75ceb39045a1f916cb` |
| Decoded audition PCM SHA-256 | `fbda34577c0213fb8ac2b07540669f5bd4f00abdffae8d7acafdd630f44984ed` | `cb49e13910360525bbdcb8b004c8c8ad6cb3a8a10a6854782946c1962ff3fe04` |
| Raw WAV SHA-256 | `8c08586d90d0f541388d4576bf8d47c2e58147224c0d9f8c9b7ccda71e043742` | `90737e00d6f56c6893f9afb23957ed1becc1aebe3571423c263210edb306dee7` |

The independent repeat at
`artifacts/listening/fidelity-rate-bmw-20khz-candidate-repeat-20258b9/` matches the
candidate's complete audition WAV and raw WAV byte for byte. The user then explicitly
selected 20 kHz as the single canonical production/cooker rate.

## Canonical cutover verification

Commit `a3ae7cd8003921b1aecd4afb3dbe24bb156cffae` made that accepted rate the
sole production contract. A clean Release build rendered the now-canonical BMW
scenario twice:

- `artifacts/listening/fidelity-rate-bmw-20khz-canonical-a3ae7cd/` (`36.98 s`);
- `artifacts/listening/fidelity-rate-bmw-20khz-canonical-repeat-a3ae7cd/` (`36.32 s`).

All eight WAV files match between those two renders byte for byte. The canonical raw
master also matches the accepted candidate byte for byte at
`90737e00d6f56c6893f9afb23957ed1becc1aebe3571423c263210edb306dee7`, and
the decoded signed-24-bit audition PCM matches it exactly at
`cb49e13910360525bbdcb8b004c8c8ad6cb3a8a10a6854782946c1962ff3fe04`.
The canonical audition container itself hashes to
`e77a236d2c94fb66b6dc2799e212b6cd0f4f9bd27643568e2f1dd379cb884dcf`;
its container differs from the pre-cutover candidate only because the embedded build
and method metadata now identify the canonical contract.

The clean build records renderer source-closure SHA-256
`38ec6853a7b5b37c7afcbfc8bc98544e95e6dc09363c61f8ae3042e4d6feb08f`.
This closes the implementation gate as well as the earlier listening gate: production
has one 20 kHz physics/capture clock, and that cutover did not alter the approved
samples.

The deterministic session and JSON-migration tests use the separate authored
`bmw-m52b28-inertial-dyno-1500-6500rpm` procedure. Its clean 20 kHz Release output is
preserved as
`reference/oracles/bmw-m52b28/bmw-m52b28-canonical-20khz-a3ae7cd-dyno-1500-6500rpm.wav`
with whole-WAV SHA-256
`61b815441fdca9e30a03b3f755a7b3a9200e8663d98a0d7924838813e41f6697` and decoded
PCM24 SHA-256
`758df536d5b4fc2fdf031d16d31ba0294300a0c10e9008bc126b0e94b5607ac6`.
This is a machine regression oracle, not an additional listening claim. The prior
10 kHz generated oracle remains historical evidence and is no longer consumed by the
active session or migration tests.

## Acceptance boundary

This closes item 1 of the verified post-parity fidelity queue. It changes the canonical
physical/capture cadence, not the engine definitions, presentation rates, resampler,
IR/convolution path, mix, or output encoding. The independently gated cylinder-lane
and collector refactor subsequently closed without changing those accepted samples;
see
[`POST_PARITY_FIDELITY_CYLINDER_LANES.md`](POST_PARITY_FIDELITY_CYLINDER_LANES.md).
