# CNet Canonical Direct NativeIO Owner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace CNet stream-owner per-I/O coroutine execution with one canonical direct NativeIO submit/observe/cancel path while preserving existing CNet semantics and the measured #288 performance benefit direction.

**Architecture:** `cnet_owner_request` becomes the sole owner of one logical CNet request plus the currently active generation-checked `native_io_request`. `cnet_owner_drive()` directly consumes NativeIO completion batches, validates `request_index + 1` routing tokens plus `slot/generation` identity, resubmits partial stream writes in the same logical request, and delegates only true logical terminals to the existing `cnet_owner_complete()` state machine. NativeIO coroutine APIs remain unchanged for other consumers; CNet has no runtime execution-mode switch.

**Tech Stack:** C11, CMake, TinyTest, Salts NativeIO, CNet owner/session/command/event/TLS layers, GitHub Actions release benchmark matrix.

**Spec:** `docs/superpowers/specs/2026-09-17-cnet-direct-nativeio-owner-design.md`

## Global Constraints

- Final production state has exactly one canonical `cnet/src/cnet_owner.c`; do not retain the #289 source generator, duplicate direct-owner libraries, profile-only mode setter, or runtime direct/coroutine switch.
- `native_io_operation.user_data` is exactly `request_index + 1`; zero is invalid.
- A completion may mutate a CNet request only after validating the active record, the exact stored `native_io_request {slot,generation}`, and the currently submitted endpoint.
- Consume the complete completion batch returned by one `native_io_backend_observe()` call before returning an error; retain the first error and continue safely identifiable later completions.
- `SALTS_OK` or `SALTS_EALREADY` from `native_io_backend_cancel()` never releases a CNet request; only the matching observed terminal completion ends the NativeIO borrow.
- A logical send owns one CNet request, one command ownership interval, one deadline, and one final send event across all partial NativeIO submissions.
- Do not change public CNet API/ABI, callback ordering, payload lifetime, session-state semantics, or #286 retained-buffer ownership rules.
- Do not remove or alter NativeIO coroutine APIs or `native_io_backend_get_coroutine_stats()`.
- `cnet_owner_get_coroutine_stats()` is removed because CNet no longer owns per-I/O coroutines.
- Exact-head correctness must pass Windows IOCP, Linux epoll, Linux io_uring, and macOS kqueue before performance evidence is considered.
- Primary performance surface is Windows IOCP / TCP / 1 KiB: 5 matched alternating baseline/candidate repeats, 32 warmups, 512 measured persistent RTTs, uninstrumented comparison rows, paired p50/p95/rate median/MAD, and same-run NativeIO direct A/A control.
- The production comparison baseline is the PR base SHA's canonical coroutine owner, built in the same workflow job; do not compare against a historical hosted run.
- Do not add install/export verification code as part of this migration.

---

## File Map

**Production owner**
- Modify `cnet/src/cnet_owner.c` — direct request ownership, submit/cancel/observe routing, partial continuation, test-only seams.
- Modify `cnet/src/cnet_owner.h` — profiling counters, removal of CNet coroutine stats surface, `CNET_INTERNAL_TESTING` diagnostics only.

**Correctness tests**
- Modify `cnet/tests/cnet_owner_test.c` — direct ownership proof, stale generation, whole-batch error handling, cancellation races, deterministic partial send.
- Modify `cnet/tests/cnet_api_test.c` — profile shape assertions for the new resubmit counters.
- Modify `cnet/tests/CMakeLists.txt` only if an added test-only source is required; prefer keeping the current `cnet_owner_test` / `cnet_owner_profile_test` dual build.

**Diagnostic accounting**
- Modify `cnet/benchmarks/cnet_benchmark_stats.h` — direct-owner fixed-control sample/attribution shape.
- Modify `cnet/benchmarks/cnet_benchmark_stats.c` — direct-owner nesting and closure identity.
- Modify `cnet/tests/cnet_benchmark_stats_test.c` — exact arithmetic contract for the new nesting.
- Modify `cnet/benchmarks/cnet_io_benchmark.c` — direct submit/resubmit terminology and profile aggregation.

**Performance evidence**
- Create `cnet/benchmarks/cnet_owner_lifecycle_compare.c` — CI-only dynamic-loader benchmark comparing base-SHA and candidate CNet DSOs without a second production owner implementation.
- Modify `cnet/benchmarks/CMakeLists.txt` — build the comparison runner without linking either CNet DSO.
- Modify `.github/workflows/native-io-release-benchmarks.yml` — Windows-only same-job base-SHA build and paired comparison, then existing full benchmark/A-A evidence.

**Documentation**
- Modify `cnet/README.md` — canonical direct-owner semantics and unchanged NativeIO coroutine availability.

---

### Task 1: Prove and implement the canonical direct request lifecycle

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`

**Interfaces:**
- Consumes: `native_io_backend_submit()`, `native_io_backend_observe()`, `native_io_backend_cancel()`, `native_io_backend_get_stats()`, `native_io_backend_get_coroutine_stats()`.
- Produces internally:
  ```c
  typedef struct cnet_owner_request {
    cnet_owner_impl *owner;
    native_io_request native_request;
    native_io_operation operation;
    size_t requested_size;
    size_t submitted_size;
    size_t completed_size;
    /* existing session/command/role/stage/active/deadline fields */
  } cnet_owner_request;
  ```
- Produces test-only diagnostic:
  ```c
  #if defined(CNET_INTERNAL_TESTING)
  bool cnet_owner_test_backend_stats(const cnet_owner *owner,
                                     native_io_backend_stats *out_native,
                                     native_io_coroutine_stats *out_coroutine);
  #endif
  ```

- [ ] **Step 1: Replace the current coroutine-ownership assertions with a direct-owner RED assertion**

In the existing TCP owner fixture, after one RECEIVE command has been accepted and `cnet_owner_drive(owner, 0)` has started the pending I/O, add this profile-build-only check:

```c
#if defined(CNET_INTERNAL_TESTING)
  {
    native_io_backend_stats native_stats = {0};
    native_io_coroutine_stats coroutine_stats =
        NATIVE_IO_COROUTINE_STATS_V1_INITIALIZER;
    check_true(cnet_owner_test_backend_stats(&owner, &native_stats, &coroutine_stats));
    check_equal(native_stats.active_requests, 1u);
    check_equal(coroutine_stats.active, 0u);
    check_equal(coroutine_stats.retained_frames, 0u);
  }
#endif
```

Delete the existing test assertions that require a retained coroutine frame or an active owner coroutine; those assertions describe the architecture being removed.

- [ ] **Step 2: Run the owner profile target and capture the expected RED**

Run:

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected first RED: compile failure because `cnet_owner_test_backend_stats()` does not exist.

- [ ] **Step 3: Add the diagnostic seam only and demonstrate the semantic RED**

Under `CNET_INTERNAL_TESTING`, implement the diagnostic by querying the already-owned NativeIO backend:

```c
bool cnet_owner_test_backend_stats(const cnet_owner *owner,
                                   native_io_backend_stats *out_native,
                                   native_io_coroutine_stats *out_coroutine) {
  const cnet_owner_impl *impl =
      owner != NULL ? (const cnet_owner_impl *)owner->impl : NULL;
  if (impl == NULL || out_native == NULL || out_coroutine == NULL) return false;
  return native_io_backend_get_stats(&impl->backend, out_native) &&
         native_io_backend_get_coroutine_stats(&impl->backend, out_coroutine);
}
```

Re-run `cnet_owner_profile_test`.

Expected second RED on the current architecture: the pending request is owned by a coroutine (`coroutine_stats.active == 1`) and/or a retained coroutine frame exists, so the new direct-owner assertion fails.

Commit the test-only seam and RED test together only after preserving this behavioral RED evidence:

```bash
git add cnet/src/cnet_owner.h cnet/src/cnet_owner.c cnet/tests/cnet_owner_test.c
git commit -m "test(cnet): specify direct owner request lifecycle"
```

- [ ] **Step 4: Replace per-request coroutine ownership with direct NativeIO request ownership**

In `cnet_owner_request`, replace the coroutine task with `native_io_request native_request` and retain the copied operation. Add `submitted_size` so completion validation is against the exact most-recent submission rather than the logical remaining length.

Add a single direct submit helper:

```c
static int cnet_owner_submit_request(cnet_owner_impl *impl,
                                     cnet_owner_request *request,
                                     bool first_submit) {
  native_io_operation submitted = request->operation;
  native_io_request native_request = {0};
  const size_t index = (size_t)(request - impl->request_records);
  int status;

  if (index >= impl->request_capacity) return SALTS_EPROTO;
  submitted.user_data = (uintptr_t)(index + 1u);
  request->submitted_size = submitted.length;

  status = native_io_backend_submit(&impl->backend, &submitted, &native_request);
  if (status == SALTS_OK) request->native_request = native_request;
  (void)first_submit;
  return status;
}
```

At this task, `first_submit` exists so Task 4 can attach separate start/resubmit profiling without duplicating submit mechanics.

Rewrite `cnet_owner_start_request()` so all existing ownership/accounting setup happens before `cnet_owner_submit_request(..., true)`. Preserve the existing failure path and logical deadline scheduling. If scheduling the deadline fails after submit, record the failure and call direct cancellation; do not release the request until a terminal is observed.

Switch the two owner cancellation loops to:

```c
status = native_io_backend_cancel(&impl->backend, request->native_request);
if (status != SALTS_OK && status != SALTS_EALREADY && first_error == SALTS_OK)
  first_error = status;
```

- [ ] **Step 5: Route direct completions in `cnet_owner_drive()`**

Introduce the internal router:

```c
static int cnet_owner_route_completion(cnet_owner_impl *impl,
                                       const native_io_completion *completion) {
  size_t index;
  cnet_owner_request *request;

  if (completion == NULL || completion->user_data == 0u) return SALTS_EPROTO;
  index = (size_t)completion->user_data - 1u;
  if (index >= impl->request_capacity) return SALTS_EPROTO;
  request = &impl->request_records[index];
  if (!request->active || request->owner != impl) return SALTS_EPROTO;
  if (completion->request.slot != request->native_request.slot ||
      completion->request.generation != request->native_request.generation)
    return SALTS_EPROTO;
  if (completion->endpoint.slot != request->operation.endpoint.slot ||
      completion->endpoint.generation != request->operation.endpoint.generation)
    return SALTS_EPROTO;

  /* Task 4 expands SEND/TLS_WRITE partial handling. */
  return cnet_owner_complete(impl, request, completion);
}
```

Replace the current `completion_count != 0u -> SALTS_EPROTO` branch in `cnet_owner_drive()` with iteration over the returned completions. Remove `cnet_owner_take_coroutine_status()` from the drive path.

Do not rewrite `cnet_owner_complete()`.

- [ ] **Step 6: Remove the coroutine entry from the active lifecycle and run the basic owner suite**

Delete calls to `native_io_backend_spawn_coroutine()` and `native_io_coroutine_await()` from CNet owner request execution. The legacy `cnet_owner_coroutine_entry()` and public-internal `cnet_owner_get_coroutine_stats()` may remain temporarily dead until Task 5 so this task stays focused on execution correctness.

Run:

```bash
cmake --build --preset linux-release-user --target cnet_owner_test cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner(_profile)?_test$' --output-on-failure
```

Expected: both owner executables pass on Linux; the new pending-request diagnostic reports NativeIO active request(s) with zero active/retained CNet-created coroutine frames.

- [ ] **Step 7: Commit the canonical direct lifecycle**

```bash
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c
git commit -m "refactor(cnet): own stream requests through direct NativeIO"
```

---

### Task 2: Make generation-safe routing and whole-batch failure behavior deterministic

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`

**Interfaces:**
- Produces test-only request snapshot:
  ```c
  typedef struct cnet_owner_test_request_snapshot {
    uintptr_t token;
    native_io_request native_request;
    native_io_endpoint endpoint;
    bool active;
  } cnet_owner_test_request_snapshot;

  bool cnet_owner_test_request_snapshot(const cnet_owner *owner,
                                        size_t request_index,
                                        cnet_owner_test_request_snapshot *out_snapshot);
  int cnet_owner_test_observe_raw(cnet_owner *owner,
                                  native_io_completion *events,
                                  size_t event_capacity,
                                  uint32_t timeout_ms,
                                  size_t *out_count);
  int cnet_owner_test_process_completion_batch(cnet_owner *owner,
                                               const native_io_completion *events,
                                               size_t count);
  ```
- Produces production helper:
  ```c
  static int cnet_owner_process_completion_batch(cnet_owner_impl *impl,
                                                 const native_io_completion *events,
                                                 size_t count);
  ```

- [ ] **Step 1: Add a stale-generation RED test**

Use a real pending receive to obtain one request snapshot. After that logical receive completes, start another receive that reuses the same bounded CNet request record. Confirm the token is the same and the NativeIO generation is different, then inject the first request identity against the second record:

```c
native_io_completion stale = {
    .request = first.native_request,
    .endpoint = second.endpoint,
    .kind = NATIVE_IO_COMPLETION_CANCELLED,
    .status = SALTS_OK,
    .user_data = second.token};

check_equal(cnet_owner_test_process_completion_batch(&owner, &stale, 1u), SALTS_EPROTO);
check_true(cnet_owner_test_request_snapshot(&owner, request_index, &after));
check_true(after.active);
check_equal(after.native_request.generation, second.native_request.generation);
```

The addressed live request must remain untouched.

- [ ] **Step 2: Add a whole-batch first-error RED test without inventing a valid terminal**

Start one pending receive and make the peer produce its real completion. Use `cnet_owner_test_observe_raw()` to dequeue that real completion from NativeIO, which legally ends the borrow. Construct a two-entry test batch with an invalid synthetic entry first and the real completion second:

```c
native_io_completion batch[2] = {0};
batch[0].kind = NATIVE_IO_COMPLETION_FAILED;
batch[0].status = SALTS_EPROTO;
batch[0].user_data = 0u; /* guaranteed invalid token */
batch[1] = observed_real_completion;

check_equal(cnet_owner_test_process_completion_batch(&owner, batch, 2u), SALTS_EPROTO);
check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
check_equal(event.kind, CNET_EVENT_RECEIVE);
```

This proves the first protocol error is returned only after the later safely identifiable real completion is settled.

- [ ] **Step 3: Run the profile owner test and verify RED**

Run:

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected: compile failure for the missing test-only snapshot/raw/batch APIs.

- [ ] **Step 4: Factor one production batch processor and thin test wrappers**

Implement:

```c
static int cnet_owner_process_completion_batch(cnet_owner_impl *impl,
                                                const native_io_completion *events,
                                                size_t count) {
  int first_error = SALTS_OK;
  for (size_t index = 0u; index < count; ++index) {
    const int status = cnet_owner_route_completion(impl, &events[index]);
    if (status != SALTS_OK && first_error == SALTS_OK) first_error = status;
  }
  return first_error;
}
```

`cnet_owner_drive()` calls this helper after every successful observe. Under `CNET_INTERNAL_TESTING`, expose only thin wrappers around request snapshots, raw observe, and this batch processor. Do not add a runtime test branch to installed CNet.

- [ ] **Step 5: Run owner tests and commit**

```bash
cmake --build --preset linux-release-user --target cnet_owner_test cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner(_profile)?_test$' --output-on-failure
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c
git commit -m "test(cnet): harden direct completion identity"
```

---

### Task 3: Preserve cancellation, timeout, close, and drain ownership through terminal observation

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Test existing: `cnet/tests/cnet_stop_contract_test.c`

**Interfaces:**
- Produces one test-only control:
  ```c
  #if defined(CNET_INTERNAL_TESTING)
  int cnet_owner_test_force_cancel_ealready_once(cnet_owner *owner);
  #endif
  ```
- Production cancellation remains through one helper:
  ```c
  static int cnet_owner_cancel_native_request(cnet_owner_impl *impl,
                                              cnet_owner_request *request);
  ```

- [ ] **Step 1: Add the deterministic `SALTS_EALREADY` RED**

Create a pending RECEIVE. Before closing the session, arm the one-shot test control. After the close command is processed, query backend stats and prove the request is still owned until a later drive observes its terminal:

```c
check_equal(cnet_owner_test_force_cancel_ealready_once(&owner), SALTS_OK);
check_equal(cnet_command_queue_publish(&commands, &close_command), SALTS_OK);
check_equal(cnet_owner_drive(&owner, 0u), SALTS_OK);
check_true(cnet_owner_test_backend_stats(&owner, &native_stats, &coroutine_stats));
check_equal(native_stats.active_requests, 1u);

check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session,
                                            CNET_SESSION_TERMINAL), SALTS_OK);
check_true(cnet_owner_test_backend_stats(&owner, &native_stats, &coroutine_stats));
check_equal(native_stats.active_requests, 0u);
```

The test hook must still call the real `native_io_backend_cancel()`; when it receives `SALTS_OK` once, it reports `SALTS_EALREADY` to CNet so the owner follows the racing-terminal branch while NativeIO still produces a real terminal completion.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected: missing `cnet_owner_test_force_cancel_ealready_once()`.

- [ ] **Step 3: Centralize direct cancellation**

Implement:

```c
static int cnet_owner_cancel_native_request(cnet_owner_impl *impl,
                                            cnet_owner_request *request) {
  int status = native_io_backend_cancel(&impl->backend, request->native_request);
#if defined(CNET_INTERNAL_TESTING)
  if (impl->test_force_cancel_ealready_once && status == SALTS_OK) {
    impl->test_force_cancel_ealready_once = false;
    return SALTS_EALREADY;
  }
#endif
  return status;
}
```

All session/request cancellation loops call this helper. `SALTS_OK` and `SALTS_EALREADY` are accepted as "terminal still pending". Unexpected cancel errors are recorded in the session, but the request record remains active because NativeIO still owns the borrow until a terminal is observed.

- [ ] **Step 4: Re-run the existing timeout matrix without weakening it**

The current owner tests already cover connect/read/write timeout paths with the deterministic owner clock. Run:

```bash
cmake --build --preset linux-release-user --target cnet_owner_test cnet_owner_profile_test cnet_stop_contract_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_owner(_profile)?_test|cnet_stop_contract_test)$' \
  --timeout 60 --output-on-failure
```

Expected: connect timeout, read timeout, write timeout, close, and stop/drain semantics all remain green with direct cancellation.

- [ ] **Step 5: Commit**

```bash
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c
git commit -m "refactor(cnet): drain direct cancellations to terminal"
```

---

### Task 4: Make partial stream sends deterministic and preserve one logical send/profile lifecycle

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/tests/cnet_api_test.c`

**Interfaces:**
- Adds profile fields:
  ```c
  uint64_t request_resubmit_ns;
  uint64_t request_resubmit_calls;
  ```
- Adds test-only cap:
  ```c
  #if defined(CNET_INTERNAL_TESTING)
  int cnet_owner_test_set_send_chunk_bytes(cnet_owner *owner, size_t bytes);
  #endif
  ```
- `cnet_owner_submit_request(impl, request, first_submit)` remains the only submit helper.

- [ ] **Step 1: Add a deterministic one-byte chunk RED**

Use plain TCP and a 4-byte logical send. Set the test chunk limit to exactly one byte. Because every successful NativeIO send was asked to send only one byte, every OK completion must report exactly one byte, producing one initial submit plus three resubmits.

```c
#if defined(CNET_INTERNAL_TESTING)
check_equal(cnet_owner_test_set_send_chunk_bytes(&owner, 1u), SALTS_OK);
check_equal(cnet_owner_profile_begin(&owner), SALTS_OK);
#endif

/* Publish one 4-byte CNET_COMMAND_SEND and drive until CNET_EVENT_SEND. */
check_equal(event.kind, CNET_EVENT_SEND);
check_equal(event.size, sizeof(payload));

#if defined(CNET_INTERNAL_PROFILING)
check_equal(cnet_owner_profile_take(&owner, &profile), SALTS_OK);
check_equal(profile.request_start_calls, (uint64_t)1u);
check_equal(profile.request_resubmit_calls, (uint64_t)3u);
check_equal(profile.request_completion_calls, (uint64_t)1u);
#endif
```

Also verify the peer receives the four bytes in order and only one logical send event is published.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected: compile failure because the resubmit profile fields and chunk seam do not exist.

- [ ] **Step 3: Implement partial continuation in the direct completion router**

For SEND/TLS_WRITE OK completions, before calling `cnet_owner_complete()`:

```c
if (completion->bytes == 0u || completion->bytes > request->submitted_size)
  return cnet_owner_fail_started_request(request, SALTS_EIO);

request->completed_size += completion->bytes;
if (request->completed_size != request->requested_size) {
  if (request->operation.kind == NATIVE_IO_OPERATION_UDP_SEND_TO)
    return cnet_owner_fail_started_request(request, SALTS_EIO);
  request->operation.buffer =
      (unsigned char *)request->operation.buffer + completion->bytes;
  request->operation.length -= completion->bytes;
  return cnet_owner_submit_request(impl, request, false);
}
```

Before final `cnet_owner_complete()`, copy the completion and normalize `bytes` to `request->completed_size` so the existing logical send state machine continues to see the whole send.

For the test-only chunk control, cap only the local copied operation passed to NativeIO:

```c
#if defined(CNET_INTERNAL_TESTING)
if (impl->test_send_chunk_bytes != 0u &&
    (request->role == CNET_OWNER_REQUEST_SEND ||
     request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
    submitted.length > impl->test_send_chunk_bytes)
  submitted.length = impl->test_send_chunk_bytes;
#endif
request->submitted_size = submitted.length;
```

Do not change `request->requested_size`, `request->operation.length`, command ownership, or deadline when applying this cap.

- [ ] **Step 4: Instrument first submit versus resubmit exactly once**

In `cnet_owner_submit_request()`:

```c
#if defined(CNET_INTERNAL_PROFILING)
const uint64_t started = cnet_owner_profile_start(impl);
#endif
status = native_io_backend_submit(&impl->backend, &submitted, &native_request);
#if defined(CNET_INTERNAL_PROFILING)
if (first_submit)
  cnet_owner_profile_finish(impl, started,
                            &impl->profile.request_start_ns,
                            &impl->profile.request_start_calls);
else
  cnet_owner_profile_finish(impl, started,
                            &impl->profile.request_resubmit_ns,
                            &impl->profile.request_resubmit_calls);
#endif
```

`request_completion_calls` increments only when the logical request actually enters `cnet_owner_complete()`, never on intermediate partial completions.

- [ ] **Step 5: Extend API profile zero assertions**

Where `cnet_api_test.c` checks an idle/profile sample with zero request lifecycle counts, add:

```c
check_equal(profile.owner.request_resubmit_ns, (uint64_t)0u);
check_equal(profile.owner.request_resubmit_calls, (uint64_t)0u);
```

- [ ] **Step 6: Run and commit**

```bash
cmake --build --preset linux-release-user --target cnet_owner_test cnet_owner_profile_test cnet_api_profile_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_owner(_profile)?_test|cnet_api_profile_test)$' \
  --output-on-failure
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c cnet/tests/cnet_api_test.c
git commit -m "feat(cnet): continue partial sends in direct owner"
```

---

### Task 5: Re-close profiling/accounting for the direct completion model and remove CNet coroutine ownership surface

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/benchmarks/cnet_benchmark_stats.h`
- Modify: `cnet/benchmarks/cnet_benchmark_stats.c`
- Modify: `cnet/tests/cnet_benchmark_stats_test.c`
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`

**Interfaces:**
- Removes:
  ```c
  bool cnet_owner_get_coroutine_stats(const cnet_owner *owner,
                                      native_io_coroutine_stats *out_stats);
  ```
- Adds fixed-control sample field:
  ```c
  uint64_t request_resubmit_ns;
  ```
- Adds attribution field:
  ```c
  double native_request_resubmit_ns;
  ```

- [ ] **Step 1: Write the new direct-owner accounting RED first**

Update the fixed-control test sample so direct request completion is no longer assumed to be nested inside `observe_ns`. Use explicit values where:

```c
const cnet_benchmark_fixed_control_sample sample = {
    .round_trips = 1u,
    .send_admit_ns = 1000u,
    .queue_publish_ns = 600u,
    .payload_copy_ns = 300u,
    .client_poll_ns = 10000u,
    .owner_drive_ns = 9000u,
    .request_lifecycle_ns = 2000u,
    .request_start_ns = 1500u,
    .request_resubmit_ns = 200u,
    .observe_ns = 3000u,
    .request_completion_ns = 2500u,
    .event_publish_ns = 1900u,
    .dispatcher_prepare_ns = 200u,
    .dispatcher_invoke_ns = 1500u,
    .dispatcher_observer_ns = 1100u,
    .dispatcher_release_ns = 100u,
    .benchmark_callback_ns = 800u,
    .benchmark_payload_check_ns = 500u};
```

The new owner nesting is:

```text
owner_drive
  contains request_lifecycle
  contains request_resubmit
  contains observe
  contains request_completion

request_completion contains event_publish
event_publish contains dispatcher stages
```

Add assertions that `native_request_resubmit_ns == 200.0` and `closure_residual_ns == 0.0`.

- [ ] **Step 2: Run the benchmark stats RED**

```bash
cmake --build --preset linux-release-user --target cnet_benchmark_stats_test
ctest --test-dir build/linux-gcc-release -R '^cnet_benchmark_stats_test$' --output-on-failure
```

Expected: compile failure because `request_resubmit_ns` and `native_request_resubmit_ns` do not exist.

- [ ] **Step 3: Update the fixed-control identity for direct routing**

Change the owner nested sum in `cnet_benchmark_attribute_fixed_control()` from the old coroutine-era relationship to:

```c
owner_nested_ns = request_lifecycle_ns + request_resubmit_ns +
                  observe_ns + request_completion_ns;
```

Use overflow-checked additions exactly like the existing code. Because `request_completion_ns` is now outside `native_io_backend_observe()`, it must be removed explicitly from `owner_control_ns`; otherwise completion control would be double-counted when it is also decomposed below.

Keep:

```c
request_control_ns = request_lifecycle_ns - request_start_ns;
completion_control_ns = request_completion_ns - event_publish_ns;
```

Set `native_request_resubmit_ns` directly from the new sample field and include it in `shared_native_total_ns` together with first submit and NativeIO observe residual. Update closure arithmetic so a valid direct sample closes to zero and inconsistent nesting returns `SALTS_ERANGE`.

- [ ] **Step 4: Update benchmark aggregation and labels**

In `cnet_io_benchmark.c` add `cnet_request_resubmit_ns` to `io_bench_series`, summarize the new profile field per RTT, pass it into `cnet_benchmark_fixed_control_sample`, and change human-readable wording:

```text
request start -> first NativeIO submit
request resubmit -> partial-send continuation submit
observe -> backend wait/dequeue only
completion control -> CNet routing/terminal control after observe
```

Do not call `request_start_ns` "coroutine spawn" anywhere after this task.

- [ ] **Step 5: Remove the obsolete CNet coroutine surface**

Delete from `cnet_owner.h/.c`:

```c
cnet_owner_get_coroutine_stats(...)
cnet_owner_coroutine_entry(...)
coroutine_status
native_io_coroutine_task coroutine
```

The Task 1 test-only `cnet_owner_test_backend_stats()` remains under `CNET_INTERNAL_TESTING` and may still query NativeIO coroutine stats solely to prove CNet created none. That does not reintroduce a CNet coroutine ownership API.

Search to prove no CNet owner coroutine calls remain:

```bash
git grep -n -E 'native_io_backend_spawn_coroutine|native_io_coroutine_await|native_io_backend_cancel_coroutine|cnet_owner_get_coroutine_stats' -- cnet
```

Expected: no matches in CNet production or tests.

- [ ] **Step 6: Run diagnostic closure and owner/API tests**

```bash
cmake --build --preset linux-release-user --target \
  cnet_benchmark_stats_test cnet_owner_test cnet_owner_profile_test \
  cnet_api_test cnet_api_profile_test cnet_io_benchmark
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_benchmark_stats_test|cnet_owner(_profile)?_test|cnet_api(_profile)?_test)$' \
  --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h \
  cnet/tests/cnet_owner_test.c cnet/tests/cnet_benchmark_stats_test.c \
  cnet/benchmarks/cnet_benchmark_stats.h cnet/benchmarks/cnet_benchmark_stats.c \
  cnet/benchmarks/cnet_io_benchmark.c
git commit -m "perf(cnet): reclose direct owner attribution"
```

---

### Task 6: Run the canonical path through TLS, pipe, VSOCK, UDP, adopted sockets, and public API semantics

**Files:**
- Modify: `cnet/tests/cnet_owner_test.c` only if a missing direct-owner regression is discovered before this task; all required new deterministic owner mechanics should already be covered by Tasks 1-4.
- Modify: `cnet/README.md`
- Test existing:
  - `cnet/tests/cnet_tls_test.c`
  - `cnet/tests/cnet_api_test.c`
  - `cnet/tests/cnet_transport_test.c`
  - `cnet/tests/cnet_datagram_test.c`
  - `cnet/tests/cnet_stop_contract_test.c`
  - `cnet/tests/cnet_vsock_integration_test.c`

**Interfaces:** No new runtime interface. This task proves the one canonical owner composes with existing protocol/state layers.

- [ ] **Step 1: Run the complete local CNet contract group before documentation changes**

```bash
cmake --build --preset linux-release-user --target \
  cnet_websocket_parser_test cnet_websocket_test cnet_session_test \
  cnet_command_test cnet_uri_test cnet_transport_test cnet_owner_test \
  cnet_owner_profile_test cnet_event_test cnet_resolver_test cnet_shards_test \
  cnet_dispatcher_test cnet_api_test cnet_api_profile_test cnet_header_cpp_test \
  cnet_tls_test cnet_datagram_test cnet_kcp_test cnet_secure_kcp_test \
  cnet_packet_endpoint_test cnet_stop_contract_test cnet_vsock_integration_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_.*_test)$' --timeout 60 --output-on-failure
```

Expected: all supported tests pass; VSOCK may use its existing skip return code when unsupported by the host.

- [ ] **Step 2: Verify that the implementation boundary did not expand**

Run:

```bash
git diff --name-only $(git merge-base HEAD master)..HEAD
```

The production migration must not contain any of these #289 experimental files or equivalents:

```text
cnet/cmake/generate_direct_owner.cmake
cnet/src/cnet_direct_profile_control.c
second direct-owner CNet library target
runtime owner-mode configuration
```

Also run:

```bash
git grep -n -E 'DIRECT_OWNER_EXPERIMENT|owner_io_mode|generate_direct_owner' -- cnet .github
```

Expected: no matches.

- [ ] **Step 3: Update `cnet/README.md`**

Document these exact facts:

```text
CNet's stream owner uses NativeIO direct completion ownership internally.
Cancellation is a request; CNet retains request/payload ownership until the matching terminal completion is observed.
Partial stream writes remain one logical CNet send and may require multiple NativeIO submissions.
NativeIO coroutine APIs remain available to other consumers; CNet simply no longer creates one coroutine per stream I/O.
```

Do not claim kernel zero-copy or that coroutines are globally slower/bad.

- [ ] **Step 4: Commit the semantic/documentation closure**

```bash
git add cnet/README.md
git commit -m "docs(cnet): describe direct owner execution"
```

---

### Task 7: Add a same-job base-SHA versus candidate performance runner without a second production owner

**Files:**
- Create: `cnet/benchmarks/cnet_owner_lifecycle_compare.c`
- Modify: `cnet/benchmarks/CMakeLists.txt`
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Benchmark executable CLI:
  ```text
  cnet_owner_lifecycle_compare <baseline-cnet-dso> <candidate-cnet-dso> <report-path>
  ```
- Workload is frozen:
  ```c
  enum {
    CNET_OWNER_COMPARE_REPEATS = 5,
    CNET_OWNER_COMPARE_WARMUPS = 32,
    CNET_OWNER_COMPARE_EXCHANGES = 512,
    CNET_OWNER_COMPARE_PAYLOAD_BYTES = 1024
  };
  ```

- [ ] **Step 1: Add a runner argument/paired-statistics RED test through executable self-check**

The runner must reject missing paths and must calculate paired deltas with the existing `cnet_benchmark_summarize_paired_delta()` helper. Put a self-check mode behind exactly `--self-test`:

```c
if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
  const double baseline[] = {100, 100, 100, 100, 100};
  const double candidate[] = {95, 96, 97, 98, 99};
  cnet_benchmark_summary summary = {0};
  if (cnet_benchmark_summarize_paired_delta(baseline, candidate, 5u, &summary) != SALTS_OK)
    return 1;
  return summary.median == -3.0 ? 0 : 1;
}
```

Add a CTest entry for only this self-check, not for hosted performance.

- [ ] **Step 2: Implement the uninstrumented dynamic-loader comparison**

Use public CNet headers only for type definitions. Do not link `salts_cnet` into the executable. Resolve these symbols from one DSO at a time:

```text
cnet_client_init
cnet_connect
cnet_send
cnet_receive
cnet_client_poll
cnet_close
cnet_client_stop
cnet_client_destroy
```

For each of five repeat pairs:

```c
const bool candidate_first = (repeat & 1u) != 0u;
```

Run one fresh persistent TCP fixture for baseline and one for candidate, alternating order per repeat. Each sample executes 32 warmups followed by 512 measured 1 KiB round trips. DSO load/unload, client construction, connect, receive-demand setup, and teardown stay outside the measured interval.

Write Markdown containing:

```text
head/base SHA metadata
baseline and candidate DSO paths
paired p50 delta median/MAD
paired p95 delta median/MAD
paired rate delta median/MAD
five raw paired repeat rows
4/1/0-style direction counts for p50
```

Do not embed the #288 3.11% value as an assertion.

- [ ] **Step 3: Wire the benchmark target without linking CNet**

In `cnet/benchmarks/CMakeLists.txt`:

```cmake
cmake_add_benchmark(cnet_owner_lifecycle_compare
  SOURCES cnet_owner_lifecycle_compare.c cnet_benchmark_stats.c
  LIBS Salts::Platform Salts::Core Salts::NativeIO
  FOLDER "cnet/benchmarks")
target_include_directories(cnet_owner_lifecycle_compare PRIVATE
  ../include ../src)
if(UNIX AND NOT APPLE)
  target_link_libraries(cnet_owner_lifecycle_compare PRIVATE dl)
endif()
add_test(NAME cnet_owner_lifecycle_compare_self_test
  COMMAND cnet_owner_lifecycle_compare --self-test)
```

On Windows use `LoadLibraryA/GetProcAddress`; on POSIX use `dlopen(RTLD_NOW | RTLD_LOCAL)/dlsym`.

- [ ] **Step 4: Verify the runner itself locally**

```bash
cmake --build --preset linux-release-user --target cnet_owner_lifecycle_compare cnet_benchmark_stats_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_owner_lifecycle_compare_self_test|cnet_benchmark_stats_test)$' \
  --output-on-failure
```

Expected: both pass.

- [ ] **Step 5: Add Windows-only same-job base-SHA build orchestration**

In the IOCP matrix job, after the candidate build and correctness tests, create a detached baseline worktree from the PR base SHA:

```powershell
$baselineSource = Join-Path $env:RUNNER_TEMP "salts-cnet-baseline"
git worktree add --detach $baselineSource "${{ github.event.pull_request.base.sha }}"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Configure the baseline with the same Visual Studio environment, triplet, dependency cache, Release flags, and `BUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF`. Use an explicit baseline build directory under `$env:RUNNER_TEMP` rather than reusing candidate presets' output directory. Build only `salts_cnet`.

The candidate DSO is the normal `salts_cnet` from `${{ matrix.build_dir }}`. The baseline DSO comes from the detached base-SHA build. Do not generate a second owner source or compile a mode-switched candidate.

- [ ] **Step 6: Run the paired owner comparison before the existing full benchmark**

Set:

```text
CNET_IO_BENCHMARK_BACKEND=iocp
```

Run:

```powershell
& $compare $baselineDso $candidateDso `
  (Join-Path $resultDir "cnet-owner-base-vs-direct.md")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Then run the existing `cnet_io_benchmark` unchanged so `libuv-native-io-cnet-benchmark.md` in the same `io-benchmark-windows-iocp` artifact continues to contain the NativeIO direct A/A control and current CNet/NativeIO reference rows.

- [ ] **Step 7: Commit the performance evidence machinery**

```bash
git add cnet/benchmarks/cnet_owner_lifecycle_compare.c \
  cnet/benchmarks/CMakeLists.txt .github/workflows/native-io-release-benchmarks.yml
git commit -m "bench(cnet): compare base owner with direct production path"
```

---

### Task 8: Exact-head release matrix, performance decision, and final audit

**Files:**
- No production file should be added here unless a previous task's verified failure identifies a concrete defect.
- Update issue/PR evidence only after exact-head results exist.

**Interfaces:** Evidence gate for #290.

- [ ] **Step 1: Audit the implementation diff against the design boundary**

Run:

```bash
git diff --stat master...HEAD
git diff --name-only master...HEAD
git grep -n -E 'native_io_backend_spawn_coroutine|native_io_coroutine_await|native_io_backend_cancel_coroutine' -- cnet/src/cnet_owner.c cnet/src/cnet_owner.h
```

Expected:
- one canonical owner implementation;
- no CNet owner coroutine lifecycle calls;
- no source generator, duplicate owner library, runtime mode, or profile mode setter;
- no #286 retained-buffer implementation folded into this PR.

- [ ] **Step 2: Run the full local Linux verification set**

```bash
cmake --build --preset linux-release-user --target \
  native_io_test native_io_header_cpp_test native_ipc_test native_ipc_header_cpp_test \
  cnet_io_benchmark cnet_owner_lifecycle_compare \
  cnet_websocket_parser_test cnet_websocket_test cnet_session_test cnet_command_test \
  cnet_uri_test cnet_transport_test cnet_owner_test cnet_owner_profile_test cnet_event_test \
  cnet_resolver_test cnet_shards_test cnet_dispatcher_test cnet_api_test cnet_api_profile_test \
  cnet_header_cpp_test cnet_tls_test cnet_datagram_test cnet_kcp_test cnet_secure_kcp_test \
  cnet_packet_endpoint_test cnet_stop_contract_test cnet_vsock_integration_test \
  cnet_benchmark_stats_test cnet_io_benchmark_config_test concurrency_deadline_queue_test
ctest --test-dir build/linux-gcc-release \
  -R '^(native_(io|ipc)_(test|header_cpp_test)|cnet_.*_test|concurrency_deadline_queue_test)$' \
  --timeout 60 --output-on-failure
```

Expected: all applicable tests pass; supported VSOCK behavior follows its existing skip contract.

- [ ] **Step 3: Open/refresh the production PR only after local verification**

The PR body must link #290 and the committed spec/plan. Explicitly state that #289 remains evidence-only and is not being merged.

- [ ] **Step 4: Require one exact-head `NativeIO and CNet release benchmarks` run**

Record:

```text
head SHA
workflow run ID
IOCP job ID
Linux epoll job ID
Linux io_uring job ID
macOS kqueue job ID
artifact IDs and SHA-256 digests
```

All four correctness jobs must be green at the same head.

- [ ] **Step 5: Evaluate Windows IOCP performance against same-run noise**

From the IOCP artifact read both:

```text
cnet-owner-base-vs-direct.md
libuv-native-io-cnet-benchmark.md
```

Apply the spec gate:

1. correctness is green;
2. candidate p50 direction is beneficial relative to the base-SHA coroutine owner;
3. compare the candidate p50 paired improvement magnitude with the same-run NativeIO direct A/A noise envelope;
4. raw p50 repeats must not show unstable direction reversal;
5. p95 and rate must not show a material aggregate regression.

Do not require exact reproduction of #288's `-3.11%`.

If p50 resolves above noise, record that the production migration retained measurable signal. If p50 is below noise but p95/rate show no regression, record it as noise-limited production verification and explain the difference from #288 without changing the architecture claim. A material p95/rate regression or semantic divergence blocks merge.

- [ ] **Step 6: Record final evidence on #290 and stop before merge**

The final #290 comment must include the exact-head correctness matrix and the paired performance table. Do not merge automatically. Leave the PR ready for explicit review/merge only after that evidence is recorded.
