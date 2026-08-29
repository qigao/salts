# CFlow Native I/O Round-Trip Batching

## Context

The loopback benchmark currently completes and acknowledges a send before it
admits the matching receive.  A blocking socket call does not cross the CFlow
Actor, backend completion, wake, Executor, callback, and acknowledge boundaries
for the send.  Serializing the asynchronous send across all of those boundaries
therefore adds one fixed completion cycle to the round-trip critical path.

This change keeps the public Actor and reactive Source APIs unchanged.  The
benchmark uses their existing bounded multi-request admission to keep the
receive pending while the send is in flight.

## Reference model

The target boundary follows the useful parts of libuv's model without adding a
libuv dependency or copying its public API:

- a native endpoint is a long-lived handle/channel owned by one backend loop;
- each send or receive is a short-lived request with exactly one terminal
  completion and release obligation;
- network readiness/completion is driven by the platform event provider, not
  by dispatching each socket operation to a worker thread;
- completion delivery may be batched, while request identity, ordering, and
  error results remain per operation;
- receive interest can stay active across multiple completions instead of
  being recreated after every send.

This corresponds to libuv's documented distinction between
[handles and requests](https://docs.libuv.org/en/v1.x/design.html#handles-and-requests)
and its single-owner [I/O loop](https://docs.libuv.org/en/v1.x/design.html#the-i-o-loop).
It is a design reference only: CFlow keeps Actor/Source demand, acknowledgement,
and Graph execution as its user-facing control plane.

The current core already provides most of this boundary: the native backend is
long-lived and bounded, native operations are retained as short-lived Actor
requests, IOCP/io_uring use one worker per backend rather than one thread per
operation, readiness backends use the shared platform reactor, and completion
publication has a bounded batch. The defect addressed here was above that
boundary: the round-trip consumer waited for send completion before admitting
receive, so it could not benefit from those facilities.

Two differences from libuv are intentional. CFlow delivers completion through
Actor/Executor and optionally Source/Graph instead of invoking all user
callbacks on the backend-loop thread. It also keeps receive as an explicit
bounded request instead of exposing an always-on `read_start` handle. A future
persistent receive/channel facade may lower into replenished bounded requests,
but it must preserve demand, backpressure, borrowed-buffer invalidation, and
shutdown drain; it is not required for this latency correction.

## Data-path protocol

| Property | Contract |
|---|---|
| Data unit | One native send or receive operation descriptor.  A round-trip batch contains at most two descriptors. |
| Source of truth | The Actor request slot remains the authoritative request state.  Batch result arrays are derived observations only. |
| Payload ownership | Payload buffers remain borrowed.  A successful Actor admission moves only the operation token and its release obligation. |
| Lifetime | Send storage is immutable and receive storage is backend-exclusive until the corresponding terminal callback and acknowledge path complete. |
| Topology | Existing MPSC Actor admission, single Actor driver, native backend completion producers, and Executor delivery remain unchanged. |
| Ordering | Receive is admitted before send.  Their completions may arrive in either order and are mapped back by result index. |
| Capacity | A paired round trip requires two operation slots even though its logical workload window is one. Existing fixed capacities remain the hard limit; no dynamic growth is introduced. |
| Backpressure | Existing `FULL`/`TURBO_EBUSY` results remain authoritative.  No operation is silently dropped or retried. |
| Failure | If only a prefix is admitted, the existing batch runner waits for and acknowledges that prefix before returning the first useful error. |
| Shutdown | Existing Actor/Source close, native cancel/drain, acknowledge, and operation release ordering is unchanged. |
| Observation | Stage operation count remains two per complete send/receive pair; the private benchmark endpoint records its largest operation batch. |

## State and error semantics

For TCP, partial send and receive completions advance independent byte offsets.
While both sides remain incomplete, the next receive and send are admitted as
one pair.  If only one side remains incomplete, it is admitted alone.  Zero-byte
progress before the requested transfer is complete is an I/O error, matching
the previous behavior.

For UDP, the receive and send are admitted as one pair.  Datagram byte counts,
source-address validation, and payload equality remain unchanged.

## Compatibility

- No installed header, ABI, error code, or public lifecycle contract changes.
- Direct and vectored benchmark drivers retain their current paths.
- Actor and Source results remain one completion per native operation.
- A raw round-trip Source requires `source_window_capacity >= 2`; the logical
  `workload_window_capacity` remains one. An explicit smaller capacity fails
  configuration validation.
- Benchmark latency changes because the Actor/Source drivers now exercise the
  concurrency already provided by their public APIs.

## Validation

1. A Source window test must observe two simultaneously occupied entries for a
   single raw-peer round trip.
2. An Actor round-trip test must observe a peak in-flight workload of two and
   exactly two acknowledged operations.
3. TCP and UDP direct, Actor, Source, vectored, cleanup, and large-payload tests
   must continue to pass.
4. Release benchmarks compare 1 KiB TCP and UDP latency before and after the
   change; throughput and error counters remain part of the output.
