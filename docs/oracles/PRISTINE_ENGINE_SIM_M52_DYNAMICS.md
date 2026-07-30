# Pristine engine-sim M52 transient dynamics oracle

Status: authoritative operating-bench evidence

Identified source:
`ange-yaghi/engine-sim@85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

Asset: pristine `assets/engines/bmw/M52B28.mr`

Simulation rate: 10,000 Hz

This record freezes behavioral observations, not source code. The oracle compiles the
identified pristine asset and runs the original rigid-body, gas, ignition, friction,
starter, and dyno implementation headlessly. Its stock 9.5-second reproduction prefix
is byte-identical to the established repeated trace.

## Authority boundary

Pristine engine-sim is authoritative until operating parity is complete. A
literature-based candidate is not an upgrade merely because it is more elaborate.
Future fidelity work changes one established subsystem at a time only after the
source-equivalent baseline remains audible and accepted.

Gas-exchange pumping is already present in the cylinder pressure-volume torque.
Pristine closed throttle continues to admit a small main-throttle flow plus the
parallel idle circuit and continues to carry fuel. Automatic overrun fuel cut is
therefore not a parity requirement.

The two executable pristine mechanical losses are:

- authored crankshaft rotation friction, `10 lb*ft = 13.55817456 Nm` for this asset;
- per-cylinder piston-wall friction using the executable C++ defaults and the previous
  physics step's cylinder-wall constraint reaction.

The declared `.mr` `cylinder_friction_parameters` surface is not connected to the
pristine cylinder backend and is not an authored capability to reproduce.

## Controlled WOT release

The upstream dyno holds exactly 1,500 RPM at full throttle for five seconds and then
releases. Times are measured from release to the first 7,000-RPM crossing.

| Executable friction | 1,500 to 7,000 RPM |
|---|---:|
| Stock | 0.4399 s |
| Crank friction disabled | 0.4358 s |
| Piston-wall friction disabled | 0.3718 s |
| Both disabled | 0.3530 s |

## Controlled closed-throttle release

The upstream dyno holds exactly 6,000 RPM at full throttle and then releases at the
same boundary that throttle becomes zero. Values are elapsed seconds from release.

| Executable friction | 5,000 RPM | 4,000 RPM | 3,000 RPM | 2,000 RPM | 1,500 RPM |
|---|---:|---:|---:|---:|---:|
| Stock | 0.3711 | 0.6665 | 1.0626 | 1.6702 | 2.1667 |
| Crank disabled | 0.4308 | 0.7964 | 1.3171 | 2.3259 | 4.0873 |
| Piston disabled | 1.4890 | 2.6123 | 4.1773 | 8.2498 | not reached |
| Both disabled | 3.3120 | 6.8721 | not reached | not reached | not reached |

The piston-wall mechanism is the dominant coast loss. Crank friction is secondary at
high RPM but materially changes the low-speed balance. One aggregate curve cannot be
declared source-equivalent merely because it matches the stock WOT crossing.

## Natural closed-throttle balance

Mean and instantaneous range over simulation time 30.0--67.5 seconds, corresponding
to 22.5--60 seconds after the controlled lift:

| Executable friction | Mean RPM | Range |
|---|---:|---:|
| Stock | 1,041.953 | 997.340--1,088.587 |
| Crank disabled | 1,458.693 | 1,431.806--1,488.624 |
| Piston disabled | 1,858.291 | 1,826.891--1,881.972 |
| Both disabled | 3,386.115 | 3,377.142--3,395.623 |

The earlier 1,260--1,275-RPM observation was only about three seconds after lift and
was not equilibrium. The stock long-running balance is approximately 1,042 RPM. It is
a natural physical balance, not a regulated-idle controller.

## Clean centered-reaction acceptance

The clean runtime does not import the pristine general-purpose constraint solver. It
uses the same piston-friction constants, written operation order, sign branch, and
one-step retained-wall timing, while resolving the admitted centered slider-crank's
wall reaction by full piston-and-midpoint-rod inverse dynamics.

That replacement was compared per cylinder directly with the pristine
`LineConstraint` result:

| State | Samples | Wall-force MAE | L1-relative | Correlation |
|---|---:|---:|---:|---:|
| Held 1,500 RPM WOT | 4,800 | 6.724 N | 1.346% | 0.999768 |
| Held 6,000 RPM WOT | 4,800 | 16.103 N | 1.358% | 0.999635 |
| First 0.1 s after 6,000-RPM lift/release | 6,000 | 13.172 N | 1.440% | 0.999332 |

Substituting actual constraint-solver kinematics into the same inverse dynamics
reconstructs its wall reaction to approximately `1e-12 N`, confirming the equations,
signs, and tick alignment. The remaining difference is constraint drift, peaking near
`123 um` at 6,000 RPM. Feeding the clean ideal reaction into the following source
friction step changes applied piston friction by less than `0.4%` L1 in all three
probes. A piston-only textbook side-force shortcut reached `27%`--`38%` L1 error at
6,000 RPM and is rejected.

## Clean end-to-end friction checkpoint

The authored BMW JSON was then run through the clean FreeEngine session with the same
five-second held-WOT preparation and exact release controls:

| Response | Pristine | Clean | Absolute error |
|---|---:|---:|---:|
| WOT 1,500 to 7,000 RPM | 0.4399 s | 0.4525 s | 0.0126 s |
| Coast 6,000 to 5,000 RPM | 0.3711 s | 0.3656 s | 0.0055 s |
| Coast 6,000 to 4,000 RPM | 0.6665 s | 0.6636 s | 0.0029 s |
| Coast 6,000 to 3,000 RPM | 1.0626 s | 1.0561 s | 0.0065 s |
| Coast 6,000 to 2,000 RPM | 1.6702 s | 1.6654 s | 0.0048 s |
| Coast 6,000 to 1,500 RPM | 2.1667 s | 2.1645 s | 0.0022 s |

The tracked short gate admits at most `0.015 s` WOT error and `0.010 s` error at every
coast crossing. Its optional 60-second balance probe measures the inclusive
22.5--60.0-second post-release window. The clean result is `1,043.605 RPM` mean with a
`996.270--1,082.267 RPM` range, compared with the pristine `1,041.953 RPM` mean and
`997.340--1,088.587 RPM` range. This closes the crank-plus-piston friction response
checkpoint; it does not claim that instantaneous crank acceleration is yet equivalent.

## Retained scratch evidence

Ignored working evidence lives under `.work/upstream-friction-ablation/`:

- `wot-{stock,no-crank,no-piston,neither}.jsonl`;
- `coast-{stock,no-crank,no-piston,neither}.jsonl`;
- `wot-from-held-1500.txt`;
- `coast-from-held-6000.txt`.

The centered-reaction comparison lives under `.work/upstream-wall-reaction/`:

- `held-1500-trace.csv`, SHA-256
  `ead4600062ea52e590c3fb763d4bb770fefd1160e81a3d7b30329f6120b5bb62`;
- `held-6000-trace.csv`, SHA-256
  `a63bc11298684cb1d876753f9ae1c59caade4ad434d94e3dba8df61fbfd232f9`;
- `coast-6000-trace.csv`, SHA-256
  `024a2d5927e704f3a637e3edea1835ed254f0a0b935d3459efb0bebe326ab703`.

Their SHA-256 identities, in that order followed by
`wot-{stock,no-crank,no-piston,neither}` and
`coast-{stock,no-crank,no-piston,neither}`, are:

```text
a2114c6822f9d505e8db516e728ef79fc587010a6e9311a14addbddc8f9f87f7
0d03d86de3a755449f3534cf28cd87b2e87a8ca09a8b2c1da7bccd8b57374878
bfad1c4f0e728476db89ed600cb4a13c26ffba6d6dd987d0c00d4c12e3dba64b
c4428ce3f75296d5f5338d4551d013625a51f229d6295c797f854a88de8cb4d6
a50ece9be28e3a0212498c9c96f5cc8900ecd0a9c9e3821b596672ce55774c12
cf997d79f87bf23be75e01e40b0756f9b758986073a36b7f1628dfcf10d77db2
d0d08408297f3703e9a6ca26b49ffb995f79dce1b9ea0e910c58b586e67cfbf7
a1cc83dd9cfc82364e2924a641eb052633c5355496c5ec9dbff707670c29e553
949075b92ceee461b8095fabbce30c10efe1cba520b821b0a06c7665b38b699d
78aaa0352b0346829ff6653311a8a5dac068754067b7607281019be5c8483116
```

The large telemetry streams remain scratch artifacts. This compact record is the
tracked provenance and acceptance authority.
