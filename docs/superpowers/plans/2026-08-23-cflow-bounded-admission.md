# CFlow Bounded Admission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bound built-in Executor and TimerQueue resources and expose exact admission outcomes without removing existing convenience APIs.

**Architecture:** Concurrency supplies stable thread-pool error codes; CFlow maps them to one admission enum. ManualExecutor and TimerQueue preallocate fixed arrays, while WorkerExecutor reuses the existing bounded thread pool. Manual and scheduler compatibility methods delegate to checked admission; Worker/Serial `post` preserves its blocking backpressure contract while `try_post` is nonblocking.

**Status:** Complete. Implemented in commits `b90abda`, `1466b12`, `4d621ab`, and `50169a0`; documented and verified by the closeout commit for this plan.

**Tech Stack:** C11, CMeta interfaces, Salts Platform/Concurrency, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

## Global Constraints

- Preserve existing function names and success behavior.
- Default capacity is the named value `4096`; zero capacity is invalid.
- Full, closed, invalid, and allocation failure are distinct checked results.
- No growing allocation occurs after successful ManualExecutor or TimerQueue initialization.
- No accepted timer task may be dropped when it moves from TimerQueue to WorkerExecutor.
- Existing thread-pool callers that test nonzero failure remain source-compatible; exact `-1` comparisons migrate to stable Salts error codes.

---

### Task 1: Stable Concurrency admission errors

**Files:**
- Modify: `concurrency/src/thread_pool.c`
- Modify: `concurrency/include/salts/thread_pool.h`
- Modify: `concurrency/tests/thread_pool_test.c`

**Interfaces:**
- Consumes: `SALTS_OK`, `SALTS_EINVAL`, `SALTS_ENOBUFS`, `SALTS_ESHUTDOWN` from `salts_error.h`.
- Produces: exact return codes from `salts_threadpool_submit()` and `salts_threadpool_try_submit()`.

- [x] **Step 1: Write failing exact-status tests**

Add cases that assert invalid arguments return `SALTS_EINVAL`, a saturated nonblocking submission returns `SALTS_ENOBUFS`, and a post-shutdown submission returns `SALTS_ESHUTDOWN`. Keep the gated-task fixture so queue occupancy is deterministic.

```c
check_equal(salts_threadpool_try_submit(NULL, gated_task, NULL), SALTS_EINVAL);
check_equal(salts_threadpool_try_submit(pool, NULL, NULL), SALTS_EINVAL);
check_equal(salts_threadpool_try_submit(pool, gated_task, NULL), SALTS_ENOBUFS);
salts_threadpool_shutdown(pool);
check_equal(salts_threadpool_try_submit(pool, gated_task, NULL), SALTS_ESHUTDOWN);
```

- [x] **Step 2: Run the focused test and confirm RED**

Run the configured Windows Release target and `ctest --preset win-release-user -R "^thread_pool_test$" --output-on-failure`. Expected failure: current implementation returns `-1` for all three conditions.

- [x] **Step 3: Return stable errors from the existing state machine**

Include `<salts_error.h>`. Validate arguments first, check `accepting` before claiming queue depth, return `SALTS_ENOBUFS` only from nonblocking full admission, and return `SALTS_ESHUTDOWN` when shutdown wakes a blocked submitter. Do not change the claim/publish/release sequence or accepted-task counters.

- [x] **Step 4: Run Concurrency owner tests**

Run `thread_pool_test` and `disruptor_test`. Expected: all pass; queue capacity, MPMC submission, shutdown, pending, and statistics behavior remain intact.

- [x] **Step 5: Commit**

```text
fix(concurrency): report exact thread-pool admission errors
```

### Task 2: Checked CFlow admission interface

**Files:**
- Create: `cflow/include/cflow/admission.h`
- Modify: `cflow/include/cflow/executor.h`
- Modify: `cflow/include/cflow/scheduler.h`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/src/executor.c`
- Modify: `cflow/src/scheduler.c`
- Modify: `cflow/src/scheduler_worker.c`
- Test: `cflow/tests/cflow_execution_test.c`
- Test: `cflow/tests/cflow_scheduler_compat_test.c`

**Interfaces:**
- Consumes: exact Concurrency return codes from Task 1.
- Produces: `cflow_admission_status`, `cflow_schedule_result`, `cflow_executor_try_post()`, and `cflow_scheduler_try_post_after()`.

- [x] **Step 1: Add failing checked-admission tests**

Assert invalid callbacks, exact-capacity acceptance, capacity-plus-one rejection, and compatibility wrapper behavior:

```c
check_equal(cflow_executor_try_post(&executor, NULL, NULL),
            CFLOW_ADMISSION_INVALID_ARGUMENT);
check_equal(cflow_executor_try_post(&executor, count_task, NULL),
            CFLOW_ADMISSION_ACCEPTED);
check_equal(cflow_executor_try_post(&executor, count_task, NULL),
            CFLOW_ADMISSION_FULL);
check_false(cflow_executor_post(&executor, count_task, NULL));
```

For Scheduler, assert `result.status == CFLOW_ADMISSION_ACCEPTED` has a nonzero id and every failure has id zero.

- [x] **Step 2: Run focused tests and confirm RED**

Build `cflow_execution_test` and `cflow_scheduler_compat_test`; run their CTest regex. Expected failure: checked types and functions do not exist.

- [x] **Step 3: Add the public enum and checked methods**

Define the types exactly as the design spec, including moving the sole `cflow_task_id` typedef into `admission.h`. Extend built-in Executor/Scheduler interface operation tables with checked calls. Implement compatibility `post` as `try_post == ACCEPTED` and compatibility `post_after` as the checked result's task id. Map Salts errors without collapsing `ENOBUFS` and `ESHUTDOWN`.

- [x] **Step 4: Verify source compatibility**

Build `tests/install_consumer/consumer.c` through `verify_installed_package`. Existing users of bool/id APIs must compile unchanged.

- [x] **Step 5: Commit**

```text
feat(cflow): expose checked execution admission
```

### Task 3: Fixed-capacity ManualExecutor and TimerQueue

**Files:**
- Modify: `cflow/src/executor.c`
- Modify: `cflow/src/timer_queue.h`
- Modify: `cflow/src/timer_queue.c`
- Modify: `cflow/src/scheduler.c`
- Modify: `cflow/src/scheduler_worker.c`
- Test: `cflow/tests/cflow_execution_test.c`

**Interfaces:**
- Consumes: checked admission from Task 2.
- Produces: the five `_init_with_capacity` initializers and fixed-capacity runtime behavior.

- [x] **Step 1: Add capacity boundary and allocation-count tests**

Cover zero, one, exact capacity, capacity plus one, and arithmetic overflow. Initialize capacity one, accept one item, reject the second, execute/cancel the first, then accept another to prove slot reuse. Record the task-array pointer before and after repeated operations and assert it is unchanged.

- [x] **Step 2: Run focused tests and confirm RED**

Expected failure: current ManualExecutor and TimerQueue grow with `realloc` and expose no capacity initializer.

- [x] **Step 3: Preallocate exact arrays**

At initialization, reject zero and `capacity > SIZE_MAX / sizeof(element)`, allocate exactly `capacity * sizeof(element)`, and store `limit` separately from current count. Delete both growth helpers. Full insertion returns `CFLOW_ADMISSION_FULL`; allocation failure occurs only during initialization.

- [x] **Step 4: Wire named defaults and WorkerExecutor capacity**

Existing initializers delegate with `CFLOW_EXECUTOR_DEFAULT_CAPACITY` or `CFLOW_TIMER_DEFAULT_CAPACITY`. Worker/Serial initializers call `salts_threadpool_create_with_config()` using the requested queue capacity.

- [x] **Step 5: Run CFlow owner tests**

Run `cflow_execution_test`, `cflow_scheduler_compat_test`, `cflow_runtime_test`, and `cflow_source_test`. Expected: all pass with the same value/order/cancellation observations.

- [x] **Step 6: Commit**

```text
feat(cflow): bound executor and timer admission
```

### Task 4: Lossless timer-to-executor handoff and metrics

**Files:**
- Modify: `cflow/include/cflow/executor.h`
- Modify: `cflow/include/cflow/scheduler.h`
- Modify: `cflow/src/executor.c`
- Modify: `cflow/src/scheduler_worker.c`
- Test: `cflow/tests/cflow_execution_test.c`

**Interfaces:**
- Consumes: bounded TimerQueue and WorkerExecutor.
- Produces: capacity/current/peak/rejection statistics and a lossless handoff state machine.

- [x] **Step 1: Add deterministic saturated-handoff tests**

Use a one-worker/one-ready-slot scheduler and gated tasks. Saturate the executor, make a timer ready, verify it remains accounted as dispatching while blocked, release capacity, and assert the timer callback runs exactly once. Repeat with shutdown during handoff and assert one cancelled/rejected-closed count with no callback.

- [x] **Step 2: Run the tests and confirm the current loss bug**

The current timer thread ignores `cflow_executor_post()` failure after removing a timer. The new saturation seam must expose that the callback disappears.

- [x] **Step 3: Implement accepted-task terminal states**

Use blocking bounded submission for timer handoff. Track `dispatching` from removal until accepted or shutdown. Every removed timer reaches exactly one of `accepted` or `cancelled_on_shutdown`. Update counters while holding the scheduler mutex; invoke no user callback while holding it.

- [x] **Step 4: Run shutdown and runtime regression tests**

Run all `cflow_*` CTests plus `thread_pool_test`. Expected: clean shutdown, no duplicate callback, and no hanging waiter.

- [x] **Step 5: Commit**

```text
fix(cflow): preserve timers across bounded handoff
```

### Task 5: Documentation and full verification

**Files:**
- Modify: `docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md`
- Modify: `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

- [x] **Step 1: Document migration and resource budget**

Record the `-1` to Salts error-code migration, default capacities, per-entry byte formula, backpressure policy, shutdown states, and checked/compatibility API mapping.

- [x] **Step 2: Run verification in increasing scope**

Run focused owner tests, `verify_installed_package`, all CFlow/Concurrency CTests, then full Windows Release CTest. Run `git diff --check`.

- [ ] **Step 3: Commit**

```text
docs(cflow): define bounded execution admission
```
