# M3 BMW parity algorithm

Status: normative pre-implementation companion

Method ID: `legacy_low_order_v1`

Applies to: the M3 BMW M52B28 parity render only
Date: 2026-07-27

## 1. Purpose, authority, and boundary

This document closes the executable meaning of the M3 low-order algorithm described
in `MODEL.md`. `MODEL.md` remains authoritative for project architecture, units,
signs, public boundaries, determinism, torque vocabulary, failure classes, and upgrade
admission. This companion does not repeat those general rules.

The source-informed authority for this method is the exact effective engine-sim commit
`9617562a7a5615c2bf84c9ec39cd5ae25c560059`. The commit is locally available but is
not asserted to be publicly fetchable. The frozen fixture authority is:

- `reference/fixtures/bmw-m52b28-p18/manifest.json`;
- `reference/oracles/bmw-m52b28/SOURCE_MATRIX.md`;
- `reference/fixtures/bmw-m52b28-p18/reference-parity.bin`, SHA-256
  `19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c`;
- `reference/fixtures/bmw-m52b28-p18/component-seeds.bin`, SHA-256
  `ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f`.

The manifest owns resolved route identities, the complete prescribed trajectory,
component seed pairs, and serialized fixture layouts. Equations, constants, state,
ordering, and bounds are fixed here and may not be inferred from a future source
checkout. A contradiction between this record, `MODEL.md`, the frozen source matrix,
or the manifest is a contract failure; implementation stops rather than choosing one.

`legacy_low_order_v1` reproduces the legacy gas, ignition, and gasoline-combustion
behavior but deliberately replaces the generic multibody constraint graph with an
analytic centered slider-crank. It does not claim source-bit-identical mechanism
torque, physical BMW accuracy, a conservative control-volume formulation, or pipe-wave
fidelity.

The accepted M2 excitation and presentation implementations remain unchanged. The M3
simulator must derive every pressure term from newly simulated state. It must never
read `reference-audit.bin` or replay either captured pre-DSP bus.

## 2. Fixed arithmetic and resolved constants

Unless an operation below states otherwise, the binary64 rules in `MODEL.md` apply.
For source parity, these legacy constants are inputs, not modernized approximations:

| Name | Exact source value |
|---|---:|
| `pi_l` | `3.14159265359` |
| `R` | `8.31446261815324 J/(mol*K)` |
| air molar mass `M_air` | `0.02897 kg/mol` |
| atmosphere/crankcase pressure | `101325 Pa abs` |
| ambient/start temperature | `298.15 K` |
| wall temperature | `363.15 K` |
| gas degrees of freedom `f` | `5` |
| heat-capacity ratio `gamma=1.0+(2.0/f)` | `1.4` |
| legacy RPM scale | `1 rpm = 0.104719755 rad/s` |
| mechanism rate | `10000 Hz` |
| mechanism step `h` | binary64 `1.0/10000.0 s` |
| gas substeps | `8` |
| gas step `hg` | binary64 `h/8 s` |

Source-authored angles use the literal unit construction
`deg_source=pi_l/180.0`. Do not collapse a source expression to a total number of
degrees before conversion. For example, `rot360=360*deg_source`,
`rot120=120*deg_source`, and an intake center is formed by additions from those
already-rounded values.

The following source helpers are normative wherever their names occur in this
companion:

```text
positive_mod(x,m):
    if x < 0:
        x = ceil(-x/m)*m + x
    return fmod(x,m)

wrap_2pi(x) = positive_mod(x,2*pi_l)
wrap_4pi(x) = positive_mod(x,4*pi_l)

legacy_clamp(x,lo,hi):
    if x <= lo: return lo
    else if x >= hi: return hi
    else: return x
```

Every later `clamp` means `legacy_clamp`. The cam's separately declared
`fmod`/half-open correction remains the sole wrap exception. These branch rules
preserve equality, signed-zero, and non-finite behavior; a library clamp or a
different modulo implementation is not interchangeable.

The legacy amount called SCFM is molar, not a standard-volume conversion:

```text
one_source_scfm = 0.002641 * 453.59237 / 60 mol/s
                = 0.019965624152833334 mol/s
```

All authored BMW data are `legacy_asset_unverified`. Their serialization into future
engine-spec types may be defined by the next concrete schema record; their values and
the algorithms consuming them are not deferred.

## 3. Canonical 17-second parity scenario

### 3.1 Prescribed motion lane

M3 is a prescribed-motion parity scenario, not a recreation of the source vehicle,
starter, dyno, or constraint solver. A reference-only harness decodes only
`engine_speed_rpm` from each of the 170,000 `reference-parity.bin` records into a
typed prescribed-speed trajectory. The core simulator receives that trajectory just
as it could receive an authored prescribed trajectory. The fixture's
`crank_angle_rad` is decoded separately as a comparator result, never as scenario
input. Captured gas pressure, directional pressure, filtered RPM, and both audit buses
are also forbidden inputs.

For outer step `k`:

- record `k` supplies the post-mechanism `rpm[k]`;
- the signed legacy crank velocity is
  `omega_legacy[k] = -rpm[k] * 0.104719755 rad/s`;
- running-direction angular speed is positive:
  `omega[k] = -omega_legacy[k]`;
- diagnostic running-direction angular acceleration is
  `alpha[k]=(omega[k]-omega_previous)/h`, with fresh
  `omega_previous=0` before record zero, then `omega_previous=omega[k]`;
- with fresh signed body angle `psi[-1]=0`, the simulator advances
  `psi[k]=fmod(psi[k-1]+omega_legacy[k]*h,4*pi_l)`;
- it then evaluates
  `theta_cycle[k]=positive_mod(-(psi[k]-120*deg_source),4*pi_l)`;
- a capture-only running-direction diagnostic starts at
  `theta_unwrapped[-1]=120*deg_source` and advances
  `theta_unwrapped[k]=theta_unwrapped[k-1]+omega[k]*h`;
- the independently decoded fixture angle must match that reconstructed angle under
  the mechanism gate in section 13;
- ignition compares the previous wrapped angle with `theta_cycle[k]`;
- analytic volume, piston speed, cams, and valve conductance use that same record;
- gas and combustion then advance eight substeps without changing prescribed motion.

The unwrapped diagnostic is never wrapped back into a physics input; `psi` and
`theta_cycle` above own all phase-sensitive computation. The initial angle before
record zero is the fresh source angle:

```text
theta_cycle_initial = 120*deg_source rad
```

The trajectory file, schema, count, and hash must match the manifest. Its captured
pressure fields remain comparator outputs and are forbidden scenario inputs. The
starter, dyno, and target-RPM fields describe provenance only because motion is
imposed.

### 3.2 Controls and retained interval

Controls are right-continuous at these half-open step intervals:

| Steps | Role | Target RPM | Requested throttle `u` | Ignition | Fuel | Starter | Dyno |
|---|---|---:|---:|---|---|---|---|
| `[0,8000)` | direction acquisition | 600 | 0.18 | off | on | on | off |
| `[8000,9000)` | direction lock | 600 | 0.18 | off | on | on | on |
| `[9000,10000)` | ignition handoff | 600 | 0.12 | on | on | off | on |
| `[10000,20000)` | loaded pre-roll | 1500 | 0.85 | on | on | off | on |
| `[20000,170000)` | audible pull | frame-held sweep | 0.85 | on | on | off | on |

For audible frame `j=0..749`, held over steps
`[20000+200*j, 20200+200*j)`, the target metadata are:

```text
target_rpm(j) = 1500 + 5000 * (j * 0.02 / 15)
```

The complete one-second bootstrap and one-second loaded pre-roll are causal state
history. Only steps `[20000,170000)` are retained for the 15-second output. No state,
history, RNG, delay, or renderer stage resets at step 20,000.

### 3.3 Step and timestamp convention

Each outer step executes in this order:

1. Apply the interval's requested throttle, ignition-enable, and fuel-enable state.
2. Advance angle from the record's prescribed post-step speed using section 3.1;
   retain the fixture angle only for comparison.
3. Update direct throttle linkage and all intake throttle state.
4. Update filtered RPM.
5. Evaluate ignition crossings and call eligible chamber ignitions in cylinder order.
6. In cylinder order, change chamber volume with legacy work, update the 256-bin
   histories, and sample intake/exhaust conductance.
7. Reset every per-edge signed-transfer accumulator and each cylinder's
   released-energy accumulator.
8. Execute eight gas substeps in the order fixed in section 9.
9. Clear one-step ignition event flags.
10. Publish one post-step `CaptureBlock`, then evaluate the frozen excitation.

The filtered speed starts at zero and is updated before ignition:

```text
alpha = h / (100 + h)
filtered_rpm[k] = alpha * filtered_rpm[k-1] + (1-alpha) * rpm[k]
```

`sample_index=k` means post-step state at `step_end=k+1`; its normative time is the
exact rational `(k+1)/10000 s`. The historical fixture's `time_s` instead starts at
an accumulator value of zero and adds binary64 `h` before writing each record, so
record zero stores `0.0001`. Pair parity records by
`sample_index`/`step_end`; preserve fixture `time_s` as evidence, but do not make
repeated floating addition the new simulator clock.

## 4. Resolved BMW definition

Every value in this section is `legacy_asset_unverified` unless explicitly described
as derived or fixture evidence. It is the resolved M3 datum, not a default to be
silently inherited by another engine.

### 4.1 Cylinder identity, geometry, and phase

The engine is a naturally aspirated inline six with one shared crankshaft and one
four-stroke cycle. Stable cylinder identity and traversal order are fixed:

| Runtime index | Stable ID | Journal angle | Ignition angle | Geometric TDC | Exhaust route |
|---:|---:|---:|---:|---:|---:|
| 0 | 1 | 0° | 0° | 30° | 1 |
| 1 | 2 | 120° | 480° | 510° | 0 |
| 2 | 3 | 240° | 240° | 270° | 1 |
| 3 | 4 | 240° | 600° | 630° | 0 |
| 4 | 5 | 120° | 120° | 150° | 1 |
| 5 | 6 | 0° | 360° | 390° | 0 |

The ignition order is 1-5-3-6-2-4. The 30° difference between the authored ignition
origin and geometric piston TDC is real behavior of this asset: its crankshaft has
`tdc=120°` while its journal placement makes cylinder 1 geometric TDC occur at cycle
angle 30°. M3 preserves that discrepancy. It must not align geometric TDC to the
ignition angle in the name of cleanup.

Resolved dimensions and inertial data are:

| Quantity | Value |
|---|---:|
| Bore `B` | `0.084 m` |
| Stroke | `0.084 m` |
| Crank radius `r` | `0.042 m` |
| Connecting-rod length `l` | `0.135 m` |
| Deck height | `0.211 m` |
| Piston compression height | `0.03182 m` |
| Head chamber volume | `34*cc_source = 0.000034000000000000007 m3` per cylinder |
| Piston displacement term | `0 m3` |
| Piston mass | `0.280 kg` each |
| Connecting-rod mass | `0.300 kg` each |
| Connecting-rod inertia | `0.0015884918028487504 kg*m2` each |
| Crankshaft mass | `5 kg` |
| Flywheel mass | `5.9 kg` |
| Authored crank inertia | `0.22986844776863666 * 0.9 kg*m2` |
| Fixed crank friction magnitude | `10*lb_ft_source = 13.558174560000001 N*m` |

Using `pi_l`, the derived piston area, total analytic displacement, clearance, and
compression ratio are:

```text
A_p = pi_l*B*B/4.0
    = 0.005541769440932761 m2

V_d_total = 6*A_p*(2*r)
          = 0.0027930517982301117 m3

V_clear = 34*cc_source
          + A_p*(0.211 - (r*cos(0)+sqrt(l*l)) - 0.03182)
        = 0.00004608105738123328 m3 per cylinder

compression_ratio = (V_clear + A_p*2*r) / V_clear
                  = 11.10195207082927
```

The fixture manifest's source-computed displacement
`0.0027930477143328905 m3` came from the sampled constraint mechanism. It remains
provenance evidence but is not substituted into analytic volume, torque, BMEP, or
clearance calculations.

### 4.2 Gas volumes, geometry, and restrictions

There is one shared intake plenum, six runner volumes, six variable cylinders, six
primary volumes, and two collector volumes. All planar geometry directions are
`(dx,dy)=(1,0)`:

| Finite volume | Count | Volume | Width | Height |
|---|---:|---:|---:|---:|
| Intake plenum | 1 | `0.0020000000000000005 m3` | `0.1 m` | `0.20000000000000004 m` |
| Intake runner | 6 | `0.000478217176 m3` | `0.192694309637593 m` | `sqrt(0.00248174) m` |
| Cylinder | 6 | variable | `0.07444306173803412 m` | `0.18531522456363275 m` |
| Exhaust primary | 6 | `0.0010439073842126675 m3` | `0.7128642119089814 m` | `sqrt(0.0014643846145918652) m` |
| Collector | 2 | `0.05000000000000001 m3` | `6.167266379343297 m` | `sqrt(0.008107319665560499) m` |

The script units are themselves evaluated operations:

```text
cm_source = 1.0/100.0
mm_source = 1.0/1000.0
inch_source = cm_source*2.54
foot_source = inch_source*12
lbf_source = 4.44822
lb_ft_source = lbf_source*foot_source
cc_source = cm_source*cm_source*cm_source
litre_source = cc_source*1000
```

The runner area is `2*12.4087*(cm_source*cm_source) = 0.00248174 m2`.
Its volume is `100*cc_source + area*(6*inch_source)`. The primary area is
`pi_l*(0.85*inch_source)*(0.85*inch_source)`; its volume is
`300*cc_source + area*(20*inch_source)`. The collector area is
`pi_l*(2*inch_source)*(2*inch_source)`; its volume is `50*litre_source` and
its length is `volume/collector_area`. Source expressions, not shortened decimal
unit constants, own binary64 construction.

The cylinder's planar geometry is deliberately fixed even though its gas-state volume
changes. The source set that geometry once before mechanism placement, when piston
position was zero:

```text
cylinder_width = sqrt(A_p)
cylinder_geometry_height =
    (34*cc_source + A_p*(0.211-0.03182))/A_p
```

Self-impulse later uses
`depth=current_cylinder_volume/(fixed_width*fixed_height)`; it must not update the
stored height to `current_volume/A_p`.

Boundary-to-cell cross-sections and directions are part of the parity behavior:

| Edge, endpoint 0 -> endpoint 1 | Endpoint-0 area | Endpoint-1 area | Direction |
|---|---:|---:|---|
| Main/idle atmosphere -> plenum | `10 m2` | `0.01 m2` | `(0,-1)` |
| Plenum -> runner | `0.01 m2` | `0.00248174 m2` | `(1,0)` |
| Runner -> cylinder | `0.00248174 m2` | `V/(V/A_p)` | `(1,0)` |
| Cylinder -> primary | `V/(V/A_p)` | `0.0014643846145918652 m2` | `(1,0)` |
| Primary -> collector | `0.0014643846145918652 m2` | `0.008107319665560499 m2` | `(1,0)` |
| Outlet atmosphere -> collector | `0.008107319665560499 m2` | `10 m2` | `(1,0)` |

The last row deliberately preserves the source's endpoint/area association even
though the large area is attached to the collector endpoint. Intake and outlet
atmospheres are finite `1000 m3` work objects reset immediately before their edge;
they are not the distinct environment-flow overload used by blowby.

At the beginning of each chamber substep, compute
`cylinder_height=V/A_p` once, then compute both valve-edge cylinder areas as
`V/cylinder_height`. This is mathematically `A_p`, but replacing the two divisions
with that literal can alter jet-momentum rounding.

For calibration of a restriction coefficient, use the dedicated source
`flowConstant` operation order in section 8.3 at `298.15 K` and `101325 Pa`.
`k_28inH2O` uses a drop of
`28*(3386.3886666666713*0.0734824) Pa`; `k_carb` uses a drop of
`1.5*3386.3886666666713 Pa`. The normative inputs and binary64 audit values are:

| Use | Source expression | Audit value |
|---|---:|---:|
| Main throttle | `k_carb(500)` | `0.015925315712742586` |
| Idle bypass | `k_carb(0.1)` | `0.0000031850631425485175` |
| Plenum-to-runner | `k_carb(500)` | `0.015925315712742586` |
| Exhaust primary-to-collector | `k_carb(200)` | `0.006370126285097034` |
| Collector outlet | `k_carb(1000)` | `0.03185063142548517` |
| One source cfm at 28 in H2O | `k_28inH2O(1)` | `0.00002748668227937587` |
| Piston blowby | `k_28inH2O(0.1)` | `0.0000027486682279375876` |

The source expressions, evaluated in the declared arithmetic order, are normative;
the decimal audit values catch unit or conversion mistakes.

### 4.3 Throttle, valves, ignition, and fuel

At each outer step, requested throttle `u` resolves as:

```text
linkage = 1 - pow(u, 2)
plate = 0.994 * linkage
main_multiplier = cos(pi_l * plate / 2)
main_k = main_multiplier * k_carb(500)
idle_k = k_carb(0.1)
```

The source-observable resolved engine throttle is `linkage`, exactly
`1-pow(u,2)`. It is distinct from the intake plate position, main-flow multiplier,
and restriction coefficient; all four use those explicit names in capture or
diagnostics.

The main and idle restrictions remain parallel. Preserve construction rounding. With
fuel enabled:

```text
ideal_afr = (0.8*12.5)*4
main_air = ideal_afr/(1+ideal_afr)
main_mix =
    (1-main_air, main_air*0.75, main_air*0.25)

idle_afr = 2
idle_air = idle_afr/(1+idle_afr)
idle_mix =
    (1-idle_air, idle_air*0.75, idle_air*0.25)
```

These are mathematically `(1/41,30/41,10/41)` and `(1/3,1/2,1/6)`, but directly
writing those rationals can differ by an ULP. With fuel disabled, both paths construct
`air=1` and therefore `(0,0.75,0.25)`. Outlet and blowby environment mixtures are
`(0,1,0)`.

Both cams use `9 mm` maximum lift, `1.27 mm` reference lift, `210°` crank duration,
exponent `0.8`, 100 construction steps, zero advance, and a `0.6 inch` base-radius
datum which does not affect lift. The centers are formed in the source's
runtime-cylinder order:

```text
rot120 = 120*deg_source
rot360 = 360*deg_source
center_multiplier[0..5] = [0,4,2,5,1,3]
intake_base = rot360 + 110*deg_source
exhaust_base = rot360 - 105*deg_source
intake_center_i = intake_base + center_multiplier[i]*rot120
exhaust_center_i = exhaust_base + center_multiplier[i]*rot120
```

The journal angles are independently constructed as
`[0,120*deg_source,240*deg_source,240*deg_source,120*deg_source,0]`.
The ignition wire angles are formed from firing rank as
`(rank/6.0)*((2*360)*deg_source)` with ranks `[0,4,2,5,1,3]`; executable
code must not substitute a pre-collapsed degree table.

The intake flow table, in source cfm at lifts `0..12 mm`, is:

```text
0, 35, 60, 90, 125, 150, 175, 200, 215, 230, 235, 235, 238
```

The exhaust table is:

```text
0, 35, 55, 85, 105, 120, 140, 150, 155, 160, 165, 165, 165
```

Each abscissa is `lift_index*mm_source`, each ordinate is converted independently
with `k_28inH2O`, and both tables use a `1.0*mm_source` triangle radius.

Ignition timing is stored in source angular-speed units. Its triangle radius is
`1000*0.104719755 rad/s`; each table abscissa is
`rpm_point*0.104719755`, and the query is `-omega_legacy`. The ordinate points are
`(0,10*deg_source)`, `(1000,10*deg_source)`, and
`(2000..7000 in 1000-rpm steps,30*deg_source)`.
The ignition cut limit is `8000 rpm` with a `0.5 s` hold. The engine asset's separate
`7000 rpm` redline is UI/test metadata here and does not replace that cut limit.

The spark-ignition fuel parameters are:

| Quantity | Value |
|---|---:|
| Molecular mass | `0.100 kg/mol` |
| Energy density | `48.1e6 J/kg` |
| Molecular AFR parameter | `12.5` |
| Maximum burning efficiency | `0.8` |
| Burning-efficiency randomness | `0.5` |
| Low-efficiency attenuation | `0.6` |
| Maximum turbulence effect | `4` |
| Maximum dilution effect | `10` |
| LBV multiplier | `1` |
| Compression ignition | disabled |

## 5. Fresh state and ownership

The parity session constructs new state; reuse or warm-start from a previous render is
forbidden:

- every finite gas volume starts at `101325 Pa`, `298.15 K`, five degrees of freedom,
  mixture `(0,1,0)`, and zero planar momentum;
- each cylinder starts at the analytic volume for
  `theta_cycle_initial=120*deg_source`;
- both 256-element piston-speed and pressure histories start at zero;
- all flames start inactive with `lit_n=0`, `total_n=0`, `percentageLit=0`,
  `efficiency=1`, `flameSpeed=0`, `lastVolume=0`, `travel_x=travel_y=0`, and
  `globalMix=(0,1,0)`;
- flow/energy accumulators, peak temperature, and burned-fuel diagnostics start at
  zero;
- filtered RPM starts at zero;
- ignition saved angle starts at `theta_cycle_initial`, its limiter timer is zero,
  every wired plug starts `enabled=true`, and every one-step `ignitionEvent` flag
  starts false;
- the six combustion PCG32 streams are seeded from the exact pairs in the fixture
  manifest;
- all six propagation delays, the M2 renderer, its route RNG streams, filters,
  resamplers, convolution state, crop state, and output serializers start fresh.

Only the scenario owns crank motion. Starter/dyno flags in the first two seconds do
not instantiate starter, vehicle, transmission, dyno, or constraint-solver state.
They remain provenance fields while their captured RPM sequence supplies motion.
There is no hidden state shared by render sessions.

## 6. Analytic mechanism

For cylinder `i`, define:

```text
geometric_tdc_i =
    wrap_2pi(120*deg_source + journal_angle_i - pi_l/2)
phi_i = wrap_2pi(theta_cycle-geometric_tdc_i)
s_i = r*cos(phi_i) + sqrt(l*l - r*r*sin(phi_i)*sin(phi_i))
x_i = r + l - s_i
V_i = V_clear + A_p*x_i

dx_dtheta_i =
    r*sin(phi_i)
    + r*r*sin(phi_i)*cos(phi_i)
      / sqrt(l*l - r*r*sin(phi_i)*sin(phi_i))

dV_dtheta_i = A_p*dx_dtheta_i
piston_speed_abs_i = abs(dx_dtheta_i * omega)
```

All transcendental calls and products occur in the written order; a later optimized
form must first demonstrate parity. Piston phase derives from crank reference and
journal geometry, never ignition wiring; the 720° table in section 4.1 merely records
the geometric TDC representative associated with each firing event. Before each outer
step's gas substeps,
`setVolume(V_i)` applies section 8.2's work operation from the previous cylinder
volume. The 30° phase term is mandatory.

The M3 post-gas indicated torque definition is:

```text
tau_gas_i = (p_cylinder_i - 101325) * dV_dtheta_i
```

It is not fed back into prescribed motion. Section 12 closes the reported torque
sum and the source-dyno limitation.

## 7. Discrete valvetrain

Construct one intake and one exhaust lobe table. With
`L=9.0*mm_source`, `L50=50.0*(inch_source/1000.0)`,
`gamma_lobe=0.8`, `D50=210.0*deg_source`, and `N=100`:

```text
a = D50 / 4.0
q = pow(2.0*L50/L, 1.0/gamma_lobe) - 1
k = acos(q) / a
extent = pi_l / k
radius = extent / (N - 5.0)

insert (0,L)
for i = 1..N-1:
    x = i*radius
    y = 0
        if x >= extent
        else L*pow(0.5 + 0.5*cos(k*x), gamma_lobe)
    insert (+x,y)
    insert (-x,y)
sort by x as insertion requires
```

The result has exactly 199 samples. Do not discard the four zero-lift pairs outside
the analytic extent. For any frozen table with radius `r_f`, triangle sampling:

- returns the first ordinate for `x <= first_x`;
- returns the last ordinate for `x >= last_x`;
- otherwise finds the closest sample, choosing the upper sample on an exact-distance
  tie; traverses from that sample down toward index zero, accepting only samples at
  or below `x`, and then from that sample up toward the final index, accepting only
  samples strictly above `x`;
- each traversal stops once `abs(x_j-x)>r_f`; for every accepted sample it evaluates
  `w_j=(r_f-abs(x_j-x))/r_f`, performs `sum += w_j*y_j`, then
  `total_weight += w_j`, in that exact order;
- returns `sum/total_weight`;
- returns zero only if the interior total weight is zero.

For outer step signed legacy body angle `psi` from section 3.1 and authored crank
center `center_crank_i`:

```text
crank_get_angle = psi - 120*deg_source
cam_base = fmod((crank_get_angle + 0)*0.5,2*pi_l)
if cam_base < 0:
    cam_base += 2*pi_l

stored_lobe_angle_i = center_crank_i/2
lobe_argument =
    wrap_to_minus_pi_inclusive(cam_base + stored_lobe_angle_i)
lift = triangle_sample(lobe_table, lobe_argument, radius)
valve_k = triangle_sample(port_table, lift, 0.001)
```

The port-table radius written as `0.001` is exactly `1.0*mm_source`.

`wrap_to_minus_pi_inclusive` first wraps to `[0,2*pi_l)`, then subtracts
`2*pi_l` when the result is greater than or equal to `pi_l`, producing
`[-pi_l,pi_l)`. Intake and exhaust `valve_k` are sampled once per outer step after
volume work and remain constant for all eight gas substeps.

## 8. Low-order gas kernel

### 8.1 State and derived quantities

A finite volume owns this state:

```text
n_mol
U_thermal_j
V_m3
momentum_x_kg_m_s
momentum_y_kg_m_s
X_fuel, X_inert, X_o2
degrees_of_freedom = 5
```

The three molar fractions have a mathematical sum of one at initialization and after
admitted transfer or reaction operations. Each stored fraction is nevertheless
computed independently; a post-operation normalization pass is forbidden because its
binary64 rounding would change parity. Products of combustion are folded into
`X_inert`; no separate
CO2, H2O, N2, dissociation, or residual species exist. With `f=5`:

```text
gamma = 1.0 + (2.0/f)
p_static = U_thermal / (0.5*f*V)
T = U_thermal / (0.5*f*n*R)
m = M_air*n
rho = (M_air*n)/V
velocity_x = momentum_x/m
velocity_y = momentum_y/m
velocity_squared = velocity_x*velocity_x + velocity_y*velocity_y
B_bulk = 0.5*m*velocity_squared
c = sqrt(gamma*p_static/rho)
```

Pressure contains thermal energy only. `B_bulk` is not added to pressure or
temperature. A zero amount returns zero for source-era diagnostic temperature,
velocity, sound speed, and total energy, but a zero amount or energy used as a flow
divisor/upstream state is an M3 nonphysical-state failure rather than an invitation
to continue through undefined arithmetic.

For a unit query direction `(dx,dy)`, directional dynamic pressure evaluates in this
exact order and is zero when `v_dir <= 0`:

```text
inverse_mass = 1/m
v_dir = inverse_mass*(dx*momentum_x + dy*momentum_y)
rho = (M_air*n)/V
c_squared = p_static*gamma/rho
mach_squared = v_dir*v_dir/c_squared
z = 1 + ((gamma-1)/2)*mach_squared
z2 = z*z
z3 = z2*z
z7 = z3*z3*z
q(dx,dy) = p_static*(sqrt(z7)-1)
```

The ordered `z2,z3,z7,sqrt` evaluation is normative; do not replace it with a
general `pow(z,3.5)` in M3.

### 8.2 Local thermal, amount, and volume operations

Initialization at `(P,V,T,X)` evaluates in this order:

```text
n = P*V/(R*T)
U = T*(0.5*f*n*R)
momentum_x = momentum_y = 0
```

Reset repeats those assignments while retaining the volume and geometry. To change
volume by `dV`, preserve the source's operation order:

```text
V_old = V
L = pow(V_old + dV, 1/3.0)
A = L*L
dL = -dV/A
W = dL*p_static_old*A
V = V_old + dV
U = U + W
```

Although this is algebraically close to `-p*dV`, reassociation changes rounding and
is not the M3 operation.

Removing `dn` at supplied per-mole thermal energy `e` performs `U-=e*dn`,
`n-=dn`, then floors a negative `n` to zero. Adding `dn` performs `U+=e*dn`,
sets `n_new=n_old+dn`, then independently assigns, in fuel/inert/O2 order:

```text
X_j = (X_j*n_old + dn*X_incoming_j)/n_new
```

If `n_new==0`, all fractions become zero. M3 validates the state after the complete
named operation and does not add other generic floors.

### 8.3 Reversible restriction rate

The exact signed helper accepts ordered endpoint pressures and temperatures:

```text
flow_rate(K,P0,P1,T0,T1):
    if K == 0:
        return 0

    if P0 > P1:
        direction = +1
        T_u = T0
        P_u = P0
        P_d = P1
    else:
        direction = -1
        T_u = T1
        P_u = P1
        P_d = P0

    r = P_d/P_u

    if r <= r_c:
        q = choked_factor
        q /= sqrt(R*T_u)
    else:
        s = pow(r,1/gamma)
        q = (2*gamma)/(gamma-1)
        q *= s*(s-r)
        q = sqrt(fmax(q,0.0)/(R*T_u))

    q *= direction*P_u
    return q*K
```

Here:

```text
r_c = pow(2/(gamma+1),gamma/(gamma-1))
choked_factor =
    sqrt(gamma)
    * pow(2/(gamma+1),(gamma+1)/(2*(gamma-1)))
```

An enclosing edge has already evaluated directional pressures and selected its
endpoints before `K==0` short-circuits this helper. Equal endpoint pressure takes the
endpoint-1 branch and produces signed negative zero. M3 does not reinterpret this
molar-flow relation as mass flow or effective area.

The dedicated calibration helper uses:

```text
P0 = 101325
PT = P0-pressure_drop
r = PT/P0

if r <= r_c:
    q = sqrt(gamma)
    q *= pow(2/(gamma+1),(gamma+1)/(2*(gamma-1)))
else:
    q = (2*gamma)/(gamma-1)
    q *= 1-pow(r,(gamma-1)/gamma)
    q = sqrt(q)
    q *= pow(r,1/gamma)

q *= P0/sqrt(R*298.15)
K = target_source_scfm_mol_s/q
```

### 8.4 Finite-volume-to-finite-volume flow

For endpoint 0 to endpoint 1 with declared direction `d=(dx,dy)`, first compute:

```text
P0 = p0 + q0(dx,dy)
P1 = p1 + q1(-dx,-dy)
```

Endpoint 0 becomes source only when `P0>P1`; endpoint 1 wins the equality tie. When
endpoint 1 wins, reverse `d`, swap the endpoint cross-sections, and remember a return
sign of `-1`; otherwise the return sign is `+1`. Evaluate section 8.3 with source
first:

```text
raw_dn = hg*molar_rate
dn = clamp(raw_dn, 0, 0.9*n_source_pre)
fraction = dn/n_source_pre
fraction_volume = fraction*V_source
fraction_mass = fraction*m_source_pre
```

The legacy source also evaluated a pressure-equilibrium candidate, but never applied
it. It is not an M3 bound and need not be retained as dead computation. The 90%
source-amount clamp above is the sole transfer-amount bound for this overload.

When `dn != 0`, the first transfer stage is:

1. Save source and sink bulk kinetic energies.
2. Add `dn` to the sink using source `U/n` and source mixture; remove it from source
   using that same `U/n`.
3. Compute `delta_p = fraction*source_momentum` after the amount transfer, subtract
   it from source momentum, and add it to sink momentum.
4. Recompute both bulk kinetic energies and perform exactly
   `sink_U -= ((B_source_after+B_sink_after)
   -(B_source_before+B_sink_before))`.

Then cache, in this order, source mass and inverse mass, sink mass and inverse mass,
both sound speeds, source x/y momentum, and sink x/y momentum. Apply jet momentum to
the sink first, then the source:

```text
if sink_area != 0:
    jet_speed = clamp((fraction_volume/sink_area)/hg,0,c_sink)
    jet_velocity_x = jet_speed*dx
    jet_velocity_y = jet_speed*dy
    jet_momentum_x = jet_velocity_x*fraction_mass
    jet_momentum_y = jet_velocity_y*fraction_mass
    sink_momentum_x += jet_momentum_x
    sink_momentum_y += jet_momentum_y

if source_area != 0 and source_mass != 0:
    jet_speed = clamp((fraction_volume/source_area)/hg,0,c_source)
    jet_velocity_x = jet_speed*dx
    jet_velocity_y = jet_speed*dy
    jet_momentum_x = jet_velocity_x*fraction_mass
    jet_momentum_y = jet_velocity_y*fraction_mass
    source_momentum_x += jet_momentum_x
    source_momentum_y += jet_momentum_y
```

The same signed directional impulse is added to source and sink; it is not an
equal-and-opposite pair. This is intentional parity behavior. Reconcile source
thermal energy first, x then y, followed by sink thermal energy, x then y:

```text
if source_mass != 0:
    source_v0x = source_initial_momentum_x*inv_source_mass
    source_v0y = source_initial_momentum_y*inv_source_mass
    source_v1x = source_momentum_x*inv_source_mass
    source_v1y = source_momentum_y*inv_source_mass
    source_U -= 0.5*source_mass
                *(source_v1x*source_v1x-source_v0x*source_v0x)
    source_U -= 0.5*source_mass
                *(source_v1y*source_v1y-source_v0y*source_v0y)

if sink_mass > 0:
    sink_v0x = sink_initial_momentum_x*inv_sink_mass
    sink_v0y = sink_initial_momentum_y*inv_sink_mass
    sink_v1x = sink_momentum_x*inv_sink_mass
    sink_v1y = sink_momentum_y*inv_sink_mass
    sink_U -= 0.5*sink_mass*(sink_v1x*sink_v1x-sink_v0x*sink_v0x)
    sink_U -= 0.5*sink_mass*(sink_v1y*sink_v1y-sink_v0y*sink_v0y)
```

Finally floor negative sink thermal energy to zero, then negative source thermal
energy to zero. Return `dn*direction_sign`, meaning positive is endpoint 0 to
endpoint 1.

This staged, asymmetric reconciliation is not asserted to conserve a physical
control-volume energy balance. Reordering it or replacing it with a conservative flux
is a later model change.

### 8.5 Finite-volume-to-environment flow

Blowby alone uses a separate overload. It compares the cylinder's static pressure to
an infinite environment without directional pressure or injected momentum. Its
static-pressure-equilibrium amount is:

```text
if p_system > P_env:
    max_dn =
        -(P_env*(0.5*f*V)-U)/(U/n)
else:
    e_env = 0.5*T_env*R*f
    max_dn =
        -(P_env*(0.5*f*V)-U)/e_env
```

Evaluate section 8.3 from system to environment; positive signed `dn` is outflow.
If `abs(raw_dn)>abs(max_dn)`, replace it with `max_dn`.

- For inflow (`dn<0`), save `B_before`, add `-dn` with `e_env` and the environment
  mixture without adding momentum, compute `B_after`, then perform the legacy
  `U += B_after-B_before`. The likely sign defect is part of parity.
- For outflow (`dn>=0`), save `n_before`, remove `dn` with the system's current
  `U/n`, then perform the following two independent, in-place assignments:

  ```text
  momentum_x -= (dn/n_before)*momentum_x
  momentum_y -= (dn/n_before)*momentum_y
  ```

This overload adds no further bulk-energy reconciliation or thermal-energy floor.
The post-operation M3 validity check still applies.

### 8.6 Sonic bound and momentum evolution

The named excess-velocity operation first evaluates
`vx=momentum_x/m`, `vy=momentum_y/m`,
`velocity_squared=vx*vx+vy*vy`, `c`, then `c_squared=c*c`. If
`c_squared>=velocity_squared` or velocity is zero it returns. Otherwise:

```text
k_squared = c_squared/velocity_squared
k = sqrt(k_squared)
momentum_x *= k
momentum_y *= k
U += 0.5*m*(velocity_squared-c_squared)
if U < 0:
    U = 0
```

For a volume with geometry `(width,height,dx,dy)`, the directional self-impulse is:

```text
depth = V/(width*height)
q0 = q( dx, dy)
q1 = q(-dx,-dy)
q2 = q( dy, dx)
q3 = q(-dy,-dx)

F0 = q0*(height*depth)
F1 = q1*(height*depth)
F2 = q2*(width*depth)
F3 = q3*(width*depth)

dpx = 0
dpy = 0
dpx += F0*dx
dpy += F0*dy
dpx -= F1*dx
dpy -= F1*dy
dpx += F2*dy
dpy += F2*dx
dpx -= F3*dy
dpy -= F3*dx

momentum_x -= dpx*hg*beta
momentum_y -= dpy*hg*beta
```

Immediately before changing momentum, evaluate `inverse_mass=1/m`,
`vx0=momentum_x*inverse_mass`, and `vy0=momentum_y*inverse_mass`. After both momentum
changes evaluate `vx1` and `vy1` with the same inverse, then perform:

```text
U -= 0.5*m*(vx1*vx1-vx0*vx0)
U -= 0.5*m*(vy1*vy1-vy0*vy0)
if U < 0:
    U = 0
```

This operation uses only the cell's own directional dynamic pressures. It is not a
static-pressure-gradient wave equation.

The named linear velocity decay with time constant `tau` is:

```text
inverse_mass = 1.0/mass()
vx0 = momentum_x*inverse_mass
vy0 = momentum_y*inverse_mass
velocity_squared_0 = vx0*vx0+vy0*vy0
s = hg/(hg+tau)
momentum_x = momentum_x*(1-s)
momentum_y = momentum_y*(1-s)
vx1 = momentum_x*inverse_mass
vy1 = momentum_y*inverse_mass
velocity_squared_1 = vx1*vx1+vy1*vy1
delta_U = 0.5*mass()*(velocity_squared_0-velocity_squared_1)
U += delta_U
```

It thermalizes the lost planar bulk energy.

## 9. Exact gas-substep schedule

The P1.8 capture was built with natural-gas resonance disabled. Therefore all
specified linear-decay calls are active. For each of the eight gas substeps, execute
the following without parallel reordering.

At the beginning of each outer step, zero one signed molar-transfer accumulator for
every distinct edge identity:

- main atmosphere-to-plenum and idle atmosphere-to-plenum;
- each of six plenum-to-runner, runner-to-cylinder, cylinder-to-primary, and
  primary-to-collector edges;
- each of two outlet-atmosphere-to-collector edges;
- each of six cylinder-to-crankcase blowby edges.

Immediately after every flow call below, add its returned signed `dn` to that edge's
accumulator. The finite-volume edges use the endpoint-0-to-endpoint-1 orientations in
section 4.2. Blowby uses positive cylinder-to-crankcase orientation. Accumulation is
diagnostic-only, occurs in the same scalar call order shown below, and never feeds
physics.

### 9.1 Collectors

For collector 0, then collector 1:

1. Reset its `1000 m3` outlet-atmosphere work volume to
   `101325 Pa`, `298.15 K`, mixture `(0,1,0)`, and zero momentum.
2. Evaluate the outlet-atmosphere/collector two-volume edge with
   `k_carb(1000)` and the exact endpoint areas in section 4.2.
3. Apply the collector excess-speed bound.
4. Apply collector self-impulse with `beta=1`.

### 9.2 Shared intake

1. Form the fuel-enabled or fuel-disabled main mixture from section 4.3.
2. Reset the `1000 m3` intake-atmosphere work volume with that mixture.
3. Evaluate the main atmosphere/plenum edge with the current `main_k`.
4. Form the idle mixture and reset the same atmosphere work volume again.
5. Evaluate the idle atmosphere/plenum edge with `idle_k`.
6. Apply the plenum excess-speed bound.
7. Apply plenum self-impulse with `beta=0.1`.

### 9.3 Cylinders

For cylinders 0 through 5:

1. Update the peak-temperature diagnostic from the pre-heat temperature.
2. Apply cylinder wall heat from section 10.1.
3. Apply blowby through the environment overload.
4. Evaluate the plenum-to-runner edge with `k_carb(500)`.
5. Apply the runner excess-speed bound.
6. Evaluate the runner-to-cylinder intake-valve edge with the outer-step valve
   conductance.
7. Apply the runner excess-speed bound, then the cylinder bound.
8. Evaluate the cylinder-to-primary exhaust-valve edge.
9. Apply the cylinder excess-speed bound, then the primary bound.
10. Evaluate the primary-to-associated-collector edge with `k_carb(200)`.
11. Apply self-impulse to runner with `beta=0.1`, cylinder with `beta=0.5`, then
    primary with `beta=1`.
12. Apply linear velocity decay to cylinder, then primary, both with
    `tau=0.01 s`.
13. If the absolute signed intake-valve transfer exceeds
    `1e-9 mol` for this gas substep, extinguish any active flame.
14. Store the latest signed exhaust-valve transfer; the intake/exhaust edge
    accumulators were updated immediately after their corresponding calls.
15. Advance combustion from section 10.6.

No sonic bound follows the three self-impulse calls in the same substep. No runner
linear decay exists. No plenum, runner, primary, or collector wall heat exists.

## 10. Ignition, heat, and combustion

### 10.1 Wall heat and blowby

At the beginning of each chamber gas substep:

```text
cylinder_height = V/A_p
wall_area = cylinder_height*pi_l*B + A_p*2
Q_to_gas =
    (363.15-T)*wall_area*100*hg
U += Q_to_gas
```

This thermodynamic cylinder height is distinct from the fixed planar-geometry height
in section 4.2. Then apply the section 8.5 blowby operation using
`k_28inH2O(0.1)`, `P_env=101325 Pa`, `T_env=298.15 K`, and mixture `(0,1,0)`.

### 10.2 Ignition crossing and limiter order

Let the timing advance be the section 4.3 triangle table evaluated at positive
running RPM and converted to radians. For plug `i`:

```text
spark_angle_i =
    wrap_4pi(ignition_angle_i - timing_advance)
```

Preserve the source's branch on signed legacy crank velocity exactly:

```text
saved = saved_angle
current = theta_cycle
spark = spark_angle_i

if omega_legacy < 0:
    if current < saved:
        current += 4*pi_l
        spark += 4*pi_l
    fire iff spark >= saved and spark < current
else:
    if current > saved:
        current -= 4*pi_l
        spark -= 4*pi_l
    fire iff spark >= current and spark < saved
```

Thus this source-parity interval is `[saved,current)`: equality at the saved endpoint
fires after motion begins, while equality at the new endpoint waits. For reverse
or exact-zero legacy velocity the interval is `[current,saved)`. The unconditional
spark shift whenever the wrapped endpoint crosses is part of the algorithm; do not
replace it with a mathematically normalized interval helper. Stable cylinder order
resolves multiple crossings.

Each outer step performs limiter work in this exact order:

1. If ignition is enabled and `limiter_timer==0`, test all crossings.
2. Subtract `h` from `limiter_timer`.
3. If absolute legacy crank velocity is greater than the `8000 rpm` limit, assign
   `limiter_timer=0.5 s`.
4. If the timer is negative, set it to zero.
5. Save the current wrapped cycle angle.

The first overspeed tick can therefore spark. Sustained overspeed continually
refreshes the cut, and a timer that crosses through zero remains cut for that tick.
Ignition disabled still updates limiter and saved-angle state.

The crossing flags are consumed in cylinder order immediately after this update.
For an asserted flag, call ignition before changing cylinder volume or history for the
outer step. The flame snapshot's geometric `last_volume` nevertheless uses the
current post-mechanism piston position; its gas pressure, temperature, composition,
and amount are still the previous outer step's thermodynamic state. Existing flame,
zero fuel, or mixture-gate rejection leaves state unchanged and consumes no random
draw.

### 10.3 Deterministic combustion random stream

Each cylinder uses its fixture seed pair `(initial_state,stream)` verbatim. Reject
`stream > 2^63-1`. PCG32 initialization and output are:

```text
state = 0
increment = (stream << 1) | 1
next_u32()
state += initial_state  # modulo 2^64
next_u32()

next_u32:
    old = state
    state = old*6364136223846793005 + increment  # modulo 2^64
    xorshifted = uint32(((old >> 18) ^ old) >> 27)
    rotation = uint32(old >> 59)
    return (xorshifted >> rotation)
           | (xorshifted << ((-rotation) & 31))
```

The return expression is evaluated in unsigned 32-bit arithmetic.

One accepted ignition obtains exactly one binary64 uniform:

```text
hi = next_u32() >> 5
lo = next_u32() >> 6
u01 = double((hi << 26) | lo) * 2^-53
```

It therefore advances PCG32 twice. No other M3 physical operation uses these six
streams.

### 10.4 Ignition snapshot and flame speed

Ignition is eligible only when no flame is active, `X_fuel != 0`, and:

```text
afr_molar = X_o2/X_fuel
equivalence_source = afr_molar/12.5
0.5 <= equivalence_source <= 1.9
```

The source name is retained even though this orientation is not conventional
equivalence ratio. On acceptance, snapshot current mixture as `global_mix`, current
amount as diagnostic `total_n`, current geometric volume as `last_volume`, set both
travel coordinates and progress counters to zero, and mark the flame active.

Mean piston speed is the ordered sum of all 256 absolute-speed history entries divided
by 256. Convert it to turbulence with a triangle radius of `1 m/s` over points
`(i,0.5*i)` for integer `i=0..29`. Define:

```text
ideal_inert = X_o2/0.7
dilution = X_inert/ideal_inert - 1
mixing =
    1
    - clamp(turbulence/4,0,1)
      * clamp(1-dilution/10,0,1)
random_attenuation = 0.6*(0.5 + 0.5*u01)
efficiency =
    0.8*(mixing*random_attenuation + (1-mixing))
```

The snapshotted laminar burning speed is:

```text
er = afr_molar/12.5
alpha = 2.4 - 0.271*pow(er,3.51)
beta = -0.357 + 0.14*pow(er,2.77)
S_L0 = 0.305 + (-0.549)*(er-1.21)*(er-1.21)
S_L = S_L0
S_L *= pow(T/298,alpha)
S_L *= pow(p_static/101325,beta)
```

Note the exact `298 K` reference, not ambient `298.15 K`. Final flame speed is `S_L`
times triangle interpolation, radius 5, queried at `turbulence/S_L` over:

```text
(0,3), (5,7.5), (10,15), (15,22.5), (20,30),
(25,37.5), (30,45), (35,52.5), (40,60), (45,67.5)
```

The current maximum of the 256 pressure-history bins and a fixed 160 psi motoring
argument are evaluated and passed in the historical call, but the admitted flame-speed
function ignores both by using a pressure adjustment of exactly one. M3 does not
invent the absent dependence.

### 10.5 History update

After any ignition call, apply section 8.2's cylinder volume work. Then compute:

```text
history_index =
    round_ties_away_from_zero(theta_cycle/(4*pi_l)*255)
```

At that index, overwrite absolute analytic piston speed and current static cylinder
pressure. The ignition just evaluated therefore saw the previous history contents.
The arrays are angle-indexed overwrite histories, not moving time windows.

### 10.6 Geometric flame advance and reaction

After heat, blowby, all four chamber edges, momentum operations, damping, and the
intake-flow cancellation check, advance an active gasoline flame:

```text
max_x = B/2
max_y = V/A_p
expansion = V/last_volume
old_x = travel_x
old_y = travel_y*expansion

travel_x = min(old_x + hg*flame_speed, max_x)
travel_y = min(old_y + hg*flame_speed, max_y)
```

If neither coordinate advances, extinguish the flame. Otherwise:

```text
burned_volume =
    travel_x*travel_x*pi_l*travel_y
previous_burned_volume =
    old_x*old_x*pi_l*old_y
delta_flame_volume = burned_volume-previous_burned_volume
n_swept = (delta_flame_volume/V)*n_current
n_reaction_request = n_swept*efficiency
```

Reaction uses the snapshotted `global_mix` to form requested species but limits
against the current cylinder species:

```text
requested_fuel = global_mix.fuel*n_reaction_request
requested_o2 = global_mix.o2*n_reaction_request

burned_fuel =
    fmin(fmin(current_fuel,requested_fuel),(2.0/25.0)*requested_o2)
burned_o2 =
    fmin(fmin(current_o2,requested_o2),(25.0/2.0)*requested_fuel)

reactants = burned_fuel+burned_o2
products = ((16.0+18.0)/(25.0+2.0))*reactants
delta_n = products-reactants
```

Subtract burned fuel/O2, add all products to inert, and perform
`n_current += delta_n`. If the new current amount is nonzero, assign each fraction
independently as its new species amount divided by that new amount, in
fuel/inert/O2 order. If it is exactly zero, assign all three fractions zero. Reaction
does not rescale planar momentum or change volume; a velocity-preserving momentum
adjustment is forbidden. Do not add a final normalization pass. Released energy is:

```text
Q_release =
    burned_fuel * 0.100 * 48.1e6 J
U += Q_release
```

Accumulate `Q_release`, unattenuated `n_swept`, burned fuel mass, and
`delta_flame_volume/V` in their corresponding diagnostic/progress fields. Finally set
`last_volume=V`. The snapshotted diagnostic `total_n` does not control later burn
amounts. Auto-ignition, diesel premixed/diffusion branches, knock, crevice burning,
and afterburning are absent.

## 11. Capture and frozen excitation handoff

After all eight gas substeps, publish one post-step capture sample. At minimum, the M3
capture carries:

- integer sample/step identity, exact rational time meaning, wrapped and diagnostic
  unwrapped crank angle, RPM, angular speed/acceleration, requested throttle
  `u`, resolved engine throttle `linkage`, plate position, main-flow multiplier,
  ignition/fuel state, limiter state, and the event journal below;
- per-cylinder volume, `dV/dtheta`, piston speed, valve lift/conductance, flame state,
  released energy, cylinder pressure/temperature/amount/composition, and indicated
  torque;
- per-edge signed outer-step-average flow in the declared source-to-sink direction;
- plenum, runner, primary, and collector static pressure, temperature, amount,
  composition, planar momentum, and directional pressure where required;
- separately named instantaneous incomplete modeled-net and friction quantities,
  plus explicit unavailable validity for cycle-derived and actuator torque.

The bounded block contract may represent source molar transfer internally for parity,
but its public physical flow is
`signed_mass_flow_kg_s = signed_outer_step_mol*M_air/h`; identity and direction are
mandatory.

The M3 event journal is append-only within an outer step and contains only these
records. Its fixed capacity is 19 records: at most six spark crossings, one limiter
transition, six ignition results, and six flame extinctions. Overflow is a contract
failure, never truncation or allocation:

| Event | Append point and required payload |
|---|---|
| `spark_crossing` | During ignition testing, in cylinder order, when an enabled plug sets its one-step flag. Record cylinder ID, raw saved/current wrapped angles, adjusted current and spark angles used by the branch, and timing advance. |
| `limiter_state_changed` | After timer subtraction, overspeed refresh, and zero floor, only when cut-active state changes. Record old/new active state, resulting timer, and whether overspeed refreshed it. |
| `ignition_result` | Immediately when the flagged cylinder is handled. Record cylinder ID and exactly one result: `accepted`, `rejected_active_flame`, `rejected_no_fuel`, `rejected_mixture_low`, or `rejected_mixture_high`; an accepted result also records efficiency and flame speed. |
| `flame_extinguished` | At the operation that clears an active flame. Record cylinder ID, gas-substep index, and exactly one reason: `intake_transfer` or `no_geometric_progress`. |

Records preserve actual execution order: spark crossings, then any limiter transition,
then cylinder-order ignition results, then gas-substep/cylinder-order flame
extinctions. Scenario control changes remain typed scenario state, not duplicate M3
events. Skipped crossing tests, repeated limiter refresh without an active-state
transition, and rejected ignition attempts do not create other implicit event kinds.
Here limiter cut-active means `limiter_timer>0`, independently of global ignition
enable. Ignition-result classification checks active flame, zero fuel, mixture below
the lower bound, and mixture above the upper bound in that order before `accepted`.
Gas-substep indices are `0..7`. Journal construction never feeds physics.

The narrow `reference_parity` extension contains exactly the comparator inputs needed
by the frozen excitation: filtered engine RPM plus, for each cylinder, primary static
pressure and forward/reverse directional dynamic pressure. Evaluate:

```text
a = min(abs(filtered_rpm),40)/40
x_i =
    a*a*a*1600
    * (primary_static_i-101325
       + 0.1*q_primary_i(+1,0)
       + 0.1*q_primary_i(-1,0))
```

Zero both buses. Feed `x_i` to that cylinder's already accepted 180-sample source-rate
delay, then accumulate cylinders 0 through 5 into their manifest route:

```text
route_term =
    sound_attenuation_i
    * ((route_audio_volume_i*delayed_x_i)/6)
    * (1.0/(6.167266379343297*6.167266379343297))
bus[route_i] += route_term
```

All sound attenuations are one. Route 0 has volume `0.5` and stable cylinder IDs
2,4,6; route 1 has volume `1` and IDs 1,3,5. Delay semantics, exact renderer
partition, presentation RNG, filtering, IR, crop, calibration, stems, and master are
owned by the frozen fixture and `P18_PRESENTATION_RENDERER.md`. No gain, noise, IR,
or normalization may be changed while diagnosing M3.

The capture is an output of simulated state. Neither the core nor excitation stage
links the fixture audit reader. Comparator tooling may read candidate capture and
fixture evidence side by side but may not feed evidence back into the render.

## 12. Torque and claim boundary

After all eight gas substeps, sum `tau_gas_i` from section 6 in cylinder order. At
positive running angular speed:

```text
tau_crank_friction = -13.558174560000001 N*m
```

At negative speed its sign reverses; at exact zero it is zero. M3's reported
incomplete modeled-net shaft torque is the gas sum plus this fixed crank-friction
term, and its physical-net completeness flag is false. No piston-wall term is
invented: the source's default piston friction depended on the previous constraint
solver's cylinder-wall reaction, which the analytic mechanism intentionally removed.
Starter, bearing, ring-pack, valvetrain, pump, oil, and accessory losses are also
absent and declared as such.

The M3 parity method does not yet define an equivalent-inertia function or its
derivative. Its prescribed-motion actuator torque is therefore reported unavailable
with that reason, never as zero and never as source dyno reaction. A later
non-prescribed scenario must close those mechanics before using the residual equation
in `MODEL.md`. Full-cycle `p*dV` already contains pumping and must not be debited a
second time. M3 does not claim source dyno-torque parity, a calibrated BMW torque
curve, or production-ready power.

M3 also marks cycle-integrated torque, work, BMEP, and power unavailable. The parity
scenario needs only the instantaneous traces, and this method does not invent
cycle-boundary interpolation or quadrature rules. A later method may admit those
quantities only after specifying their exact cycle segmentation and integration.

## 13. Verification and stop conditions

### 13.1 Before gas implementation

The mechanism-only executable gate must demonstrate:

- the complete fixture RPM sequence reconstructs all 170,000 wrapped angles from the
  declared initial angle with maximum circular error no greater than `1e-12 rad` in
  the pinned numerical environment;
- each cylinder reaches analytic minimum/maximum volume at its declared geometric TDC
  and BDC, with the mandatory 30° phase distinction intact;
- analytic `dV/dtheta` agrees with a centered finite-difference diagnostic away from
  dead centers, and has absolute magnitude no greater than `1e-14 m3/rad` at both
  dead centers;
- all valve centers, ignition angles, cylinder identities, and routes match the
  resolved tables.

Failure stops before valves or gas are layered on.

### 13.2 Before a listening candidate

Simulate all 170,000 records from fresh state. Before rendering through M2, publish a
diagnostic comparison for:

- filtered RPM and ignition/event alignment;
- every cylinder's primary static, forward-dynamic, and reverse-dynamic pressure;
- each pre-delay and post-delay cylinder excitation;
- both pre-DSP route buses;
- nonfinite/invalid-state count, bounds exercised, and route activity.

The report includes unscaled traces, timing error, DC, peak/RMS, normalized error,
correlation, and explicitly unmatched fields. These metrics diagnose implementation
and the known analytic-mechanism departure; they do not certify sound. A phase error,
missing route, collapsed directional term, nonfinite state, unexplained order
violation, or gross excitation mismatch blocks downstream rendering. Presentation
tuning is forbidden as a remedy.

Only after that report passes review is the unchanged M2 renderer allowed to produce
the canonical candidate. Supply raw and separately labelled level-matched
oracle/candidate files, both stems, full mix, hashes, routing, and elapsed/performance
report, then stop for the user's listening decision. Rejection blocks M4/M5 work;
there is no “later phase will fix it” waiver.

The standard 15-second audible clip is expected to remain near the project's
approximately 30-second budget. A single render over 60 seconds is a performance
failure requiring diagnosis or explicit user approval before proceeding. Performance
optimization cannot change arithmetic, ordering, or the model ID unnoticed.

### 13.3 Runtime failures

Only the named 90% amount clamp, environment equilibrium clip, amount floor, thermal
floors, sonic bound, table endpoints, and limiter bounds above are admitted. Abort
transactionally with the typed `MODEL.md` error and full step/component context on:

- fixture ID, schema, count, seed, route, or hash mismatch;
- forbidden audit/prebuilt-bus dependency;
- nonfinite input, intermediate, state, capture, or output;
- nonpositive required volume, amount, energy, temperature, pressure, area, or
  divisor;
- negative amount or composition after a complete admitted operation;
- table/order/identity/schedule mismatch;
- renderer/config identity differing from the accepted M2 artifact.

No recovery oscillator, noise bed, alternate physics, old binary, second model path,
or silent parameter clamp is allowed.

## 14. Source-content ledger

The implementation is governed by this document and the frozen fixture, not by a
mutable source checkout. For audit, the following SHA-256 values are hashes of file
content at effective source commit
`9617562a7a5615c2bf84c9ec39cd5ae25c560059`:

| Source path | SHA-256 | Observed semantic ownership |
|---|---|---|
| `include/constants.h` | `8117063bfe8d22bcf89bc43a25d07aad639ec13495cfbb49240169007514a6d8` | Legacy `pi` and `R` |
| `include/units.h` | `9f45afc2e31dc179fdabc7494cdc17d4cdc4b9440392818f182aaf88f264227e` | RPM, pressure, SCFM, molar-mass conversions |
| `include/gas_system.h` | `1e9216f8f970f88a1a639dd7505c1f02b2f97f549e98fde63000bb6ef08c3d6d` | Gas state and derived quantities |
| `src/gas_system.cpp` | `3b56c3b40b7302f5b72ceb0cf4c03b217012ac5bde5814e419a4e0f933172343` | Gas mutations, restrictions, flow, momentum |
| `include/combustion_chamber.h` | `19309c31477ea593169a12d26448d8866b44604a71733f2df269b204c4fd9b52` | Flame/history state |
| `src/combustion_chamber.cpp` | `0eb3289bffca9e46aa2d8bdfa76487edafb0a53b5f55bb34724b50cd2b985b00` | Chamber update, heat, flow, combustion |
| `include/ignition_module.h` | `4e148ab76353ebd3766cee44f8c1716a77f7b98442047ae05a6b91c87179537e` | Plug and limiter state |
| `src/ignition_module.cpp` | `b926af3b359fc622410c88611c9c135fd003ee4946e94f388b3f37307c5f463e` | Crossing and limiter order |
| `include/deterministic_random.h` | `d5c2fb93895d6a75adfd34cd6234a144981e62a4cba893c6ec1a6c1352a7dbb2` | PCG32 and binary64 draw |
| `include/utilities.h` | `d0620173dd694393927837947d5456b6be45a790680be5ccad189e1145c7bed1` | Clamp branch behavior |
| `src/utilities.cpp` | `3b634518202fc4d3011b655b6d2b0c20b0f0cca40ac869599bfc7b702abd54b9` | Positive modulo |
| `src/intake.cpp` | `103c335b254ca8c5f070b37ef0d7a71407d67089206d494ca5b8d0f9e987e76a` | Mixtures and intake boundary order |
| `src/exhaust_system.cpp` | `a30fa2a4590779bcb14bcd58e8dad91d46d501dc39b5f4d7230e391dbe30a74b` | Collector boundary order |
| `src/direct_throttle_linkage.cpp` | `8e70ca6c4bf9c6a1b90ff3df77b65b4e2d94366519b6a70dc22457a7375370df` | Direct throttle transform |
| `src/function.cpp` | `35a943f8526871a5bb81a07d262e41d3f90d690a1ab3d1bbafea249fa819f6ea` | Triangle interpolation |
| `include/camshaft.h` | `b9977bfdf0e7212c123b9ff44c58fb2fe55cb05db1a2f4372f1de29d262b2779` | Crank-center to cam-center conversion |
| `src/camshaft.cpp` | `1748dba81f82e70d1a894705a686394711753de98e213c5e462d0709ce7d40bd` | Cam wrap and lobe sampling |
| `scripting/include/actions.h` | `b9a9919967d210fc8a02ea19539325c2166978b5d6b1cde8b5d39e4af8073fd0` | Harmonic-lobe construction |
| `scripting/include/engine_node.h` | `7544a08b12a0ab9b08b77d2af161930ef409598c58a5226ff58c8e25bf75a4ba` | Chamber lifecycle and piston-speed table |
| `src/fuel.cpp` | `eec5de7f2f6725c05d526a5f82db7ffe6660814c10d94779cb74ddd74c69418c` | Flame-speed equations |
| `src/cylinder_head.cpp` | `70d3b184572ed3fb5503122e8ca0fe0eb564d579a30585582964491f9dd284cb` | Lift-to-conductance sampling |
| `src/crankshaft.cpp` | `52351004081b01b937cb63da11c6d862d45e4d7fe18131fb883c6f124101255d` | Legacy cycle-angle convention |
| `src/engine.cpp` | `078f5a68af304d8f2bbd350649d7d26a1a1ff65e5bdb25e00aea284e1830f94c` | Throttle/fuel propagation and RNG assignment |
| `src/piston.cpp` | `a14fa6bbf682f7838414f5a5513773733b60b7fd4076c39e35de7bf95fe5af6b` | Constraint-reaction wall-force dependency |
| `src/simulator.cpp` | `4edc1d0d0b6bf8e02f1adf4a20f630c842e49965d8477c8f8a54eb17222c22ba` | Outer order and filtered RPM |
| `src/piston_engine_simulator.cpp` | `17ac8b59447984a6a1941fd982a934b6875e0fc28ea0fde980cd9324f7531261` | Initialization, gas loop, excitation |
| `include/delay_filter.h` | `02603a94a84a1753149baf0ad476b739a8907523ff2d97679297662583b6b67a` | Source-rate integer-delay behavior |
| `assets/engines/bmw/M52B28.mr` | `2c7746f82e86cc22b0ab243f61e7fb8c155c3abf1084ad7b6f8bfee3d4e875a9` | Resolved BMW data/topology |
| `es/objects/objects.mr` | `f8214983c816f0a2e8d2cf1d733d10ea7adfbdaef94965b9c969c49e35e14dba` | Inherited head/intake/exhaust/fuel defaults |
| `es/part-library/parts/intakes.mr` | `f2331225d54ec56da44b5b658cfd51b859eb77c9bc5700a8dcace7513f5f8b1e` | Performer intake defaults |

These hashes establish behavioral provenance; they do not import source files into the
new implementation or create a compatibility obligation. M3 code is written against
this record. Once an isolated higher-fidelity replacement is accepted by verification
and listening, its superseded M3 subsystem is deleted.
