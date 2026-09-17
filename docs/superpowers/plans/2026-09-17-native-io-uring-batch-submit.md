# NativeIO io_uring Batched Submission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make NativeIO's Linux io_uring backend publish adjacent SQEs in userspace and flush them together at progress boundaries instead of calling `io_uring_enter(..., 1, ...)` for every request.

**Architecture:** Split SQ publication from kernel submission inside `native_io_io_uring.c`. `submit()` and in-flight `cancel()` publish SQEs without normally entering the kernel; `observe()` flushes all published work before blocking and flushes lane-successor SQEs generated while processing CQEs before returning completions. Add internal Linux-only profiling counters exposed through a private test hook so deterministic tests prove batching structurally without changing the public ABI.

**Tech Stack:** C11, Linux io_uring syscalls/ring mmap, TinyTest, CMake/CTest, existing `cnet_io_benchmark` release workflow.

**Spec:** `docs/superpowers/specs/2026-09-17-native-io-linux-kernel-batching-design.md`

## Global Constraints

- Implement issue #298 only; do not modify epoll/readiness policy from #299.
- Preserve `native_io_backend_submit/cancel/observe` public API and ABI.
- Preserve generation checks, bounded request capacity, endpoint ownership, FIFO lane ordering, cancellation, wake, shutdown, and terminal-completion lifetime.
- Only a lane head may be in flight; independent read/write lane heads may be published together.
- No background worker, registered buffers, fixed files, SQPOLL, or multishot receive.
- Correctness tests assert structure/counters and semantics, never wall-clock thresholds.
- Keep the external `cnet_io_benchmark` protocol unchanged for pre/post timing comparison.

---

### Task 1: Add deterministic io_uring submission-profile RED coverage

**Files:**
- Modify: `native-io/src/native_io_internal.h`
- Modify: `native-io/src/native_io_io_uring.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**
- Consumes: existing opaque `native_io_backend` whose `impl` is private to NativeIO.
- Produces: private test/profile snapshot API:

```c
typedef struct native_io_uring_profile {
  uint64_t sqes_published;
  uint64_t enter_calls;
  uint64_t enter_submitted;
  uint64_t pressure_flushes;
} native_io_uring_profile;

bool native_io_io_uring_profile_take(const native_io_backend *backend,
                                     native_io_uring_profile *out_profile);
```

This declaration lives only in `native_io_internal.h`; it is not exported from `salts/native_io.h` and does not alter public ABI.

- [ ] **Step 1: Add the private profile type and accessor declaration**

Append to `native-io/src/native_io_internal.h` under `#if defined(__linux__)`:

```c
typedef struct native_io_uring_profile {
  uint64_t sqes_published;
  uint64_t enter_calls;
  uint64_t enter_submitted;
  uint64_t pressure_flushes;
} native_io_uring_profile;

bool native_io_io_uring_profile_take(const native_io_backend *backend,
                                     native_io_uring_profile *out_profile);
```

Do not add this type to the installed/public header.

- [ ] **Step 2: Add the first failing batching test**

In `native_io_test.c`, include `../src/native_io_internal.h` only on Linux and add a Linux-only test helper using a nonblocking pipe with two independently in-flight lane heads: one PIPE_READ on the read endpoint and one PIPE_WRITE on the write endpoint. Take a profile immediately after both successful `native_io_backend_submit()` calls and before `observe()`:

```c
native_io_uring_profile before = {0};
check_equal(native_io_backend_submit(&backend, &read_op, &read_request), SALTS_OK);
check_equal(native_io_backend_submit(&backend, &write_op, &write_request), SALTS_OK);
check_true(native_io_io_uring_profile_take(&backend, &before));
check_equal(before.sqes_published, (uint64_t)2u);
check_equal(before.enter_calls, (uint64_t)0u);
check_equal(before.enter_submitted, (uint64_t)0u);
```

Then call `native_io_test_observe_all()` and assert both completions remain `NATIVE_IO_COMPLETION_OK` and bytes match.

- [ ] **Step 3: Run the focused test and verify RED**

Run:

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON
cmake --build --preset linux-release-user --target native_io_test
ctest --test-dir build/linux-gcc-release -R '^native_io_test$' --output-on-failure
```

Expected: compile/link failure until the private accessor exists, or (once minimally stubbed) assertion failure because current `uring_publish_sqe()` calls `io_uring_enter()` on each submit.

- [ ] **Step 4: Add the minimal profile fields/accessor without changing submission behavior**

Add `native_io_uring_profile profile;` to `salts_io_uring_impl`. Increment `sqes_published` when an SQE is successfully published to the mapped SQ ring; increment `enter_calls` immediately before each `uring_enter()` invocation and add positive syscall return values to `enter_submitted`. Implement:

```c
bool native_io_io_uring_profile_take(const native_io_backend *backend,
                                     native_io_uring_profile *out_profile) {
  const salts_io_uring_impl *impl;
  if (backend == NULL || out_profile == NULL || backend->impl == NULL) return false;
  impl = (const salts_io_uring_impl *)backend->impl;
  if (impl->base.kind != NATIVE_IO_BACKEND_IO_URING) return false;
  *out_profile = impl->profile;
  return true;
}
```

Keep current immediate `uring_enter(1)` behavior so the test remains RED specifically on `enter_calls == 0`.

- [ ] **Step 5: Re-run and capture valid behavioral RED**

Run the focused CTest command again.

Expected: build succeeds; batching test fails because `before.enter_calls == 2` (or at least nonzero), proving the current eager-enter behavior.

- [ ] **Step 6: Commit the RED checkpoint**

```bash
git add native-io/src/native_io_internal.h native-io/src/native_io_io_uring.c native-io/tests/native_io_test.c
git commit -m "test(native-io): expose io_uring submission batching red"
```

---

### Task 2: Separate SQ publication from flush and make the common pair GREEN

**Files:**
- Modify: `native-io/src/native_io_io_uring.c`
- Test: `native-io/tests/native_io_test.c`

**Interfaces:**
- Consumes: Task 1 profile counters.
- Produces private helpers:

```c
static unsigned uring_pending_sqes(const salts_io_uring_impl *impl);
static int uring_flush_sq(salts_io_uring_impl *impl, bool pressure_flush);
static int uring_publish_sqe(salts_io_uring_impl *impl,
                             const struct io_uring_sqe *prepared);
```

`uring_publish_sqe()` becomes publication-only except when capacity pressure requires a flush.

- [ ] **Step 1: Write RED assertions for one flush after two publishes**

Extend the Task 1 batching test. After `native_io_test_observe_all()` take another profile:

```c
native_io_uring_profile after = {0};
check_true(native_io_io_uring_profile_take(&backend, &after));
check_equal(after.sqes_published, (uint64_t)2u);
check_equal(after.enter_calls, (uint64_t)1u);
check_equal(after.enter_submitted, (uint64_t)2u);
check_equal(after.pressure_flushes, (uint64_t)0u);
```

The exact call count is deterministic for this isolated read+write pair because neither SQE needs a successor or cancel operation.

- [ ] **Step 2: Implement pending-count and flush helpers**

Use monotonic SQ ring counters:

```c
static unsigned uring_pending_sqes(const salts_io_uring_impl *impl) {
  const unsigned head = atomic_load_explicit((_Atomic unsigned *)impl->sq_head,
                                             memory_order_acquire);
  const unsigned tail = atomic_load_explicit((_Atomic unsigned *)impl->sq_tail,
                                             memory_order_acquire);
  return tail - head;
}
```

Implement `uring_flush_sq()` as a loop that recomputes pending entries and retries partial submissions:

```c
static int uring_flush_sq(salts_io_uring_impl *impl, bool pressure_flush) {
  if (pressure_flush) ++impl->profile.pressure_flushes;
  for (;;) {
    const unsigned pending = uring_pending_sqes(impl);
    int submitted;
    if (pending == 0u) return SALTS_OK;
    ++impl->profile.enter_calls;
    submitted = uring_enter(impl, pending, 0u, 0u);
    if (submitted < 0) return submitted;
    if (submitted == 0) return SALTS_EIO;
    impl->profile.enter_submitted += (uint64_t)submitted;
  }
}
```

Do not alter SQ tail inside `uring_flush_sq()`; the kernel advances SQ head for consumed submissions.

- [ ] **Step 3: Rewrite `uring_publish_sqe()` as userspace publication**

Replace eager enter/rollback logic with:

```c
static int uring_publish_sqe(salts_io_uring_impl *impl,
                             const struct io_uring_sqe *prepared) {
  unsigned head = atomic_load_explicit((_Atomic unsigned *)impl->sq_head,
                                       memory_order_acquire);
  unsigned tail = atomic_load_explicit((_Atomic unsigned *)impl->sq_tail,
                                       memory_order_relaxed);
  unsigned index;

  if (tail - head >= *impl->sq_entries) {
    const int status = uring_flush_sq(impl, true);
    if (status != SALTS_OK) return status;
    head = atomic_load_explicit((_Atomic unsigned *)impl->sq_head, memory_order_acquire);
    tail = atomic_load_explicit((_Atomic unsigned *)impl->sq_tail, memory_order_relaxed);
    if (tail - head >= *impl->sq_entries) return SALTS_EBUSY;
  }

  index = tail & *impl->sq_mask;
  impl->sqes[index] = *prepared;
  impl->sq_array[index] = index;
  atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, tail + 1u,
                        memory_order_release);
  ++impl->profile.sqes_published;
  return SALTS_OK;
}
```

There is no tail rollback after publication.

- [ ] **Step 4: Flush pending work at the start of `uring_observe()` before any blocking poll**

After initial `uring_process_cq()` / terminal drain, call:

```c
status = uring_flush_sq(impl, false);
if (status != SALTS_OK) return status;
if (*out_count != 0u) return SALTS_OK;
```

Declare `int status;` once for the function and reuse it in the poll loop.

- [ ] **Step 5: Run focused NativeIO tests**

Run:

```bash
cmake --build --preset linux-release-user --target native_io_test
ctest --test-dir build/linux-gcc-release -R '^native_io_test$' --output-on-failure
```

Expected: batching test GREEN; all pre-existing NativeIO tests GREEN.

- [ ] **Step 6: Commit the first GREEN**

```bash
git add native-io/src/native_io_io_uring.c native-io/tests/native_io_test.c
git commit -m "perf(native-io): batch io_uring submissions before observe"
```

---

### Task 3: Flush CQ-driven successors before returning and preserve cancellation/wake semantics

**Files:**
- Modify: `native-io/src/native_io_io_uring.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**
- Consumes: `uring_flush_sq()` from Task 2.
- Produces: `uring_observe()` guarantee that SQEs generated by `uring_process_cq()` are submitted before terminal completions are returned to the caller.

- [ ] **Step 1: Add a RED FIFO successor test**

Create an io_uring-only pipe test with two queued PIPE_READ requests on the same endpoint and a two-byte writer. Submit read A, read B, and write. Drive one `native_io_backend_observe()` call with completion capacity 1 so read A can become terminal while `uring_process_cq()` publishes read B as the next lane head. Immediately take the profile and assert that every SQE published so far has been submitted before `observe()` returns:

```c
check_true(native_io_io_uring_profile_take(&backend, &profile));
check_equal(profile.enter_submitted, profile.sqes_published);
```

Then drain remaining completions and assert FIFO payload/order A then B.

This fails if the successor is left published but unflushed when `observe()` returns the first completion.

- [ ] **Step 2: Add a RED in-flight cancellation progress test**

Submit a PIPE_READ with no data, call `observe()` through the existing wake/short-timeout setup as needed to ensure it has entered the kernel, then call `native_io_backend_cancel()`. Take the profile before the next observe and assert the cancel SQE may be published without requiring an immediate enter; after the next observe assert:

```c
check_equal(event.kind, NATIVE_IO_COMPLETION_CANCELLED);
check_equal(profile.enter_submitted, profile.sqes_published);
```

Preserve existing accepted cancel/error codes.

- [ ] **Step 3: Flush after each CQ processing boundary before returning completions**

In `uring_observe()`, after each `uring_process_cq()` + `uring_drain_terminals()` pair, call `uring_flush_sq(impl, false)` before testing `*out_count != 0u` and before returning for wake. Required shape:

```c
uring_process_cq(impl);
uring_drain_terminals(impl, events, limit, out_count);
status = uring_flush_sq(impl, false);
if (status != SALTS_OK) return status;
if (*out_count != 0u) return SALTS_OK;
```

Repeat the same ordering after poll readiness. This ensures `uring_start_lane()` work generated while processing CQEs is in the kernel before returning terminal events.

- [ ] **Step 4: Verify wake remains authoritative**

Run the existing NativeIO wake tests plus the full `native_io_test`. Do not replace `poll(ring_fd, wake_fd)` with `io_uring_enter(...GETEVENTS...)` in #298; the eventfd path is intentionally preserved.

Run:

```bash
ctest --test-dir build/linux-gcc-release -R '^native_io_test$' --output-on-failure
```

Expected: all tests GREEN including wake, FIFO, cancellation, broken-pipe, and new successor-flush coverage.

- [ ] **Step 5: Commit CQ-successor/cancel guarantees**

```bash
git add native-io/src/native_io_io_uring.c native-io/tests/native_io_test.c
git commit -m "fix(native-io): flush io_uring successors before observe returns"
```

---

### Task 4: Ring-pressure and shared-runtime regression gate

**Files:**
- Modify: `native-io/tests/native_io_test.c`
- Read/verify only unless failure requires a scoped fix: `cflow` io_uring file tests and CNet tests

**Interfaces:**
- Consumes: Task 2 pressure-flush path.
- Produces deterministic evidence that a full SQ ring is flushed instead of dropping or overwriting published SQEs.

- [ ] **Step 1: Add a small-capacity pressure RED/GREEN test**

Use an io_uring backend configured with enough request slots to publish more independent head operations than the mapped SQ can retain without progress, using separate endpoints/lane heads where required. Assert after submissions:

```c
check_true(native_io_io_uring_profile_take(&backend, &profile));
check_true(profile.pressure_flushes > 0u);
check_true(profile.enter_submitted <= profile.sqes_published);
```

After draining all operations:

```c
check_equal(profile.enter_submitted, profile.sqes_published);
```

If the kernel-created SQ has more entries than the public request capacity makes reachable in this test harness, do not add artificial production limits. Instead add a Linux test-only internal helper that exercises `uring_publish_sqe()` capacity using the real mapped ring and records the pressure flush; keep the helper private to the NativeIO test build.

- [ ] **Step 2: Run NativeIO + NativeIPC contracts**

```bash
cmake --build --preset linux-release-user --target native_io_test native_io_header_cpp_test native_ipc_test native_ipc_header_cpp_test
ctest --test-dir build/linux-gcc-release -R '^native_(io|ipc)_(test|header_cpp_test)$' --output-on-failure
```

Expected: 100% pass.

- [ ] **Step 3: Run CNet contracts**

Use the same release-workflow selection as PR #295:

```bash
ctest --test-dir build/linux-gcc-release -R '^(cnet_.*_test|concurrency_deadline_queue_test)$' --timeout 60 --output-on-failure
```

Expected: 100% pass.

- [ ] **Step 4: Run shared-runtime io_uring file/pipe regressions**

Search the configured CTest list for the existing file-forget/shared-runtime regression introduced around PR #296 and execute those exact tests together with NativeIO pipe tests. Do not broaden production scope unless a reproducible failure is caused by deferred SQ publication.

```bash
ctest --test-dir build/linux-gcc-release -N | grep -E 'file|io_uring|native_io|cflow'
```

Then run the identified shared-runtime tests with `--output-on-failure`.

Expected: GREEN.

- [ ] **Step 5: Commit pressure/regression coverage**

```bash
git add native-io/tests/native_io_test.c
git commit -m "test(native-io): cover io_uring batch pressure and shared runtime"
```

---

### Task 5: Exact-head benchmark evidence and final audit

**Files:**
- No production change expected.
- Update issue/PR discussion with measured evidence; do not bake timing thresholds into source.

**Interfaces:**
- Consumes: existing `cnet_io_benchmark` and release workflow.
- Produces: exact-head evidence for #298 and a clear decision whether #298 materially reduces Linux/io_uring kernel-transition overhead before #299 starts.

- [ ] **Step 1: Run local release benchmark as a sanity check**

```bash
cmake --build --preset linux-release-user --target cnet_io_benchmark
CNET_IO_BENCHMARK_BACKEND=io_uring build/linux-gcc-release/bin/cnet_io_benchmark
```

Expected: successful benchmark output with unchanged protocol and no functional failures.

- [ ] **Step 2: Push exact implementation head and run the canonical release workflow**

Use the repository's existing `NativeIO and CNet release benchmarks` workflow. Do not modify the workflow merely to obtain a favorable result.

Required io_uring artifact: `io-benchmark-linux-io-uring`.

- [ ] **Step 3: Compare against frozen PR #295 baseline**

Report for each TCP payload 1/4/8/16/32/64 KiB:

- NativeIO direct p50 and p95 before/after;
- rate before/after;
- NativeIO direct versus libuv paired delta after;
- `native_start_ns`/submit-stage before/after;
- structural profile: SQEs published, enter calls, total submitted, and average SQEs per enter on the deterministic test path.

The principal correctness claim is structural: the common independent recv+send pair is published twice and flushed with one enter while semantics remain green. Timing determines follow-up priority; there is no fixed pass percentage.

- [ ] **Step 4: Final scope audit**

Run:

```bash
git diff --name-only 0bdf08af540efe672832d0ce5bbfff1632cd27b0..HEAD
```

Expected implementation files are limited to:

```text
native-io/src/native_io_internal.h
native-io/src/native_io_io_uring.c
native-io/tests/native_io_test.c
docs/superpowers/plans/2026-09-17-native-io-uring-batch-submit.md
```

Any additional production file requires an explicit correctness reason in the PR.

- [ ] **Step 5: Final verification commit only if documentation/evidence files changed**

Do not create an empty commit. If only GitHub issue/PR comments carry the evidence, leave the verified implementation head unchanged.
