# CFlow Event/Source stability design

## Context

CFlow Event mailbox already provides a finite schema, fixed capacity, exact
status values, mutex-consistent statistics, and a single-consumer waitable. The
remaining stability gaps are in the older Source adapters:

- Source and Channel constructors can overwrite a live owning handle.
- Channel push reports only `bool`, so callers cannot distinguish invalid,
  full, and closed admission or observe bounded-resource pressure.
- Timer destruction assumes scheduler cancellation always quiesces the
  callback, although the Scheduler contract permits `cancel(id) == false` when
  the callback is already executing.
- Generic readiness accepts drivers without a cancellation operation and does
  not state when the borrowed waker stops being callable.

The affected state owners are the Source destination, Channel handle, Timer
callback admission, Run waker, and readiness driver registration.

## Decision

Keep existing successful behavior and the `cflow_channel_push()` signature.
Apply four bounded changes:

1. Every owning constructor rejects an occupied destination before allocation
   or mutation and publishes a new handle only after complete initialization.
2. Add `cflow_channel_try_push()` with exact status and a mutex-consistent stats
   snapshot. Keep `cflow_channel_push()` as a compatibility wrapper returning
   true only for `CFLOW_CHANNEL_OK`.
3. Give every accepted Timer callback its own retained state reference. Timer
   cancel clears future delivery, waits for an already-extracted waker to finish
   when called from another thread, and defers final storage release for
   reentrant close from inside that waker.
4. Require a readiness `cancel` callback. Its contract is quiescent unwatch:
   after it returns, the old borrowed waker is no longer retained or callable.
   Source destruction calls cancel before resource close.

Timer state remains the sole fact source. Its mutex protects admission, ready
state, the borrowed waker, reference count, and callback-in-flight count. A
thread-local active-callback marker avoids self-deadlock during reentrant Run
close without weakening cross-thread quiescence.

## Alternatives

- Change the existing Channel push return type: clearer in isolation, but an
  unnecessary source and ABI break. Rejected in favor of an additive API.
- Make Scheduler cancellation universally blocking: this changes the public
  Scheduler contract and can deadlock reentrant callbacks. Rejected.
- Let readiness destruction sleep or retain driver-owned state internally:
  impossible to implement correctly through the current opaque callback ABI.
  Rejected in favor of a precise driver boundary and fail-fast validation.
- Replace Channel with Event mailbox: the mailbox is heterogeneous and schema
  based, while Channel is a homogeneous Source adapter. Both abstractions have
  distinct consumers and remain supported.

## Failure, ownership, and shutdown semantics

Invalid or occupied outputs remain byte-for-byte owned by their original
object. Channel admission distinguishes invalid input, full capacity, and
closed state; no status silently allocates or blocks. Accepted values remain
FIFO and trivially copied.

For Timer, successful scheduler admission creates exactly one callback
reference. Successful scheduler cancellation releases it; otherwise the
callback releases it after any wake returns. Destroy releases only the Source
owner reference, so storage is freed exactly once after posting, callback, and
wake activity are quiescent.

For generic readiness, the driver owns the registered callback lifetime from a
successful `arm` until callback return or quiescent `cancel` return. `close`
runs only after that boundary.

## Compatibility, migration, and rollback

Existing Channel callers remain source-compatible through the bool wrapper.
Constructor overwrite changes only invalid ownership use from a leak/UAF risk
to immediate failure. Readiness adapters that omitted `cancel` must add a
quiescent unwatch callback; a no-op is valid only when `arm` never retains the
waker after returning.

No data, configuration, or deployment migration is required. The change can be
rolled back as one PR; callers that adopted the additive exact API would then
need to revert with it.

## Verification

- Mutation tests prove every rejected second constructor preserves the first
  live owner and that it remains usable and destroyable.
- Channel tests distinguish full and closed, validate counters and peak depth,
  and prove the bool wrapper preserves behavior.
- A controllable concurrent Scheduler test holds a Timer callback in flight,
  races cancellation/destruction, and proves destruction waits without UAF;
  a reentrant-wake test proves close does not self-deadlock.
- Readiness tests reject missing cancel and prove cancel completes before close.
- Run focused Release tests, all `^cflow_` tests, then affected ASan tests.
