# CFlow Statechart Host Transaction Design

## Context

The Statechart runtime currently exposes one versioned hook table that grew
from V1 through V3. The versions describe chronology rather than one stable
host contract: V1 observes stable boundaries and preprocesses external Events,
V2 observes all selected triggers, and V3 adds a mutable transaction only at a
stable boundary. A host that must normalize a trigger and atomically update the
managed state before transition selection cannot express that operation without
combining callbacks with different mutability and commit rules.

This design adds one V4 host transaction callback while retaining V1-V3 as a
source-compatible migration surface. It does not move SCXML invocation,
transport, XML, or expression semantics into CFlow.

## Decision

Add `CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V4` with exactly one callback:
`on_host_transaction`. V4 rejects every legacy callback field. The callback is
invoked in two phases:

- `CFLOW_STATECHART_HOST_PREPARE_TRIGGER`: after a trigger becomes the current
  call-scoped selection candidate and before guards or transition selection.
- `CFLOW_STATECHART_HOST_PREPARE_QUIESCENCE`: after internal, completion, and
  eventless work drains and before the macrostep settles.

The context is opaque. Read access never copies state. The first successful
`cflow_statechart_host_edit_state()` call copy-constructs a staged value and
returns the sole writable state for the callback. Internal Events and external
effect tickets are staged through bounded context functions. The callback
returns `CONTINUE`, `DROP`, or `FATAL`:

- `CONTINUE` commits a staged state, Events, and tickets as one transaction.
  With no staged work it is a true no-op and does not increment the
  configuration version.
- `DROP` is valid only for an external trigger during `PREPARE_TRIGGER`; it
  rolls back staged work and consumes the Event without transition selection.
- `FATAL` rolls back staged work, discards every ticket exactly once, and
  latches a deterministic instance error.

Invalid return values, an error string paired with non-`FATAL`, failed lazy
copy construction, queue overflow, journal overflow, and phase/result mismatch
all fail fast.

## Public Surface

The public header gains:

- `cflow_statechart_host_phase`
- `cflow_statechart_host_result`
- opaque `cflow_statechart_host_context`
- call-scoped readers for phase, trigger, published state, configuration
  version, and active-state membership
- bounded writers for editing state, raising an internal Event, and staging an
  effect ticket
- `cflow_statechart_host_transaction_fn`
- an appended `on_host_transaction` field in
  `cflow_statechart_instance_hooks`

Every returned pointer and the context itself are invalid after the callback.
The callback runs on the instance SerialExecutor without the instance mutex
held and must not block, retain the context, or destroy the instance.

## State and Ownership

The Statechart instance remains the sole fact source for the active
configuration, managed state, internal queues, and effect journal. The host
borrows the immutable trigger and published state for one call. A staged state
is instance-owned and is either atomically published or destroyed/reset by the
runtime. Each successfully staged move-only effect ticket reaches exactly one
terminal callback: commit after publication, or discard during rollback.

The runtime remains single-owner on the SerialExecutor. The host context does
not add locks and cannot escape to another thread.

## Compatibility and Migration

V1-V3 validation and behavior remain unchanged. V4 is mutually exclusive with
all legacy fields, so there is never ambiguous callback ordering. Existing
callers continue to compile and run. New callers migrate callback behavior as
follows:

| Legacy callback | V4 phase |
| --- | --- |
| `preprocess_external` and `on_event` | `PREPARE_TRIGGER` |
| `on_stable` and `on_stable_transaction` | `PREPARE_QUIESCENCE` |

No removal is planned in this change. A later major release may deprecate and
remove V1-V3 after all first-party callers use V4.

## Alternatives

Extending V3 with another mutable external callback was rejected because it
would preserve split ordering and commit rules. Moving invocation-specific
logic into Statechart was rejected because it reverses the dependency from the
generic engine to SCXML. A generic event bus was rejected because it does not
define atomic state, queue, and effect ownership.

## Failure, Rollback, and Verification

The transaction starts with no mutable copy. Any failure discards staged Events
and tickets, resets a constructed staged state, retains the published state and
configuration, and prevents transition selection. Focused TinyTest cases must
prove phase order, visibility before guards, no-op copy avoidance, external
drop, bounded queue/effect failures, rollback, and V1-V3 compatibility. The
existing `cflow_statechart_instance_test` and C++ public-header test are the
minimum regression boundary.
