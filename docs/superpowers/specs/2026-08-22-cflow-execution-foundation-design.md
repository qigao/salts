# CFlow Execution Foundation: Platform, Concurrency, Clock, Executor, and Timer

**Date:** 2026-08-22

## Status

Implemented architecture for the execution substrate beneath CFlow and the corresponding Core refactor; completion evidence is tracked below. This design intentionally stops before Event, Mailbox, Machine, Actor, IO reactor, or minicoro integration. Those layers consume the contracts defined here.

## Architectural decision

Adopt this dependency contract:

```text
Platform
  ↓
Concurrency
  ↓
CFlow
  ↓
Core

CMeta ─────→ CFlow
CMeta ─────→ TurboSTL
Core  ─────→ CMeta / CFlow / TurboSTL / Platform / Concurrency
```

The precise rule is:

```text
Platform
  → OS APIs / Threads only

Concurrency
  → Platform

CMeta
  → no Platform/Concurrency dependency unless a concrete need appears

CFlow
  → CMeta
  → Platform
  → Concurrency

TurboSTL
  → CMeta

Core
  → CMeta
  → TurboSTL
  → Platform
  → Concurrency
  → may depend on CFlow without a cycle
```

Core is therefore an upper-level common runtime/utilities module, not the owner of every low-level primitive.

## Why this refactor is needed

CFlow already contains the important protocol of a generic resumable runtime: `cflow_resumable`, `CFLOW_STEP_WAIT`, `cflow_waitable`, `cflow_waker`, a deterministic test scheduler, and a concurrent worker scheduler.

The current implementation nevertheless mixes four responsibilities:

- reading time;
- deciding when delayed work is ready;
- deciding where ready work executes;
- implementing OS thread/mutex/condition mechanics.

Two concrete problems follow.

First, `cflow/src/scheduler_worker.c` owns a second worker pool based on C11 `<threads.h>` and computes deadlines with `timespec_get(..., TIME_UTC)`. Wall-clock time can jump. Timeout, delay, retry, debounce, scheduling, and future Machine temporal transitions must use monotonic time.

Second, Core currently owns lower-level execution primitives needed by CFlow: `utils/turbo_thread.*` implements thread/sync plus a disruptor-backed thread pool, while `utils/disruptor.*` is itself a generic concurrency primitive. Making CFlow depend on Core would invert the dependency graph and prevent Core from using CFlow later.

The fix is to move those primitives below Core.

## Goals

1. Make Core structurally able to use CFlow and CMeta without cycles.
2. Make `TurboUtils::Platform` own OS clock/thread/synchronization primitives.
3. Make `TurboUtils::Concurrency` own disruptor and the existing thread pool.
4. Remove direct `<threads.h>`, pthread, and Win32 thread use from CFlow execution code.
5. Give CFlow one explicit monotonic-time model for deadlines and elapsed time.
6. Preserve deterministic virtual-time scheduling.
7. Separate **when work becomes ready** from **where work executes**.
8. Preserve `cflow_scheduler` as a compatibility facade during migration.
9. Make serialized execution a first-class policy for future Machine/Actor use.
10. Give each module its own target-scoped visibility/export contract.

## Non-goals

This phase does not implement Event/Mailbox, Machine/Statechart IR, Actor runtime, minicoro integration, IOCP/epoll/kqueue reactor integration, affinity/NUMA/work-stealing policy, or a new disruptor/thread-pool algorithm.

---

## Module ownership

### `TurboUtils::Platform`

Platform is the lowest TurboUtils runtime module and owns portable OS primitives only:

- monotonic high-resolution clock;
- realtime timestamp clock;
- thread create/join/detach;
- mutex;
- read/write lock;
- condition variable;
- once initialization;
- thread-local storage abstraction;
- thread yield/sleep;
- CPU-count query.

Platform does **not** own thread pools, task queues, disruptor, CMeta interfaces, CFlow scheduling, logging, filesystem/process utilities, or application-level policy.

`turbo_sync_set_single_threaded()` / `turbo_sync_is_single_threaded()` are explicitly **not** Platform primitives. They are a global policy over higher-level shared resources and remain Core-owned until a later policy refactor.

Platform must not depend on CMeta, CFlow, TurboSTL, Core, SDS, logging, disruptor, or Concurrency.

### `TurboUtils::Concurrency`

Concurrency owns generic concurrent execution/data structures independent of CFlow semantics:

- disruptor;
- the existing disruptor-backed `turbo_threadpool`;
- task queue/worker management required by that pool.

Concurrency depends only on Platform plus the C standard library/atomics.

It does not know about `cmeta_callable`, CFlow Graph/Stream/Machine semantics, CFlow waitables, logging, or Core utilities.

### `TurboUtils::CFlow`

CFlow owns execution semantics:

- Graph/Stream/runtime semantics;
- resumable/waitable/waker protocol;
- Clock abstraction;
- Executor abstraction;
- TimerQueue/deadline semantics;
- scheduler facade and policy composition.

CFlow may adapt the Concurrency thread pool as WorkerExecutor, but Concurrency never depends on CFlow.

### `TurboUtils::Core`

Core remains the upper-level utilities/runtime module: fmt/tlog, filesystem/process/mmap, strings/buffers, UUID/compression/regex/automata, system information, and other application-facing utilities.

Core may use CFlow internally. CFlow is a `PRIVATE` Core dependency unless a Core public header actually exposes CFlow types.

Core remains `PUBLIC` on CMeta where Core public headers expose CMeta-generated metadata.

---

## Physical layout and compatibility

New focused ownership:

```text
platform/
  CMakeLists.txt
  include/turbo/
    platform.h
    clock.h
    thread.h
  src/
    clock.c
    thread.c

concurrency/
  CMakeLists.txt
  include/turbo/
    concurrency.h
    disruptor.h
    thread_pool.h
  src/
    disruptor.c
    thread_pool.c
```

Existing includes remain compatibility surfaces during migration:

```text
utils/include/platform.h
utils/include/turbo_thread.h
utils/include/disruptor.h
```

`utils/include/turbo_thread.h` becomes a compatibility aggregate over `<turbo/thread.h>` and `<turbo/thread_pool.h>`, plus the Core-owned global synchronization-policy declarations.

`utils/include/disruptor.h` becomes a compatibility include of `<turbo/disruptor.h>`.

`utils/include/platform.h` becomes a compatibility facade: focused clock primitives come from Platform while genuinely Core-owned calendar/system-info/native-timer APIs may remain there initially.

Core's own linkage marker moves to a focused `utils/include/turbo_api.h`; new Platform/Concurrency headers do not inherit Core's export state.

---

## Visibility contract

Each module owns its producer/consumer visibility state.

Conceptual markers:

```text
TURBO_PLATFORM_API / TURBO_PLATFORM_C_API
TURBO_CONCURRENCY_API / TURBO_CONCURRENCY_C_API
TURBO_API / TURBO_C_API                    // Core only
```

On Windows, CMake owns shared-library producer/consumer state per target. Public headers do not inspect CMake `*_EXPORTS`, `BUILD_SHARED`, or `USE_SHARED` implementation macros.

If Platform/Concurrency start as static libraries, their API markers may be empty on Windows. The module-local contract still exists for a future shared build.

Public Platform/Concurrency headers must not define `_POSIX_C_SOURCE`, `_DEFAULT_SOURCE`, `_XOPEN_SOURCE`, or `_DARWIN_C_SOURCE`; feature-test state belongs to implementation files or target-private definitions.

---

## Time model

CFlow distinguishes wall time from control time:

```text
Realtime timestamp
  → logging / telemetry / persistence / display

Monotonic instant
  → timeout / delay / retry / debounce / scheduling / temporal transitions
```

CFlow control behavior uses monotonic time only.

### Platform clock backend

- Windows: `QueryPerformanceCounter` / `QueryPerformanceFrequency`.
- POSIX: `clock_gettime(CLOCK_MONOTONIC, ...)`.
- Realtime remains a separate API.

Existing `turbo_hrtime()`, `turbo_monotonic_ms()`, `turbo_realtime_ms()`, and `turbo_uptime_ms()` become Platform-owned compatibility behavior.

### CFlow semantic time types

```c
typedef struct cflow_duration { uint64_t ns; } cflow_duration;
typedef struct cflow_instant  { uint64_t ns; } cflow_instant;
typedef struct cflow_deadline { uint64_t ns; } cflow_deadline;
```

Required operations include ns/us/ms/s duration constructors, saturating instant-plus-duration, comparisons, remaining-duration calculation, and explicit legacy millisecond conversion.

### CFlow Clock

Two first-class implementations:

- SystemClock: Platform monotonic time;
- VirtualClock: deterministic logical time controlled by tests/replay.

Manual advancement is a VirtualClock capability; system time never advances artificially.

---

## Thread/timed-wait semantics

Platform thread primitives preserve current external behavior except for one semantic correction: relative condition-variable timeout means **elapsed monotonic duration**.

The current POSIX implementation derives an absolute deadline from `CLOCK_REALTIME`; that must be replaced with a monotonic condition-variable clock where supported. Windows already provides relative timeout behavior.

CFlow never sees pthread/Win32 types.

---

## Concurrency thread pool

The existing disruptor-backed thread pool is moved, not rewritten. Queue capacity, MPMC submission, shutdown rejection, pending/wait behavior, and statistics are regression contracts.

Canonical API becomes `<turbo/thread_pool.h>`; legacy `"turbo_thread.h"` remains an aggregate during migration.

---

## Executor model

Executor answers only:

> Where and under what serialization policy does a ready task execute?

It does not own time.

Initial implementations:

- ManualExecutor: no background thread; explicitly driven;
- SerialExecutor: multi-producer, at most one active callback;
- WorkerExecutor: adapter over `TurboUtils::Concurrency` thread pool;
- optional InlineExecutor for narrow integrations where reentrancy is accepted.

Future Machine/Actor mutation uses SerialExecutor by default.

---

## TimerQueue model

TimerQueue answers:

> When does a task become ready?

It stores monotonic deadlines plus stable insertion order.

Rules:

- earlier deadlines first;
- equal deadline FIFO;
- arithmetic saturates;
- cancellation succeeds only while pending;
- cancellation does not pretend to stop already-running user code.

Worker scheduling may use one Platform coordinator thread/condition wait around the earliest deadline; CFlow does not need one native OS timer per delayed task.

The existing Core `turbo_timer_*` remains an application-facing native-timer API for now and is not the CFlow TimerQueue abstraction.

---

## Scheduler compatibility facade

The existing public `cflow_scheduler` stays intact.

```text
Test scheduler
  = VirtualClock + ManualExecutor + TimerQueue

Worker scheduler
  = SystemClock + WorkerExecutor + TimerQueue
```

Legacy `post_after(delay_ticks,...)` treats ticks as milliseconds, converts to typed duration, computes a saturating monotonic deadline, and schedules into TimerQueue.

`now()` returns monotonic milliseconds through the legacy facade.

`advance()` is meaningful only for the virtual-clock backend.

`run_one/run_ready/run_until_idle` drive ManualExecutor/TimerQueue deterministically.

---

## Core dependency direction

The architectural rule is:

```text
CFlow never depends on Core.
Core may depend on CFlow.
```

Core links CFlow privately while only implementation files use it. A public dependency is added only if Core public headers expose CFlow declarations.

This gives tlog/process/driver-style Core facilities a future path to use CFlow internally without forcing CFlow upward into Core.

---

## Future Event/Machine/thread semantics

Future Event producers may run on arbitrary threads but do not mutate Machine state directly:

```text
producer A ─┐
producer B ─┼─> Mailbox ─> SerialExecutor ─> Machine
producer C ─┘
```

Independent CPU/IO work can run on WorkerExecutor; completion returns as an Event.

Machine state is serialized by construction rather than mutexing every transition.

---

## Relationship to resumable/minicoro

```text
Resumable
  VALUE / DONE / ERROR / WAIT
              ↓
            Waker
              ↓
Scheduler facade
              ↓
    Clock + Timer + Executor
              ↓
         Concurrency
              ↓
           Platform
```

Machine and minicoro adapters consume this stack. minicoro remains an optional backend/adaptor, not the public CFlow model.

---

## Build-system contract

CMake configure order may place TinyTest before Platform/Concurrency so their test subdirectories can create TinyTest-linked test targets. Configure order is not the production dependency graph.

Recommended top-level order:

```cmake
add_subdirectory(tools)
include(FindTools)
add_subdirectory(vendor)
add_subdirectory(tinytest)
add_subdirectory(platform)
add_subdirectory(concurrency)
add_subdirectory(cmeta)
add_subdirectory(cflow)
add_subdirectory(turbostl)
add_subdirectory(utils)
add_subdirectory(turbo_serial)
```

Production dependencies:

```text
TurboUtils::Platform
  → Threads/OS only

TurboUtils::Concurrency
  → TurboUtils::Platform

TurboUtils::CFlow
  PUBLIC  TurboUtils::CMeta
  PRIVATE TurboUtils::Platform
          TurboUtils::Concurrency

TurboUtils::Core
  PUBLIC  TurboUtils::CMeta
          TurboUtils::Platform / Concurrency while compatibility headers expose them
  PRIVATE TurboUtils::STL
          TurboUtils::CFlow when implementation uses it
          existing third-party libraries
```

CFlow no longer links `Threads::Threads` directly after migration; Platform owns that dependency.

All new targets participate in `TurboUtilsTargets` install/export.

---

## Migration phases

1. Split Core `TURBO_API` from the old broad `platform.h`.
2. Introduce Platform and move clock primitives.
3. Move thread/mutex/cond/rwlock/once/yield/sleep/CPU-count to Platform; keep global sync policy Core-owned.
4. Introduce Concurrency and move disruptor.
5. Move the existing thread pool into Concurrency.
6. Add CFlow typed time + SystemClock/VirtualClock.
7. Add Manual/Serial/Worker Executor and internal TimerQueue.
8. Rebuild both scheduler backends from the new components.
9. Finish Core ownership/link/export cleanup.
10. Run package/install and cross-platform verification.

Downstream only after this foundation is stable: Event + Mailbox, Machine IR, Event transitions, Machine->resumable, timer Events, minicoro->resumable, Actor/hierarchical Machine/temporal Stream operators.

---

## Testing strategy

### Platform

- monotonic observations do not decrease;
- realtime and monotonic APIs remain distinct;
- thread create/join/detach preserved;
- mutex/cond/rwlock/once preserved;
- timed condition waits obey elapsed monotonic duration;
- strict C11/C++17 public-header compilation;
- module visibility does not reuse Core export state.

### Concurrency

Retain existing disruptor worker-wait, broadcast, worker-pool and ring behavior tests. Retain existing thread-pool MPMC submission, queue capacity, shutdown rejection, pending/wait and stats tests. These tests link Concurrency directly rather than Core.

### CFlow

- typed duration conversion/saturation;
- VirtualClock exact advancement without real sleeping;
- TimerQueue ordering/FIFO/cancellation;
- Manual/Serial/Worker executor semantics;
- legacy scheduler milliseconds;
- worker scheduler monotonic deadlines;
- existing runtime/close/wake concurrency behavior.

### Integration

Core-only compatibility consumers include legacy `platform.h`, `turbo_thread.h`, and `disruptor.h` and link only `TurboUtils::Core`, relying on declared transitive dependencies rather than manual link additions.

Linux/Windows/macOS native builds exercise the module boundaries and installed
package exports. PR #58 branch head
`7396925e2b0a6bb592c2122ff9c321a2b5489f3a` passed conformance run
[32653089799](https://github.com/qigao/turbo-utils/actions/runs/32653089799).
The same run verified Android arm64 cross-build/install/package exports; it did
not execute Android binaries.

---

## Acceptance criteria

| Criterion | Status | Evidence |
|---|---|---|
| 1. Platform exists without upper-layer dependencies. | Implemented + Linux/Windows CI verified | `platform/CMakeLists.txt` links only `Threads::Threads`; the Platform tests link `TurboUtils::Platform` directly. |
| 2. Concurrency depends only on Platform plus standard/OS facilities. | Implemented + Linux/Windows CI verified | `concurrency/CMakeLists.txt` publishes only `TurboUtils::Platform`; `concurrency/src/` owns the OS-independent pool/disruptor implementation. |
| 3. Disruptor and thread-pool implementations are no longer Core-owned. | Implemented + Linux/Windows CI verified | Canonical implementations are `concurrency/src/disruptor.c` and `concurrency/src/thread_pool.c`; Core retains compatibility includes only. |
| 4. Global single-threaded policy remains outside Platform. | Implemented + Linux/Windows CI verified | `turbo_sync_set_single_threaded()` and `turbo_sync_is_single_threaded()` remain in `utils/src/turbo_thread.c` and are exercised by `utils/tests/test_execution_compat.c`. |
| 5. CFlow has no direct native thread-library dependency. | Implemented + Linux/Windows CI verified | `cflow/src/scheduler_worker.c` uses `<turbo/thread.h>`; `cflow/CMakeLists.txt` links Platform/Concurrency rather than `Threads::Threads`. |
| 6. CFlow delayed work uses monotonic time. | Implemented + Linux/Windows CI verified | `cflow/src/clock.c` obtains system control time from `turbo_hrtime()`; TimerQueue and scheduler tests exercise typed deadlines and relative waits. |
| 7. WorkerExecutor reuses Concurrency. | Implemented + Linux/Windows CI verified | `cflow/src/executor.c` adapts `turbo_threadpool_*`; no second CFlow worker-pool implementation remains. |
| 8. VirtualClock is deterministic. | Implemented + Linux/Windows CI verified | `cflow/tests/cflow_execution_test.c` verifies exact manual advancement and rejects manual advancement for SystemClock. |
| 9. Legacy public includes remain source-compatible. | Implemented + Linux/Windows CI verified | `utils/include/platform.h`, `utils/include/turbo_thread.h`, and `utils/include/disruptor.h` remain compatibility surfaces; `test_execution_compat` links only Core. |
| 10. Platform/Concurrency export state is target-scoped. | Implemented + Linux/Windows CI verified | Module-local export headers and target definitions live under `platform/include/turbo/platform.h`, `concurrency/include/turbo/concurrency.h`, and their CMake targets. |
| 11. Core can depend on CFlow without a cycle. | Implemented + Linux/Windows CI verified | `utils/CMakeLists.txt` links CFlow privately; CFlow links CMeta publicly and Platform/Concurrency privately, never Core. |
| 12. Owner behavior tests pass. | Implemented + Linux/Windows CI verified | `platform_thread_test`, `platform_clock_test`, `disruptor_test`, `thread_pool_test`, CFlow tests, and `test_execution_compat` are registered under their owning modules. |
| 13. Installed exports are consumable. | Implemented + Linux/Windows CI verified | `verify_installed_package` installs the SDK and builds external consumers naming only Platform, Concurrency, CMeta, CFlow, STL, or Core. |
| 14. Built-in execution admission is bounded and observable. | Implemented + Linux/macOS/Windows CI verified; Android cross-build verified | Execution Model v2 adds explicit capacities, checked full/closed results, shutdown operations, statistics, and lossless timer handoff tests. PR #58 run 32653089799 passed native owner tests and Android package compilation. |

### Host evidence

| Host | Status | Evidence boundary |
|---|---|---|
| Windows Release | Implemented + CI verified | [PR #58 Windows job](https://github.com/qigao/turbo-utils/actions/runs/32653089799/job/97227595405) passed Release configure/build/test at the final code head. |
| Linux Release | Implemented + CI verified | [PR #58 Linux job](https://github.com/qigao/turbo-utils/actions/runs/32653089799/job/97227595385) passed owner tests, Lean/C certificate checks, and installed-package consumers at the final code head. |
| macOS 15 Release | Implemented + native CI verified | [PR #58 macOS job](https://github.com/qigao/turbo-utils/actions/runs/32653089799/job/97227597826) passed native Release build, owner tests, and installed-package consumers. |
| Android arm64-v8a Release | Cross-build/package CI verified | [PR #58 Android job](https://github.com/qigao/turbo-utils/actions/runs/32653089799/job/97227595331) built and installed `android-24`/`arm64-v8a`, verified exported headers/CMake targets, and uploaded evidence. Android runtime was not tested. |

## Residual work outside this completion patch

- Event, Mailbox, Machine, Actor, reactor, and minicoro remain explicit design non-goals rather than missing foundation features.
- Android device/emulator runtime remains a separate evidence gap; current Android verification stops at cross-build, install, and package exports.
- Ordered parallel reduction and the executable Lean/C refinement certificate are complete in Execution Model v2; shared-runner throughput remains measurement evidence rather than a universal performance guarantee.
- ManualExecutor, WorkerExecutor, and TimerQueue now use explicit bounded admission. The migration, default capacities, byte-budget formulas, compatibility behavior, shutdown terminal states, and metrics are specified in `2026-08-23-cflow-execution-model-v2-design.md`.

## Architectural consequence

The project gains a real execution stack:

```text
what runs      → Task / Resumable
when it runs   → Clock + TimerQueue
where it runs  → Executor
how it runs    → Concurrency
on what OS     → Platform
```

and a clean semantic stack:

```text
Event / Machine / Actor / Stream / minicoro adapter
                    ↓
            CFlow Resumable
                    ↓
       Clock + Timer + Executor
                    ↓
             Concurrency
                    ↓
               Platform
```

> **OS mechanics belong to Platform; generic concurrent execution belongs to Concurrency; execution semantics belong to CFlow; higher-level utilities and policy belong to Core. Core may use CFlow, but CFlow never reaches upward into Core.**
