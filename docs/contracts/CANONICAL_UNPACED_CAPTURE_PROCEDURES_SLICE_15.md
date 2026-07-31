# Canonical unpaced capture procedures — slice 15

Status: implemented and runtime-verified on 2026-07-31; recording hashes remain
slice-16 work.

Scope: define the smallest BMW M52TUB28 finite-scenario set that independently covers
crank, startup/catch, settled idle, loaded rise, part load, coast fall, neutral
limiter, limiter lift/recovery, and shutdown. This slice changes no engine physics,
source generation, routing, conditioning, impulse response, mastering, or live-session
lifetime.

## 1. Six procedures cover nine capture roles

| Procedure | Capture roles | Authored interval |
| --- | --- | --- |
| `canonical-crank-only-0rpm.json` | crank | `0.0–2.5 s`: fuel and ignition off; starter continuously engaged |
| `cold-start-crank-catch-0rpm.json` | startup/catch | `0.0–0.7 s`: crank; ignition at `0.7 s`; starter release at `1.3 s`; end at `4.0 s` |
| `held-idle-region-700rpm.json` | settled idle | `6.44–21.44 s`: settled 700 RPM held operating point |
| `canonical-loaded-rise-part-load-coast-1500-4500rpm.json` | loaded rise, part load, coast fall | `3.0–9.0 s`: WOT 1,500→4,500 RPM; `9.0–11.0 s`: 45% throttle at 4,500 RPM; `11.0–14.0 s`: 4% throttle and unforced fall |
| `warm-running-free-rev-700rpm.json` | neutral limiter, limiter lift/recovery | release at `0.9 s`; 90% throttle at `0.95 s`; lift at `2.55 s`; recovery demand at `5.3 s`; end at `6.4 s` |
| `canonical-key-off-shutdown-700rpm.json` | shutdown | audible at `0.9 s`; key-off cuts fuel and ignition at `1.7 s`; end at `2.8 s` |

These are six scenarios rather than nine because loaded rise, stabilized part load, and
the following coast are one state-continuous load cycle, while limiter contact and its
lift/recovery must likewise share crank state. Crank-only remains separate from
startup/catch so starter mechanics can be heard without firing.

## 2. Motion ownership is explicit

- Crank, startup/catch, neutral limiter/recovery, and shutdown use `free_engine`.
- Settled idle uses the existing `held_speed` operating point. It is a stable 700 RPM
  sound capture, not evidence of an ECU idle-speed controller; the authored engine has
  no such controller yet.
- Loaded rise and part load use the bounded `held_dyno` absorber. Maximum driving
  torque is exactly zero, so after the lift the dyno cannot pull the crank back onto
  the falling target lane. The coast is dynamically unforced.
- Shutdown uses 4% throttle and an explicit `40 N m` external bench resistance to keep
  the released free crank near the 700 RPM starting region before key-off. This is
  declared scenario load, not hidden accessory or vehicle behavior.

## 3. Unpaced publication contract

Every procedure remains an ordinary `finite_scenario` document and is exported by a
fresh unpaced execution of its authored timeline. Live workbench changes are not folded
into the capture. The six jobs own no shared simulation state and may run concurrently.

All procedures retain the accepted listening profile:

- physics and capture at 10 kHz;
- source processing, acoustics, and delivery at 192 kHz;
- `master-engine-raw` and `master-engine-audition` output buses;
- the existing M52TUB28 presentation routes, conditioning, configured IR, and
  mastering.

The longest authored horizon is the existing settled-idle procedure at `21.44 s`.
That bounds a concurrent six-procedure batch by one short clip rather than the sum of
all six durations.

## 4. Gates

`canonical-capture-map.test.mjs` freezes the exact six-to-nine role mapping, compound
phase boundaries, accepted output buses, and Web catalog reachability.
`canonical-captures.integration.mjs` compiles and fully executes the three new finite
procedures through the exact C ABI/WASM session path. The full workbench smoke gate
also fetches and compiles each new repository package through the visible JSON editor.

The rebuilt WASM gate observed non-fired crank motion from `79.4166` to `315.8972`
RPM, the loaded run at a maximum `4500.000000000001` RPM and final `1216.0109` RPM,
and shutdown at a maximum `725.2095` RPM with the crank stopped at `1.98 s`. The full
browser gate passed with zero startup underruns and retained the existing canonical
browser WAV SHA-256 `2972cdad90d08d31ddfac3ca99a4efcda93a15db637d93abc2b7844085c3e4b2`.

A six-job native production export ran concurrently from implementation base
`9bf89e4`. The batch completed in approximately `30.4 s`, bounded by settled idle;
individual wall times were crank `4.07 s`, startup/catch `6.49 s`, settled idle
`30.39 s`, loaded/part/coast `22.18 s`, limiter/lift/recovery `10.23 s`, and shutdown
`4.50 s`. Local proof artifacts are in
`.work/slice15-canonical-captures-9bf89e4/`. Their raw / audition / manifest SHA-256
triples are:

- crank: `55d5c3608cde60d3a05bcba51c9a0769ea988d6ce5ebc80f85b2061db7ce18ea` /
  `1352c07802aa375a76e96e5e4302670aad93821e9b5c6fc0835b6e673aaa7d53` /
  `c2d46dbbf3623bd77dd1b82f5c36ce360c7eefc802b3e033d3853e5c5bb50236`;
- startup/catch: `55b4f80735dcb9c74d26b2eae7d0c7495bedfd9fd68a4b118e97bf71f7ec8571` /
  `9b734c2f5f63db130f6fa1db63b48cd818a0736bea3f9f94bd62752752f1beea` /
  `856615c7779d7a5f0099ece9c889caaa38da19561b4dfb6a090e0e8de76fabc7`;
- settled idle: `98b8529054210927921bb6c9c1e5df51bbe6daf66c8596bef169368a53d0167a` /
  `b7184541dc156b9c5831e971ee2ab99ce89d726b1ddfe38e082d0cd3c9306ff7` /
  `9c93791cc3fcd7fc0ec1439287c771ef32f49f1156131b836f4da19a1b6a2b59`;
- loaded/part/coast: `7f633479caf20869cd30c60520e050394c6f2474eb4d4697804eb0ec4ed26711` /
  `97a9407533a0d0891d9bc67ae5df14bcdd3a9dbf75f5a8b5009ec7aed2d65441` /
  `194f76934d3de9b85172af611b584dcf23252eaa16056c9fa6dc8e001fd6ca85`;
- limiter/lift/recovery: `c20e061a555c3b0be549c39d42f430cace61130ad605491401255f4e1621468c` /
  `ec89f4047371c91b0bec94c6eaa4d175b69cf74d3bb37855d354d8b771ef9e98` /
  `35e906bcd7116a932bec59b7794af4e5aa29c532e836a14f005bea2278132360`;
- shutdown: `09f431e5b08874ef865ef7b457293e8b5f8c4c45234200086bdced8ca7576bb7` /
  `2fdb3b58fa49a79d713bc6fad01933c4b100c22bac467c4de5662068bd0ad389` /
  `96e91a8ad6b93c0ad8be576c6e2e1916c71061a4773f2dcf849633cca41f9439`.

Slice 16 owns immutable WAV hashes and cross-family representative recordings. This
slice proves that each required BMW procedure is declarative, executable, bounded, and
exportable; it does not claim a new sound-fidelity improvement.
