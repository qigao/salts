# CFlow Network Benchmark Baseline Design

## Background

The release benchmark currently reports application MiB/s for both the 64-byte latency profile and the larger throughput profile. The arithmetic is correct, but the latency rows can be mistaken for socket bandwidth. The benchmark also lacks a direct socket client using the same loopback echo peer, so Actor overhead cannot be separated from operating-system and runner overhead.

## Decision

Add a `direct` benchmark driver that is valid only with the raw peer, blocking wait mode, and disabled stage timing. It uses blocking socket calls on the client and the existing blocking raw echo server. It does not initialize a CFlow native backend, Actor, Executor, Source, or wake latch.

Keep the existing `cflow-network-benchmark/v1` JSON schema. A direct result reports `driver=direct` and `backend=socket`; existing Actor and Source records retain their current values. Add Echo/s to the general workflow summary and add a paired direct-versus-Actor diagnostic summary using identical protocol, payload, observation budget, and raw peer.

The diagnostic payload set is 64 B, 1 KiB, and 64 KiB over TCP. Each pair is bounded by the existing samples and exchanges configuration. Direct and Actor execute sequentially in counterbalanced order; they do not share mutable runtime state.

## Ownership and lifecycle

- The fixture owns the two connected sockets, payload buffers, and raw server thread.
- Direct mode keeps the client socket blocking and performs one serialized send/echo receive at a time.
- Actor and Source modes retain the existing nonblocking client and backend ownership rules.
- The raw server remains the single source of echoed data and verifies exactly the configured exchange count.
- All paths join the server and close sockets through the existing fixture cleanup boundary.

## Error semantics

Unsupported combinations fail during configuration with `SALTS_EINVAL`; there is no fallback from direct to Actor or from blocking to busy mode. Socket errors propagate through the existing Salts error convention and fail the TinyTest benchmark.

## Deferred work

An inflight/windowed throughput mode is deliberately deferred until the direct-to-Actor ratio is available. That mode requires a separate bounded buffer and request-state protocol; adding it now would prevent the baseline from isolating orchestration overhead.

## Verification

- Parser and compatibility tests reject invalid direct combinations.
- Real TCP and UDP loopback tests exercise direct exchange behavior.
- Release benchmark smoke runs validate direct JSON and compare it with Actor under the same payload.
- The existing Actor, Source, and dual-native benchmark configurations remain green.
