# CNet Packet Logical-Send Terminals

Status: Accepted for qigao/salts#219.

## Context

`cnet_packet_send()` reports bounded copy/admission only. UDP later reaches a NativeIO terminal, while one KCP logical message may produce multiple datagrams and becomes transport-complete only after the peer acknowledges every KCP segment in that message. Treating admission or an individual KCP datagram completion as the logical terminal would release downstream state too early.

The packet endpoint is a single caller-driven owner. Its callbacks execute serially from the owner lane. Received views and callback arguments remain borrowed only through callback return.

## Decision

An additive size/versioned terminal configuration enables tagged logical sends without changing `cnet_packet_send()` semantics. The configuration declares a hard maximum number of tagged operations. A successful tagged admission copies the payload into the existing bounded transport and retains only scalar operation metadata; a failed admission retains nothing and produces no terminal callback.

UDP completes from the unique NativeIO datagram terminal. KCP, authenticated KCP, and FEC complete when KCP's cumulative unacknowledged sequence advances past the logical message's final segment. Native completion of any KCP/FEC datagram is not a logical completion. Tagged KCP therefore requires message mode; stream mode is rejected only by the tagged-terminal initialization path.

Each terminal carries the original tag, generation-checked session, logical byte count, and concrete Salts status. The endpoint stores authoritative operation state in a fixed pool and per-session FIFO; endpoint metadata never grows after initialization. KCP 1.7 still allocates its bounded protocol segments. Tagged KCP stages those upstream allocations in a private KCP queue and splices the complete message only after every fragment allocation succeeds, so a reported allocation failure returns `SALTS_ENOMEM` without partial admission. The upstream Debug library asserts before reporting a segment-allocation failure; removing that build-mode limitation with per-session preallocated segments is tracked by qigao/salts#239. Pool exhaustion rejects admission with `SALTS_ENOBUFS`. Tags are opaque values and need not be unique; correlation is by the endpoint's private operation generation.

Explicit session close and endpoint stop stop new admission first. Unacknowledged KCP operations settle with `SALTS_ECANCELED`; UDP operations remain pending until NativeIO publishes their actual terminal. Protocol/session failure settles retained KCP operations with that concrete failure. Terminal callbacks are dispatched before the session's CLOSED notification and never recursively from another packet user callback. Stop returns only after all native terminals, logical terminals, and CLOSED notifications drain. Destroy requires successful stop.

## Ownership and capacity protocol

- Data unit: one copied UDP datagram or KCP logical message plus one fixed scalar operation record.
- Fact source: NativeIO owns admitted UDP bytes; KCP owns retained protocol segments; the endpoint operation pool owns tag/status/correlation metadata.
- Topology: one endpoint owner, one serialized callback lane, multiple bounded sessions, FIFO KCP completion per session.
- Capacity: UDP bytes remain bounded by datagram send slots and maximum datagram size; KCP bytes remain bounded by KCP segment capacity and maximum message size; tagged operation count is independently bounded by terminal configuration.
- Backpressure: deterministic transport validation runs before tagged-operation reservation. Admission returns the concrete readiness, validation, stale-handle, message-size, KCP segment-capacity, NativeIO capacity, allocation, or tagged-operation-capacity error. There is no blocking, dropping, expansion, or fallback.
- Shutdown: stop admission, cancel KCP protocol ownership, drain/cancel NativeIO, publish each logical terminal exactly once, publish CLOSED, then permit destroy.

## Compatibility and verification

Legacy initialization and `cnet_packet_send()` remain source-compatible and retain admission-only semantics. Consumers opt into the new ABI explicitly. Verification covers UDP admission versus terminal, fixed-capacity rejection and callback reuse, stale and reused sessions, the KCP 127-fragment boundary, delayed-ack multi-fragment KCP, dead-link status, authenticated KCP/FEC acknowledgement-before-failure ordering, close/stop races, concrete error precedence, and exactly-once callbacks under Debug and Release.
