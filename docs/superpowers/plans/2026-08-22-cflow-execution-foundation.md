# CFlow Execution Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Historical execution record:** The implementation described here landed through the Platform, Concurrency, Clock, Executor, TimerQueue, scheduler, and Core-compatibility commit series. The unchecked boxes preserve the original step-by-step script; current completion evidence is maintained in `2026-08-23-cflow-execution-foundation-completion.md` and the design status table.

**Goal:** Refactor TurboUtils into a real `Platform -> Concurrency -> CFlow -> Core` execution stack, remove CFlow's duplicate worker-pool/wall-clock scheduling implementation, and preserve existing Core/CFlow public behavior during migration.

**Architecture:** `TurboUtils::Platform` owns OS clock/thread/synchronization primitives. `TurboUtils::Concurrency` owns disruptor and the existing disruptor-backed thread pool. CFlow builds typed Clock/Executor/TimerQueue semantics on those modules while retaining `cflow_scheduler` as a compatibility facade. Core moves above these modules and may depend on CFlow/CMeta without creating a cycle.

**Tech Stack:** C11, C++17 public-header compatibility, CMake 3.20+, CMeta `CMETA_INTERFACE`, TinyTest, Win32 threads/SRW locks/condition variables, POSIX pthread/`CLOCK_MONOTONIC`, existing Turbo disruptor/thread pool.

**Spec:** `docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md`

## Global Constraints

- CFlow must never link `TurboUtils::Core`; Core may link CFlow.
- `TurboUtils::Platform` must not depend on CMeta, CFlow, TurboSTL, Core, SDS, logging, disruptor, Concurrency, or Core policy state.
- `TurboUtils::Concurrency` depends only on Platform plus standard/OS facilities.
- `turbo_sync_set_single_threaded()` / `turbo_sync_is_single_threaded()` remain Core-owned policy; do not move them into Platform.
- CFlow deadlines/timeouts/delays use monotonic time only; realtime is never a control-flow deadline source.
- Existing `cflow_scheduler` legacy delay ticks remain milliseconds.
- Platform/Concurrency export state is target-scoped and does not reuse Core `TURBO_API` producer/consumer state.
- Public headers must not define `_POSIX_C_SOURCE`, `_DEFAULT_SOURCE`, `_XOPEN_SOURCE`, or `_DARWIN_C_SOURCE`.
- Existing legacy includes `platform.h`, `turbo_thread.h`, and `disruptor.h` remain source-compatible during the migration window.
- Do not rewrite disruptor or thread-pool algorithms; move ownership and adapt interfaces.
- Strict C11 and C++17 public-header compilation must remain valid on Linux/Windows/macOS/Android-supported builds.
- Each task follows RED -> GREEN -> focused regression -> commit. Do not suppress diagnostics with `-Wno-*`.

---

## File Structure

### New Platform module

- `platform/CMakeLists.txt` — `TurboUtils::Platform` target, install/export, private OS feature macros.
- `platform/include/turbo/platform.h` — Platform-local API/export markers only.
- `platform/include/turbo/clock.h` — monotonic/realtime clock primitives and conversion helpers.
- `platform/include/turbo/thread.h` — thread, mutex, rwlock, condition variable, once, TLS, yield/sleep, CPU count.
- `platform/src/clock.c` — Win32/POSIX clock backends.
- `platform/src/thread.c` — Win32/POSIX thread/synchronization backends.
- `platform/tests/CMakeLists.txt` — focused Platform tests.
- `platform/tests/platform_clock_test.c` — monotonic/realtime contract.
- `platform/tests/platform_thread_test.c` — thread/sync/timed-wait contract.
- `platform/tests/platform_header_cpp_test.cpp` — C++17 public-header compile contract.

### New Concurrency module

- `concurrency/CMakeLists.txt` — `TurboUtils::Concurrency` target and install/export.
- `concurrency/include/turbo/concurrency.h` — module-local API/export markers.
- `concurrency/include/turbo/disruptor.h` — canonical disruptor public API.
- `concurrency/include/turbo/thread_pool.h` — canonical thread-pool public API.
- `concurrency/src/disruptor.c` — moved existing disruptor implementation.
- `concurrency/src/thread_pool.c` — extracted existing thread-pool implementation.
- `concurrency/tests/CMakeLists.txt` — moved concurrency regression tests.
- `concurrency/tests/disruptor_test.c` — moved `utils/tests/test_disruptor.c` behavior.
- `concurrency/tests/thread_pool_test.c` — moved `utils/tests/test_threadpool.c` behavior.

### Core compatibility and policy

- `utils/include/turbo_api.h` — Core-only `TURBO_API` / `TURBO_C_API` contract.
- `utils/include/platform.h` — compatibility facade; Core system-info/calendar/native-timer API plus focused Platform includes.
- `utils/include/turbo_thread.h` — compatibility aggregate over `<turbo/thread.h>` and `<turbo/thread_pool.h>`, plus Core global synchronization-policy declarations.
- `utils/include/disruptor.h` — compatibility include of `<turbo/disruptor.h>`.
- `utils/src/turbo_sync_policy.c` — Core-owned `turbo_sync_set_single_threaded()` / `turbo_sync_is_single_threaded()` state.
- `utils/src/platform.c` — retains Core-owned system-info/calendar/native-timer logic after clock extraction.
- `utils/CMakeLists.txt` — Core dependencies updated to Platform/Concurrency/CFlow as required.

### CFlow execution foundation

- `cflow/include/cflow/time.h` — typed duration/instant/deadline values.
- `cflow/include/cflow/clock.h` — CMeta Clock interface, SystemClock and VirtualClock constructors.
- `cflow/include/cflow/executor.h` — CMeta Executor interface and Manual/Serial/Worker constructors.
- `cflow/src/clock.c` — Clock implementations.
- `cflow/src/executor.c` — Manual executor and Concurrency-backed worker/serial adapters.
- `cflow/src/timer_queue.h` — internal TimerQueue API.
- `cflow/src/timer_queue.c` — monotonic ordered delayed-task queue.
- `cflow/src/scheduler.c` — deterministic scheduler facade rebuilt from VirtualClock + ManualExecutor + TimerQueue.
- `cflow/src/scheduler_worker.c` — worker scheduler facade rebuilt from SystemClock + WorkerExecutor + TimerQueue + Platform wait primitives.
- `cflow/tests/cflow_execution_test.c` — time/clock/executor/timer focused tests.
- Existing `cflow/tests/cflow_runtime_test.c` — compatibility/concurrency regression.

---

### Task 1: Separate Core API linkage from `platform.h`

**Files:**
- Create: `utils/include/turbo_api.h`
- Modify: `utils/include/platform.h`
- Modify: `utils/tests/test_platform.c`
- Modify: `utils/tests/test_platform_build_state_contract.c`

**Interfaces:**
- Produces: Core-only `TURBO_API` and `TURBO_C_API` definitions with exactly the current target-provided semantics.
- Consumes: current Core CMake definitions of `TURBO_API` on Windows.

- [ ] **Step 1: Write the failing include-contract test**

Add a direct Core API-header include to `utils/tests/test_platform_build_state_contract.c` before `platform.h`:

```c
#include "turbo_api.h"
#include "platform.h"
#include "tinytest.h"

#ifndef TURBO_API
#error "turbo_api.h must expose the Core target API marker"
#endif
#ifndef TURBO_C_API
#error "turbo_api.h must expose C linkage composition"
#endif
```

Keep the existing checks that `TURBO_BUILD_SHARED`, `TURBO_USE_SHARED`, and `turbo_utils_EXPORTS` are absent.

- [ ] **Step 2: Run the focused build to verify RED**

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user --target test_platform_build_state_contract
```

Expected: compile failure because `utils/include/turbo_api.h` does not exist.

- [ ] **Step 3: Create `utils/include/turbo_api.h`**

Move only the Core API linkage block out of `platform.h`:

```c
#ifndef TURBO_API_H
#define TURBO_API_H

#ifndef TURBO_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define TURBO_API __attribute__((visibility("default")))
  #else
    #define TURBO_API
  #endif
#endif

#ifndef TURBO_C_API
  #ifdef __cplusplus
    #define TURBO_C_API extern "C" TURBO_API
  #else
    #define TURBO_C_API TURBO_API
  #endif
#endif

#endif
```

Replace the corresponding block in `utils/include/platform.h` with:

```c
#include "turbo_api.h"
```

Do not move OS/time/thread declarations in this task.

- [ ] **Step 4: Run focused and header tests**

```bash
cmake --build --preset linux-release-user --target test_platform test_platform_build_state_contract test_platform_header_cpp
ctest --preset linux-release-user -R "^(test_platform|test_platform_build_state_contract|test_platform_header_cpp)$" --output-on-failure
```

Expected: all selected targets build and tests pass.

- [ ] **Step 5: Commit**

```bash
git add utils/include/turbo_api.h utils/include/platform.h utils/tests/test_platform.c utils/tests/test_platform_build_state_contract.c
git commit -m "refactor(core): isolate API linkage contract"
```

---

### Task 2: Introduce `TurboUtils::Platform` and extract clock ownership

**Files:**
- Create: `platform/CMakeLists.txt`
- Create: `platform/include/turbo/platform.h`
- Create: `platform/include/turbo/clock.h`
- Create: `platform/src/clock.c`
- Create: `platform/tests/CMakeLists.txt`
- Create: `platform/tests/platform_clock_test.c`
- Create: `platform/tests/platform_header_cpp_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `utils/include/platform.h`
- Modify: `utils/src/platform.c`
- Modify: `utils/CMakeLists.txt`

**Interfaces:**
- Produces: `TurboUtils::Platform`, `turbo_hrtime()`, `turbo_monotonic_ms()`, `turbo_realtime_ms()`, `turbo_uptime_ms()`, `turbo_ns_to_ms()`, `turbo_ms_to_ns()` from `<turbo/clock.h>`.
- Produces: `TURBO_PLATFORM_API` / `TURBO_PLATFORM_C_API` from `<turbo/platform.h>`.
- Preserves: legacy inclusion of those APIs through `utils/include/platform.h`.

- [ ] **Step 1: Add Platform clock tests before the target exists**

Create `platform/tests/platform_clock_test.c`:

```c
#include <turbo/clock.h>
#include "tinytest.h"

spec("Platform clock") {
  it("keeps monotonic time nondecreasing") {
    uint64_t first = turbo_hrtime();
    uint64_t second = turbo_hrtime();
    check(second >= first);
    check(turbo_monotonic_ms() > 0);
  }

  it("keeps conversion helpers deterministic") {
    check_equal(turbo_ns_to_ms(1999999ULL), 1ULL);
    check_equal(turbo_ms_to_ns(7ULL), 7000000ULL);
  }

  it("keeps realtime separate from monotonic time") {
    check(turbo_realtime_ms() > 0);
  }
}
```

Create `platform/tests/platform_header_cpp_test.cpp`:

```cpp
#include <turbo/clock.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(turbo_hrtime()), uint64_t>);
int main() { return turbo_hrtime() > 0 ? 0 : 1; }
```

- [ ] **Step 2: Configure to verify RED**

Add `add_subdirectory(platform)` after `tinytest` in top-level CMake and configure:

```bash
cmake --preset linux-release-user
```

Expected: failure until `platform/CMakeLists.txt` and headers exist.

- [ ] **Step 3: Create the Platform target and module-local API header**

`platform/CMakeLists.txt` starts with:

```cmake
set(TARGET_NAME turbo_platform)
add_library(${TARGET_NAME} STATIC src/clock.c)
cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::Platform
  FOLDER "platform"
  EXPORT_NAME Platform)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)
if(APPLE)
  target_compile_definitions(${TARGET_NAME} PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(${TARGET_NAME} PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()
install(TARGETS ${TARGET_NAME} EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/turbo DESTINATION include FILES_MATCHING PATTERN "*.h")
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

`platform/include/turbo/platform.h` defines only Platform-local linkage markers. On Windows they default to empty for the initial static target; no Core marker is reused.

- [ ] **Step 4: Move clock declarations and implementation**

Create `platform/include/turbo/clock.h` with:

```c
#ifndef TURBO_CLOCK_H
#define TURBO_CLOCK_H
#include <turbo/platform.h>
#include <stdint.h>

TURBO_PLATFORM_C_API uint64_t turbo_hrtime(void);
TURBO_PLATFORM_C_API uint64_t turbo_monotonic_ms(void);
TURBO_PLATFORM_C_API uint64_t turbo_realtime_ms(void);
TURBO_PLATFORM_C_API uint64_t turbo_uptime_ms(void);

static inline uint64_t turbo_ns_to_ms(uint64_t ns) { return ns / 1000000ULL; }
static inline uint64_t turbo_ms_to_ns(uint64_t ms) { return ms * 1000000ULL; }
#endif
```

Move the existing Win32 QPC and POSIX `CLOCK_MONOTONIC` implementations from `utils/src/platform.c` into `platform/src/clock.c`. Preserve behavior; do not redesign realtime/calendar code here.

Update `utils/include/platform.h` to include `<turbo/clock.h>` and remove duplicate clock declarations/helpers. Update Core:

```cmake
target_link_libraries(${TARGET_NAME}
  PUBLIC TurboUtils::CMeta TurboUtils::Platform
  PRIVATE ...)
```

- [ ] **Step 5: Build and test GREEN**

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user --target platform_clock_test platform_header_cpp_test test_platform
ctest --preset linux-release-user -R "^(platform_clock_test|platform_header_cpp_test|test_platform)$" --output-on-failure
```

Expected: all pass; no duplicate clock symbols in Core link.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt platform utils/include/platform.h utils/src/platform.c utils/CMakeLists.txt
git commit -m "refactor(platform): extract clock module"
```

---

### Task 3: Extract thread and synchronization primitives into Platform

**Files:**
- Create: `platform/include/turbo/thread.h`
- Create: `platform/src/thread.c`
- Create: `platform/tests/platform_thread_test.c`
- Modify: `platform/CMakeLists.txt`
- Modify: `utils/include/turbo_thread.h`
- Modify: `utils/src/turbo_thread.c`
- Modify: `utils/tests/test_threadpool.c` only as needed for includes during the transition

**Interfaces:**
- Produces: `turbo_mutex_t`, `turbo_cond_t`, `turbo_rwlock_t`, `turbo_thread_t`, `turbo_once_t`, `turbo_thread_*`, `turbo_mutex_*`, `turbo_cond_*`, `turbo_rwlock_*`, `turbo_once`, `turbo_sleep_ms`, `turbo_thread_yield`, `turbo_cpu_count` from `<turbo/thread.h>`.
- Preserves: legacy `"turbo_thread.h"` include; thread-pool and global sync-policy declarations remain in the compatibility header until Task 5.
- Explicitly does not move: `turbo_sync_set_single_threaded()` or `turbo_sync_is_single_threaded()`.

- [ ] **Step 1: Add primitive/timed-wait tests**

Create `platform/tests/platform_thread_test.c`:

```c
#include <turbo/clock.h>
#include <turbo/thread.h>
#include "tinytest.h"
#include <errno.h>

static void set_flag(void *arg) { *(int *)arg = 1; }

spec("Platform thread primitives") {
  it("creates and joins a thread") {
    turbo_thread_t thread;
    int flag = 0;
    check_equal(turbo_thread_create(&thread, set_flag, &flag), 0);
    check_equal(turbo_thread_join(&thread), 0);
    check_equal(flag, 1);
  }

  it("times condition waits using elapsed duration") {
    turbo_mutex_t mutex;
    turbo_cond_t cond;
    turbo_mutex_init(&mutex);
    turbo_cond_init(&cond);
    turbo_mutex_lock(&mutex);
    uint64_t before = turbo_hrtime();
    int rc = turbo_cond_timedwait(&cond, &mutex, 20ULL * 1000000ULL);
    uint64_t elapsed = turbo_hrtime() - before;
    turbo_mutex_unlock(&mutex);
    check_equal(rc, -ETIMEDOUT);
    check(elapsed >= 10ULL * 1000000ULL);
    check(elapsed < 1000ULL * 1000000ULL);
    turbo_cond_destroy(&cond);
    turbo_mutex_destroy(&mutex);
  }
}
```

- [ ] **Step 2: Verify RED**

```bash
cmake --build --preset linux-release-user --target platform_thread_test
```

Expected: failure because `<turbo/thread.h>` does not exist.

- [ ] **Step 3: Move primitive declarations and implementations**

Move only OS primitive declarations from `utils/include/turbo_thread.h` into `platform/include/turbo/thread.h`. Move Win32/POSIX mutex/cond/rwlock/once/thread/sleep/yield/CPU-count implementations into `platform/src/thread.c`.

Leave thread-pool declarations and the two `turbo_sync_*` policy declarations in `utils/include/turbo_thread.h` for now.

Leave the thread-pool implementation and `g_single_threaded` policy implementation in `utils/src/turbo_thread.c` until Task 5.

- [ ] **Step 4: Make POSIX timed waits monotonic**

Implement POSIX `turbo_cond_t` with a private wrapper configured using:

```c
pthread_condattr_t attr;
pthread_condattr_init(&attr);
pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
pthread_cond_init(&cond->native, &attr);
pthread_condattr_destroy(&attr);
```

Construct timed-wait absolute deadlines from `CLOCK_MONOTONIC`. Keep Windows relative `SleepConditionVariableSRW` behavior.

- [ ] **Step 5: Build focused tests and the still-Core-owned pool test**

```bash
cmake --build --preset linux-release-user --target platform_thread_test test_threadpool
ctest --preset linux-release-user -R "^(platform_thread_test|test_threadpool)$" --output-on-failure
```

Expected: primitive behavior and existing pool behavior both pass.

- [ ] **Step 6: Commit**

```bash
git add platform utils/include/turbo_thread.h utils/src/turbo_thread.c utils/tests/test_threadpool.c
git commit -m "refactor(platform): extract thread primitives"
```

---

### Task 4: Move disruptor into `TurboUtils::Concurrency`

**Files:**
- Create: `concurrency/CMakeLists.txt`
- Create: `concurrency/include/turbo/concurrency.h`
- Create: `concurrency/include/turbo/disruptor.h`
- Create: `concurrency/src/disruptor.c`
- Create: `concurrency/tests/CMakeLists.txt`
- Create: `concurrency/tests/disruptor_test.c`
- Modify: `CMakeLists.txt`
- Replace: `utils/include/disruptor.h` with compatibility wrapper
- Delete: `utils/src/disruptor.c`
- Delete: `utils/tests/test_disruptor.c`
- Modify: `utils/CMakeLists.txt`

**Interfaces:**
- Produces: `TurboUtils::Concurrency` and canonical `<turbo/disruptor.h>` API.
- Consumes: `<turbo/thread.h>` from Platform.
- Preserves: legacy `"disruptor.h"` include and Core-link consumer behavior through transitive Concurrency linkage.

- [ ] **Step 1: Copy the disruptor regression test under the new owner**

Create `concurrency/tests/disruptor_test.c` from the existing test and change only includes:

```c
#include <turbo/disruptor.h>
#include <turbo/thread.h>
#include "tinytest.h"
```

Keep worker-wait, ring-capacity, broadcast, and worker-pool assertions unchanged.

- [ ] **Step 2: Verify RED at configure/build**

Add `add_subdirectory(concurrency)` after Platform in top-level CMake, then:

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user --target disruptor_test
```

Expected: failure until Concurrency target/header exists.

- [ ] **Step 3: Create Concurrency target and move disruptor implementation**

`concurrency/CMakeLists.txt`:

```cmake
set(TARGET_NAME turbo_concurrency)
add_library(${TARGET_NAME} STATIC src/disruptor.c)
cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::Concurrency
  FOLDER "concurrency"
  EXPORT_NAME Concurrency)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::Platform)
```

Move implementation without algorithm changes; replace includes with:

```c
#include <turbo/disruptor.h>
#include <turbo/thread.h>
```

Replace `utils/include/disruptor.h` with:

```c
#ifndef TURBO_DISRUPTOR_COMPAT_H
#define TURBO_DISRUPTOR_COMPAT_H
#include <turbo/disruptor.h>
#endif
```

Make Core link Concurrency `PUBLIC` while legacy Core-installed headers expose the compatibility API.

- [ ] **Step 4: Run focused regression**

```bash
cmake --build --preset linux-release-user --target disruptor_test test_threadpool
ctest --preset linux-release-user -R "^(disruptor_test|test_threadpool)$" --output-on-failure
```

Expected: unchanged disruptor behavior and existing pool behavior.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt concurrency utils/include/disruptor.h utils/CMakeLists.txt
git rm utils/src/disruptor.c utils/tests/test_disruptor.c
git commit -m "refactor(concurrency): extract disruptor module"
```

---

### Task 5: Move the existing thread pool into Concurrency and keep Core policy in Core

**Files:**
- Create: `concurrency/include/turbo/thread_pool.h`
- Create: `concurrency/src/thread_pool.c`
- Create: `concurrency/tests/thread_pool_test.c`
- Create: `utils/src/turbo_sync_policy.c`
- Modify: `concurrency/CMakeLists.txt`
- Modify: `utils/include/turbo_thread.h`
- Delete: `utils/src/turbo_thread.c`
- Delete: `utils/tests/test_threadpool.c`

**Interfaces:**
- Produces: existing `turbo_threadpool_*` API from `<turbo/thread_pool.h>`.
- Consumes: `<turbo/thread.h>` and `<turbo/disruptor.h>`.
- Preserves: queue capacity, MPMC submitters, shutdown rejection, pending/wait/stats semantics.
- Preserves in Core: `turbo_sync_set_single_threaded()` / `turbo_sync_is_single_threaded()` and `g_single_threaded` policy state.

- [ ] **Step 1: Move the thread-pool test to the new owner**

Create `concurrency/tests/thread_pool_test.c` from `utils/tests/test_threadpool.c` and use:

```c
#include <turbo/thread.h>
#include <turbo/thread_pool.h>
#include "tinytest.h"
```

Do not weaken or remove the multi-producer, queue-capacity, shutdown, pending, or stats tests.

- [ ] **Step 2: Create the canonical thread-pool header**

Move these types and all existing `turbo_threadpool_*` declarations into `<turbo/thread_pool.h>` using `TURBO_CONCURRENCY_C_API`:

```c
typedef struct turbo_threadpool_s turbo_threadpool_t;
typedef void (*turbo_task_fn)(void *arg);

typedef struct {
  int num_threads;
  size_t queue_capacity;
} turbo_threadpool_config_t;

typedef struct {
  int num_threads;
  size_t queue_capacity;
  int accepting;
  int64_t submitted_tasks;
  int64_t started_tasks;
  int64_t completed_tasks;
  int64_t rejected_tasks;
  int64_t queued_tasks;
  int64_t active_tasks;
  int64_t pending_tasks;
} turbo_threadpool_stats_t;
```

- [ ] **Step 3: Extract pool implementation and Core policy separately**

Move the existing Thread Pool section into `concurrency/src/thread_pool.c` with:

```c
#include <turbo/disruptor.h>
#include <turbo/thread.h>
#include <turbo/thread_pool.h>
```

Create `utils/src/turbo_sync_policy.c`:

```c
#include "turbo_thread.h"

static int g_single_threaded = 0;

void turbo_sync_set_single_threaded(int enabled) {
  g_single_threaded = enabled;
}

int turbo_sync_is_single_threaded(void) {
  return g_single_threaded;
}
```

`utils/include/turbo_thread.h` becomes:

```c
#ifndef TURBO_THREAD_COMPAT_H
#define TURBO_THREAD_COMPAT_H
#include "turbo_api.h"
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

TURBO_C_API void turbo_sync_set_single_threaded(int enabled);
TURBO_C_API int turbo_sync_is_single_threaded(void);
#endif
```

Delete `utils/src/turbo_thread.c` only after all primitive, pool, and policy code has a new owner.

- [ ] **Step 4: Run migrated and Core-link regression**

```bash
cmake --build --preset linux-release-user --target thread_pool_test turbo_utils
ctest --preset linux-release-user -R "^thread_pool_test$" --output-on-failure
```

Expected: new owner test passes; Core links without duplicate/missing thread, pool, disruptor, or sync-policy symbols.

- [ ] **Step 5: Commit**

```bash
git add concurrency utils/include/turbo_thread.h utils/src/turbo_sync_policy.c
git rm utils/src/turbo_thread.c utils/tests/test_threadpool.c
git commit -m "refactor(concurrency): move thread pool below Core"
```

---

### Task 6: Add typed CFlow time and Clock interfaces

**Files:**
- Create: `cflow/include/cflow/time.h`
- Create: `cflow/include/cflow/clock.h`
- Create: `cflow/src/clock.c`
- Create: `cflow/tests/cflow_execution_test.c`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `cflow_duration`, `cflow_instant`, `cflow_deadline`.
- Produces: `cflow_clock`, `cflow_clock_system_init`, `cflow_clock_virtual_init`.
- Produces: clock capability `CMETA_CLOCK_CAP_MANUAL` and generic `cflow_clock_advance()` returning false for SystemClock.
- Consumes: `turbo_hrtime()` from Platform.

- [ ] **Step 1: Write failing typed-time tests**

Add `cflow_execution_test.c`:

```c
#include <cflow/clock.h>
#include <cflow/time.h>
#include "tinytest.h"

spec("CFlow execution time") {
  it("saturates deadline arithmetic") {
    cflow_instant now = { UINT64_MAX - 5u };
    cflow_duration delay = cflow_duration_from_ns(10u);
    cflow_deadline deadline = cflow_deadline_after(now, delay);
    check_equal(deadline.ns, UINT64_MAX);
  }

  it("advances virtual time exactly") {
    cflow_clock clock = {0};
    check_true(cflow_clock_virtual_init(&clock, (cflow_instant){100u}));
    check_equal(cflow_clock_now(&clock).ns, 100u);
    check_true(cflow_clock_advance(&clock, cflow_duration_from_ns(25u)));
    check_equal(cflow_clock_now(&clock).ns, 125u);
    cflow_clock_destroy(&clock);
  }
}
```

- [ ] **Step 2: Verify RED**

```bash
cmake --build --preset linux-release-user --target cflow_execution_test
```

Expected: missing CFlow time/clock headers/types.

- [ ] **Step 3: Implement `cflow/time.h`**

Use strong wrappers:

```c
typedef struct { uint64_t ns; } cflow_duration;
typedef struct { uint64_t ns; } cflow_instant;
typedef struct { uint64_t ns; } cflow_deadline;
```

Provide inline `cflow_duration_from_ns/us/ms/s`, `cflow_deadline_after`, `cflow_instant_to_ms`, and `cflow_deadline_remaining`. All multiplication/addition saturates at `UINT64_MAX`.

- [ ] **Step 4: Implement CMeta Clock interface**

```c
enum { CMETA_CLOCK_CAP_MANUAL = 1u << 0 };

#define CMETA_CLOCK_METHODS(X,I) \
  X(I,R0,cflow_instant,now,_) \
  X(I,R1,bool,advance,cflow_duration,delta) \
  X(I,V0,void,destroy,_)
CMETA_INTERFACE(cflow_clock, CMETA_CLOCK_METHODS);

bool cflow_clock_system_init(cflow_clock *clock);
bool cflow_clock_virtual_init(cflow_clock *clock, cflow_instant start);
```

System `now()` returns `{ turbo_hrtime() }`; System `advance()` returns false. VirtualClock stores logical ns and saturates advancement.

- [ ] **Step 5: Run focused tests**

```bash
cmake --build --preset linux-release-user --target cflow_execution_test cflow_header_cpp_test
ctest --preset linux-release-user -R "^(cflow_execution_test|cflow_header_cpp_test)$" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add cflow/include/cflow/time.h cflow/include/cflow/clock.h cflow/src/clock.c cflow/tests cflow/CMakeLists.txt
git commit -m "feat(cflow): add typed clock semantics"
```

---

### Task 7: Add Executor and internal TimerQueue

**Files:**
- Create: `cflow/include/cflow/executor.h`
- Create: `cflow/src/executor.c`
- Create: `cflow/src/timer_queue.h`
- Create: `cflow/src/timer_queue.c`
- Modify: `cflow/include/cflow/scheduler.h`
- Modify: `cflow/tests/cflow_execution_test.c`
- Modify: `cflow/CMakeLists.txt`

**Interfaces:**
- Produces: `cflow_executor_manual_init`, `cflow_executor_serial_init`, `cflow_executor_worker_init`.
- Produces internal TimerQueue APIs used by schedulers.
- Consumes: `turbo_threadpool_*` from Concurrency.

- [ ] **Step 1: Add failing Manual/Serial/Worker executor tests**

Use atomics to prove SerialExecutor has at most one active callback:

```c
static atomic_int active_callbacks;
static atomic_int max_active;

static void serial_probe(void *user) {
  (void)user;
  int active = atomic_fetch_add(&active_callbacks, 1) + 1;
  int seen = atomic_load(&max_active);
  while (active > seen &&
         !atomic_compare_exchange_weak(&max_active, &seen, active)) {}
  turbo_sleep_ms(1);
  atomic_fetch_sub(&active_callbacks, 1);
}
```

Submit multiple tasks from multiple producer threads and assert `max_active == 1` for SerialExecutor. ManualExecutor test asserts no callback before `run_one()`.

- [ ] **Step 2: Define Executor interface**

Move `cflow_task_fn` to `executor.h` and make `scheduler.h` include it:

```c
typedef void (*cflow_task_fn)(void *user);

#define CMETA_EXECUTOR_METHODS(X,I) \
  X(I,R2,bool,post,cflow_task_fn,fn,void *,user) \
  X(I,R0,bool,run_one,_) \
  X(I,R0,size_t,run_ready,_) \
  X(I,R0,bool,wait_idle,_) \
  X(I,R0,size_t,pending,_) \
  X(I,V0,void,destroy,_)
CMETA_INTERFACE(cflow_executor, CMETA_EXECUTOR_METHODS);
```

ManualExecutor owns a simple FIFO. SerialExecutor wraps `turbo_threadpool_create(1)`. WorkerExecutor wraps `turbo_threadpool_create(workers)`.

- [ ] **Step 3: Define TimerQueue internal API**

```c
typedef uint64_t cflow_timer_id;

typedef struct cflow_timer_task {
  cflow_timer_id id;
  cflow_deadline deadline;
  uint64_t order;
  cflow_task_fn fn;
  void *user;
  bool cancelled;
} cflow_timer_task;

typedef struct cflow_timer_queue {
  cflow_timer_task *items;
  size_t count;
  size_t capacity;
  cflow_timer_id next_id;
  uint64_t next_order;
} cflow_timer_queue;

bool cflow_timer_queue_init(cflow_timer_queue *q);
void cflow_timer_queue_destroy(cflow_timer_queue *q);
cflow_timer_id cflow_timer_queue_schedule(cflow_timer_queue *q,
    cflow_deadline deadline, cflow_task_fn fn, void *user);
bool cflow_timer_queue_cancel(cflow_timer_queue *q, cflow_timer_id id);
bool cflow_timer_queue_next_deadline(const cflow_timer_queue *q, cflow_deadline *out);
bool cflow_timer_queue_take_ready(cflow_timer_queue *q,
    cflow_instant now, cflow_timer_task *out);
size_t cflow_timer_queue_pending(const cflow_timer_queue *q);
```

`take_ready` selects smallest deadline then smallest insertion order and removes it, allowing worker scheduler locks to be released before posting to Executor.

- [ ] **Step 4: Add TimerQueue ordering/cancellation tests**

Schedule A@20, B@10, C@10 and assert ready order `B, C, A`. Cancel C before readiness and assert `B, A`.

- [ ] **Step 5: Run focused tests**

```bash
cmake --build --preset linux-release-user --target cflow_execution_test
ctest --preset linux-release-user -R "^cflow_execution_test$" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add cflow/include/cflow/executor.h cflow/include/cflow/scheduler.h cflow/src/executor.c cflow/src/timer_queue.* cflow/tests/cflow_execution_test.c cflow/CMakeLists.txt
git commit -m "feat(cflow): add executor and timer queue"
```

---

### Task 8: Rebuild both scheduler backends from the new components

**Files:**
- Modify: `cflow/src/scheduler.c`
- Modify: `cflow/src/scheduler_worker.c`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/cflow_execution_test.c`
- Modify: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Preserves: existing `cflow_scheduler` public interface and capability bits.
- Test backend: VirtualClock + ManualExecutor + TimerQueue.
- Worker backend: SystemClock + WorkerExecutor + TimerQueue + Platform mutex/cond/thread coordinator.
- Removes: direct `<threads.h>` and `TIME_UTC` usage from CFlow.

- [ ] **Step 1: Add public-facade compatibility tests**

```c
it("keeps legacy scheduler ticks in milliseconds") {
  cflow_scheduler scheduler = {0};
  check_true(cflow_scheduler_test_init(&scheduler));
  check_equal(cflow_scheduler_now(&scheduler), 0u);
  check_equal(cflow_scheduler_advance(&scheduler, 25u), (size_t)0u);
  check_equal(cflow_scheduler_now(&scheduler), 25u);
  cflow_scheduler_destroy(&scheduler);
}
```

Add an equal-deadline FIFO test through the public scheduler rather than only TimerQueue internals.

- [ ] **Step 2: Rebuild deterministic scheduler**

Replace private test-loop time/task state with:

```c
cflow_clock clock;
cflow_executor executor;
cflow_timer_queue timers;
```

Map `post_after(delay_ms)` through `cflow_duration_from_ms`, `cflow_clock_now`, and `cflow_deadline_after`. `advance(ms)` advances VirtualClock then drains ready timers into ManualExecutor.

- [ ] **Step 3: Rebuild worker scheduler without `<threads.h>`**

```c
typedef struct worker_state {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  turbo_thread_t timer_thread;
  cflow_clock clock;
  cflow_executor executor;
  cflow_timer_queue timers;
  bool stopping;
} worker_state;
```

Coordinator loop:

```text
lock
  if no timer -> cond_wait
  else if deadline <= now -> take_ready
  else -> timedwait(remaining monotonic ns)
unlock
if task was ready -> cflow_executor_post(...)
repeat
```

Signal `changed` when a new earlier timer is inserted, cancelled, or shutdown begins. Never hold `worker_state.mutex` while calling `cflow_executor_post()` or user code.

- [ ] **Step 4: Remove direct thread-library dependency**

```cmake
target_link_libraries(${TARGET_NAME}
  PUBLIC TurboUtils::CMeta
  PRIVATE TurboUtils::Platform TurboUtils::Concurrency)
```

Remove `Threads::Threads` from CFlow. Replace `<threads.h>` usage in CFlow source/tests with `<turbo/thread.h>` where explicit producer threads are still required.

- [ ] **Step 5: Run full CFlow regression**

```bash
cmake --build --preset linux-release-user --target cflow_execution_test cflow_runtime_test cflow_graph_test cflow_pipeline_test cflow_header_cpp_test
ctest --preset linux-release-user -R "^(cflow_)" --output-on-failure
```

Expected: all CFlow tests pass, including existing close/wake concurrency behavior.

- [ ] **Step 6: Commit**

```bash
git add cflow
git commit -m "refactor(cflow): compose scheduler execution foundation"
```

---

### Task 9: Finish Core ownership cleanup and package dependency graph

**Files:**
- Modify: `utils/CMakeLists.txt`
- Modify: `utils/include/platform.h`
- Modify: `utils/include/turbo_thread.h`
- Modify: `utils/include/disruptor.h`
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboUtilsConfig.cmake.in` only if exported dependency discovery requires it
- Create: `utils/tests/test_execution_compat.c`

**Interfaces:**
- Core may link `TurboUtils::CFlow` privately.
- Core publishes Platform/Concurrency transitively while legacy Core public headers aggregate those APIs.
- CFlow never links Core.

- [ ] **Step 1: Make the target/configure graph explicit**

Use:

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

TinyTest appears before Platform/Concurrency only so their test subdirectories can create TinyTest-linked test targets. Production Platform/Concurrency targets do not link TinyTest.

- [ ] **Step 2: Finalize Core dependency visibility**

```cmake
target_link_libraries(
  turbo_utils
  PUBLIC TurboUtils::CMeta
         TurboUtils::Platform
         TurboUtils::Concurrency
  PRIVATE TurboUtils::STL
          TurboUtils::CFlow
          ${FMT_LEXER_LIBRARY}
          TurboUtils::SDS
          aklomp::base64
          zstd::libzstd
          ${LOG_PATTERN_LEXER_LIBRARY})
```

Keep CFlow `PRIVATE` because no Core public header exposes CFlow types yet.

- [ ] **Step 3: Remove public feature-test definitions from Core**

Change `_DARWIN_C_SOURCE`, `_POSIX_C_SOURCE`, `_DEFAULT_SOURCE`, and `_XOPEN_SOURCE` target definitions from `PUBLIC` to `PRIVATE`. Add required definitions to the owning implementation targets privately.

- [ ] **Step 4: Add behavior/link compatibility test**

Create `utils/tests/test_execution_compat.c`:

```c
#include "platform.h"
#include "turbo_thread.h"
#include "disruptor.h"

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (pool == NULL) return 1;
  turbo_threadpool_destroy(pool);
  turbo_sync_set_single_threaded(1);
  if (!turbo_sync_is_single_threaded()) return 2;
  turbo_sync_set_single_threaded(0);
  return turbo_hrtime() == 0 ? 3 : 0;
}
```

Register it linking `TurboUtils::Core` only. This proves transitive compile/link behavior; it is not a source-spelling test.

- [ ] **Step 5: Build full repository**

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: build succeeds and all registered tests pass.

- [ ] **Step 6: Verify installed target graph with external consumers**

Install to a temporary prefix. Configure one tiny external CMake consumer linking only `TurboUtils::Core` and another linking only `TurboUtils::CFlow`. Neither consumer manually adds Platform/Concurrency.

Expected target graph:

```text
Core -> CMeta + Platform + Concurrency (+ private CFlow/STL)
CFlow -> CMeta (+ private Platform/Concurrency)
Concurrency -> Platform
Platform -> Threads/OS
```

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt cmake utils
git commit -m "refactor(core): move execution substrate below Core"
```

---

### Task 10: Cross-platform CI and final verification

**Files:**
- Modify only when required by existing workflow assumptions: `.github/workflows/*.yml`, CMake presets, module-specific test CMakeLists.
- Do not duplicate preset compiler flags in workflow YAML.

**Interfaces:**
- Validates the complete module visibility/dependency contract.

- [ ] **Step 1: Run Linux strict build from a clean build directory**

```bash
cmake --preset linux-release-user --fresh
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: zero build errors and zero test failures.

- [ ] **Step 2: Build module targets independently**

```bash
cmake --build --preset linux-release-user --target turbo_platform turbo_concurrency turbo_cflow turbo_utils
```

Expected: each resolves only declared lower-level dependencies; no duplicate symbol failures.

- [ ] **Step 3: Verify boundaries through compile/link evidence**

Do not add grep-based source-style tests. Ensure:

- Platform tests link `TurboUtils::Platform` only (+ TinyTest as test dependency).
- Concurrency tests link `TurboUtils::Concurrency` only (+ TinyTest).
- CFlow tests link `TurboUtils::CFlow` only (+ TinyTest).
- Core compatibility tests link `TurboUtils::Core` only (+ TinyTest if using TinyTest).

Missing transitive dependencies therefore fail naturally at compile/link time.

- [ ] **Step 4: Push and inspect Linux/Windows/macOS/Android CI**

Use existing presets/workflows. For any failure, inspect the first real configure/build/test error and fix the owning module; do not suppress diagnostics or hand-copy preset flags into workflow YAML.

- [ ] **Step 5: Final diff review**

Verify:

- no CFlow -> Core dependency;
- no `TIME_UTC` deadline source in CFlow worker scheduling;
- no `<threads.h>` in CFlow scheduler/runtime;
- no disruptor/thread-pool implementation remains under Core;
- `turbo_sync_*` policy remains Core-owned;
- legacy public include wrappers remain;
- Platform/Concurrency visibility state is independent of Core.

- [ ] **Step 6: Final commit/squash only after fresh CI evidence**

If history contains mechanical migration commits, squash only after the final tree has passed clean build/test/CI checks, then rerun CI for the new head before claiming completion.
