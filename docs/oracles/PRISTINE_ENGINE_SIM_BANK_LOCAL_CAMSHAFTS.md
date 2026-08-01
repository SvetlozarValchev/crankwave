# Pristine engine-sim bank-local camshaft oracle

Status: source behavior frozen for clean-room parity  
Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

## Executed ownership

Pristine does not own one engine-wide intake shape and one engine-wide exhaust shape.
Each generated `Camshaft` owns:

- one lobe-profile function;
- one advance value;
- one base-radius value;
- one ordered array of lobe centerlines; and
- the crankshaft pointer supplied during generation.

A standard valvetrain owns one intake-camshaft pointer and one exhaust-camshaft pointer.
Each cylinder head owns its valvetrain, so separate bank-local heads may execute
different intake or exhaust camshafts without an equality restriction. Lobes belonging
to one camshaft necessarily share that camshaft's profile; only their centerlines
differ.

The source boundary is visible in:

- `scripting/include/camshaft_node.h:21-46`, where one profile is generated for a
  camshaft and the authored lobe centers are appended;
- `include/camshaft.h:23-44` and `src/camshaft.cpp:25-56`, where the runtime retains
  that profile and samples it for each local lobe;
- `include/standard_valvetrain.h:6-21` and `src/standard_valvetrain.cpp:14-32`, where a
  standard valvetrain delegates to its two camshafts; and
- `src/cylinder_head.cpp:30-66`, where each head retains its own valvetrain and samples
  its own port-flow curves at the resulting lifts.

Pristine passes crankshaft array element zero to every bank/head during generation
(`scripting/include/engine_node.h:85-99`). Bank-local cams therefore follow the output
crank; they do not follow a cylinder's secondary crankshaft independently.

## Exact lift path

For a camshaft with body angle `body`, output-crank TDC reference `tdc`, authored
advance `advance`, and authored crank-angle lobe center `center`, pristine evaluates:

```text
crank = body - tdc
cam = positive_mod((crank + advance) * 0.5, 2*pi)
stored_center = center / 2
argument = fold_to_minus_pi_inclusive(cam + stored_center)
lift = lobe_profile.sampleTriangle(argument)
```

The source comment describes advance in camshaft degrees, but the executable equation
above halves it together with crank angle. Clean-room parity follows the code, not the
comment.

`Function::sampleTriangle()` clamps outside the first and last samples. Inside the
domain it uses points within the filter radius with weight
`(radius - abs(sample_x - x)) / radius`, then returns the weighted sum divided by the
weight sum (`src/function.cpp:104-134`). The existing clean-room triangle sampler and
harmonic-lobe construction remain the authority; this checkpoint changes ownership,
not either numerical algorithm.

`CombustionChamber::update()` samples the head-local intake and exhaust conductance at
the physics-step boundary (`src/combustion_chamber.cpp:226-233`). The subsequent fluid
substeps reuse those cached values. The clean-room runtime must likewise select the
correct cylinder-bound cam profile before its existing gas step.

Base radius is retained in authored/resolved identity because pristine carries it on
the camshaft, but it is not added to valve lift and is consumed only by native GUI
geometry. It must not change gas flow or PCM.

## Asset evidence and acceptance fixture

An exhaustive check of the 27 engine definitions at the pinned commit found no shipped
asset with different same-role shapes across banks. Multi-bank builders such as the
Subaru, V-engine, Kohler, Shovelhead, and radial examples pass a common intake profile
to each intake cam and a common exhaust profile to each exhaust cam.

The clean-room acceptance fixture is therefore a deliberately counterfactual Subaru
EJ25 pair:

- A retains the pristine-derived equal bank-local cams;
- B changes only the negative-bank intake cam shape;
- both retain the same cylinders, bank axes, centerlines, gas path, firing order,
  routes, controls, and presentation; and
- focused left-only and right-only mutations prove that profile selection follows the
  cylinder's actual camshaft binding rather than the first or last profile in a list.

This fixture proves a source-executable topology and deterministic ownership. It is not
a real Subaru cam calibration and is not a pristine audio oracle. Existing accepted
engines must retain byte-identical PCM throughout the checkpoint.

## Deliberate boundary

This checkpoint admits distinct standard camshafts. It does not simultaneously admit
multiple bank-local VTEC selectors, change cam interpolation, retune an engine, add an
audio source, or claim higher acoustic fidelity. Those concerns remain separately
reviewable.
