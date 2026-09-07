# CFlow directional NativeIO benchmark plan

## Problem

The current adapter benchmark treats one concurrent send plus receive pair as a
single exchange and reports one latency/rate value. That is a valid loopback
transport workload, but it is not a valid Actor/Reactive endpoint metric:
Reactive NativeIO ownership and Subscriber delivery run on different workers,
and the combined value cannot identify which endpoint or scheduling boundary
produced the cost.

## Contract

- Measure `TX` and `RX` as independent workloads. One measured operation owns
  exactly one NativeIO direction.
- Run the opposite raw endpoint with bounded waits on one fixed peer worker.
  Do not attach both ends to the measured NativeIO owner.
- Keep NativeIO direct, Actor/NativeIO, and Source/NativeIO comparisons within
  the same direction, payload, transport, backend, and repeat.
- Preserve fixed capacities, generation-checked terminal completion, payload
  validation, exact release/acknowledgement, quiescence, and ordered shutdown.
- Report directional latency percentiles, directional operations/second and
  MiB/second, process CPU, stage timings, and semantic-gate counters. Do not
  publish a merged send+receive latency or throughput ratio. Process CPU covers
  every participating worker and is not a per-thread metric.

## Implementation

1. Add an explicit direction to each fixture and result matrix.
2. Attach only the measured endpoint; use a dedicated one-worker Salts pool to
   drive the opposite blocking socket/pipe endpoint.
3. Restrict Direct, Actor, and Source operation admission/completion accounting
   to that direction. Use Source window one because each directional workload
   has one independent operation lane.
4. Record latency from peer-task admission to the measured endpoint's terminal;
   peer completion and payload equality remain mandatory correctness gates.
5. Update output and documentation to state the owner/thread topology and the
   directional denominator.

## Verification

- Build and run `cflow_native_io_adapter_benchmark` in Release.
- Confirm every TCP/Pipe, TX/RX, payload, and mode row reports zero semantic
  errors/rejections/stale completions.
- Run adjacent Actor, Publisher, NativeIO adapter, and benchmark CI slices.
- Run format and whitespace checks.
- Require Windows IOCP, Linux epoll, and macOS kqueue Release benchmark jobs at
  the exact implementation SHA before merging.

## Compatibility and rollback

No public API, ABI, protocol, or production runtime behavior changes. Only the
benchmark fixture and report schema change. Rollback is one benchmark-only
commit; prior artifacts remain interpretable by their recorded commit SHA.
