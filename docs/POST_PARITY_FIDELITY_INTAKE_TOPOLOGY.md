# Post-parity intake-route topology gate

Status: historical silent-topology checkpoint, complete and exhaust-PCM-identical on
2026-08-03; superseded by the accepted active path on 2026-08-04.

Current production behavior and listening evidence are recorded in
[`POST_PARITY_FIDELITY_ACTIVE_INTAKE_LISTENING_GATE.md`](POST_PARITY_FIDELITY_ACTIVE_INTAKE_LISTENING_GATE.md).

## Decision

At this checkpoint, the engine, capture, presentation, session, C ABI, WASM, and
native-publication contracts first carried a typed `intake_inlet` source route
independently from exhaust routes. There was, and remains, one canonical cooker clock:

- physics and capture: `20000/1 Hz`;
- reconstruction, presentation, and delivery: `192000/1 Hz`;
- method quantum: 400 capture frames to 3,840 presentation frames.

The low-order capture for an intake route observes the already executed physical
intake model rather than inventing a parallel engine model:

- the route source is its resolved intake plenum;
- pressure and temperature come from that plenum;
- signed mass flow is the main-throttle plus idle-bypass boundary flow, reoriented to
  the public source-volume-to-exterior convention;
- exterior inlet-mouth area remains explicit positive zero because the authored
  plenum cross-section is internal solver geometry, not a microphone/radiation area.

This historical topology checkpoint deliberately published all three intake stems as
canonical positive zero. Public bus descriptors marked them `declared_silent`;
exhaust stems and both masters remained `active`. Silent intake routes did not enter
reconstruction, conditioning, convolution, random-stream consumption, or master
reduction. That prevented a topology declaration from masquerading as audible intake
fidelity and prevented an inserted zero operation from changing the accepted Float32
exhaust sum.

The sole portable boundary is now C ABI v6. There are no v5 aliases or compatibility
decoders.

## Exact-output evidence

The GNU Release `EngineSession` gate rebuilt the current source tree and compared the
complete canonical BMW M52B28 inertial-dyno recording with the frozen 20 kHz oracle:

- audible frames: `2,880,000` mono frames;
- PCM24 extent: `8,640,000` bytes;
- decoded PCM24 SHA-256:
  `758df536d5b4fc2fdf031d16d31ba0294300a0c10e9008bc126b0e94b5607ac6`;
- result: every PCM byte matched;
- elapsed time on the development PC: `113.20 s` for the instrumented regression
  executable, not a normal release bake-time claim.

Focused gates additionally execute an interleaved exhaust/intake/exhaust capture,
preserve both exhaust lanes, verify intake plenum pressure/temperature and signed
boundary flow, publish exact positive-zero intake stems, preserve both masters, bind
the explicit signal disposition through native/C/WASM/JavaScript, and project intake
artifacts through the native render job.

## Historical acceptance boundary

This commit is plumbing, not a sound improvement and not a listening candidate. It
does not claim an exterior inlet impedance, mouth radiation model, intake transfer
function, microphone position, or calibrated intake level. It does not run exhaust
jitter, derivative, generic air noise, or exhaust IR processing over intake pressure.

The required next change was one isolated sound-bearing BMW intake-pressure
diagnostic. It had to retain the accepted exhaust output, render matched
exhaust-control/intake-solo/full comparisons, and stop for listening before further
fidelity work.

That gate closed on 2026-08-04 at commit
`953040294274362fd699436e3040cbcee8f70d30`. The user accepted the active intake
comparison, every exhaust stem remained byte-identical to the accepted 20 kHz
control, and the active implementation became the sole production path. The
`declared_silent` behavior described above remains only this historical checkpoint;
it is not an optional or compatibility mode in current execution.
