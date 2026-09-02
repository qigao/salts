# Coroutine Executor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded multi-core coroutine executor whose persistent worker shards are isolated from user threads while keeping `turbo_coro_pool_t` a single-owner frame reuse primitive.

**Architecture:** `turbo_coro_executor_t` is an opaque owner built over the existing `turbo_threadpool_t`. Each persistent worker task owns exactly one scheduler, one coroutine pool, one bounded task queue, and one bounded completion-wake queue; submitted coroutines stay on that shard for their entire lifetime. Submission copies a small task descriptor. A generation-checked await token routes an external terminal signal back to the owner shard without resuming on the completion thread.

**Tech Stack:** C11, TurboUtils Coroutine, Concurrency/Disruptor, Platform threading, TinyTest, CMake presets.

**Spec:** `docs/superpowers/plans/2026-09-02-coro-executor.md#design-protocol`

## Global Constraints

- Do not add thread or executor behavior to `turbo_coro_pool_t`; only document its single-owner contract.
- Reuse `turbo_threadpool_t`, `disruptor_t`, `turbo_mutex_t`, and `turbo_cond_t`; do not introduce another generic thread-pool or queue implementation.
- Every worker owns a private scheduler and pool. Coroutine frames and live coroutines never migrate between workers.
- All capacities are hard limits. Queue full returns `TURBO_ENOBUFS`; closed admission returns `TURBO_ESHUTDOWN`.
- A successful task submission invokes exactly one of `run` or `cancel`, then invokes `finalize` when non-NULL. A rejected submission invokes no callback.
- `shutdown` closes admission and drains accepted tasks. `destroy` requires external callers to have stopped concurrent API calls and must not be called from an executor callback.
- The generic executor runs finite cooperative coroutines and provides only a generic await/wake route. NativeIO request ownership and completion observation remain a later adapter and are not moved into this module.

## Design Protocol

| Item | Contract |
|---|---|
| Data unit | A copied fixed-size `turbo_coro_executor_task_t`; plus one copyable `turbo_coro_executor_await_t` naming an executor-owned await slot. |
| Fact source | The task queue is authoritative before start. An external subsystem remains authoritative for operation progress/result; an await slot stores only bound frame, terminal signal, and copied status. |
| Ownership | Rejected submission leaves `arg` with the caller and invokes no callback. Accepted submission borrows `arg` until `finalize`, or until `run`/`cancel` returns when `finalize` is NULL. |
| Topology | MPSC producers per shard, one fixed consumer/owner thread per shard. The backing worker pool runs one persistent owner task per worker. |
| Ordering | FIFO within one shard. Round-robin submission does not promise global order across shards. Explicit `submit_to` preserves connection affinity. |
| Capacity | `queue_capacity_per_worker` task entries and `coroutine_pool.max_capacity` frames/await slots per shard. The wake queue is the next power of two at least as large as the frame limit, so one terminal wake per active frame cannot overflow under the protocol. |
| Backpressure | `try_submit*` returns `TURBO_ENOBUFS`. Blocking `submit*` waits for queue space, but returns `TURBO_EBUSY` instead of self-blocking from the same executor. Shutdown wakes blocked submitters with `TURBO_ESHUTDOWN`. |
| Failure | A worker-side frame creation/storage failure invokes `cancel(arg, status)` when present, then `finalize(arg)` when present, and records a cancelled settlement. |
| Shutdown | Close task admission while continuing to accept terminal completion for already-issued await handles. External operation owners must publish terminal completion and quiesce before destroy; missing completion prevents drain. |
| Observability | Aggregate task counters plus active/waiting await counts, worker count, and task queue capacity. |

---

### Task 1: Public executor contract and failing behavior tests

**Files:**
- Create: `coroutine/include/turbo_coro_executor.h`
- Create: `coroutine/tests/turbo_coro_executor_test.c`
- Create: `coroutine/tests/CMakeLists.txt`
- Modify: `coroutine/CMakeLists.txt`
- Modify: `coroutine/include/turbo_coro_pool.h`

**Interfaces:**
- Produces: opaque `turbo_coro_executor_t`, configuration/stat/task descriptors, create, submit/try-submit, shard submit, shutdown, wait, destroy, current-executor/current-shard, and stats APIs.

- [x] **Step 1: Write the failing tests**

  Add TinyTest cases that compile against the desired header and assert: explicit shard execution survives `coro_yield()`, current executor is NULL on the user thread, round-robin reaches multiple shards, a full queue returns `TURBO_ENOBUFS`, blocking submission resumes after capacity is released, shutdown rejects new work while draining accepted work, and task finalization occurs exactly once.

- [x] **Step 2: Register the test target**

  Add `coroutine/tests/CMakeLists.txt` using `cmake_add_test(... LIBS TurboUtils::Coroutine TurboUtils::TinyTest)` and enable the subdirectory under `BUILD_TESTS`.

- [x] **Step 3: Verify RED**

  Run `cmake --fresh --preset win-release-user`, then build `turbo_coro_executor_test`. Expected result: compilation fails because `turbo_coro_executor.h` and its symbols do not yet exist.

### Task 2: Bounded sharded executor implementation

**Files:**
- Create: `coroutine/src/turbo_coro_executor.c`
- Modify: `coroutine/CMakeLists.txt`

**Interfaces:**
- Consumes: `turbo_threadpool_t`, `disruptor_t`, `coro_scheduler_t`, `turbo_coro_pool_t`, Turbo error codes, mutexes, and condition variables.
- Produces: the complete API declared by Task 1.

- [x] **Step 1: Implement validated construction**

  Validate worker count, power-of-two per-worker queue capacity, bounded coroutine capacity, and descriptor storage size with checked arithmetic. Allocate all shard control structures, register one Disruptor consumer per shard, create private scheduler/pool instances, then submit one persistent shard loop per backing thread-pool worker. Any partial failure unwinds initialized resources in reverse order.

- [x] **Step 2: Implement admission and affinity**

  Copy descriptors into the selected shard queue. Round-robin uses an unsigned atomic sequence; explicit shard APIs reject an out-of-range shard with `TURBO_EINVAL`. `try` APIs reject full queues immediately; blocking APIs wait on the shard space condition and reject same-executor self-wait with `TURBO_EBUSY`.

- [x] **Step 3: Implement owner execution and settlement**

  The owner drains FIFO commands while private pool capacity is available, copies each task into coroutine storage, adopts the frame into its scheduler, and releases the queue entry. Scheduler cleanup returns the frame to the same private pool and updates completion/progress counters. Start failures call cancel/finalize on the owner thread.

- [x] **Step 4: Implement shutdown, waiting, and stats**

  Close admission once, wake blocked producers and workers, drain accepted work, expose aggregate counters, reject self-wait, and destroy only after all persistent shard loops have returned.

- [x] **Step 5: Verify GREEN**

  Build and run `turbo_coro_executor_test`; all new tests must pass without warnings or hangs.

### Task 3: Installed API and compatibility verification

**Files:**
- Modify: `tests/install_consumer/consumer.c`
- Modify: `coroutine/CMakeLists.txt`

**Interfaces:**
- Consumes: installed `TurboUtils::Coroutine` only.
- Produces: an installed-header/link contract for the executor and its transitive Concurrency/Platform dependencies.

- [x] **Step 1: Extend the installed consumer**

  Include `turbo_coro_executor.h`, create a one-worker executor, submit one finite coroutine, wait, and destroy it. The consumer must link only `TurboUtils::Coroutine`.

- [x] **Step 2: Run package verification**

  Build the install preset and run `verify_installed_package` so the exported target proves the new transitive dependency and installed header contract.

### Task 4: Regression and delivery verification

**Files:**
- Modify only if verification exposes a defect in files already listed above.

- [x] **Step 1: Format and focused verification**

  Run `clang-format` on the new/changed C headers, sources, and tests; rebuild and run `turbo_coro_executor_test` plus `test_turbo_coro`.

- [x] **Step 2: Sanitizer and adjacent regressions**

  Build the focused targets with `win-dev-user`, run the executor and coroutine tests under ASan, then run the Release Coroutine/Concurrency test subset.

- [x] **Step 3: Repository verification**

  Run the complete Release build/CTest when focused tests are green, `git diff --check`, `codegraph sync .`, and confirm `.codegraph/` remains untracked/ignored.

- [ ] **Step 4: Commit and update the existing PR**

  Commit the verified executor as a separate logical commit and push `feat/cnet-tls` so PR #204 includes the new capability and CI can validate all platforms.

### Task 5: Executor-aware cooperative yield and external await

**Files:**
- Modify: `coroutine/include/turbo_coro_executor.h`
- Modify: `coroutine/src/turbo_coro_executor.c`
- Modify: `coroutine/tests/turbo_coro_executor_test.c`
- Modify: `coroutine/README.md`
- Modify: `tests/install_consumer/consumer.c`

- [x] **Step 1: Write failing lifecycle tests**

  Cover calls outside an executor coroutine, completion before suspend, external completion after suspend, same-shard resume, completion after shutdown, abort after external submission failure, stale handles, cross-executor rejection, and cleanup of an unconsumed await.

- [x] **Step 2: Add generation-checked await slots**

  Allocate exactly one slot per possible active frame. Allow at most one active await per frame. `await_begin` reserves the slot, `await_abort` releases an unsubmitted operation, and normal task cleanup invalidates an unconsumed handle.

- [x] **Step 3: Add a separate bounded completion path**

  Allocate a per-shard Disruptor wake queue with capacity rounded up from the maximum active frame count. `await_complete` may run on any external thread but only publishes a copied handle/status; the shard owner consumes that queue before scheduling ready frames.

- [x] **Step 4: Preserve completion-before-suspend and shutdown semantics**

  Record early completion directly in its slot so `await` returns without suspension. Continue accepting completion for valid handles after task admission closes. Duplicate completion is explicit and stale handles cannot name another executor.

- [x] **Step 5: Verify installed API and concurrency regressions**

  Run focused Release/ASan tests, repeat the race-sensitive executor suite, verify installed C/C++ consumers, then run the complete Release build and CTest before delivery.
