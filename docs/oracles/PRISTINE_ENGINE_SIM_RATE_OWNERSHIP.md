# Pristine engine-sim rate-ownership oracle

Status: pinned-source and history audit
Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`
Audit date: 2026-08-02

Read every pinned citation through the repository-local object database at
`../engine-sim`, using `git -C ../engine-sim show <authority>:<path>`. The neighboring
working tree contains later fork changes and is not the pristine authority.

## Finding

Pristine engine-sim has one authored mechanism clock. Gas exchange advances eight
substeps per mechanism step, and one exhaust-excitation record is emitted after every
mechanism step. There is no separately authored pristine capture or excitation rate.

The per-cylinder exhaust-delay FIFO is the exception: its length is calculated with a
literal `10000.0` samples/s even when the engine's authored simulation frequency is
different. History shows that this is a stale implementation assumption, not general
rate ownership:

1. commit `db70656c2b19d03046402137de11847ac1287faa` added the public
   `simulation_frequency` input on 2022-08-27;
2. commit `46d964e88cba05af46b418cdafdd59817999fe42` added unequal-header
   propagation delays on 2022-09-10 with the literal 10 kHz;
3. commit `4f7e06b211d0b51914aed0539b397ac27f70d0f3` added curated engines
   ranging from 5 kHz to 40 kHz on 2022-09-28 without changing that literal.

The clean-room contract therefore treats solver, capture, and excitation cadence as
scenario-owned and derives physical propagation delay from the admitted capture clock.
It does not preserve the stale 10 kHz FIFO assumption as a second engine-definition
authority. Existing 10 kHz requests must remain binary-identical.

## Exact pristine clock graph

### Authored mechanism clock

The public MR `engine` has `simulation_frequency: 10000` as a default and forwards the
value to `_engine` (`es/objects/objects.mr:89-90,113-116,132-135`). The scripting node
stores it in `Engine::Parameters::initialSimulationFrequency`
(`scripting/include/engine_node.h:157-164`), `Engine` retains it
(`src/engine.cpp:39-43,70-73`), and application loading applies it to the simulator
(`src/engine_sim_application.cpp:468-483`). Thus 10 kHz is a default, not the only
pristine mechanism rate.

For simulation frequency `F`, pristine evaluates:

```text
h = 1.0 / F
steps_for_live_frame = round((wall_dt * simulation_speed) / h)
```

(`include/simulator.h:57-60`, `src/simulator.cpp:64-75`). Each accepted step executes
the rigid-body system, engine/throttle, vehicle, transmission, filtered-speed update,
piston-engine step, and exhaust-source write in that order
(`src/simulator.cpp:95-151`).

### Fluid clock

`PistonEngineSimulator` initializes `m_fluidSimulationSteps = 8`
(`src/piston_engine_simulator.cpp:11-27`). For every outer mechanism step it evaluates

```text
hg = h / 8
```

then processes every exhaust, every intake, and every chamber in written nested-loop
order for all eight substeps (`src/piston_engine_simulator.cpp:283-318`). The pristine
fluid rate is consequently `8*F`; it is not an independent authored clock.

### Capture and excitation clock

Pristine has no public capture object. `writeToSynthesizer()` is called exactly once at
the end of every committed mechanism step (`src/simulator.cpp:147-150`). That function
forms one pressure excitation per cylinder, advances one per-cylinder delay FIFO,
serially accumulates cylinders into exhaust routes, and writes one route vector to the
synthesizer (`src/piston_engine_simulator.cpp:370-412`). At normal simulation speed,
mechanism, observation, and excitation therefore share `F`.

The source expression and association are:

```text
a  = min(abs(filtered_engine_speed), 40.0) / 40.0
a3 = a * a * a
x  = a3 * 1600 * (
         1.0 * (primary_static_pressure - 1 atm)
       + 0.1 * primary_dynamic_pressure(+1, 0)
       + 0.1 * primary_dynamic_pressure(-1, 0))

delayed = cylinder_delay_fifo(x)
route_bus += sound_attenuation
             * (route_audio_volume * delayed / cylinder_count)
             * (1 / (exhaust_length * exhaust_length))
```

Cylinder and route traversal use runtime index order. Generalizing clocks must not
reassociate this arithmetic or change the post-step observation phase.

### Stale propagation clock

For every cylinder, pristine first evaluates:

```text
exhaust_length = header_primary_length + exhaust_system_length
delay_seconds  = exhaust_length / 343.0
delay_samples  = int(round(delay_seconds * 10000.0))
```

(`src/piston_engine_simulator.cpp:218-226`, `include/delay_filter.h:20-26`). A delay of
`N` samples returns zero for the first `N` inputs and then returns the input from `N`
mechanism records earlier; a zero-sample delay passes the current input
(`include/delay_filter.h:32-43`).

At an authored mechanism rate `F`, the actual pristine wall-clock delay is therefore
approximately

```text
T_effective = round(T_physical * 10000) / F
```

instead of `T_physical`. It is half the authored duration at 20 kHz, about 2/7 of the
authored duration at 35 kHz, and twice the authored duration at 5 kHz. The delay FIFO is
created before runtime and is not rebuilt when the GUI changes simulation frequency,
which is further evidence that the literal is stale rather than a coherent second
clock.

### Rates actually authored by pristine assets

The pinned engine catalog explicitly authors the following non-default values:

| Rate | Pinned curated engines |
|---:|---|
| 40 kHz | Honda TRX520 |
| 35 kHz | Harley-Davidson Shovelhead |
| 30 kHz | Kohler CH750 |
| 20 kHz | Hayabusa, Honda VTEC, and three Subaru variants |
| 17 kHz | Audi inline five |
| 12 kHz | radial five |
| 10 kHz | 2JZ, GM LS, Ferrari F136; also the public default |
| 7.5 kHz | radial nine |
| 7 kHz | Merlin V12 |
| 6.5 kHz | LFA V10 |
| 5 kHz | Ferrari 412 T2 |

These values occur in `assets/engines/atg-video-1/*.mr` and
`assets/engines/atg-video-2/*.mr`. The pinned stock
`assets/engines/bmw/M52B28.mr` does not override the public 10 kHz default. The later
local `m52tub28_cleanroom_baseline.mr` is a separate authored asset and explicitly asks
for 20 kHz; it must not be used to rewrite the pinned stock-BMW fact.

## Audio-rate facts that are not solver ownership

Pristine initializes its synthesizer and device at 44.1 kHz
(`src/simulator.cpp:204-211`, `src/engine_sim_application.cpp:169-177`). At each live
frame it tells the synthesizer that its input rate is
`simulation_frequency*simulation_speed` (`src/simulator.cpp:64-75`). The pristine
synthesizer linearly interpolates between consecutive route vectors onto its 44.1 kHz
buffer (`src/synthesizer.cpp:168-195`) and applies its route DSP at the audio rate.

At the pinned commit the per-route reconstruction low-pass is 1.9 kHz, not 8 kHz
(`src/synthesizer.cpp:58-79`). The fixed 8 kHz filter belongs to the later downstream
fork commit `bf3d59e09c00723eb2de2b6c4b7407723e5d2161`; it is not pristine behavior.

The clean-room renderer also does not use pristine's linear 44.1 kHz output path. Its
accepted presentation method is a causal 257-tap, 4096-phase windowed-sinc
reconstruction from 10 kHz to 192 kHz, followed by 192 kHz route conditioning,
convolution, and publication. Static 44.1 kHz PCM16 is an input-IR constraint, not the
engine-output format. None of those presentation facts makes 10 kHz an engine-owned
solver constant.

## Greenfield ownership and current execution boundary

The current declarative scenario already carries five explicit rational rates. Their
ownership is:

| Clock | Owner and current rule |
|---|---|
| Physics | `scenario.rates.physics`; sole owner of outer solver step `h=denominator/numerator`. |
| Capture | `scenario.rates.capture`; the low-order parity executor currently requires it to equal physics and publishes one post-step record per physics step. |
| Excitation | The admitted capture block's clock; it must equal the scenario capture clock. There is no separately authored excitation rate. |
| Propagation delay | Physical engine path length plus the selected excitation method's propagation speed, resolved against the admitted capture clock when the session is compiled. |
| Source processing | `scenario.rates.source_processing`; accepted public presentation remains exactly 192 kHz. |
| Acoustics and delivery | Their explicit scenario rates; accepted production presentation currently requires both to equal 192 kHz. |

`LegacyReferenceExcitationProfile` therefore needs physical path geometry, audible gains,
propagation speed, and deterministic accumulation order. It must not retain a duplicate
engine-level `delay_rate` or scenario-specific cached `resolved_delay_samples`.

This parity slice generalizes headless solver, capture, excitation, and delay ownership.
It does **not** publish a new higher-rate production-audio method. The production source
stage remains the accepted explicit 10 kHz-to-192 kHz method until the post-parity
fidelity gate renders and auditions a higher physical/capture rate. That later audition
is expected to change PCM because both solver resolution and correctly timed propagation
can change the source signal.

## Exact 10 kHz preservation requirements

For every existing 10 kHz fixture, the refactor must preserve:

1. `h` as binary64 `double(1)/double(10000)` and `hg` as `h/double(8)`;
2. all mechanics, governor, limiter, ignition, gas, and combustion operation order;
3. outer-step flow publication as `(signed_mol*M_air)/h`, not a reassociated form;
4. one post-step capture and one excitation input per physics step;
5. delay resolution in this written order:
   `((header_length + route_length) / propagation_speed) * rate_hz`, then
   `std::round`, range check, and integer conversion;
6. the zero-delay and `N`-record FIFO semantics above;
7. cylinder-order excitation calculation and route accumulation;
8. the existing causal-reconstruction coefficient table, initial phase, 200-to-3840
   block extent, RNG draw cadence, conditioning, IR, and mastering;
9. exact accepted decoded PCM and completed WAV bytes for unchanged procedures.

Request identity and manifest schema may change when redundant resolved fields are
removed; such metadata churn must not be mistaken for an audio change. A public
non-10-kHz recording remains deliberately outside this parity checkpoint and requires
the later control/candidate listening gate.

## Clean-room completion evidence

The focused 20 kHz lower-layer gate executes one 400-frame/20 ms mechanics, gas, and
capture block. It proves a 50 us crank step, rate-scaled mass-flow publication, and a
360-sample propagation delay for the same physical path whose frozen 10 kHz fixture
resolves to 180 samples. Both delays represent exactly 18 ms.

All 84 non-publication tests pass. A clean Release build of the authored BMW migration
gate produced request identity
`e01b872b91f142ef65633783a736cbea79169e99b4c45e8df6d68ffa481369d6` and the unchanged
8,640,586-byte complete audition WAV with SHA-256
`f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552`.
The request identity changed because the redundant engine-owned delay rate and cached
delay counts were removed; the complete audio container remained byte-for-byte exact.
