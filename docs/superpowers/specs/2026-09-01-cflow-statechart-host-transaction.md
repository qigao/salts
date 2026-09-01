# CFlow Statechart Host Transaction Design

## Context

The former Statechart hook table accumulated three callback generations with
different mutability and commit rules. Keeping those layouts made callback
ordering ambiguous and forced hosts to compose observation, preprocessing, and
stable-boundary mutation themselves.

This design replaces that surface with one V4-only host transaction callback.
V1-V3 are intentionally removed; this is a breaking API/ABI cleanup. It does
not move SCXML invocation, transport, XML, or expression semantics into CFlow.

## Decision

Expose `CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V4` with exactly one callback:
`on_host_transaction`. Any version other than V4 is rejected. The callback is
invoked in two phases:

- `CFLOW_STATECHART_HOST_PREPARE_TRIGGER`: after a trigger becomes the current
  call-scoped selection candidate and before guards or transition selection.
- `CFLOW_STATECHART_HOST_PREPARE_QUIESCENCE`: after internal, completion, and
  eventless work drains and before the macrostep settles.

The context is opaque. Read access never copies state. The first successful
`cflow_statechart_host_context_edit_state()` call copy-constructs a staged
value and returns the sole writable state for the callback. Internal Events and
external effect tickets are staged through bounded context functions. The
callback returns `CONTINUE`, `DROP`, or `FATAL`:

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
- the V4-only `cflow_statechart_instance_hooks` table containing
  `abi_version`, exact `struct_size`, and `on_host_transaction`

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

There is no runtime fallback or prefix acceptance. A non-null hook table must
declare V4, use the exact V4 structure size, and provide `on_host_transaction`.
Callers migrate old behavior as follows:

| Legacy callback | V4 phase |
| --- | --- |
| `preprocess_external` and `on_event` | `PREPARE_TRIGGER` |
| `on_stable` and `on_stable_transaction` | `PREPARE_QUIESCENCE` |

First-party Statechart and TurboSCXML callers migrate in the same change so no
repository path depends on the removed layouts. Downstream callers must update
at source level and rebuild; binaries compiled against V1-V3 are incompatible.

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
drop, bounded queue/effect failures, rollback, exact V4 shape validation, and
rejection of all pre-V4 versions. The existing
`cflow_statechart_instance_test`, installed consumer, and C++ public-header test
are the minimum regression boundary.
