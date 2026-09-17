# NativeIO io_uring Batching Cancellation Amendment

Date: 2026-09-17
Issue: #298
Parent design: `docs/superpowers/specs/2026-09-17-native-io-linux-kernel-batching-design.md`

## Purpose

This amendment tightens the #298 io_uring design after implementation-plan review exposed a cancellation ambiguity created by deferred SQ submission. It supersedes the parent design only where this document is more specific about request submission state, cancellation of published-but-unsubmitted SQEs, partial `io_uring_enter()` submission, and ring-pressure testing.

The optimization goal is unchanged: adjacent SQEs are published in userspace and submitted in batches at progress boundaries instead of calling `io_uring_enter(..., to_submit=1, ...)` for every operation.

## 1. Request Submission State

`SALTS_IO_URING_PENDING` remains the request lifecycle phase, but pending requests now carry a separate submission state:

1. `QUEUED` — retained in the NativeIO lane but no SQE is present in the shared SQ ring. This is the normal state for non-head same-direction requests.
2. `PUBLISHED` — an SQE has been written into the mapped SQ ring and the SQ tail has been advanced, but that SQE has not yet been accepted by an `io_uring_enter()` submission call.
3. `SUBMITTED` — the SQE has crossed the kernel submission boundary and the request waits for a CQE.

Only the lane head may become `PUBLISHED` or `SUBMITTED`. A same-direction successor remains `QUEUED` until its predecessor reaches terminal CQ state.

This replaces the current binary `in_flight` interpretation. A `PUBLISHED` request is not equivalent to a purely userspace-queued request because its SQE occupies a ring slot and would execute if a later flush submitted it unchanged.

## 2. Bounded SQ Metadata

The backend keeps bounded metadata aligned with the mapped SQ ring. One metadata entry exists per SQ slot; no heap spill queue is added.

Each published SQ slot records one of:

- request operation, with the exact `native_io_request` handle;
- async-cancel control operation;
- discard NOP.

The backend also keeps a monotonic local sequence for the first published SQE not yet accepted by `io_uring_enter()`. The mapped SQ tail remains the publication tail. These two counters define the userspace-pending submission range.

Request metadata stores the exact generation-checked request handle so partial submission never marks a later reused request slot as submitted.

## 3. Publication and Flush

Publishing an SQE:

- checks mapped SQ capacity using the kernel SQ head and current published tail;
- writes the SQE and SQ-array index;
- records bounded slot metadata;
- publishes the SQ tail;
- marks request operations `PUBLISHED`;
- does not normally enter the kernel.

Flushing:

- submits every currently published-but-unsubmitted SQE in publication order;
- calls `io_uring_enter()` with the whole pending count rather than one SQE;
- on a positive partial result, advances the local pending-submit sequence by exactly the returned count and marks only those request metadata entries `SUBMITTED`;
- retries the remaining pending range;
- on a negative error, leaves the unsubmitted SQEs published and retryable, increments the native submission error statistic, and returns the error;
- never rewinds the mapped SQ tail after publication.

This tail rule is required because once several SQEs have been published, rewinding the global tail to undo one failed flush could discard unrelated operations published behind it.

## 4. Cancellation Semantics

### 4.1 `QUEUED`

Cancellation remains entirely in userspace: remove the request from its lane and queue the existing CANCELLED terminal completion. No kernel control operation is needed.

### 4.2 `PUBLISHED`

Cancellation must not simply release the request while leaving its original SQE in the mapped ring.

Instead, before that SQE crosses the kernel submission boundary:

1. validate that the recorded SQ metadata still identifies the exact request handle and generation;
2. replace the pending SQE in place with `IORING_OP_NOP`;
3. replace its metadata with `DISCARD_NOP`;
4. give the NOP a reserved discard `user_data` token that can never be mistaken for a request token;
5. remove the request from its lane and queue CANCELLED terminal state in userspace;
6. if it was the lane head, allow the next lane request to become `PUBLISHED` after the NOP in publication order.

The discard NOP is later flushed normally and its CQE is ignored except for impossible/error accounting. The original I/O operation can therefore never consume data after the user-visible cancellation.

Before `observe()` returns the userspace CANCELLED completion, it must flush pending SQEs so the replacement NOP and any newly published successor cross the submission boundary or a flush error is returned instead.

### 4.3 `SUBMITTED`

Cancellation keeps the existing kernel path: publish `IORING_OP_ASYNC_CANCEL` for the exact native request token. The request remains pending until the existing request/cancel CQ rules establish terminal state.

The async-cancel control SQE participates in the same batching path and may share a flush with unrelated pending SQEs.

## 5. Reserved Tokens

Request tokens continue to encode non-zero generation and non-zero request slot.

Use two explicit non-request control tokens:

- `0` for async-cancel control CQEs, preserving the current convention;
- `UINT64_MAX` for discard-NOP CQEs.

The current request-capacity bound (`request_capacity <= UINT32_MAX / 2`) means a generated request token cannot equal `UINT64_MAX`.

`uring_process_cq()` handles both control tokens before request-token lookup.

## 6. Observe Progress Boundary

The authoritative order is:

1. process CQEs already visible;
2. publish any lane successors unlocked by those CQEs;
3. flush all published-but-unsubmitted SQEs, including replacement NOPs, async cancels, and successors;
4. if flush fails, return that backend error without draining terminal request records;
5. drain terminal completions to the caller;
6. if caller completions are available, return;
7. otherwise block using the existing `poll(ring_fd, wake_fd)` timeout/wake behavior;
8. after readiness, repeat CQ processing, successor publication, flush, then terminal draining.

This guarantees that `observe()` never returns a terminal completion while work that the same progress step made runnable is stranded solely in userspace.

## 7. Ring Capacity and Pressure

The io_uring backend currently requests `2 * request_capacity` SQ entries. Under valid public API occupancy, at most one request-operation SQE per active request plus one cancel-control SQE per active request can be needed, so the normal request/control model is intentionally bounded by that ring-sizing rule.

The implementation must still handle a full mapped SQ defensively: flush the existing pending range, reload the kernel SQ head, and retry publication; return the existing busy/native error if capacity is still unavailable.

The deterministic test requirement is therefore:

- verify the ring sizing/capacity invariant through an internal snapshot or focused internal test;
- verify the defensive full-ring branch through an internal helper seam only if it can be done without adding a fake public configuration or production-only complexity.

A synthetic public workload that violates the established `2 * request_capacity` bound is not required.

## 8. Deterministic Structural Evidence

The #298 implementation must provide internal, non-installed test visibility sufficient to establish:

- two independent lane heads can both be `PUBLISHED` before any kernel-enter flush;
- one flush boundary requests a batch containing both operations;
- partial positive submission advances state for exactly the accepted prefix;
- CQ-driven lane successors are flushed before `observe()` returns the predecessor completion;
- a `PUBLISHED` request can be cancelled without its original operation reaching the kernel;
- a `SUBMITTED` request still uses async cancel;
- wake and timeout behavior remain unchanged.

Wall-clock values are not unit-test assertions. The existing release benchmark remains the external performance measurement.
