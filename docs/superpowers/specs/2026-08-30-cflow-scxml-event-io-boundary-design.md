# CFlow SCXML Event I/O Boundary Decision

Status: accepted for issue #182, 2026-08-30.

## Context

CFlow SCXML already lowers executable content into native CFlow, executes it
through a bounded Statechart runtime, and exposes versioned Event I/O adapter
tables. The runtime owns one session's queues and transactional effect tickets.
It does not own a process-wide session registry, ingress decoder, network
transport, or cross-session scheduler.

SCXML 1.0 requires the SCXML Event I/O Processor, but intentionally leaves its
transport platform-specific. It defines source-relative targets, message-field
mapping, inaccessible-target errors, and a per-session location. The design
must preserve the existing dependency direction:

`SCXML adapter -> CFlow -> CMeta`

CBind/CSerde can convert an external JSON/XML/YAML envelope into typed CMeta at
the application boundary. Moving those codecs or transport policy into the
Statechart runtime would reverse the dependency and make deterministic
transition execution depend on optional formats.

## Options

### A. Built-in bounded processor

Salts would add a registry, router, timer/cancellation registry, ingress
decoder, scheduler, and lifecycle coordinator. This could provide an in-process
default, but it would introduce a new shared owner across sessions and require
public registration/configuration APIs. HTTP would still be a separate optional
adapter.

- Dependencies: synchronization, address policy, scheduling, and codecs or an
  additional envelope ABI.
- ABI/API: new opaque processor handles, endpoint registration, capacities,
  dispatcher lifecycle, and probably new versioned configuration structures.
- Performance: an extra shared hop and contention point on every cross-session
  send; capacity and scheduler tuning become library policy.
- Security: the library must define tenant accessibility, spoofing protection,
  input limits, and transport trust boundaries without application identity
  context.
- Migration: existing adapters either become wrappers around the built-in
  processor or coexist with two routing fact sources.
- Rollback: difficult after applications depend on processor-owned addresses
  and lifecycle.

### B. Conforming host adapter

Keep the versioned function table as a Bridge/Adapter boundary. Salts owns
compilation, one-session execution, internal delivery, transactional reservation
hooks, bounded Event admission, and precise error conversion. The embedding host
owns registration, accessibility, routing, transport, decoding, and dispatch.

- Dependencies: no new production dependency; CBind/CSerde remains optional at
  the host format boundary.
- ABI/API: adapter tables remain unchanged. One additive read-only
  `cflow_scxml_session_copy_location()` query makes the already-exposed system
  location usable as the host registry key.
- Performance: local `#_internal` remains direct. Cross-session traffic pays one
  bounded host-queue hop; the host chooses topology and capacity using its load
  model.
- Security: accessibility and transport identity stay with the application that
  owns principals and deployment boundaries. The runtime still validates all
  bounded admission inputs.
- Migration: current adapters continue unchanged. Hosts may incrementally add
  registry routes and v3 decoding without changing SCXML programs.
- Rollback: remove use of the location query and contract test; no document,
  adapter-table, or serialized-data migration is required.

## Decision

Choose option B. The existing versioned adapter is the smallest stable
dependency-inversion seam. A built-in registry would be a service locator with
process-global policy and would duplicate application state. Salts does
not claim that the library alone is a standalone mandatory SCXML Event I/O
Processor.

The only production code addition is the immutable location-copy query. It is
additive at source/API level and adds one exported function plus one enum; no
existing structure size, function table, request layout, binary data, or
accepted document changes. Existing v1/v2/v3 adapters continue to compile and
behave identically.

## Ownership and state protocol

The host registry is the sole cross-session routing fact source. Each endpoint
borrows a live session/program only from successful registration through
quiescent unregistration. Each accepted prepare owns one fixed-capacity ingress
row and a deep copy of callback-scoped fields. A row moves exactly once through:

`free -> reserved -> ready -> in-flight -> free`

Rollback uses `reserved -> free`. Target backpressure uses
`in-flight -> ready`. Close prevents new reservations and drains or cancels rows
until none can refer to the session. Registration changes are control-plane
operations and require the affected endpoint to be quiescent.

Callbacks for a single session are serialized by its executor. A shared router
must synchronize producers from different sessions, preserve commit order per
source, and never hold its registry/queue lock while calling a session, codec,
transport, or user callback. The production runtime never reads or mutates the
host registry.

## Error and security semantics

- Invalid or unsupported nonempty processor type: `ERROR_EXECUTION`.
- Missing, stale, or inaccessible route: `ERROR_COMMUNICATION`.
- No reserved host capacity: `FULL`, mapped to `error.communication`.
- Closed adapter: `CLOSED`, mapped to `error.communication`.
- Target mailbox full after commit: retain the bounded row and retry through
  the host scheduler; do not drop or allocate an unbounded fallback.
- Decode/schema/data-format failure: discard the message, report
  `error.communication` to the intended receiver, and notify the sender while
  it remains live.
- Contract corruption such as a missing ticket callback:
  `INVALID_CONTRACT`, which fails the session rather than masking the invariant.

The host must authenticate any external transport before registry lookup,
authorize sender-to-target access, bound every field and payload before copying,
and avoid logging payloads or credentials. The library location is an address,
not an authorization token.

## Verification and conformance claim

The fixed-capacity `cflow_scxml_event_io_contract_test` uses public APIs to
exercise direct cross-session delivery, omitted-target self resolution,
parent/invoke aliases, inaccessible targets, queue saturation, rollback,
metadata mapping, and close/quiescence. The normal SCXML tests cover the
location query including sizing, stability, invalid lifetime, and no partial
write.

Session initialization performs initial entry work before returning. A complete
host profile therefore creates an `INITIALIZING` logical source endpoint and
reply address before init, permits its adapter to reserve/commit bounded rows,
but does not dispatch them to the initializing session. After successful init
the caller copies the generated location, registers it as an active route, and
enables dispatch. Initialization failure invokes close, which discards the
unactivated rows. The repository contract adapter activates after init and does
not claim this initial-send case; this limitation is another reason the W3C
processor rows remain unsupported.

This contract test proves that a host can satisfy the boundary without internal
access. It does not test a production transport or authorize a standalone/full
SCXML processor claim. The W3C Event I/O manifest rows remain unsupported until
they run with an explicitly selected conforming host profile.

## Migration and rollback

Existing applications need no change. A conforming host may:

1. initialize a session with its adapter context;
2. copy and register the location after initialization, before admitting any
   event that can execute external `send`;
3. bind parent/invoke routes as those lifecycles commit;
4. close, reach adapter quiescence, unregister, then destroy.

If the boundary proves insufficient, a future built-in processor must be a
separate opaque service implementing this same adapter contract. It must not be
embedded into `cflow_scxml_session`; that preserves a rollback path to an
application-supplied service and keeps the runtime ABI stable.
