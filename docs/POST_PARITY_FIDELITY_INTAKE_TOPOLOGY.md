# Post-parity intake-route topology gate

Status: complete and exhaust-PCM-identical on 2026-08-03.

## Decision

The engine, capture, presentation, session, C ABI, WASM, and native-publication
contracts now carry a typed `intake_inlet` source route independently from exhaust
routes. There is still one canonical cooker clock:

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

The topology checkpoint deliberately publishes all three intake stems as canonical
positive zero. Public bus descriptors mark them `declared_silent`; exhaust stems and
both masters remain `active`. Silent intake routes do not enter reconstruction,
conditioning, convolution, random-stream consumption, or master reduction. This
prevents a topology declaration from masquerading as audible intake fidelity and
prevents an inserted zero operation from changing the accepted Float32 exhaust sum.

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

## Acceptance boundary

This commit is plumbing, not a sound improvement and not a listening candidate. It
does not claim an exterior inlet impedance, mouth radiation model, intake transfer
function, microphone position, or calibrated intake level. It does not run exhaust
jitter, derivative, generic air noise, or exhaust IR processing over intake pressure.

The next change is one isolated sound-bearing BMW intake-pressure diagnostic. It must
retain the accepted exhaust output, render matched exhaust-control/intake-solo/full
comparisons, and stop for listening before any further fidelity work. An accepted
intake implementation becomes the one production path; rejected diagnostic work is
removed rather than retained as an optional or compatibility mode.
