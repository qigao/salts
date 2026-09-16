# CNet Canonical Direct NativeIO Owner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace CNet stream-owner per-I/O coroutine execution with one canonical direct NativeIO submit/observe/cancel path while preserving current CNet semantics and the #288 performance benefit direction.

**Architecture:** `cnet_owner_request` owns one logical CNet request plus the currently active generation-checked `native_io_request`. `cnet_owner_drive()` directly consumes NativeIO completion batches, validates `request_index + 1` routing tokens plus request/endpoint generation identity, resubmits partial stream writes inside the same logical request, and delegates only logical terminals to the existing `cnet_owner_complete()` state machine. NativeIO coroutine APIs remain unchanged for other consumers; CNet has no runtime owner-mode switch.

**Tech Stack:** C11, CMake, TinyTest, Salts NativeIO, CNet owner/session/command/event/TLS layers, GitHub Actions release benchmark matrix.

**Spec:** `docs/superpowers/specs/2026-09-17-cnet-direct-nativeio-owner-design.md`

## Global Constraints

- Final production state has exactly one canonical `cnet/src/cnet_owner.c`; do not retain the #289 source generator, duplicate direct-owner libraries, profile-only mode setter, or runtime direct/coroutine switch.
- `native_io_operation.user_data` is exactly `request_index + 1`; zero is invalid.
- A completion may mutate a CNet request only after validating the active record, the exact stored `native_io_request {slot,generation}`, and the currently submitted endpoint.
- Consume the complete completion batch returned by one `native_io_backend_observe()` call before returning an error; retain the first error and continue later completions that can still be identified safely.
- `SALTS_OK` or `SALTS_EALREADY` from `native_io_backend_cancel()` never releases a CNet request; only the matching observed terminal completion ends the NativeIO borrow.
- A logical send owns one CNet request, one command ownership interval, one deadline, and one final send event across all partial NativeIO submissions.
- Do not change public CNet API/ABI, callback ordering, payload lifetime, session-state semantics, or #286 retained-buffer ownership rules.
- Do not remove or alter NativeIO coroutine APIs or `native_io_backend_get_coroutine_stats()`.
- Remove `cnet_owner_get_coroutine_stats()` because CNet no longer owns per-I/O coroutines.
- Exact-head correctness must pass Windows IOCP, Linux epoll, Linux io_uring, and macOS kqueue before performance evidence is considered.
- Primary performance surface is Windows IOCP / TCP / 1 KiB: 5 matched alternating baseline/candidate repeats, 32 warmups, 512 measured persistent RTTs, uninstrumented comparison rows, paired p50/p95/rate median/MAD, and same-run NativeIO direct A/A control.
- The production comparison baseline is the PR base SHA's canonical coroutine owner, built in the same workflow job; do not compare against a historical hosted run.
- Do not add install/export verification code as part of this migration.

---

## File Map

**Production owner**
- Modify `cnet/src/cnet_owner.c` — direct request ownership, submit/cancel/observe routing, partial continuation, test-only diagnostics.
- Modify `cnet/src/cnet_owner.h` — profiling counters, removal of CNet coroutine stats surface, `CNET_INTERNAL_TESTING` diagnostics.

**Correctness tests**
- Modify `cnet/tests/cnet_owner_test.c` — direct ownership proof, stale generation, whole-batch error handling, cancellation races, deterministic partial send.
- Modify `cnet/tests/cnet_api_test.c` — profile shape assertions for resubmit counters.

**Diagnostic accounting**
- Modify `cnet/benchmarks/cnet_benchmark_stats.h` — direct-owner fixed-control sample/attribution shape.
- Modify `cnet/benchmarks/cnet_benchmark_stats.c` — direct-owner nesting and closure identity.
- Modify `cnet/tests/cnet_benchmark_stats_test.c` — exact arithmetic contract for the new nesting.
- Modify `cnet/benchmarks/cnet_io_benchmark.c` — direct submit/resubmit terminology and aggregation.

**Performance evidence**
- Create `cnet/benchmarks/cnet_owner_lifecycle_compare.c` — CI-only dynamic-loader benchmark comparing base-SHA and candidate CNet DSOs without a second production owner implementation.
- Modify `cnet/benchmarks/CMakeLists.txt` — build/self-test the comparison runner without linking either CNet DSO.
- Modify `.github/workflows/native-io-release-benchmarks.yml` — Windows-only base-SHA build and paired comparison, then the existing full benchmark/A-A evidence.

**Documentation**
- Modify `cnet/README.md` — canonical direct-owner semantics and unchanged NativeIO coroutine availability.

---

### Task 1: Prove and implement the canonical direct request lifecycle

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`

**Interfaces:**

```c
typedef struct cnet_owner_request {
  cnet_owner_impl *owner;
  native_io_request native_request;
  native_io_operation operation;
  size_t requested_size;
  size_t submitted_size;
  size_t completed_size;
  cnet_session_handle session;
  cnet_command_view command;
  cnet_owner_request_role role;
  cnet_session_stage stage;
  bool active;
  bool close_after_send;
  salts_deadline_id deadline;
} cnet_owner_request;
```

Test-only diagnostic:

```c
#if defined(CNET_INTERNAL_TESTING)
bool cnet_owner_test_backend_stats(const cnet_owner *owner,
                                   native_io_backend_stats *out_native,
                                   native_io_coroutine_stats *out_coroutine);
#endif
```

- [ ] **Step 1: Replace coroutine-ownership assertions with the direct-owner RED**

In the existing TCP owner fixture, after a RECEIVE command is accepted and one drive starts the pending I/O, add:

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

Delete the existing assertions that require an active owner coroutine or a retained coroutine frame.

- [ ] **Step 2: Run and capture compile RED**

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected first RED: `cnet_owner_test_backend_stats` is missing.

- [ ] **Step 3: Add only the test diagnostic and capture behavioral RED**

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

Expected second RED on the current architecture: the pending request is owned by a coroutine (`active == 1`) and/or a retained coroutine frame exists.

Commit the RED contract and test-only seam:

```bash
git add cnet/src/cnet_owner.h cnet/src/cnet_owner.c cnet/tests/cnet_owner_test.c
git commit -m "test(cnet): specify direct owner request lifecycle"
```

- [ ] **Step 4: Add one direct-submit helper and move `cnet_owner_start_request()` to it**

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

#if defined(CNET_INTERNAL_PROFILING)
  {
    const uint64_t started = cnet_owner_profile_start(impl);
    status = native_io_backend_submit(&impl->backend, &submitted, &native_request);
    if (first_submit)
      cnet_owner_profile_finish(impl, started,
                                &impl->profile.request_start_ns,
                                &impl->profile.request_start_calls);
  }
#else
  status = native_io_backend_submit(&impl->backend, &submitted, &native_request);
  (void)first_submit;
#endif
  if (status == SALTS_OK) request->native_request = native_request;
  return status;
}
```

`cnet_owner_start_request()` keeps the existing ownership order: acquire record, transfer command view, increment session/owner active counts, set read/write-active state, then call `cnet_owner_submit_request(..., true)`, then schedule the logical deadline. If deadline scheduling fails after submit, record failure and request cancellation; do not release the submitted request before its terminal completion is observed.

Switch owner cancellation loops to `native_io_backend_cancel(&impl->backend, request->native_request)`, accepting both `SALTS_OK` and `SALTS_EALREADY` as nonterminal cancellation outcomes.

- [ ] **Step 5: Route direct completions and preserve partial-send semantics immediately**

Add identity validation first:

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
```

For SEND/TLS_WRITE OK completions, preserve the existing logical partial-send behavior in this task rather than temporarily regressing it:

```c
  if ((request->role == CNET_OWNER_REQUEST_SEND ||
       request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
      completion->kind == NATIVE_IO_COMPLETION_OK) {
    native_io_completion terminal = *completion;
    int status;

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
    terminal.bytes = request->completed_size;
#if defined(CNET_INTERNAL_PROFILING)
    {
      const uint64_t started = cnet_owner_profile_start(impl);
      status = cnet_owner_complete(impl, request, &terminal);
      cnet_owner_profile_finish(impl, started,
                                &impl->profile.request_completion_ns,
                                &impl->profile.request_completion_calls);
    }
    return status;
#else
    return cnet_owner_complete(impl, request, &terminal);
#endif
  }
```

For all other terminal roles/kinds, measure and call the existing `cnet_owner_complete()` once. Intermediate partial completions must not increment logical completion counters.

- [ ] **Step 6: Make `cnet_owner_drive()` consume direct completions**

Replace the current `completion_count != 0u -> SALTS_EPROTO` rule with iteration over the returned completions. Remove `cnet_owner_take_coroutine_status()` from the drive path. Do not rewrite `cnet_owner_complete()`.

Delete calls to `native_io_backend_spawn_coroutine()` and `native_io_coroutine_await()` from active CNet request execution. `cnet_owner_coroutine_entry()` and `cnet_owner_get_coroutine_stats()` may remain dead until Task 5, but no live request may use them after this task.

- [ ] **Step 7: Run and commit**

```bash
cmake --build --preset linux-release-user --target cnet_owner_test cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner(_profile)?_test$' --output-on-failure
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c
git commit -m "refactor(cnet): own stream requests through direct NativeIO"
```

Expected: both owner executables pass; a pending request appears in NativeIO stats with zero CNet-created coroutine activity/retained frames.

---

### Task 2: Make stale-generation rejection and whole-batch error handling deterministic

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`

**Interfaces:**

```c
#if defined(CNET_INTERNAL_TESTING)
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
#endif
```

Production helper:

```c
static int cnet_owner_process_completion_batch(cnet_owner_impl *impl,
                                                const native_io_completion *events,
                                                size_t count);
```

- [ ] **Step 1: Add a capacity-1 stale-generation RED test**

Use a dedicated fixture with `request_capacity = 1` and `completion_batch_capacity = 1`, guaranteeing reuse of CNet request index 0 and the sole NativeIO request slot. Capture request 1's snapshot, complete it normally, start request 2, then verify same token with a new NativeIO generation. Inject request 1's identity against request 2:

```c
native_io_completion stale = {
    .request = first.native_request,
    .endpoint = second.endpoint,
    .kind = NATIVE_IO_COMPLETION_CANCELLED,
    .status = SALTS_OK,
    .user_data = second.token};

check_equal(cnet_owner_test_process_completion_batch(&owner, &stale, 1u), SALTS_EPROTO);
check_true(cnet_owner_test_request_snapshot(&owner, 0u, &after));
check_true(after.active);
check_equal(after.native_request.generation, second.native_request.generation);
```

The live request must remain untouched.

- [ ] **Step 2: Add a whole-batch first-error RED test**

Use `completion_batch_capacity = 2`. Start one real pending receive and make the peer produce its terminal. Dequeue that real terminal with `cnet_owner_test_observe_raw()` so NativeIO has legally ended its borrow. Prepend an invalid synthetic entry and process both together:

```c
native_io_completion batch[2] = {0};
batch[0].kind = NATIVE_IO_COMPLETION_FAILED;
batch[0].status = SALTS_EPROTO;
batch[0].user_data = 0u;
batch[1] = observed_real_completion;

check_equal(cnet_owner_test_process_completion_batch(&owner, batch, 2u), SALTS_EPROTO);
check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
check_equal(event.kind, CNET_EVENT_RECEIVE);
```

This proves a first error cannot discard a later valid terminal from the already-dequeued batch.

- [ ] **Step 3: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected: missing test-only snapshot/raw/batch APIs.

- [ ] **Step 4: Implement one production batch processor and thin test wrappers**

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

`cnet_owner_drive()` calls this helper for every successful observe batch. Test wrappers expose request snapshots, raw observe, and this same batch processor only under `CNET_INTERNAL_TESTING`.

- [ ] **Step 5: Run and commit**

```bash
cmake --build --preset linux-release-user --target cnet_owner_test cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner(_profile)?_test$' --output-on-failure
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c
git commit -m "test(cnet): harden direct completion identity"
```

---

### Task 3: Preserve cancellation, timeout, close, and drain ownership until terminal observation

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Test: `cnet/tests/cnet_stop_contract_test.c`

**Interface:**

```c
#if defined(CNET_INTERNAL_TESTING)
int cnet_owner_test_force_cancel_ealready_once(cnet_owner *owner);
#endif
```

Production cancellation helper:

```c
static int cnet_owner_cancel_native_request(cnet_owner_impl *impl,
                                            cnet_owner_request *request);
```

- [ ] **Step 1: Add deterministic `SALTS_EALREADY` RED coverage**

Create one pending RECEIVE. Arm the one-shot control, publish CLOSE, drive once, and prove NativeIO still owns one active request. Then continue driving to terminal and prove active requests drop to zero:

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

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected: missing `cnet_owner_test_force_cancel_ealready_once()`.

- [ ] **Step 3: Centralize direct cancellation**

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

The test control still invokes real NativeIO cancellation; it only changes CNet's observed return code from one successful cancel to `SALTS_EALREADY`, so a real CANCELLED terminal still arrives later. Every owner cancellation loop uses this helper. Unexpected cancel errors are recorded, but an unobserved NativeIO request is never released.

- [ ] **Step 4: Re-run deterministic timeout/stop coverage**

```bash
cmake --build --preset linux-release-user --target \
  cnet_owner_test cnet_owner_profile_test cnet_stop_contract_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_owner(_profile)?_test|cnet_stop_contract_test)$' \
  --timeout 60 --output-on-failure
```

Expected: existing connect/read/write timeout, close, and stop/drain contracts remain green.

- [ ] **Step 5: Commit**

```bash
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h cnet/tests/cnet_owner_test.c
git commit -m "refactor(cnet): drain direct cancellations to terminal"
```

---

### Task 4: Make partial-send resubmission and profiling deterministic

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/tests/cnet_api_test.c`

**Interfaces:**

```c
uint64_t request_resubmit_ns;
uint64_t request_resubmit_calls;
```

Test-only cap:

```c
#if defined(CNET_INTERNAL_TESTING)
int cnet_owner_test_set_send_chunk_bytes(cnet_owner *owner, size_t bytes);
#endif
```

- [ ] **Step 1: Add a deterministic 1-byte chunk RED**

Use plain TCP and one 4-byte logical send. Set the test chunk limit to one byte. A successful NativeIO operation submitted with length 1 can only complete with one byte, so the logical send deterministically requires one initial submit plus three resubmits.

```c
check_equal(cnet_owner_test_set_send_chunk_bytes(&owner, 1u), SALTS_OK);
check_equal(cnet_owner_profile_begin(&owner), SALTS_OK);
/* publish one 4-byte CNET_COMMAND_SEND and drive to CNET_EVENT_SEND */
check_equal(event.kind, CNET_EVENT_SEND);
check_equal(event.size, sizeof(payload));
check_equal(cnet_owner_profile_take(&owner, &profile), SALTS_OK);
check_equal(profile.request_start_calls, (uint64_t)1u);
check_equal(profile.request_resubmit_calls, (uint64_t)3u);
check_equal(profile.request_completion_calls, (uint64_t)1u);
```

Also verify the peer receives exactly the four payload bytes and only one logical send event is published.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected: missing resubmit fields and send-chunk test control.

- [ ] **Step 3: Add the test-only cap without changing logical send state**

In `cnet_owner_submit_request()`, cap only the local submitted descriptor:

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

Do not change `requested_size`, logical remaining `request->operation.length`, command ownership, or deadline.

- [ ] **Step 4: Measure initial submit and resubmit separately**

Extend the Task 1 submit helper:

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

Logical `request_completion_calls` remains exactly one for the 4-byte send.

- [ ] **Step 5: Extend API profile zero assertions**

In `cnet_api_test.c` profile samples that currently assert zero request activity, add:

```c
check_equal(profile.owner.request_resubmit_ns, (uint64_t)0u);
check_equal(profile.owner.request_resubmit_calls, (uint64_t)0u);
```

- [ ] **Step 6: Run and commit**

```bash
cmake --build --preset linux-release-user --target \
  cnet_owner_test cnet_owner_profile_test cnet_api_profile_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_owner(_profile)?_test|cnet_api_profile_test)$' \
  --output-on-failure
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h \
  cnet/tests/cnet_owner_test.c cnet/tests/cnet_api_test.c
git commit -m "test(cnet): measure direct partial resubmission"
```

---

### Task 5: Re-close diagnostic accounting for direct completion routing and remove CNet coroutine ownership surface

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/benchmarks/cnet_benchmark_stats.h`
- Modify: `cnet/benchmarks/cnet_benchmark_stats.c`
- Modify: `cnet/tests/cnet_benchmark_stats_test.c`
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`

**Interfaces:**

Remove:

```c
bool cnet_owner_get_coroutine_stats(const cnet_owner *owner,
                                    native_io_coroutine_stats *out_stats);
```

Add to fixed-control sample:

```c
uint64_t request_resubmit_ns;
```

Replace the coroutine-era attribution name:

```c
double native_observe_ns;
double native_request_resubmit_ns;
```

`native_observe_residual_ns` is removed because direct completion control is no longer nested inside `native_io_backend_observe()`.

- [ ] **Step 1: Write the new arithmetic RED**

Use this exact sample:

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

Assert:

```c
check_equal(attribution.total_budget_ns, 11000.0);
check_equal(attribution.owner_control_ns, 1300.0);
check_equal(attribution.request_control_ns, 500.0);
check_equal(attribution.native_request_start_ns, 1500.0);
check_equal(attribution.native_request_resubmit_ns, 200.0);
check_equal(attribution.native_observe_ns, 3000.0);
check_equal(attribution.completion_control_ns, 600.0);
check_equal(attribution.fixed_control_total_ns, 5200.0);
check_equal(attribution.shared_native_total_ns, 4700.0);
check_equal(attribution.benchmark_work_total_ns, 800.0);
check_equal(attribution.closure_residual_ns, 0.0);
```

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_benchmark_stats_test
ctest --test-dir build/linux-gcc-release -R '^cnet_benchmark_stats_test$' --output-on-failure
```

Expected: missing resubmit/new observe attribution fields.

- [ ] **Step 3: Implement the direct-owner nesting identity**

Use overflow-checked addition for:

```text
owner_nested = request_lifecycle
             + request_resubmit
             + observe
             + request_completion
```

Then:

```c
owner_control_ns = owner_drive_ns - owner_nested_ns;
request_control_ns = request_lifecycle_ns - request_start_ns;
native_observe_ns = observe_ns;
completion_control_ns = request_completion_ns - event_publish_ns;
```

`shared_native_total_ns` is first submit + resubmit + full backend observe. Do not subtract `request_completion_ns` from observe: under direct routing, `native_io_backend_observe()` returns before CNet routes the completion.

- [ ] **Step 4: Update benchmark aggregation/labels**

In `cnet_io_benchmark.c` add a `cnet_request_resubmit_ns` summary, pass `request_resubmit_ns` into the fixed-control sample, and change report wording to:

```text
request start = first NativeIO submit
request resubmit = partial-send continuation submit
observe = backend wait/dequeue only
completion control = CNet routing/terminal control after observe
```

No CNet benchmark label may call `request_start_ns` a coroutine spawn after this task.

- [ ] **Step 5: Remove the obsolete CNet coroutine ownership surface**

Delete from `cnet_owner.h/.c`:

```text
native_io_coroutine_task coroutine
cnet_owner_coroutine_entry()
coroutine_status
cnet_owner_take_coroutine_status()
cnet_owner_get_coroutine_stats()
```

Keep only the `CNET_INTERNAL_TESTING` backend-stats diagnostic from Task 1; it may query NativeIO coroutine stats to prove CNet did not create any, but it is not a CNet runtime ownership API.

Run:

```bash
git grep -n -E 'native_io_backend_spawn_coroutine|native_io_coroutine_await|native_io_backend_cancel_coroutine|cnet_owner_get_coroutine_stats' -- cnet
```

Expected: no matches.

- [ ] **Step 6: Run and commit**

```bash
cmake --build --preset linux-release-user --target \
  cnet_benchmark_stats_test cnet_owner_test cnet_owner_profile_test \
  cnet_api_test cnet_api_profile_test cnet_io_benchmark
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_benchmark_stats_test|cnet_owner(_profile)?_test|cnet_api(_profile)?_test)$' \
  --output-on-failure
git add cnet/src/cnet_owner.c cnet/src/cnet_owner.h \
  cnet/tests/cnet_owner_test.c cnet/tests/cnet_benchmark_stats_test.c \
  cnet/benchmarks/cnet_benchmark_stats.h cnet/benchmarks/cnet_benchmark_stats.c \
  cnet/benchmarks/cnet_io_benchmark.c
git commit -m "perf(cnet): reclose direct owner attribution"
```

---

### Task 6: Close protocol/state regressions and document the canonical path

**Files:**
- Modify: `cnet/README.md`
- Test: `cnet/tests/cnet_tls_test.c`
- Test: `cnet/tests/cnet_api_test.c`
- Test: `cnet/tests/cnet_transport_test.c`
- Test: `cnet/tests/cnet_datagram_test.c`
- Test: `cnet/tests/cnet_stop_contract_test.c`
- Test: `cnet/tests/cnet_vsock_integration_test.c`

**Interfaces:** No new runtime interface.

- [ ] **Step 1: Run the full local CNet contract group**

```bash
cmake --build --preset linux-release-user --target \
  cnet_websocket_parser_test cnet_websocket_test cnet_session_test cnet_command_test \
  cnet_uri_test cnet_transport_test cnet_owner_test cnet_owner_profile_test \
  cnet_event_test cnet_resolver_test cnet_shards_test cnet_dispatcher_test \
  cnet_api_test cnet_api_profile_test cnet_header_cpp_test cnet_tls_test \
  cnet_datagram_test cnet_kcp_test cnet_secure_kcp_test cnet_packet_endpoint_test \
  cnet_stop_contract_test cnet_vsock_integration_test
ctest --test-dir build/linux-gcc-release \
  -R '^cnet_.*_test$' --timeout 60 --output-on-failure
```

Expected: all supported tests pass; VSOCK retains its existing skip contract when the host does not support it.

- [ ] **Step 2: Audit the source boundary**

```bash
git grep -n -E 'DIRECT_OWNER_EXPERIMENT|owner_io_mode|generate_direct_owner' -- cnet .github
```

Expected: no matches.

The implementation must not contain:

```text
cnet/cmake/generate_direct_owner.cmake
cnet/src/cnet_direct_profile_control.c
a second direct-owner CNet library target
a runtime owner-mode configuration
```

- [ ] **Step 3: Update `cnet/README.md` with exact semantics**

Document:

```text
CNet's stream owner uses NativeIO direct completion ownership internally.
Cancellation is a request; CNet retains request/payload ownership until the matching terminal completion is observed.
Partial stream writes remain one logical CNet send and may require multiple NativeIO submissions.
NativeIO coroutine APIs remain available to other consumers; CNet simply no longer creates one coroutine per stream I/O.
```

Do not claim kernel zero-copy or that coroutines are globally inferior.

- [ ] **Step 4: Commit**

```bash
git add cnet/README.md
git commit -m "docs(cnet): describe direct owner execution"
```

---

### Task 7: Add same-job base-SHA versus candidate performance evidence without a second production owner

**Files:**
- Create: `cnet/benchmarks/cnet_owner_lifecycle_compare.c`
- Modify: `cnet/benchmarks/CMakeLists.txt`
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

**Interface:**

```text
cnet_owner_lifecycle_compare <baseline-cnet-dso> <candidate-cnet-dso> <report-path>
```

Frozen workload:

```c
enum {
  CNET_OWNER_COMPARE_REPEATS = 5,
  CNET_OWNER_COMPARE_WARMUPS = 32,
  CNET_OWNER_COMPARE_EXCHANGES = 512,
  CNET_OWNER_COMPARE_PAYLOAD_BYTES = 1024
};
```

- [ ] **Step 1: Create the runner with a deterministic self-test**

Support exactly `--self-test`:

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

All other invocations require exactly three paths after the executable name.

- [ ] **Step 2: Implement uninstrumented DSO comparison**

Use public CNet headers for types but do not link `salts_cnet`. Load one library at a time and resolve:

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

Use `LoadLibraryA/GetProcAddress` on Windows and `dlopen(RTLD_NOW | RTLD_LOCAL)/dlsym` on POSIX.

For each repeat:

```c
const bool candidate_first = (repeat & 1u) != 0u;
```

Each sample uses a fresh client + dedicated loopback echo peer, 32 warmups, then 512 measured persistent 1 KiB round trips. DSO load/unload, fixture construction, connect, receive-demand setup, and teardown remain outside the measured interval.

Report:

```text
baseline/candidate DSO paths
5 raw paired rows
paired p50 median/MAD
paired p95 median/MAD
paired rate median/MAD
p50 direction counts (candidate faster/tie/slower)
```

Do not assert or bake in #288's 3.11% result.

- [ ] **Step 3: Wire build and self-test**

In `cnet/benchmarks/CMakeLists.txt`:

```cmake
cmake_add_benchmark(cnet_owner_lifecycle_compare
  SOURCES cnet_owner_lifecycle_compare.c cnet_benchmark_stats.c
  LIBS Salts::Platform Salts::Core Salts::NativeIO
  FOLDER "cnet/benchmarks")
target_include_directories(cnet_owner_lifecycle_compare PRIVATE ../include ../src)
if(UNIX AND NOT APPLE)
  target_link_libraries(cnet_owner_lifecycle_compare PRIVATE dl)
endif()
add_test(NAME cnet_owner_lifecycle_compare_self_test
  COMMAND cnet_owner_lifecycle_compare --self-test)
add_dependencies(cnet_io_benchmark cnet_owner_lifecycle_compare)
```

- [ ] **Step 4: Run the runner self-test locally**

```bash
cmake --build --preset linux-release-user --target \
  cnet_owner_lifecycle_compare cnet_benchmark_stats_test cnet_io_benchmark
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_owner_lifecycle_compare_self_test|cnet_benchmark_stats_test)$' \
  --output-on-failure
```

Expected: both pass.

- [ ] **Step 5: Build the PR base SHA in the same Windows IOCP job**

After candidate correctness passes, create a detached worktree:

```powershell
$baselineSource = Join-Path $env:RUNNER_TEMP "salts-cnet-baseline"
git worktree add --detach $baselineSource "${{ github.event.pull_request.base.sha }}"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Use the same Visual Studio environment and vcpkg cache, then configure/build from inside the baseline worktree so its preset output directory is isolated under that worktree:

```powershell
Push-Location $baselineSource
cmake --preset win-release-user -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset win-release-user --target salts_cnet
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Pop-Location
```

Use:

```powershell
$baselineDso = Join-Path $baselineSource "build\Msvc-Release\bin\salts_cnet.dll"
$candidateDso = Join-Path $env:GITHUB_WORKSPACE "${{ matrix.build_dir }}\bin\salts_cnet.dll"
$compare = Join-Path $env:GITHUB_WORKSPACE "${{ matrix.build_dir }}\bin\cnet_owner_lifecycle_compare.exe"
```

Validate all three paths before measurement.

- [ ] **Step 6: Run paired base/candidate measurement before the existing full benchmark**

```powershell
$resultDir = Join-Path $env:GITHUB_WORKSPACE "native-io-results"
New-Item -ItemType Directory -Force -Path $resultDir | Out-Null
$env:CNET_IO_BENCHMARK_BACKEND = "iocp"
& $compare $baselineDso $candidateDso `
  (Join-Path $resultDir "cnet-owner-base-vs-direct.md")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Then run the existing `cnet_io_benchmark` unchanged so the same `io-benchmark-windows-iocp` artifact also contains `libuv-native-io-cnet-benchmark.md` with the NativeIO direct A/A control.

- [ ] **Step 7: Commit**

```bash
git add cnet/benchmarks/cnet_owner_lifecycle_compare.c \
  cnet/benchmarks/CMakeLists.txt .github/workflows/native-io-release-benchmarks.yml
git commit -m "bench(cnet): compare base owner with direct production path"
```

---

### Task 8: Exact-head release matrix, performance decision, and final audit

**Files:** Evidence-only task. Do not modify source in this task. Any source failure returns execution to the task that owns that behavior rather than being patched here.

- [ ] **Step 1: Audit the implementation boundary**

```bash
git diff --stat master...HEAD
git diff --name-only master...HEAD
git grep -n -E 'native_io_backend_spawn_coroutine|native_io_coroutine_await|native_io_backend_cancel_coroutine' -- cnet/src/cnet_owner.c cnet/src/cnet_owner.h
```

Expected: one canonical owner implementation and no per-I/O coroutine lifecycle calls.

Also verify none of these exist in the diff:

```text
source-text owner generator
duplicate direct-owner CNet library
runtime owner mode
profile-only execution-mode setter
#286 retained-buffer implementation
```

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

Expected: all applicable tests pass; VSOCK follows its existing skip contract where unsupported.

- [ ] **Step 3: Open or refresh the production PR after local verification**

The PR body links #290, the committed spec, and this committed plan. State explicitly that #289 remains evidence-only and is not merged.

- [ ] **Step 4: Require one exact-head release benchmark run**

Record:

```text
head SHA
workflow run ID
IOCP job ID
epoll job ID
io_uring job ID
kqueue job ID
artifact IDs
artifact SHA-256 digests
```

All four correctness jobs must be green at the same head.

- [ ] **Step 5: Evaluate Windows IOCP performance against same-run noise**

Read from the same IOCP artifact:

```text
cnet-owner-base-vs-direct.md
libuv-native-io-cnet-benchmark.md
```

Apply the spec gate:

1. candidate correctness is green;
2. candidate p50 direction is beneficial relative to the base-SHA coroutine owner;
3. compare candidate paired p50 improvement magnitude with the same-run NativeIO direct A/A noise envelope;
4. raw p50 repeats do not show unstable direction reversal;
5. p95 and rate show no material aggregate regression.

Do not require exact reproduction of #288's `-3.11%`.

If p50 resolves above noise, record that production retained measurable signal. If p50 is below noise but p95/rate show no regression, record the verification as noise-limited and explain the difference from #288 without changing the architecture claim. A material p95/rate regression or semantic divergence blocks merge.

- [ ] **Step 6: Record final evidence on #290 and stop before merge**

The #290 evidence comment contains the exact-head correctness matrix and paired performance table. Do not merge automatically; leave the production PR for explicit review/merge.
