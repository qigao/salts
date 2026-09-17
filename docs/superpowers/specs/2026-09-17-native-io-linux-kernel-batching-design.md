# NativeIO Linux Kernel Transition Batching Design

Date: 2026-09-17
Issues: #298, #299
Baseline branch: `master` at `c4197712261a563ed7e238152b34cb50a2ef98a9`
Benchmark evidence source: PR #295 exact head `88d4677e34b86c3210b3168450f435554e1a4ab0`

## 1. Context

PR #295's four-platform `cnet_io_benchmark` isolates a Linux-specific NativeIO cost. The same public NativeIO request/completion abstraction is approximately competitive with libuv on Windows/IOCP and macOS/kqueue, while Linux epoll and io_uring show a repeatable gap on the persistent TCP/UDP echo workload.

The Linux implementations currently cross the userspace/kernel boundary too eagerly:

- io_uring publishes one SQE and immediately calls `io_uring_enter(..., to_submit=1, ...)`;
- readiness/epoll performs a nonblocking socket attempt during `submit()`, then directly calls `epoll_ctl()` whenever the derived read/write interest mask changes.

The intended optimization is therefore not a CNet change and not a change to the NativeIO public abstraction. It is a backend progress-policy change: preserve request semantics while reducing kernel transitions by batching or coalescing work that is already adjacent in userspace.

Implementation order is fixed:

1. #298: io_uring deferred/batched SQ submission.
2. Measure exact-head A/B.
3. #299: epoll deferred/coalesced interest removal and rearm.
4. Measure exact-head A/B again.

The two changes must not be implemented together before the first measurement because doing so would destroy attribution.

## 2. Baseline Evidence

On PR #295 exact head, Linux io_uring TCP p50 NativeIO direct versus libuv is:

| payload | NativeIO direct | libuv |
| ---: | ---: | ---: |
| 1 KiB | 34.843 us | 32.569 us |
| 4 KiB | 36.024 us | 34.482 us |
| 8 KiB | 42.464 us | 34.292 us |
| 16 KiB | 45.788 us | 43.556 us |
| 32 KiB | 51.558 us | 49.815 us |
| 64 KiB | 73.460 us | 69.605 us |

The io_uring diagnostic path reports two submits and two observes per RTT. Direct submit time ranges from about 11.2 us/RT at 1 KiB to 26.4 us/RT at 64 KiB.

On the same exact head, Linux epoll TCP p50 NativeIO direct versus libuv is:

| payload | NativeIO direct | libuv |
| ---: | ---: | ---: |
| 1 KiB | 34.892 us | 33.190 us |
| 4 KiB | 35.934 us | 30.716 us |
| 8 KiB | 38.058 us | 33.321 us |
| 16 KiB | 45.199 us | 42.304 us |
| 32 KiB | 55.044 us | 51.398 us |
| 64 KiB | 72.691 us | 69.075 us |

The epoll diagnostic path also reports two submits and two observes per RTT. Direct submit time ranges from about 11.5 us/RT at 1 KiB to 26.3 us/RT at 64 KiB.

These timings are evidence for prioritization, not correctness thresholds. Unit tests must verify structure and semantics rather than wall-clock deltas.

## 3. Goals

The design must:

- reduce Linux NativeIO kernel transitions on adjacent operations;
- preserve the public `native_io_backend_submit/cancel/observe` API and request handles;
- preserve generation checks, bounded request capacity, endpoint ownership, FIFO stream lane ordering, cancellation, wake, shutdown, and terminal-completion lifetime;
- preserve the existing single-owner backend model; no background worker is introduced;
- allow deterministic tests to prove that batching/coalescing happens without depending on benchmark timing;
- retain the existing four-platform benchmark as external evidence.

## 4. Non-goals

This work does not add:

- registered io_uring buffers or fixed files;
- SQPOLL;
- multishot receive;
- edge-triggered or `EPOLLONESHOT` epoll semantics;
- an io_uring-based `epoll_ctl` implementation;
- CNet receive/send policy changes;
- public API changes;
- a new scheduler or background progress thread.

## 5. Required Invariants

### 5.1 Request acceptance

A successful `native_io_backend_submit()` still means the backend has accepted ownership of a request record and the operation will make progress when the caller drives the backend with `observe()`.

A request must never be reported as accepted if its request slot or endpoint state was not successfully reserved.

### 5.2 Stream ordering

For each endpoint lane, only the lane head may be in-flight in the kernel. A successor may be published only after the previous head reaches a terminal CQ/completion state.

Batching across independent read/write lanes is allowed. Batching must not cause two same-direction stream operations on one endpoint to consume bytes out of FIFO order.

### 5.3 Terminal ownership

Terminal request records remain retained until `observe()` copies their completion to the caller. Batching must not release or reuse a request slot earlier than the existing contract.

### 5.4 Cancellation

Cancellation of a request that has not entered the kernel remains a userspace terminal transition.

Cancellation of an in-flight io_uring request still uses `IORING_OP_ASYNC_CANCEL` and does not become terminal until the corresponding request/cancel completion rules are satisfied.

### 5.5 Wake

`native_io_backend_wake()` must continue to interrupt a blocking `observe()` without depending on pending network/file I/O. Existing `eventfd` wake semantics remain authoritative for the io_uring backend.

### 5.6 No hidden asynchronous worker

All flushing/progress occurs on the calling thread inside submit/cancel only when required for capacity/error semantics, or inside `observe()` at defined progress boundaries.

## 6. E1 — io_uring Deferred/Batched SQ Submission (#298)

### 6.1 Current behavior

`uring_publish_sqe()` currently:

1. loads SQ head/tail;
2. writes one SQE and SQ array entry;
3. publishes the new SQ tail;
4. immediately calls `io_uring_enter(..., 1, 0, 0)`;
5. rolls back the tail if the enter fails.

The benchmark submits receive then send before calling `observe()`. The current implementation therefore performs two kernel submission entries where one batched entry is sufficient.

### 6.2 New submission model

Split the current operation into two concepts:

- `uring_publish_sqe()`: userspace publication only;
- `uring_flush_sq()`: submit all currently published-but-not-consumed SQEs to the kernel.

`uring_publish_sqe()` must not normally call `io_uring_enter()`.

It may flush only when SQ capacity would otherwise prevent publication. If a capacity-triggered flush fails, publication fails and the caller observes the same failure synchronously.

### 6.3 Flush calculation

The backend may derive pending SQ work from the mapped SQ head/tail rather than maintaining a second unbounded queue.

A flush must:

1. load kernel-visible SQ head with acquire ordering;
2. load the published SQ tail;
3. compute `tail - head` using the ring's unsigned monotonic counters;
4. if zero, return success without a syscall;
5. call `io_uring_enter(ring_fd, pending, 0, 0, ...)`;
6. handle interrupted syscalls as today;
7. tolerate partial submission by retrying the remaining published entries until no pending entries remain or a real error occurs.

The implementation must not rewind a published SQ tail after another SQE has been published behind it. Once publication is decoupled from enter, rollback-by-tail is no longer a valid general failure mechanism.

### 6.4 Observe progress boundary

`uring_observe()` remains responsible for external progress and still uses `poll()` on both `ring_fd` and `wake_fd` so the existing wake contract is preserved.

Before `observe()` blocks, it must flush all pending SQEs.

It must also flush SQEs generated while processing CQ completions before returning to the caller. This is required because `uring_process_cq()` can complete a lane head and publish the next lane request; without a final flush, that successor would remain userspace-only until a later `observe()` call.

Required order at an observe boundary:

1. process already available CQEs;
2. drain terminal completions into the caller batch;
3. flush SQEs generated before or during CQ processing;
4. if caller completions are available, return them;
5. otherwise block in `poll()` using the existing timeout/wake semantics;
6. after poll readiness, process CQEs, drain terminals, flush newly published successors, then return/continue.

This preserves current wake behavior while still batching the common receive+send pair into one kernel submission.

### 6.5 Ring-pressure behavior

If publication finds the SQ ring full:

- first flush all published SQEs;
- reload SQ head;
- retry capacity once progress is visible;
- return the existing busy/error result if no slot becomes available or the flush fails.

No heap spill queue is introduced.

### 6.6 Cancellation

An in-flight cancel SQE is published through the same batching path. If the caller subsequently calls `observe()`, the cancel SQE is flushed with any other pending work.

If publication needs capacity, the same pressure flush rule applies.

A non-in-flight queued request is still cancelled entirely in userspace exactly as today.

### 6.7 E1 deterministic evidence

Add backend-visible test instrumentation or an internal test hook sufficient to verify at least:

- two independent accepted head operations can be published before one SQ flush boundary;
- `observe()` flushes pending work before blocking;
- CQ-driven lane successor publication is flushed before `observe()` returns a terminal completion;
- capacity pressure forces a flush without losing SQEs;
- cancellation still reaches a terminal state;
- wake still interrupts `observe()`.

The test seam must remain internal and must not alter the public ABI.

## 7. E2 — epoll Deferred/Coalesced Interest Removal (#299)

### 7.1 Current behavior

The readiness backend derives an endpoint mask from non-empty read/write lanes. Every mask change calls the driver immediately. The epoll driver maps that directly to `epoll_ctl(ADD/MOD/DEL)`.

For a receive completion followed immediately by a receive rearm, the logical sequence can be:

`READ -> 0 -> READ`

If both transitions reach the kernel, NativeIO pays a DEL/MOD plus a later ADD/MOD even though the externally useful final state is unchanged.

### 7.2 Correctness constraint on deferral

The first registration/expansion of an interest can fail with a native driver error. Existing `submit()` behavior can surface that error synchronously. The optimization must not silently convert all such failures into later `observe()` failures.

Therefore E2 uses asymmetric deferral:

- interest additions/expansions required to make a newly accepted request observable are applied synchronously when the applied mask does not already cover the desired mask;
- completion-driven interest removals may be deferred because keeping an already-applied interest temporarily active is safe as long as the backend does not block indefinitely or spin on it;
- explicit cancellation paths that currently promise a synchronous driver-update failure continue to flush their required update before returning;
- before `observe()` blocks, any stale applied interests that no longer correspond to pending lanes are flushed.

This narrower rule captures the hot completion/rearm case while minimizing API error-timing changes.

### 7.3 Endpoint state

Separate logical desired interests from kernel-applied interests.

Each readiness endpoint needs:

- `applied_interests`: mask known to be installed in the driver;
- desired interests derived from current lane heads;
- a dirty marker indicating that desired and applied state may differ.

A bounded dirty-endpoint queue/array sized to endpoint capacity tracks endpoints requiring a later shrink/removal flush. An endpoint is inserted at most once while dirty.

If an endpoint transitions back to its already-applied mask before flush, it remains in the dirty queue but the flush becomes a userspace no-op and clears the dirty flag. This is the desired `READ -> 0 -> READ` coalescing behavior.

### 7.4 Submit behavior

After adding a request to a lane:

- derive the desired mask;
- if `desired` contains bits not present in `applied_interests`, synchronously call the driver update and preserve current submit failure/rollback behavior;
- if `applied_interests` already covers `desired`, no kernel update is required;
- if the endpoint was dirty because a prior completion scheduled removal, restoring the desired mask to the applied mask cancels that removal in userspace.

### 7.5 Completion behavior

When `readiness_drive_lane()` completes and removes a lane head:

- recompute desired interests;
- if desired is a subset of applied interests, mark the endpoint dirty instead of immediately issuing `epoll_ctl`;
- do not synchronously remove the kernel interest merely to return a terminal completion.

This creates the coalescing window in which CNet/user code can rearm receive before the next blocking wait.

### 7.6 Observe flush boundary

Before the readiness backend performs a blocking driver `wait()`:

- flush dirty endpoint masks to the driver;
- clear each dirty marker after successful synchronization;
- if a driver update fails, return that error instead of blocking with an inconsistent desired/applied state.

If a dirty endpoint's desired mask again equals the applied mask, clear it with no driver call.

The backend may process already available terminal completions before flushing; it must not enter a blocking wait while stale applied interests remain unexamined.

### 7.7 Cancel and release

Cancellation retains its current externally visible error behavior: if cancel removes a request and requires an interest shrink whose driver failure would previously be returned by cancel, flush that endpoint synchronously before returning success.

Endpoint release remains forbidden while active requests exist. Before final endpoint reuse, its dirty state must be cleared and its applied interest mask must be zero so stale registrations cannot leak across generations.

### 7.8 E2 deterministic evidence

Add internal counters/test hooks sufficient to verify:

- initial `0 -> READ` still applies synchronously;
- completion `READ -> 0` can be deferred;
- completion followed by rearm before wait yields no DEL+ADD pair;
- completion without rearm flushes removal before the next blocking wait;
- cancel preserves synchronous update/error behavior;
- endpoint release/reuse cannot inherit stale interests;
- wake and timeout semantics remain unchanged.

## 8. Measurement Protocol

Use the existing release `cnet_io_benchmark` protocol unchanged for the external comparison:

- five independent repeats;
- 32 warmups;
- 512 persistent RTTs per repeat;
- driver order rotation;
- p50, p95, and rate reported as medians with paired delta/MAD;
- stage profiling performed in separate diagnostic repeats.

For #298, measure only the io_uring host against the exact pre-change baseline before starting #299.

For #299, measure the epoll host against its exact pre-change baseline. Also rerun io_uring as a regression control if the shared readiness/native-io surface changed during implementation.

No acceptance gate is a single percentage target. The required claim is structural first: fewer kernel submission/update transitions with preserved correctness. Timing is used to quantify the effect and determine whether follow-up work is warranted.

## 9. Acceptance Gates

### #298

- focused NativeIO io_uring tests prove batching structurally;
- existing NativeIO and NativeIPC contracts pass;
- CNet contracts pass;
- no regression in shared-runtime file/pipe cancellation/forget behavior;
- exact-head Linux io_uring release benchmark is captured;
- benchmark report includes pre/post NativeIO direct p50/p95/rate and submit/observe stage timings;
- implementation stays scoped to io_uring/backend test/instrumentation unless a correctness defect forces a documented expansion.

### #299

- focused readiness/epoll tests prove interest coalescing structurally;
- existing NativeIO contracts pass;
- CNet contracts pass;
- no lost wakeup, timeout, cancel, release, or generation regression;
- exact-head Linux epoll release benchmark is captured;
- report includes kernel-interest update counts plus pre/post p50/p95/rate;
- io_uring remains green as a regression control if shared readiness code is touched.

## 10. Risks and Failure Modes

### Published but unflushed io_uring SQEs

Risk: accepted requests stall because no later boundary submits them.

Control: `observe()` flushes before every blocking wait and flushes CQ-generated successors before returning.

### Partial `io_uring_enter` submission

Risk: assuming all requested SQEs were submitted can strand ring entries.

Control: flush recomputes/retries until no published entries remain pending or a real error is returned.

### io_uring lane successor latency

Risk: a successor published during CQ processing waits for the caller's next `observe()`.

Control: final flush after CQ processing and before returning terminal completions.

### epoll stale writable interest spin

Risk: deferring removal of an always-ready write interest can cause repeated immediate waits.

Control: dirty interests are flushed before any blocking driver wait. Deferral exists only across the userspace completion/rearm window, not across the next wait boundary.

### epoll error-timing drift

Risk: deferring all updates would move native errors from submit/cancel to observe.

Control: apply required interest expansions synchronously; keep cancel-required synchronization synchronous; defer primarily completion-driven shrink/removal.

### Cross-platform regression

Risk: refactoring generic NativeIO code changes IOCP/kqueue behavior.

Control: #298 stays io_uring-specific; #299 changes shared readiness state but kqueue semantics must remain covered by the four-platform contract suite, while epoll-only timing claims remain platform-specific.

## 11. Implementation Sequencing

The implementation plan must preserve the following gates:

1. #298 tests/instrumentation RED.
2. #298 minimal GREEN.
3. focused contracts.
4. exact-head io_uring benchmark and evidence commit/comment.
5. review #298 before starting #299.
6. #299 tests/instrumentation RED.
7. #299 minimal GREEN.
8. focused contracts.
9. exact-head epoll benchmark plus io_uring regression control where applicable.
10. final review with each performance claim tied to exact-head evidence.

#295 remains independent; none of these backend changes are to be folded into its zero-copy-slice implementation branch.