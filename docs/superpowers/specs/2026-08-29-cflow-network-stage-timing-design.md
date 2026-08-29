# CFlow Network Stage Timing Attribution Design

## Decision

Stage-timed `cflow-network-benchmark/v1` records use one explicit contract:

- enabled records set `stage_timing_version` to `3`;
- disabled records set it to `0` and emit every timing total and mean as zero;
- missing versions, unsupported versions, missing fields, negative values, and
  inconsistent residuals fail validation.

Stage timing explains where one Actor or Source run spends time. It is not a
performance baseline. End-to-end latency, throughput, tail latency, and CPU
cost are evaluated only against the same-run Direct transport result.

## Data contract

The benchmark owns one local timing sample per admitted operation or batch and
accumulates it in fixture-owned state. Production CFlow objects do not store
benchmark timing data.

The top-level decomposition is:

```text
completion_drive_ns >= drive_ns + wait_ns + completion_process_ns
completion_residual_ns = completion_drive_ns
                         - drive_ns
                         - wait_ns
                         - completion_process_ns
```

The nested handoff decomposition is:

```text
drive_residual_ns = drive_ns - dispatch_ns - executor_ns
wait_residual_ns = wait_ns - completion_ready_ns - wake_resume_ns
```

Enabled reports also require:

```text
io_operations > 0
abs(emitted_mean - total / io_operations) <= 0.0005
```

Totals are authoritative. The half-unit tolerance accounts only for the JSON
writer's three-decimal representation. Checked arithmetic is required before
an accumulator update; overflow or an invalid decomposition fails fast.

## Timing boundaries

| Component | Starts | Stops |
|---|---|---|
| Admission | Immediately before submit or resume | Immediately after admission returns |
| Completion+drive | Admission succeeds | Completion processing finishes |
| Drive | Immediately before a pump call | Immediately after that pump call |
| Dispatch | Before Actor mailbox or Source scheduler driving | After that driving step |
| Executor | Before the Actor Executor or Source-owned I/O driver | After that execution step |
| Wait | Immediately before condition wait or yield | Immediately after it returns |
| Completion ready | Immediately before a blocking wait | At the first coalesced wake signal |
| Wake resume | At that signal | After the waiter reacquires the latch mutex |
| Completion processing | Completion becomes observable | Validation, copy, and acknowledgement finish |

Busy-yield runs have no condition signal, so their named wait subcomponents
remain zero. Timeout, spurious wake, loop, clock, branch, and bookkeeping time
remains in the appropriate residual.

## Ownership and concurrency

The wake timestamp is benchmark-local state protected by the existing latch
mutex. The backend wake callback produces the first pending signal timestamp;
the benchmark wait loop consumes and clears it together with `pending`.
Destruction remains legal only after endpoint and backend quiescence. Stage
timing adds no queue, allocation, runtime ownership, or public API.

## Reporting

- Stage and handoff tables contain absolute nanoseconds per operation for Actor
  and Source independently.
- No stage field is divided by another CFlow model's field.
- Transport tables show absolute values plus `Actor/Direct` and
  `Source/Direct` ratios derived from identical same-run workloads.

## Verification

- TinyTest covers checked accumulation, enabled and disabled records, nested
  residual invariants, and real Source round trips.
- PowerShell tests reject missing versions, incomplete records, negative
  values, residual mismatches, and means outside emitted precision.
- The release workflow validates every record before publishing it.
- Windows Release uses repository presets; CI supplies cross-platform evidence.
