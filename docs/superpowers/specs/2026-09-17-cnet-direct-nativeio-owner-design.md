# CNet Canonical Direct NativeIO Owner Design

Issue: #290  
Evidence experiment: #288 / draft PR #289  
Design branch: `design/cnet-direct-owner-290`

## 1. Decision

CNet stream ownership will use NativeIO's direct completion API as its single canonical execution path.

CNet will no longer create one NativeIO coroutine per stream I/O request. The owner will submit operations with `native_io_backend_submit()`, retain the returned generation-checked `native_io_request`, observe terminal completions with `native_io_backend_observe()`, and route each completion back to the owning CNet request record before calling the existing CNet terminal state machine.

NativeIO coroutine APIs remain supported for callers and higher-level components that want structured async control flow. This change removes only CNet's current per-I/O coroutine usage; it does not remove or deprecate NativeIO coroutine functionality.

There is no production runtime switch between coroutine and direct CNet owner modes. The final product has one owner implementation and one execution model.

## 2. Evidence and motivation

The pre-registered #288 experiment changed only the execution model around one CNet stream request while preserving the surrounding CNet state machine. Its primary Windows IOCP / TCP / 1 KiB measurement at exact head `89532f734ddfdb9de15c8d63ea2d8f19153aa945`, workflow run `35119930058` (#327), produced:

- B0 per-I/O coroutine owner p50 median: 45.000 us;
- B1 direct NativeIO owner p50 median: 43.600 us;
- paired p50 B1 vs B0: -3.11% +/- 0.24 percentage points;
- paired p95: -2.60% +/- 1.71 percentage points;
- paired rate: +1.58% +/- 1.65 percentage points;
- same-run NativeIO direct A/A p50 noise envelope: 1.26 percentage points;
- raw p50 direction: four faster, one tie, zero slower.

The direct variant also passed the release correctness matrix on Windows IOCP, Linux epoll, Linux io_uring, and macOS kqueue.

The experiment therefore closed with the pre-registered outcome:

> `direct owner lifecycle has measurable signal`

The #289 implementation remains evidence machinery only. Its source transformation, duplicate diagnostic libraries, dynamic-load benchmark runner, and profile-only mode seam are not the desired production architecture.

## 3. Scope

This design changes the CNet owner-side execution of accepted stream I/O requests.

In scope:

- TCP and VSOCK connect requests that use the stream owner;
- TCP, VSOCK, and pipe reads/writes that use the stream owner;
- TLS cipher reads/writes because they are driven through the same owner request machinery;
- direct cancellation and terminal completion routing;
- partial stream-send continuation;
- owner profiling semantics that currently refer to coroutine lifecycle;
- removal of CNet-internal coroutine ownership statistics that become meaningless after migration;
- correctness and performance verification for the canonical path.

Out of scope:

- removing NativeIO coroutine APIs;
- adding a public CNet execution-mode option;
- persistent per-connection coroutine design;
- dispatcher, client-lock, or command-queue bypass;
- backend-specific IOCP-only fast paths;
- #286 retained-buffer zero-copy implementation;
- changes to user-visible CNet connection, callback, timeout, or payload-lifetime semantics.

## 4. Current architecture

Today each `cnet_owner_request` stores a `native_io_coroutine_task`. `cnet_owner_start_request()` acquires the bounded CNet request record and starts a fresh NativeIO coroutine. The coroutine repeatedly calls `native_io_coroutine_await()` for a logical request, including partial stream-send continuation, and finally calls `cnet_owner_complete()`.

Cancellation uses `native_io_backend_cancel_coroutine()`. `cnet_owner_drive()` calls `native_io_backend_observe()`, but direct completions are not expected: coroutine-owned completions are consumed by NativeIO to resume the corresponding coroutine. A non-zero direct completion count is therefore currently treated as `SALTS_EPROTO`.

The surrounding CNet semantics are already centralized outside the coroutine entry:

- request/session capacity and ownership accounting;
- command ownership and release;
- deadline scheduling and expiry;
- connection/session transitions;
- TLS pumping;
- event publication and backpressure;
- terminal request release;
- receive re-arm;
- session finalization.

The production migration must preserve those mechanisms rather than reimplement them.

## 5. Target architecture

The canonical stream request lifecycle becomes:

```text
CNet command / receive re-arm / TLS pump
        |
        v
cnet_owner_start_request()
        |
        +-- acquire bounded cnet_owner_request
        +-- attach logical CNet metadata
        +-- set operation.user_data = CNet request token
        +-- native_io_backend_submit()
        +-- store returned native_io_request {slot,generation}
        +-- schedule logical deadline if configured
        |
        v
cnet_owner_drive()
        |
        +-- native_io_backend_observe()
        +-- for every returned completion:
              locate CNet request by user_data token
              validate active CNet record
              validate completion.request == stored native_io_request
              validate completion.endpoint == current operation endpoint
              route partial send or terminal completion
        |
        v
cnet_owner_complete()
        |
        +-- existing event/session/TLS/finalization semantics
```

Cancellation becomes:

```text
native_io_backend_cancel(stored native_io_request)
        |
        +-- SALTS_OK: cancellation requested, request remains owned
        +-- SALTS_EALREADY: terminal already racing, request remains owned
        |
        v
native_io_backend_observe()
        |
        v
authoritative terminal completion
        |
        v
CNet request may be released
```

## 6. Request identity and stale-completion safety

### 6.1 NativeIO identity is authoritative

`native_io_request` contains `slot` and `generation`, and `native_io_completion.request` returns that identity. A CNet request record stores the exact handle returned by its latest successful direct submit.

CNet must never treat an array index, pointer address, or `user_data` token alone as proof that a completion belongs to a request.

### 6.2 CNet routing token

Before submit, CNet sets `native_io_operation.user_data` to a non-zero token identifying the bounded CNet request record. The preferred token is `request_index + 1` because request capacity is already bounded to a range representable by `uintptr_t`; zero remains invalid.

On completion:

1. `user_data` must decode to an in-range CNet request record;
2. the record must still be active and owned by the current owner;
3. the returned `completion.request.slot` and `completion.request.generation` must exactly match the record's stored `native_io_request`;
4. the returned endpoint must match the endpoint of the currently submitted operation;
5. only then may CNet mutate or settle the logical request.

A stale or duplicate completion that targets a recycled CNet record therefore fails the NativeIO generation check even if it carries the same record token.

A mismatch is an internal protocol error and must fail closed; it must not settle a different request.

## 7. Direct request admission

`cnet_owner_start_request()` remains the sole helper that converts one accepted logical CNet operation into an owned CNet request.

The existing ordering is retained as far as possible:

1. acquire a free CNet request record;
2. copy session, command, role, operation, stage, logical size, and close-after-send metadata;
3. transfer command ownership into the request when applicable;
4. increment session and owner active-request counts;
5. set read/write-active flags according to role;
6. assign the CNet request routing token to the copied operation;
7. call `native_io_backend_submit()`;
8. on success, store the returned `native_io_request`;
9. schedule the existing logical request deadline when configured.

A synchronous submit failure follows the existing started-request failure path: CNet releases the request record and command ownership according to current rules, records the same session failure stage, and preserves current accepted-command asynchronous failure semantics.

If deadline scheduling fails after a successful submit, CNet records the failure and requests cancellation of the submitted NativeIO request. The CNet request must remain owned until the resulting authoritative terminal completion is observed. Failure to schedule a deadline is not permission to free a buffer or request still borrowed by NativeIO.

## 8. Completion-batch processing

`cnet_owner_drive()` becomes the direct completion router.

`native_io_backend_observe()` may return up to `completion_batch_capacity` terminal completions. CNet must consume and settle the complete returned batch before discarding the array or returning from that drive because every returned completion ends a NativeIO borrow and names a terminal request.

For each completion, the owner performs the identity checks in section 6 and then dispatches by request role.

Event publication may fill the public event queue while a completion batch is being processed. Existing owner-local pending-event storage remains the backpressure mechanism. Its capacity is already derived from the completion batch size and must remain sufficient to let the owner settle the whole observed batch without losing later terminal completions.

The owner must not stop processing a returned completion batch merely because an earlier completion published or queued a user-visible event.

After the complete batch is settled, normal drive exit rules apply: pending/public events may cause the drive to return before another blocking observe.

Receive re-arm remains deferred through the existing receive-rearm queue and occurs on a subsequent owner-drive stage. This preserves the current protection against reusing receive storage while other completions from the observed batch are still being settled.

## 9. Partial stream-send continuation

A logical CNet send remains one CNet request, one command ownership interval, one logical deadline, and one final `CNET_EVENT_SEND`.

When a direct completion for SEND or TLS_WRITE is `NATIVE_IO_COMPLETION_OK`:

1. `completion.bytes` must be non-zero and no larger than the currently submitted operation length;
2. add the bytes to the logical request's `completed_size`;
3. if `completed_size == requested_size`, normalize terminal completion bytes to the logical total and enter `cnet_owner_complete()`;
4. otherwise, datagram partial send is an error as today;
5. for stream/pipe continuation, advance `operation.buffer` and reduce `operation.length`;
6. reapply the same CNet request routing token;
7. directly submit the remaining operation;
8. replace the request record's stored `native_io_request` with the newly returned generation-checked handle.

The CNet request record, command view, active counts, read/write-active flag, and deadline are not released or recreated between partial submits.

A partial-send resubmit failure uses the existing started-request failure/session-failure path. No send event is published for an incomplete logical send.

## 10. Cancellation, deadlines, and close races

All owner cancellation helpers switch from coroutine task cancellation to direct request cancellation using the stored `native_io_request`.

The NativeIO contract is preserved exactly:

- `SALTS_OK` from `native_io_backend_cancel()` means cancellation was marked, not terminal;
- `SALTS_EALREADY` means the native operation is already completing;
- neither result permits CNet request release;
- only the matching observed terminal completion ends the NativeIO borrow and permits CNet terminal settlement.

This applies to:

- read/write deadline expiry;
- connect timeout;
- explicit close while I/O is pending;
- session failure that cancels sibling requests;
- stop/drain paths.

The existing CNet pending failure status/stage remains the source for user-visible timeout/shutdown error semantics. A later CANCELLED completion must not overwrite an earlier recorded timeout/failure reason.

## 11. Existing semantic handlers remain canonical

`cnet_owner_complete()` remains the single logical terminal handler.

The migration must not duplicate its behavior for:

- successful connect and connected event publication;
- EOF handling;
- receive event publication;
- receive-demand/re-arm behavior;
- send event publication;
- close-after-send;
- failure propagation and sibling cancellation;
- TLS read/write pump transitions;
- session finalization.

Direct routing may perform only the mechanics required before a logical terminal: identity validation, partial-send accumulation/resubmit, and normalization of the final completion.

This boundary is important: the experiment showed the execution wrapper is removable; it did not justify rewriting the CNet state machine.

## 12. TLS, pipe, VSOCK, UDP, and adopted sockets

TLS cipher reads/writes already use `cnet_owner_start_request()` and therefore migrate automatically to direct request ownership. TLS handshake/plaintext state stays in `cnet_tls_state` and `cnet_owner_tls_pump()`.

Pipe and VSOCK stream operations use the same owner request machinery and therefore follow the same direct completion rules on supported backends.

Adopted TCP/VSOCK sockets preserve their existing transport setup and immediate-open behavior; only later owner I/O requests change execution model.

UDP's existing immediately connected transport behavior and datagram-specific semantics are not redesigned. Where UDP uses the shared request helper, it still receives direct NativeIO request ownership, but datagram partial sends remain invalid.

## 13. Command and payload ownership

This change does not alter CNet command ownership.

For copied sends, the request continues to retain the command view until the logical send reaches its existing terminal release point.

For #286 retained-buffer sends, once that work lands, the command entry remains the sole CNet owner of the retained buffer. The direct NativeIO operation borrows command payload bytes exactly as the coroutine await path does today. Partial sends advance a borrowed pointer inside that same command-owned storage; no second payload owner is introduced.

NativeIO's documented payload borrow lasts from successful submit until the matching terminal completion is returned by observe. Therefore a request may replace/advance the borrowed operation only after the previous matching completion has been observed.

## 14. Profiling semantics

The installed CNet API remains unchanged. Internal profiling must describe the canonical direct path instead of preserving coroutine-specific wording.

The profiling design follows these rules:

- `owner_drive_ns` and `observe_ns` keep their current meanings;
- the stage currently called `request_start_ns` represents the first direct NativeIO submit of one logical CNet request rather than coroutine spawn-through-first-await;
- partial-send resubmission cost must not be silently mislabeled as coroutine control; it is either separately accounted for in owner/completion residuals or given an explicitly named internal diagnostic field if exact attribution requires it;
- `request_completion_ns` continues to represent logical CNet terminal completion control and excludes intermediate partial-send completions;
- benchmark documentation and fixed-control attribution must be reviewed so no formula continues to claim that `request_start_ns` measures coroutine spawn.

Performance acceptance is based on uninstrumented p50/p95/rate comparison rows. Stage attribution is diagnostic evidence and must not be used if its accounting identity is invalid after the semantic change.

## 15. Removal of CNet coroutine ownership surface

`cnet_owner_get_coroutine_stats()` is an internal CNet owner API whose stated purpose is to report coroutine state owned by the shard. Once CNet no longer owns per-I/O coroutines, returning zero-filled values would preserve a name while destroying its meaning.

The canonical migration therefore removes:

- `native_io_coroutine_task coroutine` from `cnet_owner_request`;
- `cnet_owner_coroutine_entry()`;
- owner-local `coroutine_status` plumbing;
- `cnet_owner_get_coroutine_stats()` and the CNet owner tests that assert shard-owned coroutine capacity/activity.

NativeIO's public `native_io_backend_get_coroutine_stats()` and all NativeIO coroutine APIs remain unchanged.

## 16. Production source shape

The final implementation has one canonical `cnet/src/cnet_owner.c`.

The production PR must not retain #289 experimental machinery:

- no source-text generator for a second owner implementation;
- no duplicate direct-owner CNet production/diagnostic DSO used as an execution mode;
- no runtime direct/coroutine owner mode;
- no profile-only mode setter;
- no dynamic loading requirement in CNet runtime;
- no per-I/O `native_io_backend_spawn_coroutine()` in the CNet stream owner.

Temporary verification tooling may exist only if it is clearly test/CI-only and does not create a second production owner implementation.

## 17. Correctness verification

Implementation follows TDD and must prove the direct lifecycle rather than only relying on the #288 spike.

Required deterministic coverage includes:

- successful TCP connect terminal;
- normal send terminal;
- deterministic partial stream-send continuation with one logical send event;
- receive re-arm;
- read timeout;
- write timeout;
- connect timeout;
- close with pending read/write;
- direct cancel returning `SALTS_OK` while terminal remains pending;
- direct cancel returning `SALTS_EALREADY` while terminal remains pending;
- peer failure;
- stop/drain;
- stale or duplicate completion token cannot settle a recycled CNet request;
- matching CNet token with mismatched NativeIO generation fails closed;
- TLS client/server owner integration and STARTTLS paths;
- pipe and VSOCK paths where supported;
- command/payload ownership remains exact through terminal settlement.

Exact-head release CI must pass on:

- Windows IOCP;
- Linux epoll;
- Linux io_uring;
- macOS kqueue.

No performance conclusion is valid before the correctness matrix is green.

## 18. Performance verification

Primary surface remains Windows IOCP / TCP / 1 KiB because #288 demonstrated that this workload exposes fixed lifecycle overhead without large payload-copy dominance.

The production migration requires controlled exact-head evidence with:

- five matched repeats;
- 32 warmups;
- 512 measured persistent round trips per sample;
- uninstrumented comparison rows;
- paired p50, p95, and rate median/MAD;
- same-run NativeIO direct A/A null control.

The comparison baseline must be the pre-migration canonical coroutine owner, not an unrelated historical hosted run. Verification should build/run baseline and candidate within the same CI job or otherwise produce a same-host, same-run paired artifact. This may use CI-only orchestration around the base SHA and candidate SHA; it must not require retaining a second production owner implementation in the source tree.

Acceptance requires:

1. candidate correctness is green;
2. the direct-owner p50 direction remains beneficial relative to the coroutine baseline;
3. a claimed measurable benefit must exceed the same-run A/A p50 noise envelope;
4. p95 and rate show no material aggregate regression hidden by p50;
5. raw repeats do not show direction instability that invalidates the aggregate claim.

The exact #288 -3.11% p50 number is not a hard target because hosted runners vary. If the canonical implementation's benefit falls below the same-run noise floor, the production PR must report that result rather than claim the spike number; it may still be acceptable only if correctness is complete, no regression is present, and the implementation difference is understood.

## 19. Interaction with #286 retained-buffer zero-copy

#290 removes fixed execution/control overhead. #286 removes payload-size-dependent command staging copies. They remain orthogonal.

The #290 implementation must not redesign `cnet_send_buffer()` or retained-buffer ownership. If #286 lands first, #290 must preserve its ownership rules. If #290 lands first, #286 must treat the new direct owner as a borrower of the command-owned payload.

After both changes are production-ready, a separate 64 KiB measurement may quantify their combined effect. That combined measurement is not an acceptance gate for either individual issue.

## 20. Migration boundary and rollback

The implementation should be reviewable as one semantic migration, not as a permanent dual-mode period.

During development, RED tests may be introduced before the direct path. Once GREEN, the canonical owner switches to direct submit/observe/cancel and coroutine-specific CNet owner code is removed in the same production branch.

If direct completion routing cannot satisfy an existing semantic contract, the migration stops and the contract mismatch is resolved before performance work continues. The project must not hide divergence behind a runtime fallback to the old coroutine owner.

Rollback of the production change is a normal git revert to the prior canonical owner; there is no runtime compatibility flag to maintain.

## 21. Final invariants

The production design is complete only when all of the following are true:

- one canonical CNet stream owner exists;
- every accepted direct NativeIO operation is represented by one active bounded CNet request record;
- every active record stores the exact current NativeIO request handle;
- completion routing requires both a valid CNet record token and matching NativeIO slot/generation;
- no CNet request or payload borrow is released before its authoritative terminal completion is observed;
- partial stream sends retain one logical CNet request and one logical deadline across resubmits;
- cancellation never substitutes for terminal observation;
- `cnet_owner_complete()` remains the single logical terminal semantic handler;
- CNet no longer spawns a coroutine per stream request;
- NativeIO coroutine APIs remain available outside CNet;
- no production runtime mode switch or duplicate owner implementation remains;
- exact-head correctness and controlled performance evidence are recorded before merge.
