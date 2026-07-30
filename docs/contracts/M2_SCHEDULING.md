# M2 deterministic scheduling and bounded streaming

Status: retired historical implementation record; current block ownership is defined
by [`ENGINE_SESSION_API.md`](ENGINE_SESSION_API.md)

Applies to: fixed-rate horizon resolution, method-owned block partitioning,
constant-memory traversal, capture-clock coverage, deterministic cancellation points,
and the frozen P1.8 schedule

This document preserves the reasoning behind the former standalone schedule types.
Those types have been deleted rather than retained as a compatibility layer. This
checkpoint did not decode the BMW fixture, simulate an engine, generate an
excitation signal, run DSP, serialize telemetry/WAV files, or admit render success. It
established the clock and transport rules those stages used. Present-tense statements
below describe repository state at that checkpoint.

## 1. Integer clock resolution

Scenario times remain binary64 in the current resolved data contract. Admission now
resolves them once against each reduced-rational clock through
`resolve_frame_index(time_s, rate)`. It accepts only a value within binary64
representation error of an integral frame and caps accepted indices inside
binary64's consecutive-integer range. A genuine fractional-frame boundary is
rejected.

After resolution, scheduling uses only integer indices. It never advances a floating
timestamp through repeated addition. `CaptureClock::timestamp_s()` remains a
diagnostic conversion and cannot drive execution.

The clock-grid validator requires:

- total, audible-start, and audible-duration boundaries on physics, capture,
  source-processing, acoustic, and delivery grids;
- a positive total and audible frame count on every clock;
- the half-open audible interval to end exactly at every total horizon;
- preparation, operating-state, and authored trajectory boundaries on the physics
  grid;
- fixed warm-up plus settling to end exactly at the physics audible-start frame;
- the fixed-horizon complete-cycle sampling endpoint to resolve exactly on the
  physics grid and equal the physics audible-start frame.

Manifest media validation and capture horizon validation use the same resolver, so
they cannot silently round the same scenario differently.

## 2. Method-owned partition versus transport capacity

These are intentionally different:

- `RenderQuality::capture_block_capacity_frames` is the maximum frame payload the
  session transport may borrow at once.
- `RenderQuality::event_journal_capacity_records` is the maximum event-record payload
  the same borrowed capture block may contain.
- `SchedulePolicy::capture_partition_frames` is the exact internal partition owned
  by the selected numerical/presentation method.
- `SchedulePolicy::maximum_event_records_per_block` is that method's positive
  worst-case journal bound, independent of the transport's possibly larger capacity.
- `RenderSink::write_artifact_chunk()` sizes are publication details and affect
  neither value.

A method partition and its worst-case event journal must fit the declared transport
capacities. Raising capacity does not alter the method partition; lowering it below a
required bound rejects the schedule. There is no inferred default partition.

The current scheduler admits exactly one deterministic worker per render session and
creates no thread pool. Callers may run independent sessions concurrently.

## 3. Compiled plan and block cursor

`compile_render_schedule()` produces a compact plan or a path-bearing
`ValidationReport`. The plan records, for each of the five clocks:

- exact rational rate;
- total frame count;
- half-open audible frame range;
- maximum frames assigned to one method block.

Admission requires the method partition to project to an integral boundary on every
target clock. A fractional target-clock boundary is rejected until a versioned
per-clock phase policy defines its ownership. Admitted boundaries use an
overflow-checked rational projection from the absolute capture index, and the final
projected boundary must equal the clock's resolved total exactly.

These ranges define deterministic transport ownership, not reconstruction DSP phase.
For example, a reconstruction method's cumulative-ceil input phase remains part of
that versioned method and cannot be inferred from the scheduler's frame ranges.

`ScheduleCursor` owns only the plan, next block ordinal, next capture index, and a
terminal state. It returns one `ScheduledRenderBlock` by value at a time and never
allocates a duration-sized block list. Blocks are nonempty, contiguous, ordered, no
larger than the method partition, and include exact per-clock crop intersections.
The existing `CaptureBlockView` remains the borrowed callback payload: its storage is
valid only until the consumer returns and its frame count cannot exceed the declared
frame transport capacity. Its event journal likewise cannot exceed the declared record
capacity, so variable event multiplicity cannot create an unbounded block.

## 4. Frozen BMW P1.8 schedule

`compile_p18_reference_schedule()` binds the content-addressed P1.8 algorithm record
to:

```text
capture/physics rate       10,000 Hz
source/acoustic/delivery  192,000 Hz
capture method block           200 frames
source frames per block       3,840 frames
total capture frames        170,000
total method blocks             850
total source frames        3,264,000
audible source range      [384,000, 3,264,000)
audible source frames      2,880,000
```

The first 50 blocks are the one-second bootstrap, the next 50 are loaded pre-roll,
and the final 750 are audible. Capture is post-step. Source state is continuous
through every boundary and the crop; the schedule requests no tail. The 3,840-frame
source block is below the frozen 9,600-frame convolution limit.

A frame capacity of 200 or greater and an event capacity of at least
`19 * 200 = 3,800` records admit this partition. For example, frame capacity 256 still
produces exactly 850 blocks of 200; frame capacity 199 is rejected. Sink byte chunks
cannot change any of these counts.

## 5. Cancellation

`RenderControl` carries a C++20 stop token. Structural and evidence-rights preflight
run first, so cancellation cannot hide an invalid request. The scheduler observes a
stop only between complete method-owned blocks, including one final poll after the
last block and before completion/publication.

A cancellation step records the first unprocessed frame on every clock. It is
terminal and idempotent. The public render boundary reports `FailureKind::cancelled`
with the complete request identity and no success manifest. In the current
fail-closed build, a pre-requested stop is observable before execution and leaves the
sink idle. Once a real route begins a sink transaction, a cancellation before commit
must abort it exactly once; commit remains terminal.

## 6. Capture half-open boundary

Capture validation now checks the integer sample interval
`[first_sample_index, first_sample_index + frame_count)` against the resolved capture
horizon. A final post-step sample timestamped exactly at total duration is valid. A
pre-step sample beginning at total duration is outside the half-open scenario and is
rejected.

## 7. Deferred work

The next checkbox supplies concrete telemetry/WAV sinks and focused DSP primitives.
Fixture decoding, P1.8 excitation/reconstruction/conditioning/convolution, manifest
construction, and an admitted end-to-end route remain later checkboxes. No test-only
renderer or fabricated audio path is introduced here.
