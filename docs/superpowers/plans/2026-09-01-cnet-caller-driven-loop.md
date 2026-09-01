# CNet Caller-Driven Loop Implementation Plan

> **Execution record:** Implemented inline. Checkboxes record locally verified work.

**Goal:** Remove CNet-owned I/O worker threads and make the application thread explicitly drive NativeIO progress and pooled connection coroutines.

**Architecture:** One `cnet_client` owns one NativeIO backend and one authoritative session owner. `cnet_client_poll()` drains bounded commands, observes NativeIO, resumes owner-affine coroutines, and invokes callbacks on its caller. Cross-thread progress is not part of the core contract; a later adapter may add an MPSC mailbox without changing the owner model.

**Tech Stack:** C11, NativeIO, TurboUtils coroutine pool, c-ares, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-31-cnet-actor-reactive-network-design.md`

## Global Constraints

- CNet and NativeIO create no I/O worker thread.
- One client is driven by one caller thread; callbacks are serialized inside `cnet_client_poll()`.
- All queues, request slots, coroutine frames, receive storage, and resolver results remain bounded.
- No backend, resolver, allocation, or synchronous-I/O fallback is selected silently.
- Existing connect/send/receive/state/error and exact-once terminal semantics remain unchanged.

---

### Task 1: Public poll contract and architecture decision

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `docs/superpowers/specs/2026-08-31-cnet-actor-reactive-network-design.md`
- Test: `cnet/tests/cnet_api_test.c`

**Interfaces:**
- Produces: `int cnet_client_poll(cnet_client *client, uint32_t timeout_ms, size_t *out_events);`
- Produces: caller-owned callback and shutdown semantics used by later tasks.

- [x] **Step 1: Write the failing API test**

```c
it("requires caller polling to advance callbacks") {
  size_t events = SIZE_MAX;
  check_equal(cnet_client_poll(&client, 0u, &events), TURBO_OK);
  check_equal(events, (size_t)0u);
}
```

- [x] **Step 2: Run the test and verify RED**

Run the configured Windows preset target and `cnet_api_test --filter "requires caller polling"`.
Expected: compile/link failure because `cnet_client_poll` does not exist.

- [x] **Step 3: Add the minimal declaration and contract**

Document that poll is single-owner, non-reentrant, invokes callbacks inline, returns the number of delivered events, and treats an idle timeout as successful zero progress.

- [x] **Step 4: Update the architecture spec**

Replace the internal shard-thread topology with caller-driven progress, record the compatibility change, and retain the same bounded ownership and terminal-state invariants.

### Task 2: Caller-driven owner execution

**Files:**
- Modify: `cnet/src/cnet_shards.h`
- Modify: `cnet/src/cnet_shards.c`
- Modify: `cnet/src/cnet_client.c`
- Test: `cnet/tests/cnet_shards_test.c`
- Test: `cnet/tests/cnet_api_test.c`

**Interfaces:**
- Consumes: `cnet_client_poll` public contract.
- Produces: `cnet_shards_poll()` and threadless init/stop/destroy.

- [x] **Step 1: Write failing owner-progress tests**

Add tests proving initialization does not advance a connection before polling, polling delivers callbacks on the polling thread, callback-issued commands are accepted without recursive callback delivery, and stop drains by driving the same owner.

- [x] **Step 2: Verify the tests fail under automatic background progress**

Run only `cnet_shards_test` and the focused `cnet_api_test` cases. Expected: callback progress occurs without the new poll boundary or the symbol is missing.

- [x] **Step 3: Remove the owner thread pool**

Delete `owner_pool`, owner task submission, worker stop flags, joins, and sleep polling. Initialization creates bounded owners only. `cnet_shards_poll()` calls `cnet_owner_drive()` directly.

- [x] **Step 4: Make stop caller-driven**

Close admission, enqueue close commands, repeatedly drive the owner within the supplied deadline, drain terminal callbacks, close NativeIO, and preserve the client on timeout.

- [x] **Step 5: Verify GREEN and adjacent lifecycle regressions**

Run focused API, shards, owner, dispatcher, TCP, UDP, and Pipe tests.

### Task 3: Threadless resolver progress

**Files:**
- Modify: `cnet/src/cnet_resolver.h`
- Modify: `cnet/src/cnet_resolver.c`
- Modify: `cnet/src/cnet_owner.c`
- Test: `cnet/tests/cnet_resolver_test.c`
- Test: `cnet/tests/cnet_api_test.c`

**Interfaces:**
- Produces: bounded caller-driven resolver polling integrated with the same owner loop.

- [x] **Step 1: Write failing resolver tests**

Test that a hostname query makes progress only when the caller drives the resolver and that cancel/close settle without an event thread.

- [x] **Step 2: Verify RED with `ARES_OPT_EVENT_THREAD`**

Run the resolver filter and confirm callback progress can occur independently of caller drive.

- [x] **Step 3: Replace event-thread initialization**

Initialize c-ares without `ARES_OPT_EVENT_THREAD`, expose its socket readiness and timeout requirements to the CNet owner, and process ready DNS descriptors on the caller thread. Do not introduce synchronous DNS or a hidden worker fallback.

- [x] **Step 4: Verify GREEN across hostname success, failure, cancellation, timeout, and module shutdown**

Run resolver, owner, API, and module tests on the configured preset.

### Task 4: Coroutine-pool and benchmark verification

**Files:**
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`
- Test: `cnet/tests/cnet_owner_test.c`

**Interfaces:**
- Consumes: caller-driven poll and threadless resolver.
- Produces: bounded reusable coroutine-frame ownership and comparable NativeIO/CNet measurements.

- [x] **Step 1: Verify bounded coroutine retention tests**

Assert task capacity, active awaits, cancellation retention, and lazy frame
reuse through NativeIO and owner tests.

- [x] **Step 2: Measure per-operation pooled coroutine cost**

Compare Direct NativeIO, the same operations through NativeIO coroutine
await/resume, and CNet with one common workload before changing coroutine
lifetime. This separates coroutine scheduling from the required public payload
copy and session/callback semantics; there is no OS-thread handoff.

- [x] **Step 3: Retain bounded pooled per-operation coroutines**

NativeIO lazily retains coroutine frames up to `request_capacity` and reuses
them after terminal completion. A persistent connection coroutine would still
perform the same await/resume switches and would require a second command-wait
resume protocol, so it is not introduced without profiling evidence.

- [x] **Step 4: Update the benchmark topology**

Drive CNet from the benchmark caller thread and report P50/P95, throughput,
receive/send admission, poll, callback, and public polls per round trip against
the same direct NativeIO workload.

- [x] **Step 5: Run correctness and performance verification**

Run focused tests, all CNet/NativeIO tests, then the Release benchmark. Treat performance as measured evidence; do not encode unstable timing thresholds in correctness tests.
