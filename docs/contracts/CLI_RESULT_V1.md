# Native CLI machine result v1

`render`, `pack-revengine`, `inspect-revengine`, `verify-revengine`, and
`inspect-ir-catalog` accept
`--result-format json`. In that mode the command writes exactly one UTF-8 JSON
object followed by one LF to standard output for either success or failure. It does
not write human diagnostics to standard error. Process-launch failures that occur
before `engine-sim-offline` starts are outside this contract.

JSON strings preserve valid non-ASCII UTF-8 bytes. An invalid input byte sequence
is represented with the Unicode replacement character escape, so command output is
always valid UTF-8 JSON rather than a byte-wise Latin-1 expansion.

Every result begins with these keys in this order:

```json
{
  "schema": "engine-sim-offline.cli-result.v1",
  "release_identity": "1.1.0",
  "command": "render",
  "ok": true,
  "code": "success",
  "exit_code": 0
}
```

`release_identity` is the identity of the installed distribution. `command` is one
of the five command spellings above. `code` is a stable symbolic outcome code and
`exit_code` is the exact process status. Success appends a command-specific
`result` object. Failure appends `message` and may append typed diagnostic fields.
Consumers must select behavior from `schema`, `command`, `ok`, `code`, and
`exit_code`; `message` is explanatory text rather than a programmatic identifier.

Render success contains `output_directory` and `manifest`. Pack success contains
`output_file`, `container_bytes`, `entry_count`, and `container_sha256`. Inspect and
verify success contain the REVENGINE version, verification state, byte/count/hash
summary, ordered entry records, and either a verified package descriptor or null.
IR-catalog inspection success contains `catalog_sha256`, `entry_count`, and the
validated `engine-sim-offline/ir-authoring-catalog.v1` object. Its release identity
must equal the outer installed release identity, and every exposed selection is
cross-checked against the installed technical asset catalog.
Potentially wide byte counts are decimal strings. Entry counts and container
versions are JSON integers.

Failure codes are drawn from these stable families:

- `usage-error`, `memory-exhausted`, and `software-error`;
- the published native input/output error labels such as `path-not-found` and
  `destination-exists`;
- `authoring-diagnostics`, accompanied by the ordered authored diagnostics;
- `revengine-data-error`, `revengine-input-unavailable`,
  `revengine-output-unavailable`, and `revengine-operation-unavailable`;
- `render-invalid-specification`, `render-unreachable-target`,
  `render-event-schedule-violation`, `render-nonphysical-state`,
  `render-numerical-failure`, `render-incomplete-source-route`,
  `render-evidence-rights-failure`, `render-artifact-publication-failure`, and
  `render-contract-violation`;
- `render-cancelled`, `render-terminated`, and `render-deadline-exceeded`.
- `pack-revengine-cancelled`, `pack-revengine-terminated`, and
  `pack-revengine-deadline-exceeded`;
- `inspect-revengine-cancelled`, `inspect-revengine-terminated`, and
  `inspect-revengine-deadline-exceeded`;
- `verify-revengine-cancelled`, `verify-revengine-terminated`, and
  `verify-revengine-deadline-exceeded`.

The four rendering/container commands accept `--deadline-unix-ms <epoch-ms>`, where
the value is a
positive base-10 signed-64-bit-compatible Unix epoch deadline in milliseconds. An
expired deadline or a deadline reached during work returns exit 75 with the
command-specific `*-deadline-exceeded` code. SIGINT or SIGTERM requests cooperative
termination and returns exit 75 with the command-specific `*-terminated` code.
`inspect-ir-catalog` is a bounded local metadata query and does not accept a deadline.

Render cancellation is propagated through the native `RenderControl` stop token
and is observed between complete 20 ms session blocks and at publication
boundaries. A begun but uncommitted directory transaction is aborted and its
private staging tree is removed; an incomplete public output is never a successful
result.

Pack observes cancellation while walking and reading the package and while writing
the container. On Linux, it writes through a unique private file opened with
`O_EXCL` and `O_NOFOLLOW`, then uses a no-overwrite link as the publication
linearization point. Cancellation before that point removes the private file and
does not expose the requested output. Once no-overwrite publication succeeds, the
committed result wins over a later termination request. Inspect and verify observe
cancellation during bounded file reads and between container/package validation
phases.

Without `--result-format json`, the existing human-readable stdout/stderr and
sysexits-compatible status behavior remains in force.
