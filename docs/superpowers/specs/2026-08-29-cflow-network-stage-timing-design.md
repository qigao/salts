# CFlow Network Stage Timing Attribution Design

## Decision

Keep the existing `cflow-network-benchmark/v1` records and legacy
`completion_drive_ns` fields readable, but add a versioned decomposition for
stage-timed Actor and Source runs:

- `drive_ns`: time spent inside explicit Actor/Source pump calls.
- `wait_ns`: time spent in the benchmark's blocking wait or busy-yield step.
- `completion_process_ns`: time spent validating, copying, and acknowledging
  an observed completion.
- `completion_residual_ns`: the legacy post-admission interval minus the three
  measured components. This captures loop, clock, branch, and bookkeeping
  work that is not assigned to a named component.

The legacy `completion_drive_ns` remains the complete post-admission wall-time
interval so existing result readers continue to work. New records expose
`stage_timing_version: 2`; disabled records use version `0` and all stage
totals and means are zero.

## Evidence and Root Cause

The current benchmark starts `completion_drive_ns` immediately after
admission and stops it only after the network wait, pump, result validation,
and acknowledgement complete. The same interval therefore contains real
network latency and benchmark waiting as well as execution work. Comparing it
as pure drive overhead misattributes the dominant cost.

This is a measurement-contract defect. It does not establish that the Actor,
Source, executor, scheduler, or native backend state machine is incorrect.

## Data Contract

The benchmark owns one local timing sample per admitted operation or batch.
The sample is accumulated into one fixture-owned `network_stage_measurement`.
No runtime object or public CFlow type stores benchmark timing state.

For every enabled version-2 report:

```text
completion_drive_ns >= drive_ns + wait_ns + completion_process_ns
completion_residual_ns = completion_drive_ns
                         - drive_ns
                         - wait_ns
                         - completion_process_ns
operations > 0
abs(emitted mean - corresponding total / operations) <= 0.0005
```

The half-unit tolerance is the exact contract for the benchmark's three-digit
`%.3f` JSON representation. It accepts either adjacent result at a binary
floating-point tie while still rejecting a value outside the emitted
precision; totals remain the authoritative exact values.

Checked arithmetic is required before committing any accumulator update. An
invalid decomposition or overflow fails the benchmark with the existing
range-error path; it is never clamped or silently repaired.

The timer boundaries are:

| Component | Starts | Stops |
|---|---|---|
| Admission | Immediately before submit/resume | Immediately after admission returns |
| Legacy post-admission | Admission succeeds | Completion processing finishes |
| Drive | Immediately before each pump call | Immediately after that pump call |
| Wait | Immediately before cond-wait or yield | Immediately after it returns |
| Completion processing | Completion becomes observable | Validation/copy/acknowledge finishes |

Timer-call overhead remains observable in the residual instead of being
assigned to the runtime.

## Alternatives

1. Rename `completion_drive_ns` in place. Rejected because it breaks stored
   artifacts, scripts, and workflow checks that consume the v1 field.
2. Remove stage timing and use only wall/process CPU metrics. Rejected because
   those metrics cannot locate Actor-versus-Source overhead inside the same
   workload.
3. Instrument production CFlow APIs. Rejected because benchmark attribution
   does not justify public ABI or hot-path changes.
4. Add the local version-2 decomposition. Selected because it preserves the
   existing fact source and API while making the waiting contribution explicit.

## Architectural Impact

- **CFlow runtime:** no API, state, ownership, scheduling, or error-semantic
  change.
- **Benchmark:** adds local monotonic-clock observations. Complexity remains
  O(number of pump/wait iterations), with O(1) state per fixture/sample.
- **Statistics:** version-2 summaries compare named components and residual;
  version-1 records retain the legacy combined-stage fields for historical
  readability.
- **CI:** validates version, non-negative decomposition, exact residual, and
  zero-valued disabled timing before publishing an artifact.

The additional clock reads perturb stage-timed diagnostic runs. Stage timing
is already opt-in and is not used as the primary throughput result, so this is
an explicit measurement cost rather than a production performance change.

## Compatibility, Migration, and Rollback

The JSON schema string stays `cflow-network-benchmark/v1`; all existing fields
remain present with their current meaning. Readers must treat absent
`stage_timing_version` as legacy version 1. New summary columns are additive.

Rollback consists of reverting the additive fields and readers. Stored v2
JSON remains valid JSON and older readers continue to consume the retained v1
fields.

## Verification

- TinyTest checks atomic overflow behavior, decomposition invariants, disabled
  zero behavior, and a real Source round trip with non-zero named stages.
- PowerShell tests check v2 component summaries, residual calculation,
  malformed decomposition rejection, and legacy v1 compatibility.
- The release benchmark workflow validates every emitted record before upload.
- Windows Release builds and runs the benchmark target through repository
  presets; CI remains the cross-platform validation boundary.

## Version 3 Handoff Attribution

Version 2 established that `drive_ns` and `wait_ns` dominate the measured
post-admission interval, but each remains too broad to identify the handoff
cost. Version 3 keeps every version-2 field and adds two nested decompositions:

```text
drive_residual_ns = drive_ns - dispatch_ns - executor_ns
wait_residual_ns = wait_ns - completion_ready_ns - wake_resume_ns
```

`dispatch_ns` measures Actor mailbox driving for the Actor adapter and CFlow
scheduler driving for the Source adapter. `executor_ns` measures the manual
Executor for Actor and the Source-owned IO driver for Source. The Source owner
encapsulates its Actor and Executor, so this boundary intentionally measures
the public owner operation rather than reaching into production internals.

For blocking waits, `completion_ready_ns` starts immediately before the
condition wait and stops at the first coalesced wake signal. `wake_resume_ns`
starts at that signal and stops after the waiting thread has reacquired the
latch mutex. A timeout or spurious wake without an observed signal remains in
`wait_residual_ns`. Busy-yield runs have no condition signal, so both named
wait subcomponents remain zero.

The wake timestamp is benchmark-local state protected by the existing latch
mutex. The backend wake callback is the producer and the benchmark wait loop
is the consumer. The callback preserves the first pending signal timestamp;
the consumer clears it together with `pending`. Destruction remains legal only
after endpoint/backend quiescence, exactly as in version 2. No queue,
allocation, runtime ownership, or public API is added.

Readers accept versions 0, 2, and 3. Historical version-0 records may omit the
version-3 fields; if any new field is present, all must be present and zero.
Version 3 requires all nested totals and means, exact residual arithmetic, and
non-negative values. The release report adds a separate compact handoff table
instead of widening the existing stage table. Rollback can stop emitting
version 3 while the reader continues to accept stored version-3 artifacts.
