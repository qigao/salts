# CFlow Execution Model v2 Design

## Status

In progress. Phase G-1 bounded admission is implemented on the execution-model-v2 branch. Ordered parallel reduction, executable Lean/C refinement evidence, and macOS/Android host verification remain subsequent phases.

The proposal intentionally does not add Event, Mailbox, Machine, Actor, reactor, or minicoro adapters. Those remain consumers of the execution foundation rather than prerequisites for completing the current Direct/Plan/Kernel model.

## Decision summary

The work is split into four independently reviewable changes:

1. make built-in Executor and TimerQueue growth bounded and expose exact admission results;
2. add an explicitly requested, ordered Parallel Reduce path to compiled Plan evaluation;
3. replace the abstract Graph-to-Plan trust note with a checkable certificate contract for the supported C fragment;
4. add native macOS conformance and Android arm64 cross-build jobs.

Each change must pass independently. A failure in a later phase does not weaken or roll back an earlier phase's public contracts.

## Approved compatibility transition

The existing `turbo_threadpool_submit()` and `turbo_threadpool_try_submit()` return `0` on acceptance and an undocumented `-1` for every failure. Exact CFlow admission reporting cannot distinguish invalid input, full queue, or shutdown while that ambiguity remains.

Execution Model v2 defines stable TurboUtils error returns:

| Condition | Return code |
|---|---|
| accepted | `TURBO_OK` |
| invalid pool or callback | `TURBO_EINVAL` |
| bounded queue full in `try_submit` | `TURBO_ENOBUFS` |
| pool no longer accepts work | `TURBO_ESHUTDOWN` |
| internal allocation failure during creation | creation returns `NULL` |

Callers that already test `rc != 0` remain source-compatible. A caller that compares exactly with `-1` must migrate. This is an observable return-code change and must be approved before implementation.

The dependency-neutral error constants live in `platform/include/turbo/error_codes.h`; the existing Core `turbo_error.h` includes that header. This preserves the dependency direction `Core -> Concurrency -> Platform` while giving Concurrency and CFlow one error-code fact source.

Adding checked admission, shutdown, and statistics methods to the generated Executor/Scheduler operation tables also requires external interface implementers to rebuild and provide the new entries. Ordinary callers retain the same `cflow_executor`/`cflow_scheduler` object layout and convenience calls, but an independently compiled custom implementation is not binary-compatible across this v2 boundary. The repository currently exposes no versioned plugin ABI for these interfaces; the change is therefore an explicit v2 ABI transition.

Existing CFlow convenience calls remain available:

- `cflow_executor_post()` continues to return `bool`;
- `cflow_scheduler_post()` and `cflow_scheduler_post_after()` continue to return a nonzero task id or zero;
- existing initializers continue to exist and use named defaults.

ManualExecutor and both schedulers map compatibility calls directly to checked admission. WorkerExecutor and SerialExecutor preserve the existing blocking `post()` contract so a full bounded queue applies backpressure; their new `try_post()` is the nonblocking exact-status operation. Thus existing producer behavior is preserved while new callers can select fail-fast admission explicitly.

New checked entry points carry the exact status. No implicit retry, allocation beyond the configured limit, or serial fallback is permitted.

## Phase G-1: bounded admission

### Public types

`cflow/include/cflow/admission.h` defines:

```c
typedef enum cflow_admission_status {
    CFLOW_ADMISSION_ACCEPTED = 0,
    CFLOW_ADMISSION_INVALID_ARGUMENT,
    CFLOW_ADMISSION_FULL,
    CFLOW_ADMISSION_CLOSED,
    CFLOW_ADMISSION_ALLOCATION_FAILED
} cflow_admission_status;

typedef uint64_t cflow_task_id;

typedef struct cflow_schedule_result {
    cflow_admission_status status;
    cflow_task_id task_id;
} cflow_schedule_result;
```

`admission.h` becomes the single owner of `cflow_task_id`; `scheduler.h` includes it rather than repeating the typedef.

Executor and Scheduler interfaces gain checked methods while retaining their compatibility methods:

```c
cflow_admission_status cflow_executor_try_post(
    cflow_executor *executor, cflow_task_fn fn, void *user);

cflow_schedule_result cflow_scheduler_try_post_after(
    cflow_scheduler *scheduler, uint64_t delay_ticks,
    cflow_task_fn fn, void *user);
```

They also expose idempotent `shutdown()` and snapshot `get_stats()` operations. Executor statistics report capacity, current/peak pending, and full/closed rejection counts. Scheduler statistics additionally distinguish ready, timer, and dispatching work and count timers cancelled during shutdown.

Built-in initializers gain explicit capacities:

```c
enum { CFLOW_EXECUTOR_DEFAULT_CAPACITY = 4096u };
enum { CFLOW_TIMER_DEFAULT_CAPACITY = 4096u };

bool cflow_executor_manual_init_with_capacity(
    cflow_executor *executor, size_t capacity);
bool cflow_executor_serial_init_with_capacity(
    cflow_executor *executor, size_t capacity);
bool cflow_executor_worker_init_with_capacity(
    cflow_executor *executor, size_t workers, size_t capacity);

bool cflow_scheduler_test_init_with_capacity(
    cflow_scheduler *scheduler, size_t ready_capacity, size_t timer_capacity);
bool cflow_scheduler_worker_init_with_capacity(
    cflow_scheduler *scheduler, size_t workers,
    size_t ready_capacity, size_t timer_capacity);
```

Zero capacity is invalid. Existing initializers delegate to the checked forms with the named defaults.

### Data-path protocol

| Item | Manual/Timer path | Worker path |
|---|---|---|
| Data unit | `(fn,user)` task or deadline task | `(fn,user)` thread-pool task |
| Fact source | fixed-capacity contiguous CFlow array | Concurrency thread-pool Disruptor queue |
| Ownership | task payload is borrowed; scheduler owns only callback records | same; successful submit transfers scheduling responsibility, not payload ownership |
| Topology | single owner | MPMC producers, worker-pool consumers |
| Order | Manual FIFO; Timer deadline then insertion order | worker execution order is not promised |
| Full behavior | checked API returns `FULL`; compatibility API returns false/zero | `try_post` returns `FULL`; blocking `post` applies bounded backpressure |
| Shutdown | reject new work; destroy cancels pending timers and drains or joins executor per existing contract | thread-pool shutdown wakes blocked submitters and workers |
| Observability | capacity, current/peak pending, rejected-full, rejected-closed | derived from thread-pool stats plus scheduler timer counters |

ManualExecutor and TimerQueue allocate their complete task arrays during initialization. The data path performs no `realloc`. Capacity arithmetic is checked before allocation. TimerQueue remains protected by the owning scheduler mutex in the worker scheduler and single-owned by the test scheduler.

The fixed-array payload budget is computed rather than hidden: ManualExecutor reserves `capacity * sizeof(cflow_manual_task)` bytes and TimerQueue reserves `capacity * sizeof(cflow_timer_task)` bytes, after checking `capacity <= SIZE_MAX / sizeof(entry)`. On the current 64-bit ABI the entries are respectively 16 and 40 bytes, so the two default 4096-entry arrays reserve 65,536 and 163,840 bytes (229,376 bytes total), excluding executor/scheduler state and thread-pool storage. Exact byte counts remain ABI-dependent; the formulas and hard capacities are the contract.

The timer thread uses blocking executor submission after removing a ready timer. The task is counted as `dispatching` until it reaches exactly one terminal handoff state: accepted by the executor, or `cancelled_on_shutdown`. Shutdown first closes timer admission, cancels queued timers, wakes a blocked executor submission, joins the timer thread, and then drains/destroys accepted executor work. A ready timer is therefore neither silently dropped nor duplicated under backpressure.

## Phase G-2: ordered Parallel Reduce

### Eligibility

Parallel reduction is Plan-only and opt-in. It is admitted only when:

- the compiled plan is valid and has one terminal `REDUCE` instruction;
- the reducer is a binary endomorphism whose metadata declares `PURE`, `TOTAL`, `ASSOCIATIVE`, and `NO_ALIAS`;
- every preceding instruction is synchronous, total, deterministic, and safe to execute before the split;
- input is a borrowed contiguous array that remains alive for the call;
- a borrowed concurrent executor with checked admission is supplied;
- `task_count >= 2`, `task_count <= max_tasks`, and every chunk is nonempty.

The implementation materializes or fuses the prefix using the existing Plan evaluator, splits the resulting values into contiguous nonempty chunks, reduces each chunk independently, and combines partials in increasing chunk index. This is ordered reassociation. It does not permute values and therefore does not require commutativity.

### API

```c
typedef enum cflow_plan_execution_mode {
    CFLOW_PLAN_EXECUTION_SEQUENTIAL = 0,
    CFLOW_PLAN_EXECUTION_PARALLEL_REDUCE
} cflow_plan_execution_mode;

typedef struct cflow_plan_eval_options {
    cflow_plan_execution_mode mode;
    cflow_executor *executor;
    size_t max_tasks;
    size_t min_items_per_task;
} cflow_plan_eval_options;

bool cflow_plan_parallel_reduce_supported(const cflow_plan *plan);
bool cflow_plan_eval_array_with_options(
    const cflow_plan *plan, const void *inputs, size_t input_count,
    const cflow_plan_eval_options *options, cflow_result *out);
```

`cflow_plan_eval_array()` delegates with sequential options, so existing calls never become parallel implicitly.

### Ownership and failure

The caller owns and keeps the input array and executor alive through return. One evaluation owns its prefix buffer, task descriptors, per-chunk partial slots, mutex, and condition variable. Workers borrow these objects until their completion count is published.

Each task writes only its own partial slot. The caller waits for exactly the number of accepted tasks. If allocation, admission, or a callback fails, no result is committed. Already accepted tasks are joined through the evaluation latch, all temporary objects are destroyed, and the function returns false. It must not restart sequentially after parallel execution begins.

### Complexity

For `n` prefix values and `p` accepted chunks:

- work: `Theta(n)` callbacks;
- reduction critical path: `Theta(ceil(n/p) + p)` callbacks;
- temporary payload: `O(n * value_size + p * partial_slot_size)`;
- scheduler submissions: exactly `p`;
- result count: zero for empty input, otherwise one.

Benchmarks report sequential and ordered-parallel rows for 1 Ki, 64 Ki, and 1 Mi items. Shared-runner results are evidence, not a hard cross-host speed gate. A local regression greater than 10% for the untouched sequential path blocks the change.

## Phase G-3: Lean/C refinement certificate

No claim is made that Lean verifies arbitrary compiled C or the platform ABI. The deliverable is a smaller, checkable contract for the CFlow Plan fragment.

Plan compilation emits an immutable certificate containing:

- source normalized-Graph fingerprint;
- ordered opcode sequence;
- semantic input/output type identities;
- callable semantic identities and declared properties;
- selected execution path and capability requirements;
- ordered Parallel Reduce chunking policy when present.

Certificates are runtime witnesses, not a persistent wire format. Rows retain bounded `cmeta_callable` values and the checker uses `cmeta_callable_same()`; type comparison uses `cmeta_type_equal()`. Only schema version, opcode, path, and property-mask numerals are stable ABI. Pointer values and structure padding are never hashed or serialized.

The C checker validates the certificate transactionally against the normalized Graph and compiled Plan. Tampered, stale, unsupported, or property-incomplete certificates fail without executing.

Lean defines the same certificate schema and proves:

1. a valid linear certificate preserves the normalized Graph's observation;
2. the sequential Plan interpreter preserves the certificate denotation;
3. ordered parallel chunk reduction preserves the sequential reduction result under the R11 premises;
4. rejection of an invalid certificate cannot authorize execution.

The existing `CompileContract` trust note is narrowed to the certificate checker's implementation and the C compiler/runtime. CI builds the Lean project and runs C certificate conformance tests from the same stable enum and schema version. The documentation must continue to call this a refinement certificate, not a full formal verification of the C binary.

## Phase G-4: host verification

The conformance workflow gains:

- one pinned `macos-15` native Release configure/build/test/package-consumer job;
- one pinned `ubuntu-24.04` Android arm64 Release cross-build job.

The Android job is compile/link/package evidence only. It does not claim device runtime behavior. It uses the NDK-provided `<NDK>/build/cmake/android.toolchain.cmake`, which is the Android-supported direct-CMake path, and targets the existing `android-24`/`arm64-v8a` preset contract. The GitHub runner currently exposes `ANDROID_NDK_HOME`; the job validates the directory and toolchain file before configure rather than silently downloading another version.

Primary references:

- [Android NDK CMake toolchain contract](https://developer.android.com/ndk/guides/cmake)
- [GitHub macOS runner image inventory](https://github.com/actions/runner-images/blob/main/images/macos/macos-14-Readme.md)
- [GitHub Ubuntu runner image inventory](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md)

The macOS job runs the same owner-test regex and installed-package consumer target as Linux/Windows. The Android job builds the exported libraries and installs the package for arm64; it cannot run host executables produced for Android.

## Verification matrix

| Boundary | Required evidence |
|---|---|
| bounded admission | 0/1/exact/full, invalid capacity, shutdown rejection, no allocation after init, rejection counters |
| timer handoff | full executor backpressure, shutdown during handoff, deadline/FIFO preservation, no loss or duplicate |
| parallel reduce | empty/one/chunk boundary, noncommutative associative reducer order, callback failure, partial submission failure, repeated evaluation |
| certificate | valid Direct/Plan/Kernel witness, stale graph, changed callable, changed type, changed opcode, missing property, unsupported relation |
| Lean | `lake test` plus no `sorry`/`admit`/`axiom` in production theorem modules |
| host | Linux/Windows/macOS native tests and package consumer; Android arm64 configure/build/install |

## Rollback

Each phase is independently reversible:

- G-1 can restore default initialization and remove checked admission while retaining existing bool/id calls.
- G-2 is behind an explicit execution mode; removing it leaves sequential Plan unchanged.
- G-3 certificates are additive and do not change Plan execution unless explicitly requested.
- G-4 is workflow-only and can be reverted without changing produced libraries.
